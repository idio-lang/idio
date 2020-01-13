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
static void idio_read_error (IDIO handle, IDIO msg, IDIO detail)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (detail);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO line;
    line = idio_integer (IDIO_HANDLE_LINE (handle));
    IDIO pos;
    pos = idio_integer (IDIO_HANDLE_POS (handle));

    IDIO c = idio_struct_instance (idio_condition_read_error_type,
				   IDIO_LIST5 (msg,
					       idio_string_C (IDIO_HANDLE_NAME (handle)),
					       detail,
					       line,
					       pos));
    idio_raise_condition (idio_S_false, c);
}

static void idio_read_error_parse (IDIO handle, IDIO detail, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static void idio_read_error_parse_args (IDIO handle, char *msg, IDIO args)
{
    IDIO_ASSERT (handle);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_read_error (handle, idio_get_output_string (sh), args);
}

static void idio_read_error_parse_printf (IDIO handle, IDIO detail, char *fmt, ...)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (fmt);
    IDIO_TYPE_ASSERT (handle, handle);

    va_list fmt_args;
    va_start (fmt_args, fmt);
    IDIO msg = idio_error_string (fmt, fmt_args);
    va_end (fmt_args);

    idio_read_error (handle, msg, detail);
}

static void idio_read_error_parse_word_too_long (IDIO handle, IDIO detail, char *word)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (word);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("word is too long: '", sh);
    idio_display_C (word, sh);
    idio_display_C ("'", sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static void idio_read_error_list_eof (IDIO handle, IDIO detail)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("EOF in list", sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static void idio_read_error_pair_separator (IDIO handle, IDIO detail, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static void idio_read_error_string (IDIO handle, IDIO detail, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("string: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static void idio_read_error_named_character (IDIO handle, IDIO detail, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("character: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static void idio_read_error_named_character_unknown_name (IDIO handle, IDIO detail, char *name)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (name);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("unknown character name: ", sh);
    idio_display_C (name, sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static void idio_read_error_utf8_decode (IDIO handle, IDIO detail, char *msg)
{
    IDIO_ASSERT (handle);
    IDIO_ASSERT (detail);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (handle, handle);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("UTF-8 decode: ", sh);
    idio_display_C (msg, sh);

    idio_read_error (handle, idio_get_output_string (sh), detail);
}

static IDIO idio_read_1_expr (IDIO handle, char *ic, int depth);
static IDIO idio_read_block (IDIO handle, IDIO closedel, char *ic, int depth);

static void idio_read_whitespace (IDIO handle)
{
    for (;;) {
	int c = idio_handle_getc (handle);

	switch (c) {
	case EOF:
	    return;
	case IDIO_CHAR_SPACE:
	case IDIO_CHAR_TAB:
	    break;
	default:
	    idio_handle_ungetc (handle, c);
	    return;
	}
    }
}

static void idio_read_newline (IDIO handle)
{
    for (;;) {
	int c = idio_handle_getc (handle);

	switch (c) {
	case EOF:
	    return;
	case IDIO_CHAR_CR:
	case IDIO_CHAR_NL:
	    break;
	default:
	    idio_handle_ungetc (handle, c);
	    return;
	}
    }
}

static IDIO idio_read_list (IDIO handle, IDIO opendel, char *ic, int depth)
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
	idio_read_error_parse_args (handle, "unexpected list open delimiter '%s'", opendel);
	return idio_S_unspec;
    }

    IDIO r = idio_S_nil;

    for (;;) {
	IDIO e = idio_read_1_expr (handle, ic, depth);

	if (idio_handle_eofp (handle)) {
	    idio_read_error_list_eof (handle, IDIO_C_LOCATION ("idio_read_list"));
	    return idio_S_unspec;
	} else if (idio_T_eol == e) {
	    /* continue */
	} else if (idio_T_pair_separator == e) {
	    /* ( & a) */
	    if (count < 1) {
		char em[BUFSIZ];
		sprintf (em, "nothing before %c in list", IDIO_PAIR_SEPARATOR);
		idio_read_error_pair_separator (handle, IDIO_C_LOCATION ("idio_read_list"), em);
		return idio_S_unspec;
	    }

	    /*
	     * XXX should only expect a single expr after
	     * IDIO_PAIR_SEPARATOR, ie. not a list: (a . b c)
	     */
	    IDIO pt = idio_read_1_expr (handle, ic, depth);
	    while (idio_T_eol == pt) {
		pt = idio_read_1_expr (handle, ic, depth);
	    }

	    if (idio_handle_eofp (handle)) {
		idio_read_error_list_eof (handle, IDIO_C_LOCATION ("idio_read_list"));
		return idio_S_unspec;
	    } else if (closedel == pt) {
		/* (a &) */
		char em[BUFSIZ];
		sprintf (em, "nothing after %c in list", IDIO_PAIR_SEPARATOR);
		idio_read_error_pair_separator (handle, IDIO_C_LOCATION ("idio_read_list"), em);
		return idio_S_unspec;
	    }

	    /*
	     * This should be the closing delimiter
	     */
	    IDIO del = idio_read_1_expr (handle, ic, depth);
	    while (idio_T_eol == del) {
		del = idio_read_1_expr (handle, ic, depth);
	    }

	    if (idio_handle_eofp (handle)) {
		idio_read_error_list_eof (handle, IDIO_C_LOCATION ("idio_read_list"));
		return idio_S_unspec;
	    } else if (closedel == del) {
		return idio_improper_list_reverse (r, pt);
	    } else {
		/* (a & b c) */
		idio_debug ("extra=%s\n", del);
		char em[BUFSIZ];
		sprintf (em, "more than one expression after %c in list", IDIO_PAIR_SEPARATOR);
		idio_read_error_pair_separator (handle, IDIO_C_LOCATION ("idio_read_list"), em);
		return idio_S_unspec;
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
		IDIO ne = idio_read_1_expr (handle, ic, depth);
		while (idio_T_eol == ne) {
		    ne = idio_read_1_expr (handle, ic, depth);
		}

		if (idio_handle_eofp (handle)) {
		    idio_read_error_list_eof (handle, IDIO_C_LOCATION ("idio_read_list"));
		    return idio_S_unspec;
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
		r = idio_operator_expand (r, 0);
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
		    idio_error_C ("unexpected token in list", IDIO_LIST2 (handle, e), IDIO_C_LOCATION ("idio_read_list"));
		}
	    }

	    if (IDIO_TYPE_CONSTANT_TOKENP (e)) {
		uintptr_t ev = IDIO_CONSTANT_TOKEN_VAL (e);
		switch (ev) {
		case IDIO_TOKEN_LANGLE:
		    e = idio_S_lt;
		    break;
		case IDIO_TOKEN_RANGLE:
		    e = idio_S_gt;
		    break;
		case IDIO_TOKEN_DOT:
		    e = idio_S_dot;
		    break;
		default:
		    idio_error_C ("unexpected token in list", IDIO_LIST2 (handle, e), IDIO_C_LOCATION ("idio_read_list"));
		}
	    }

	    r = idio_pair (e, r);
	}
    }

    idio_read_error_parse_printf (handle, IDIO_C_LOCATION ("idio_read_list"), "impossible!");
    return idio_S_unspec;
}

static IDIO idio_read_quote (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_1_expr (handle, ic, depth);
    e = IDIO_LIST2 (idio_S_quote, e);

    return e;
}

static IDIO idio_read_quasiquote (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_1_expr (handle, ic, depth);
    e = IDIO_LIST2 (idio_S_quasiquote, e);

    return e;
}

static IDIO idio_read_unquote_splicing (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_1_expr (handle, ic, depth);
    e = IDIO_LIST2 (idio_S_unquotesplicing, e);

    return e;
}

static IDIO idio_read_unquote (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_1_expr (handle, ic, depth);
    e = IDIO_LIST2 (idio_S_unquote, e);

    return e;
}

static IDIO idio_read_escape (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_1_expr (handle, ic, depth);
    e = IDIO_LIST2 (idio_S_escape, e);

    return e;
}

static void idio_read_comment (IDIO handle, int depth)
{
    IDIO_ASSERT (handle);

    for (;;) {
	int c = idio_handle_getc (handle);

	if (idio_handle_eofp (handle)) {
	    return;
	}

	switch (c) {
	case IDIO_CHAR_CR:
	case IDIO_CHAR_NL:
	    idio_handle_ungetc (handle, c);
	    return;
	}
    }
}

static IDIO idio_read_string (IDIO handle)
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
	int c = idio_handle_getc (handle);

	if (EOF == c) {
	    idio_read_error_string (handle, IDIO_C_LOCATION ("idio_read_string"), "unterminated");
	    return idio_S_unspec;
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

static IDIO idio_read_named_character (IDIO handle)
{
    IDIO_ASSERT (handle);

    char buf[IDIO_CHARACTER_MAX_NAME_LEN+1];
    int i;
    intptr_t c;

    for (i = 0 ; i < IDIO_CHARACTER_MAX_NAME_LEN; i++) {
	c = idio_handle_getc (handle);

	if (EOF == c) {
	    idio_read_error_named_character (handle, IDIO_C_LOCATION ("idio_read_named_character"), "EOF");
	    return idio_S_unspec;
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

    idio_handle_ungetc (handle, c);
    buf[i] = '\0';

    IDIO r;

    /* can i==0 happen? EOF? */
    if (0 == i) {
	idio_read_error_named_character (handle, IDIO_C_LOCATION ("idio_read_named_character"), "no letters in character name?");
	return idio_S_unspec;
    } else if (1 == i) {
	r = IDIO_CHARACTER (buf[0]);
    } else {
	r = idio_character_lookup (buf);

	if (r == idio_S_unspec) {
	    idio_read_error_named_character_unknown_name (handle, IDIO_C_LOCATION ("idio_read_named_character"), buf);
	    return idio_S_unspec;
	}
    }

    idio_gc_stats_inc (IDIO_TYPE_CONSTANT_CHARACTER);
    return r;
}

static IDIO idio_read_array (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_list (handle, idio_T_lbracket, ic, IDIO_LIST_BRACKET (depth + 1));
    return idio_list_to_array (e);
}

static IDIO idio_read_hash (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_list (handle, idio_T_lbrace, ic, IDIO_LIST_BRACE (depth + 1));

    return idio_hash_alist_to_hash (e, idio_S_nil);
}

static IDIO idio_read_template (IDIO handle, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    char interpc[IDIO_INTERPOLATION_CHARS];
    for (i = 0; i < IDIO_INTERPOLATION_CHARS; i++) {
	interpc[i] = idio_default_interpolation_chars[i];
    }
    i = 0;

    int c = idio_handle_getc (handle);

    while (! IDIO_OPEN_DELIMITER (c)) {
	if (i > (IDIO_INTERPOLATION_CHARS + 1)) {
	    idio_read_error_parse_printf (handle, IDIO_C_LOCATION ("idio_read_template"), "too many interpolation characters: #%d: %c (%#x)", i, c, c);
	    return idio_S_unspec;
	}

	switch (c) {
	case EOF:
	    idio_read_error_named_character (handle, IDIO_C_LOCATION ("idio_read_template"), "EOF");
	    return idio_S_unspec;
	default:
	    if (IDIO_CHAR_DOT != c) {
		interpc[i] = c;
	    }
	}

	i++;
	c = idio_handle_getc (handle);
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
	closedel = idio_T_rbracket;
	depth = IDIO_LIST_BRACKET (depth + 1);
	break;
    case IDIO_CHAR_LANGLE:
	closedel = idio_T_rangle;
	depth = IDIO_LIST_ANGLE (depth + 1);
	break;
    default:
	idio_read_error_parse_printf (handle, IDIO_C_LOCATION ("idio_read_template"), "unexpected template delimiter: %c (%#x)", c, c);
	return idio_S_unspec;
    }

    IDIO e = idio_read_block (handle, closedel, interpc, depth);
    /*
     * idio_read_block has returned (block expr) and we only want expr
     *
     * Note that (block expr1 expr2+) means we need to wrap a begin
     * round expr1 expr2+ -- unlike quasiquote!
     */
    if (idio_S_nil == IDIO_PAIR_TT (e)) {
	return IDIO_LIST2 (idio_S_quasiquote, IDIO_PAIR_HT (e));
    } else {
	return IDIO_LIST2 (idio_S_quasiquote,
			   idio_list_append2 (IDIO_LIST1 (idio_S_begin),
					      IDIO_PAIR_T (e)));
    }
}

static IDIO idio_read_pathname (IDIO handle, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    char interpc[IDIO_INTERPOLATION_CHARS];
    for (i = 0; i < IDIO_INTERPOLATION_CHARS; i++) {
	interpc[i] = idio_default_interpolation_chars[i];
    }
    i = 0;

    int c = idio_handle_getc (handle);

    while (IDIO_CHAR_DQUOTE != c) {
	if (i > (IDIO_INTERPOLATION_CHARS + 1)) {
	    idio_read_error_parse_printf (handle, IDIO_C_LOCATION ("idio_read_pathname"), "too many interpolation characters: #%d: %c (%#x)", i, c, c);
	    return idio_S_unspec;
	}

	switch (c) {
	case EOF:
	    idio_read_error_named_character (handle, IDIO_C_LOCATION ("idio_read_pathname"), "EOF");
	    return idio_S_unspec;
	default:
	    if (IDIO_CHAR_DOT != c) {
		interpc[i] = c;
	    }
	}

	i++;
	c = idio_handle_getc (handle);
    }

    IDIO e = idio_read_string (handle);

    return idio_struct_instance (idio_path_type, IDIO_LIST1 (e));
}

static IDIO idio_read_bignum (IDIO handle, char basec, int radix)
{
    IDIO_ASSERT (handle);

    int c = idio_handle_getc (handle);

    int neg = 0;

    switch (c) {
    case '-':
	neg = 1;
	c = idio_handle_getc (handle);
	break;
    case '+':
	c = idio_handle_getc (handle);
	break;
    }

    char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz"; /* base 36 is possible */
    int max_base = strlen (digits);

    if (radix > max_base) {
	char em[BUFSIZ];
	sprintf (em, "bignum base #%c (%d) > max base %d", basec, radix, max_base);
	idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_bignum"), em);
	return idio_S_unspec;
    }

    IDIO base = idio_bignum_integer_intmax_t (radix);
    IDIO bn = idio_bignum_integer_intmax_t (0);

    int ndigits = 0;
    int i;
    while (! IDIO_SEPARATOR (c)) {
	if (idio_handle_eofp (handle)) {
	    break;
	}

	i = 0;
	while (digits[i] &&
	       digits[i] != c) {
	    i++;
	}

	if (i >= radix) {
	    char em[BUFSIZ];
	    sprintf (em, "invalid digit %c in bignum base #%c", c, basec);
	    idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_bignum"), em);
	    return idio_S_unspec;
	}

	IDIO bn_i = idio_bignum_integer_intmax_t (i);

	bn = idio_bignum_multiply (bn, base);
	bn = idio_bignum_add (bn, bn_i);
	ndigits++;

	c = idio_handle_getc (handle);
    }

    if (0 == ndigits) {
	char em[BUFSIZ];
	sprintf (em, "no digits after bignum base #%c", basec);
	idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_bignum"), em);
	return idio_S_unspec;
    }

    idio_handle_ungetc (handle, c);

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
	    num = idio_bignum_C (str);

	    /* convert to a fixnum if possible */
	    IDIO fn = idio_bignum_to_fixnum (num);
	    if (idio_S_nil != fn) {
		num = fn;
	    }
	}
    }

    return num;
}

static IDIO idio_read_word (IDIO handle, int c)
{
    char buf[IDIO_WORD_MAX_LEN];
    int i = 0;

    for (;;) {
	buf[i++] = c;

	if (i >= IDIO_WORD_MAX_LEN) {
	    buf[IDIO_WORD_MAX_LEN-1] = '\0';
	    idio_read_error_parse_word_too_long (handle, IDIO_C_LOCATION ("idio_read_word"), buf);
	}

	c = idio_handle_getc (handle);

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
	    idio_handle_ungetc (handle, c);
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
	if (':' == buf[0] &&
	     i > 1 &&
	     ! ispunct (buf[1])) {
	    r = idio_keywords_C_intern (buf + 1);
	} else {
	    r = idio_symbols_C_intern (buf);
	}
    }

    return r;
}

static IDIO idio_read_1_expr_nl (IDIO handle, char *ic, int depth, int nl)
{
    int c = idio_handle_getc (handle);

    for (;;) {

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
	    c = idio_handle_getc (handle);
	    if (c == ic[1]) {
		return idio_read_unquote_splicing (handle, ic, depth);
	    }
	    idio_handle_ungetc (handle, c);
	    return idio_read_unquote (handle, ic, depth);
	} else if (c == ic[2]) {
	    return idio_read_quote (handle, ic, depth);
	} else if (c == ic[3]) {
	    c = idio_handle_getc (handle);
	    switch (c) {
	    case IDIO_CHAR_CR:
	    case IDIO_CHAR_NL:
		idio_read_newline (handle);
		break;
	    default:
		idio_handle_ungetc (handle, c);
		return idio_read_escape (handle, ic, depth);
	    }
	} else {

	    switch (c) {
	    case EOF:
		return idio_S_eof;
	    case IDIO_CHAR_SPACE:
	    case IDIO_CHAR_TAB:
		idio_read_whitespace (handle);
		break;
	    case IDIO_CHAR_CR:
	    case IDIO_CHAR_NL:
		if (0 == nl) {
		    idio_read_newline (handle);
		}
		return idio_T_eol;
	    case IDIO_CHAR_LPAREN:
		return idio_read_list (handle, idio_T_lparen, ic, IDIO_LIST_PAREN (depth) + 1);
	    case IDIO_CHAR_RPAREN:
		if (IDIO_LIST_PAREN_P (depth)) {
		    return idio_T_rparen;
		} else {
		    idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_1_expr_nl"), "unexpected ')'");
		    return idio_S_unspec;
		}
		break;
	    case IDIO_CHAR_LBRACE:
		return idio_read_block (handle, idio_T_rbrace, ic, IDIO_LIST_BRACE (depth + 1));
	    case IDIO_CHAR_RBRACE:
		if (IDIO_LIST_BRACE_P (depth)) {
		    return idio_T_rbrace;
		} else {
		    idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_1_expr_nl"), "unexpected '}'");
		    return idio_S_unspec;
		}
		break;
	    case IDIO_CHAR_LBRACKET:
		return idio_read_block (handle, idio_T_rbracket, ic, IDIO_LIST_BRACKET (depth + 1));
	    case IDIO_CHAR_RBRACKET:
		if (IDIO_LIST_BRACKET_P (depth)) {
		    return idio_T_rbracket;
		} else {
		    idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_1_expr_nl"), "unexpected ']'");
		    return idio_S_unspec;
		}
		break;
	    case IDIO_CHAR_DOT:
		{
		    /*
		     * We could be looking at the ... symbol for
		     * syntax-rules.  certainly, multiple consecutive
		     * DOTs are not an indexing operation
		     */
		    int c = idio_handle_getc (handle);
		    switch (c) {
		    case '.':
			{
			    idio_handle_ungetc (handle, c);
			    return idio_read_word (handle, IDIO_CHAR_DOT);
			}
			break;
		    default:
			idio_handle_ungetc (handle, c);
			return idio_T_dot;
		    }
		}
		break;
	    case IDIO_CHAR_BACKQUOTE:
		{
		    char qq_ic[] = { IDIO_CHAR_COMMA, IDIO_CHAR_AT, IDIO_CHAR_SQUOTE, IDIO_CHAR_BACKSLASH };
		    return idio_read_quasiquote (handle, qq_ic, depth);
		}
	    case IDIO_CHAR_HASH:
		{
		    int c = idio_handle_getc (handle);
		    switch (c) {
		    case 'f':
			return idio_S_false;
		    case 't':
			return idio_S_true;
		    case 'n':
			return idio_S_nil;
		    case '\\':
			return idio_read_named_character (handle);
		    case '[':
			return idio_read_array (handle, ic, IDIO_LIST_BRACKET (depth + 1));
		    case '{':
			return idio_read_hash (handle, ic, IDIO_LIST_BRACE (depth + 1));
		    case 'b':
			return idio_read_bignum (handle, c, 2);
		    case 'd':
			return idio_read_bignum (handle, c, 10);
		    case 'o':
			return idio_read_bignum (handle, c, 8);
		    case 'x':
			return idio_read_bignum (handle, c, 16);
		    case 'e':
		    case 'i':
			{
			    int inexact = ('i' == c);
			    IDIO bn = idio_read_1_expr (handle, ic, depth);

			    if (IDIO_TYPE_FIXNUMP (bn)) {
				if (0 == inexact) {
				    return bn;
				} else {
				    bn = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (bn));
				}
			    }

			    if (! idio_isa_bignum (bn)) {
				char em[BUFSIZ];
				sprintf (em, "number expected after #%c: got %s", inexact ? 'i' : 'e', idio_type2string (bn));
				idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_1_expr_nl"), em);
				return idio_S_unspec;
			    }

			    if (IDIO_BIGNUM_INTEGER_P (bn)) {
				if (! inexact) {
				    return bn;
				}

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

			    return bn;
			}
		    case 'T':
			return idio_read_template (handle, depth);
		    case 'P':
			return idio_read_pathname (handle, depth);
		    case '<':
			{
			    char em[BUFSIZ];
			    sprintf (em, "not ready for # format: %c (%02x)", c, c);
			    idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_1_expr_nl"), em);
			    return idio_S_unspec;
			}
		    default:
			{
			    char em[BUFSIZ];
			    sprintf (em, "unexpected # format: '%c' (%#02x)", c, c);
			    idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_1_expr_nl"), em);
			    return idio_S_unspec;
			}

		    }
		}
		break;
	    case IDIO_PAIR_SEPARATOR:
		{
		    int cp = idio_handle_peek (handle);

		    if (IDIO_SEPARATOR (cp)) {
			if (depth) {
			    return idio_T_pair_separator;
			} else {
			    char em[BUFSIZ];
			    sprintf (em, "unexpected %c outside of list", IDIO_PAIR_SEPARATOR);
			    idio_read_error_parse (handle, IDIO_C_LOCATION ("idio_read_1_expr_nl"), em);
			    return idio_S_unspec;
			}
		    }

		    return idio_read_word (handle, c);
		}
	    case IDIO_CHAR_SEMICOLON:
		idio_read_comment (handle, depth);
		break;
	    case IDIO_CHAR_DQUOTE:
		return idio_read_string (handle);
		break;
	    default:
		return idio_read_word (handle, c);
	    }
	}

	c = idio_handle_getc (handle);
    }
}

static IDIO idio_read_1_expr (IDIO handle, char *ic, int depth)
{
    return idio_read_1_expr_nl (handle, ic, depth, 0);
}

/*
 * Looping around for EOF/EOL/"}" means that a single expression
 * becomes "(expr)" so check to see if the collected list is one
 * element long and use only the head if so.
 *
 * idio_read_expr_line returns a tuple: ({expr} . {reason}) where {reason}
 * is why the line was complete.
 *
 * The point being that a "line" could be terminated by an actual EOL
 * or EOF or the closing brace of a block.  Which some people care
 * about.
 */
static IDIO idio_read_expr_line (IDIO handle, IDIO closedel, char *ic, int depth)
{
    IDIO r = idio_S_nil;
    int count = 0;

    for (;;) {
	off_t line = IDIO_HANDLE_LINE(handle);
	IDIO expr = idio_read_1_expr_nl (handle, ic, depth, 1);

	if (idio_S_eof == expr) {
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
		if (idio_S_nil == IDIO_PAIR_T (r)) {
		    r = IDIO_PAIR_H (r);
		} else {
		    r = idio_operator_expand (r, 0);
		}
		return idio_pair (r, idio_S_eof);
	    } else {
		return idio_pair (idio_S_eof, idio_S_eof);
	    }
	} else if (idio_T_eol == expr) {
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
		if (idio_S_nil == IDIO_PAIR_T (r)) {
		    r = IDIO_PAIR_H (r);
		} else {
		    r = idio_operator_expand (r, 0);
		}
		return idio_pair (r, idio_T_eol);
	    } else {
		/* blank line */
	    }
	} else if (closedel == expr) {
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
		if (idio_S_nil == IDIO_PAIR_T (r)) {
		    r = IDIO_PAIR_H (r);
		} else {
		    r = idio_operator_expand (r, 0);
		}
		return idio_pair (r, closedel);
	    } else {
		return idio_pair (r, closedel);
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
		    IDIO ne = idio_read_1_expr (handle, ic, depth);
		    while (idio_T_eol == ne) {
			ne = idio_read_1_expr (handle, ic, depth);
		    }

		    if (idio_handle_eofp (handle)) {
			idio_read_error_list_eof (handle, IDIO_C_LOCATION ("idio_read_expr_line"));
			return idio_S_unspec;
		    }

		    r = idio_pair (expr, r);
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
		    idio_error_C ("unexpected constant in line", IDIO_LIST2 (handle, expr), IDIO_C_LOCATION ("idio_read_expr_line"));
		}
	    } else if (IDIO_TYPE_CONSTANT_TOKENP (expr)) {
		uintptr_t ev = IDIO_CONSTANT_TOKEN_VAL (expr);
		switch (ev) {
		case IDIO_TOKEN_LANGLE:
		    expr = idio_S_lt;
		    break;
		case IDIO_TOKEN_RANGLE:
		    expr = idio_S_gt;
		    break;
		case IDIO_TOKEN_DOT:
		    expr = idio_S_dot;
		    break;
		case IDIO_TOKEN_PAIR_SEPARATOR:
		    expr = idio_S_pair_separator;
		    break;
		default:
		    idio_error_C ("unexpected token in line", IDIO_LIST2 (handle, expr), IDIO_C_LOCATION ("idio_read_expr_line"));
		}
	    } else {
		/* idio_read_set_source_property (expr, idio_KW_handle, handle); */
		/* idio_read_set_source_property (expr, idio_KW_line, idio_fixnum (line)); */
		/* IDIO sp = idio_read_source_properties (expr); */
	    }

	    r = idio_pair (expr, r);
	}
	count++;
    }
}

