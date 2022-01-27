/*
 * Copyright (c) 2017-2022 Ian Fitchet <idf(at)idio-lang.org>
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

/*
 * unicode.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "hash.h"
#include "idio-string.h"
#include "keyword.h"
#include "module.h"
#include "pair.h"
#include "primitive.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

IDIO idio_unicode_module = idio_S_nil;

/**
 * static idio_unicode_hash - table of known unicode names to values
 *
 */
static IDIO idio_unicode_hash = idio_S_nil;

static const uint8_t idio_utf8d[] = {
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

static char const *hex_DIGITS = "0123456789ABCDEF";

idio_unicode_t inline idio_utf8_decode (idio_unicode_t* state, idio_unicode_t* codep, idio_unicode_t byte)
{
    idio_unicode_t type = idio_utf8d[byte];

    *codep = (*state != IDIO_UTF8_ACCEPT) ?
	(byte & 0x3fu) | (*codep << 6) :
	(0xff >> type) & (byte);

    *state = idio_utf8d[256 + *state + type];
    return *state;
}

int idio_unicode_valid_code_point (idio_unicode_t cp)
{
    if (/* too big */
	cp > 0x10FFFF ||
	/* too small */
	cp < 0) {
	return 0;
    }

    return 1;
}

/*
 * Called by write-char -- if you want to write out noncharacter
 * Unicode code points use a string with a \xhh escape.
 */
int idio_unicode_character_code_point (idio_unicode_t cp)
{
    if (idio_unicode_valid_code_point (cp) == 0 ||
	/* non-characters */
	(cp >= 0xFDD0 &&
	 cp <= 0xFDEF) ||
	/* high-surrogate & low-surrogate */
	(cp >= 0xD800 &&
	 cp <= 0xDFFF) ||
	/* 0xFFFE (byte-order) & 0xFFFF in any plane */
	((cp & 0xFFFF) == 0xFFFE) ||
	((cp & 0xFFFF) == 0xFFFF)) {
	return 0;
    }

    return 1;
}

int idio_isa_unicode (IDIO o)
{
    IDIO_ASSERT (o);

    if (((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_UNICODE_MARK) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("unicode?",  unicode_p, (IDIO o), "o", "\
test if `o` is a unicode value				\n\
							\n\
:param o: object to test				\n\
:return: ``#t`` if `o` is a unicode value, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_unicode (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("unicode->plane",  unicode2plane, (IDIO cp), "cp", "\
return the Unicode plane of `cp`		\n\
					\n\
:param cp: unicode to analyse		\n\
:return: Unicode plane `cp`		\n\
:rtype: fixnum				\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: unicode-errors/unicode2plane-bad-type.idio
     *
     * unicode->plane #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    return idio_fixnum (IDIO_UNICODE_VAL (cp) >> 16);
}

IDIO_DEFINE_PRIMITIVE1_DS ("unicode->plane-codepoint",  unicode2plane_codepoint, (IDIO cp), "cp", "\
return the lower 16 bits of `cp`		\n\
					\n\
:param cp: unicode to convert		\n\
:return: lower 16 bits of of `cp`	\n\
:rtype: fixnum				\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: unicode-errors/unicode2plane-codepoint-bad-type.idio
     *
     * unicode->plane-codepoint #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    return idio_fixnum (IDIO_UNICODE_VAL (cp) & 0xffff);
}

IDIO_DEFINE_PRIMITIVE1_DS ("unicode->integer",  unicode2integer, (IDIO cp), "cp", "\
convert `cp` to an integer		\n\
					\n\
:param cp: unicode to convert		\n\
:return: integer conversion of `cp`	\n\
:rtype: fixnum				\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: unicode-errors/unicode2integer-bad-type.idio
     *
     * unicode->integer #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    return idio_fixnum (IDIO_UNICODE_VAL (cp));
}

/*
 * construct a UTF-8 sequence from an Idio string
 *
 * escapes says:
 *
 *   * to turn, say, a single ASCII 07 (Alert, Bell, ...)  into the
 *     C-style two-character escape sequence \a
 *
 *   * to turn any non-isprint(3) code point for a pathname into \xHH
 *
 *   * to turn U+0000 - U+001F into \xHH (JSON)
 *
 * quoted says to add a " at the front and back
 *
 * caller must IDIO_GC_FREE() this string
 *
 */
char *idio_utf8_string (IDIO str, size_t *sizep, int escapes, int quoted, int use_prec)
{
    IDIO_ASSERT (str);
    IDIO_C_ASSERT (sizep);

    IDIO_TYPE_ASSERT (string, str);

    int prec = 0;
    if (use_prec &&
	idio_S_nil != idio_print_conversion_precision_sym) {
	IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
					      idio_Idio_module,
					      IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcp) {
	    if (idio_isa_fixnum (ipcp)) {
		prec = IDIO_FIXNUM_VAL (ipcp);
	    } else {
		/*
		 * Test Case: ??
		 *
		 * If we set idio-print-conversion-precision to
		 * something not a fixnum (nor #f) then it affects
		 * *everything* in the codebase that uses
		 * idio-print-conversion-precision before we get here.
		 */
		idio_error_param_type ("fixnum", ipcp, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }

    char *s;
    size_t len;
    IDIO_FLAGS_T flags;
    if (idio_isa_substring (str)) {
	s = IDIO_SUBSTRING_S (str);
	len = IDIO_SUBSTRING_LEN (str);
	flags = IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (str));
    } else {
	s = IDIO_STRING_S (str);
	len = IDIO_STRING_LEN (str);
	flags = IDIO_STRING_FLAGS (str);
    }

    if (prec > 0 &&
	prec < len) {
	len = prec;
    }

    uint8_t *s8 = (uint8_t *) s;
    uint16_t *s16 = (uint16_t *) s;
    uint32_t *s32 = (uint32_t *) s;

    /*
     * Figure out the number of bytes required including escape
     * sequences
     */
    int is_octet = (flags & IDIO_STRING_FLAG_OCTET);
    int is_pathname = (flags & (IDIO_STRING_FLAG_PATHNAME | IDIO_STRING_FLAG_FD_PATHNAME | IDIO_STRING_FLAG_FIFO_PATHNAME));

    size_t i;
    size_t n = 0;
    for (i = 0; i < len; i++) {
	uint32_t c = 0;
	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    c = s8[i];
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    c = s16[i];
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    c = s32[i];
	    break;
	case IDIO_STRING_FLAG_OCTET:
	case IDIO_STRING_FLAG_PATHNAME:
	case IDIO_STRING_FLAG_FD_PATHNAME:
	case IDIO_STRING_FLAG_FIFO_PATHNAME:
	    c = s8[i];
	    break;
	default:
	    {
		/*
		 * Test Case: coding error
		 */
		char em[30];
		idio_snprintf (em, 30, "%#x", flags);

		idio_error_param_value_msg_only ("idio_utf8_string", "unexpected string flag", em, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}

	if (c >= 0x10000) {
	    n += 4;
	} else if (c >= 0x0800) {
	    n += 3;
	} else if (c >= 0x0080) {
	    n += 2;
	} else {
	    n += 1;
	}

	if (escapes) {
	    switch (c) {
	    case '\a': n++; break;
	    case '\b': n++; break;
	    case '\e': n++; break;
	    case '\f': n++; break;
	    case '\n': n++; break;
	    case '\r': n++; break;
	    case '\t': n++; break;
	    case '\v': n++; break;
	    case '"': n++; break;
	    case '\\': n++; break;
	    default:
		if ((is_pathname ||
		     is_octet) &&
		    ! isprint (c)) {
		    /* c (1 char) -> \xhh (4 chars) */
		    n += 3;
		} else if (c < 0x20) {
		    /* c (1 char) -> \xhh (4 chars) */
		    n += 3;
		}
	    }
	}
    }

    size_t bytes = n + 1;
    if (IDIO_UTF8_STRING_QUOTED == quoted) {
	bytes += 2;		/* leading and trailing "s */
	if (is_pathname ||
	    is_octet) {
	    bytes += 2;		/* leading %P or %B */
	}
	if (flags & IDIO_STRING_FLAG_FIFO_PATHNAME) {
	    bytes += 1;		/* subsequent F to give %PF */
	}
    }

    char *r = idio_alloc (bytes);

    n = 0;
    if (IDIO_UTF8_STRING_QUOTED == quoted) {
	if (is_pathname) {
	    r[n++] = '%';
	    r[n++] = 'P';
	    if (flags & IDIO_STRING_FLAG_FIFO_PATHNAME) {
		r[n++] = 'F';
	    }
	} else if (is_octet) {
	    r[n++] = '%';
	    r[n++] = 'B';
	}
	r[n++] = '"';
    }
    for (i = 0; i < len; i++) {
	uint32_t c = 0;
	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    c = s8[i];
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    c = s16[i];
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    c = s32[i];
	    break;
	case IDIO_STRING_FLAG_OCTET:
	case IDIO_STRING_FLAG_PATHNAME:
	case IDIO_STRING_FLAG_FD_PATHNAME:
	case IDIO_STRING_FLAG_FIFO_PATHNAME:
	    c = s8[i];
	    break;
	default:
	    {
		/*
		 * Test Case: coding error -- and should have been picked
		 * up above!
		 */
		char em[30];
		idio_snprintf (em, 30, "%#x", flags);

		idio_error_param_value_msg_only ("idio_utf8_string", "unexpected string flag", em, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}

	char ec = 0;
	char hex = 0;
	if (escapes) {
	    switch (c) {
	    case '\a': ec = 'a'; break;
	    case '\b': ec = 'b'; break;
	    case '\e': ec = 'e'; break;
	    case '\f': ec = 'f'; break;
	    case '\n': ec = 'n'; break;
	    case '\r': ec = 'r'; break;
	    case '\t': ec = 't'; break;
	    case '\v': ec = 'v'; break;
	    case '"': ec = '"'; break;
	    case '\\': ec = '\\'; break;
	    default:
		if ((is_pathname ||
		     is_octet) &&
		    ! isprint (c)) {
		    hex = 1;
		} else if (c < 0x20) {
		    hex = 1;
		}

	    }
	}

	if (ec) {
	    r[n++] = '\\';
	    r[n++] = ec;
	} else if (hex) {
	    r[n++] = '\\';
	    r[n++] = 'x';
	    r[n++] = hex_DIGITS[(c & 0xf0) >> 4];
	    r[n++] = hex_DIGITS[(c & 0x0f)];
	} else {
	    if (is_pathname ||
		is_octet) {
		r[n++] = c;
	    } else {
		if (idio_unicode_valid_code_point (c)) {
		    if (c >= 0x10000) {
			r[n++] = 0xf0 | ((c & (0x07 << 18)) >> 18);
			r[n++] = 0x80 | ((c & (0x3f << 12)) >> 12);
			r[n++] = 0x80 | ((c & (0x3f << 6)) >> 6);
			r[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
		    } else if (c >= 0x0800) {
			r[n++] = 0xe0 | ((c & (0x0f << 12)) >> 12);
			r[n++] = 0x80 | ((c & (0x3f << 6)) >> 6);
			r[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
		    } else if (c >= 0x0080) {
			r[n++] = 0xc0 | ((c & (0x1f << 6)) >> 6);
			r[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
		    } else {
			r[n++] = c & 0x7f;
		    }
		} else {
		    /*
		     * Test Case: ??
		     *
		     * Coding error.
		     */
		    /*
		     * Hopefully, this is guarded against elsewhere
		     */
		    char em[50];
		    idio_snprintf (em, 50, "U+%04" PRIX32 " is invalid", c);

		    idio_error_param_value_msg_only ("idio_utf8_string", "Unicode code point", em, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		}
	    }
	}
    }

    if (IDIO_UTF8_STRING_QUOTED == quoted) {
	r[n++] = '"';
    }
    r[n] = '\0';
    *sizep = n;

    return r;
}

/*
 * construct a UTF-8 sequence from a Unicode code point
 *
 * caller must supply a char* (of at least 4 bytes) and an int* which
 * will be set to the number of bytes written to the char*
 *
 */
void idio_utf8_code_point (idio_unicode_t c, char *buf, int *sizep)
{
    int n = 0;

    if (idio_unicode_valid_code_point (c)) {
	if (c >= 0x10000) {
	    buf[n++] = 0xf0 | ((c & (0x07 << 18)) >> 18);
	    buf[n++] = 0x80 | ((c & (0x3f << 12)) >> 12);
	    buf[n++] = 0x80 | ((c & (0x3f << 6)) >> 6);
	    buf[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
	} else if (c >= 0x0800) {
	    buf[n++] = 0xe0 | ((c & (0x0f << 12)) >> 12);
	    buf[n++] = 0x80 | ((c & (0x3f << 6)) >> 6);
	    buf[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
	} else if (c >= 0x0080) {
	    buf[n++] = 0xc0 | ((c & (0x1f << 6)) >> 6);
	    buf[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
	} else {
	    buf[n++] = c & 0x7f;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	/*
	 * Hopefully, this is guarded against elsewhere
	 */
	char em[50];
	idio_snprintf (em, 50, "U+%04" PRIX32 " is invalid", c);

	idio_error_param_value_msg_only ("idio_utf8_code_point", "Unicode code point", em, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    *sizep = n;
}

int idio_unicode_C_eqp (const void *s1, const void *s2)
{
    /*
     * We should only be here for idio_unicode_hash key comparisons
     * but hash keys default to idio_S_nil
     */
    if (idio_S_nil == s1 ||
	idio_S_nil == s2) {
	return 0;
    }

    return (0 == strcmp ((char const *) s1, (char const *) s2));
}

idio_hi_t idio_unicode_C_hash (IDIO h, const void *s)
{
    size_t hvalue = (uintptr_t) s;

    if (idio_S_nil != s) {
	hvalue = idio_hash_default_hash_C_string_C (strlen ((char *) s), s);
    }

    return (hvalue & IDIO_HASH_MASK (h));
}

IDIO idio_unicode_C_intern (char const *s, size_t const blen, IDIO v)
{
    IDIO_C_ASSERT (s);

    IDIO c = idio_hash_ref (idio_unicode_hash, s);

    if (idio_S_unspec == c) {
	char *copy = idio_alloc (blen + 1);
	memcpy (copy, s, blen);
	copy[blen] = '\0';

	idio_hash_put (idio_unicode_hash, copy, v);
    }

    return v;
}

/**
 * idio_unicode_lookup - return the code point of a named unicode code
 * point
 *
 * @s: unicode name (a C string)
 *
 * Return:
 * The Idio value of the code point or %idio_S_unspec.
 */
IDIO idio_unicode_lookup (char const *s)
{
    IDIO_C_ASSERT (s);

    return idio_hash_ref (idio_unicode_hash, s);
}

/*
 * All the unicode-*? are essentially identical
 */
/*
 * Test Cases:
 *
 *   unicode-errors/unicode-eq-bad-first-type.idio
 *   unicode-errors/unicode-eq-bad-second-type.idio
 *   unicode-errors/unicode-eq-bad-args-type.idio
 *   unicode-errors/unicode-eq-bad-arg-type.idio
 *
 * unicode=? #t #t
 * unicode=? #U+1 #t
 * unicode=? #U+1 #U+1 #t
 * unicode=? #U+1 #U+1 '(#t)
 */
#define IDIO_DEFINE_UNICODE_PRIMITIVE2V(name,cname,cmp,accessor)	\
    IDIO_DEFINE_PRIMITIVE2V (name, cname, (IDIO c1, IDIO c2, IDIO args)) \
    {									\
	IDIO_ASSERT (c1);						\
	IDIO_ASSERT (c2);						\
	IDIO_ASSERT (args);						\
									\
	IDIO_USER_TYPE_ASSERT (unicode, c1);				\
	IDIO_USER_TYPE_ASSERT (unicode, c2);				\
	IDIO_USER_TYPE_ASSERT (list, args);				\
									\
	args = idio_pair (c2, args);					\
									\
	IDIO r = idio_S_true;						\
									\
	while (idio_S_nil != args) {					\
	    c2 = IDIO_PAIR_H (args);					\
	    IDIO_USER_TYPE_ASSERT (unicode, c2);			\
	    if (! (accessor (c1) cmp accessor (c2))) {			\
		r = idio_S_false;					\
		break;							\
	    }								\
	    c1 = c2;							\
	    args = IDIO_PAIR_T (args);					\
	}								\
									\
	return r;							\
    }

#define IDIO_DEFINE_UNICODE_CS_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_UNICODE_PRIMITIVE2V (name, unicode_ ## cname ## _p, cmp, IDIO_UNICODE_VAL)

IDIO_DEFINE_UNICODE_CS_PRIMITIVE2V ("unicode=?", eq, ==)

#define IDIO_UNICODE_INTERN_C(name,c)	(idio_unicode_C_intern (name, sizeof (name) - 1, IDIO_UNICODE (c)))

void idio_unicode_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (unicode_p);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, unicode2plane);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, unicode2plane_codepoint);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, unicode2integer);

    /*
     * The unicode_* functions were autogenerated but we still need to
     * add the sigstr and docstr
     */
    IDIO fvi = IDIO_ADD_PRIMITIVE (unicode_eq_p);
    IDIO p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, IDIO_STATIC_STR_LEN ("cp1 cp2 [...]"));
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, IDIO_STATIC_STR_LEN ("\
test if unicode arguments are equal			\n\
							\n\
:param cp1: unicode					\n\
:param cp2: unicode					\n\
:param ...: unicode					\n\
:return: ``#t`` if arguments are equal, ``#f`` otherwise	\n\
"));
}

void idio_init_unicode ()
{
    idio_module_table_register (idio_unicode_add_primitives, NULL, NULL);

    idio_unicode_module = idio_module (IDIO_SYMBOLS_C_INTERN ("unicode"));

    idio_unicode_hash = idio_hash (1<<7, idio_unicode_C_eqp, idio_unicode_C_hash, idio_S_nil, idio_S_nil);
    idio_gc_protect_auto (idio_unicode_hash);
    IDIO_HASH_FLAGS (idio_unicode_hash) |= IDIO_HASH_FLAG_STRING_KEYS;

    /* ASCII C0 control characters */
    IDIO_UNICODE_INTERN_C ("nul", 0);
    IDIO_UNICODE_INTERN_C ("soh", 1);
    IDIO_UNICODE_INTERN_C ("stx", 2);
    IDIO_UNICODE_INTERN_C ("etx", 3);
    IDIO_UNICODE_INTERN_C ("eot", 4);
    IDIO_UNICODE_INTERN_C ("enq", 5);
    IDIO_UNICODE_INTERN_C ("ack", 6);
    IDIO_UNICODE_INTERN_C ("bel", 7);
    IDIO_UNICODE_INTERN_C ("bs", 8);
    IDIO_UNICODE_INTERN_C ("ht", 9);
    IDIO_UNICODE_INTERN_C ("lf", 10);
    IDIO_UNICODE_INTERN_C ("vt", 11);
    IDIO_UNICODE_INTERN_C ("ff", 12);
    IDIO_UNICODE_INTERN_C ("cr", 13);
    IDIO_UNICODE_INTERN_C ("so", 14);
    IDIO_UNICODE_INTERN_C ("si", 15);
    IDIO_UNICODE_INTERN_C ("dle", 16);
    IDIO_UNICODE_INTERN_C ("dcl", 17);
    IDIO_UNICODE_INTERN_C ("dc2", 18);
    IDIO_UNICODE_INTERN_C ("dc3", 19);
    IDIO_UNICODE_INTERN_C ("dc4", 20);
    IDIO_UNICODE_INTERN_C ("nak", 21);
    IDIO_UNICODE_INTERN_C ("syn", 22);
    IDIO_UNICODE_INTERN_C ("etb", 23);
    IDIO_UNICODE_INTERN_C ("can", 24);
    IDIO_UNICODE_INTERN_C ("em", 25);
    IDIO_UNICODE_INTERN_C ("sub", 26);
    IDIO_UNICODE_INTERN_C ("esc", 27);
    IDIO_UNICODE_INTERN_C ("fs", 28);
    IDIO_UNICODE_INTERN_C ("gs", 29);
    IDIO_UNICODE_INTERN_C ("rs", 30);
    IDIO_UNICODE_INTERN_C ("us", 31);
    IDIO_UNICODE_INTERN_C ("sp", 32);

    /* C and other common names */
    /* nul as above */
    IDIO_UNICODE_INTERN_C ("alarm",		'\a'); /* 0x07 */
    IDIO_UNICODE_INTERN_C ("backspace",		'\b'); /* 0x08 */
    IDIO_UNICODE_INTERN_C ("tab",		'\t'); /* 0x09 */
    IDIO_UNICODE_INTERN_C ("linefeed",		'\n'); /* 0x0a */
    IDIO_UNICODE_INTERN_C ("newline",		'\n'); /* 0x0a */
    IDIO_UNICODE_INTERN_C ("vtab",		'\v'); /* 0x0b */
    IDIO_UNICODE_INTERN_C ("page",		'\f'); /* 0x0c */
    IDIO_UNICODE_INTERN_C ("return",		'\r'); /* 0x0d */
    IDIO_UNICODE_INTERN_C ("carriage-return",	'\r'); /* 0x0d */
    IDIO_UNICODE_INTERN_C ("esc",		0x1b); /* 0x1b */
    IDIO_UNICODE_INTERN_C ("escape",		0x1b); /* 0x1b */
    IDIO_UNICODE_INTERN_C ("space",		' ');  /* 0x20 */
    IDIO_UNICODE_INTERN_C ("del",		0x7f); /* 0x7f */
    IDIO_UNICODE_INTERN_C ("delete",		0x7f); /* 0x7f */

    /*
     * As #\{ is the start of a named character we need to be able to
     * "characterise" a left brace itself.  Lets have a couple of
     * ways:
     *
     * #\{lbrace}
     * #\{{}
     */
    IDIO_UNICODE_INTERN_C ("lbrace",		'{');
    IDIO_UNICODE_INTERN_C ("{",			'{');

    /* Unicode code points... */

    idio_constant_unicode_vtable = idio_vtable (IDIO_TYPE_CONSTANT_UNICODE);

    idio_vtable_add_method (idio_constant_unicode_vtable,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_constant_unicode));

    idio_vtable_add_method (idio_constant_unicode_vtable,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_util_method_2string));
}

