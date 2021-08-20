/*
 * Copyright (c) 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * json5-token.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "json5-unicode.h"
#include "json5-token.h"
#include "utf8.h"

/*
 * https://262.ecma-international.org/5.1/#sec-7.6.1.1
 */
static const char *json5_ECMA_keywords[] = {
    "break",	"do",		"instanceof",	"typeof",
    "case",	"else",		"new",		"var",
    "catch",	"finally",	"return",	"void",
    "continue",	"for",		"switch",	"while",
    "debugger",	"function",	"this",		"with",
    "default",	"if",		"throw",
    "delete",	"in",		"try",
};

/*
 * https://262.ecma-international.org/5.1/#sec-7.6.1.2
 */
static const char *json5_ECMA_future[] = {
    "class",		"enum",		"extends",	"super",
    "const",		"export",	"import",
    "implements",	"let",		"private",	"public",	"yield",
    "interface",	"package",	"protected",	"static",
};

/*
 * UES - UnicodeEscapeSequence only for Identifiers,
 * https://262.ecma-international.org/5.1/#sec-7.8.4
 */
static json5_unicode_string_t *json5_token_UES_identifier (json5_unicode_string_t *s, size_t start, size_t end, int UES)
{
    json5_unicode_string_t *so = (json5_unicode_string_t *) malloc (sizeof (json5_unicode_string_t));
    so->width = s->width;
    so->len = end - start;
    so->i = 0;
    so->s = (char *) malloc (so->len * so->width);

    for (size_t i = start; i < end; i++) {
	json5_unicode_t cp = json5_unicode_string_peek (s, i);

	if ('\\' == cp) {
	    json5_unicode_t ecp;
	    if (json5_ECMA_UnicodeEscapeSequence (s, &ecp)) {
		cp = ecp;
	    } else {
		fprintf (stderr, "_token_esc_id: failed to recognise UES at %zd in %zd - %zd\n", i, start, end);
		exit (1);
	    }
	}

	json5_unicode_string_set (so, so->i++, cp);
    }

    return so;
}

static json5_token_t *json5_token_string (json5_unicode_string_t *s, const json5_unicode_t delim)
{
    json5_token_t *token = (json5_token_t *) malloc (sizeof (json5_token_t));
    token->next = NULL;
    token->type = JSON5_TOKEN_STRING;

    token->start = s->i;

    /*
     * Figure out the string's extents.  This probably fails on an invalid string.
     */
    size_t i = s->i;
    for (; s->i < s->len; i++) {
	json5_unicode_t cp = json5_unicode_string_peek (s, i);
	if ('\\' == cp) {
	    json5_unicode_t cp1 = json5_unicode_string_peek (s, i + 1);
	    if (delim == cp1 ||
		'\\' == cp1) {
		i += 1;		/* i++ in the for as well */
		continue;
	    }
	}

	if (delim == cp) {
	    break;
	}
    }

    /*
     * Create a string that is as many characters as code points
     * although escape sequences will result in a small reduction in
     * final string length.
     */
    json5_unicode_string_t *so = (json5_unicode_string_t *) malloc (sizeof (json5_unicode_string_t));
    so->width = s->width;
    so->len = i - token->start;
    so->i = 0;
    so->s = (char *) malloc (so->len * so->width);

    for (; s->i < s->len;) {
	size_t start = s->i;
	json5_unicode_t cp = json5_unicode_string_peek (s, s->i);

	if (delim == cp) {
	    json5_unicode_string_next (s);
	    break;
	}

	json5_unicode_t ecp;
	if (json5_ECMA_LineTerminator (s, &ecp)) {
	    fprintf (stderr, "token_string: unexpected LineTerminator %#04X at %zd in %zd - %zd\n", ecp, s->i, token->start, i);
	    exit (1);
	}

	if ('\\' == cp) {
	    s->i = start + 1;
	    if (json5_ECMA_EscapeSequence (s, &ecp)) {
		cp = ecp;
	    } else {
		s->i = start + 1;
		if (json5_ECMA_LineTerminatorSequence (s, &ecp)) {
		    continue;
		}
	    }
	}

	json5_unicode_string_set (so, so->i++, cp);
    }

    token->end = s->i;

    token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
    token->value->type = JSON5_VALUE_STRING;
    token->value->u.s = so;
    return token;
}

