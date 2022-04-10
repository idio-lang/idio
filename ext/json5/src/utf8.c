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
 * utf8.c
 *
 */

/*
 * Parts of this code are a straight copy of Bjoern Hoehrmann’s
 * DFA-based decoder, http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
 * for which the licence reads:

License

Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include <sys/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "json5-token.h"
#include "json5-unicode.h"
#include "utf8.h"

static const uint8_t json5_utf8d[] = {
    // The first part of the table maps bytes to character classes that
    // to reduce the size of the transition table and create bitmasks.
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

    // The second part is a transition table that maps a combination
    // of a state of the automaton and a character class to a state.
     0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
};

inline static json5_unicode_t json5_utf8_decode (json5_unicode_t* state, json5_unicode_t* codep, json5_unicode_t byte)
{
    json5_unicode_t type = json5_utf8d[byte];

    *codep = (*state != JSON5_UTF8_ACCEPT) ?
	(byte & 0x3fu) | (*codep << 6) :
	(0xff >> type) & (byte);

    *state = json5_utf8d[256 + *state + type];
    return *state;
}

json5_unicode_string_t *json5_utf8_string_C_len (char *s_C, const size_t slen)
{
    json5_unicode_string_width_t width = JSON5_UNICODE_STRING_WIDTH_1BYTE;
    json5_unicode_t codepoint;
    json5_unicode_t state = JSON5_UTF8_ACCEPT;

    size_t cp_count = 0;
    uint8_t *us_C = (unsigned char *) s_C;
    size_t i;
    for (i = 0; i < slen; us_C++, i++) {
	state = JSON5_UTF8_ACCEPT;
	for ( ; i < slen; us_C++, i++) {
	    json5_utf8_decode (&state, &codepoint, *us_C);

	    if (JSON5_UTF8_ACCEPT == state) {
		break;
	    } else if (JSON5_UTF8_REJECT == state) {
		codepoint = 0xFFFD;
		break;
	    }
	    /*
	     * more bytes required...
	     */
	}

	if (codepoint > 0xffff) {
	    width = JSON5_UNICODE_STRING_WIDTH_4BYTE;
	} else if (codepoint > 0xff &&
		   JSON5_UNICODE_STRING_WIDTH_1BYTE == width) {
	    width = JSON5_UNICODE_STRING_WIDTH_2BYTE;
	}
	cp_count++;
    }

    /*
     * How many bytes do we need?
     */
    size_t reqd_bytes = 0;
    switch (width) {
    case JSON5_UNICODE_STRING_WIDTH_1BYTE:
	reqd_bytes = cp_count;
	break;
    case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	reqd_bytes = cp_count * 2;
	break;
    case JSON5_UNICODE_STRING_WIDTH_4BYTE:
	reqd_bytes = cp_count * 4;
	break;
    default:
	/*
	 * Test Case: coding error?
	 */
	json5_error_printf ("UTF-8 decode: unexpected width: %#x", width);

	/* notreached */
	return NULL;
    }

    json5_unicode_string_t *so = (json5_unicode_string_t *) malloc (sizeof (json5_unicode_string_t));
    so->s = (char *) malloc (reqd_bytes + 1);
    so->width = width;

    uint8_t *us8 = (uint8_t *) so->s;
    uint16_t *us16 = (uint16_t *) so->s;
    uint32_t *us32 = (uint32_t *) so->s;

    cp_count = 0;
    us_C = (unsigned char *) s_C;
    for (i = 0; i < slen; cp_count++, us_C++, i++) {
	state = JSON5_UTF8_ACCEPT;
	for ( ; i < slen; us_C++, i++) {
	    json5_utf8_decode (&state, &codepoint, *us_C);

	    if (JSON5_UTF8_ACCEPT == state) {
		break;
	    } else if (JSON5_UTF8_REJECT == state) {
		codepoint = 0xFFFD;
		break;
	    }
	    /*
	     * more bytes required...
	     */
	}

	switch (width) {
	case JSON5_UNICODE_STRING_WIDTH_1BYTE:
	    us8[cp_count] = (uint8_t) codepoint;
	    break;
	case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	    us16[cp_count] = (uint16_t) codepoint;
	    break;
	case JSON5_UNICODE_STRING_WIDTH_4BYTE:
	    us32[cp_count] = (uint32_t) codepoint;
	    break;
	default:
	    /*
	     * Test Case: coding error?
	     */
	    json5_error_printf ("UTF-8 decode: unexpected width: %#x", width);

	    /* notreached */
	    return NULL;
	}
    }

    so->len = cp_count;
    so->i = 0;

    return so;
}
