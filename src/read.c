/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * read.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "bignum.h"
#include "bitset.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "keyword.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "read.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

IDIO idio_read_module;
IDIO idio_read_reader_sym;
IDIO idio_lexobj_type;
IDIO idio_src_properties;

#define IDIO_CHAR_NUL		'\0'

#define IDIO_CHAR_SPACE		' '
#define IDIO_CHAR_TAB		'\t'
#define IDIO_CHAR_NL		'\n'
#define IDIO_CHAR_CR		'\r'

#define IDIO_CHAR_LPAREN	'('
#define IDIO_CHAR_RPAREN	')'
#define IDIO_CHAR_LBRACE	'{'
#define IDIO_CHAR_RBRACE	'}'
#define IDIO_CHAR_LBRACKET	'['
#define IDIO_CHAR_RBRACKET	']'
#define IDIO_CHAR_LANGLE	'<'
#define IDIO_CHAR_RANGLE	'>'
#define IDIO_CHAR_SQUOTE	'\''
#define IDIO_CHAR_COMMA		','
#define IDIO_CHAR_BACKQUOTE	'`'
#define IDIO_CHAR_DOT		'.'
#define IDIO_CHAR_SLASH		'/'
#define IDIO_CHAR_COLON		':'
#define IDIO_CHAR_SEMICOLON	';'
#define IDIO_CHAR_DQUOTE	'"'
#define IDIO_CHAR_HASH		'#'
#define IDIO_CHAR_AT		'@'
#define IDIO_CHAR_BACKSLASH	'\\'
#define IDIO_CHAR_AMPERSAND	'&'
#define IDIO_CHAR_PERCENT	'%'
#define IDIO_CHAR_DOLLARS	'$'
#define IDIO_CHAR_EXCLAMATION	'!'
#define IDIO_CHAR_EQUALS	'='
#define IDIO_CHAR_PIPE		'|'
#define IDIO_CHAR_ASTERISK	'*'
#define IDIO_CHAR_HYPHEN_MINUS	'-'
#define IDIO_CHAR_PLUS_SIGN	'+'
#define IDIO_CHAR_PERIOD	'.'

#define IDIO_UNICODE_REPLACEMENT_CHARACTER 0xFFFD

/*
 * What separates words from one another in Idio?
 *
 * Whitespace - SPACE TAB NL CR
 *
 * start and end of lists LPAREN RPAREN
 *
 *	A(B)C	=>	A ( B ) C
 *
 * which allow words to abutt parentheses.
 *
 * end of arrays RBRACKET
 *
 *	(start is handled by #[ -- HASH LBRACKET )
 *
 *	#[A]B	=>	#[ A ] B
 *
 * also LBRACKET to avoid words like a[b
 *
 * similarly LBRACE and RBRACE to avoid a{b and c}d
 *
 * value indexing - DOT
 *
 *	A.B	=>	A . B
 *	A.B.C	=>	A . B . C
 *
 * result qualifying - SEMICOLON
 *
 *	A;B	=>	A ; B
 *	A;B;C	=>	A ; B ; C
 *
 * Quoted objects - SQUOTE BACKQUOTE COMMA
 *  (these should be deprecated in favour of interpolation character array)
 *
 * Strings - DQUOTE (start and end)
 *
 *	A"B"C	=>	A "B" C
 */
#define IDIO_SEPARATOR(c)	(IDIO_CHAR_SPACE == (c) ||		\
				 IDIO_CHAR_TAB == (c) ||		\
				 IDIO_CHAR_NL == (c) ||			\
				 IDIO_CHAR_CR == (c) ||			\
				 IDIO_CHAR_LPAREN == (c) ||		\
				 IDIO_CHAR_RPAREN == (c) ||		\
				 IDIO_CHAR_LBRACKET == (c) ||		\
				 IDIO_CHAR_RBRACKET == (c) ||		\
				 IDIO_CHAR_LBRACE == (c) ||		\
				 IDIO_CHAR_RBRACE == (c) ||		\
				 IDIO_CHAR_DOT == (c) ||		\
				 IDIO_CHAR_SEMICOLON == (c) ||		\
				 IDIO_CHAR_DQUOTE == (c))

#define IDIO_OPEN_DELIMITER(c)	(IDIO_CHAR_LPAREN == (c) ||		\
				 IDIO_CHAR_LBRACE == (c) ||		\
				 IDIO_CHAR_LBRACKET == (c))

/*
 * IDIO_LIST_X are counters (up to 0xffff) for depths of various kinds
 * of nestable things
 */
#define IDIO_LIST_PAREN_MARK		(1 << 16) /* 0x01hhhh */
#define IDIO_LIST_BRACE_MARK		(1 << 17) /* 0x02hhhh */
#define IDIO_LIST_BRACKET_MARK		(1 << 18) /* 0x04hhhh */
#define IDIO_LIST_ANGLE_MARK		(1 << 19) /* 0x08hhhh */

/* QUOTE and QUASIQUOTE are on or off */
#define IDIO_LIST_QUOTE_MARK		(1 << 28) /* 0x1000hhhh */
#define IDIO_LIST_QUASIQUOTE_MARK	(1 << 29) /* 0x2000hhhh */

#define IDIO_LIST_STRING_MARK		(1 << 30) /* 0x4000hhhh */
#define IDIO_LIST_CONSTANT_MARK		(1 << 31) /* 0x8000hhhh */

#define IDIO_LIST_MASK			(0xffff | IDIO_LIST_QUOTE_MARK | IDIO_LIST_QUASIQUOTE_MARK)

#define IDIO_LIST_PAREN(d)		(IDIO_LIST_PAREN_MARK | (d))
#define IDIO_LIST_BRACE(d)		(IDIO_LIST_BRACE_MARK | (d))
#define IDIO_LIST_BRACKET(d)		(IDIO_LIST_BRACKET_MARK | (d))
#define IDIO_LIST_ANGLE(d)		(IDIO_LIST_ANGLE_MARK | (d))

#define IDIO_LIST_QUOTE(d)		(IDIO_LIST_QUOTE_MARK | (d))
#define IDIO_LIST_QUASIQUOTE(d)		(IDIO_LIST_QUASIQUOTE_MARK | (d))
#define IDIO_LIST_STRING(d)		(IDIO_LIST_STRING_MARK | (d))
#define IDIO_LIST_CONSTANT(d)		(IDIO_LIST_CONSTANT_MARK | (d))

#define IDIO_LIST_PAREN_P(d)		((d) & IDIO_LIST_PAREN_MARK)
#define IDIO_LIST_BRACE_P(d)		((d) & IDIO_LIST_BRACE_MARK)
#define IDIO_LIST_BRACKET_P(d)		((d) & IDIO_LIST_BRACKET_MARK)
#define IDIO_LIST_ANGLE_P(d)		((d) & IDIO_LIST_ANGLE_MARK)

#define IDIO_LIST_QUOTE_P(d)		((d) & IDIO_LIST_QUOTE_MARK)
#define IDIO_LIST_QUASIQUOTE_P(d)	((d) & IDIO_LIST_QUASIQUOTE_MARK)
#define IDIO_LIST_STRING_P(d)		((d) & IDIO_LIST_STRING_MARK)
#define IDIO_LIST_CONSTANT_P(d)		((d) & IDIO_LIST_CONSTANT_MARK)

/*
 * Default template interpolation characters:
 *
 * IDIO_CHAR_DOT == use default (ie. skip) => IDIO_CHAR_DOT cannot be
 * one of the interpolation characters
 *
 * 1. expression substitution == unquote
 * 2. expression splicing == unquotesplicing
 * 3. expression quoting
 * 4. escape char to prevent operator handling
 */
idio_unicode_t idio_default_template_ic[] = { IDIO_CHAR_DOLLARS, IDIO_CHAR_AT, IDIO_CHAR_SQUOTE, IDIO_CHAR_BACKSLASH };
#define IDIO_TEMPLATE_IC 4

/*
 * Default string interpolation characters:
 *
 * IDIO_CHAR_DOT == use default (ie. skip) => IDIO_CHAR_DOT cannot be
 * one of the interpolation characters
 *
 * 1. expression substitution == unquote
 * 4. escape char
 */
idio_unicode_t idio_default_string_ic[] = { IDIO_CHAR_NUL, IDIO_CHAR_BACKSLASH };
#define IDIO_STRING_IC 2
idio_unicode_t idio_default_istring_ic[] = { IDIO_CHAR_DOLLARS, IDIO_CHAR_BACKSLASH };
#define IDIO_ISTRING_IC 2

/*
 * In the case of named characters, eg. #\newline (as opposed to #\a,
 * the character 'a') what is the longest name (eg, "newline") we
 * should look out for.  Otherwise we'll read in
 * #\supercalifragilisticexpialidocious which is fine except that I
 * don't know what character that is.

 * That said, there's no reason why we shouldn't be able to use
 * Unicode named characters.  What's the longest one of those?  In
 * Unicode 13.0.0 it appears to be 88 for the LEFT/RIGHT variants of
 * "BOX DRAWINGS LIGHT DIAGONAL UPPER CENTRE TO MIDDLE RIGHT AND
 * MIDDLE LEFT TO LOWER CENTRE"
 *
 * In the meanwhile, we only have a handler for "space" and
 * "newline"...
 */
#define IDIO_CHARACTER_MAX_NAME_LEN	100
#define IDIO_CHARACTER_SPACE		"space"
#define IDIO_CHARACTER_NEWLINE		"newline"

/*
 * SRFI-36: all parse errors are decendents of ^read-error
 */
static void idio_read_error (IDIO handle, IDIO lo, IDIO c_location, IDIO msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (string, msg);

    /*
     * How do we describe our error?
     *
     * We have two things useful to the user:
     *
     *  * ``msg`` describing the error.
     *
     *  * ``lo`` where the lexical object began.
     *
     * and a couple of things more useful to the developer:
     *
     *  * ``handle`` which tells us where we've gotten to -- which
     * could be the end of the file for an unterminated string.
     *
     *  * ``c_location`` which is an IDIO_C_LOCATION() and probably
     * only useful for debug -- the user won't care where in the C
     * code the error in their user code was spotted.
     */

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    idio_display_C (": reached line ", dsh);
    idio_display (idio_integer (IDIO_HANDLE_LINE (handle)), dsh);
    idio_display_C (": pos ", dsh);
    idio_display (idio_integer (IDIO_HANDLE_POS (handle)), dsh);

    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_read_error_type,
				   IDIO_LIST5 (msg,
					       idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME),
					       detail,
					       idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE),
					       idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_POS)));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_read_error_parse (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_parse_args (IDIO handle, IDIO lo, IDIO c_location, char const *msg, IDIO args)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);
    idio_display (args, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_parse_printf (IDIO handle, IDIO lo, IDIO c_location, char const *fmt, ...)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (fmt);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    va_list fmt_args;
    va_start (fmt_args, fmt);
    IDIO msg = idio_error_string (fmt, fmt_args);
    va_end (fmt_args);

    idio_read_error (handle, lo, c_location, msg);

    /* notreached */
}

