/*
 * Copyright (c) 2015 Ian Fitchet <idf(at)idio-lang.org>
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

#define IDIO_SEPARATOR(c)	(IDIO_CHAR_SPACE == (c) ||		\
				 IDIO_CHAR_TAB == (c) ||		\
				 IDIO_CHAR_NL == (c) ||			\
				 IDIO_CHAR_CR == (c) ||			\
				 IDIO_CHAR_LPAREN == (c) ||		\
				 IDIO_CHAR_RPAREN == (c) ||		\
				 IDIO_CHAR_SEMICOLON == (c) ||		\
				 IDIO_CHAR_SQUOTE == (c) ||		\
				 IDIO_CHAR_BACKQUOTE == (c) ||		\
				 IDIO_CHAR_COMMA == (c) ||		\
				 IDIO_CHAR_DQUOTE == (c))

#define IDIO_OPEN_DELIMITER(c)	(IDIO_CHAR_LPAREN == (c) ||		\
				 IDIO_CHAR_LBRACE == (c) ||		\
				 IDIO_CHAR_LBRACKET == (c) ||		\
				 IDIO_CHAR_LANGLE == (c))

/*
 * Default interpolation characters:
 *
 * IDIO_CHAR_DOT == use default (ie. skip) => IDIO_CHAR_DOT cannot be
 * one of the three
 *
 * 1. expression substitution == unquote
 * 2. expression splicing == unquotesplicing
 * 3. escape char
 */
char idio_default_interpolation_chars[] = { IDIO_CHAR_DOLLARS, IDIO_CHAR_AT, IDIO_CHAR_BACKSLASH };
#define IDIO_INTERPOLATION_CHARS 3

/*
 * In the case of named characters, eg. #\newline (as opposed to #\a,
 * the character 'a') what is the longest name (eg, "newline") we
 * should look out for.  Otherwise we'll read in
 * #\supercalifragilisticexpialidocious which is fine except that I
 * don't know what character that is.

 * That said, there's no reason why we shouldn't be able to use
 * Unicode named characters.  What's the longest one of those?
 * According to http://www.unicode.org/charts/charindex.html and
 * turning non-printing chars into _s, say, then "Aboriginal Syllabics
 * Extended, Unified Canadian" is some 47 chars long.  The longest is
 * 52 chars (Digraphs Matching Serbian Cyrillic Letters, Croatian,
 * 01C4).

 * In the meanwhile, we only have a handler for "space" and
 * "newline"...
 */
#define IDIO_CHARACTER_MAX_NAME_LEN	10
#define IDIO_CHARACTER_SPACE		"space"
#define IDIO_CHARACTER_NEWLINE		"newline"

static void idio_error_read_parse (IDIO handle, char *msg)
{
    idio_error_message ("%s:%zd:%zd: %s", IDIO_HANDLE_NAME (handle), IDIO_HANDLE_LINE (handle), IDIO_HANDLE_POS (handle), msg);
}

static void idio_error_read_parse_word_too_long (IDIO handle, char *w)
{
    idio_error_message ("%s:%zd:%zd: word is too long: %s...", IDIO_HANDLE_NAME (handle), IDIO_HANDLE_LINE (handle), IDIO_HANDLE_POS (handle), w);
}

static void idio_error_read_list_eof (IDIO handle)
{
    idio_error_message ("%s: EOF in list", IDIO_HANDLE_NAME (handle));
}

static void idio_error_read_list_ampersand (IDIO handle, char *msg)
{
    idio_error_message ("%s:%zd:%zd: %s", IDIO_HANDLE_NAME (handle), IDIO_HANDLE_POS (handle), IDIO_HANDLE_POS (handle), msg);
}

static void idio_error_read_string (IDIO handle, char *msg)
{
    idio_error_message ("%s:%zd:%zd: string %s", IDIO_HANDLE_NAME (handle), IDIO_HANDLE_POS (handle), IDIO_HANDLE_POS (handle), msg);
}

static void idio_error_read_character (IDIO handle, char *msg)
{
    idio_error_message ("%s:%zd:%zd: character %s", IDIO_HANDLE_NAME (handle), IDIO_HANDLE_POS (handle), IDIO_HANDLE_POS (handle), msg);
}

