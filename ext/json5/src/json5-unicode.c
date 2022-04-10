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
 * json5-unicode.c
 *
 */

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "json5-token.h"
#include "json5-unicode.h"
#include "usi.h"
#include "usi-wrap.h"
#include "utf8.h"

unsigned int h2i (char c)
{
    if (c >= '0' &&
	c <= '9') {
	return c - '0';
    } else if (c >= 'A' &&
	       c <= 'F') {
	return c - 'A' + 10;
    } else if (c >= 'a' &&
	      c <= 'f') {
	return c - 'a' + 10;
    } else {
	/*
	 * Test Case: coding error?
	 *
	 * Everywhere where we call h2i(x) we have previously called
	 * isxdigit(x).  We're just defending against developer error,
	 * here.
	 */
	json5_error_printf ("hex2integer: %#04X is not a hex digit", c);

	/* notreached */
	return -1;
    }
}

int json5_unicode_valid_code_point (json5_unicode_t cp)
{
    if (cp > 0x10FFFF) {
	return 0;
    }

    return 1;
}

/*
 * All of the json5_ECMA_* tests will return 1 or 0 if they match or
 * fail.  If they match *cpp will have been assigned the code point.
 * If they do not match, *cpp is undefined.
 *
 * They will all have moved the underlying json5_unicode_string_t
 * pointer forwards.  It is up to the caller to reset it.
 */

int json5_ECMA_LineTerminator (json5_unicode_string_t *s, json5_unicode_t *cpp)
{
    int r = 1;

    if (json5_unicode_string_available (s, 1)) {
	json5_unicode_t cp = json5_unicode_string_next (s);
	switch (cp) {
	case 0x0A:   *cpp = cp; break;	/* Line feed */
	case 0x0D:   *cpp = cp; break;	/* Carriage return */
	case 0x2028: *cpp = cp; break;	/* Line separator */
	case 0x2029: *cpp = cp; break;	/* Paragraph separator */
	default:
	    r = 0;
	    break;
	}
    } else {
	r = 0;
    }

    return r;
}

int json5_ECMA_LineTerminatorSequence (json5_unicode_string_t *s, json5_unicode_t *cpp)
{
    int r = 1;

    if (json5_unicode_string_available (s, 1)) {
	json5_unicode_t cp = json5_unicode_string_next (s);
	if (JSON5_UNICODE_INVALID == cp) {
	    return 0;
	}

	switch (cp) {
	case 0x0A:   *cpp = cp; break;	/* Line feed */
	case 0x2028: *cpp = cp; break;	/* Line separator */
	case 0x2029: *cpp = cp; break;	/* Paragraph separator */
	case 0x0D:
	    if (json5_unicode_string_available (s, 1)) {
		json5_unicode_t cp1 = json5_unicode_string_peek (s, s->i);
		if (JSON5_UNICODE_INVALID == cp1) {
		    return 0;
		}

		if (0x0A == cp1) {
		    json5_unicode_string_next (s);
		}
	    }
	    *cpp = cp; break;	/* Carriage return */
	default:
	    r = 0;
	    break;
	}
    } else {
	r = 0;
    }

    return r;
}

int json5_ECMA_SingleEscapeCharacter (json5_unicode_string_t *s, json5_unicode_t *cpp)
{
    int r = 1;

    if (json5_unicode_string_available (s, 1)) {
	json5_unicode_t cp = json5_unicode_string_next (s);
	switch (cp) {
	case '\'': *cpp = 0x27; break;
	case '"':  *cpp = 0x22; break;
	case '\\': *cpp = 0x5C; break;
	case 'b':  *cpp = 0x08; break;
	case 'f':  *cpp = 0x0C; break;
	case 'n':  *cpp = 0x0A; break;
	case 'r':  *cpp = 0x0D; break;
	case 't':  *cpp = 0x09; break;
	case 'v':  *cpp = 0x0B; break;
	default:
	    r = 0;
	    break;
	}
    } else {
	r = 0;
    }

    return r;
}