static IDIO idio_read_block (IDIO handle, IDIO closedel, char *ic, int depth)
{
    IDIO r = idio_S_nil;

    for (;;) {
	IDIO line = idio_read_expr_line (handle, closedel, ic, depth);
	IDIO expr = IDIO_PAIR_H (line);
	IDIO reason = IDIO_PAIR_T (line);

	if (idio_S_nil != expr) {
	    r = idio_pair (expr, r);
	}

	if (idio_S_eof == expr) {
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
		if (depth) {
		    return idio_list_append2 (IDIO_LIST1 (idio_S_block), r);
		} else {
		    return idio_list_append2 (IDIO_LIST1 (idio_S_block), r);
		}
	    } else {
		return idio_S_eof;
	    }
	} else if (closedel == reason) {
	    r = idio_list_reverse (r);
	    if (depth) {
		return idio_list_append2 (IDIO_LIST1 (idio_S_block), r);
	    } else {
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

    idio_gc_pause ();

    /*
     * dummy value for closedel
     */
    IDIO line = idio_read_expr_line (handle, idio_T_eol, idio_default_interpolation_chars, 0);
    idio_gc_resume ();

    return IDIO_PAIR_H (line);
}

/*
 * Called by read-expr in handle.c
 */
IDIO idio_read_expr (IDIO handle)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    idio_gc_pause ();

    /*
     * There's an Idio/Scheme semantic cross-over here.  Should
     * reading an Idio expression fault at EOL?
     *
     * For the sake of the existing (S9) tests we'll hold to Scheme
     * semantics.
     */
    IDIO expr = idio_T_eol;
    while (idio_T_eol == expr) {
	expr = idio_read_1_expr (handle, idio_default_interpolation_chars, 0);
	if (idio_S_eof == expr) {
	    break;
	}
    }
    idio_gc_resume ();

    return expr;
}

IDIO idio_read_char (IDIO handle)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    int c = idio_handle_getc (handle);

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

IDIO idio_read_character (IDIO handle)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    idio_utf8_t codepoint;
    idio_utf8_t state = 0;

    int i;
    for (i = 0; ; i++) {
	int c = idio_handle_getc (handle);

	if (EOF == c) {
	    if (i) {
		fprintf (stderr, "EOF w/ i=%d\n", i);
		idio_read_error_utf8_decode (handle, IDIO_C_LOCATION ("idio_read_character"), "EOF");
	    } else {
		return idio_S_eof;
	    }
	}

	/*
	 * if this is a non-ASCII value then it should be the result
	 * of an idio_handle_ungetc
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
	    idio_read_error_utf8_decode (handle, IDIO_C_LOCATION ("idio_read_character"), "not well-formed");
	}
    }

    return IDIO_CHARACTER (codepoint);
}

void idio_init_read ()
{
}

void idio_read_add_primitives ()
{
}

void idio_final_read ()
{
}