static void idio_error_read_character_unknown_name (IDIO handle, char *name)
{
    idio_error_message ("%s:%zd:%zd: unknown character name %s", IDIO_HANDLE_NAME (handle), IDIO_HANDLE_POS (handle), IDIO_HANDLE_POS (handle), name);
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
    } else {
	idio_error_message ("unexpected list open delimeter '%s'", idio_as_string (opendel, 1));
	return idio_S_unspec;
    }

    IDIO r = idio_S_nil;
    
    for (;;) {
	IDIO e = idio_read_1_expr (handle, ic, depth);

	if (idio_handle_eofp (handle)) {
	    idio_error_read_list_eof (handle);
	    return idio_S_unspec;
	} else if (idio_T_eol == e) {
	    /* continue */
	} else if (idio_T_ampersand == e) {
	    /* ( & a) */
	    if (count < 1) {
		idio_error_read_list_ampersand (handle, "nothing before & in list");
		return idio_S_unspec;
	    }

	    /*
	     * XXX should only expect a single expr after ampersand, ie. not
	     * a list: (a & b c)
	     */
	    IDIO cdr = idio_read_1_expr (handle, ic, depth);
	    while (idio_T_eol == cdr) {
		cdr = idio_read_1_expr (handle, ic, depth);
	    }

	    if (idio_handle_eofp (handle)) {
		idio_error_read_list_eof (handle);
		return idio_S_unspec;
	    } else if (closedel == cdr) {
		/* (a &) */
		idio_error_read_list_ampersand (handle, "nothing after & in list");
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
		idio_error_read_list_eof (handle);
		return idio_S_unspec;
	    } else if (closedel == del) {
		return idio_improper_list_reverse (r, cdr);
	    } else {
		/* (a & b c) */
		idio_debug ("extra=%s\n", del);
		idio_error_read_list_ampersand (handle, "more than one expression after & in list");
		return idio_S_unspec;
	    }
	}

	if (idio_T_eol != e) {
	    count++;

	    if (closedel == e) {
		r = idio_list_reverse (r);
		r = idio_meaning_operators (r, 0);
		return r;
	    }

	    r = idio_pair (e, r);
	}
    }

    idio_error_message ("idio_read_list: impossible!");
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
	    idio_error_read_string (handle, "unterminated");
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