int json5_ECMA_NonEscapeCharacter (json5_unicode_string_t *s, json5_unicode_t *cpp)
{
    int r = 1;

    size_t start = s->i;
    if (json5_ECMA_SingleEscapeCharacter (s, cpp)) {
	return 0;
    }

    s->i = start;
    if (json5_unicode_string_available (s, 1)) {
	*cpp = json5_unicode_string_next (s);

	if (r &&
	    *cpp >= '0' &&
	    *cpp <= '9') {
	    r = 0;
	}

	if (r &&
	    ('x' == *cpp ||
	     'u' == *cpp)) {
	    r = 0;
	}

	if (r) {
	    s->i = start;
	    if (json5_ECMA_LineTerminator (s, cpp)) {
		r = 0;
	    }
	}
    } else {
	r = 0;
    }

    return r;
}

int json5_ECMA_CharacterEscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp)
{
    size_t start = s->i;

    if (json5_ECMA_SingleEscapeCharacter (s, cpp)) {
	return 1;
    }

    s->i = start;
    if (json5_ECMA_NonEscapeCharacter (s, cpp)) {
	return 1;
    }

    return 0;
}

int json5_ECMA_HexEscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp, json5_token_t *ft, json5_unicode_string_t *so)
{
    int r = 1;

    if (json5_unicode_string_available (s, 1)) {
	json5_unicode_t cp1 = json5_unicode_string_next (s);
	switch (cp1) {
	case 'x':
	    /*
	     * Four char \xHH esc sequences
	     */
	    if (json5_unicode_string_available (s, 2)) {
		json5_unicode_t h1 = json5_unicode_string_next (s);
		json5_unicode_t h2 = json5_unicode_string_next (s);
		if (isxdigit (h1) &&
		    isxdigit (h2)) {
		    *cpp = (h2i (h1) << 4) + h2i (h2);
		} else {
		    free (s->s);
		    free (s);
		    json5_token_free_remaining (ft);
		    if (NULL != so) {
			free (so->s);
			free (so);
		    }

		    /*
		     * Test Case: json5-errors/parse-ECMA_HexEscapeSequence-bad-digit.idio
		     *
		     * json5/parse-string " '\\xqq' "
		     *
		     * NB. \\x as this is an Idio string!
		     */
		    json5_error_printf ("json5/ECMA_HexEscapeSequence: \\%c U+%04X U+%04X", cp1, h1, h2);

		    /* notreached */
		    return 0;
		}
	    } else {
		r = 0;
	    }
	    break;
	default:
	    r = 0;
	    break;
	}
    } else {
	r = 0;
    }

    return r;
}

