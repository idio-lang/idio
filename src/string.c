/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * string.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vars.h"
#include "vm.h"

static void idio_string_error (IDIO msg, IDIO detail, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (detail);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (string, detail);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO lsh;
    IDIO dsh;
    idio_error_init (NULL, &lsh, &dsh, c_location);

    if (idio_S_nil != detail) {
	idio_display (detail, dsh);
    }

    idio_error_raise_cont (idio_condition_string_error_type,
			   IDIO_LIST3 (msg,
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

void idio_string_error_C (char const *msg, IDIO detail, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (detail);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    idio_string_error (idio_get_output_string (msh), detail, c_location);

    /* notreached */
}

/*
 * Code coverage:
 *
 * Coding error?
 */
static void idio_string_utf8_decode_error (char const *msg, char const *det, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("string UTF-8 decode: ", msh);
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (det, dsh);

    idio_string_error (idio_get_output_string (msh), idio_get_output_string (dsh), c_location);

    /* notreached */
}

void idio_string_size_error (char const *msg, ptrdiff_t size, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("string size: ", msh);
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "%td", size);
    idio_display_C_len (em, eml, dsh);

    idio_string_error (idio_get_output_string (msh), idio_get_output_string (dsh), c_location);

    /* notreached */
}

void idio_string_length_error (char const *msg, IDIO str, ptrdiff_t index, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (str);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("string length: ", msh);
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    if (idio_S_nil != str) {
	idio_display_C ("\"", dsh);
	idio_display (str, dsh);
	idio_display_C ("\" ", dsh);
    }

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "index %td", index);
    idio_display_C_len (em, eml, dsh);

    idio_string_error (idio_get_output_string (msh), idio_get_output_string (dsh), c_location);

    /* notreached */
}

/*
 * Code coverage:
 *
 * Not triggered as I can't provoke the clauses!
 */
void idio_substring_length_error (char const *msg, IDIO str, ptrdiff_t index, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (str);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("substring length: ", msh);
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    if (idio_S_nil != str) {
	idio_display_C ("\"", dsh);
	idio_display (str, dsh);
	idio_display_C ("\" ", dsh);
    }

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "index %td", index);
    idio_display_C_len (em, eml, dsh);

    idio_string_error (idio_get_output_string (msh), idio_get_output_string (dsh), c_location);

    /* notreached */
}

void idio_substring_index_error (char const *msg, IDIO str, ptrdiff_t ip0, ptrdiff_t ipn, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (str);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, str);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("substring index: ", msh);
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, " offset %td end %td of %zd code points", ip0, ipn, idio_string_len (str));
    idio_display_C_len (em, eml, dsh);

    idio_string_error (idio_get_output_string (msh), idio_get_output_string (dsh), c_location);

    /* notreached */
}

void idio_string_format_error (char const *msg, IDIO str, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (str);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, str);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("string format: ", msh);
    idio_display_C (msg, msh);

    idio_string_error (idio_get_output_string (msh), str, c_location);

    /* notreached */
}

void idio_string_width_error (char const *msg, IDIO str, IDIO rc, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (str);
    IDIO_ASSERT (rc);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("string width: ", msh);
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    if (idio_S_nil != str) {
	idio_display_C ("\"", dsh);
	idio_display (str, dsh);
	idio_display_C ("\" ", dsh);
    }
    idio_display (rc, dsh);

    idio_string_error (idio_get_output_string (msh), idio_get_output_string (dsh), c_location);

    /* notreached */
}

size_t idio_string_storage_size (IDIO s)
{
    IDIO_ASSERT (s);

    IDIO_TYPE_ASSERT (string, s);

    size_t width = 0;
    IDIO_FLAGS_T flags;

    if (idio_isa (s, IDIO_TYPE_SUBSTRING)) {
	flags = IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (s));
    } else {
	flags = IDIO_STRING_FLAGS (s);
    }

    switch (flags) {
    case IDIO_STRING_FLAG_1BYTE:
	width = 1;
	break;
    case IDIO_STRING_FLAG_2BYTE:
	width = 2;
	break;
    case IDIO_STRING_FLAG_4BYTE:
	width = 4;
	break;
    case IDIO_STRING_FLAG_OCTET:
    case IDIO_STRING_FLAG_PATHNAME:
    case IDIO_STRING_FLAG_FD_PATHNAME:
    case IDIO_STRING_FLAG_FIFO_PATHNAME:
	width = 1;
	break;
    default:
	{
	    /*
	     * Test Case: coding error
	     */
	    char em[30];
	    idio_snprintf (em, 30, "%#x", flags);

	    idio_error_param_value_msg_only ("idio_string_storage_size", "unexpected string flag", em, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return 0;
	}
    }

    return width;
}

IDIO idio_string_C_len (char const *s_C, size_t const blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);

    IDIO_FLAGS_T flags = IDIO_STRING_FLAG_1BYTE;

    idio_unicode_t codepoint;
    idio_unicode_t state = IDIO_UTF8_ACCEPT;

    size_t cp_count = 0;
    uint8_t *us_C = (unsigned char *) s_C;
    size_t i;
    for (i = 0; i < blen; us_C++, i++) {
	state = IDIO_UTF8_ACCEPT;
	for ( ; i < blen; us_C++, i++) {
	    idio_utf8_decode (&state, &codepoint, *us_C);

	    if (IDIO_UTF8_ACCEPT == state) {
		break;
	    } else if (IDIO_UTF8_REJECT == state) {
		/* fprintf (stderr, "The (UTF-8) string is not well-formed with %#02X at byte %zu/%zu\n", *us_C, i, blen); */
		codepoint = 0xFFFD;
		break;
	    }
	    /*
	     * more bytes required...
	     */
	}

	if (codepoint > 0xffff) {
	    flags = IDIO_STRING_FLAG_4BYTE;
	} else if (codepoint > 0xff &&
		   IDIO_STRING_FLAG_1BYTE == flags) {
	    flags = IDIO_STRING_FLAG_2BYTE;
	}
	cp_count++;
    }

    /*
     * How many bytes do we need?
     */
    size_t reqd_bytes = 0;
    switch (flags) {
    case IDIO_STRING_FLAG_1BYTE:
	reqd_bytes = cp_count;
	break;
    case IDIO_STRING_FLAG_2BYTE:
	reqd_bytes = cp_count * 2;
	break;
    case IDIO_STRING_FLAG_4BYTE:
	reqd_bytes = cp_count * 4;
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    char em[30];
	    idio_snprintf (em, 30, "%#x", flags);

	    idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO_GC_ALLOC (IDIO_STRING_S (so), reqd_bytes + 1);
    IDIO_STRING_BLEN (so) = reqd_bytes;

    uint8_t *us8 = (uint8_t *) IDIO_STRING_S (so);
    uint16_t *us16 = (uint16_t *) IDIO_STRING_S (so);
    uint32_t *us32 = (uint32_t *) IDIO_STRING_S (so);

    cp_count = 0;
    us_C = (unsigned char *) s_C;
    for (i = 0; i < blen; cp_count++, us_C++, i++) {
	state = IDIO_UTF8_ACCEPT;
	for ( ; i < blen; us_C++, i++) {
	    idio_utf8_decode (&state, &codepoint, *us_C);

	    if (IDIO_UTF8_ACCEPT == state) {
		break;
	    } else if (IDIO_UTF8_REJECT == state) {
		codepoint = 0xFFFD;
		break;
	    }
	    /*
	     * more bytes required...
	     */
	}

	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    us8[cp_count] = (uint8_t) codepoint;
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    us16[cp_count] = (uint16_t) codepoint;
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    us32[cp_count] = (uint32_t) codepoint;
	    break;
	default:
	    {
		/*
		 * Test Case: ??
		 *
		 * Coding error.  Should have been caught above.
		 */
		char em[30];
		idio_snprintf (em, 30, "%#x", flags);

		idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}
    }

    IDIO_STRING_S (so)[reqd_bytes] = '\0';

    IDIO_STRING_LEN (so) = cp_count;
    IDIO_STRING_FLAGS (so) = flags;

    return so;
}

IDIO idio_string_C (char const *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_string_C_len (s_C, strlen (s_C));
}

IDIO idio_octet_string_C_len (char const *s_C, size_t const blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);

    IDIO_GC_ALLOC (IDIO_STRING_S (so), blen + 1);
    IDIO_STRING_BLEN (so) = blen;

    uint8_t *us8 = (uint8_t *) IDIO_STRING_S (so);

    uint8_t *us_C = (unsigned char *) s_C;
    size_t i;
    for (i = 0; i < blen; i++) {
	us8[i] = (uint8_t) us_C[i];
    }

    IDIO_STRING_S (so)[i] = '\0';

    IDIO_STRING_LEN (so) = blen;
    IDIO_STRING_FLAGS (so) = IDIO_STRING_FLAG_OCTET;

    return so;
}

/*
 * Creating an octet string, something that can contain ASCII NULs,
 * from a C string, which cannot, seems slightly pointless but I guess
 * it keeps the interface similar and allows someone to scribble over
 * the string later.
 */
IDIO idio_octet_string_C (char const *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_octet_string_C_len (s_C, strlen (s_C));
}

/*
 * Avoiding decoding the UTF-8 string twice requires creating a
 * temporary array of codepoints of unknown size (until we decode the
 * UTF-8 string).  We could realloc() an array as we go along or, as
 * here, create a automatic variable the size of the number of bytes
 * (the worst case for ASCII strings).  We can then just copy the
 * array of code points into the appropriate sized array
 * us8/us16/us32.
 *
 * It turns out to make *very* little difference, the UTF-8 decode
 * algorithm is very quick.
 */

