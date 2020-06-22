/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

#include "idio.h"

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

/*
 * What separates words from one another in Idio?
 *
 * Whitespace - SPACE TAB NL CR
 *
 * start and end of lists LPAREN RPAREN
 *
 *	A(B)C	=>	A ( B ) C
 *
 * end of arrays RBRACKET
 *
 *	(start is handled by #[ -- HASH LBRACKET )
 *
 *	#[A]B	=>	#[ A ] B
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
				 IDIO_CHAR_RBRACKET == (c) ||		\
				 IDIO_CHAR_DOT == (c) ||		\
				 IDIO_CHAR_SEMICOLON == (c) ||		\
				 IDIO_CHAR_SQUOTE == (c) ||		\
				 IDIO_CHAR_BACKQUOTE == (c) ||		\
				 IDIO_CHAR_COMMA == (c) ||		\
				 IDIO_CHAR_DQUOTE == (c))

#define IDIO_OPEN_DELIMITER(c)	(IDIO_CHAR_LPAREN == (c) ||		\
				 IDIO_CHAR_LBRACE == (c) ||		\
				 IDIO_CHAR_LBRACKET == (c) ||		\
				 IDIO_CHAR_LANGLE == (c))

#define IDIO_LIST_DEPTH_MARK	(0xffff)
#define IDIO_LIST_PAREN_MARK	(1<<16)
#define IDIO_LIST_BRACE_MARK	(1<<17)
#define IDIO_LIST_BRACKET_MARK	(1<<18)
#define IDIO_LIST_ANGLE_MARK	(1<<19)

#define IDIO_LIST_PAREN(d)	(IDIO_LIST_PAREN_MARK | ((d) & IDIO_LIST_DEPTH_MARK))
#define IDIO_LIST_BRACE(d)	(IDIO_LIST_BRACE_MARK | ((d) & IDIO_LIST_DEPTH_MARK))
#define IDIO_LIST_BRACKET(d)	(IDIO_LIST_BRACKET_MARK | ((d) & IDIO_LIST_DEPTH_MARK))
#define IDIO_LIST_ANGLE(d)	(IDIO_LIST_ANGLE_MARK | ((d) & IDIO_LIST_DEPTH_MARK))

#define IDIO_LIST_PAREN_P(d)	(((d) & IDIO_LIST_PAREN_MARK) && ((d) & IDIO_LIST_DEPTH_MARK))
#define IDIO_LIST_BRACE_P(d)	(((d) & IDIO_LIST_BRACE_MARK) && ((d) & IDIO_LIST_DEPTH_MARK))
#define IDIO_LIST_BRACKET_P(d)	(((d) & IDIO_LIST_BRACKET_MARK) && ((d) & IDIO_LIST_DEPTH_MARK))
#define IDIO_LIST_ANGLE_P(d)	(((d) & IDIO_LIST_ANGLE_MARK) && ((d) & IDIO_LIST_DEPTH_MARK))

/*
 * Default interpolation characters:
 *
 * IDIO_CHAR_DOT == use default (ie. skip) => IDIO_CHAR_DOT cannot be
 * one of the interpolation characters
 *
 * 1. expression substitution == unquote
 * 2. expression splicing == unquotesplicing
 * 3. expression quoting
 * 4. escape char to prevent operator handling
 */
char idio_default_interpolation_chars[] = { IDIO_CHAR_DOLLARS, IDIO_CHAR_AT, IDIO_CHAR_SQUOTE, IDIO_CHAR_BACKSLASH };
#define IDIO_INTERPOLATION_CHARS 4

/*
 * In the case of named characters, eg. #\newline (as opposed to #\a,
 * the character 'a') what is the longest name (eg, "newline") we
 * should look out for.  Otherwise we'll read in
 * #\supercalifragilisticexpialidocious which is fine except that I
 * don't know what character that is.

 * That said, there's no reason why we shouldn't be able to use
 * Unicode named characters.  What's the longest one of those?
 * According to http://www.unicode.org/charts/charindex.html and
 * turning non-printing chars into underscores, say, then "Aboriginal
 * Syllabics Extended, Unified Canadian" is some 47 chars long.  The
 * longest is 52 chars ("Digraphs Matching Serbian Cyrillic Letters,
 * Croatian, 01C4").

 * In the meanwhile, we only have a handler for "space" and
 * "newline"...
 */
#define IDIO_CHARACTER_MAX_NAME_LEN	10
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
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (c_location, sh);
    idio_display_C (": reached line ", sh);
    idio_display (idio_integer (IDIO_HANDLE_LINE (handle)), sh);
    idio_display_C (": pos ", sh);
    idio_display (idio_integer (IDIO_HANDLE_POS (handle)), sh);

    detail = idio_get_output_string (sh);
#endif

    IDIO c = idio_struct_instance (idio_condition_read_error_type,
				   IDIO_LIST5 (msg,
					       idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME),
					       detail,
					       idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE),
					       idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_POS)));
    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

static void idio_read_error_parse (IDIO handle, IDIO lo, IDIO c_location, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));
}

