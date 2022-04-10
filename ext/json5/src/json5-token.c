/*
 * Copyright (c) 2021-2022 Ian Fitchet <idf(at)idio-lang.org>
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

#include "json5-token.h"
#include "json5-unicode.h"
#include "utf8.h"

void json5_value_array_free (json5_array_t *a)
{
    if (NULL == a) {
	return;
    }

    for (; NULL != a;) {
	json5_free_value (a->element);
	json5_array_t *pa = a;
	a = a->next;
	free (pa);
    }
}

void json5_value_object_free (json5_object_t *o)
{
    if (NULL == o) {
	return;
    }

    for (; NULL != o;) {
	json5_free_value (o->name);
	json5_free_value (o->value);

	json5_object_t *po = o;
	o = o->next;
	free (po);
    }
}

void json5_free_value (json5_value_t *v)
{
    if (NULL == v) {
	return;
    }

    switch (v->type) {
    case JSON5_VALUE_NULL:
	free (v);
	break;
    case JSON5_VALUE_BOOLEAN:
	switch (v->u.l) {
	case JSON5_LITERAL_NULL:
	    /* shouldn't get here */
	    printf ("null?");
	    break;
	case JSON5_LITERAL_TRUE:
	    free (v);
	    break;
	case JSON5_LITERAL_FALSE:
	    free (v);
	    break;
	default:
	    /*
	     * Test Case: coding error?
	     */
	    json5_error_printf ("json5/value free: unexpected literal type %d", v->u.l);

	    /* notreached */
	    return;
	}
	break;
    case JSON5_VALUE_STRING:
	free (v->u.s->s);
	free (v->u.s);
	free (v);
	break;
    case JSON5_VALUE_NUMBER:
	free (v->u.n);
	free (v);
	break;
    case JSON5_VALUE_OBJECT:
	json5_value_object_free (v->u.o);
	free (v);
	break;
    case JSON5_VALUE_ARRAY:
	json5_value_array_free (v->u.a);
	free (v);
	break;
    case JSON5_VALUE_PUNCTUATOR:
	free (v);
	break;
    case JSON5_VALUE_IDENTIFIER:
	free (v->u.s->s);
	free (v->u.s);
	free (v);
	break;
    default:
	/*
	 * Test Case: coding error?
	 */
	json5_error_printf ("json5/value free: unexpected value type %d", v->type);

	/* notreached */
	return;
    }
}

void json5_token_free_remaining (json5_token_t *ct)
{
    for (; NULL != ct;) {
	json5_token_t *nt = ct->next;
	json5_free_value (ct->value);
	free (ct);
	ct = nt;
    }
}

/*
 * UES - UnicodeEscapeSequence only for Identifiers,
 * https://262.ecma-international.org/5.1/#sec-7.8.4
 */
static json5_unicode_string_t *json5_token_UES_identifier (json5_unicode_string_t *s, json5_token_t *ft, size_t start, size_t end)
{
    json5_unicode_string_t *so = (json5_unicode_string_t *) malloc (sizeof (json5_unicode_string_t));
    so->width = s->width;
    so->len = end - start;
    so->i = 0;
    so->s = (char *) malloc (so->len * so->width);

    s->i = start;
    for (size_t i = start; s->i < end; i++) {
	json5_unicode_t cp = json5_unicode_string_next (s);

	if ('\\' == cp) {
	    json5_unicode_t ecp;
	    if (json5_ECMA_UnicodeEscapeSequence (s, &ecp, ft, so)) {
		cp = ecp;
	    } else {
		free (so->s);
		free (so);
		free (s->s);
		free (s);
		json5_token_free_remaining (ft);

		/*
		 * Test Case: coding error?
		 *
		 * json5/parse-string "{ X\\x00: true }"
		 *
		 * where we know \xHH is invalid (can only be a
		 * UnicodeEscapeSequence in an ECMAIdentifier)
		 *
		 * This gets picked up at the bottom of
		 * json5_tokenize() as "json5/tokenize at 3: expected
		 * ECMAIdentifierStart: U+005C" where the construction
		 * of the ECMAIdentifier starts with X then stops with
		 * the invalid HexEscapeSequence.  It then immediately
		 * retries the next token starting with the invalid
		 * HexEscapeSequence and can fail.
		 *
		 * We don't get a look in.
		 */
		json5_error_printf ("json5/tokenize identifier at %zd: failed to recognise UnicodeEscapeSequence at %zd", start, i - start);

		/* notreached */
		return NULL;
	    }
	}

	json5_unicode_string_set (so, so->i++, cp);
    }

    /*
     * Set the actual string length
     */
    so->len = so->i;

    return so;
}