static json5_token_t *json5_token_number (json5_unicode_string_t *s)
{
    json5_token_t *token = (json5_token_t *) malloc (sizeof (json5_token_t));
    token->next = NULL;
    token->type = JSON5_TOKEN_NUMBER;

    token->start = s->i;

    int sign = 0;
    int named = 0;		/* [+-][Infinity|NaN] */
    int dec = 1;		/* decimal or hex */
    int integer = 1;		/* integer or floating point */
    int leading_0 = 1;
    int in_exp = 0;
    int exp_sign = 0;

    int done = 0;
    for (; s->i < s->len;) {
	json5_unicode_t cp = json5_unicode_string_next (s);

	switch (cp) {
	case '+':
	case '-':
	    if (in_exp) {
		if (exp_sign) {
		    fprintf (stderr, "_number: double signed exp %zd\n", s->i - 2);
		    exit (1);
		} else {
		    exp_sign = 1;
		    if ('-' == cp) {
			exp_sign = -1;
		    }
		}
	    } else {
		if (sign) {
		    fprintf (stderr, "_number: double signed %zd\n", s->i - 2);
		    exit (1);
		} else {
		    sign = 1;
		    if ('-' == cp) {
			sign = -1;
		    }
		}
	    }
	    break;

	case '0':
	    {
		if (leading_0) {
		    leading_0 = 0;
		    json5_unicode_t cp1 = json5_unicode_string_peek (s, s->i);

		    switch (cp1) {
		    case '.':	/* 0. */
			integer = 0;
		    case 'e':	/* 0e */
		    case 'E':	/* 0E */
			s->i++;
			break;

		    case 'x':	/* 0x */
		    case 'X':	/* 0X */
			dec = 0;
			s->i++;
			break;

		    default:
			{
			    if (json5_ECMA_IdentifierStart (cp1, s) ||
				isdigit (cp1)) {
				fprintf (stderr, "_number: leading zero: 0%c %04X at %zd\n", cp1, cp1, s->i - 1);
				exit (1);
			    }
			}

		    }
		}
	    }
	    break;

	case '.':
	    integer = 0;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	    leading_0 = 0;
	    break;

	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'f':
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F':
	    leading_0 = 0;
	    if (dec) {
		if (!('e' == cp ||
		      'E' == cp)) {
		    fprintf (stderr, "_number: hex in dec : %c at %zd\n", cp, s->i);
		    exit (1);
		}
		in_exp = 1;
		integer = 0;
	    }
	    break;

	default:
	    if ('I' == cp &&
		json5_unicode_string_n_equal (s, "nfinity", 7)) {
		named = 1;
		token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
		token->value->type = JSON5_VALUE_NUMBER;
		token->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
		token->value->u.n->type = -1 ==sign ? JSON5_NUMBER_NEG_INFINITY : JSON5_NUMBER_INFINITY;

		s->i += 8;	/* pretend to go one over */
		done = 1;
	    } else if ('N' == cp &&
		       json5_unicode_string_n_equal (s, "aN", 2)) {
		named = 1;
		token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
		token->value->type = JSON5_VALUE_NUMBER;
		token->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
		token->value->u.n->type = -1 == sign ? JSON5_NUMBER_NEG_NAN : JSON5_NUMBER_NAN;

		s->i += 3;	/* pretend to go one over */
		done = 1;
	    } else {
		done = 1;
	    }
	    break;
	}

	if (done) {
	    break;
	}
    }

    /*
     * We went one over
     */
    s->i--;
    token->end = s->i;

    /*
     * XXX must not be followed by an IdentifierStart or DecimalDigit
     */
    json5_unicode_t cp = json5_unicode_string_peek (s, s->i);

    if (json5_ECMA_IdentifierStart (cp, s) ||
	isdigit (cp)) {
	fprintf (stderr, "_number: followed by: %04X at %zd\n", cp, s->i);
	exit (1);
    }

    size_t start = token->start;

    cp = json5_unicode_string_peek (s, start);
    if ('+' == cp ||
	'-' == cp) {
	start++;
    }

    if (named) {
    } else if ((dec && integer) ||
	0 == dec) {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->value->type = JSON5_VALUE_NUMBER;
	json5_number_t * np = (json5_number_t *) malloc (sizeof (json5_number_t));
	token->value->u.n = np;
	np->type = ECMA_NUMBER_INTEGER;
	np->u.i = 0;

	int base = 10;
	if (0 == dec) {
	    base = 16;
	    start += 2;		/* skip the leading 0x */
	}

	for (size_t i = start; i < token->end; i++) {
	    np->u.i *= base;
	    np->u.i += h2i (json5_unicode_string_peek (s, i));
	}

	if (-1 == sign) {
	    np->u.i = - np->u.i;
	}
    } else {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->value->type = JSON5_VALUE_NUMBER;
	json5_number_t * np = (json5_number_t *) malloc (sizeof (json5_number_t));
	token->value->u.n = np;
	np->type = ECMA_NUMBER_FLOAT;
	np->u.f = 0;

	int dp = 0;		/* seen decimal places incl . */

	in_exp = 0;
	int exp = 0;
	for (size_t i = start; i < token->end; i++) {
	    json5_unicode_t cp = json5_unicode_string_peek (s, i);

	    if ('.' == cp) {
		dp = 1;
	    } else if ('e' == cp ||
		       'E' == cp) {
		in_exp = 1;
	    } else {
		if (in_exp) {
		    if (!('+' == cp ||
			  '-' == cp)) {
			exp *= 10;
			exp += h2i (cp);
		    }
		} else {
		    np->u.f *= 10;
		    np->u.f += h2i (cp);
		    if (dp) {
			dp++;
		    }
		}
	    }
	}

	if (dp > 1) {
	    dp--;		/* for . itself */
	    for (;dp > 0; dp --) {
		np->u.f /= 10;
	    }
	}

	if (exp) {
	    if (exp > 324) {
		fprintf (stderr, "WARNING: exponent %c%d for %e: out of range of double\n", -1 == exp_sign ? '-' : '+', exp, np->u.f);
	    }
	    if (-1 == exp_sign) {
		for (int i = 0; i < exp; i++) {
		    np->u.f *= 0.1;
		}
	    } else {
		for (int i = 0; i < exp; i++) {
		    np->u.f *= 10;
		}
	    }
	}

	if (-1 == sign) {
	    np->u.f = - np->u.f;
	}
    }

    return token;
}