static IDIO idio_read_character (IDIO handle)
{
    IDIO_ASSERT (handle);

    char buf[IDIO_CHARACTER_MAX_NAME_LEN+1];
    int i;
    intptr_t c;
    
    for (i = 0 ; i < IDIO_CHARACTER_MAX_NAME_LEN; i++) {
	c = idio_handle_getc (handle);

	if (EOF == c) {
	    idio_error_read_character (handle, "EOF");
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
	idio_error_read_character (handle, "no letters in character name?");
	return idio_S_unspec;
    } else if (1 == i) {
	r = IDIO_CHARACTER (buf[0]);
    } else {
	r = idio_character_lookup (buf);

	if (r == idio_S_unspec) {
	    idio_error_read_character_unknown_name (handle, buf);
	    return idio_S_unspec;
	}
    }

    idio_gc_stats_inc (IDIO_TYPE_CHARACTER);
    return r;
}

static IDIO idio_read_vector (IDIO handle, char *ic, int depth)
{
    IDIO_ASSERT (handle);

    IDIO e = idio_read_list (handle, idio_T_lparen, ic, depth);
    return idio_list_to_array (e);
}

static IDIO idio_read_template (IDIO handle, int depth)
{
    IDIO_ASSERT (handle);

    int i;
    char ic[IDIO_INTERPOLATION_CHARS];
    for (i = 0; i < IDIO_INTERPOLATION_CHARS; i++) {
	ic[i] = idio_default_interpolation_chars[i];
    }
    i = 0;
    
    int c = idio_handle_getc (handle);

    while (! IDIO_OPEN_DELIMITER (c)) {
	if (i > (IDIO_INTERPOLATION_CHARS + 1)) {
	    idio_error_message ("too many interpolation characters: #%d: %c (%#x)", i, c, c);
	    return idio_S_unspec;
	}

	switch (c) {
	case EOF:
	    idio_error_read_character (handle, "EOF");
	    return idio_S_unspec;
	default:
	    if (IDIO_CHAR_DOT != c) {
		ic[i] = c;
	    }
	}

	i++;
	c = idio_handle_getc (handle);
    }

    IDIO closedel = idio_S_nil;
    switch (c) {
    case IDIO_CHAR_LPAREN:
	closedel = idio_T_rparen;
	break;
    case IDIO_CHAR_LBRACE:
	closedel = idio_T_rbrace;
	break;
    case IDIO_CHAR_LBRACKET:
	closedel = idio_T_rbracket;
	break;
    case IDIO_CHAR_LANGLE:
	closedel = idio_T_rangle;
	break;
    default:
	idio_error_message ("unexpected template delimiter: %c (%#x)", c, c);
	return idio_S_unspec;
    }

    IDIO e = idio_read_block (handle, closedel, ic, depth);
    /*
     * idio_read_block has returned (block expr) and we only want expr
     *
     * Note that (block expr1 expr2+) means we need to wrap a begin
     * round expr1 expr2+ -- unlike quasiquote!
     */
    if (idio_S_nil == IDIO_PAIR_T (IDIO_PAIR_T (e))) {
	return IDIO_LIST2 (idio_S_quasiquote, IDIO_PAIR_H (IDIO_PAIR_T (e)));
    } else {
	return IDIO_LIST2 (idio_S_quasiquote,
			   idio_list_append2 (IDIO_LIST1 (idio_S_begin),
					      IDIO_PAIR_T (e)));
    }
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
	idio_error_read_parse (handle, em);
	return idio_S_unspec;
    }

    IDIO base = idio_bignum_integer_int64 (radix);
    IDIO bn = idio_bignum_integer_int64 (0);
    
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
	    idio_error_read_parse (handle, em);
	    return idio_S_unspec;
	}

	IDIO bn_i = idio_bignum_integer_int64 (i);
	
	bn = idio_bignum_multiply (bn, base);
	bn = idio_bignum_add (bn, bn_i);
	ndigits++;
	
	c = idio_handle_getc (handle);
    }

    if (0 == ndigits) {
	char em[BUFSIZ];
	sprintf (em, "no digits after bignum base #%c", basec);
	idio_error_read_parse (handle, em);
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
	    idio_error_read_parse_word_too_long (handle, buf);
	}

	c = idio_handle_getc (handle);

	if (EOF == c) {
	    break;
	}

	if (IDIO_SEPARATOR (c)) {
	    idio_handle_ungetc (handle, c);
	    break;
	}
    }

    buf[i] = '\0';

    IDIO r = idio_read_number_C (handle, buf);

    if (idio_S_nil == r) {
	r = idio_symbols_C_intern (buf);
    }

    return r;
}