int json5_ECMA_UnicodeEscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp, json5_token_t *ft, json5_unicode_string_t *so)
{
    int r = 1;

    if (json5_unicode_string_available (s, 1)) {
	json5_unicode_t cp1 = json5_unicode_string_next (s);
	switch (cp1) {
	case 'u':
	    /*
	     * Six char \uHHHH esc sequences
	     */
	    if (json5_unicode_string_available (s, 4)) {
		json5_unicode_t h1 = json5_unicode_string_next (s);
		json5_unicode_t h2 = json5_unicode_string_next (s);
		json5_unicode_t h3 = json5_unicode_string_next (s);
		json5_unicode_t h4 = json5_unicode_string_next (s);
		if (isxdigit (h1) &&
		    isxdigit (h2) &&
		    isxdigit (h3) &&
		    isxdigit (h4)) {

		    /*
		     * Surrogate pairs use UTF-16 encodings
		     */
		    uint16_t hs = (h2i (h1) << 12) + (h2i (h2) << 8) + (h2i (h3) << 4) + h2i (h4);
		    if (hs >= 0xD800 &&
			hs <= 0xDBFF) {
			/*
			 * There must be a low surrogate following
			 */
			if (json5_unicode_string_available (s, 6)) {
			    json5_unicode_t ls_esc = json5_unicode_string_next (s);
			    json5_unicode_t ls_u = json5_unicode_string_next (s);
			    json5_unicode_t ls_h1 = json5_unicode_string_next (s);
			    json5_unicode_t ls_h2 = json5_unicode_string_next (s);
			    json5_unicode_t ls_h3 = json5_unicode_string_next (s);
			    json5_unicode_t ls_h4 = json5_unicode_string_next (s);
			    if ('\\' == ls_esc &&
				'u' == ls_u &&
				isxdigit (ls_h1) &&
				isxdigit (ls_h2) &&
				isxdigit (ls_h3) &&
				isxdigit (ls_h4)) {

				/*
				 * Surrogate pairs use UTF-16 encodings
				 */
				uint16_t ls = (h2i (ls_h1) << 12) + (h2i (ls_h2) << 8) + (h2i (ls_h3) << 4) + h2i (ls_h4);
				if (ls >= 0xDC00 &&
				    ls <= 0xDFFF) {
				    *cpp = 0x10000 + ((hs - 0xD800) * 0x400) + (ls - 0xDC00);
				} else {
				    size_t s_i = s->i;
				    free (s->s);
				    free (s);
				    json5_token_free_remaining (ft);
				    if (NULL != so) {
					free (so->s);
					free (so);
				    }

				    /*
				     * Test Case: json5-errors/parse-ECMA_UnicodeEscapeSequence-invalid-low-surrogate-range.idio
				     *
				     * json5/parse-string " '\\uD800\\u0061' "
				     *
				     * NB. \\x as this is an Idio string!
				     */
				    json5_error_printf ("json5/ECMA_UnicodeEscapeSequence: low surrogate range 0xDC00 <= %#06X <= 0xDFFF at %zd", ls, s_i - 6);
				    /* notreached */
				    return 0;
				}
			    } else {
				size_t s_i = s->i;
				free (s->s);
				free (s);
				json5_token_free_remaining (ft);
				if (NULL != so) {
				    free (so->s);
				    free (so);
				}

				/*
				 * Test Case: json5-errors/parse-ECMA_UnicodeEscapeSequence-invalid-low-surrogate.idio
				 *
				 * json5/parse-string " '\\uD800\\u006q' "
				 *
				 * NB. \\x as this is an Idio string!
				 */
				/*
				 * NB. ls_esc and ls_u could be any
				 * characters to print in hex form.
				 */
				json5_error_printf ("json5/ECMA_UnicodeEscapeSequence: not a low surrogate U+%04X U+%04X U+%04X U+%04X U+%04X U+%04X at %zd", ls_esc, ls_u, ls_h1, ls_h2, ls_h3, ls_h4, s_i - 6);
				/* notreached */
				return 0;
			    }
			} else {
			    size_t s_i = s->i;
			    size_t s_len = s->len;
			    free (s->s);
			    free (s);
			    json5_token_free_remaining (ft);
			    if (NULL != so) {
				free (so->s);
				free (so);
			    }

			    /*
			     * Test Case: json5-errors/parse-ECMA_UnicodeEscapeSequence-no-low-surrogate.idio
			     *
			     * json5/parse-string " '\\uD800' "
			     *
			     * NB. \\x as this is an Idio string and
			     * within 6 of the end of the string!
			     */
			    json5_error_printf ("json5/ECMA_UnicodeEscapeSequence: expecting a low surrogate at %zd / %zd", s_i, s_len);
			    /* notreached */
			    return 0;
			}
		    } else {
			/*
			 * Regular \uHHHH
			 */
			*cpp = hs;
		    }
		} else {
		    size_t s_i = s->i;
		    free (s->s);
		    free (s);
		    json5_token_free_remaining (ft);
		    if (NULL != so) {
			free (so->s);
			free (so);
		    }

		    /*
		     * Test Case: json5-errors/parse-ECMA_UnicodeEscapeSequence-not-hex-digits.idio
		     *
		     * json5/parse-string " '\\uqwer' "
		     *
		     * NB. \\x as this is an Idio string!
		     */
		    json5_error_printf ("json5/ECMA_UnicodeEscapeSequence: not hex digits \\%c U+%04X U+%04X U+%04X U+%04X at %zd", cp1, h1, h2, h3, h4, s_i - 6);

		    /* notreached */
		    return 0;
		}
	    } else {
		size_t s_i = s->i;
		size_t s_len = s->len;
		free (s->s);
		free (s);
		json5_token_free_remaining (ft);
		if (NULL != so) {
		    free (so->s);
		    free (so);
		}

		/*
		 * Test Case: json5-errors/parse-ECMA_UnicodeEscapeSequence-no-hex-digits.idio
		 *
		 * json5/parse-string " '\\u' "
		 *
		 * NB. \\x as this is an Idio string and within 6 of
		 * the end of the string!
		 */
		json5_error_printf ("json5/ECMA_UnicodeEscapeSequence: EOS after \\%c at %zd / %zd", cp1, s_i, s_len);

		/* notreached */
		return 0;
	    }
	    break;
	default:
	    r = 0;
	    break;
	}
    } else {
	r = 0;
    }

    return r;
}