static json5_token_t *json5_token_identifier (json5_unicode_string_t *s)
{
    json5_token_t *token = (json5_token_t *) malloc (sizeof (json5_token_t));
    token->next = NULL;
    token->type = JSON5_TOKEN_IDENTIFIER;

    token->start = s->i;

    int done = 0;
    for (; s->i < s->len;) {
	size_t start = s->i;
	json5_unicode_t cp = json5_unicode_string_next (s);

	if (json5_ECMA_IdentifierPart (cp, s) == 0) {
	    s->i = start;
	    done = 1;
	}

	if (done) {
	    break;
	}
    }

    token->end = s->i;

    s->i = token->start;
    if (json5_unicode_string_n_equal (s, "null", 4)) {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->value->type = JSON5_VALUE_NULL;
	token->value->u.l = JSON5_LITERAL_NULL;
    } else if (json5_unicode_string_n_equal (s, "true", 4)) {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->value->type = JSON5_VALUE_BOOLEAN;
	token->value->u.l = JSON5_LITERAL_TRUE;
    } else if (json5_unicode_string_n_equal (s, "false", 4)) {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->value->type = JSON5_VALUE_BOOLEAN;
	token->value->u.l = JSON5_LITERAL_FALSE;
    } else if (json5_unicode_string_n_equal (s, "Infinity", 8)) {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->type = JSON5_TOKEN_NUMBER;
	token->value->type = JSON5_VALUE_NUMBER;
	token->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
	token->value->u.n->type = JSON5_NUMBER_INFINITY;
    } else if (json5_unicode_string_n_equal (s, "NaN", 3)) {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->type = JSON5_TOKEN_NUMBER;
	token->value->type = JSON5_VALUE_NUMBER;
	token->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
	token->value->u.n->type = JSON5_NUMBER_NAN;
    } else {
	token->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	token->value->type = JSON5_VALUE_IDENTIFIER;
	token->value->u.s = json5_token_UES_identifier (s, token->start, token->end, 1);

	size_t slen = token->end - token->start;
	for (const char **k = json5_ECMA_keywords; *k; k++) {
	    size_t klen = strlen (*k);
	    if (slen == klen &&
		json5_unicode_string_n_equal (s, *k, klen)) {
		fprintf (stderr, "identifier is a keyword: %s\n", *k);
		exit (1);
	    }
	}
	for (const char **frw = json5_ECMA_future; *frw; frw++) {
	    size_t frwlen = strlen (*frw);
	    if (slen == frwlen &&
		json5_unicode_string_n_equal (s, *frw, frwlen)) {
		fprintf (stderr, "identifier is a future reserved word: %s\n", *frw);
		exit (1);
	    }
	}
    }
    s->i = token->end;

    return token;
}