static IDIO idio_read_1_expr (IDIO handle, char *ic, int depth)
{
    int c = idio_handle_getc (handle);

    for (;;) {

	/*
	 * cf. handling of , and ,@
	 */
	if (c == ic[0]) {
	    c = idio_handle_getc (handle);
	    if (c == ic[1]) {
		return idio_read_unquote_splicing (handle, ic, depth);
	    }
	    idio_handle_ungetc (handle, c);
	    return idio_read_unquote (handle, ic, depth);
	}

	switch (c) {
	case EOF:
	    return idio_S_eof;
	case IDIO_CHAR_SPACE:
	case IDIO_CHAR_TAB:
	    idio_read_whitespace (handle);
	    break;
	case IDIO_CHAR_CR:
	case IDIO_CHAR_NL:
	    idio_read_newline (handle);
	    return idio_T_eol;
	case IDIO_CHAR_BACKSLASH:
	    {
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
	    }
	    break;
	case IDIO_CHAR_LPAREN:
	    return idio_read_list (handle, idio_T_lparen, ic, depth + 1);
	case IDIO_CHAR_RPAREN:
	    if (depth) {
		return idio_T_rparen;
	    } else {
		idio_error_read_parse (handle, "unexpected ')'");
		return idio_S_unspec;
	    }
	    break;
	case IDIO_CHAR_LBRACE:
	    return idio_read_block (handle, idio_T_rbrace, ic, depth + 1);
	case IDIO_CHAR_RBRACE:
	    if (depth) {
		return idio_T_rbrace;
	    } else {
		idio_error_read_parse (handle, "unexpected '}'");
		return idio_S_unspec;
	    }
	    break;
	case IDIO_CHAR_RBRACKET:
	    return idio_T_rbracket;
	    break;
	case IDIO_CHAR_RANGLE:
	    {
		c = idio_handle_peek (handle);
		switch (c) {
		case EOF:
		    return idio_T_rangle;
		case IDIO_CHAR_EQUALS:
		    return idio_read_word (handle, IDIO_CHAR_RANGLE);
		default:
		    return idio_T_rangle;
		}
	    }
	    break;
	case IDIO_CHAR_SQUOTE:
	    return idio_read_quote (handle, ic, depth);
	case IDIO_CHAR_BACKQUOTE:
	    {
		char qq_ic[] = { IDIO_CHAR_COMMA, IDIO_CHAR_AT, IDIO_CHAR_BACKSLASH };
		return idio_read_quasiquote (handle, qq_ic, depth);
	    }
	    /*
	case IDIO_CHAR_COMMA:
	    {
		c = idio_handle_getc (handle);
		if (IDIO_CHAR_AT == c) {
		    return idio_read_unquote_splicing (handle, ic, depth);
		}
		idio_handle_ungetc (handle, c);
		return idio_read_unquote (handle, ic, depth);
	    }
	    */
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
		    return idio_read_character (handle);
		case '(':
		    return idio_read_vector (handle, ic, depth + 1);
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
				bn = idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (bn));
			    }
			}

			if (! idio_isa_bignum (bn)) {
			    char em[BUFSIZ];
			    sprintf (em, "number expected after #%c: got %s", inexact ? 'i' : 'e', idio_type2string (bn));
			    idio_error_read_parse (handle, em);
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
		case '<':
		    {
			char em[BUFSIZ];
			sprintf (em, "not ready for # format: %c (%02x)", c, c);
			idio_error_read_parse (handle, em);
			return idio_S_unspec;
		    }
		default:
		    {
			char em[BUFSIZ];
			sprintf (em, "unexpected # format: %c (%02x)", c, c);
			idio_error_read_parse (handle, em);
			return idio_S_unspec;
		    }
		    
		}
	    }
	    break;
	case IDIO_CHAR_PERCENT:
	    {
		int c = idio_handle_getc (handle);
		switch (c) {
		case 'T':
		    return idio_read_template (handle, depth + 1);
		default:
		    idio_handle_ungetc (handle, c);
		    return idio_read_word (handle, IDIO_CHAR_PERCENT);
		}
	    }
	    break;
	case IDIO_CHAR_AMPERSAND:
	    {
		int cp = idio_handle_peek (handle);

		if (IDIO_SEPARATOR (cp)) {
		    if (depth) {
			return idio_T_ampersand;
		    } else {
			idio_error_read_parse (handle, "unexpected ampersand outside of list");
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

	c = idio_handle_getc (handle);
    }
}

/*
 * Looping around for EOF/EOL/"}" means that a single expression
 * becomes "(expr)" so check to see if the collected list is one
 * element long and use only the head if so.
 */
static IDIO idio_read_line (IDIO handle, IDIO closedel, char *ic, int depth)
{
    IDIO r = idio_S_nil;
    
    for (;;) {
	IDIO expr = idio_read_1_expr (handle, ic, depth);
	if (! (idio_T_eol == expr &&
	       idio_S_nil == r)) {
	}
	if (idio_S_eof == expr) {
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
		if (idio_S_nil == IDIO_PAIR_T (r)) {
		    r = IDIO_PAIR_H (r);
		} else {
		    r = idio_meaning_operators (r, 0);
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
		    r = idio_meaning_operators (r, 0);
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
		    r = idio_meaning_operators (r, 0);
		}
		return idio_pair (r, closedel);
	    } else {
		return idio_pair (r, closedel);
	    }
	} else {
	    r = idio_pair (expr, r);
	}
    }
}

static IDIO idio_read_block (IDIO handle, IDIO closedel, char *ic, int depth)
{
    IDIO r = idio_S_nil;
    
    for (;;) {
	IDIO line = idio_read_line (handle, closedel, ic, depth);
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

IDIO idio_read (IDIO handle)
{
    IDIO_ASSERT (handle);
    IDIO_TYPE_ASSERT (handle, handle);

    idio_gc_pause ();

    /*
     * dummy value for closedel
     */
    IDIO line = idio_read_line (handle, idio_T_eol, idio_default_interpolation_chars, 0);
    idio_gc_resume ();
    
    return IDIO_PAIR_H (line);
}

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
	return IDIO_CHARACTER (c);
    }
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