int json5_ECMA_EscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp, json5_token_t *ft, json5_unicode_string_t *so)
{
    size_t start = s->i;

    if (json5_ECMA_CharacterEscapeSequence (s, cpp)) {
	return 1;
    }

    s->i = start;
    if (json5_unicode_string_available (s, 1)) {
	json5_unicode_t cp1 = json5_unicode_string_next (s);

	if ('0' == cp1) {
	    *cpp = 0;
	    return 1;
	}
    } else {
	return 0;
    }

    s->i = start;
    if (json5_ECMA_HexEscapeSequence (s, cpp, ft, so)) {
	return 1;
    }

    s->i = start;
    if (json5_ECMA_UnicodeEscapeSequence (s, cpp, ft, so)) {
	return 1;
    }

    return 0;
}

int json5_ECMA_IdentifierStart (json5_unicode_t cp, json5_unicode_string_t *s)
{
    int r = 0;

    const idio_USI_t *var = idio_USI_codepoint (cp);

    if (IDIO_USI_CATEGORY_Lu == var->category ||
	IDIO_USI_CATEGORY_Ll == var->category ||
	IDIO_USI_CATEGORY_Lt == var->category ||
	IDIO_USI_CATEGORY_Lm == var->category ||
	IDIO_USI_CATEGORY_Lo == var->category ||
	IDIO_USI_CATEGORY_Nl == var->category ||
	'$' == cp ||
	'_' == cp) {
	r = 1;
    } else if ('\\' == cp) {
	json5_unicode_t ecp;

	if (json5_ECMA_UnicodeEscapeSequence (s, &ecp, NULL, NULL)) {
	    r = 1;
	}
    }

    return r;
}

int json5_ECMA_IdentifierPart (json5_unicode_t cp, json5_unicode_string_t *s)
{
    int r = 0;

    if (json5_ECMA_IdentifierStart (cp, s)) {
	r = 1;
    } else {
	const idio_USI_t *var = idio_USI_codepoint (cp);

	if (IDIO_USI_CATEGORY_Mn == var->category ||
	    IDIO_USI_CATEGORY_Mc == var->category ||
	    IDIO_USI_CATEGORY_Nd == var->category ||
	    IDIO_USI_CATEGORY_Pc == var->category ||
	    var->flags & IDIO_USI_FLAG_ZWJ ||
	    0x200C == cp) {
	    r = 1;
	}
    }

    return r;
}