IDIO idio_string_C_array_lens (size_t const ns, char const *a_C[], size_t const lens[])
{
    IDIO_C_ASSERT (a_C);
    IDIO_C_ASSERT (lens);

    IDIO so;

    so = idio_gc_get (IDIO_TYPE_STRING);

    size_t ai;

    IDIO_FLAGS_T flags = IDIO_STRING_FLAG_1BYTE;

    idio_unicode_t codepoint;
    idio_unicode_t state;
    uint8_t *ua_C;

    size_t cp_count = 0;
    for (ai = 0; ai < ns; ai++) {
	size_t blen = lens[ai];

	ua_C = (unsigned char *) a_C[ai];
	for (size_t i = 0; i < blen; ua_C++, i++) {
	    state = IDIO_UTF8_ACCEPT;
	    for ( ; i < blen; ua_C++, i++) {
		idio_utf8_decode (&state, &codepoint, *ua_C);

		if (IDIO_UTF8_ACCEPT == state) {
		    break;
		} else if (IDIO_UTF8_REJECT == state) {
		    /*
		     * Test Case: ??
		     *
		     * I'm not sure we can get here as the individual
		     * strings passed to join-string will have been
		     * handled by idio_string_C_len (), above.
		     *
		     * Callers to here from idio_string_array () will
		     * be feeding in C strings.
		     */
#ifdef IDIO_DEBUG
		    fprintf (stderr, "The (UTF-8) string is not well-formed with %#02X at byte %zu/%zu\n", *ua_C, i, blen);
#endif
		    codepoint = 0xFFFD;
		    break;
		}
		/*
		 * more bytes required...
		 */
	    }

	    if (codepoint > 0xffff) {
		flags = IDIO_STRING_FLAG_4BYTE;
	    } else if (codepoint > 0xff &&
		       IDIO_STRING_FLAG_1BYTE == flags) {
		flags = IDIO_STRING_FLAG_2BYTE;
	    }
	    cp_count++;
	}
    }

    /*
     * How many bytes do we need?
     */
    size_t reqd_bytes = 0;
    switch (flags) {
    case IDIO_STRING_FLAG_1BYTE:
	reqd_bytes = cp_count;
	break;
    case IDIO_STRING_FLAG_2BYTE:
	reqd_bytes = cp_count * 2;
	break;
    case IDIO_STRING_FLAG_4BYTE:
	reqd_bytes = cp_count * 4;
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    char em[30];
	    idio_snprintf (em, 30, "%#x", flags);

	    idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO_GC_ALLOC (IDIO_STRING_S (so), reqd_bytes + 1);
    IDIO_STRING_BLEN (so) = reqd_bytes;

    uint8_t *us8 = (uint8_t *) IDIO_STRING_S (so);
    uint16_t *us16 = (uint16_t *) IDIO_STRING_S (so);
    uint32_t *us32 = (uint32_t *) IDIO_STRING_S (so);

    cp_count = 0;
    for (ai = 0; ai < ns; ai++) {
	size_t blen = lens[ai];
	state = IDIO_UTF8_ACCEPT;

	ua_C = (unsigned char *) a_C[ai];
	for (size_t i = 0; i < blen; cp_count++, ua_C++, i++) {
	    for (; i < blen; ua_C++, i++) {
		idio_utf8_decode (&state, &codepoint, *ua_C);

		if (IDIO_UTF8_ACCEPT == state) {
		    break;
		} else if (IDIO_UTF8_REJECT == state) {
		    /*
		     * See comment above.
		     */
		    codepoint = 0xFFFD;
		    break;
		}
		/*
		 * more bytes required...
		 */
	    }

	    switch (flags) {
	    case IDIO_STRING_FLAG_1BYTE:
		us8[cp_count] = (uint8_t) codepoint;
		break;
	    case IDIO_STRING_FLAG_2BYTE:
		us16[cp_count] = (uint16_t) codepoint;
		break;
	    case IDIO_STRING_FLAG_4BYTE:
		us32[cp_count] = (uint32_t) codepoint;
		break;
	    default:
		{
		    /*
		     * Test Case: ??
		     *
		     * Coding error.  Should have been caught above.
		     */
		    char em[30];
		    idio_snprintf (em, 30, "%#x", flags);

		    idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }
	}
    }

    IDIO_STRING_S (so)[reqd_bytes] = '\0';

    IDIO_STRING_FLAGS (so) = flags;
    IDIO_STRING_LEN (so) = cp_count;

    return so;
}

IDIO idio_string_C_array (size_t const ns, char const *a_C[])
{
    IDIO_C_ASSERT (a_C);

    size_t lens[ns];
    size_t ai;

    for (ai = 0; ai < ns; ai++) {
	lens[ai] = strlen (a_C[ai]);
    }

    return idio_string_C_array_lens (ns, a_C, lens);
}

IDIO idio_copy_string (IDIO string)
{
    IDIO_ASSERT (string);

    IDIO_TYPE_ASSERT (string, string);

    IDIO copy = idio_S_unspec;

    switch (idio_type (string)) {
    case IDIO_TYPE_STRING:
	{
	    copy = idio_gc_get (IDIO_TYPE_STRING);

	    size_t blen = IDIO_STRING_BLEN (string);
	    IDIO_GC_ALLOC (IDIO_STRING_S (copy), blen + 1);
	    IDIO_STRING_BLEN (copy) = blen;

	    memcpy (IDIO_STRING_S (copy), IDIO_STRING_S (string), blen);
	    IDIO_STRING_S (copy)[blen] = '\0';

	    IDIO_STRING_LEN (copy) = IDIO_STRING_LEN (string);
	    IDIO_STRING_FLAGS (copy) = IDIO_STRING_FLAGS (string);
	    break;
	}
    case IDIO_TYPE_SUBSTRING:
	{
	    size_t sw = idio_string_storage_size (string);

	    size_t offset = (IDIO_SUBSTRING_S (string) - IDIO_STRING_S (IDIO_SUBSTRING_PARENT (string))) / sw;
	    copy = idio_substring_offset_len (IDIO_SUBSTRING_PARENT (string),
					      offset,
					      IDIO_SUBSTRING_LEN (string));
	    break;
	}
    }

    return copy;
}

int idio_isa_string (IDIO so)
{
    IDIO_ASSERT (so);

    return (idio_isa (so, IDIO_TYPE_STRING) ||
	    idio_isa (so, IDIO_TYPE_SUBSTRING));
}

IDIO_DEFINE_PRIMITIVE1_DS ("string?", string_p, (IDIO o), "o", "\
test if `o` is an string			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an string, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_string (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_octet_string (IDIO o)
{
    IDIO_ASSERT (o);

    return ((idio_isa (o, IDIO_TYPE_STRING) &&
	     (IDIO_STRING_FLAGS (o) & IDIO_STRING_FLAG_OCTET)) ||
	    (idio_isa (o, IDIO_TYPE_SUBSTRING) &&
	     (IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (o)) & IDIO_STRING_FLAG_OCTET)));
}

IDIO_DEFINE_PRIMITIVE1_DS ("octet-string?", octet_string_p, (IDIO o), "o", "\
test if `o` is an octet string			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an octet string, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_octet_string (o)) {
	r = idio_S_true;
    }

    return r;
}

void idio_free_string (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    IDIO_GC_FREE (IDIO_STRING_S (so), IDIO_STRING_BLEN (so) + 1);
}

/*
 * Watch out for substrings of substrings.
 *
 * If SUB1 is a substring of X and we want SUB2 to be a substring of
 * SUB1 then SUB2 is just another substring of X.
 */
IDIO idio_substring_offset_len (IDIO str, size_t const offset, size_t const len)
{
    IDIO_ASSERT (str);
    IDIO_C_ASSERT (offset >= 0);
    IDIO_C_ASSERT (len >= 0);

    size_t width = idio_string_storage_size (str);

    if (idio_isa (str, IDIO_TYPE_SUBSTRING)) {
	IDIO parent = IDIO_SUBSTRING_PARENT (str);

	if (offset > IDIO_STRING_LEN (parent)) {
	    /*
	     * Test Case: ??
	     *
	     * Not sure how to provoke this as there are guards on
	     * most callers
	     */
	    idio_substring_length_error ("starts beyond parent string", parent, offset, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	size_t ss_offset = (IDIO_SUBSTRING_S (str) - IDIO_STRING_S (IDIO_SUBSTRING_PARENT (str))) / width;
	size_t prospective_len = ss_offset + offset + len;
	if (IDIO_STRING_LEN (parent) < prospective_len) {
	    /*
	     * Test Case: ??
	     *
	     * Not sure how to provoke this as there are guards on
	     * most callers
	     */
	    idio_substring_length_error ("extends beyond parent substring", parent, prospective_len, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	if (offset > IDIO_STRING_LEN (str)) {
	    /*
	     * Test Case: ??
	     *
	     * Not sure how to provoke this as there are guards on
	     * most callers
	     */
	    idio_substring_length_error ("starts beyond parent string", str, offset, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	size_t prospective_len = offset + len;
	if (IDIO_STRING_LEN (str) < prospective_len) {
	    /*
	     * Test Case: ??
	     *
	     * Not sure how to provoke this as there are guards on
	     * most callers
	     */
	    idio_substring_length_error ("extends beyond parent string", str, prospective_len, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO so = idio_gc_get (IDIO_TYPE_SUBSTRING);
    IDIO_SUBSTRING_LEN (so) = len;

    if (idio_isa (str, IDIO_TYPE_SUBSTRING)) {
	IDIO_SUBSTRING_S (so) = IDIO_SUBSTRING_S (str) + offset * width;
	IDIO_SUBSTRING_PARENT (so) = IDIO_SUBSTRING_PARENT (str);
    } else {
	IDIO_SUBSTRING_S (so) = IDIO_STRING_S (str) + offset * width;
	IDIO_SUBSTRING_PARENT (so) = str;
    }

    return so;
}

int idio_isa_substring (IDIO so)
{
    IDIO_ASSERT (so);

    return idio_isa (so, IDIO_TYPE_SUBSTRING);
}

void idio_free_substring (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (substring, so);
}

size_t idio_string_len (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    size_t blen = 0;

    switch (idio_type (so)) {
    case IDIO_TYPE_STRING:
	blen = IDIO_STRING_LEN (so);
	break;
    case IDIO_TYPE_SUBSTRING:
	blen = IDIO_SUBSTRING_LEN (so);
	break;
    }

    return blen;
}

/*
 * caller must idio_gc_free(3) this string
 */
char *idio_string_as_C (IDIO so, size_t *sizep)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    return idio_utf8_string (so, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED, IDIO_UTF8_STRING_USEPREC);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("make-string", make_string, (IDIO size, IDIO args), "size [fillc]", "\
create a string with an initial length of `size`\n\
						\n\
:param size: initial string size		\n\
:type size: integer				\n\
:param fillc: fill character value, defaults to \n\
	``#\\{space}``				\n\
:type fillc: unicode, optional			\n\
:return: the new string				\n\
:rtype: string					\n\
")
{
    IDIO_ASSERT (size);
    IDIO_ASSERT (args);

    ptrdiff_t cp_count = -1;

    if (idio_isa_fixnum (size)) {
	cp_count = IDIO_FIXNUM_VAL (size);
    } else if (idio_isa_bignum (size)) {
	if (IDIO_BIGNUM_INTEGER_P (size)) {
	    /*
	     * Test Case: ??
	     *
	     * Not sure we want to create a enormous string for test.
	     */
	    cp_count = idio_bignum_ptrdiff_t_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		/*
		 * Test Case: string-errors/make-string-float.idio
		 *
		 * make-string 1.5
		 */
		idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		/*
		 * Code coverage:
		 *
		 * make-string 1e1
		 */
		cp_count = idio_bignum_ptrdiff_t_value (size_i);
	    }
	}
    } else {
	/*
	 * Test Case: string-errors/make-string-unicode.idio
	 *
	 * make-string #\a
	 */
	idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (cp_count < 0) {
	/*
	 * Test Case: string-errors/make-string-negative.idio
	 *
	 * make-string -10
	 */
	idio_string_size_error ("invalid", cp_count, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    idio_unicode_t fillc = ' ';
    IDIO_FLAGS_T flags = IDIO_STRING_FLAG_1BYTE;

    if (idio_S_nil != args) {
	IDIO fill = IDIO_PAIR_H (args);
	/*
	 * Test Case: string-errors/make-string-bad-fill-type.idio
	 *
	 * make-string 5 #t
	 */
	IDIO_USER_TYPE_ASSERT (unicode, fill);

	fillc = IDIO_UNICODE_VAL (fill);
	if (idio_unicode_valid_code_point (fillc)) {
	    if (fillc > 0xffff) {
		flags = IDIO_STRING_FLAG_4BYTE;
	    } else if (fillc > 0xff) {
		flags = IDIO_STRING_FLAG_2BYTE;
	    }
	} else {
	    /*
	     * Hopefully, this is guarded against elsewhere
	     */
	    fprintf (stderr, "make-string: oops fillc=%#x is invalid\n", fillc);
	}
    }

    /*
     * How many bytes do we need?
     */
    size_t reqd_bytes = 0;
    switch (flags) {
    case IDIO_STRING_FLAG_1BYTE:
	reqd_bytes = cp_count;
	break;
    case IDIO_STRING_FLAG_2BYTE:
	reqd_bytes = cp_count * 2;
	break;
    case IDIO_STRING_FLAG_4BYTE:
	reqd_bytes = cp_count * 4;
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    char em[30];
	    idio_snprintf (em, 30, "%#x", flags);

	    idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);

    IDIO_GC_ALLOC (IDIO_STRING_S (so), reqd_bytes + 1);
    IDIO_STRING_BLEN (so) = reqd_bytes;

    uint8_t *us8 = (uint8_t *) IDIO_STRING_S (so);
    uint16_t *us16 = (uint16_t *) IDIO_STRING_S (so);
    uint32_t *us32 = (uint32_t *) IDIO_STRING_S (so);

    size_t i;
    for (i = 0; i < cp_count; i++) {
	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    us8[i] = (uint8_t) fillc;
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    us16[i] = (uint16_t) fillc;
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    us32[i] = (uint32_t) fillc;
	    break;
	default:
	    {
		/*
		 * Test Case: ??
		 *
		 * Coding error.  Should have been caught above.
		 */
		char em[30];
		idio_snprintf (em, 30, "%#x", flags);

		idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}
    }

    IDIO_STRING_S (so)[reqd_bytes] = '\0';

    IDIO_STRING_LEN (so) = cp_count;
    IDIO_STRING_FLAGS (so) = flags;

    return so;
}

IDIO_DEFINE_PRIMITIVE1_DS ("string->octet-string", string2octet_string, (IDIO s), "s", "\
return an octet string of the UTF-8 encoding of `s`\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:return: octet string				\n\
:rtype: octet string				\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: string-errors/string2octet-string-bad-type.idio
     *
     * string->octet-string #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    size_t size = 0;
    char *s_C = NULL;

    s_C = idio_string_as_C (s, &size);

    IDIO r = idio_octet_string_C_len (s_C, size);

    IDIO_GC_FREE (s_C, size);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("string->pathname", string2pathname, (IDIO s), "s", "\
return a pathname of the UTF-8 encoding of `s`\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:return: pathname				\n\
:rtype: pathname				\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: string-errors/string2pathname-bad-type.idio
     *
     * string->pathname #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    size_t size = 0;
    char *s_C = NULL;

    s_C = idio_string_as_C (s, &size);

    IDIO r = idio_pathname_C_len (s_C, size);

    IDIO_GC_FREE (s_C, size);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("string->list", string2list, (IDIO s), "s", "\
return a list of the Unicode code points in `s`	\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:return: list of Unicode code points		\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: string-errors/string2list-bad-type.idio
     *
     * string->list #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    uint8_t *s8 = NULL;
    uint16_t *s16 = NULL;
    uint32_t *s32 = NULL;
    size_t slen = -1;
    size_t width = idio_string_storage_size (s);

    if (idio_isa_substring (s)) {
	slen = IDIO_SUBSTRING_LEN (s);
	switch (width) {
	case 1:
	    s8 = (uint8_t *) IDIO_SUBSTRING_S (s);
	    break;
	case 2:
	    s16 = (uint16_t *) IDIO_SUBSTRING_S (s);
	    break;
	case 4:
	    s32 = (uint32_t *) IDIO_SUBSTRING_S (s);
	    break;
	}
    } else {
	slen = IDIO_STRING_LEN (s);
	switch (width) {
	case 1:
	    s8 = (uint8_t *) IDIO_STRING_S (s);
	    break;
	case 2:
	    s16 = (uint16_t *) IDIO_STRING_S (s);
	    break;
	case 4:
	    s32 = (uint32_t *) IDIO_STRING_S (s);
	    break;
	}
    }

    IDIO r = idio_S_nil;

    ptrdiff_t si;
    for (si = slen - 1; si >=0; si--) {
	switch (width) {
	case 1:
	    r = idio_pair (IDIO_UNICODE (s8[si]), r);
	    break;
	case 2:
	    r = idio_pair (IDIO_UNICODE (s16[si]), r);
	    break;
	case 4:
	    r = idio_pair (IDIO_UNICODE (s32[si]), r);
	    break;
	}
    }

    return r;
}

/*
 * Why is list->string here and not in pair.c?
 *
 * Probably because knwoing stuff about the internals of strings is
 * more bespoke than knowing about the internals of lists.  So keep
 * the knowledge in as few places as possible.
 */
IDIO idio_list_list2string (IDIO l)
{
    IDIO_ASSERT (l);

    IDIO_TYPE_ASSERT (list, l);

    IDIO_FLAGS_T flags = IDIO_STRING_FLAG_1BYTE;

    IDIO l0 = l;

    size_t cp_count = 0;
    while (idio_S_nil != l) {
	IDIO h = IDIO_PAIR_H (l);

	if (idio_isa_unicode (h)) {
	    idio_unicode_t codepoint = IDIO_UNICODE_VAL (h);

	    if (codepoint > 0xffff) {
		flags = IDIO_STRING_FLAG_4BYTE;
	    } else if (codepoint > 0xff &&
		       IDIO_STRING_FLAG_1BYTE == flags) {
		flags = IDIO_STRING_FLAG_2BYTE;
	    }
	    cp_count++;
	} else {
	    /*
	     * Test Case: string-errors/list2string-bad-value.idio
	     *
	     * list->string '(#\a #\b c)
	     */
	    idio_error_param_type ("unicode", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	l = IDIO_PAIR_T (l);
	cp_count++;
    }

    /*
     * How many bytes do we need?
     */
    size_t reqd_bytes = 0;
    switch (flags) {
    case IDIO_STRING_FLAG_1BYTE:
	reqd_bytes = cp_count;
	break;
    case IDIO_STRING_FLAG_2BYTE:
	reqd_bytes = cp_count * 2;
	break;
    case IDIO_STRING_FLAG_4BYTE:
	reqd_bytes = cp_count * 4;
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    char em[30];
	    idio_snprintf (em, 30, "%#x", flags);

	    idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);

    IDIO_GC_ALLOC (IDIO_STRING_S (so), reqd_bytes + 1);
    IDIO_STRING_BLEN (so) = reqd_bytes;

    uint8_t *us8 = (uint8_t *) IDIO_STRING_S (so);
    uint16_t *us16 = (uint16_t *) IDIO_STRING_S (so);
    uint32_t *us32 = (uint32_t *) IDIO_STRING_S (so);

    cp_count = 0;
    l = l0;

    while (idio_S_nil != l) {
	IDIO h = IDIO_PAIR_H (l);

	idio_unicode_t codepoint = IDIO_UNICODE_VAL (h);

	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    us8[cp_count] = (uint8_t) codepoint;
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    us16[cp_count] = (uint16_t) codepoint;
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    us32[cp_count] = (uint32_t) codepoint;
	    break;
	default:
	    {
		/*
		 * Test Case: ??
		 *
		 * Coding error.  Should have been caught above.
		 */
		char em[30];
		idio_snprintf (em, 30, "%#x", flags);

		idio_string_utf8_decode_error ("unexpected flag", em, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	l = IDIO_PAIR_T (l);
	cp_count++;
    }

    IDIO_STRING_S (so)[reqd_bytes] = '\0';

    IDIO_STRING_LEN (so) = cp_count;
    IDIO_STRING_FLAGS (so) = flags;

    return so;
}

IDIO_DEFINE_PRIMITIVE1_DS ("list->string", list2string, (IDIO l), "l", "\
return a string from the list of the Unicode	\n\
code points in `l`				\n\
						\n\
:param l: list of code points			\n\
:type s: list					\n\
:return: string					\n\
:rtype: string					\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: string-errors/list2string-bad-type.idio
     *
     * list->string #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_list2string (l);
}

IDIO_DEFINE_PRIMITIVE1_DS ("string->symbol", string2symbol, (IDIO s), "s", "\
return a symbol derived from `s`		\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:return: symbol					\n\
:rtype: symbol					\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: string-errors/string2symbol-bad-type.idio
     *
     * string->symbol #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    size_t size = 0;
    char *sC = idio_string_as_C (s, &size);

    /*
     * Use size + 1 to avoid a truncation warning -- we're just
     * seeing if sC includes a NUL
     */
    size_t C_size = idio_strnlen (sC, size + 1);
    if (C_size != size) {
	/*
	 * Test Case: string-errors/string2symbol-format.idio
	 *
	 * string->symbol (join-string (make-string 1 #U+0) '("hello" "world"))
	 */
	IDIO_GC_FREE (sC, size);

	idio_string_format_error ("string contains an ASCII NUL", s, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_symbols_C_intern (sC, size);

    IDIO_GC_FREE (sC, size);

    return r;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("append-string", append_string, (IDIO args), "[args]", "\
append strings								\n\
									\n\
:param args: strings to append together					\n\
:type args: list, optional						\n\
:return: string	(\"\" if no `args` supplied)				\n\
									\n\
``append-string`` will gracefully degrade the string variant based on	\n\
the arguments: `unicode` > `pathname` > `octet-string`			\n\
									\n\
``append-string`` takes multiple arguments each of which is		\n\
a string.								\n\
									\n\
.. seealso:: :ref:`concatenate-string <concatenate-string>` which takes	\n\
	a single argument which is a list of strings.			\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_S_nil;

    ptrdiff_t ns = idio_list_length (args);
    if (ns) {
	char *copies[ns];
	size_t lens[ns];
	int substring[ns];

	ptrdiff_t si;
	for (si = 0; si < ns ; si++) {
	    copies[si] = NULL;
	    lens[si] = 0;
	    substring[si] = 0;
	}

	IDIO oargs = args;

	/*
	 * Careful!  We need to degenerate the string type where
	 * unicode > pathname > octet
	 *
	 * This is incovenient as unicode strings will need to be
	 * encoded as UTF-8 whereas pathname and octet-strings will
	 * need to be copied verbatim.
	 *
	 * However, we don't know the result string's variant until
	 * we've looked at all the inputs and we don't know how long
	 * an encoded UTF-8 string is until we've encoded it and we
	 * won't do that unless we have to.
	 */
	IDIO_FLAGS_T r_flags = IDIO_STRING_FLAG_1BYTE;
	size_t cp_count = 0;

	for (si = 0; idio_S_nil != args; si++) {
	    IDIO str = IDIO_PAIR_H (args);

	    /*
	     * Test Case: string-errors/append-string-bad-arg-type.idio
	     *
	     * append-string #t
	     */
	    IDIO_USER_TYPE_ASSERT (string, str);

	    IDIO_FLAGS_T str_flags;

	    if (idio_isa (str, IDIO_TYPE_SUBSTRING)) {
		substring[si] = 1;
		str_flags = IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (str));
		cp_count += IDIO_SUBSTRING_LEN (str);
	    } else {
		str_flags = IDIO_STRING_FLAGS (str);
		cp_count += IDIO_STRING_LEN (str);
	    }

	    switch (str_flags) {
	    case IDIO_STRING_FLAG_1BYTE:
		break;
	    case IDIO_STRING_FLAG_2BYTE:
		if (IDIO_STRING_FLAG_1BYTE == r_flags) {
		    r_flags = IDIO_STRING_FLAG_2BYTE;
		}
		break;
	    case IDIO_STRING_FLAG_4BYTE:
		if (IDIO_STRING_FLAG_1BYTE == r_flags ||
		    IDIO_STRING_FLAG_2BYTE == r_flags) {
		    r_flags = IDIO_STRING_FLAG_4BYTE;
		}
		break;
	    case IDIO_STRING_FLAG_OCTET:
		r_flags = str_flags;
		break;
	    case IDIO_STRING_FLAG_PATHNAME:
		if (r_flags != IDIO_STRING_FLAG_OCTET) {
		    r_flags = str_flags;
		}
		break;
	    default:
		{
		    switch (str_flags) {
		    case IDIO_STRING_FLAG_FD_PATHNAME:
			idio_error_param_value_msg_only ("append-string", "string", "illegal use of FD pathname", IDIO_C_FUNC_LOCATION ());
			break;
		    case IDIO_STRING_FLAG_FIFO_PATHNAME:
			idio_error_param_value_msg_only ("append-string", "string", "illegal use of FIFO pathname", IDIO_C_FUNC_LOCATION ());
			break;
		    default:
			idio_error_param_value_msg_only ("append-string", "string", "unexpected string variant", IDIO_C_FUNC_LOCATION ());
			break;
		    }

		    return idio_S_notreached;
		}
	    }

	    args = IDIO_PAIR_T (args);
	}

	/*
	 * How many bytes do we need to allocate for the result?
	 *
	 * If the result is a unicode string then it is the cp_count *
	 * cp width.
	 *
	 * If we are degrading then it is the sum of the lengths of
	 * the UTF-8 encodings of the unicode string parts plus the
	 * lengths of the pathname / octet-string parts.
	 */
	size_t reqd_bytes = 0;

	switch (r_flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    reqd_bytes = cp_count;
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    reqd_bytes = cp_count * 2;
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    reqd_bytes = cp_count * 4;
	    break;
	case IDIO_STRING_FLAG_OCTET:
	case IDIO_STRING_FLAG_PATHNAME:
	    {
		args = oargs;
		for (si = 0; idio_S_nil != args; si++) {
		    IDIO str = IDIO_PAIR_H (args);

		    size_t size = 0;

		    IDIO_FLAGS_T str_flags;
		    size_t str_len;

		    if (substring[si]) {
			str_flags = IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (str));
			str_len = IDIO_SUBSTRING_LEN (str);
		    } else {
			str_flags = IDIO_STRING_FLAGS (str);
			str_len = IDIO_STRING_LEN (str);
		    }

		    switch (str_flags) {
		    case IDIO_STRING_FLAG_1BYTE:
		    case IDIO_STRING_FLAG_2BYTE:
		    case IDIO_STRING_FLAG_4BYTE:
			copies[si] = idio_string_as_C (str, &size);
			lens[si] = size;
			reqd_bytes += size;
			break;
		    case IDIO_STRING_FLAG_OCTET:
		    case IDIO_STRING_FLAG_PATHNAME:
			reqd_bytes += str_len;
			break;
		    }

		    args = IDIO_PAIR_T (args);
		}
	    }
	}

	IDIO so = idio_gc_get (IDIO_TYPE_STRING);

	IDIO_GC_ALLOC (IDIO_STRING_S (so), reqd_bytes + 1);
	IDIO_STRING_BLEN (so) = reqd_bytes;

	uint8_t *so8 = (uint8_t *) IDIO_STRING_S (so);
	uint16_t *so16 = (uint16_t *) IDIO_STRING_S (so);
	uint32_t *so32 = (uint32_t *) IDIO_STRING_S (so);

	size_t ri = 0;
	args = oargs;
	for (si = 0; idio_S_nil != args; si++) {
	    IDIO str = IDIO_PAIR_H (args);

	    uint8_t *str8;
	    uint16_t *str16;
	    uint32_t *str32;

	    IDIO_FLAGS_T str_flags;
	    size_t str_len;

	    if (substring[si]) {
		str8 = (uint8_t *) IDIO_SUBSTRING_S (str);
		str16 = (uint16_t *) IDIO_SUBSTRING_S (str);
		str32 = (uint32_t *) IDIO_SUBSTRING_S (str);
		str_flags = IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (str));
		str_len = IDIO_SUBSTRING_LEN (str);
	    } else {
		str8 = (uint8_t *) IDIO_STRING_S (str);
		str16 = (uint16_t *) IDIO_STRING_S (str);
		str32 = (uint32_t *) IDIO_STRING_S (str);
		str_flags = IDIO_STRING_FLAGS (str);
		str_len = IDIO_STRING_LEN (str);
	    }

	    switch (r_flags) {
	    case IDIO_STRING_FLAG_1BYTE:
	    case IDIO_STRING_FLAG_2BYTE:
	    case IDIO_STRING_FLAG_4BYTE:
		{
		    int fault = 0;
		    for (size_t str_i = 0; str_i < str_len; str_i++, ri++) {
			switch (r_flags) {
			case IDIO_STRING_FLAG_1BYTE:
			    switch (str_flags) {
			    case IDIO_STRING_FLAG_1BYTE:
				so8[ri] = str8[str_i];
				break;
			    default:
				fault = 1;
				break;
			    }
			    break;
			case IDIO_STRING_FLAG_2BYTE:
			    switch (str_flags) {
			    case IDIO_STRING_FLAG_1BYTE:
				so16[ri] = str8[str_i];
				break;
			    case IDIO_STRING_FLAG_2BYTE:
				so16[ri] = str16[str_i];
				break;
			    default:
				fault = 1;
				break;
			    }
			    break;
			case IDIO_STRING_FLAG_4BYTE:
			    switch (str_flags) {
			    case IDIO_STRING_FLAG_1BYTE:
				so32[ri] = str8[str_i];
				break;
			    case IDIO_STRING_FLAG_2BYTE:
				so32[ri] = str16[str_i];
				break;
			    case IDIO_STRING_FLAG_4BYTE:
				so32[ri] = str32[str_i];
				break;
			    default:
				fault = 1;
				break;
			    }
			    break;
			}

			if (fault) {
			    /*
			     * Test Case: coding error
			     */
			    idio_error_param_value_msg_only ("append-string", "string", "unexpected string width", IDIO_C_FUNC_LOCATION ());

			    return idio_S_notreached;
			}
		    }
		    break;
		}
		break;
	    case IDIO_STRING_FLAG_OCTET:
		{
		    switch (str_flags) {
		    case IDIO_STRING_FLAG_1BYTE:
		    case IDIO_STRING_FLAG_2BYTE:
		    case IDIO_STRING_FLAG_4BYTE:
			{
			    for (size_t str_i = 0; str_i < lens[si]; str_i++, ri++) {
				so8[ri] = copies[si][str_i];
			    }
			}
			break;
		    case IDIO_STRING_FLAG_OCTET:
		    case IDIO_STRING_FLAG_PATHNAME:
			{
			    for (size_t str_i = 0; str_i < str_len; str_i++, ri++) {
				so8[ri] = str8[str_i];
			    }
			}
			break;
		    default:
			idio_error_param_value_msg_only ("append-string", "string", "unexpected string width", IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }
		}
		break;
	    case IDIO_STRING_FLAG_PATHNAME:
		{
		    switch (str_flags) {
		    case IDIO_STRING_FLAG_1BYTE:
		    case IDIO_STRING_FLAG_2BYTE:
		    case IDIO_STRING_FLAG_4BYTE:
			{
			    for (size_t str_i = 0; str_i < lens[si]; str_i++, ri++) {
				so8[ri] = copies[si][str_i];
			    }
			}
			break;
		    case IDIO_STRING_FLAG_PATHNAME:
			{
			    for (size_t str_i = 0; str_i < str_len; str_i++, ri++) {
				if (substring[si]) {
				    so8[ri] = IDIO_SUBSTRING_S (str) [str_i];
				} else {
				    so8[ri] = IDIO_STRING_S (str) [str_i];
				}
			    }
			}
			break;
		    default:
			idio_error_param_value_msg_only ("append-string", "string", "unexpected string width", IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }
		}
		break;
	    default:
		/*
		 * Test Case: coding error
		 */
		idio_error_param_value_msg_only ("append-string", "string", "unexpected string width", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    args = IDIO_PAIR_T (args);
	}

	for (si = 0; si < ns; si++) {
	    IDIO_GC_FREE (copies[si], lens[si]);
	}

	IDIO_STRING_S (so)[reqd_bytes] = '\0';

	IDIO_STRING_FLAGS (so) = r_flags;
	IDIO_STRING_LEN (so) = ri;

	r = so;
    } else {
	r = idio_string_C_len ("", 0);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("copy-string", copy_string, (IDIO s), "s", "\
return a copy of `s` which is not ``eq?`` to `s`	\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:return: string					\n\
:rtype: string					\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: string-errors/copy-string-bad-type.idio
     *
     * copy-string #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    return idio_copy_string (s);
}

IDIO_DEFINE_PRIMITIVE1_DS ("string-length", string_length, (IDIO s), "s", "\
return the number of code points in `s`		\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:return: number of code points			\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: string-errors/string-length-bad-type.idio
     *
     * string-length #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    return idio_integer (idio_string_len (s));
}

IDIO idio_string_ref_C (IDIO s, size_t i)
{
    IDIO_ASSERT (s);

    IDIO_TYPE_ASSERT (string, s);

    uint8_t *s8 = NULL;
    uint16_t *s16 = NULL;
    uint32_t *s32 = NULL;
    size_t slen = -1;
    size_t width = idio_string_storage_size (s);

    if (idio_isa_substring (s)) {
	slen = IDIO_SUBSTRING_LEN (s);
	switch (width) {
	case 1:
	    s8 = (uint8_t *) IDIO_SUBSTRING_S (s);
	    break;
	case 2:
	    s16 = (uint16_t *) IDIO_SUBSTRING_S (s);
	    break;
	case 4:
	    s32 = (uint32_t *) IDIO_SUBSTRING_S (s);
	    break;
	}
    } else {
	slen = IDIO_STRING_LEN (s);
	switch (width) {
	case 1:
	    s8 = (uint8_t *) IDIO_STRING_S (s);
	    break;
	case 2:
	    s16 = (uint16_t *) IDIO_STRING_S (s);
	    break;
	case 4:
	    s32 = (uint32_t *) IDIO_STRING_S (s);
	    break;
	}
    }

    if (i < 0 ||
	i >= slen) {
	/*
	 * Test Cases:
	 *
	 *   string-errors/string-ref-negative.idio
	 *   string-errors/string-ref-too-large.idio
	 *   string-errors/string-ref-bignum.idio
	 *
	 * string-ref "hello world" -1
	 * string-ref "hello world" 100
	 * string-ref "hello world" #x2000000000000000
	 */
	idio_string_length_error ("out of bounds", s, i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    switch (width) {
    case 1:
	return IDIO_UNICODE (s8[i]);
	break;
    case 2:
	return IDIO_UNICODE (s16[i]);
	break;
    case 4:
	return IDIO_UNICODE (s32[i]);
	break;
    }

    /* probably... */
    return idio_S_notreached;
}

IDIO idio_string_ref (IDIO s, IDIO index)
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);

    IDIO_TYPE_ASSERT (string, s);

    ptrdiff_t i = -1;

    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    /*
	     * Test Case: string-errors/string-ref-bignum.idio
	     *
	     * This is a code coverage case which will (probably)
	     * provoke the out of bounds error below.
	     */
	    i = idio_bignum_ptrdiff_t_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		/*
		 * Test Case: string-errors/string-ref-float.idio
		 *
		 * string-ref "hello world" 1.5
		 */
		idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		i = idio_bignum_ptrdiff_t_value (index_i);
	    }
	}
    } else {
	/*
	 * Test Case: string-errors/string-ref-unicode.idio
	 *
	 * string-ref "hello world" #\a
	 */
	idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_string_ref_C (s, i);
}

IDIO_DEFINE_PRIMITIVE2_DS ("string-ref", string_ref, (IDIO s, IDIO index), "s index", "\
return code point at position `index` in `s`	\n\
						\n\
positions start at 0				\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:param index: position				\n\
:type index: integer				\n\
:return: code point				\n\
:rtype: unicode					\n\
")
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);

    /*
     * Test Case: string-errors/string-ref-bad-type.idio
     *
     * string-ref #t 0
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    return idio_string_ref (s, index);
}

IDIO idio_string_set (IDIO s, IDIO index, IDIO c)
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);
    IDIO_ASSERT (c);

    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (unicode, c);

    /*
     * Test Case: string-errors/string-set-constant.idio
     *
     * string-set! "hello" 1.5 #\a
     */
    IDIO_ASSERT_NOT_CONST (string, s);

    ptrdiff_t i = -1;

    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    /*
	     * Test Case: string-errors/string-set-bignum.idio
	     *
	     * string-set! (copy-string "hello") #x2000000000000000
	     *
	     * This is a code coverage case which will (probably)
	     * provoke the out of bounds error below.
	     */
	    i = idio_bignum_ptrdiff_t_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		/*
		 * Test Case: string-errors/string-set-float.idio
		 *
		 * string-set! (copy-string "hello") 1.5 #\a
		 */
		idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		i = idio_bignum_ptrdiff_t_value (index_i);
	    }
	}
    } else {
	/*
	 * Test Case: string-errors/string-set-unicode.idio
	 *
	 * string-set! (copy-string "hello") #\a #\a
	 */
	idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    size_t cwidth = 0;
    idio_unicode_t uc = IDIO_UNICODE_VAL (c);
    if (idio_unicode_valid_code_point (uc)) {
	if (uc <= 0xff) {
	    cwidth = 1;
	} else if (uc <= 0xffff) {
	    cwidth = 2;
	} else {
	    cwidth = 4;
	}
    } else {
	/*
	 * Hopefully, this is guarded against elsewhere
	 */
	fprintf (stderr, "string-set: oops c=%#x is invalid\n", uc);
    }

    uint8_t *s8 = NULL;
    uint16_t *s16 = NULL;
    uint32_t *s32 = NULL;
    size_t slen = -1;
    size_t width = idio_string_storage_size (s);

    if (idio_isa_substring (s)) {
	slen = IDIO_SUBSTRING_LEN (s);
	switch (width) {
	case 1:
	    s8 = (uint8_t *) IDIO_SUBSTRING_S (s);
	    break;
	case 2:
	    s16 = (uint16_t *) IDIO_SUBSTRING_S (s);
	    break;
	case 4:
	    s32 = (uint32_t *) IDIO_SUBSTRING_S (s);
	    break;
	}
    } else {
	slen = IDIO_STRING_LEN (s);
	switch (width) {
	case 1:
	    s8 = (uint8_t *) IDIO_STRING_S (s);
	    break;
	case 2:
	    s16 = (uint16_t *) IDIO_STRING_S (s);
	    break;
	case 4:
	    s32 = (uint32_t *) IDIO_STRING_S (s);
	    break;
	}
    }

    if (cwidth > width) {
	/*
	 * Test Case: string-errors/string-set-too-wide.idio
	 *
	 * string-set! (copy-string "hello") 0 #U+127
	 */
	idio_string_width_error ("replacement char too wide", s, c, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (i < 0 ||
	i >= slen) {
	/*
	 * Test Cases: string-errors/string-set-negative.idio
	 * Test Cases: string-errors/string-set-too-large.idio
	 */
	idio_string_length_error ("out of bounds", s, i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    switch (width) {
    case 1:
	s8[i] = uc;
	break;
    case 2:
	s16[i] = uc;
	break;
    case 4:
	s32[i] = uc;
	break;
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("string-set!", string_set, (IDIO s, IDIO index, IDIO c), "s index c", "\
set position `index` of `s` to `c`		\n\
						\n\
positions start at 0				\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:param index: position				\n\
:type index: integer				\n\
:param c: code point				\n\
:type c: unicode				\n\
:return: ``#<unspec>``				\n\
						\n\
`string-set!` will fail if `c` is wider than the\n\
existing storage allocation for `s`		\n\
")
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);
    IDIO_ASSERT (c);

    /*
     * Test Case: string-errors/string-set-bad-type.idio
     *
     * string-set! #t 0 #U+0
     */
    IDIO_USER_TYPE_ASSERT (string, s);
    /*
     * Test Case: string-errors/string-set-bad-value-type.idio
     *
     * string-set! (make-string 5) 0 #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, c);

    return idio_string_set (s, index, c);
}

IDIO_DEFINE_PRIMITIVE2_DS ("string-fill!", string_fill, (IDIO s, IDIO fill), "s fill", "\
set all positions of `s` to `fill`		\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:param fill: code point				\n\
:type fill: unicode				\n\
:return: ``#<unspec>``				\n\
						\n\
`string-fill!` will fail if `c` is wider than the\n\
existing storage allocation for `s`		\n\
")
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (fill);

    /*
     * Test Case: string-errors/string-fill-bad-type.idio
     *
     * string-fill! #t #U+0
     */
    IDIO_USER_TYPE_ASSERT (string, s);
    /*
     * Test Case: string-errors/string-fill-bad-value-type.idio
     *
     * string-fill! (make-string 5) #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, fill);

    /*
     * Test Case: string-errors/string-fill-constant.idio
     *
     * string-fill! "hello" #\a
     */
    IDIO_ASSERT_NOT_CONST (string, s);

    /*
     * Ostensibly string-fill! is a loop over string-set!
     *
     * It could be quicker by duplicating the storage width code here
     */

    size_t slen;

    if (idio_isa_substring (s)) {
	slen = IDIO_SUBSTRING_LEN (s);
    } else {
	slen = IDIO_STRING_LEN (s);
    }

    size_t i;
    for (i = 0; i < slen; i++) {
	idio_string_set (s, idio_fixnum (i), fill);
    }

    return s;
}

/*
 * substring
 *
 * This is the Scheme style interface: substring str offset end
 *
 * where {end} is *not* included in the substring.
 *
 * The (most common) alternative is: substring str offset len
 */
IDIO_DEFINE_PRIMITIVE2V_DS ("substring", substring, (IDIO s, IDIO p0, IDIO args), "s p0 [pn]", "\
return a substring of `s` from position `p0`	\n\
through to but excluding position `pn`		\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:param p0: position				\n\
:type p0: integer				\n\
:param pn: position, defaults to string length	\n\
:type pn: integer, optional			\n\
:return: the substring				\n\
:rtype: string					\n\
						\n\
If `p0` or `pn` are negative they are considered	\n\
to be with respect to the end of the string.  This	\n\
can still result in a negative index.			\n\
")
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (p0);
    IDIO_ASSERT (args);

    /*
     * Test Case: string-errors/substring-bad-type.idio
     *
     * substring #t 0 1
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    size_t l = idio_string_len (s);

    ptrdiff_t ip0 = -1;
    ptrdiff_t ipn = l;

    if (idio_isa_fixnum (p0)) {
	ip0 = IDIO_FIXNUM_VAL (p0);
    } else if (idio_isa_bignum (p0)) {
	if (IDIO_BIGNUM_INTEGER_P (p0)) {
	    /*
	     * Test Case: string-errors/substring-offset-bignum.idio
	     *
	     * substring "hello" #x2000000000000000
	     *
	     * This is a code coverage case which will (probably)
	     * provoke the out of bounds error below.
	     */
	    ip0 = idio_bignum_ptrdiff_t_value (p0);
	} else {
	    IDIO p0_i = idio_bignum_real_to_integer (p0);
	    if (idio_S_nil == p0_i) {
		/*
		 * Test Case: string-errors/substring-offset-float.idio
		 *
		 * substring "hello world" 1.5 2
		 */
		idio_error_param_type ("integer", p0, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		ip0 = idio_bignum_ptrdiff_t_value (p0_i);
	    }
	}
    } else {
	/*
	 * Test Case: string-errors/substring-offset-unicode.idio
	 *
	 * substring "hello world" #\a 2
	 */
	idio_error_param_type ("integer", p0, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (ip0 < 0) {
	ip0 += l;
    }

    if (idio_S_nil != args) {
	IDIO pn = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (pn)) {
	    ipn = IDIO_FIXNUM_VAL (pn);
	} else if (idio_isa_bignum (pn)) {
	    if (IDIO_BIGNUM_INTEGER_P (pn)) {
		/*
		 * Test Case: string-errors/substring-length-bignum.idio
		 *
		 * substring "hello" 0 #x2000000000000000
		 *
		 * This is a code coverage case which will (probably)
		 * provoke the out of bounds error below.
		 */
		ipn = idio_bignum_ptrdiff_t_value (pn);
	    } else {
		IDIO pn_i = idio_bignum_real_to_integer (pn);
		if (idio_S_nil == pn_i) {
		    /*
		     * Test Case: string-errors/substring-length-float.idio
		     *
		     * substring "hello world" 2 1.5
		     */
		    idio_error_param_type ("integer", pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		} else {
		    ipn = idio_bignum_ptrdiff_t_value (pn_i);
		}
	    }
	} else {
	    /*
	     * Test Case: string-errors/substring-length-unicode.idio
	     *
	     * substring "hello world" 0 #\a
	     */
	    idio_error_param_type ("integer", pn, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (ipn < 0) {
	ipn += l;
    }

    if (ip0 < 0 ||
	ip0 > l ||
	ipn < 0 ||
	ipn > l ||
	ipn < ip0) {
	/*
	 * Test Case: string-errors/substring-oob-offset-negative.idio
	 * Test Case: string-errors/substring-oob-offset-too-large.idio
	 * Test Case: string-errors/substring-oob-length-negative.idio
	 * Test Case: string-errors/substring-oob-length-too-large.idio
	 * Test Case: string-errors/substring-oob-start-after-end.idio
	 */
	idio_substring_index_error ("out of bounds", s, ip0, ipn, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_unspec;
    ptrdiff_t rl = ipn - ip0;

    if (rl) {
	r = idio_substring_offset_len (s, ip0, rl);
    } else {
	r = idio_string_C_len ("", 0);
    }

    return r;
}

IDIO idio_string_index (IDIO s, IDIO c, int from_end)
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (c);

    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (unicode, c);

    size_t slen;

    if (idio_isa_substring (s)) {
	slen = IDIO_SUBSTRING_LEN (s);
    } else {
	slen = IDIO_STRING_LEN (s);
    }

    for (size_t i = 0; i < slen; i++) {
	size_t index = from_end ? slen - 1 - i : i;
	IDIO sc = idio_string_ref_C (s, index);
	if (sc == c) {
	    return idio_fixnum (index);
	}
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("string-index", string_index, (IDIO s, IDIO c), "s c", "\
return the index of `c` in `s` or ``#f``		\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:param c: code point				\n\
:type c: unicode				\n\
:return: index or ``#f``				\n\
:rtype: integer or ``#f``				\n\
")
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (c);

    /*
     * Test Case: string-errors/string-index-bad-string-type.idio
     *
     * string-index #t #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    /*
     * Test Case: string-errors/string-index-bad-character-type.idio
     *
     * string-index "hello" #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, c);

    return idio_string_index (s, c, 0);
}

IDIO_DEFINE_PRIMITIVE2_DS ("string-rindex", string_rindex, (IDIO s, IDIO c), "s c", "\
return the rightmost index of `c` in `s` or ``#f``	\n\
						\n\
:param s: string				\n\
:type s: string					\n\
:param c: code point				\n\
:type c: unicode				\n\
:return: index or ``#f``				\n\
:rtype: integer or ``#f``				\n\
")
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (c);

    /*
     * Test Case: string-errors/string-rindex-bad-string-type.idio
     *
     * string-rindex #t #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    /*
     * Test Case: string-errors/string-rindex-bad-character-type.idio
     *
     * string-rindex "hello" #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, c);

    return idio_string_index (s, c, 1);
}

int idio_string_equal (IDIO s1, IDIO s2)
{
    IDIO_ASSERT (s1);
    IDIO_ASSERT (s2);

    IDIO_TYPE_ASSERT (string, s1);
    IDIO_TYPE_ASSERT (string, s2);

    uint8_t *s1_8 = NULL;
    uint16_t *s1_16 = NULL;
    uint32_t *s1_32 = NULL;
    size_t s1len = -1;
    size_t s1w = idio_string_storage_size (s1);

    if (idio_isa_substring (s1)) {
	s1len = IDIO_SUBSTRING_LEN (s1);
	switch (s1w) {
	case 1:
	    s1_8 = (uint8_t *) IDIO_SUBSTRING_S (s1);
	    break;
	case 2:
	    s1_16 = (uint16_t *) IDIO_SUBSTRING_S (s1);
	    break;
	case 4:
	    s1_32 = (uint32_t *) IDIO_SUBSTRING_S (s1);
	    break;
	}
    } else {
	s1len = IDIO_STRING_LEN (s1);
	switch (s1w) {
	case 1:
	    s1_8 = (uint8_t *) IDIO_STRING_S (s1);
	    break;
	case 2:
	    s1_16 = (uint16_t *) IDIO_STRING_S (s1);
	    break;
	case 4:
	    s1_32 = (uint32_t *) IDIO_STRING_S (s1);
	    break;
	}
    }

    uint8_t *s2_8 = NULL;
    uint16_t *s2_16 = NULL;
    uint32_t *s2_32 = NULL;
    size_t s2len = -1;
    size_t s2w = idio_string_storage_size (s2);

    if (idio_isa_substring (s2)) {
	s2len = IDIO_SUBSTRING_LEN (s2);
	switch (s2w) {
	case 1:
	    s2_8 = (uint8_t *) IDIO_SUBSTRING_S (s2);
	    break;
	case 2:
	    s2_16 = (uint16_t *) IDIO_SUBSTRING_S (s2);
	    break;
	case 4:
	    s2_32 = (uint32_t *) IDIO_SUBSTRING_S (s2);
	    break;
	}
    } else {
	s2len = IDIO_STRING_LEN (s2);
	switch (s2w) {
	case 1:
	    s2_8 = (uint8_t *) IDIO_STRING_S (s2);
	    break;
	case 2:
	    s2_16 = (uint16_t *) IDIO_STRING_S (s2);
	    break;
	case 4:
	    s2_32 = (uint32_t *) IDIO_STRING_S (s2);
	    break;
	}
    }

    if (s1len != s2len) {
	/*
	 * Probably only a coding error can get here as the length
	 * comparison should have occurred in idio_equal().
	 */
	return 0;
    }

    size_t i;
    for (i = 0; i < s1len; i++) {
	idio_unicode_t s1_cp = 0;
	switch (s1w) {
	case 1:
	    s1_cp = s1_8[i];
	    break;
	case 2:
	    s1_cp = s1_16[i];
	    break;
	case 4:
	    s1_cp = s1_32[i];
	    break;
	}

	idio_unicode_t s2_cp = 0;
	switch (s2w) {
	case 1:
	    s2_cp = s2_8[i];
	    break;
	case 2:
	    s2_cp = s2_16[i];
	    break;
	case 4:
	    s2_cp = s2_32[i];
	    break;
	}

	if (s1_cp != s2_cp) {
	    return 0;
	}
    }

    return 1;
}

/*
 * All the string-*? are essentially identical.
 *
 * Note: From the Guile docs:
 *
 * If two strings differ in length but are the same up to the length
 * of the shorter string, the shorter string is considered to be less
 * than the longer string.
 *
 */
/*
 * Test Cases:
 *
 *   string-errors/string=-bad-first-type.idio
 *   string-errors/string=-bad-second-type.idio
 *   string-errors/string=-bad-remainder-type.idio
 *
 * string=? #t "hello"
 * string=? "hello" #t
 * string=? "hello" "hello" #t
 */
#define IDIO_DEFINE_STRING_PRIMITIVE2V(name,cname,cmp,scmp)		\
    IDIO_DEFINE_PRIMITIVE2V (name, cname, (IDIO s1, IDIO s2, IDIO args)) \
    {									\
	IDIO_ASSERT (s1);						\
	IDIO_ASSERT (s2);						\
	IDIO_USER_TYPE_ASSERT (string, s1);				\
	IDIO_USER_TYPE_ASSERT (string, s2);				\
	IDIO_USER_TYPE_ASSERT (list, args);				\
									\
	args = idio_pair (s2, args);					\
									\
	size_t l1 = 0;							\
	char *C1 = idio_string_as_C (s1, &l1);				\
									\
	while (idio_S_nil != args) {					\
	    s2 = IDIO_PAIR_H (args);					\
	    IDIO_USER_TYPE_ASSERT (string, s2);				\
	    size_t l2 = 0;						\
	    char *C2 = idio_string_as_C (s2, &l2);			\
									\
	    int l = l1;							\
	    if (l2 < l1) {						\
		l = l2;							\
	    }								\
									\
	    int sc = scmp (C1, C2, l);					\
									\
	    /* C result */						\
	    int cr = (sc cmp 0);					\
									\
	    if (0 == sc) {						\
		cr = (l1 cmp l2);					\
	    }								\
									\
	    if (0 == cr) {						\
		IDIO_GC_FREE (C1, l1);					\
		IDIO_GC_FREE (C2, l2);					\
		return idio_S_false;					\
	    }								\
									\
	    IDIO_GC_FREE (C1, l1);					\
	    C1 = C2;							\
	    l1 = l2;							\
	    args = IDIO_PAIR_T (args);					\
	}								\
									\
	IDIO_GC_FREE (C1, l1);						\
	return idio_S_true;						\
    }

#define IDIO_DEFINE_STRING_CS_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_STRING_PRIMITIVE2V (name, string_ci_ ## cname ## _p, cmp, strncmp)

#define IDIO_DEFINE_STRING_CI_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_STRING_PRIMITIVE2V (name, string_ ## cname ## _p, cmp, strncasecmp)

IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string<?", lt, <)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string<=?", le, <=)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string=?", eq, ==)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string>=?", ge, >=)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string>?", gt, >)

IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci<?", lt, <)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci<=?", le, <=)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci=?", eq, ==)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci>=?", ge, >=)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci>?", gt, >)

/*
 * Tokenizing Strings
 *
 * strtok(3)/strtok_r(3)/strsep(3) all tokenize a string but modify
 * the original string at the same time (actually, they not only
 * insert NUL but therefore lose the identity of the character that
 * caused the tokenization).
 *
 * Here in Idio we want to take advantage of Idio substrings being
 * true substrings of a parent string.  The reason the SUBSTRING type
 * exists, in fact, is for string-splitting.
 *
 * Clearly we can't run around destroying the original string as
 * anyone referencing it will be "disappointed."  Hence, we need to
 * have a tokenizer that will return both start and end (preferably
 * length) of the token.  As usual we can't return two things in C so
 * we'll have to pass in a modifiable length parameter.  Noting the
 * re-entrancy issues with strtok (and presumably strsep) we should
 * presumably pass in a modifiable "saved" parameter too.
 *
 * We can pass a flag to indicate how aggressive we should be in
 * tokenizing -- primarily whether adjacent delimiters generate
 * multiple tokens (strtok() vs. strsep()).  That is, does "a::b" with
 * a ":" delimiter become 2 tokens: "a" and "b" or three tokens: "a",
 * "" and "b"?
 *
 * As we should have the previous token-causing delimiter available to
 * us we can then be just plain weird and generate tokens only if the
 * adjacent delimiters differ.  (Must find a use-case for that.)
 *
 *
 * This code started out much like the code in GCC's libc strtok_r()
 * implementation then modifed for a length limit, creating a sort of
 * strntok_r().
 *
 * It has been modified again to operate on Unicode strings,
 * ie. arrays of code points (of some width) -- therefore including
 * ASCII NULs -- such that the code isn't passing 'char *'s around but
 * simply offsets into the original string, {in}.
 */

/*
 * idio_strspn()
 *
 * cf. size_t strspn(char const *s, char const *accept);
 *
 *   The strspn() function calculates the length (in bytes) of the
 *   initial segment of s which consists entirely of bytes in accept.
 *
 *   The strspn() function returns the number of bytes in the initial
 *   segment of s which consist only of bytes from accept.
 *
 * Of course, we're returning the number of Unicode code points of the
 * initial segment ...
 */
static size_t idio_strspn (ptrdiff_t i, char const *is, size_t const ilen, size_t const iw, char const *as, size_t const alen, size_t const aw)
{
    uint8_t *is8 = NULL;
    uint16_t *is16 = NULL;
    uint32_t *is32 = NULL;

    switch (iw) {
    case 1:
	is8 = (uint8_t *) is;
	break;
    case 2:
	is16 = (uint16_t *) is;
	break;
    case 4:
	is32 = (uint32_t *) is;
	break;
    }

    uint8_t *as8 = NULL;
    uint16_t *as16 = NULL;
    uint32_t *as32 = NULL;

    switch (aw) {
    case 1:
	as8 = (uint8_t *) as;
	break;
    case 2:
	as16 = (uint16_t *) as;
	break;
    case 4:
	as32 = (uint32_t *) as;
	break;
    }

    size_t r;
    for (r = 0; (i + r) < ilen; r++) {
	size_t ii = i + r;
	idio_unicode_t icp = 0;
	switch (iw) {
	case 1:
	    icp = is8[ii];
	    break;
	case 2:
	    icp = is16[ii];
	    break;
	case 4:
	    icp = is32[ii];
	    break;
	}

	int seen = 0;
	for (size_t ai = 0; ai < alen ; ai++) {
	    idio_unicode_t acp = 0;
	    switch (aw) {
	    case 1:
		acp = as8[ai];
		break;
	    case 2:
		acp = as16[ai];
		break;
	    case 4:
		acp = as32[ai];
		break;
	    }

	    if (icp == acp) {
		seen = 1;
		break;
	    }
	}

	if (0 == seen) {
	    return r;
	}
    }

    return r;
}

/*
 * idio_strpbrk()
 *
 * cf. char *strpbrk(char const *s, char const *accept);
 *
 *   The strpbrk() function locates the first occurrence in the string
 *   s of any of the bytes in the string accept.
 *
 *   The strpbrk() function returns a pointer to the byte in s that
 *   matches one of the bytes in accept, or NULL if no such byte is
 *   found.
 *
 *
 * Note that the C string version of idio_strpbrk, using 'char *'s
 * rather than offsets, can use NULL as a sentinel value -- which is
 * outside of the string.  We're using offsets so must return an
 * invalid offset such as -1.
 */
#define IDIO_STRING_STRPBRK_SENTINEL	-1

static ptrdiff_t idio_strpbrk (ptrdiff_t i, char const *is, size_t const ilen, size_t const iw, char const *as, size_t const alen, size_t const aw)
{
    uint8_t *is8 = NULL;
    uint16_t *is16 = NULL;
    uint32_t *is32 = NULL;

    switch (iw) {
    case 1:
	is8 = (uint8_t *) is;
	break;
    case 2:
	is16 = (uint16_t *) is;
	break;
    case 4:
	is32 = (uint32_t *) is;
	break;
    }

    uint8_t *as8 = NULL;
    uint16_t *as16 = NULL;
    uint32_t *as32 = NULL;

    switch (aw) {
    case 1:
	as8 = (uint8_t *) as;
	break;
    case 2:
	as16 = (uint16_t *) as;
	break;
    case 4:
	as32 = (uint32_t *) as;
	break;
    }

    ptrdiff_t r;
    for (r = i; r < ilen; r++) {
	idio_unicode_t icp = 0;
	switch (iw) {
	case 1:
	    icp = is8[r];
	    break;
	case 2:
	    icp = is16[r];
	    break;
	case 4:
	    icp = is32[r];
	    break;
	}

	for (size_t ai = 0; ai < alen ; ai++) {
	    idio_unicode_t acp = 0;
	    switch (aw) {
	    case 1:
		acp = as8[ai];
		break;
	    case 2:
		acp = as16[ai];
		break;
	    case 4:
		acp = as32[ai];
		break;
	    }

	    if (icp == acp) {
		return r;
	    }
	}
    }

    return IDIO_STRING_STRPBRK_SENTINEL;
}

/*
 * Note that the C string version of idio_string_token, using 'char
 * *'s rather than offsets, can use NULL as a sentinel value -- which
 * is outside of the string.  We'll have to use -1 (and ptrdiff_t
 * rather than size_t) to do something similar.
 */
#define IDIO_STRING_TOKEN_SENTINEL	-1

static ptrdiff_t idio_string_token (ptrdiff_t i, char const *is, size_t const ilen, size_t const iw, char const *ds, size_t const dlen, size_t const dw, int flags, ptrdiff_t *saved, size_t *len)
{
    /*
     * U+FFFF is specifically not a valid Unicode character so it's a
     * safe enough bet for a sentinel value.
     */
    idio_unicode_t prev_delim = 0xFFFF;

    uint8_t *is8 = NULL;
    uint16_t *is16 = NULL;
    uint32_t *is32 = NULL;

    switch (iw) {
    case 1:
	is8 = (uint8_t *) is;
	break;
    case 2:
	is16 = (uint16_t *) is;
	break;
    case 4:
	is32 = (uint32_t *) is;
	break;
    }

    /*
     * 1. pickup from where we left off.  This won't happen the first
     * time through.
     *
     * If the pickup (saved) is the sentinel value then we're done.
     */
    if (IDIO_STRING_TOKEN_SENTINEL == i) {
	i = *saved;
	if (IDIO_STRING_TOKEN_SENTINEL == i) {
	    *len = 0;
	    return IDIO_STRING_TOKEN_SENTINEL;
	}
	switch (iw) {
	case 1:
	    prev_delim = is8[i - 1];
	    break;
	case 2:
	    prev_delim = is16[i - 1];
	    break;
	case 4:
	    prev_delim = is32[i - 1];
	    break;
	}
    }

    if (IDIO_STRING_TOKEN_INEXACT (flags)) {
	i += idio_strspn (i, is, ilen, iw, ds, dlen, dw);
    } else if (0 &&
	       0xFFFF != prev_delim) {
	ptrdiff_t start = i;
	ptrdiff_t end = i + idio_strspn (i, is, ilen, iw, ds, dlen, dw);
	for (; i < end; i++) {
	    idio_unicode_t icp = 0;
	    switch (iw) {
	    case 1:
		icp = is8[i];
		break;
	    case 2:
		icp = is16[i];
		break;
	    case 4:
		icp = is32[i];
		break;
	    }
	    if (prev_delim != icp) {
		*len = start - i;
		return i;
	    }
	}
    }

    /*
     * If we're at (or beyond?) the end of the string then both set
     * the pickup up for and return the sentinel value.  We shouldn't
     * re-enter but we might.
     */
    if (i >= ilen) {
	*saved = IDIO_STRING_TOKEN_SENTINEL;
	*len = 0;
	return IDIO_STRING_TOKEN_SENTINEL;
    }

    /*
     * Walk forward until we find a delimiter
     */
    ptrdiff_t start = idio_strpbrk (i, is, ilen, iw, ds, dlen, dw);

    if (IDIO_STRING_STRPBRK_SENTINEL == start) {
	*saved = IDIO_STRING_TOKEN_SENTINEL;
	*len = ilen - i;
    } else {
	if (start < ilen) {
	    *saved = start + 1;
	    *len = start - i;
	} else {
	    /*
	     * Test Case: ??
	     */
	    *saved = IDIO_STRING_TOKEN_SENTINEL;
	    *len = start - i;
	}
    }

    return i;
}

IDIO idio_split_string (IDIO in, IDIO delim, int flags)
{
    IDIO_ASSERT (in);
    IDIO_ASSERT (delim);

    IDIO_TYPE_ASSERT (string, in);
    IDIO_TYPE_ASSERT (string, delim);

    char *is;
    size_t ilen = -1;
    size_t iw = idio_string_storage_size (in);

    if (idio_isa_substring (in)) {
	is = IDIO_SUBSTRING_S (in);
	ilen = IDIO_SUBSTRING_LEN (in);
    } else {
	is = IDIO_STRING_S (in);
	ilen = IDIO_STRING_LEN (in);
    }

    if (0 == ilen) {
	/*
	 * split-string "" delim
	 */

	return IDIO_LIST1 (in);
    }

    char *ds;
    size_t dlen = -1;
    size_t dw = idio_string_storage_size (delim);

    if (idio_isa_substring (delim)) {
	ds = IDIO_SUBSTRING_S (delim);
	dlen = IDIO_SUBSTRING_LEN (delim);
    } else {
	ds = IDIO_STRING_S (delim);
	dlen = IDIO_STRING_LEN (delim);
    }

    if (0 == dlen) {
	/*
	 * split-string str ""
	 *
	 * What does that mean, semantically?
	 *
	 * Is it an error?  It's not syntactically wrong so probably not.
	 *
	 * Is it split-string to the max?  There's an empty string in
	 * between every character in a string?  Not sure as the
	 * delimiter string is the set of characters you want to use
	 * to split the string with.  You should use string->list in
	 * this case, anyway.
	 *
	 * Is it saying there are no delimiter characters therefore do
	 * not split the string?
	 */
	return IDIO_LIST1 (in);
    }

    /*
     * offsets within string
     */
    ptrdiff_t saved;
    size_t len = ilen;

    ptrdiff_t i = 0;
    IDIO r = idio_S_nil;
    int wants_array = 0;
    if (IDIO_STRING_TOKEN_ARRAY (flags)) {
	 wants_array = 1;
	 r = idio_array (4);
	 idio_array_push (r, in);
    }

    for (; ; i = IDIO_STRING_TOKEN_SENTINEL) {
	ptrdiff_t start = idio_string_token (i, is, ilen, iw, ds, dlen, dw, flags, &saved, &len);

	if (IDIO_STRING_TOKEN_SENTINEL == start) {
	    break;
	}

	if (IDIO_STRING_TOKEN_INEXACT (flags) &&
	    0 == len) {
	    break;
	}

	IDIO subs = idio_substring_offset_len (in, start, len);
	if (wants_array) {
	     idio_array_push (r, subs);
	} else {
	     r = idio_pair (subs, r);
	}
    }

    if (wants_array) {
	 return r;
    } else {
	 return idio_list_reverse (r);
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("split-string", split_string, (IDIO in, IDIO args), "in [delim]", "\
split string `in` using characters from `delim`		\n\
into a list of strings					\n\
							\n\
:param in: string to split				\n\
:type in: string					\n\
:param delim: delimiter characters, defaults to :ref:`IFS <IFS>`	\n\
:type delim: string, optional				\n\
:return: list (of strings)				\n\
							\n\
Adjacent characters from `delim` are considered a	\n\
single delimiter.					\n\
							\n\
.. seealso:: :ref:`split-string-exactly <split-string-exactly>`	\n\
	which treats `delim` more fastidiously		\n\
	and :ref:`fields <fields>` which returns an array	\n\
")
{
    IDIO_ASSERT (in);
    IDIO_ASSERT (args);

    /*
     * Test Case: string-errors/split-string-bad-type.idio
     *
     * split-string #t "abc"
     */
    IDIO_USER_TYPE_ASSERT (string, in);

    IDIO delim;
    if (idio_S_nil == args) {
	delim = idio_module_current_symbol_value_recurse_defined (idio_vars_IFS_sym);
    } else {
	delim = IDIO_PAIR_H (args);
    }

    /*
     * Test Cases:
     *   string-errors/split-string-bad-delim-type.idio
     *   string-errors/split-string-bad-IFS-type.idio
     *
     * split-string "abc" #t
     */
    IDIO_USER_TYPE_ASSERT (string, delim);

    return idio_split_string (in, delim, IDIO_STRING_TOKEN_FLAG_NONE);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("split-string-exactly", split_string_exactly, (IDIO in, IDIO args), "in [delim]", "\
split string `in` using characters from `delim`		\n\
into a list	 of strings				\n\
							\n\
:param in: string to split				\n\
:type in: string					\n\
:param delim: delimiter characters, defaults to :ref:`IFS <IFS>`	\n\
:type delim: string, optional				\n\
:return: list (of strings)				\n\
							\n\
Adjacent characters from `delim` are considered		\n\
separate delimiters.					\n\
							\n\
.. seealso:: :ref:`split-string <split-string>`		\n\
")
{
    IDIO_ASSERT (in);
    IDIO_ASSERT (args);

    /*
     * Test Case: string-errors/split-string-exactly-bad-type.idio
     *
     * split-string-exactly #t "abc"
     */
    IDIO_USER_TYPE_ASSERT (string, in);

    IDIO delim;
    if (idio_S_nil == args) {
	delim = idio_module_current_symbol_value_recurse_defined (idio_vars_IFS_sym);
    } else {
	delim = IDIO_PAIR_H (args);
    }

    /*
     * Test Cases:
     *   string-errors/split-string-exactly-bad-delim-type.idio
     *   string-errors/split-string-exactly-bad-IFS-type.idio
     *
     * split-string-exactly "abc" #t
     */
    IDIO_USER_TYPE_ASSERT (string, delim);

    return idio_split_string (in, delim, IDIO_STRING_TOKEN_FLAG_EXACT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("fields", fields, (IDIO in), "in", "\
split string `in` using characters from :ref:`IFS <IFS>`\n\
into an array with the first element the original string\n\
							\n\
:param in: string to split				\n\
:type in: string					\n\
:return: array (of strings)				\n\
							\n\
Adjacent characters from :var:`IFS` are considered a	\n\
single delimiter.					\n\
							\n\
.. seealso:: :ref:`split-string <split-string>` which	\n\
	returns a list	\n\
")
{
    IDIO_ASSERT (in);

    /*
     * Test Case: string-errors/fields-bad-type.idio
     *
     * fields #t
     */
    IDIO_USER_TYPE_ASSERT (string, in);

    IDIO delim = idio_module_current_symbol_value_recurse_defined (idio_vars_IFS_sym);

    /*
     * Test Case: string-errors/fields-bad-IFS-type.idio
     *
     * IFS :~ #t
     * fields "abc"
     */
    IDIO_USER_TYPE_ASSERT (string, delim);

    return idio_split_string (in, delim, IDIO_STRING_TOKEN_FLAG_ARRAY);
}

IDIO idio_strip_string (IDIO str, IDIO discard, IDIO ends)
{
    IDIO_ASSERT (str);
    IDIO_ASSERT (discard);
    IDIO_ASSERT (ends);

    IDIO_TYPE_ASSERT (string, str);
    IDIO_TYPE_ASSERT (string, discard);
    IDIO_TYPE_ASSERT (list, ends);

    char *ss;
    size_t slen = -1;
    size_t sw = idio_string_storage_size (str);

    if (idio_isa_substring (str)) {
	ss = IDIO_SUBSTRING_S (str);
	slen = IDIO_SUBSTRING_LEN (str);
    } else {
	ss = IDIO_STRING_S (str);
	slen = IDIO_STRING_LEN (str);
    }

    if (0 == slen) {
	/*
	 * strip-string "" discard
	 */

	return str;
    }

    char *ds;
    size_t dlen = -1;
    size_t dw = idio_string_storage_size (discard);

    if (idio_isa_substring (discard)) {
	ds = IDIO_SUBSTRING_S (discard);
	dlen = IDIO_SUBSTRING_LEN (discard);
    } else {
	ds = IDIO_STRING_S (discard);
	dlen = IDIO_STRING_LEN (discard);
    }

    if (0 == dlen) {
	/*
	 * strip-string str ""
	 */
	return str;
    }

#define IDIO_STRING_STRIP_LEFT	(1<<0)
#define IDIO_STRING_STRIP_RIGHT	(1<<1)

    int ends_bits = IDIO_STRING_STRIP_RIGHT;

    if (idio_S_nil != ends) {
	IDIO end = IDIO_PAIR_H (ends);
	if (idio_S_left == end) {
	    ends_bits = IDIO_STRING_STRIP_LEFT;
	} else if (idio_S_right == end) {
	    ends_bits = IDIO_STRING_STRIP_RIGHT;
	} else if (idio_S_both == end) {
	    ends_bits = IDIO_STRING_STRIP_LEFT | IDIO_STRING_STRIP_RIGHT;
	} else if (idio_S_none == end) {
	    return str;
	} else {
	    /*
	     * Test Case: string-errors/strip-string-bad-ends.idio
	     *
	     * strip-string "abc" "a" 'neither
	     */
	    idio_error_param_value_msg ("strip-string", "ends", end, "should be 'left, 'right, 'both or 'none", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    uint8_t *ss8 = NULL;
    uint16_t *ss16 = NULL;
    uint32_t *ss32 = NULL;

    switch (sw) {
    case 1:
	ss8 = (uint8_t *) ss;
	break;
    case 2:
	ss16 = (uint16_t *) ss;
	break;
    case 4:
	ss32 = (uint32_t *) ss;
	break;
    }

    uint8_t *ds8 = NULL;
    uint16_t *ds16 = NULL;
    uint32_t *ds32 = NULL;

    switch (dw) {
    case 1:
	ds8 = (uint8_t *) ds;
	break;
    case 2:
	ds16 = (uint16_t *) ds;
	break;
    case 4:
	ds32 = (uint32_t *) ds;
	break;
    }

    size_t rso = 0;
    if (ends_bits & IDIO_STRING_STRIP_LEFT) {
	for (;rso < slen; rso++) {
	    idio_unicode_t scp = 0;
	    switch (sw) {
	    case 1:
		scp = ss8[rso];
		break;
	    case 2:
		scp = ss16[rso];
		break;
	    case 4:
		scp = ss32[rso];
		break;
	    }

	    size_t di;
	    for (di = 0; di < dlen; di++) {
		idio_unicode_t dcp = 0;
		switch (dw) {
		case 1:
		    dcp = ds8[di];
		    break;
		case 2:
		    dcp = ds16[di];
		    break;
		case 4:
		    dcp = ds32[di];
		    break;
		}

		if (scp == dcp) {
		    /* leave the discard for loop */
		    break;
		}
	    }

	    if (di == dlen) {
		/* leave the left strip loop */
		break;
	    }
	}

	if (rso == slen) {
	    /* stripped the lot */
	    return idio_substring_offset_len (str, 0, 0);
	}
    }

    /* cheeky!  but we're decrementing */
    ssize_t reo = slen - 1;

    /*
     * There is at least one non-discard character at the
     * start of the string therefore the reo decrement will never
     * (probably) reach zero and be decremented -- which is never good
     * for a size_t.
     */
    if (rso < slen &&
	ends_bits & IDIO_STRING_STRIP_RIGHT) {
	for (;reo >= rso; reo--) {
	    if (reo < 0) {
		break;
	    }
	    idio_unicode_t scp = 0;
	    switch (sw) {
	    case 1:
		scp = ss8[reo];
		break;
	    case 2:
		scp = ss16[reo];
		break;
	    case 4:
		scp = ss32[reo];
		break;
	    }

	    size_t di;
	    for (di = 0; di < dlen; di++) {
		idio_unicode_t dcp = 0;
		switch (dw) {
		case 1:
		    dcp = ds8[di];
		    break;
		case 2:
		    dcp = ds16[di];
		    break;
		case 4:
		    dcp = ds32[di];
		    break;
		}

		if (scp == dcp) {
		    /* leave the discard for loop */
		    break;
		}
	    }

	    if (di == dlen) {
		/* leave the right strip loop */
		break;
	    }
	}
    }

    if (0 == rso &&
	slen == reo) {
	return str;
    } else {
	return idio_substring_offset_len (str, rso, reo - rso + 1);
    }
}


IDIO_DEFINE_PRIMITIVE2V_DS ("strip-string", strip_string, (IDIO str, IDIO discard, IDIO ends), "str discard [ends]", "\
return a string which is `str` without leading,	\n\
trailing (or both) `discard` characters		\n\
						\n\
:param str: string				\n\
:type str: string				\n\
:param discard: string				\n\
:type discard: string				\n\
:param ends: ``'left``, ``'right`` (default), ``'both`` or ``'none``	\n\
:type ends: symbol, optional			\n\
:return: string					\n\
						\n\
The returned value could be `str` or a substring\n\
of `str`					\n\
")
{
    IDIO_ASSERT (str);
    IDIO_ASSERT (discard);
    IDIO_ASSERT (ends);

    /*
     * Test Case: string-errors/strip-string-bad-str-type.idio
     *
     * strip-string #t #t
     */
    IDIO_USER_TYPE_ASSERT (string, str);
    /*
     * Test Case: string-errors/strip-string-bad-discard-type.idio
     *
     * strip-string "abc" #t
     */
    IDIO_USER_TYPE_ASSERT (string, discard);
    /*
     * Test Case: n/a
     *
     * ends is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, ends);

    return idio_strip_string (str, discard, ends);
}

void idio_string_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (string_p);
    IDIO_ADD_PRIMITIVE (octet_string_p);
    IDIO_ADD_PRIMITIVE (make_string);
    IDIO_ADD_PRIMITIVE (string2octet_string);
    IDIO_ADD_PRIMITIVE (string2pathname);
    IDIO_ADD_PRIMITIVE (string2list);
    IDIO_ADD_PRIMITIVE (list2string);
    IDIO_ADD_PRIMITIVE (string2symbol);
    IDIO_ADD_PRIMITIVE (append_string);
    IDIO_ADD_PRIMITIVE (copy_string);
    IDIO_ADD_PRIMITIVE (string_length);
    IDIO_ADD_PRIMITIVE (string_ref);
    IDIO_ADD_PRIMITIVE (string_set);
    IDIO_ADD_PRIMITIVE (string_fill);
    IDIO_ADD_PRIMITIVE (substring);
    IDIO_ADD_PRIMITIVE (string_index);
    IDIO_ADD_PRIMITIVE (string_rindex);

    IDIO_ADD_PRIMITIVE (string_ci_le_p);
    IDIO_ADD_PRIMITIVE (string_ci_lt_p);
    IDIO_ADD_PRIMITIVE (string_ci_eq_p);
    IDIO_ADD_PRIMITIVE (string_ci_ge_p);
    IDIO_ADD_PRIMITIVE (string_ci_gt_p);
    IDIO_ADD_PRIMITIVE (string_le_p);
    IDIO_ADD_PRIMITIVE (string_lt_p);
    IDIO_ADD_PRIMITIVE (string_eq_p);
    IDIO_ADD_PRIMITIVE (string_ge_p);
    IDIO_ADD_PRIMITIVE (string_gt_p);

    IDIO_ADD_PRIMITIVE (split_string);
    IDIO_ADD_PRIMITIVE (split_string_exactly);
    IDIO_ADD_PRIMITIVE (fields);
    IDIO_ADD_PRIMITIVE (strip_string);
}

void idio_init_string ()
{
    idio_module_table_register (idio_string_add_primitives, NULL, NULL);
}