static void idio_read_error_parse_args (IDIO handle, IDIO lo, IDIO c_location, char *msg, IDIO args)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);
    idio_display (args, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_parse_printf (IDIO handle, IDIO lo, IDIO c_location, char *fmt, ...)
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

static void idio_read_error_parse_word_too_long (IDIO handle, IDIO lo, IDIO c_location, char *word)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (word);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("word is too long: '", sh);
    idio_display_C (word, sh);
    idio_display_C ("'", sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));
}

static void idio_read_error_list_eof (IDIO handle, IDIO lo, IDIO c_location)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("EOF in list", sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_pair_separator (IDIO handle, IDIO lo, IDIO c_location, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_comment (IDIO handle, IDIO lo, IDIO c_location, char *e_msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (e_msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("comment: ", sh);
    idio_display_C (e_msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_string (IDIO handle, IDIO lo, IDIO c_location, char *e_msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (e_msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("string: ", sh);
    idio_display_C (e_msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_named_character (IDIO handle, IDIO lo, IDIO c_location, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("named character: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_named_character_unknown_name (IDIO handle, IDIO lo, IDIO c_location, char *name)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (name);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("unknown named character: ", sh);
    idio_display_C (name, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_template (IDIO handle, IDIO lo, IDIO c_location, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("template: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_pathname (IDIO handle, IDIO lo, IDIO c_location, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("pathname: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_bignum (IDIO handle, IDIO lo, IDIO c_location, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("bignum: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static void idio_read_error_utf8_decode (IDIO handle, IDIO lo, IDIO c_location, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("UTF-8 decode: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, lo, c_location, idio_get_output_string (sh));

    /* notreached */
}

static IDIO idio_read_1_expr (IDIO handle, char *ic, int depth);
static IDIO idio_read_block (IDIO handle, IDIO lo, IDIO closedel, char *ic, int depth);

static void idio_read_whitespace (IDIO handle)
{
    for (;;) {
	int c = idio_getc_handle (handle);

	switch (c) {
	case EOF:
	    return;
	case IDIO_CHAR_SPACE:
	case IDIO_CHAR_TAB:
	    break;
	default:
	    idio_ungetc_handle (handle, c);
	    return;
	}
    }
}

static void idio_read_newline (IDIO handle)
{
    for (;;) {
	int c = idio_getc_handle (handle);

	switch (c) {
	case EOF:
	    return;
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
static IDIO idio_read_list (IDIO handle, IDIO list_lo, IDIO opendel, char *ic, int depth)
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
		sprintf (em, "nothing before %c in list", IDIO_PAIR_SEPARATOR);
		idio_read_error_pair_separator (handle, lo, IDIO_C_FUNC_LOCATION (), em);

		return idio_S_notreached;
	    }

	    /*
	     * XXX should only expect a single expr after
	     * IDIO_PAIR_SEPARATOR, ie. not a list: (a & b c)
	     */
	    IDIO lo = idio_read_1_expr (handle, ic, depth);
	    IDIO pt = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
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
		sprintf (em, "nothing after %c in list", IDIO_PAIR_SEPARATOR);
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
		sprintf (em, "more than one expression after %c in list", IDIO_PAIR_SEPARATOR);
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
		r = idio_list_reverse (r);
		if (idio_isa_pair (r)) {
		    IDIO r_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
		    idio_struct_instance_set_direct (r_lo, IDIO_LEXOBJ_EXPR, r);
		    idio_hash_put (idio_src_properties, r, r_lo);
		}
		r = idio_operator_expand (r, 0);
		if (idio_isa_pair (r)) {
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
		    idio_error_C ("unexpected token in list", IDIO_LIST2 (handle, e), IDIO_C_FUNC_LOCATION ());

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
		    idio_error_C ("unexpected token in list", IDIO_LIST2 (handle, e), IDIO_C_FUNC_LOCATION ());

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

static IDIO idio_read_quote (IDIO handle, IDIO lo, char *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO qlo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (qlo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_quote, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_quasiquote (IDIO handle, IDIO lo, char *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO qqlo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (qqlo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_quasiquote, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_unquote_splicing (IDIO handle, IDIO lo, char *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO uslo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (uslo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_unquotesplicing, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_unquote (IDIO handle, IDIO lo, char *ic, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO uqlo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (uqlo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_unquote, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static IDIO idio_read_escape (IDIO handle, IDIO lo, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO elo = idio_read_1_expr (handle, ic, depth);
    IDIO e = idio_struct_instance_ref_direct (elo, IDIO_LEXOBJ_EXPR);
    IDIO r = IDIO_LIST2 (idio_S_escape, e);
    idio_meaning_copy_src_properties (e, r);

    return r;
}

static void idio_read_line_comment (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    for (;;) {
	int c = idio_getc_handle (handle);

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

/*
 * Block comments #| ... |# can be nested!
 *
 * #|
 * zero, one
 * #|
 * or more lines
 * |#
 * nested
 * |#
 *
 * You can also change the default escape char, \, using the first
 * char after #|
 */
static void idio_read_block_comment (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (lo);

    IDIO_TYPE_ASSERT (handle, handle);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    char esc_char = IDIO_CHAR_BACKSLASH;
    int esc = 0;
    int pipe_esc = 0;
    int hash_esc = 0;
    
    int c = idio_getc_handle (handle);

    if (idio_eofp_handle (handle)) {
	/*
	 * Test Case: read-error/block-comment-initial-eof.idio
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
	     * Test Case: read-coverage/block-comment-eof.idio
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
		    idio_read_block_comment (handle, lo, depth + 1);
		}
	    } else {
		switch (c) {
		    /*
		     * Test Case: read-coverage/block-comment-escaped-pipe.idio
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
 * Block comments #| ... |# can be nested!
 *
 * If the opening #| is followed by whitespace then an isgraph() word
 * and the handle is a file-handle then that word is used as an
 * extension into which the text of the comment is appended.  The
 * remaining text on the line is ignored.
 *
 * #| .rst
 * ..note this is ReStructuredText
 * #|
 *
 */
static void idio_read_sl_block_comment (IDIO handle, int depth)
{
    IDIO_ASSERT (handle);

    char ext[BUFSIZ];
    char *e = ext;
    int ext_possible = 0;
    int ext_leading_ws = 1;
    char comment_file_name[BUFSIZ];
    FILE *comment_file = NULL;
    int write_comment = 0;
    
    if (idio_isa_file_handle (handle)) {
	/*
	 * Exclude ext for stdin/out/err
	 */
	idio_file_handle_stream_t *fhsp = (idio_file_handle_stream_t *) IDIO_HANDLE_STREAM (handle);
	int s_flags = IDIO_FILE_HANDLE_STREAM_FLAGS (fhsp);

	if ((s_flags & IDIO_FILE_HANDLE_FLAG_STDIO) == 0) {
	    ext_possible = 1;
	}
    }

    int pipe_char = 0;
    
    for (;;) {
	int c = idio_getc_handle (handle);

	if (idio_eofp_handle (handle)) {
	    /*
	     * Test Case: read-coverage/block-comment-eof.idio
	     *
	     * ;; no newline!
	     */
	    fprintf (stderr, "read-block-comment: EOF in comment\n");
	    if (NULL != comment_file) {
		if (fclose (comment_file)) {
		    fprintf (stderr, "fclose (%s): %s\n", comment_file_name, strerror (errno));
		}
	    }
	    return;
	}

	switch (c) {
	case IDIO_CHAR_PIPE:
	    pipe_char = 1;
	    continue;
	case IDIO_CHAR_HASH:
	    if (pipe_char) {
		if (NULL != comment_file) {
		    if (fclose (comment_file)) {
			fprintf (stderr, "fclose (%s): %s\n", comment_file_name, strerror (errno));
		    }
		}
		return;
	    }
	    break;
	case IDIO_CHAR_NL:
	    if (NULL != comment_file) {
		write_comment = 1;
	    }
	}

	if (ext_possible) {
	    if (isgraph (c)) {
		ext_leading_ws = 0;
		if (e == ext) {
		    if (IDIO_CHAR_DOT != c)  {
			fprintf (stderr, "(!^.) ext invalidated by %c\n", c);
			ext_possible = 0;
			ext_leading_ws = 0;
			ext[0] = '\0';
			continue;
		    }
		}

		*e = c;
		e++;
	    } else if (isspace (c) ||
		       IDIO_CHAR_NL == c) {
		if (ext_leading_ws) {
		} else {
		    *e = '\0';
		    ext_possible = 0;
		    if (strlen (ext)) {
			fprintf (stderr, "f-h ext is %s\n", ext);
			IDIO filename_I = IDIO_HANDLE_PATHNAME (handle);

			if (idio_S_nil != filename_I) {
			    strcpy (comment_file_name, idio_string_s (filename_I));
			    strcat (comment_file_name, ext);

			    comment_file = fopen (comment_file_name, "a");
			    if (NULL == comment_file) {
				fprintf (stderr, "fopen (%s): %s\n", comment_file_name, strerror (errno));
			    }

			    if (IDIO_CHAR_NL == c) {
				write_comment = 1;
			    }
	  		} else {    
 		 	    idio_debug ("empty PATHNAME for %s\n", handle);
			}
		    }
		}
	    } else {
		fprintf (stderr, "(any) ext invalidated by %c\n", c);
		ext_possible = 0;
		ext_leading_ws = 0;
		ext[0] = '\0';
	    }
	}

	if (pipe_char) {
	    pipe_char = 0;
	}

	if (write_comment) {
	    fputc (c, comment_file);
	}
    }
}

/*
 * idio_read_string returns the string -- not a lexical object.
 */
static IDIO idio_read_string (IDIO handle, IDIO lo)
{
    IDIO_ASSERT (handle);

    /*
     * In a feeble attempt to reduce fragmentation we can imagine
     * allocating reasonable sized chunks of memory aligned to 2**n
     * boundaries.  However, malloc(3) seems to use a couple of
     * pointers so any individual allocation length (alen) should be
     * docked that amount
     */

#define IDIO_STRING_CHUNK_LEN	1<<1
    int nchunks = 1;
    size_t alen = (nchunks * IDIO_STRING_CHUNK_LEN); /* - 2 * sizeof (void *); */
    char *buf = idio_alloc (alen);
    size_t slen = 0;

    int done = 0;
    int esc = 0;

    while (! done) {
	int c = idio_getc_handle (handle);

	if (EOF == c) {
	    /*
	     * Test Case: read-errors/string-unterminated.idio
	     *
	     * "
	     */
	    idio_read_error_string (handle, lo, IDIO_C_FUNC_LOCATION (), "unterminated");

	    return idio_S_notreached;
	}

	switch (c) {
	case IDIO_CHAR_DQUOTE:
	    if (esc) {
		buf[slen++] = c;
	    } else {
		done = 1;
	    }
	    break;
	case IDIO_CHAR_BACKSLASH:
	    if (esc) {
		buf[slen++] = c;
	    } else {
		esc = 1;
		continue;
	    }
	    break;
	default:
	    if (esc) {
		switch (c) {
		    /*
		     * Test Case: read-coverage/string-escaped-characters.idio
		     *
		     * "\a\b..."
		     */
		case 'a': c = '\a'; break; /* alarm (bell) */
		case 'b': c = '\b'; break; /* backspace */
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
		    /* idio_read_hex2 */
		    break;

		    /* \u hhhh UTF-16 */
		    /* \e ESC (\x1B) GCC extension */

		default:
		    /* leave alone */
		    break;
		}
	    }
	    buf[slen++] = c;
	    break;
	}

	if (esc) {
	    esc = 0;
	}

	if (slen >= alen) {
	    alen += IDIO_STRING_CHUNK_LEN;
	    buf = idio_realloc (buf, alen);
	}
    }

    buf[slen] = '\0';

    IDIO r = idio_string_C (buf);

    free (buf);

    return r;
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
    intptr_t c;

    for (i = 0 ; i < IDIO_CHARACTER_MAX_NAME_LEN; i++) {
	c = idio_getc_handle (handle);

	if (EOF == c) {
	    /*
	     * Test Case: read-errors/named-character-eof.idio
	     *
	     * #\
	     */
	    idio_read_error_named_character (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	}

	/* first char could be a non-alpha, eg. #\( so that's not a
	   reason to break out of the loop but after that all
	   characters in the name must be alpha (until we choose to
	   handle Unicode names etc.)
	*/
	if (i &&
	    ! isalpha (c)) {
	    break;
	}

	buf[i] = c;
    }

    idio_ungetc_handle (handle, c);
    buf[i] = '\0';

    IDIO r;

    if (0 == i) {
	/*
	 * Can i==0 happen? Can't be EOF as that's picked up by
	 * read-errors/named-character-eof.idio
	 *
	 * Test Case: ??
	 *
	 */
	idio_read_error_named_character (handle, lo, IDIO_C_FUNC_LOCATION (), "no letters in character name?");

	return idio_S_notreached;
    } else if (1 == i) {
	r = IDIO_CHARACTER (buf[0]);
    } else {
	r = idio_character_lookup (buf);

	if (r == idio_S_unspec) {
	    /*
	     * Test Case: read-errors/named-character-unknown.idio
	     *
	     * #\caveat
	     *
	     * XXX This is a bit tricky as (at the time of writing)
	     * we're limited to isalpha() chars so no underscores or
	     * colons or things that (gensym) might create which would
	     * prevent someone from accidentally introducing #\caveat
	     * as a real named character.
	     */
	    idio_read_error_named_character_unknown_name (handle, lo, IDIO_C_FUNC_LOCATION (), buf);

	    return idio_S_notreached;
	}
    }

    idio_gc_stats_inc (IDIO_TYPE_CONSTANT_CHARACTER);
    return r;
}

/*
 * idio_read_array returns the array -- not a lexical object.
 */
static IDIO idio_read_array (IDIO handle, IDIO lo, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO l = idio_read_list (handle, lo, idio_T_lbracket, ic, IDIO_LIST_BRACKET (depth + 1));
    return idio_list_to_array (l);
}

/*
 * idio_read_hash returns the hash -- not a lexical object.
 */
static IDIO idio_read_hash (IDIO handle, IDIO lo, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO l = idio_read_list (handle, lo, idio_T_lbrace, ic, IDIO_LIST_BRACE (depth + 1));
    return idio_hash_alist_to_hash (l, idio_S_nil);
}

static IDIO idio_read_template (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    char interpc[IDIO_INTERPOLATION_CHARS];
    for (i = 0; i < IDIO_INTERPOLATION_CHARS; i++) {
	interpc[i] = idio_default_interpolation_chars[i];
    }
    i = 0;

    int c = idio_getc_handle (handle);

    while (! IDIO_OPEN_DELIMITER (c)) {
	if (i >= IDIO_INTERPOLATION_CHARS) {
	    /*
	     * Test Case: read-errors/template-too-many-ic.idio
	     *
	     * #T*&^%${ 1 }
	     */
	    char em[BUFSIZ];
	    sprintf (em, "too many interpolation characters: #%d: %c (%#x)", i + 1, c, c);
	    idio_read_error_template (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}

	switch (c) {
	case EOF:
	    /*
	     * Test Case: read-errors/template-eof.idio
	     *
	     * #T
	     */
	    idio_read_error_template (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	default:
	    if (IDIO_CHAR_DOT != c) {
		interpc[i] = c;
	    }
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
    case IDIO_CHAR_LANGLE:
	/*
	 * Test Case: read-coverage/template-bracketing.idio
	 *
	 * #T< 1 >
	 *
	 * XXX This case has been removed as the > is interpreted as
	 * an operator and we get an EOF error
	 */
	closedel = idio_T_rangle;
	depth = IDIO_LIST_ANGLE (depth + 1);
	break;
    default:
	{
	    /*
	     * Can only get here if IDIO_OPEN_DELIMITER() doesn't
	     * match the case entries above
	     */
	    char em[BUFSIZ];
	    sprintf (em, "unexpected delimiter: %c (%#x)", c, c);
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
	idio_meaning_copy_src_properties (IDIO_PAIR_HT (e), r);

	return r;
    } else {
	IDIO ep = idio_list_append2 (IDIO_LIST1 (idio_S_begin),
				     IDIO_PAIR_T (e));
	idio_meaning_copy_src_properties (IDIO_PAIR_HT (e), ep);

	IDIO r = IDIO_LIST2 (idio_S_quasiquote,
			     ep);
	idio_meaning_copy_src_properties (IDIO_PAIR_HT (e), r);

	return r;
    }
}

static IDIO idio_read_pathname (IDIO handle, IDIO lo, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    char interpc[IDIO_INTERPOLATION_CHARS];
    for (i = 0; i < IDIO_INTERPOLATION_CHARS; i++) {
	interpc[i] = idio_default_interpolation_chars[i];
    }
    i = 0;

    int c = idio_getc_handle (handle);

    while (IDIO_CHAR_DQUOTE != c) {
	if (i >= IDIO_INTERPOLATION_CHARS) {
	    /*
	     * Test Case: read-errors/pathname-too-many-ic.idio
	     *
	     * #P*&^%$" * "
	     */
	    char em[BUFSIZ];
	    sprintf (em, "too many interpolation characters: #%d: %c (%#x)", i + 1, c, c);
	    idio_read_error_pathname (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}

	switch (c) {
	case EOF:
	    /*
	     * Test Case: read-errors/pathname-eof.idio
	     *
	     * #P
	     */
	    idio_read_error_pathname (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

	    return idio_S_notreached;
	default:
	    if (IDIO_CHAR_DOT != c) {
		interpc[i] = c;
	    }
	}

	i++;
	c = idio_getc_handle (handle);
    }

    /*
     * Test Case: read-coverage/pathname.idio
     *
     * struct-instance? #P" *.c "
     */
    IDIO e = idio_read_string (handle, lo);

    return idio_struct_instance (idio_path_type, IDIO_LIST1 (e));
}

static IDIO idio_read_bignum_radix (IDIO handle, IDIO lo, char basec, int radix)
{
    IDIO_ASSERT (handle);

    int c = idio_getc_handle (handle);

    int neg = 0;

    switch (c) {
    case '-':
	neg = 1;
	c = idio_getc_handle (handle);
	break;
    case '+':
	c = idio_getc_handle (handle);
	break;
    }

    char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz"; /* base 36 is possible */
    int max_base = strlen (digits);

    if (radix > max_base) {
	/*
	 * Shouldn't get here unless someone changes the parser to
	 * allow non-canonical radices, #A1, say.
	 */
	char em[BUFSIZ];
	sprintf (em, "base #%c (%d) > max base %d", basec, radix, max_base);
	idio_read_error_bignum (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	return idio_S_notreached;
    }

    IDIO base = idio_bignum_integer_intmax_t (radix);
    IDIO bn = idio_bignum_integer_intmax_t (0);

    int ndigits = 0;
    int i;
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

	i = 0;
	while (digits[i] &&
	       digits[i] != c) {
	    i++;
	}

	if (i >= radix) {
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
	    sprintf (em, "invalid digit %c in bignum base #%c", c, basec);
	    idio_read_error_bignum (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	    return idio_S_notreached;
	}

	IDIO bn_i = idio_bignum_integer_intmax_t (i);

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
	sprintf (em, "no digits after bignum base #%c", basec);
	idio_read_error_bignum (handle, lo, IDIO_C_FUNC_LOCATION (), em);

	return idio_S_notreached;
    }

    idio_ungetc_handle (handle, c);

    if (neg) {
	bn = idio_bignum_negate (bn);
    }

    /* convert to a fixnum if possible */
    IDIO fn = idio_bignum_to_fixnum (bn);
    if (idio_S_nil != fn) {
	bn = fn;
    }

    return bn;
}

/*
  Numbers in Scheme: http://docs.racket-lang.org/reference/reader.html#%28part._parse-number%29

  [+-]?[0-9]+
  [+-]?[0-9]*.[0-9]*
  [+-]?[0-9]*E[+-]?[0-9]+

 */

/* this is a port of string_numeric_p from S9fES */

#define EXPONENT(c)	('d' == c || 'D' == c || \
			 'e' == c || 'E' == c || \
			 'f' == c || 'F' == c || \
			 'l' == c || 'L' == c || \
			 's' == c || 'S' == c)

static IDIO idio_read_number_C (IDIO handle, char *str)
{
    char *s = str;
    int i = 0;

    /*
     * algorithm from Nils M Holm's Scheme 9 from Empty Space
     */

    int has_sign = 0;
    if ('+' == s[i] ||
	'-' == s[i]) {
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
	if ('#' == s[i]) {
	    inexact = 1;
	}

	if (EXPONENT (s[i]) &&
	    has_digit &&
	    ! has_exp) {
	    if (isdigit (s[i+1]) ||
		'#' == s[i+1]) {
		has_exp = 1;
	    } else if (('+' == s[i+1] ||
			'-' == s[i+1]) &&
		       (isdigit (s[i+2]) ||
			'#' == s[i+2])) {
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
	} else if ('.' == s[i] &&
		   ! has_period) {
	    has_period = 1;
	} else if ('#' == s[i] &&
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
	num = idio_bignum_C (str);
    } else {
	/*
	 * It might be possible to use a fixnum -- if it's small
	 * enough.
	 *
	 * log2(10) => 3.22 bits per decimal digit, we have (i-1)
	 * digits so multiple that by four for some rounding error.
	 */
	if (((i - 1) * 4) < ((sizeof (intptr_t) * 8) - 2)) {
	    num = idio_fixnum_C (str, 10);
	    idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	} else {
	    /*
	     * Test Case: read-coverage/numbers.idio
	     *
	     * 12345678901234567890
	     */
	    num = idio_bignum_C (str);

	    /* convert to a fixnum if possible */
	    IDIO fn = idio_bignum_to_fixnum (num);
	    if (idio_S_nil != fn) {
		/*
		 * Test Case: read-coverage/numbers.idio
		 *
		 * 64 / 32 bit bignum segment max digits
		 *
		 * 100000000000000000 / 100000000
		 */
		num = fn;
	    }
	}
    }

    return num;
}

static IDIO idio_read_word (IDIO handle, IDIO lo, int c)
{
    char buf[IDIO_WORD_MAX_LEN + 1];
    int i = 0;

    for (;;) {
	buf[i++] = c;

	if (i > IDIO_WORD_MAX_LEN) {
	    /*
	     * Test Case: read-errors/word-too-long.idio
	     *
	     * aaaa...aaaa
	     *
	     * (a very long word consisting of 'a's, you get the picture)
	     *
	     * Actually the test case has two words, one is
	     * IDIO_WORD_MAX_LEN chars long and the other os
	     * IDIO_WORD_MAX_LEN+1 chars long.  The first should not
	     * cause an error.
	     */
	    buf[IDIO_WORD_MAX_LEN] = '\0';
	    idio_read_error_parse_word_too_long (handle, lo, IDIO_C_FUNC_LOCATION (), buf);

	    return idio_S_notreached;
	}

	c = idio_getc_handle (handle);

	if (EOF == c) {
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
	    }

	    /*
	     * Remember, i will be >= 1 and c is effectively the
	     * lookahead char
	     *
	     * If the previous charcater was also DOT then this is a
	     * symbol, eg. ..., so continue reading characters.
	     */
	    if (IDIO_CHAR_DOT == buf[i-1]) {
		continue;
	    }
	}

	if (IDIO_SEPARATOR (c)) {
	    idio_ungetc_handle (handle, c);
	    break;
	}
    }

    buf[i] = '\0';

    IDIO r = idio_read_number_C (handle, buf);

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
	    r = idio_keywords_C_intern (buf + 1);
	} else {
	    r = idio_symbols_C_intern (buf);
	}
    }

    return r;
}

/*
 * idio_read_1_expr_nl returns a lexical object
 */
static IDIO idio_read_1_expr_nl (IDIO handle, char *ic, int depth, int return_nl)
{
    IDIO lo = idio_struct_instance (idio_lexobj_type,
				    idio_pair (IDIO_HANDLE_FILENAME (handle),
				    idio_pair (idio_integer (IDIO_HANDLE_LINE (handle)),
				    idio_pair (idio_integer (IDIO_HANDLE_POS (handle)),
				    idio_pair (idio_S_unspec,
				    idio_S_nil)))));

    int c = idio_getc_handle (handle);

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
	    c = idio_getc_handle (handle);
	    if (c == ic[1]) {
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_unquote_splicing (handle, lo, ic, depth));
		return lo;
	    }
	    idio_ungetc_handle (handle, c);
	    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_unquote (handle, lo, ic, depth));
	    return lo;
	} else if (c == ic[2]) {
	    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_quote (handle, lo, ic, depth));
	    return lo;
	} else if (c == ic[3]) {
	    c = idio_getc_handle (handle);
	    switch (c) {
	    case IDIO_CHAR_CR:
	    case IDIO_CHAR_NL:
		idio_read_newline (handle);
		break;
	    default:
		idio_ungetc_handle (handle, c);
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_escape (handle, lo, ic, depth));
		return lo;
	    }
	} else {

	    switch (c) {
	    case EOF:
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_eof);
		return lo;
	    case IDIO_CHAR_SPACE:
	    case IDIO_CHAR_TAB:
		idio_read_whitespace (handle);
		moved = 1;
		break;
	    case IDIO_CHAR_CR:
	    case IDIO_CHAR_NL:
		if (0 == return_nl) {
		    idio_read_newline (handle);
		}
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_eol);
		return lo;
	    case IDIO_CHAR_LPAREN:
		{
		    IDIO l = idio_read_list (handle, lo, idio_T_lparen, ic, IDIO_LIST_PAREN (depth) + 1);
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, l);
		    if (idio_S_nil != l) {
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
		IDIO block = idio_read_block (handle, lo, idio_T_rbrace, ic, IDIO_LIST_BRACE (depth + 1));
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
		     * We could be looking at the ... symbol for
		     * syntax-rules.  certainly, multiple consecutive
		     * DOTs are not an indexing operation
		     */
		    int c = idio_getc_handle (handle);
		    switch (c) {
		    case IDIO_CHAR_DOT:
			{
			    idio_ungetc_handle (handle, c);
			    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, IDIO_CHAR_DOT));
			    return lo;
			}
			break;
		    default:
			idio_ungetc_handle (handle, c);
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_T_dot);
			return lo;
		    }
		}
		break;
	    case IDIO_CHAR_BACKQUOTE:
		{
		    char qq_ic[] = { IDIO_CHAR_COMMA, IDIO_CHAR_AT, IDIO_CHAR_SQUOTE, IDIO_CHAR_BACKSLASH };
		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_quasiquote (handle, lo, qq_ic, depth));
		    return lo;
		}
	    case IDIO_CHAR_HASH:
		{
		    int c = idio_getc_handle (handle);
		    switch (c) {
		    case 'f':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_false);
			return lo;
		    case 't':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_true);
			return lo;
		    case 'n':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_S_nil);
			return lo;
		    case IDIO_CHAR_BACKSLASH:
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_named_character (handle, lo));
			return lo;
		    case IDIO_CHAR_LBRACKET:
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_array (handle, lo, ic, IDIO_LIST_BRACKET (depth + 1)));
			return lo;
		    case IDIO_CHAR_LBRACE:
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_hash (handle, lo, ic, IDIO_LIST_BRACE (depth + 1)));
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
				sprintf (em, "number expected after #%c: got %s", inexact ? 'i' : 'e', idio_type2string (bn));
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
		    case 'T':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_template (handle, lo, depth));
			return lo;
		    case 'P':
			idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_pathname (handle, lo, depth));
			return lo;
		    case IDIO_CHAR_LANGLE:
			{
			    /*
			     * Test Case: read-errors/not-ready-for-hash-format.idio
			     *
			     * #<foo>
			     */
			    char em[BUFSIZ];
			    sprintf (em, "not ready for # format: %c (%02x)", c, c);
			    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}
		    case IDIO_CHAR_PIPE:
			idio_read_block_comment (handle, lo, depth);
			return lo;
		    case IDIO_CHAR_SEMICOLON:
			idio_read (handle);
			return lo;
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
			    sprintf (em, "unexpected # format: '%c' (%#02x)", c, c);
			    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}

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
			    sprintf (em, "unexpected %c outside of list", IDIO_PAIR_SEPARATOR);
			    idio_read_error_parse (handle, lo, IDIO_C_FUNC_LOCATION (), em);

			    return idio_S_notreached;
			}
		    }

		    idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, c));
		    return lo;
		}
	    case IDIO_CHAR_SEMICOLON:
		idio_read_line_comment (handle, lo, depth);
		moved = 1;
		break;
	    case IDIO_CHAR_DQUOTE:
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_string (handle, lo));
		return lo;
	    default:
		idio_struct_instance_set_direct (lo, IDIO_LEXOBJ_EXPR, idio_read_word (handle, lo, c));
		return lo;
	    }
	}

	c = idio_getc_handle (handle);
    }
}

static IDIO idio_read_1_expr (IDIO handle, char *ic, int depth)
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
static IDIO idio_read_expr_line (IDIO handle, IDIO closedel, char *ic, int depth)
{
    IDIO line_lo = idio_struct_instance (idio_lexobj_type,
					 idio_pair (IDIO_HANDLE_FILENAME (handle),
					 idio_pair (idio_integer (IDIO_HANDLE_LINE (handle)),
					 idio_pair (idio_integer (IDIO_HANDLE_POS (handle)),
					 idio_pair (idio_S_unspec,
					 idio_S_nil)))));

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
		re = idio_list_reverse (re);
		if (idio_S_nil == IDIO_PAIR_T (re)) {
		    re = IDIO_PAIR_H (re);
		} else {
		    if (idio_isa_pair (re)) {
			IDIO re_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
			idio_struct_instance_set_direct (re_lo, IDIO_LEXOBJ_EXPR, re);
			idio_hash_put (idio_src_properties, re, re_lo);
		    }
		    re = idio_operator_expand (re, 0);
		    if (idio_isa_pair (re)) {
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
		re = idio_list_reverse (re);
		if (idio_S_nil == IDIO_PAIR_T (re)) {
		    re = IDIO_PAIR_H (re);
		} else {
		    if (idio_isa_pair (re)) {
			IDIO re_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
			idio_struct_instance_set_direct (re_lo, IDIO_LEXOBJ_EXPR, re);
			idio_hash_put (idio_src_properties, re, re_lo);
		    }
		    re = idio_operator_expand (re, 0);
		    if (idio_isa_pair (re)) {
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
		re = idio_list_reverse (re);
		if (idio_S_nil == IDIO_PAIR_T (re)) {
		    re = IDIO_PAIR_H (re);
		} else {
		    if (idio_isa_pair (re)) {
			IDIO re_lo = idio_copy (lo, IDIO_COPY_SHALLOW);
			idio_struct_instance_set_direct (re_lo, IDIO_LEXOBJ_EXPR, re);
			idio_hash_put (idio_src_properties, re, re_lo);
		    }
		    re = idio_operator_expand (re, 0);
		    if (idio_isa_pair (re)) {
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
		    idio_error_C ("unexpected constant in line", IDIO_LIST2 (handle, expr), IDIO_C_FUNC_LOCATION ());

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
		    idio_error_C ("unexpected token in line", IDIO_LIST2 (handle, expr), IDIO_C_FUNC_LOCATION ());

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
static IDIO idio_read_block (IDIO handle, IDIO lo, IDIO closedel, char *ic, int depth)
{
    IDIO r = idio_S_nil;

    for (;;) {
	IDIO line_p = idio_read_expr_line (handle, closedel, ic, depth);
	IDIO line_lo = IDIO_PAIR_H (line_p);
	IDIO expr = idio_struct_instance_ref_direct (line_lo, IDIO_LEXOBJ_EXPR);
	IDIO reason = IDIO_PAIR_T (line_p);

	if (idio_isa_pair (expr)) {
	    idio_hash_put (idio_src_properties, expr, line_lo);
	}

	if (idio_S_nil != expr) {
	    r = idio_pair (expr, r);
	}

	if (closedel == reason) {
	    r = idio_list_reverse (r);
	    if (depth) {
		return idio_list_append2 (IDIO_LIST1 (idio_S_block), r);
	    } else {
		/*
		 * At the time of writing idio_read_block is always
		 * called with a >0 value for depth.
		 *
		 * Given that both clauses do the same thing...what
		 * was I thinking?
		 */
		return idio_list_append2 (IDIO_LIST1 (idio_S_block), r);
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
    IDIO line_p = idio_read_expr_line (handle, idio_T_eol, idio_default_interpolation_chars, 0);
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
	IDIO lo = idio_read_1_expr (handle, idio_default_interpolation_chars, 0);
	expr = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
	if (idio_S_eof == expr) {
	    break;
	}
    }

    idio_gc_resume ("idio_read_expr");

    return expr;
}

IDIO idio_read_char (IDIO handle)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    int c = idio_getc_handle (handle);

    if (EOF == c) {
	return idio_S_eof;
    } else {
	/*
	 * Technically more of an IDIO_OCTET than a true (UTF-8)
	 * character but you didn't read this, right?
	 */
	return IDIO_CHARACTER (c);
    }
}

IDIO idio_read_character (IDIO handle, IDIO lo)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    idio_utf8_t codepoint;
    idio_utf8_t state = 0;

    int i;
    for (i = 0; ; i++) {
	int c = idio_getc_handle (handle);

	if (EOF == c) {
	    if (i) {
		fprintf (stderr, "EOF w/ i=%d\n", i);
		idio_read_error_utf8_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "EOF");

		return idio_S_notreached;

	    } else {
		return idio_S_eof;
	    }
	}

	/*
	 * if this is a non-ASCII value then it should be the result
	 * of an idio_ungetc_handle
	 */
	if (c > 0x7f) {
	    codepoint = c;
	    break;
	}

	if (0 == idio_utf8_decode (&state, &codepoint, c)) {
	    /* fprintf (stderr, "idio_read_character: U+%04X\n", codepoint); */
	    break;
	}

	if (state != IDIO_UTF8_ACCEPT) {
	    fprintf (stderr, "The (UTF-8) string is not well-formed\n");
	    idio_read_error_utf8_decode (handle, lo, IDIO_C_FUNC_LOCATION (), "not well-formed");

	    return idio_S_notreached;
	}
    }

    return IDIO_CHARACTER (codepoint);
}

void idio_init_read ()
{
    IDIO name = idio_symbols_C_intern ("%idio-lexical-object");
    idio_lexobj_type = idio_struct_type (name,
					 idio_S_nil,
					 idio_pair (idio_symbols_C_intern ("name"),
					 idio_pair (idio_symbols_C_intern ("line"),
					 idio_pair (idio_symbols_C_intern ("pos"),
					 idio_pair (idio_symbols_C_intern ("expr"),
					 idio_S_nil)))));
    idio_module_set_symbol_value (name, idio_lexobj_type, idio_Idio_module);

    idio_src_properties = IDIO_HASH_EQP (4 * 1024);
    IDIO_HASH_FLAGS (idio_src_properties) |= IDIO_HASH_FLAG_WEAK_KEYS;
    name = idio_symbols_C_intern ("%idio-src-properties");
    idio_module_set_symbol_value (name, idio_src_properties, idio_Idio_module);
}

void idio_read_add_primitives ()
{
}

void idio_final_read ()
{
}