void json5_widen_unicode_string (json5_unicode_string_t *s, json5_unicode_string_width_t width)
{
    char *p = (char *) realloc (s->s, s->len * width);

    if (NULL == p) {
	perror ("json5_widen_unicode_string: realloc");
	exit (1);
    }
    s->s = p;

    /*
     * Walk backwards so we don't overwrite the LHS with wider
     * elements
     */
    for (ptrdiff_t i = s->len - 1; i >= 0; i--) {
	json5_unicode_t cp = 0;

	switch (s->width) {
	case JSON5_UNICODE_STRING_WIDTH_1BYTE:
	    cp = ((uint8_t *) s->s) [i];
	    break;
	case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	    cp = ((uint16_t *) s->s) [i];
	    break;
	case JSON5_UNICODE_STRING_WIDTH_4BYTE: /* shouldn't be required */
	    cp = ((uint32_t *) s->s) [i];
	    break;
	default:
	    /*
	     * Test Cases: coding error?
	     */
	    json5_error_printf ("JSON5-string-widen: unexpected s width: %#x", s->width);

	    /* notreached */
	    return;
	}

	switch (width) {
	case JSON5_UNICODE_STRING_WIDTH_1BYTE: /* shouldn't be required */
	    ((uint8_t *) s->s) [i] = cp;
	    break;
	case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	    ((uint16_t *) s->s) [i] = cp;
	    break;
	case JSON5_UNICODE_STRING_WIDTH_4BYTE:
	    ((uint32_t *) s->s) [i] = cp;
	    break;
	default:
	    /*
	     * Test Cases: coding error?
	     */
	    json5_error_printf ("JSON5-string-widen: unexpected width: %#x", width);

	    /* notreached */
	    return;
	}
    }

    s->width = width;
}

void json5_unicode_string_set (json5_unicode_string_t *s, size_t i, json5_unicode_t cp)
{
    if (i >= s->len) {
	/*
	 * Test Cases: coding error?
	 */
	json5_error_printf ("JSON5-string-set: invalid index for U+%04X %zd / %zd characters", cp, i, s->len);

	/* notreached */
	return;
    }

    if (cp >= 0x10000 &&
	s->width != JSON5_UNICODE_STRING_WIDTH_4BYTE) {
	json5_widen_unicode_string (s, JSON5_UNICODE_STRING_WIDTH_4BYTE);
    } else if (cp >= 0x100 &&
	s->width < JSON5_UNICODE_STRING_WIDTH_2BYTE) {
	json5_widen_unicode_string (s, JSON5_UNICODE_STRING_WIDTH_2BYTE);
    }

    switch (s->width) {
    case JSON5_UNICODE_STRING_WIDTH_1BYTE:
	((uint8_t *) s->s) [i] = cp;
	break;
    case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	((uint16_t *) s->s) [i] = cp;
	break;
    case JSON5_UNICODE_STRING_WIDTH_4BYTE:
	((uint32_t *) s->s) [i] = cp;
	break;
    default:
	/*
	 * Test Cases: coding error?
	 */
	json5_error_printf ("JSON5-string-set: unexpected width: %#x", s->width);

	/* notreached */
	return;
    }
}

int json5_unicode_string_n_equal (json5_unicode_string_t *s, const char *scmp, size_t n)
{
    if ((s->i + n) > s->len) {
	n = s->len - s->i - 1;
    }

    int r = 1;

    for (size_t si = 0; si < n && *scmp; si++, scmp++) {
	size_t i = s->i + si;
	switch (s->width) {
	case JSON5_UNICODE_STRING_WIDTH_1BYTE:
	    if (*scmp != ((int8_t *) s->s)[i]) {
		r = 0;
	    }
	    break;
	case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	    if (*scmp != ((int16_t *) s->s)[i]) {
		r = 0;
	    }
	    break;
	case JSON5_UNICODE_STRING_WIDTH_4BYTE:
	    if (*scmp != ((int32_t *) s->s)[i]) {
		r = 0;
	    }
	    break;
	default:
	    /*
	     * Test Cases: coding error?
	     */
	    json5_error_printf ("JSON5-string-n-equal: unexpected width: %#x", s->width);

	    /* notreached */
	    return 0;
	}

	if (0 == r) {
	    break;
	}
    }

    return r;
}