static void idio_read_error_parse_word_too_long (IDIO handle, IDIO lo, IDIO c_location, char const *word)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (word);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("word is too long: '", msh);
    idio_display_C (word, msh);
    idio_display_C ("'", msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_list_eof (IDIO handle, IDIO lo, IDIO c_location)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("EOF in list", msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_pair_separator (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_comment (IDIO handle, IDIO lo, IDIO c_location, char const *e_msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (e_msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("comment: ", msh);
    idio_display_C (e_msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_string (IDIO handle, IDIO lo, IDIO c_location, char const *e_msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (e_msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("string: ", msh);
    idio_display_C (e_msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_named_character (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("named character: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_named_character_unknown_name (IDIO handle, IDIO lo, IDIO c_location, char const *name)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (name);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("unknown named character: ", msh);
    idio_display_C (name, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_bitset (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("bitset: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_template (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("template: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_pathname (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("pathname: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_integer (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("integer: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_bignum (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("bignum: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_utf8_decode (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("UTF-8 decode: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static void idio_read_error_unicode_decode (IDIO handle, IDIO lo, IDIO c_location, char const *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("U+hhhh decode: ", msh);
    idio_display_C (msg, msh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (msh));

    /* notreached */
}

static IDIO idio_read_1_expr (IDIO handle, idio_unicode_t *ic, int depth);
static IDIO idio_read_block (IDIO handle, IDIO lo, IDIO closedel, idio_unicode_t *ic, int depth);
static IDIO idio_read_number_C (IDIO handle, char const *str);
static uintmax_t idio_read_uintmax_radix (IDIO handle, IDIO lo, char basec, int radix, int lim);
static IDIO idio_read_bignum_radix (IDIO handle, IDIO lo, char basec, int radix);
static IDIO idio_read_unescape (IDIO ls);
static IDIO idio_read_named_character (IDIO handle, IDIO lo);

IDIO idio_read_lexobj_from_handle (IDIO handle)
{
    IDIO_ASSERT (handle);

    IDIO_TYPE_ASSERT (handle, handle);

    return idio_struct_instance (idio_lexobj_type,
				 idio_pair (IDIO_HANDLE_FILENAME (handle),
				 idio_pair (idio_integer (IDIO_HANDLE_LINE (handle)),
				 idio_pair (idio_integer (IDIO_HANDLE_POS (handle)),
				 idio_pair (idio_S_unspec,
				 idio_S_nil)))));
}

IDIO idio_read_unicode (IDIO handle, IDIO lo)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    idio_unicode_t c = idio_getc_handle (handle);

    if (idio_eofp_handle (handle)) {
	/*
	 * Test Case: read-errors/unicode-eof.idio
	 *
	 * #U
	 */
	idio_read_error_unicode_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	return idio_S_notreached;
    }

    IDIO r;

    if ('+' != c) {
	/*
	 * Test Case: read-errors/unicode-not-plus.idio
	 *
	 * #U-
	 */
	idio_read_error_unicode_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "U not followed by +");

	return idio_S_notreached;
    }

    uint32_t cp;
    IDIO I_cp = idio_read_bignum_radix (handle, lo, 'x', 16);

    if (idio_isa_fixnum (I_cp)) {
	cp = (uint32_t) IDIO_FIXNUM_VAL (I_cp);
    } else if (idio_isa_bignum (I_cp)) {
	cp = idio_bignum_uint64_t_value (I_cp);
    } else {
	/*
	 * Test Case: read-errors/??
	 *
	 * with digits limited to 0-9A-F I don't think we can get
	 * a non-integer
	 */
	idio_error_param_type ("unicode cp should be an integer", I_cp, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * cp is unsigned to negative code points are now too big
     */
    if (idio_unicode_valid_code_point (cp) == 0) {
	/*
	 * Test Case: read-errors/unicode-too-big.idio
	 *
	 * #U+110000
	 */
	idio_read_error_unicode_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "code point too big");

	return idio_S_notreached;
    }

    r = IDIO_UNICODE (cp);

    return r;
}

/*
 * idio_getc_handle() will come here as well as idio_read_character(),
 * below
 *
 * We can raise a conditon for EOF but not for badly formed UTF-8
 * sequences.  Return the replacement character for those.
 */
idio_unicode_t idio_read_character_int (IDIO handle, IDIO lo, int kind)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    if (idio_S_nil == lo) {
	/* called from outside read.c */
	lo = idio_read_lexobj_from_handle (handle);

    } else {
	IDIO_TYPE_ASSERT (struct_instance, lo);
    }

    idio_unicode_t codepoint;
    idio_unicode_t state = 0;

    int i;
    for (i = 0; ; i++) {
	uint8_t uc = idio_getb_handle (handle);

	idio_utf8_decode (&state, &codepoint, uc);
	if (IDIO_UTF8_ACCEPT == state) {
	    break;
	} else if (IDIO_UTF8_REJECT == state) {
	    /*
	     * First up, check if we've hit EOF
	     */
	    if (idio_eofp_handle (handle)) {
		if (IDIO_READ_CHARACTER_SIMPLE == kind) {
		    return EOF;
		} else {
		    /*
		     * Test Case: read-errors/character-eof.idio
		     *
		     * #\
		     */
		    idio_read_error_utf8_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

		    /* notreached */
		    return EOF;
		}
	    }

	    /*
	     * Test Case: read-errors/character-invalid-utf8.idio
	     *
	     * #\x
	     *
	     * where x is a literal byte which is not a UTF-8 prefix
	     * of some sort (as you'll get EOF, above, instead when
	     * this goes round the loop).  0xFE and 0xFF cannot appear
	     * in a valid UTF-8 sequence -- remember UTF-8 != Unicode.
	     *
	     * The test uses a literal 0xFE byte which, depending on
	     * the mood/whim of your editor, may appear as LATIN SMALL
	     * LETTER THORN from C1 Controls and Latin-1 Supplement.
	     * Emacs and vi seem to guess at ISO 8859-1 whereas less
	     * displays <FE>.
	     *
	     * Also be leery of cut'n'paste as your GUI may do the
	     * decent thing and convert the, Unicode code point U+00FE
	     * LATIN SMALL LETTER THORN into a UTF-8 0xC3 0xBE
	     * sequence which is correctly 0xFE in Unicode thus
	     * defeating the point of our invalid UTF-8 test.
	     *
	     * *shakes fist*
	     */

	    if (IDIO_READ_CHARACTER_SIMPLE == kind) {
		return IDIO_UNICODE_REPLACEMENT_CHARACTER;
	    } else {
		idio_read_error_utf8_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "not well-formed");

		/* notreached */
		return EOF;
	    }
	}
	/*
	 * more bytes required...
	 */
    }

    if (state != IDIO_UTF8_ACCEPT) {

	/*
	 * XXX can we get here at all?
	 */

	if (idio_eofp_handle (handle)) {
	    if (IDIO_READ_CHARACTER_SIMPLE == kind) {
		return EOF;
	    } else {
		/*
		 * Test Case: read-errors/character-incomplete-eof.idio
		 *
		 * #\x
		 *
		 * where x is a literal byte which *is* a UTF-8 prefix
		 *
		 * From the example above, the test uses 0xC3 from the
		 * start of the sequence 0xC3 0xBE the UTF-8 sequence
		 * for Unicode U+00FE LATIN SMALL LETTER THORN
		 */
		idio_read_error_utf8_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

		/* notreached */
		return EOF;
	    }
	}

	/*
	 * Test Case: read-errors/character-invalid-utf8.idio
	 *
	 * #\x
	 *
	 * where x is a literal byte which is not a UTF-8 prefix of
	 * some sort (as you'll get EOF, above, instead)
	 */
	if (IDIO_READ_CHARACTER_SIMPLE == kind) {
	    return IDIO_UNICODE_REPLACEMENT_CHARACTER;
	} else {
	    idio_read_error_utf8_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "not well-formed");

	    /* notreached */
	    return EOF;
	}
    }

    return codepoint;
}

IDIO idio_read_character (IDIO handle, IDIO lo, int kind)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    idio_unicode_t codepoint = idio_read_character_int (handle, lo, kind);

    if (EOF == codepoint) {
	return idio_S_eof;
    }

    if (IDIO_READ_CHARACTER_SIMPLE == kind) {
	idio_gc_stats_inc (IDIO_TYPE_CONSTANT_UNICODE);
	return IDIO_UNICODE (codepoint);
    } else {
	if (IDIO_CHAR_LBRACE == codepoint) {
	    return idio_read_named_character (handle, lo);
	} else {
	    idio_gc_stats_inc (IDIO_TYPE_CONSTANT_UNICODE);
	    return IDIO_UNICODE (codepoint);
	}
    }
}

static void idio_read_whitespace (IDIO handle, IDIO lo)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    for (;;) {
	idio_unicode_t c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    return;
	}

	switch (c) {
	case IDIO_CHAR_SPACE:
	case IDIO_CHAR_TAB:
	    break;
	default:
	    idio_ungetc_handle (handle, c);
	    return;
	}
    }
}

static void idio_read_newline (IDIO handle, IDIO lo)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    for (;;) {
	idio_unicode_t c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    return;
	}

	switch (c) {
	case IDIO_CHAR_CR:
	case IDIO_CHAR_NL:
	    break;
	default:
	    idio_ungetc_handle (handle, c);
	    return;
	}
    }
}

/*
 * idio_read_list returns the list -- not a lexical object.
 */
static IDIO idio_read_list (IDIO handle, IDIO list_lo, IDIO opendel, idio_unicode_t *ic, int depth)
{
    int count = 0;		/* # of elements in list */

    IDIO closedel;
    if (opendel == idio_T_lparen) {
	closedel = idio_T_rparen;
    } else if (opendel == idio_T_lbrace) {
	closedel = idio_T_rbrace;
    } else if (opendel == idio_T_lbracket) {
	closedel = idio_T_rbracket;
    } else {
	/*
	 * Not possible to write a check for this error condition
	 * without making this or the calling code use an unexpected
	 * delimiter.
	 *
	 * However it catches a development corner case.
	 */
	idio_read_error_parse_args (handle, list_lo, IDIO_C_FUNC_LOCATION (), "unexpected list open delimiter ", opendel);

	return idio_S_notreached;
    }

    IDIO r = idio_S_nil;

    for (;;) {
	IDIO lo = idio_read_1_expr (handle, ic, depth);
	IDIO e = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-errors/list-eof.idio
	     *
	     * ( 1
	     */
	    idio_read_error_list_eof (handle, lo, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else if (idio_T_eol == e) {
	    /* continue */
	} else if (idio_T_pair_separator == e) {
	    if (count < 1) {
		/*
		 * Test Case: read-errors/imp-list-before.idio
		 *
		 * ( & 2 )
		 */
		char em[BUFSIZ];
		idio_snprintf (em, BUFSIZ, "nothing before %c in list", IDIO_PAIR_SEPARATOR);

		idio_read_error_pair_separator (handle, lo, IDIO_C_FUNC_LOCATION (), em);

		return idio_S_notreached;
	    }

	    /*
	     * XXX should only expect a single expr after
	     * IDIO_PAIR_SEPARATOR, ie. not a list: (a & b c)
	     */
	    IDIO lo = idio_read_1_expr (handle, ic, depth);
	    IDIO pt = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
	    /*
	     * Careful of:
	     *
	     * ( a &
	     *
	     *   b )
	     */
	    while (idio_T_eol == pt) {
		IDIO lo = idio_read_1_expr (handle, ic, depth);
		pt = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
	    }

	    if (idio_eofp_handle (handle)) {
		/*
		 * Test Case: read-errors/imp-list-eof-after-sep.idio
		 *
		 * ( 1 &
		 */
		idio_read_error_list_eof (handle, lo, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else if (closedel == pt) {
		/*
		 * Test Case: read-errors/imp-list-nothing-after-sep.idio
		 *
		 * ( 1 & )
		 */
		char em[BUFSIZ];
		idio_snprintf (em, BUFSIZ, "nothing after %c in list", IDIO_PAIR_SEPARATOR);

		idio_read_error_pair_separator (handle, lo, IDIO_C_FUNC_LOCATION (), em);

		return idio_S_notreached;
	    }

	    /*
	     * This should be the closing delimiter
	     */
	    lo = idio_read_1_expr (handle, ic, depth);
	    IDIO del = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
	    while (idio_T_eol == del) {
		/*
		 * Test Case: read-coverage/imp-list-eol-before-delim.idio
		 *
		 * '( 1 & 2
		 *
		 * )
		 */
		lo = idio_read_1_expr (handle, ic, depth);
		del = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
	    }

	    if (idio_eofp_handle (handle)) {
		/*
		 * Test Case: read-errors/imp-list-eof-before-delim.idio
		 *
		 * ( 1 & 2
		 *
		 * XXX Actually, this EOF gets picked up by the first
		 * {if} in this sequence...  Does it make any
		 * difference?  I suppose there's a
		 * position-in-parsing difference in which case we'll
		 * (redundantly) leave the test case in even if code
		 * coverage says it's not picked up here (today).
		 */
		idio_read_error_list_eof (handle, lo, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else if (closedel == del) {
		r = idio_improper_list_reverse (r, pt);
		return r;
	    } else {
		/*
		 * Test case: read-errors/imp-list-many-after.idio
		 *
		 * ( 1 & 2 3 )
		 */
		char em[BUFSIZ];
		idio_snprintf (em, BUFSIZ, "more than one expression after %c in list", IDIO_PAIR_SEPARATOR);

		idio_read_error_pair_separator (handle, lo, IDIO_C_FUNC_LOCATION (), em);

		return idio_S_notreached;
	    }
	}

	IDIO op = idio_infix_operatorp (e);

	if (idio_S_false != op) {
	    /* ( ... {op} <EOL>
	     *   ... )
	     */

	    /*
	     * An operator cannot be in functional position although
	     * several operators and functional names clash!  So, skip
	     * if it's the first element in the list.
	     */
	    if (count > 0) {
		IDIO lo = idio_read_1_expr (handle, ic, depth);
		IDIO ne = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
		while (idio_T_eol == ne) {
		    IDIO lo = idio_read_1_expr (handle, ic, depth);
		    ne = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
		}

		if (idio_eofp_handle (handle)) {
		    /*
		     * Test Case: read-errors/op-eof.idio
		     *
		     * 1 +
		     *
		     * Nominally this is a dupe of the one in
		     * idio_read_expr_line()
		     */
		    idio_read_error_list_eof (handle, lo, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}

		r = idio_pair (e, r);
		count++;
		e = ne;
	    }
	}

	if (idio_T_eol != e) {
	    count++;

	    if (closedel == e) {
		r = idio_list_nreverse (r);
		if (! (IDIO_LIST_QUOTE_P (depth) ||
		       IDIO_LIST_CONSTANT_P (depth))) {
		    r = idio_operator_expand (r, 0);
		}
		r = idio_read_unescape (r);
		if (idio_isa_pair (r) &&
		    IDIO_LIST_CONSTANT_P (depth) == 0) {
		    IDIO r_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
		    idio_struct_instance_set_direct (r_lo, IDIO_LEXOBJ_EXPR, r);
		    idio_hash_put (idio_src_properties, r, r_lo);
		}
		return r;
	    }

	    /*
	     * A few tokens can slip through the net...
	     */
	    if (IDIO_TYPE_CONSTANT_IDIOP (e)) {
		uintptr_t ev = IDIO_CONSTANT_IDIO_VAL (e);
		switch (ev) {
		case IDIO_CONSTANT_NIL:
		case IDIO_CONSTANT_UNDEF:
		case IDIO_CONSTANT_UNSPEC:
		case IDIO_CONSTANT_EOF:
		case IDIO_CONSTANT_TRUE:
		case IDIO_CONSTANT_FALSE:
		case IDIO_CONSTANT_VOID:
		case IDIO_CONSTANT_NAN:
		    break;
		default:
		    /*
		     * Test case: ??
		     */
		    idio_coding_error_C ("unexpected token in list", IDIO_LIST2 (handle, e), IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }

	    if (IDIO_TYPE_CONSTANT_TOKENP (e)) {
		uintptr_t ev = IDIO_CONSTANT_TOKEN_VAL (e);
		switch (ev) {
		case IDIO_TOKEN_DOT:
		    e = idio_S_dot;
		    break;
		default:
		    /*
		     * Test case: ??
		     */
		    idio_coding_error_C ("unexpected token in list", IDIO_LIST2 (handle, e), IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }

	    r = idio_pair (e, r);
	}
    }

    /*
     * Test Case: ??
     */
    idio_read_error_parse_printf (handle, list_lo, IDIO_C_FUNC_LOCATION (), "impossible!");

    return idio_S_notreached;
}

static IDIO idio_read_quote (IDIO handle, IDIO lo, idio_unicode_t *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO qlo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (qlo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_quote, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_quasiquote (IDIO handle, IDIO lo, idio_unicode_t *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO qqlo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (qqlo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_quasiquote, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_unquote_splicing (IDIO handle, IDIO lo, idio_unicode_t *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO uslo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (uslo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_unquotesplicing, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_unquote (IDIO handle, IDIO lo, idio_unicode_t *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO uqlo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (uqlo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_unquote, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_escape (IDIO handle, IDIO lo, idio_unicode_t *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO elo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (elo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_escape, e);

    return r;
}

static IDIO idio_read_unescape (IDIO ls)
{
    IDIO_ASSERT (ls);

    IDIO_TYPE_ASSERT (list, ls);

    IDIO r = idio_S_nil;

    while (idio_S_nil != ls) {
	IDIO e = IDIO_PAIR_H (ls);

	if (idio_isa_pair (e) &&
	    idio_S_escape == IDIO_PAIR_H (e) &&
	    idio_isa_pair (IDIO_PAIR_T (e)) &&
	    idio_S_nil != IDIO_PAIR_HT (e) &&
	    idio_S_nil == IDIO_PAIR_TT (e)) {
	    e = IDIO_PAIR_HT (e);
	}

	r = idio_pair (e, r);

	ls = IDIO_PAIR_T (ls);
    }

    return idio_list_nreverse (r);
}

static void idio_read_line_comment (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    for (;;) {
	idio_unicode_t c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-coverage/line-comment-eof.idio
	     *
	     * ;; no newline!
	     */

	    /*
	     * Not strictly an error -- just no newline at the end of
	     * the file
	     */
	    return;
	}

	switch (c) {
	case IDIO_CHAR_CR:
	case IDIO_CHAR_NL:
	    idio_ungetc_handle (handle, c);
	    return;
	}
    }
}

static void idio_read_sl_block_comment (IDIO handle, IDIO lo, int depth);

/*
 * Block comments #* ... *# can be nested!
 *
 * #*
 * zero, one
 * #*
 * or more lines
 * *#
 * nested
 * *#
 *
 * You can also change the default escape char, \, using the first
 * char after #*
 */
static void idio_read_block_comment (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    char esc_char = IDIO_CHAR_BACKSLASH;
    int esc = 0;
    int asterisk_esc = 0;
    int hash_esc = 0;

    idio_unicode_t c = idio_getc_handle (handle);

    if (idio_eofp_handle (handle)) {
	/*
	 * Test Case: read-error/block-comment-asterisk-initial-eof.idio
	 *
	 * #*
	 */
	idio_read_error_comment (handle, lo, IDIO_C_FUNC_LOCATION (), "unterminated");

	/* notreached */
    }

    if (isgraph (c)) {
	esc_char = c;
    }

    for (;;) {
	c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-coverage/block-comment-asterisk-eof.idio
	     *
	     * #* ...
	     */
	    idio_read_error_comment (handle, lo, IDIO_C_FUNC_LOCATION (), "unterminated");

	    /* notreached */
	}

	if (esc_char == c) {
	    /*
	     * If esc is set we quietly consume the character
	     */
	    if (0 == esc) {
		esc = 1;
		continue;
	    }
	} else {
	    if (esc) {
		/*
		 * quietly consume c
		 */
		esc = 0;
	    } else if (asterisk_esc) {
		switch (c) {
		case IDIO_CHAR_HASH:
		    return;
		}
	    } else if (hash_esc) {
		switch (c) {
		case IDIO_CHAR_ASTERISK:
		    idio_read_block_comment (handle, lo, depth + 1);
		    break;
		case IDIO_CHAR_PIPE:
		    idio_read_sl_block_comment (handle, lo, depth + 1);
		    break;
		}
	    } else {
		switch (c) {
		    /*
		     * Test Case: read-coverage/block-comment-asterisk-escaped-asterisk.idio
		     *
		     * #* * *#
		     */
		case IDIO_CHAR_ASTERISK:
		    asterisk_esc = 1;
		    continue;
		case IDIO_CHAR_HASH:
		    hash_esc = 1;
		    continue;
		}
	    }

	    if (asterisk_esc) {
		asterisk_esc = 0;
	    }
	    if (hash_esc) {
		hash_esc = 0;
	    }
	}
    }
}

/*
 * Semi-literate block comments #| ... |# can be nested!
 *
 * The line of the opening #| should be the engine the comment text
 * will be piped into.  No engines are currently defined by maybe
 * something like:
 *
 * #| .rst
 * ..note this is ReStructuredText
 * #|
 *
 */
static void idio_read_sl_block_comment (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    char esc_char = IDIO_CHAR_BACKSLASH;
    int esc = 0;
    int pipe_esc = 0;
    int hash_esc = 0;

    idio_unicode_t c = idio_getc_handle (handle);

    if (idio_eofp_handle (handle)) {
	/*
	 * Test Case: read-error/block-comment-pipe-initial-eof.idio
	 *
	 * #|
	 */
	idio_read_error_comment (handle, lo, IDIO_C_FUNC_LOCATION (), "unterminated");

	/* notreached */
    }

    if (isgraph (c)) {
	esc_char = c;
    }

    for (;;) {
	c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-coverage/block-comment-pipe-eof.idio
	     *
	     * #| ...
	     */
	    idio_read_error_comment (handle, lo, IDIO_C_FUNC_LOCATION (), "unterminated");

	    /* notreached */
	}

	if (esc_char == c) {
	    /*
	     * If esc is set we quietly consume the character
	     */
	    if (0 == esc) {
		esc = 1;
		continue;
	    }
	} else {
	    if (esc) {
		/*
		 * quietly consume c
		 */
		esc = 0;
	    } else if (pipe_esc) {
		switch (c) {
		case IDIO_CHAR_HASH:
		    return;
		}
	    } else if (hash_esc) {
		switch (c) {
		case IDIO_CHAR_PIPE:
		    idio_read_sl_block_comment (handle, lo, depth + 1);
		    break;
		case IDIO_CHAR_ASTERISK:
		    idio_read_block_comment (handle, lo, depth + 1);
		    break;
		}
	    } else {
		switch (c) {
		    /*
		     * Test Case: read-coverage/block-comment-pipe-escaped-pipe.idio
		     *
		     * #| | |#
		     */
		case IDIO_CHAR_PIPE:
		    pipe_esc = 1;
		    continue;
		case IDIO_CHAR_HASH:
		    hash_esc = 1;
		    continue;
		}
	    }

	    if (pipe_esc) {
		pipe_esc = 0;
	    }
	    if (hash_esc) {
		hash_esc = 0;
	    }
	}
    }
}

/*
 * idio_read_string returns the string -- not a lexical object.
 *
 * If the first interpolation character is non-NUL then the result is
 *
 *   (concatenate-string (map ->string (list e+)))
 *
 * where each {e} could be a string or an expression to be evaluated
 * (->string will convert non-strings to string if required).
 *
 * There is an interesting question about *how* we read in a UTF-8
 * encoded string.
 *
 * 1. read in Unicode code points until the successful decode is ",
 *    U+0022 (QUOTATION MARK), -- you will have to use a uint32_t
 *    array as you cannot predict what codepoint is coming next.
 *
 * 2. read in a byte at a time until the byte is " -- handily
 *    identical to U+0022 (QUOTATION MARK) -- then use the utf8
 *    decoder on the byte array
 *
 * In both cases, you now require to convert the uint32_t[] or
 * uint8_t[] into a correctly sized Idio string.
 *
 * The problem though is what happens with malformed UTF-8 sequences.  In each case
 *
 * 1. if you have a valid UTF-8 prefix where " is not a valid sequence
 *    then *that* " is consumed and you will continue looking for " in
 *    the rest of the file
 *
 * 2. you will stop at the first " byte and if the previous byte was a
 *    valid prefix expecting another byte then it will fail UTF-8
 *    decoding
 */

#define IDIO_READ_STRING_UTF8	(1<<0)
#define IDIO_READ_STRING_OCTET	(1<<1)
#define IDIO_READ_STRING_PATH	(1<<2)
static IDIO idio_read_string (IDIO handle, IDIO lo, idio_unicode_t delim, idio_unicode_t *ic, int flag)
{
    IDIO_ASSERT (handle);
    IDIO_C_ASSERT (ic);

    /*
     * In a feeble attempt to reduce fragmentation we can imagine
     * allocating reasonable sized chunks of memory aligned to 2**n
     * boundaries.  However, malloc(3) seems to use a couple of
     * pointers so any individual allocation length (alen) should be
     * docked that amount
     */

#define IDIO_STRING_CHUNK_LEN	sizeof (unsigned int)
    int nchunks = 1;
    size_t alen = (nchunks * IDIO_STRING_CHUNK_LEN); /* - 2 * sizeof (void *); */
    char *abuf = idio_alloc (alen);
    size_t slen = 0;

    int done = 0;
    int esc = 0;

    IDIO r = idio_S_nil;

    while (! done) {
	int c = idio_getb_handle (handle);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-errors/string-eof.idio
	     *
	     * "
	     */
	    idio_free (abuf);

	    idio_read_error_string (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	}

	if (delim == c) {
	    if (esc) {
		abuf[slen++] = c;
	    } else {
		done = 1;
	    }
	} else if (ic[0] &&
		   ic[0] == c) { /* interpolate */

	    abuf[slen] = '\0';

	    if (slen) {
		IDIO s = idio_string_C_len (abuf, slen);
		IDIO_FLAGS (s) |= IDIO_FLAG_CONST;

		r = idio_pair (s, r);
		slen = 0;
	    }

	    idio_unicode_t opendel = idio_getc_handle (handle);
	    IDIO closedel;

	    int depth;

	    switch (opendel) {
	    case IDIO_CHAR_LBRACE:
		closedel = idio_T_rbrace;
		depth = IDIO_LIST_BRACE (1);
		break;
	    default:
		{
		    char em[BUFSIZ];
		    idio_snprintf (em, BUFSIZ, "unexpected interpolation delimiter '%c' (%#x)", opendel, opendel);

		    idio_read_error_string (handle, lo, IDIO_C_FUNC_LOCATION (), em);

		    return idio_S_notreached;
		}
		break;
	    }

	    IDIO e = idio_read_block (handle, lo, closedel, idio_default_template_ic, depth);
	    /*
	     * idio_read_block has returned (block expr) and we only want expr
	     *
	     * Note that (block expr1 expr2+) means we need to wrap a begin
	     * round expr1 expr2+ -- unlike quasiquote!
	     */
	    if (idio_S_nil == IDIO_PAIR_TT (e)) {
		r = idio_pair (IDIO_PAIR_HT (e), r);
	    } else {
		IDIO ep = idio_list_append2 (IDIO_LIST1 (idio_S_begin),
					     IDIO_PAIR_T (e));
		r = idio_pair (ep, r);
	    }
	} else if (ic[1] == c) { /* escape */
	    if (esc) {
		abuf[slen++] = c;
	    } else {
		esc = 1;
		continue;
	    }
	} else {
	    int use_c = 1;
	    if (esc) {
		switch (c) {
		    /*
		     * Test Case: read-coverage/string-escaped-characters.idio
		     *
		     * "\a\b..."
		     */
		case 'a': c = '\a'; break; /* alarm (bell) */
		case 'b': c = '\b'; break; /* backspace */
		case 'e': c = '\e'; break; /* ESC */
		case 'f': c = '\f'; break; /* formfeed */
		case 'n': c = '\n'; break; /* newline */
		case 'r': c = '\r'; break; /* carriage return */
		case 't': c = '\t'; break; /* horizontal tab */
		case 'v': c = '\v'; break; /* vertical tab */

		    /* \\ handled above */
		    /* \' handled by default */
		    /* \" handled above */
		    /* \? handled by default */
		    /* \0 NUL or start of octal ?? - ignored => 0 (zero) */
		    /* \NL (line continuation) - ignored => NL (newline) */

		case 'x':
		    c = idio_read_uintmax_radix (handle, lo, c, 16, 2);
		    break;

		case 'u':
		case 'U':
		    {
			/*
			 * Our goal here is to reproduce what would
			 * have been the UTF-8 representation of the
			 * code point identified by the hex digits
			 * we're about to read in.
			 *
			 * idio_string_C_len() will recompose them
			 * into an Idio internal 1/2/4 bytes width
			 * string.
			 *
			 * idio_pathname_C_len() will take the string
			 * verbatim -- which primarily affects \x
			 * above.
			 */
			int lim = 8;
			switch (c) {
			case 'u': lim = 4; break;
			}
			uintmax_t u = idio_read_uintmax_radix (handle, lo, c, 16, lim);

			char b[4];
			int n;
			if (idio_unicode_valid_code_point (u)) {
			    if (u >= 0x10000) {
				b[0] = 0xf0 | ((u & (0x07 << 18)) >> 18);
				b[1] = 0x80 | ((u & (0x3f << 12)) >> 12);
				b[2] = 0x80 | ((u & (0x3f << 6)) >> 6);
				b[3] = 0x80 | ((u & (0x3f << 0)) >> 0);
				n = 4;
			    } else if (u >= 0x0800) {
				b[0] = 0xe0 | ((u & (0x0f << 12)) >> 12);
				b[1] = 0x80 | ((u & (0x3f << 6)) >> 6);
				b[2] = 0x80 | ((u & (0x3f << 0)) >> 0);
				n = 3;
			    } else if (u >= 0x0080) {
				b[0] = 0xc0 | ((u & (0x1f << 6)) >> 6);
				b[1] = 0x80 | ((u & (0x3f << 0)) >> 0);
				n = 2;
			    } else {
				b[0] = u & 0x7f;
				n = 1;
			    }
			} else {
			    /*
			     * Test Cases:
			     *
			     *   read-errors/string-unicode-invalid-code-point-1.idio
			     *   read-errors/string-unicode-invalid-code-point-2.idio
			     *
			     * "\uD800"
			     * "\U00A92021"
			     *
			     * presumably meant to be "\u00A92021" for
			     * the UTF-8 "©2021"
			     */
			    char em[BUFSIZ];
			    idio_snprintf (em, BUFSIZ, "Unicode code point U+%04jX is invalid", u);

			    idio_read_error_string (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    /* notreached */
			    return NULL;
			}

			if ((slen + n) >= alen) {
			    alen += IDIO_STRING_CHUNK_LEN;
			    abuf = idio_realloc (abuf, alen);
			}

			for (int i = 0; i < n; i++) {
			    abuf[slen++] = b[i];
			}

			use_c = 0;
		    }
		    break;

		    /* \e ESC (\x1B) GCC extension */

		default:
		    /* leave alone */
		    break;
		}
	    }

	    if (use_c) {
		abuf[slen++] = c;
	    }
	}

	if (esc) {
	    esc = 0;
	}

	if (slen >= alen) {
	    alen += IDIO_STRING_CHUNK_LEN;
	    abuf = idio_realloc (abuf, alen);
	}
    }

    abuf[slen] = '\0';

    switch (flag) {
    case IDIO_READ_STRING_UTF8:
	{
	    IDIO s = idio_string_C_len (abuf, slen);
	    IDIO_FLAGS (s) |= IDIO_FLAG_CONST;

	    if (ic[0]) {
		r = idio_pair (s, r);
		r = idio_list_nreverse (r);
		r = IDIO_LIST2 (idio_S_concatenate_string,
				IDIO_LIST3 (idio_S_map,
					    idio_S_2string,
					    idio_list_append2 (IDIO_LIST1 (idio_S_list),
							       r)));
	    } else {
		r = s;
	    }
	}
	break;
    case IDIO_READ_STRING_OCTET:
	r = idio_octet_string_C_len (abuf, slen);
	break;
    case IDIO_READ_STRING_PATH:
	r = idio_pathname_C_len (abuf, slen);
	break;
    }

    idio_free (abuf);
    return r;
}

static IDIO idio_read_utf8_string (IDIO handle, IDIO lo, idio_unicode_t delim, idio_unicode_t *ic)
{
    return idio_read_string (handle, lo, delim, ic, IDIO_READ_STRING_UTF8);
}

static IDIO idio_read_octet_string (IDIO handle, IDIO lo, idio_unicode_t delim, idio_unicode_t *ic)
{
    return idio_read_string (handle, lo, delim, ic, IDIO_READ_STRING_OCTET);
}

static IDIO idio_read_path_string (IDIO handle, IDIO lo, idio_unicode_t delim, idio_unicode_t *ic)
{
    return idio_read_string (handle, lo, delim, ic, IDIO_READ_STRING_PATH);
}

/*
 * idio_read_named_character returns the character -- not a lexical
 * object.
 */
static IDIO idio_read_named_character (IDIO handle, IDIO lo)
{
    IDIO_ASSERT (handle);

    char buf[IDIO_CHARACTER_MAX_NAME_LEN+1];
    int i;
    idio_unicode_t c;

    for (i = 0 ; i < IDIO_CHARACTER_MAX_NAME_LEN; i++) {
	c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-errors/named-character-eof.idio
	     *
	     * #\{
	     */
	    idio_read_error_named_character (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	}

	/*
	 * All subsequent c's should be ASCII until '}' -- I believe
	 * that other than test files non-ASCII chars only appear in
	 * comments in Unicode data files so a named character can
	 * only be named by ASCII characters.
	*/
	if (c > 0x7f) {
	    /*
	     * Test Case: read-errors/named-character-non-ASCII.idio
	     *
	     * #\{newlïne}
	     *
	     * In the test Case, that's the UTF-8 for U+00EF (LATIN
	     * SMALL LETTER I WITH DIAERESIS) in the middle, which may
	     * show up as a Latin small letter I with diaeresis or as
	     * the UTF-8 tuple 0xC3 0xAF.
	     *
	     * The point being that neith 0xC3 or 0xAF are ASCII and
	     * therefore is cannot be the name of a character.
	     */
	    idio_read_error_named_character (handle, lo, IDIO_C_FUNC_LOCATION (), "non-ASCII");

	    return idio_S_notreached;
	}

	if ('}' == c) {
	    break;
	}

	buf[i] = c;
    }

    buf[i] = '\0';

    IDIO r;

    if (0 == i) {
	/*
	 * Test Case: read-errors/named-character-no-name.idio
	 *
	 * #\{}
	 *
	 */
	idio_read_error_named_character (handle, lo, IDIO_C_FUNC_LOCATION (), "no letters in character name?");

	return idio_S_notreached;
    } else {
	r = idio_unicode_lookup (buf);

	if (r == idio_S_unspec) {
	    /*
	     * Test Case: read-errors/named-character-unknown.idio
	     *
	     * #\{caveat}
	     *
	     * XXX This is a bit tricky as (at the time of writing)
	     * we're limited to ASCII chars so no underscores or
	     * colons or things that (gensym) might create which would
	     * prevent someone from accidentally introducing
	     * #\{caveat} as a real named character.
	     */
	    idio_read_error_named_character_unknown_name (handle, lo, IDIO_C_FUNC_LOCATION (), buf);

	    return idio_S_notreached;
	}
    }

    idio_gc_stats_inc (IDIO_TYPE_CONSTANT_UNICODE);
    return r;
}

/*
 * idio_read_array returns the array -- not a lexical object.
 */
static IDIO idio_read_array (IDIO handle, IDIO lo, idio_unicode_t *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO l = idio_read_list (handle, lo, idio_T_lbracket, ic, IDIO_LIST_BRACKET (depth + 1));
    return idio_list_to_array (l);
}

/*
 * idio_read_hash returns the hash -- not a lexical object.
 */
static IDIO idio_read_hash (IDIO handle, IDIO lo, idio_unicode_t *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO l = idio_read_list (handle, lo, idio_T_lbrace, ic, IDIO_LIST_CONSTANT (IDIO_LIST_BRACE (depth + 1)));
    IDIO r = idio_hash_alist_to_hash (l, idio_S_nil);
    return r;
}

/*
 * idio_read_bitset returns the bitset -- not a lexical object.
 */
static IDIO idio_read_bitset_buf_sh = idio_S_nil;
static IDIO idio_read_bitset_offset_sh = idio_S_nil;
static IDIO idio_read_bitset_end_sh = idio_S_nil;

static IDIO idio_read_bitset (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);

    idio_unicode_t c = idio_getc_handle (handle);

    if (idio_eofp_handle (handle)) {
	/*
	 * Test Case: read-errors/bitset-eof.idio
	 *
	 * #B
	 */
	idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	return idio_S_notreached;
    }

    switch (c) {
    case IDIO_CHAR_LBRACE:
	break;
    default:
	/*
	 * Test Case: read-errors/bitset-not-lbrace.idio
	 *
	 * #B[
	 */
	idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "not a {");

	return idio_S_notreached;
    }

    idio_read_whitespace (handle, lo);

    IDIO bs = idio_S_nil;

    int done = 0;
    int seen_size = 0;

    /*
     * NB offset is a signed type to catch read errors
     */
    ptrdiff_t offset = 0;

    while (! done) {
	char buf[IDIO_WORD_MAX_LEN + 1];
	char *bit_block = buf;
	int i = 0;
	int eow = 0;

	c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-errors/bitset-internal-eof-1.idio
	     *
	     * #B{
	     *
	     */
	    idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	}

	if (IDIO_CHAR_RBRACE == c) {
	    done = 1;
	    break;
	}

	/*
	 * The bitset description is moderately complicated:
	 *
	 * size block*
	 *
	 * a block can be
	 *
	 *   block-bits (a combination of 0s and 1s)
	 *
	 *   offset:block-bits (jump to offset then block-bits)
	 *
	 *   first-last (a range of set bits with first and last being
	 *   offsets)
	 */
	char *bit_range = NULL;

	for (;;) {
	    buf[i++] = c;

	    if (i > IDIO_WORD_MAX_LEN) {
		/*
		 * Test Case: read-errors/bitset-word-too-long.idio
		 *
		 * #B{ 0000...0000 }
		 *
		 * (a very long word consisting of '0's, you get the picture)
		 *
		 * Actually the test case has two words, one is
		 * IDIO_WORD_MAX_LEN chars long and the other is
		 * IDIO_WORD_MAX_LEN+1 chars long.  The first should not
		 * cause an error.
		 */
		buf[IDIO_WORD_MAX_LEN] = '\0';
		idio_read_error_parse_word_too_long (handle, lo, IDIO_C_FUNC_LOCATION (), buf);

		return idio_S_notreached;
	    }

	    c = idio_getc_handle (handle);

	    if (idio_eofp_handle (handle)) {
		/*
		 * Test Case: read-errors/bitset-internal-eof-2.idio
		 *
		 * #B{ 23
		 *
		 */
		idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

		return idio_S_notreached;
	    }

	    switch (c) {
	    case IDIO_CHAR_SPACE:
	    case IDIO_CHAR_TAB:
	    case IDIO_CHAR_NL:
	    case IDIO_CHAR_CR:
		idio_read_whitespace (handle, lo);
		if (i > 0) {
		    eow = 1;
		}
		break;
	    case IDIO_CHAR_COLON:
		bit_block = buf + i;
		break;
	    case IDIO_CHAR_HYPHEN_MINUS:
		bit_range = buf + i;
		break;
	    case IDIO_CHAR_RBRACE:
		if (i > 0) {
		    eow = 1;
		}
		done = 1;
		break;
	    }

	    if (eow ||
		done) {
		break;
	    }
	}

	buf[i] = '\0';

	if (eow) {
	    if (0 == seen_size) {
		idio_reopen_input_string_handle_C (idio_read_bitset_buf_sh, buf, i);
		IDIO I_size = idio_read_number_C (idio_read_bitset_buf_sh, buf);

		/*
		 * NB bs_size is a signed type to catch read errors
		 */
		ptrdiff_t bs_size = 0;

		/*
		 * idio_read_number_C() is a bit broad so there are
		 * extra checks on size.
		 */
		if (idio_isa_fixnum (I_size)) {
		    bs_size = IDIO_FIXNUM_VAL (I_size);

		    if (bs_size < 0) {
			/*
			 * Test Case: read-errors/bitset-size-negative.idio
			 *
			 * #B{ -1 }
			 *
			 */
			idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "size must be a positive decimal integer");

			return idio_S_notreached;
		    }
		} else if (idio_isa_bignum (I_size)) {
		    if (idio_bignum_negative_p (I_size)) {
			/*
			 * Test Case: read-errors/bitset-size-negative-bignum.idio
			 *
			 * #B{ -2305843009213693952 }
			 * #B{ -536870912 }
			 *
			 */
			idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "size must be a positive decimal integer");

			return idio_S_notreached;
		    }

		    IDIO size_i = idio_bignum_real_to_integer (I_size);
		    if (idio_S_nil == size_i) {
			/*
			 * Test Case: read-errors/bitset-size-floating-point.idio
			 *
			 * #B{ 3.1 }
			 *
			 */
			idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "size must be a positive decimal integer");

			return idio_S_notreached;
		    }

		    /*
		     * Code coverage?
		     *
		     * To get a bignum here means we passed a
		     * number that was more than INTPTR_MAX >> 2:
		     * ie. 2^30-1 or 2^62-1.  I don't think we
		     * want to create a bitset that big for
		     * testing...
		     */
		    bs_size = idio_bignum_uint64_t_value (I_size);
		} else {
		    /*
		     * Test Case: read-errors/bitset-size-non-integer.idio
		     *
		     * #B{ + }
		     *
		     * a plus sign on its own cause
		     * idio_read_number_C() to return idio_S_nil
		     */
		    idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "size must be a positive decimal integer");

		    return idio_S_notreached;
		}

		bs = idio_bitset (bs_size);
		seen_size = 1;
	    } else {
		idio_reopen_input_string_handle_C (idio_read_bitset_buf_sh, buf, i);

		IDIO lo = idio_read_lexobj_from_handle (idio_read_bitset_buf_sh);

		if (NULL != bit_range) {
		    /* bit_range is pointing at the HYPEN_MINUS */
		    *bit_range = '\0';
		    bit_range++;

		    ssize_t buf_len = bit_range - buf - 1;
		    idio_reopen_input_string_handle_C (idio_read_bitset_offset_sh, buf, buf_len);

		    IDIO I_offset = idio_read_bignum_radix (idio_read_bitset_offset_sh, lo, 'x', 16);

		    if (idio_isa_fixnum (I_offset)) {
			offset = IDIO_FIXNUM_VAL (I_offset);

			if (offset < 0) {
			    /*
			     * Test Case: read-errors/bitset-range-start-negative.idio
			     *
			     * #B{ 3 -2-0: }
			     */
			    idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "range start must be a positive base-16 integer");

			    return idio_S_notreached;
			}
		    } else if (idio_isa_bignum (I_offset)) {
			if (idio_bignum_negative_p (I_offset)) {
			    /*
			     * Test Case: read-errors/bitset-range-start-negative-bignum.idio
			     *
			     * #B{ 3 -2000000000000000-20 }
			     */
			    idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "range start must be a positive base-16 integer");

			    return idio_S_notreached;
			}

			/*
			 * Test Case: read-errors/bitset-range-start-too-big-bignum.idio
			 *
			 * #B{ 3 2000000000000000-20 }
			 *
			 * Technically a code coverage issue but causes the too big error
			 */
			offset = idio_bignum_uint64_t_value (I_offset);
		    } else {
			/*
			 * Test Case: read-errors/bitset-range-start-non-integer.idio
			 *
			 * #B{ 3 +-0 }
			 *
			 * a plus sign on its own cause
			 * idio_read_number_C() to return idio_S_nil
			 *
			 * However, we will not see this warning
			 * because idio_read_bignum_radix() will have
			 * barfed before we get here.
			 */

			return idio_S_notreached;
		    }

		    /*
		     * Check if idio_read_bignum_radix() read fewer
		     * characters than in buf indicating buf isn't a
		     * valid hex value
		     */
		    if (idio_handle_tell (idio_read_bitset_offset_sh) != buf_len) {
			/*
			 * Test Case: read-errors/bitset-range-start-floating-point.idio
			 *
			 * #B{ 3 1.1-2 }
			 *
			 */
			/*
			 * XXX BUFSIZ could be the same order of
			 * magnitude as {buf}, which is
			 * IDIO_WORD_MAX_LEN bytes.  So curtail the
			 * amount of buf we add into {em}, here.
			 *
			 * 32 should cover the potential textual
			 * content of {em}.
			 */
			int len = BUFSIZ - 32;
			if (buf_len < len) {
			  len = buf_len;
			}
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "range start %#zx from \"%.*s\"", offset, len, buf);

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    /*
		     * offset is size_t, ie unsigned to can't be < 0
		     */
		    if (offset > (ssize_t) IDIO_BITSET_SIZE (bs)) {
			/*
			 * Test Case: read-errors/bitset-range-start-too-big.idio
			 *
			 * #B{ 3 10-20 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "range start %#zx > bitset size %#zx", offset, IDIO_BITSET_SIZE (bs));

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    if (offset % CHAR_BIT) {
			/*
			 * Test Case: read-errors/bitset-range-start-non-byte-boundary.idio
			 *
			 * #B{ 3 1-20 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "range start %#zx is not a byte boundary", offset);

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    /*
		     * idio_read_bignum_radix () is going to catch any
		     * negative 'range end' numbers, eg. 0--10 --
		     * which becomes "0", "-" and "-10", because the
		     * '-' (in "-10") is not a valid base-16 digit.
		     *
		     * Accordingly, {end} could be a size_t but we'll
		     * leave it as ptrdiff_t in case we revise our
		     * understanding.
		     */
		    ptrdiff_t end = 0;

		    /*
		     * bit_range_len is {i} less {buf_len} less 1 (for the NUL)
		     */
		    ssize_t bit_range_len = i - buf_len - 1;
		    idio_reopen_input_string_handle_C (idio_read_bitset_end_sh, bit_range, bit_range_len);

		    /*
		     * Test Case: read-errors/bitset-range-end-negative.idio
		     *
		     * #B{ 3 0--10 }
		     */
		    IDIO I_end = idio_read_bignum_radix (idio_read_bitset_end_sh, lo, 'x', 16);

		    if (idio_isa_fixnum (I_end)) {
			end = IDIO_FIXNUM_VAL (I_end);
		    } else if (idio_isa_bignum (I_end)) {
			/*
			 * Test Case: read-errors/bitset-range-end-too-big-bignum.idio
			 *
			 * #B{ 3 20-2000000000000000 }
			 *
			 * Technically a code coverage issue but causes the too big error
			 */
			end = idio_bignum_uint64_t_value (I_end);
		    }

		    if (idio_handle_tell (idio_read_bitset_end_sh) != bit_range_len) {
			/*
			 * Test Case: read-errors/bitset-range-end-floating-point.idio
			 *
			 * #B{ 3 0-2.1 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "range end %#zx from \"%s\"", end, bit_range);

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    if (end > (ssize_t) IDIO_BITSET_SIZE (bs)) {
			/*
			 * Test Case: read-errors/bitset-range-end-too-big.idio
			 *
			 * #B{ 3 0-20 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "range end %#zx > bitset size %#zx", end, IDIO_BITSET_SIZE (bs));

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    if (end % CHAR_BIT) {
			/*
			 * Test Case: read-errors/bitset-range-end-non-byte-boundary.idio
			 *
			 * #B{ 3 0-2 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "range end %#zx is not a byte boundary", end);

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    if (offset > end) {
			/*
			 * Test Case: read-errors/bitset-range-start-greater-end.idio
			 *
			 * #B{ 30 10-0 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "range start %#zx > range end %#zx", offset, end);

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    while (offset <= end) {
			size_t n = offset / IDIO_BITSET_BITS_PER_WORD;
			int b = (offset % IDIO_BITSET_BITS_PER_WORD) / CHAR_BIT;

			    if (0 == b &&
				(offset + (ssize_t) IDIO_BITSET_BITS_PER_WORD) < end) {
				IDIO_BITSET_WORDS (bs, n) = IDIO_BITSET_WORD_MAX;
				offset += IDIO_BITSET_BITS_PER_WORD;
			    } else {
				idio_bitset_word_t br = 0;

				for (; offset <= end &&
					 b < (ssize_t) sizeof (idio_bitset_word_t); b++) {
				    idio_bitset_word_t mask = UCHAR_MAX;
				    mask <<= (b * CHAR_BIT);
				    br |= mask;

				    offset += CHAR_BIT;
				}
				IDIO_BITSET_WORDS (bs, n) |= br;
			    }
		    }

		} else {
		    if (bit_block != buf) {
			/* bit_block is pointing at the colon */
			*bit_block = '\0';
			bit_block++;

			IDIO off_sh = idio_open_input_string_handle_C (buf, bit_block - buf - 1);

			IDIO I_offset = idio_read_bignum_radix (off_sh, lo, 'x', 16);

			if (idio_isa_fixnum (I_offset)) {
			    offset = IDIO_FIXNUM_VAL (I_offset);

			    if (offset < 0) {
				/*
				 * Test Case: read-errors/bitset-offset-negative.idio
				 *
				 * #B{ 3 -2: }
				 */
				idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "offset must be a positive base-16 integer");

				return idio_S_notreached;
			    }
			} else if (idio_isa_bignum (I_offset)) {
			    if (idio_bignum_negative_p (I_offset)) {
				/*
				 * Test Case: read-errors/bitset-offset-negative-bignum.idio
				 *
				 * #B{ 3 -2000000000000000: }
				 */
				idio_read_error_bitset (handle, lo, IDIO_C_FUNC_LOCATION (), "offset must be a positive base-16 integer");

				return idio_S_notreached;
			    }

			    /*
			     * Test Case: read-errors/bitset-offset-too-big-bignum.idio
			     *
			     * #B{ 3 2000000000000000:00 }
			     *
			     * Technically a code coverage issue but causes the too big error
			     */
			    offset = idio_bignum_uint64_t_value (I_offset);
			} else {
			    /*
			     * Test Case: read-errors/bitset-offset-non-integer.idio
			     *
			     * #B{ 3 +: }
			     *
			     * a plus sign on its own cause
			     * idio_read_number_C() to return idio_S_nil
			     *
			     * However, we will not see this warning
			     * because idio_read_bignum_radix() will have
			     * barfed before we get here.
			     */

			    return idio_S_notreached;
			}

			if (offset > (ssize_t) IDIO_BITSET_SIZE (bs)) {
			    /*
			     * Test Case: read-errors/bitset-offset-too-big.idio
			     *
			     * #B{ 3 10:00 }
			     *
			     */
			    char em[BUFSIZ];
			    idio_snprintf (em, BUFSIZ, "offset %#zx > bitset size %#zx", offset, IDIO_BITSET_SIZE (bs));

			    idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}

			if (offset % CHAR_BIT) {
			    /*
			     * Test Case: read-errors/bitset-offset-non-byte-boundary.idio
			     *
			     * #B{ 3 1:00 }
			     *
			     */
			    char em[BUFSIZ];
			    idio_snprintf (em, BUFSIZ, "offset %#zx is not a byte boundary", offset);

			    idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}

		    }

		    size_t bit_block_len = i - (bit_block - buf);
		    if (bit_block_len > CHAR_BIT) {
			/*
			 * Test Case: read-errors/bitset-offset-too-many-bits-in-block.idio
			 *
			 * #B{ 3 101010101 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "bitset bits should be fewer than %d", CHAR_BIT);

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    if ((offset + bit_block_len) > IDIO_BITSET_SIZE (bs)) {
			/*
			 * Test Case: read-errors/bitset-offset-too-many-bits.idio
			 *
			 * #B{ 3 10101010 }
			 *
			 */
			char em[BUFSIZ];
			idio_snprintf (em, BUFSIZ, "offset %#zx + %zu bits > bitset size %#zx", offset, bit_block_len, IDIO_BITSET_SIZE (bs));

			idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

			return idio_S_notreached;
		    }

		    idio_bitset_word_t new = 0;
		    for (unsigned int i = 0; i < bit_block_len; i++) {
			switch (bit_block[i]) {
			case '0':
			    break;
			case '1':
			    new |= (1 << i);
			    break;
			default:
			    {
				/*
				 * Test Case: read-errors/bitset-offset-bad-bits.idio
				 *
				 * #B{ 3 012 }
				 *
				 */
				char em[BUFSIZ];
				idio_snprintf (em, BUFSIZ, "bits should be 0/1, not %#x @%d", bit_block[i], i);

				idio_read_error_bitset (idio_read_bitset_buf_sh, lo, IDIO_C_FUNC_LOCATION (), em);

				return idio_S_notreached;
			    }
			    break;
			}
		    }

		    size_t n = offset / IDIO_BITSET_BITS_PER_WORD;
		    size_t b = (offset % IDIO_BITSET_BITS_PER_WORD) / CHAR_BIT;
		    idio_bitset_word_t ul = IDIO_BITSET_WORDS (bs, n);
		    idio_bitset_word_t mask = UCHAR_MAX;
		    mask <<= (b * CHAR_BIT);
		    ul = (ul & ~mask) | (new << b * CHAR_BIT);
		    IDIO_BITSET_WORDS (bs, n) = ul;

		    offset += bit_block_len;
		}
	    }
	}
    }

    return bs;
}

static IDIO idio_read_pathname (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    idio_unicode_t interpc[IDIO_STRING_IC];
    for (i = 0; i < IDIO_STRING_IC; i++) {
	interpc[i] = idio_default_string_ic[i];
    }
    i = 0;

    idio_unicode_t c = idio_getc_handle (handle);

    while (!(IDIO_OPEN_DELIMITER (c) ||
	     IDIO_CHAR_DQUOTE == c)) {
	if (i >= IDIO_STRING_IC) {
	    /*
	     * Test Case: read-errors/pathname-too-many-ic.idio
	     *
	     * #P*&^%$" * "
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "too many interpolation characters: #%d: %c (%#x)", i + 1, c, c);

	    idio_read_error_pathname (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-errors/pathname-eof.idio
	     *
	     * #P
	     */
	    idio_read_error_pathname (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	}

	if (IDIO_CHAR_DOT != c) {
	    interpc[i] = c;
	}

	i++;
	c = idio_getc_handle (handle);
    }

    /*
     * Test Case: read-coverage/pathname-bracketing.idio
     *
     * #P" ({[ "
     * #P( "{[ )
     * #P{ "([ }
     * #P[ "({ ]
     */
    char closedel = 0;
    switch (c) {
    case IDIO_CHAR_DQUOTE:
	closedel = IDIO_CHAR_DQUOTE;
	break;
    case IDIO_CHAR_LPAREN:
	closedel = IDIO_CHAR_RPAREN;
	break;
    case IDIO_CHAR_LBRACE:
	closedel = IDIO_CHAR_RBRACE;
	break;
    case IDIO_CHAR_LBRACKET:
	closedel = IDIO_CHAR_RBRACKET;
	break;
    default:
	{
	    /*
	     * Can only get here if IDIO_OPEN_DELIMITER() doesn't
	     * match the case entries above
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "unexpected delimiter: %c (%#x)", c, c);

	    idio_read_error_pathname (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}
    }

    /*
     * Test Case: read-coverage/pathname.idio
     *
     * struct-instance? #P" *.c "
     */
    IDIO e = idio_read_path_string (handle, lo, closedel, interpc);

    return idio_struct_instance (idio_path_type, IDIO_LIST1 (e));
}

/*
 * interpolated strings
 *
 * an append-string variation on a template's append -- albeit using
 * concatenate-string (and map and ...)
 */
static IDIO idio_read_istring (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    idio_unicode_t is_ic[IDIO_ISTRING_IC];
    for (i = 0; i < IDIO_ISTRING_IC; i++) {
	is_ic[i] = idio_default_istring_ic[i];
    }
    i = 0;

    idio_unicode_t c = idio_getc_handle (handle);

    while (! IDIO_OPEN_DELIMITER (c)) {
	if (i >= IDIO_ISTRING_IC) {
	    /*
	     * Test Case: read-errors/istring-too-many-ic.idio
	     *
	     * #S*&^%${ 1 }
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "too many interpolation characters: #%d: %c (%#x)", i + 1, c, c);

	    idio_read_error_string (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-errors/istring-eof.idio
	     *
	     * #S
	     */
	    idio_read_error_string (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	}

	if (IDIO_CHAR_DOT != c) {
	    is_ic[i] = c;
	}

	i++;
	c = idio_getc_handle (handle);
    }

    idio_unicode_t closedel;
    switch (c) {
    case IDIO_CHAR_LPAREN:
	/*
	 * Test Case: read-coverage/istring-bracketing.idio
	 *
	 * #S( 1 )
	 * #S[ 1 ]
	 */
	closedel = IDIO_CHAR_RPAREN;
	break;
    case IDIO_CHAR_LBRACE:
	closedel = IDIO_CHAR_RBRACE;
	break;
    case IDIO_CHAR_LBRACKET:
	/*
	 * Test Case: read-coverage/istring-bracketing.idio
	 *
	 * #S( 1 )
	 * #S[ 1 ]
	 */
	closedel = IDIO_CHAR_RBRACKET;
	break;
    default:
	{
	    /*
	     * Can only get here if IDIO_OPEN_DELIMITER() doesn't
	     * match the case entries above
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "unexpected delimiter: %c (%#x)", c, c);

	    idio_read_error_template (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}
    }

    IDIO r = idio_read_utf8_string (handle, lo, closedel, is_ic);

    return r;
}

static IDIO idio_read_template (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    idio_unicode_t interpc[IDIO_TEMPLATE_IC];
    for (i = 0; i < IDIO_TEMPLATE_IC; i++) {
	interpc[i] = idio_default_template_ic[i];
    }
    i = 0;

    idio_unicode_t c = idio_getc_handle (handle);

    while (! IDIO_OPEN_DELIMITER (c)) {
	if (i >= IDIO_TEMPLATE_IC) {
	    /*
	     * Test Case: read-errors/template-too-many-ic.idio
	     *
	     * #T*&^%${ 1 }
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "too many interpolation characters: #%d: %c (%#x)", i + 1, c, c);

	    idio_read_error_template (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-errors/template-eof.idio
	     *
	     * #T
	     */
	    idio_read_error_template (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	}

	if (IDIO_CHAR_DOT != c) {
	    interpc[i] = c;
	}

	i++;
	c = idio_getc_handle (handle);
    }

    IDIO closedel = idio_S_nil;
    switch (c) {
    case IDIO_CHAR_LPAREN:
	closedel = idio_T_rparen;
	depth = IDIO_LIST_PAREN (depth + 1);
	break;
    case IDIO_CHAR_LBRACE:
	closedel = idio_T_rbrace;
	depth = IDIO_LIST_BRACE (depth + 1);
	break;
    case IDIO_CHAR_LBRACKET:
	/*
	 * Test Case: read-coverage/template-bracketing.idio
	 *
	 * #T[ 1 ]
	 */
	closedel = idio_T_rbracket;
	depth = IDIO_LIST_BRACKET (depth + 1);
	break;
    default:
	{
	    /*
	     * Can only get here if IDIO_OPEN_DELIMITER() doesn't
	     * match the case entries above
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "unexpected delimiter: %c (%#x)", c, c);

	    idio_read_error_template (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}
    }

    IDIO e = idio_read_block (handle, lo, closedel, interpc, depth);
    /*
     * idio_read_block has returned (block expr) and we only want expr
     *
     * Note that (block expr1 expr2+) means we need to wrap a begin
     * round expr1 expr2+ -- unlike quasiquote!
     */
    if (idio_S_nil == IDIO_PAIR_TT (e)) {
	IDIO r = IDIO_LIST2 (idio_S_quasiquote, IDIO_PAIR_HT (e));
	idio_meaning_copy_src_properties (e, r);

	return r;
    } else {
	IDIO ep = idio_list_append2 (IDIO_LIST1 (idio_S_begin),
				     IDIO_PAIR_T (e));

	IDIO r = IDIO_LIST2 (idio_S_quasiquote,
			     ep);
	idio_meaning_copy_src_properties (e, r);

	return r;
    }
}

static char const idio_read_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz"; /* base 36 is possible */
static char const idio_read_DIGITS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"; /* base 36 is possible */

/*
 * Annoyingly similar to reading in bignums, see
 * idio_read_bignum_radix, but used for escape sequences inside
 * strings where we (usually) limit the number of characters read in
 *
 * XXX How do we avoid, say, base=16 and lim=9 which will overflow a
 * uintmax_t?
 */
static uintmax_t idio_read_uintmax_radix (IDIO handle, IDIO lo, char basec, int radix, int lim)
{
    IDIO_ASSERT (handle);

    idio_unicode_t c = idio_getc_handle (handle);

    /*
     * EOF will be caught as no digits below which is a clearer error
     * message and handles more case
     */

    int max_base = sizeof (idio_read_digits) - 1;

    if (radix > max_base) {
	/*
	 * Shouldn't get here unless someone changes the parser to
	 * allow non-canonical radices, "\q1", say.
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "base #%c (%d) > max base %d", basec, radix, max_base);

	idio_read_error_integer (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	/* notreached */
	return 0;
    }

    uintmax_t base = radix;
    uintmax_t ui = 0;

    int ndigits = 0;
    while (! (IDIO_SEPARATOR (c) ||
	       (lim &&
		ndigits >= lim))) {
	if (idio_eofp_handle (handle)) {
	    /*
	     * Hmm, this gets picked up by EOF in a string
	     */
	    break;
	}

	int di = 0;
	while (idio_read_digits[di] &&
	       (idio_read_digits[di] != c &&
		idio_read_DIGITS[di] != c)) {
	    di++;
	}

	if (di >= radix) {
	    break;
	}

	ui *= base;
	ui += di;
	ndigits++;

	c = idio_getc_handle (handle);
    }

    if (0 == ndigits) {
	/*
	 * Test Case: read-errors/integer-no-digits.idio
	 *
	 * "\x"
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "no digits after integer base #%c", basec);

	idio_read_error_integer (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	/* notreached */
	return 0;
    }

    /*
     * We were ungetc'ing in EOF conditions -- which is confusing
     */
    if (idio_eofp_handle (handle) == 0) {
	idio_ungetc_handle (handle, c);
    }

    return ui;
}

static IDIO idio_read_bignum_radix (IDIO handle, IDIO lo, char basec, int radix)
{
    IDIO_ASSERT (handle);

    idio_unicode_t c = idio_getc_handle (handle);

    /*
     * EOF will be caught as no digits below which is a clearer error
     * message and handles more case
     */

    int neg = 0;

    switch (c) {
    case IDIO_CHAR_HYPHEN_MINUS:
	neg = 1;
	c = idio_getc_handle (handle);
	break;
    case IDIO_CHAR_PLUS_SIGN:
	c = idio_getc_handle (handle);
	break;
    }

    /*
     * EOF will be caught as no digits below which is a clearer error
     * message and handles more case
     */

    int max_base = sizeof (idio_read_digits) - 1;

    if (radix > max_base) {
	/*
	 * Shouldn't get here unless someone changes the parser to
	 * allow non-canonical radices, #A1, say.
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "base #%c (%d) > max base %d", basec, radix, max_base);

	idio_read_error_bignum (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	return idio_S_notreached;
    }

    IDIO base = idio_bignum_integer_intmax_t (radix);
    IDIO bn = idio_bignum_integer_intmax_t (0);

    int ndigits = 0;
    while (! IDIO_SEPARATOR (c)) {
	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-coverage/bignum-radix-sep-eof.idio
	     *
	     * #d1
	     *
	     * XXX no newline
	     */
	    break;
	}

	int di = 0;
	while (idio_read_digits[di] &&
	       (idio_read_digits[di] != c &&
		idio_read_DIGITS[di] != c)) {
	    di++;
	}

	if (di >= radix) {
	    /*
	     * Test Case: read-errors/bignum-invalid-digit.idio
	     *
	     * #d1a
	     *
	     * #d means a decimal number but 'a' is from a base-11 (or
	     * higher) format.  The above cannot mean #d1 (ie. 1)
	     * followed by the symbol 'a'.  Use whitespace if that's
	     * what you want.
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "invalid digit '%c' in bignum base #%c", c, basec);

	    idio_read_error_bignum (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}

	IDIO bn_i = idio_bignum_integer_intmax_t (di);

	bn = idio_bignum_multiply (bn, base);
	bn = idio_bignum_add (bn, bn_i);
	ndigits++;

	c = idio_getc_handle (handle);
    }

    if (0 == ndigits) {
	/*
	 * Test Case: read-errors/bignum-no-digits.idio
	 *
	 * #d
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "no digits after bignum base #%c", basec);

	idio_read_error_bignum (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	return idio_S_notreached;
    }

    /*
     * We were ungetc'ing in EOF conditions -- which is confusing
     */
    if (idio_eofp_handle (handle) == 0) {
	idio_ungetc_handle (handle, c);
    }

    if (neg) {
	bn = idio_bignum_negate (bn);
    }

    /* convert to a fixnum if possible */
    bn = idio_bignum_to_fixnum (bn);

    return bn;
}

/*
  Numbers in Scheme: http://docs.racket-lang.org/reference/reader.html#%28part._parse-number%29

  [+-]?[0-9]+
  [+-]?[0-9]*.[0-9]*
  [+-]?[0-9]*E[+-]?[0-9]+

 */

/* this is a port of string_numeric_p from S9fES */

#define IDIO_READ_BIGNUM_EXPONENT(c)	('d' == c || 'D' == c || \
					 'e' == c || 'E' == c || \
					 'f' == c || 'F' == c || \
					 'l' == c || 'L' == c || \
					 's' == c || 'S' == c)

static IDIO idio_read_number_C (IDIO handle, char const *str)
{
    char *s = (char *) str;
    int i = 0;

    /*
     * algorithm from Nils M Holm's Scheme 9 from Empty Space
     */

    int has_sign = 0;
    if (IDIO_CHAR_PLUS_SIGN == s[i] ||
	IDIO_CHAR_HYPHEN_MINUS == s[i]) {
	has_sign = 1;
	i++;
    }

    /* could be +/- function symbols */
    if (! s[i]) {
	return idio_S_nil;
    }

    int has_digit = 0;
    int has_period = 0;
    int has_exp = 0;
    int inexact = 0;

    for (; s[i]; i++) {
	if (IDIO_CHAR_HASH == s[i]) {
	    inexact = 1;
	}

	if (IDIO_READ_BIGNUM_EXPONENT (s[i]) &&
	    has_digit &&
	    ! has_exp) {
	    if (isdigit (s[i+1]) ||
		IDIO_CHAR_HASH == s[i+1]) {
		has_exp = 1;
	    } else if ((IDIO_CHAR_PLUS_SIGN == s[i+1] ||
			IDIO_CHAR_HYPHEN_MINUS == s[i+1]) &&
		       (isdigit (s[i+2]) ||
			IDIO_CHAR_HASH == s[i+2])) {
		has_exp = 1;

		/* extra i++ to skip the +/- next time round the
		   loop */
		i++;
	    } else {
		/*
		 * Test Case: read-coverage/numbers.idio
		 *
		 * 1eq
		 */
		return idio_S_nil;
	    }
	} else if (IDIO_CHAR_PERIOD == s[i] &&
		   ! has_period) {
	    has_period = 1;
	} else if (IDIO_CHAR_HASH == s[i] &&
		   (has_digit ||
		    has_period ||
		    has_sign)) {
	    has_digit = 1;
	} else if (isdigit (s[i])) {
	    has_digit = 1;
	} else {
	    return idio_S_nil;
	}
    }

    IDIO num = idio_S_nil;

    if (has_period ||
	has_exp ||
	inexact) {
	num = idio_bignum_C (str, i);
    } else {
	/*
	 * It might be possible to use a fixnum -- if it's small
	 * enough.
	 *
	 * log2(10) => 3.32 bits per decimal digit, we have (i-1)
	 * digits so multiple that by four for some rounding error.
	 *
	 * Compare to the number of available FIXNUM bits less a sign
	 * bit.
	 */
	if (((i - 1) * 4) < (((ssize_t) sizeof (intptr_t) * CHAR_BIT) - IDIO_TYPE_BITS - 1)) {
	    num = idio_fixnum_C (str, 10);
	    idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	} else {
	    /*
	     * Test Case: read-coverage/numbers.idio
	     *
	     * 12345678901234567890
	     */
	    num = idio_bignum_C (str, i);

	    /* convert to a fixnum if possible */
	    num = idio_bignum_to_fixnum (num);
	}
    }

    return num;
}

static IDIO idio_read_word (IDIO handle, IDIO lo, idio_unicode_t c, idio_unicode_t *ic)
{
    /*
     * +4 in case c is encoded in 4 bytes and we're on the cusp
     */
    char buf[IDIO_WORD_MAX_LEN + 4];
    int i = 0;

    int not_a_number = 0;

    for (;;) {
	int size;
	idio_utf8_code_point (c, buf + i, &size);
	i += size;

	if (i > IDIO_WORD_MAX_LEN) {
	    /*
	     * Test Case: read-errors/word-too-long.idio
	     *
	     * aaaa...aaaa
	     *
	     * (a very long word consisting of 'a's, you get the picture)
	     *
	     * Actually the test case has two words, one is
	     * IDIO_WORD_MAX_LEN chars long and the other is
	     * IDIO_WORD_MAX_LEN+1 chars long.  The first should not
	     * cause an error.
	     */
	    buf[IDIO_WORD_MAX_LEN] = '\0';
	    idio_read_error_parse_word_too_long (handle, lo, IDIO_C_FUNC_LOCATION (), buf);

	    return idio_S_notreached;
	}

	c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    break;
	}

	/*
	 * Hmm.  DOT is notionally a word separator (for value
	 * indexing) but it's seen in floating point numbers too...
	 * We need to spot the difference.
	 *
	 * Oh, and don't forget the symbol ... is used in
	 * syntax-rules.
	 *
	 * And .# is an inexact number constructor
	 *
	 * NB If we get a second DOT later in the "number" then
	 * idio_read_number_C() should fail and we'll fall through to
	 * the word separator clause.
	 *
	 * This means that if we were reading:
	 *
	 * var.index	- var is not a number => word separator
	 *
	 * 3.141	- 3 is a number => continue for 3.141
	 *
	 * var.3.141	- => var DOT 3.141
	 *		  var is indexed by the bignum 3.141
	 */
	if (IDIO_CHAR_DOT == c) {
	    buf[i] = '\0';

	    IDIO r = idio_read_number_C (handle, buf);

	    if (idio_S_nil != r) {
		continue;
	    } else {
		not_a_number = 1;
	    }

	    /*
	     * Remember, i will be >= 1 and c is effectively the
	     * lookahead char
	     *
	     * If the previous character was also DOT then this is a
	     * symbol, eg. ..., so continue reading characters.
	     */
	    if (IDIO_CHAR_DOT == buf[i-1]) {
		not_a_number = 1;
		continue;
	    }
	}

	if (c == ic[3]) {
	    c = idio_getc_handle (handle);
	    if (idio_eofp_handle (handle)) {
		/*
		 * Test Case: ?? was: read-errors/interpc-escape-eof.idio
		 *
		 * \
		 */
		idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF in escaped word");

		return idio_S_notreached;
	    }

	    continue;
	}

	if (IDIO_SEPARATOR (c)) {
	    idio_ungetc_handle (handle, c);
	    break;
	}

	/*
	 * What to do if we see an interpolation character in the
	 * word?
	 *
	 * ic[0] - unquote
	 * ic[1] - unquote-splicing
	 * ic[2] - quote
	 * ic[3] - escape -- handled above
	 *
	 * unquote-splicing should only occur after unquote so should
	 * be a generally allowed character in a word
	 *
	 * quote is more interesting as I can't see a case for quote
	 * mid-word where it is meant to quote the following
	 * expression
	 *
	 * unquote, however, might well occur mid-expression in a
	 * template for another language: PATH=!IDIOPATH:$PATH except
	 * that the reader doesn't know that the : terminates the
	 * word.  So, in practice, unquote is only useful at the start
	 * of a word and shouldn't break a word subsequently.
	 */
    }

    buf[i] = '\0';

    IDIO r = idio_S_nil;

    if (0 == not_a_number) {
	r = idio_read_number_C (handle, buf);
    }

    if (idio_S_nil == r) {
	/*
	 * Could be a symbol or a keyword.
	 *
	 * Awkwardly, := is a symbol.
	 *
	 * All keywords will be : followed by a non-punctutation char
	 */
	if (IDIO_CHAR_COLON == buf[0] &&
	    i > 1 &&
	    ! ispunct (buf[1])) {
	    r = idio_keywords_C_intern (buf + 1, i - 1);
	} else {
	    r = idio_symbols_C_intern (buf, i);
	}
    }

    return r;
}

/*
 * idio_read_1_expr_nl returns a lexical object
 */
static IDIO idio_read_1_expr_nl (IDIO handle, idio_unicode_t *ic, int depth, int return_nl)
{
    IDIO lo = idio_read_lexobj_from_handle (handle);

    idio_unicode_t c = idio_getc_handle (handle);

    /*
     * moved is representative of moving over whitespace, comments
     * before we reach our expression.  As such we should update the
     * lexical object with where we've moved to.
     */
    int moved = 0;
    for (;;) {
	if (moved) {
	    moved = 0;
	    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_LINE, idio_integer (IDIO_HANDLE_LINE (handle)));
	    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_POS, idio_integer (IDIO_HANDLE_POS (handle)));
	}

	/*
	 * Template interpolation character handling.  cf. quasiquote
	 * handling of , and ,@ and ' with added \
	 *
	 * ic[0] - unquote
	 * ic[1] - unquote-splicing
	 * ic[2] - quote
	 * ic[3] - escape operator handling: map \+ '(1 2 3) =!=> + map '(1 2 3)
	 */
	if (c == ic[0]) {
	    if (IDIO_LIST_QUASIQUOTE_P (depth)) {
		/*
		 * We should be disabling quaiquotation for the
		 * quasiquoted expression
		 */
		depth &= (~ IDIO_LIST_QUASIQUOTE_MARK);

		c = idio_getc_handle (handle);
		if (idio_eofp_handle (handle)) {
		    /*
		     * Test Case: read-errors/interpc-quasiquote-eof.idio
		     *
		     * #T{ $
		     */
		    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF in quasiquote");

		    return idio_S_notreached;
		}

		if (c == ic[1]) {
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_unquote_splicing (handle, lo, ic, depth + 1));
		    return lo;
		}

		/*
		 * It's not unquote-splicing so put c back.
		 */
		idio_ungetc_handle (handle, c);

		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_unquote (handle, lo, ic, depth + 1));
		return lo;
	    } else {
		/*
		 * ic[0] is nominally only good inside a quasiquoted
		 * expression so this must be a genuine word, eg. a
		 * submatch, '($ ...), from SRFI-115.
		 */
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, c, ic));
		return lo;
	    }
	} else if (c == ic[2]) {
	    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_quote (handle, lo, ic, IDIO_LIST_QUOTE (depth) + 1));
	    return lo;
	} else if (c == ic[3]) {
	    c = idio_getc_handle (handle);
	    if (idio_eofp_handle (handle)) {
		/*
		 * Test Case: read-errors/interpc-escape-eof.idio
		 *
		 * \
		 */
		idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF in escape");

		return idio_S_notreached;
	    }

	    switch (c) {
	    case IDIO_CHAR_CR:
	    case IDIO_CHAR_NL:
		idio_read_newline (handle, lo);
		break;
	    default:
		idio_ungetc_handle (handle, c);
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_escape (handle, lo, ic, depth));
		return lo;
	    }
	} else {

	    if (idio_eofp_handle (handle)) {
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_eof);
		return lo;
	    }

	    switch (c) {
	    case IDIO_CHAR_SPACE:
	    case IDIO_CHAR_TAB:
		idio_read_whitespace (handle, lo);
		moved = 1;
		break;
	    case IDIO_CHAR_CR:
	    case IDIO_CHAR_NL:
		if (0 == return_nl) {
		    idio_read_newline (handle, lo);
		}
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_eol);
		return lo;
	    case IDIO_CHAR_LPAREN:
		{
		    IDIO l = idio_read_list (handle, lo, idio_T_lparen, ic, IDIO_LIST_PAREN (depth) + 1);
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, l);
		    if (idio_S_nil != l &&
			IDIO_LIST_CONSTANT_P (depth) == 0) {
			idio_hash_put (idio_src_properties, l, lo);
		    }
		    return lo;
		}
	    case IDIO_CHAR_RPAREN:
		if (IDIO_LIST_PAREN_P (depth)) {
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_rparen);
		    return lo;
		} else {
		    /*
		     * Test Case: read-errors/unexpected-rparen.idio
		     *
		     * )
		     */
		    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "unexpected ')'");

		    return idio_S_notreached;
		}
		break;
	    case IDIO_CHAR_LBRACE:
	    {
		/*
		 * If we're looking at "{{..." then:
		 *
		 *   1. consume the inner block
		 *
		 *   2. look at the next character
		 *
		 *      if it is } then we've read in a "{{...}}"
		 *      subshell block
		 *
		 *      if it isn't then carry on reading the rest of
		 *      this block and insert the subshell
		 */
		IDIO subshell = idio_S_nil;
		if (IDIO_CHAR_LBRACE == idio_peek_handle (handle)) {
		    idio_getc_handle (handle);

		    subshell = idio_read_block (handle, lo, idio_T_rbrace, ic, IDIO_LIST_BRACE (depth + 1));

		    /* is this a } character? */
		    if (IDIO_CHAR_RBRACE == idio_peek_handle (handle)) {
			idio_getc_handle (handle);

			/* convert to a subshell */
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, IDIO_LIST2 (idio_S_subshell, subshell));
			idio_hash_put (idio_src_properties, subshell, lo);
			return lo;
		    }
		}

		IDIO block = idio_read_block (handle, lo, idio_T_rbrace, ic, IDIO_LIST_BRACE (depth + 1));

		if (idio_S_nil != subshell) {
		    /*
		     * subshell will be
		     *
		     * (block ...subshell...)
		     *
		     * and we want to join it and block such that we
		     * have
		     *
		     * (block (block ...subshell...) ...)
		     */
		    block = idio_pair (idio_S_block,
				       idio_list_append2 (IDIO_LIST1 (subshell),
							  IDIO_PAIR_T (block)));
		}
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, block);
		idio_hash_put (idio_src_properties, block, lo);
		return lo;
	    }
	    case IDIO_CHAR_RBRACE:
		if (IDIO_LIST_BRACE_P (depth)) {
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_rbrace);
		    return lo;
		} else {
		    /*
		     * Test Case: read-errors/unexpected-rbrace.idio
		     *
		     * }
		     */
		    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "unexpected '}'");

		    return idio_S_notreached;
		}
		break;
	    case IDIO_CHAR_LBRACKET:
	    {
		/*
		 * Test Case: read-coverage/bracket-block.idio
		 *
		 * '[ 1 2 ]
		 */
		IDIO block = idio_read_block (handle, lo, idio_T_rbracket, ic, IDIO_LIST_BRACKET (depth + 1));
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, block);
		idio_hash_put (idio_src_properties, block, lo);
		return lo;
	    }
	    case IDIO_CHAR_RBRACKET:
		if (IDIO_LIST_BRACKET_P (depth)) {
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_rbracket);
		    return lo;
		} else {
		    /*
		     * Test Case: read-errors/unexpected-rbracket.idio
		     *
		     * ]
		     */
		    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "unexpected ']'");

		    return idio_S_notreached;
		}
		break;
	    case IDIO_CHAR_DOT:
		{
		    /*
		     * We could be looking at
		     *
		     * - the ... symbol for syntax-rules.  certainly,
		     *   multiple consecutive DOTs are not an indexing
		     *   operation
		     *
		     * - ./ccc where we're unlikely to indexing
                     *   something by /ccc and probably mean a
                     *   filename, particularly a command name
		     */
		    idio_unicode_t c2 = idio_getc_handle (handle);
		    switch (c2) {
		    case IDIO_CHAR_DOT:
		    case IDIO_CHAR_SLASH:
			{
			    idio_ungetc_handle (handle, c2);
			    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, c, ic));
			    return lo;
			}
			break;
		    default:
			idio_ungetc_handle (handle, c2);
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_dot);
			return lo;
		    }
		}
		break;
	    case IDIO_CHAR_BACKQUOTE:
		{
		    /*
		     * This is a Scheme-ly quasiquote so use the
		     * Scheme quasiquote chars -- plus \
		     */
		    idio_unicode_t qq_ic[] = { IDIO_CHAR_COMMA, IDIO_CHAR_AT, IDIO_CHAR_SQUOTE, IDIO_CHAR_BACKSLASH };
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_quasiquote (handle, lo, qq_ic, IDIO_LIST_QUASIQUOTE (depth) + 1));
		    return lo;
		}
	    case IDIO_CHAR_HASH:
		{
		    idio_unicode_t c = idio_getc_handle (handle);
		    if (idio_eofp_handle (handle)) {
			/*
			 * Test Case: read-errors/hash-format-eof.idio
			 *
			 * #
			 */
			idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "#-format EOF");

			return idio_S_notreached;
		    }

		    switch (c) {
			/* constants */
		    case 'f':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_false);
			return lo;
		    case 't':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_true);
			return lo;
		    case 'n':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_nil);
			return lo;

			/* lists */
		    case IDIO_CHAR_BACKSLASH:
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_character (handle, lo, IDIO_READ_CHARACTER_EXTENDED));
			return lo;
		    case IDIO_CHAR_LBRACKET:
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_array (handle, lo, ic, depth + 1));
			return lo;
		    case IDIO_CHAR_LBRACE:
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_hash (handle, lo, ic, depth + 1));
			return lo;

			/* numbers and unicode */
		    case 'B':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_bitset (handle, lo, depth));
			return lo;
		    case 'b':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_bignum_radix (handle, lo, c, 2));
			return lo;
		    case 'd':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_bignum_radix (handle, lo, c, 10));
			return lo;
		    case 'o':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_bignum_radix (handle, lo, c, 8));
			return lo;
		    case 'x':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_bignum_radix (handle, lo, c, 16));
			return lo;
		    case 'U':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_unicode (handle, lo));
			return lo;
		    case 'e':
		    case 'i':
			{
			    int inexact = ('i' == c);
			    IDIO lo = idio_read_1_expr (handle, ic, depth);
			    IDIO bn = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);

			    /*
			     * If the input was #e0 or #i0 then
			     * idio_read_1_expr will return a fixnum
			     * for the 0
			     */
			    if (IDIO_TYPE_FIXNUMP (bn)) {
				if (0 == inexact) {
				    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, bn);
				    return lo;
				} else {
				    bn = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (bn));
				}
			    }

			    if (! idio_isa_bignum (bn)) {
				/*
				 * Test Cases:
				 * read-errors/exact-expected-number.idio
				 * read-errors/inexact-expected-number.idio
				 *
				 * #eq
				 * #iq
				 *
				 * 'q' is not a character expected in
				 * an exact or inexact number.
				 */
				char em[BUFSIZ];
				idio_snprintf (em, BUFSIZ, "number expected after #%c: got %s", inexact ? 'i' : 'e', idio_type2string (bn));

				idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

				return idio_S_notreached;
			    }

			    if (IDIO_BIGNUM_INTEGER_P (bn)) {
				if (! inexact) {
				    /*
				     * Test Case: read-coverage/bignum-integer.idio
				     *
				     * #e1000000000000000000 / #e1000000000
				     */
				    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, bn);
				    return lo;
				}

				/*
				 * Test Case: read-coverage/bignum-integer.idio
				 *
				 * #i-1000000000000000000 / #i-1000000000
				 */
				int flags = 0;
				if (idio_bignum_negative_p (bn)) {
				    flags |= IDIO_BIGNUM_FLAG_REAL_NEGATIVE;
				}

				bn = idio_bignum_abs (bn);

				bn = idio_bignum_real (flags, 0, IDIO_BIGNUM_SIG (bn));

				bn = idio_bignum_normalize (bn);
			    }

			    if (inexact) {
				IDIO_BIGNUM_FLAGS (bn) |= IDIO_BIGNUM_FLAG_REAL_INEXACT;
			    }

			    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, bn);
			    return lo;
			}
			break;

			/* structured forms */
		    case 'P':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_pathname (handle, lo, depth));
			return lo;
		    case 'S':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_istring (handle, lo, IDIO_LIST_STRING (depth) + 1));
			return lo;
		    case 'T':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_template (handle, lo, IDIO_LIST_QUASIQUOTE (depth) + 1));
			return lo;

			/* invalid */
		    case IDIO_CHAR_LANGLE:
			{
			    /*
			     * Test Case: read-errors/not-ready-for-hash-format.idio
			     *
			     * #<foo>
			     */
			    char em[BUFSIZ];
			    idio_snprintf (em, BUFSIZ, "not ready for '#%c' format", c);

			    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}

			/* comments */
		    case IDIO_CHAR_ASTERISK:
			idio_read_block_comment (handle, lo, depth);
			break;
		    case IDIO_CHAR_PIPE:
			idio_read_sl_block_comment (handle, lo, depth);
			break;
		    case IDIO_CHAR_SEMICOLON:
			idio_read_expr (handle);
			break;

			/* #! on first line */
		    case IDIO_CHAR_EXCLAMATION:
			if (1 == IDIO_HANDLE_LINE (handle) &&
			    2 == IDIO_HANDLE_POS (handle)) {
			    idio_read_line_comment (handle, lo, depth);
			    moved = 1;
			} else {
			    /*
			     * Test Case: read-errors/unexpected-hash-bang.idio
			     *
			     *  #! /usr/bin/env idio
			     *
			     * XXX First character of first line
			     */
			    char em[BUFSIZ];
			    idio_snprintf (em, BUFSIZ, "unexpected '#%c' format (# then %#02x)", c, c);

			    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}
			break;
		    default:
			{
			    /*
			     * Test Case: read-errors/unexpected-hash-format.idio
			     *
			     * #^foo
			     *
			     * XXX Of course we run the risk of
			     * someone introducing the #^ format for
			     * vital purposes...
			     */
			    char em[BUFSIZ];
			    idio_snprintf (em, BUFSIZ, "unexpected '#%c' format (# then %#02x)", c, c);

			    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}

		    }
		}
		break;
	    case IDIO_CHAR_PERCENT:
		{
		    idio_unicode_t c2 = idio_getc_handle (handle);
		    if (idio_eofp_handle (handle)) {
			/*
			 * Test Case: read-errors/percent-format-eof.idio
			 *
			 * %
			 */
			idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), "%-format EOF");

			return idio_S_notreached;
		    }

		    switch (c2) {
			/*
			 * This should be %O (U+004F LATIN CAPITAL
			 * LETTER O) but that's too easily confused
			 * with %0 (U+0030 DIGIT ZERO).
			 *
			 * On the other hand, I think that an
			 * octet_string is more obvious in intent than
			 * a byte_string.  YMMV
			 */
		    case 'B':
			{
			    idio_unicode_t odelim = idio_getc_handle (handle);
			    idio_unicode_t cdelim = odelim;
			    switch (odelim) {
			    case IDIO_CHAR_LPAREN:
				cdelim = IDIO_CHAR_RPAREN;
				break;
			    case IDIO_CHAR_LBRACE:
				cdelim = IDIO_CHAR_RBRACE;
				break;
			    case IDIO_CHAR_LBRACKET:
				cdelim = IDIO_CHAR_RBRACKET;
				break;
			    default:
				/* use odelim */
				break;
			    }
			    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_octet_string (handle, lo, cdelim, idio_default_string_ic));
			    return lo;
			}
		    case 'P':
			{
			    idio_unicode_t odelim = idio_getc_handle (handle);
			    idio_unicode_t cdelim = odelim;
			    switch (odelim) {
			    case IDIO_CHAR_LPAREN:
				cdelim = IDIO_CHAR_RPAREN;
				break;
			    case IDIO_CHAR_LBRACE:
				cdelim = IDIO_CHAR_RBRACE;
				break;
			    case IDIO_CHAR_LBRACKET:
				cdelim = IDIO_CHAR_RBRACKET;
				break;
			    default:
				/* use odelim */
				break;
			    }
			    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_path_string (handle, lo, cdelim, idio_default_string_ic));
			    return lo;
			}
		    default:
			idio_ungetc_handle (handle, c2);
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, c, ic));
			return lo;
		    }
		}
		break;
	    case IDIO_PAIR_SEPARATOR:
		{
		    int cp = idio_peek_handle (handle);

		    if (IDIO_SEPARATOR (cp)) {
			if (depth) {
			    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_pair_separator);
			    return lo;
			} else {
			    /*
			     * Test Case: read-errors/unexpected-pair-separator.idio
			     *
			     * &
			     */
			    char em[BUFSIZ];
			    idio_snprintf (em, BUFSIZ, "unexpected %c outside of list", IDIO_PAIR_SEPARATOR);

			    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}
		    }

		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, c, ic));
		    return lo;
		}
	    case IDIO_CHAR_SEMICOLON:
		idio_read_line_comment (handle, lo, depth);
		moved = 1;
		break;
	    case IDIO_CHAR_DQUOTE:
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_utf8_string (handle, lo, IDIO_CHAR_DQUOTE, idio_default_string_ic));
		return lo;
	    default:
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, c, ic));
		return lo;
	    }
	}

	c = idio_getc_handle (handle);
    }
}