json5_token_t *json5_tokenize (json5_unicode_string_t *s)
{
    json5_token_t *root = (json5_token_t *) malloc (sizeof (json5_token_t));
    root->next = NULL;
    root->type = JSON5_TOKEN_ROOT;

    json5_token_t *ct = root;

    for (; s->i < s->len;) {
	json5_unicode_skip_ws (s);

	if (s->i >= s->len) {
	    fprintf (stderr, "_tokenize leading ws -> EOS\n");
	    break;
	}

	size_t start = s->i;
	json5_unicode_t cp = json5_unicode_string_next (s);

	switch (cp) {
	case '/':
	    {
		json5_unicode_t cp1 = json5_unicode_string_peek (s, s->i);
		if ('/' == cp1) {
		    json5_unicode_skip_slc (s);
		} else if ('*' == cp1) {
		    json5_unicode_skip_mlc (s);
		} else {
		    fprintf (stderr, "_tokenize: unexpected / at %zd\n", s->i - 1);
		    break;
		}
	    }
	    break;

	case '{':
	case '}':
	case '[':
	case ']':
	case ':':
	case ',':
	    {
		ct->next = (json5_token_t *) malloc (sizeof (json5_token_t));
		ct = ct->next;
		ct->next = NULL;
		ct->type = JSON5_TOKEN_PUNCTUATOR;
		ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
		ct->value->type = JSON5_VALUE_PUNCTUATOR;
		switch (cp) {
		case '{':
		    ct->value->u.p = JSON5_PUNCTUATOR_LBRACE;
		    break;
		case '}':
		    ct->value->u.p = JSON5_PUNCTUATOR_RBRACE;
		    break;
		case '[':
		    ct->value->u.p = JSON5_PUNCTUATOR_LBRACKET;
		    break;
		case ']':
		    ct->value->u.p = JSON5_PUNCTUATOR_RBRACKET;
		    break;
		case ':':
		    ct->value->u.p = JSON5_PUNCTUATOR_COLON;
		    break;
		case ',':
		    ct->value->u.p = JSON5_PUNCTUATOR_COMMA;
		    break;
		default:
		    fprintf (stderr, "_tokenize: %04X: missed a punctuation clause\n", cp);
		    exit (1);
		}
		ct->start = start;
		ct->end = s->i;
	    }
	    break;

	case '"':
	case '\'':
	    ct->next = json5_token_string (s, cp);
	    ct = ct->next;
	    break;

	    /*
	     * NumericLiteral
	     */
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case '+':
	case '-':
	case '.':
	    s->i = start;
	    ct->next = json5_token_number (s);
	    ct = ct->next;
	    break;

	default:
	    if (json5_ECMA_IdentifierStart (cp, s)) {
		s->i = start;
		ct->next = json5_token_identifier (s);
		ct = ct->next;
	    } else {
		fprintf (stderr, "_tokenize + %04X at %zd\n", cp, start);
		break;
	    }
	}

	json5_unicode_skip_ws (s);

	if (s->i >= s->len) {
	    fprintf (stderr, "_tokenize trailing ws -> EOS\n");
	    break;
	}
    }

    return root;
}

json5_token_t *json5_tokenize_string_C (char *s_C, size_t slen)
{
    json5_unicode_string_t *s = json5_utf8_string_C_len (s_C, slen);

    json5_token_t *root = json5_tokenize (s);

    return root;
}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: utf-8-unix */
/* End: */