json5_unicode_t json5_unicode_string_peek (json5_unicode_string_t *s, size_t i)
{
    json5_unicode_t cp = 0;

    if (i >= s->len) {
	return JSON5_UNICODE_INVALID;
    }

    switch (s->width) {
    case JSON5_UNICODE_STRING_WIDTH_1BYTE:
	cp = ((uint8_t *) s->s) [i];
	break;
    case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	cp = ((uint16_t *) s->s) [i];
	break;
    case JSON5_UNICODE_STRING_WIDTH_4BYTE:
	cp = ((uint32_t *) s->s) [i];
	break;
    default:
	/*
	 * Test Cases: coding error?
	 */
	json5_error_printf ("JSON5-string-peek: unexpected width: %#x", s->width);

	/* notreached */
	return 0x110000;
    }

    return cp;
}

json5_unicode_t json5_unicode_string_next (json5_unicode_string_t *s)
{
    json5_unicode_t cp = json5_unicode_string_peek (s, s->i);
    s->i++;
    return cp;
}

int json5_unicode_string_available (json5_unicode_string_t *s, size_t const reqd)
{
    return ((s->len - s->i) >= reqd);
}

void json5_unicode_skip_ws (json5_unicode_string_t *s)
{
    /*
     * JSON5 WhiteSpace before and after any JSON5Token
     *
     * https://spec.json5.org/#white-space
     */
    for (; s->i < s->len;) {
	json5_unicode_t cp = json5_unicode_string_peek (s, s->i);
	if (JSON5_UNICODE_INVALID == cp) {
	    return;
	}

	int done = 0;

	switch (cp) {
	case 0x09:		/* Horizontal tab */
	case 0X0A:		/* Line feed */
	case 0x0B:		/* Vertical tab */
	case 0x0C:		/* Form feed */
	case 0x0D:		/* Carriage return */
	case 0x20:		/* Space (was in Zs)*/
	case 0xA0:		/* Non-breaking space (was in Zs) */
	case 0x2028:		/* Line separator */
	case 0x2029:		/* Paragraph separator */
	case 0xFEFF:		/* Byte order mark */
	    break;
	default:
	    if (idio_usi_codepoint_is_category (cp, IDIO_USI_CATEGORY_Zs) == 0) {
		done = 1;
	    }
	    break;
	}

	if (done) {
	    break;
	}

	s->i++;
    }
}

void json5_unicode_skip_slc (json5_unicode_string_t *s)
{
    /*
     * JSON5 https://spec.json5.org/#comments
     *
     * to a LineTerminator
     */
    for (s->i++; s->i < s->len;) {
	json5_unicode_t cp;

	if (json5_ECMA_LineTerminator (s, &cp)) {
	    break;
	}

	/*
	 * json5_ECMA_LineTerminator() has moved the cursor on one
	 */
    }
}

void json5_unicode_skip_bc (json5_unicode_string_t *s, json5_token_t *ft)
{
    /*
     * JSON5 https://spec.json5.org/#comments
     *
     * to an asterisk solidus
     */

    /*
     * We only get here if s->i is the * in / * so s->i is 1+
     *
     * (modulo the SPACE character to stop the C compiler whinging)
     */
    size_t bc_start = s->i - 1;
    int done = 0;
    int asterisk = 0;
    for (s->i++; s->i < s->len;) {
	json5_unicode_t cp = json5_unicode_string_peek (s, s->i);
	if (JSON5_UNICODE_INVALID == cp) {
	    break;
	}

	if ('*' == cp) {
	    asterisk = 1;
	} else if (asterisk &&
		   '/' == cp) {
	    done = 1;
	} else {
	    asterisk = 0;
	}

	s->i++;

	if (done) {
	    break;
	}
    }

    if (!done) {
	free (s->s);
	free (s);
	json5_token_free_remaining (ft);

	/*
	 * Test Case: json5-errors/parse-unterminated-block-comment.idio
	 *
	 * json5/parse-string "/ *"
	 *
	 * (modulo the SPACE character to stop the C compiler
	 * whinging)
	 */
	json5_error_printf ("json5/block-comment at %zd: unterminated", bc_start);

	/* notreached */
	return;
    }
}