static IDIO idio_read_1_expr (IDIO handle, idio_unicode_t *ic, int depth)
{
    IDIO lo = idio_read_1_expr_nl (handle, ic, depth, 0);
    return lo;
}

/*
 * Looping around for EOF/EOL/"}" means that a single expression
 * becomes "(expr)" so check to see if the collected list is one
 * element long and use only the head if so.
 *
 * idio_read_expr_line returns a pair: ({lexobj} & {reason}) where
 * {reason} is why the line was complete.
 *
 * The point being that a "line" could be terminated by an actual EOL
 * or EOF or the closing brace of a block.  Which some people care
 * about.
 */
static IDIO idio_read_expr_line (IDIO handle, IDIO closedel, idio_unicode_t *ic, int depth)
{
    IDIO line_lo = idio_read_lexobj_from_handle (handle);

    /*
     * The return expression
     */
    IDIO re = idio_S_nil;
    int count = 0;

    int skipped = 0;

    for (;;) {
	IDIO lo = idio_read_1_expr_nl (handle, ic, depth, 1);
	if (skipped) {
	    skipped = 0;
	    idio_struct_instance_set_direct (line_lo, IDIO_LEXOBJ_LINE, idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE));
	    idio_struct_instance_set_direct (line_lo, IDIO_LEXOBJ_POS, idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_POS));
	}
	IDIO expr = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);

	if (idio_S_eof == expr) {
	    if (idio_S_nil != re) {
		re = idio_list_nreverse (re);
		if (idio_S_nil == IDIO_PAIR_T (re)) {
		    re = IDIO_PAIR_H (re);
		} else {
		    if (! (IDIO_LIST_QUOTE_P (depth) ||
			   IDIO_LIST_CONSTANT_P (depth))) {
			re = idio_operator_expand (re, 0);
		    }
		    re = idio_read_unescape (re);
		    if (idio_isa_pair (re) &&
			IDIO_LIST_CONSTANT_P (depth) == 0) {
			IDIO re_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
			idio_struct_instance_set_direct (re_lo, IDIO_LEXOBJ_EXPR, re);
			idio_hash_put (idio_src_properties, re, re_lo);
		    }
		}
		idio_struct_instance_set_direct (line_lo, IDIO_LEXOBJ_EXPR, re);
		return idio_pair (line_lo, idio_S_eof);
	    } else {
		idio_struct_instance_set_direct (line_lo, IDIO_LEXOBJ_EXPR, idio_S_eof);
		return idio_pair (line_lo, idio_S_eof);
	    }
	} else if (idio_T_eol == expr) {
	    if (idio_S_nil != re) {
		re = idio_list_nreverse (re);
		if (idio_S_nil == IDIO_PAIR_T (re)) {
		    re = IDIO_PAIR_H (re);
		} else {
		    if (! (IDIO_LIST_QUOTE_P (depth) ||
		       IDIO_LIST_CONSTANT_P (depth))) {
			re = idio_operator_expand (re, 0);
		    }
		    re = idio_read_unescape (re);
		    if (idio_isa_pair (re) &&
			IDIO_LIST_CONSTANT_P (depth) == 0) {
			IDIO re_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
			idio_struct_instance_set_direct (re_lo, IDIO_LEXOBJ_EXPR, re);
			idio_hash_put (idio_src_properties, re, re_lo);
		    }
		}
		idio_struct_instance_set_direct (line_lo, IDIO_LEXOBJ_EXPR, re);
		return idio_pair (line_lo, idio_T_eol);
	    } else {
		/* blank line */
		skipped = 1;
	    }
	} else if (closedel == expr) {
	    if (idio_S_nil != re) {
		re = idio_list_nreverse (re);
		idio_hash_put (idio_src_properties, re, lo);
		if (idio_S_nil == IDIO_PAIR_T (re)) {
		    re = IDIO_PAIR_H (re);
		} else {
		    if (! (IDIO_LIST_QUOTE_P (depth) ||
		       IDIO_LIST_CONSTANT_P (depth))) {
			re = idio_operator_expand (re, 0);
		    }
		    re = idio_read_unescape (re);
		    if (idio_isa_pair (re) &&
			IDIO_LIST_CONSTANT_P (depth) == 0) {
			IDIO re_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
			idio_struct_instance_set_direct (re_lo, IDIO_LEXOBJ_EXPR, re);
			idio_hash_put (idio_src_properties, re, re_lo);
		    }
		}
		idio_struct_instance_set_direct (line_lo, IDIO_LEXOBJ_EXPR, re);
		return idio_pair (line_lo, closedel);
	    } else {
		idio_struct_instance_set_direct (line_lo, IDIO_LEXOBJ_EXPR, re);
		return idio_pair (line_lo, closedel);
	    }
	} else {

	    IDIO op = idio_infix_operatorp (expr);

	    if (idio_S_false != op) {
		/* ( ... {op} <EOL>
		 *   ... )
		 */

		/*
		 * An operator cannot be in functional position although
		 * several operators and functional names clash!  So, skip
		 * if it's the first element in the list.
		 */
		if (count > 0) {
		    IDIO lo = idio_read_1_expr (handle, ic, depth);
		    IDIO ne = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
		    while (idio_T_eol == ne) {
			lo = idio_read_1_expr (handle, ic, depth);
			ne = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
		    }

		    if (idio_eofp_handle (handle)) {
			/*
			 * Test Case: read-errors/op-eof.idio
			 *
			 * 1 +
			 *
			 * Nominally this is a dup of the one in
			 * idio_read_list()
			 */
			idio_read_error_list_eof (handle, lo, IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }

		    re = idio_pair (expr, re);
		    count++;
		    expr = ne;
		}
	    }

	    /*
	     * A few tokens can slip through the net...
	     */
	    if (IDIO_TYPE_CONSTANT_IDIOP (expr)) {
		uintptr_t ev = IDIO_CONSTANT_IDIO_VAL (expr);
		switch (ev) {
		case IDIO_CONSTANT_NIL:
		case IDIO_CONSTANT_UNDEF:
		case IDIO_CONSTANT_UNSPEC:
		case IDIO_CONSTANT_EOF:
		case IDIO_CONSTANT_TRUE:
		case IDIO_CONSTANT_FALSE:
		case IDIO_CONSTANT_VOID:
		case IDIO_CONSTANT_NAN:
		    break;
		default:
		    idio_coding_error_C ("unexpected constant in line", IDIO_LIST2 (handle, expr), IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    } else if (IDIO_TYPE_CONSTANT_TOKENP (expr)) {
		uintptr_t ev = IDIO_CONSTANT_TOKEN_VAL (expr);
		switch (ev) {
		case IDIO_TOKEN_DOT:
		    expr = idio_S_dot;
		    break;
		case IDIO_TOKEN_PAIR_SEPARATOR:
		    expr = idio_S_pair_separator;
		    break;
		default:
		    idio_debug ("expected %s as closing delimiter\n", closedel);
		    idio_coding_error_C ("unexpected token in line", IDIO_LIST2 (handle, expr), IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }

	    re = idio_pair (expr, re);
	}
	count++;
    }
}

/*
 *
 */
static IDIO idio_read_block (IDIO handle, IDIO lo, IDIO closedel, idio_unicode_t *ic, int depth)
{
    IDIO r = idio_S_nil;

    for (;;) {
	IDIO line_p = idio_read_expr_line (handle, closedel, ic, depth);
	IDIO line_lo = IDIO_PAIR_H (line_p);
	IDIO expr = idio_struct_instance_ref_direct (line_lo, IDIO_LEXOBJ_EXPR);
	IDIO reason = IDIO_PAIR_T (line_p);

	if (idio_isa_pair (expr) &&
	    IDIO_LIST_CONSTANT_P (depth) == 0) {
	    idio_hash_put (idio_src_properties, expr, line_lo);
	}

	if (idio_S_nil != expr) {
	    r = idio_pair (expr, r);
	}

	if (closedel == reason) {
	    r = idio_list_nreverse (r);

	    /*
	     * A few places will have used us to read a block in but
	     * not want the leading (block ...) so tag the result
	     * theyre looking for with line_lo.
	     */
	    if (idio_S_nil != r) {
		idio_hash_put (idio_src_properties, r, line_lo);
	    }

	    if (depth) {
		if (idio_S_nil == r) {
#ifdef IDIO_DEBUG
		    idio_debug ("NOTICE: idio_read_block: %s", idio_struct_instance_ref_direct (line_lo, IDIO_LEXOBJ_NAME));
		    idio_debug (":line %s", idio_struct_instance_ref_direct (line_lo, IDIO_LEXOBJ_LINE));
		    fprintf (stderr, ": empty body for block => void\n");
#endif
		    return idio_pair (idio_S_block, r);
		} else {
		    r = idio_list_append2 (IDIO_LIST1 (idio_S_block), r);
		    idio_hash_put (idio_src_properties, r, line_lo);
		    return r;
		}
	    } else {
		/*
		 * At the time of writing idio_read_block is always
		 * called with a >0 value for depth.
		 *
		 * Given that both clauses do the same thing...what
		 * was I thinking?
		 */
		idio_list_append2 (IDIO_LIST1 (idio_S_block), r);
		idio_hash_put (idio_src_properties, r, line_lo);
		return r;
	    }
	}
    }
}

/*
 * idio_read is file-handle.c's and load-handle's reader
 */
IDIO idio_read (IDIO handle)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    idio_gc_pause ("idio_read");

    /*
     * dummy value for closedel
     */
    IDIO line_p = idio_read_expr_line (handle, idio_T_eol, idio_default_template_ic, 0);
    IDIO line_lo = IDIO_PAIR_H (line_p);
    IDIO expr = idio_struct_instance_ref_direct (line_lo, IDIO_LEXOBJ_EXPR);
    if (idio_S_nil != expr) {
	idio_hash_put (idio_src_properties, expr, line_lo);
    }

    idio_gc_resume ("idio_read");

    return expr;
}

/*
 * Called by read-expr in handle.c
 */
IDIO idio_read_expr (IDIO handle)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    idio_gc_pause ("idio_read_expr");

    /*
     * There's an Idio/Scheme semantic cross-over here.  Should
     * reading an Idio expression fault at EOL?
     *
     * For the sake of the existing (S9) tests we'll hold to Scheme
     * semantics.
     */
    IDIO expr = idio_T_eol;
    while (idio_T_eol == expr) {
	IDIO lo = idio_read_1_expr (handle, idio_default_template_ic, 0);
	expr = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
	if (idio_S_eof == expr) {
	    break;
	}
    }

    idio_gc_resume ("idio_read_expr");

    return expr;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("read-number", read_number, (IDIO src, IDIO args), "src [radix]", "\
read a number from `src`				\n\
							\n\
:param str: source to read from				\n\
:type str: handle or string				\n\
:param radix: use radix, defaults to 10			\n\
:type radix: fixnum, optional				\n\
:return: number						\n\
							\n\
``read-number`` supports bases up to 36 for which the	\n\
normal hexadecimal digits are extended to all ASCII	\n\
``Alphabetic`` code points: ``0-9a-z`` and ``0-9A-Z``	\n\
")
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (args);

    IDIO handle = src;

    if (! idio_isa_handle (src)) {
	if (idio_isa_string (src)) {
	    size_t size = 0;
	    char *src_C = idio_string_as_C (src, &size);
	    /*
	     * XXX if src has ASCII NULs?
	     *
	     * Not sure what to do.
	     */
	    handle = idio_open_input_string_handle_C (src_C, size);

	    IDIO_GC_FREE (src_C, size);
	} else {
	    idio_error_param_type ("handle|string", src, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO lo = idio_read_lexobj_from_handle (handle);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    unsigned int radix = 10;

    if (idio_S_nil != args) {
	IDIO I_radix = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (I_radix)) {
	    radix = IDIO_FIXNUM_VAL (I_radix);

	    if (radix < 2) {
		char em[BUFSIZ];
		idio_snprintf (em, BUFSIZ, "radix %d < 2", radix);

		idio_read_error_bignum (handle, lo, IDIO_C_FUNC_LOCATION (), em);

		return idio_S_notreached;
	    }
	} else {
	    idio_error_param_type ("fixnum", I_radix, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return idio_read_bignum_radix (handle, lo, '?', radix);
}

char *idio_constant_token_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    char *r = NULL;
    char *t;

    intptr_t C_v = IDIO_CONSTANT_TOKEN_VAL (v);

    char m[BUFSIZ];

    switch (C_v) {

    case IDIO_TOKEN_DOT:                           t = "T/.";                         break;
    case IDIO_TOKEN_LPAREN:                        t = "T/(";                         break;
    case IDIO_TOKEN_RPAREN:                        t = "T/)";                         break;
    case IDIO_TOKEN_LBRACE:                        t = "T/{";                         break;
    case IDIO_TOKEN_RBRACE:                        t = "T/}";                         break;
    case IDIO_TOKEN_LBRACKET:                      t = "T/[";                         break;
    case IDIO_TOKEN_RBRACKET:                      t = "T/]";                         break;
    case IDIO_TOKEN_LANGLE:                        t = "T/<";                         break;
    case IDIO_TOKEN_RANGLE:                        t = "T/>";                         break;
    case IDIO_TOKEN_EOL:                           t = "T/EOL";                       break;
    case IDIO_TOKEN_PAIR_SEPARATOR:                t = "&";                           break;

    default:
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There should be a case clause above.
	 */
	idio_snprintf (m, BUFSIZ, "#<type/constant/token v=%10p VAL(v)=%#x>", v, C_v);
	idio_error_warning_message ("unhandled reader TOKEN: %s\n", m);
	t = m;
	break;
    }

    if (NULL == t) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There should be a case clause above.
	 */
	*sizep = idio_asprintf (&r, "#<TOKEN? %10p>", v);
    } else {
	*sizep = idio_asprintf (&r, "%s", t);
    }

    return r;
}

IDIO idio_constant_token_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_constant_token_as_C_string (v, sizep, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_read_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (read_number);
}

void idio_final_read ()
{
    idio_gc_remove_weak_object (idio_src_properties);
}

void idio_init_read ()
{
    idio_module_table_register (idio_read_add_primitives, idio_final_read, NULL);

    idio_read_reader_sym = IDIO_SYMBOLS_C_INTERN ("reader");
    idio_read_module = idio_module (idio_read_reader_sym);

    IDIO name = IDIO_SYMBOLS_C_INTERN ("%idio-lexical-object");
    idio_lexobj_type = idio_struct_type (name,
					 idio_S_nil,
					 idio_pair (IDIO_SYMBOLS_C_INTERN ("name"),
					 idio_pair (IDIO_SYMBOLS_C_INTERN ("line"),
					 idio_pair (IDIO_SYMBOLS_C_INTERN ("pos"),
					 idio_pair (IDIO_SYMBOLS_C_INTERN ("expr"),
					 idio_S_nil)))));
    idio_module_set_symbol_value (name, idio_lexobj_type, idio_Idio_module);

    idio_src_properties = IDIO_HASH_EQP (1024);
    idio_gc_add_weak_object (idio_src_properties);
    name = IDIO_SYMBOLS_C_INTERN ("%idio-src-properties");
    idio_module_set_symbol_value (name, idio_src_properties, idio_Idio_module);

    idio_read_bitset_buf_sh = idio_open_input_string_handle_C ("", 0);
    idio_gc_protect_auto (idio_read_bitset_buf_sh);
    idio_read_bitset_offset_sh = idio_open_input_string_handle_C ("", 0);
    idio_gc_protect_auto (idio_read_bitset_offset_sh);
    idio_read_bitset_end_sh = idio_open_input_string_handle_C ("", 0);
    idio_gc_protect_auto (idio_read_bitset_end_sh);

    idio_vtable_t *ct_vt = idio_vtable (IDIO_TYPE_CONSTANT_TOKEN);

    idio_vtable_add_method (ct_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_constant_token));

    idio_vtable_add_method (ct_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_constant_token_method_2string));
}