static json5_token_t *json5_token_string (json5_token_t *ct, json5_unicode_string_t *s, const json5_unicode_t delim, json5_token_t *ft)
{
    ct->next = (json5_token_t *) malloc (sizeof (json5_token_t));
    ct = ct->next;
    ct->next = NULL;
    ct->value = NULL;
    ct->type = JSON5_TOKEN_STRING;

    ct->start = s->i;

    /*
     * Figure out the string's extents.  This probably fails on an invalid string.
     */
    size_t s_start = s->i;
    size_t i = s->i;
    int done = 0;
    for (; i < s->len; i++) {
	json5_unicode_t cp = json5_unicode_string_peek (s, i);

	if ('\\' == cp) {
	    json5_unicode_t cp1 = json5_unicode_string_peek (s, i + 1);
	    if (JSON5_UNICODE_INVALID == cp1) {
		break;
	    }

	    if (delim == cp1 ||
		'\\' == cp1) {
		i += 1;		/* i++ in the for as well */
		continue;
	    }
	}

	if (delim == cp) {
	    done = 1;
	    break;
	}
    }

    if (0 == done) {
	free (s->s);
	free (s);
	json5_token_free_remaining (ft);

	/*
	 * Test Case: json5-errors/parse-unterminated-string.idio
	 *
	 * json5/parse-string "'hello"
	 */
	json5_error_printf ("json5/tokenize string at %zd: unterminated", s_start);

	/* notreached */
	return NULL;
    }

    /*
     * Create a string that is as many characters as code points
     * although escape sequences will result in a small reduction in
     * final string length.
     */
    json5_unicode_string_t *so = (json5_unicode_string_t *) malloc (sizeof (json5_unicode_string_t));
    so->width = s->width;
    so->len = i - ct->start;
    so->i = 0;
    so->s = (char *) malloc (so->len * so->width);

    for (; s->i < s->len;) {
	size_t start = s->i;
	json5_unicode_t cp = json5_unicode_string_peek (s, start);

	if (delim == cp) {
	    json5_unicode_string_next (s);
	    break;
	}

	json5_unicode_t ecp;
	if (json5_ECMA_LineTerminator (s, &ecp)) {
	    size_t s_i = s->i;
	    free (so->s);
	    free (so);
	    free (s->s);
	    free (s);
	    json5_token_free_remaining (ft);

	    /*
	     * Test Case: json5-errors/parse-string-unescaped-LineTerminator.idio
	     *
	     * json5/parse-string "'\n'"
	     *
	     * Note this is an Idio \n becoming a real newline
	     */
	    json5_error_printf ("json5/tokenize string at %zd: unescaped LineTerminator %#04X at %zd", s_start, ecp, s_i);

	    /* notreached */
	    return NULL;
	}

	if ('\\' == cp) {
	    s->i = start + 1;
	    if (json5_ECMA_EscapeSequence (s, &ecp, ft, so)) {
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

    /*
     * Set the actual string length
     */
    so->len = so->i;

    ct->end = s->i;

    ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
    ct->value->type = JSON5_VALUE_STRING;
    ct->value->u.s = so;

    return ct;
}

static json5_token_t *json5_token_number (json5_token_t *ct, json5_unicode_string_t *s, json5_token_t *ft)
{
    ct->next = (json5_token_t *) malloc (sizeof (json5_token_t));
    ct = ct->next;
    ct->next = NULL;
    ct->value = NULL;
    ct->type = JSON5_TOKEN_NUMBER;

    size_t n_start = s->i;
    ct->start = s->i;

    int sign = 0;
    int named = 0;		/* [+-][Infinity|NaN] */
    int dec = 1;		/* decimal or hex */
    int integer = 1;		/* integer or floating point */
    int leading_0 = 1;
    int trailing_dot = 0;
    int in_exp = 0;
    int exp_sign = 0;
    int digits = 0;
    int exp_digits = 0;

    int done = 0;
    for (; s->i < s->len;) {
	json5_unicode_t cp = json5_unicode_string_next (s);

	switch (cp) {
	case '+':
	case '-':
	    if (in_exp) {
		if (exp_sign) {
		    size_t s_i = s->i;
		    free (s->s);
		    free (s);
		    json5_token_free_remaining (ft);

		    /*
		     * Test Case: json5-errors/parse-number-double-signed-exponent.idio
		     *
		     * json5/parse-string "10e+-0"
		     */
		    json5_error_printf ("json5/tokenize number at %zd: double signed exponent at %zd", n_start, s_i - 2);

		    /* notreached */
		    return NULL;
		} else {
		    exp_sign = 1;
		    if ('-' == cp) {
			exp_sign = -1;
		    }
		}
	    } else {
		if (sign) {
		    free (s->s);
		    free (s);
		    json5_token_free_remaining (ft);

		    /*
		     * Test Case: json5-errors/parse-number-double-signed.idio
		     *
		     * json5/parse-string "+-10e+0"
		     */
		    json5_error_printf ("json5/tokenize number at %zd: double signed", n_start);

		    /* notreached */
		    return NULL;
		} else {
		    sign = 1;
		    if ('-' == cp) {
			sign = -1;
		    }
		}
	    }
	    break;

	case '0':
	    if (leading_0) {
		leading_0 = 0;
		json5_unicode_t cp1 = json5_unicode_string_peek (s, s->i);

		if (JSON5_UNICODE_INVALID == cp1) {
		    digits++;
		} else {
		    switch (cp1) {
		    case '.':	/* 0. */
			integer = 0;
			digits++;
			break;

		    case 'e':	/* 0e */
		    case 'E':	/* 0E */
			integer = 0;
			digits++;
			/* skip the e/E */
			s->i++;
			in_exp = 1;
			break;

		    case 'x':	/* 0x */
		    case 'X':	/* 0X */
			dec = 0;
			/* skip the x/X */
			s->i++;
			/* XXX no digits yet */
			break;

		    case ']':
		    case '}':
		    case ',':
			digits++;
			break;

		    default:
			free (s->s);
			free (s);
			json5_token_free_remaining (ft);

			/*
			 * Test Case: json5-errors/parse-number-leading-zero.idio
			 *
			 * json5/parse-string "0123"
			 */
			json5_error_printf ("json5/tokenize number at %zd: leading zero", n_start);

			/* notreached */
			return NULL;
		    }
		}
	    } else {
		if (in_exp) {
		    exp_digits++;
		} else {
		    digits++;
		}
		trailing_dot = 0;
	    }
	    break;

	case '.':
	    leading_0 = 0;
	    integer = 0;
	    if (in_exp) {
		size_t s_i = s->i;
		free (s->s);
		free (s);
		json5_token_free_remaining (ft);

		/*
		 * I wouldn't classify 1e2.3 as an error of having a
		 * floating point exponent but rather that someone has
		 * appended .2 to a valid number.
		 *
		 * I think that way because only integer exponents are
		 * valid numbers,
		 * https://262.ecma-international.org/5.1/#sec-7.8.3,
		 * and that to identify floating point exponents
		 * implies testing a potential grammar rather than
		 * sticking to a defined grammar.
		 *
		 * We only look out for it because
		 * https://github.com/json5/json5-tests claims it is
		 * an error.
		 */
		/*
		 * Test Case: json5-errors/parse-number-floating-point-exponent.idio
		 *
		 * json5/parse-string "1e2.3"
		 */
		json5_error_printf ("json5/tokenize number at %zd: floating point exponent at %zd", n_start, s_i - 1);

		/* notreached */
		return NULL;
	    }
	    trailing_dot = 1;
	    break;

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
	    trailing_dot = 0;
	    if (in_exp) {
		exp_digits++;
	    } else {
		digits++;
	    }
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
	    if (in_exp) {
		/*
		 * The exponent can only be a SignedInteger and not,
		 * say, Hex, or we have an IdentifierStart immediately
		 * after a NumericLiteral
		 */
		free (s->s);
		free (s);
		json5_token_free_remaining (ft);

		/*
		 * Test Case: json5-errors/parse-number-hex-digit-in-exponent.idio
		 *
		 * json5/parse-string "0ee"
		 */
		json5_error_printf ("json5/tokenize number at %zd: hex digit in exponent", n_start);

		/* notreached */
		return NULL;
	    }
	    if (dec) {
		if (!('e' == cp ||
		      'E' == cp)) {
		    size_t s_i = s->i;
		    free (s->s);
		    free (s);
		    json5_token_free_remaining (ft);

		    /*
		     * Test Case: json5-errors/parse-number-hex-in-decimal.idio
		     *
		     * json5/parse-string "1f"
		     */
		    json5_error_printf ("json5/tokenize number at %zd: hex in decimal: '%c' at %zd", n_start, cp, s_i - 1);

		    /* notreached */
		    return NULL;
		}

		if (0 == digits) {
		    free (s->s);
		    free (s);
		    json5_token_free_remaining (ft);

		    /*
		     * Test Case: json5-errors/parse-number-no-mantissa-digits.idio
		     *
		     * json5/parse-string ".e"
		     */
		    json5_error_printf ("json5/tokenize number at %zd: no mantissa digits", n_start);

		    /* notreached */
		    return NULL;
		}

		in_exp = 1;
		integer = 0;
	    }
	    digits++;
	    break;

	default:
	    if ('I' == cp &&
		json5_unicode_string_n_equal (s, "nfinity", 7)) {
		named = 1;
		ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
		ct->value->type = JSON5_VALUE_NUMBER;
		ct->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
		ct->value->u.n->type = -1 ==sign ? JSON5_NUMBER_NEG_INFINITY : JSON5_NUMBER_INFINITY;

		/*
		 * We haven't strictly seen any digits but we have
		 * seen a JSON5 number
		 */
		digits++;

		s->i += 8;	/* pretend to go one over */
		done = 1;
	    } else if ('N' == cp &&
		       json5_unicode_string_n_equal (s, "aN", 2)) {
		named = 1;
		ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
		ct->value->type = JSON5_VALUE_NUMBER;
		ct->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
		ct->value->u.n->type = -1 == sign ? JSON5_NUMBER_NEG_NAN : JSON5_NUMBER_NAN;

		/*
		 * We haven't strictly seen any digits but we have
		 * seen a JSON5 number
		 */
		digits++;

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
     * Normally we will have gone one character beyond the end of the
     * number -- unless the number was the last (only!) thing in the
     * JSON in which case we are at EOS
     */
    if (done) {
	s->i--;
    }
    ct->end = s->i;

    if (0 == digits) {
	free (s->s);
	free (s);
	json5_token_free_remaining (ft);

	/*
	 * Test Case: json5-errors/parse-number-no-digits.idio
	 *
	 * json5/parse-string "."
	 */
	json5_error_printf ("json5/tokenize number at %zd: no digits", n_start);

	/* notreached */
	return NULL;
    }

    if (dec &&
	in_exp &&
	0 == exp_digits &&
	0 == trailing_dot) {
	free (s->s);
	free (s);
	json5_token_free_remaining (ft);

	/*
	 * Test Case: json5-errors/parse-number-no-exponent-digits.idio
	 *
	 * json5/parse-string ".0e"
	 */
	json5_error_printf ("json5/tokenize number at %zd: no exponent digits", n_start);

	/* notreached */
	return NULL;
    }

    /*
     * https://262.ecma-international.org/5.1/#sec-7.8.3
     *
     * The source character immediately following a NumericLiteral
     * must not be an IdentifierStart or DecimalDigit.
     */
    json5_unicode_t cp = json5_unicode_string_peek (s, s->i);

    if (JSON5_UNICODE_INVALID != cp) {
	if (json5_ECMA_IdentifierStart (cp, s) ||
	    isdigit (cp)) {
	    size_t s_i = s->i;
	    free (s->s);
	    free (s);
	    json5_token_free_remaining (ft);

	    /*
	     * Test Case: json5-errors/parse-number-invalid-next-character.idio
	     *
	     * json5/parse-string "1X"
	     */
	    json5_error_printf ("json5/tokenize number at %zd: followed by U+%04X at %zd", n_start, cp, s_i);

	    /* notreached */
	    return NULL;
	}
    }

    size_t start = ct->start;

    cp = json5_unicode_string_peek (s, start);
    if ('+' == cp ||
	'-' == cp) {
	start++;
    }

    if (named) {
    } else if ((dec && integer) ||
	0 == dec) {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->value->type = JSON5_VALUE_NUMBER;
	json5_number_t * np = (json5_number_t *) malloc (sizeof (json5_number_t));
	ct->value->u.n = np;
	np->type = ECMA_NUMBER_INTEGER;
	np->u.i = 0;

	int base = 10;
	if (0 == dec) {
	    base = 16;
	    start += 2;		/* skip the leading 0x */
	}

	for (size_t i = start; i < ct->end; i++) {
	    np->u.i *= base;
	    np->u.i += h2i (json5_unicode_string_peek (s, i));
	}

	if (-1 == sign) {
	    np->u.i = - np->u.i;
	}
    } else {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->value->type = JSON5_VALUE_NUMBER;
	json5_number_t * np = (json5_number_t *) malloc (sizeof (json5_number_t));
	ct->value->u.n = np;
	np->type = ECMA_NUMBER_FLOAT;
	np->u.f = 0;

	int dp = 0;		/* seen decimal places incl . */

	in_exp = 0;
	int exp = 0;
	for (size_t i = start; i < ct->end; i++) {
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

    return ct;
}

static json5_token_t *json5_token_identifier (json5_token_t *ct, json5_unicode_string_t *s, json5_token_t *ft)
{
    ct->next = (json5_token_t *) malloc (sizeof (json5_token_t));
    ct = ct->next;
    ct->next = NULL;
    ct->value = NULL;
    ct->type = JSON5_TOKEN_IDENTIFIER;

    ct->start = s->i;

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

    ct->end = s->i;

    s->i = ct->start;
    if (json5_unicode_string_n_equal (s, "null", 4)) {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->value->type = JSON5_VALUE_NULL;
	ct->value->u.l = JSON5_LITERAL_NULL;
    } else if (json5_unicode_string_n_equal (s, "true", 4)) {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->value->type = JSON5_VALUE_BOOLEAN;
	ct->value->u.l = JSON5_LITERAL_TRUE;
    } else if (json5_unicode_string_n_equal (s, "false", 4)) {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->value->type = JSON5_VALUE_BOOLEAN;
	ct->value->u.l = JSON5_LITERAL_FALSE;
    } else if (json5_unicode_string_n_equal (s, "Infinity", 8)) {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->type = JSON5_TOKEN_NUMBER;
	ct->value->type = JSON5_VALUE_NUMBER;
	ct->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
	ct->value->u.n->type = JSON5_NUMBER_INFINITY;
    } else if (json5_unicode_string_n_equal (s, "NaN", 3)) {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->type = JSON5_TOKEN_NUMBER;
	ct->value->type = JSON5_VALUE_NUMBER;
	ct->value->u.n = (json5_number_t *) malloc (sizeof (json5_number_t));
	ct->value->u.n->type = JSON5_NUMBER_NAN;
    } else {
	ct->value = (json5_value_t *) malloc (sizeof (json5_value_t));
	ct->value->type = JSON5_VALUE_IDENTIFIER;
	ct->value->u.s = json5_token_UES_identifier (s, ft, ct->start, ct->end);

	s->i = ct->start;
    }
    s->i = ct->end;

    return ct;
}

json5_token_t *json5_tokenize (json5_unicode_string_t *s)
{
    json5_token_t *root = (json5_token_t *) malloc (sizeof (json5_token_t));
    root->next = NULL;
    root->value = NULL;
    root->type = JSON5_TOKEN_ROOT;

    json5_token_t *ct = root;

    for (; s->i < s->len;) {
	json5_unicode_skip_ws (s);

	if (s->i >= s->len) {
	    json5_token_free_remaining (root);
	    free (s->s);
	    free (s);

	    /*
	     * Test Case: json5-errors/parse-blank-string.idio
	     *
	     * json5/parse-string "  "
	     */
	    json5_error_printf ("json5/tokenize: no tokens");

	    /* notreached */
	    return NULL;
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
		    json5_unicode_skip_bc (s, root);
		} else {
		    size_t s_i = s->i;
		    json5_token_free_remaining (root);
		    free (s->s);
		    free (s);

		    /*
		     * Test Case: json5-errors/parse-single-forward-slash.idio
		     *
		     * json5/parse-string "/ / comment"
		     */
		    json5_error_printf ("json5/tokenize at %zd: unexpected /", s_i - 1);

		    /* notreached */
		    return NULL;
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
		    json5_token_free_remaining (root);
		    size_t s_i = s->i;
		    free (s->s);
		    free (s);

		    /*
		     * Test Case: coding error?
		     */
		    json5_error_printf ("json5/tokenize at %zd: U+%04X: unexpected JSON5 punctuator", s_i, cp);

		    /* notreached */
		    return NULL;
		}
		ct->start = start;
		ct->end = s->i;
	    }
	    break;

	case '"':
	case '\'':
	    ct = json5_token_string (ct, s, cp, root);
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
	    ct = json5_token_number (ct, s, root);
	    break;

	default:
	    if (json5_ECMA_IdentifierStart (cp, s)) {
		s->i = start;
		ct = json5_token_identifier (ct, s, root);
	    } else {
		json5_token_free_remaining (root);
		free (s->s);
		free (s);

		/*
		 * Test Case: json5-errors/parse-punctuation.idio
		 *
		 * json5/parse-string "*"
		 */
		json5_error_printf ("json5/tokenize at %zd: expected ECMAIdentifierStart: U+%04X", start, cp);

		/* notreached */
		return NULL;
	    }
	}

	json5_unicode_skip_ws (s);

	if (s->i >= s->len) {
	    break;
	}
    }

    return root;
}

json5_token_t *json5_tokenize_string (json5_unicode_string_t *so)
{
    json5_token_t *root = json5_tokenize (so);

    return root;
}

json5_token_t *json5_tokenize_string_C (char *s_C, size_t slen)
{
    json5_unicode_string_t *so = json5_utf8_string_C_len (s_C, slen);

    json5_token_t *root = json5_tokenize (so);

    free (so->s);
    free (so);

    return root;
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
