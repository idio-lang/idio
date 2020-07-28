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
 * string.c
 *
 */

#include "idio.h"

static void idio_string_error_utf8_decode (const char *s_C, IDIO c_location, char *msg)
{
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("UTF-8 decode: ", msh);
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C ((char *) s_C, dsh);

    IDIO c = idio_struct_instance (idio_condition_idio_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       c_location,
					       idio_get_output_string (dsh)));

    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

void idio_string_error_length (char *m, IDIO s, ptrdiff_t i, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (s);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (string, c_location);

    char em[BUFSIZ];
    sprintf (em, "%s: %td", m, i);
    idio_error_C (em, s, c_location);
}

void idio_substring_error_index (char *m, IDIO s, ptrdiff_t ip0, ptrdiff_t ipn, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (s);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (string, c_location);

    char em[BUFSIZ];
    sprintf (em, "%s: %td %td", m, ip0, ipn);
    idio_error_C (em, s, c_location);
}

IDIO idio_string_C_len (const char *s_C, size_t blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);

    IDIO_FPRINTF (stderr, "idio_string_C: %10p = '%s'\n", so, s_C);

    IDIO_FLAGS_T flags = IDIO_STRING_FLAG_1BYTE;

    idio_unicode_t codepoint;
    idio_unicode_t state = IDIO_UTF8_ACCEPT;

    size_t cp_count = 0;
    uint8_t *s8 = (unsigned char *) s_C;
    size_t i;
    for (i = 0; i < blen; s8++, i++) {
	for ( ; i < blen; s8++, i++) {
	    if (idio_utf8_decode (&state, &codepoint, *s8) == IDIO_UTF8_ACCEPT) {
		break;
	    }
	}

	if (state != IDIO_UTF8_ACCEPT) {
	    fprintf (stderr, "The (UTF-8) string [%s] is not well-formed at ['%c' %u %xu %x] %zu/%zu\n", s_C, *s8, *s8, *s8, *s8, i, blen);
	    IDIO_C_ASSERT (0);
	    exit (3);
	    /*
	     * XXX passing the not-well-formed string into the error
	     * handler will result it it trying to be printed (to a
	     * output string handle) which means it will come through
	     * this code...only to fail again!
	     */
	    idio_string_error_utf8_decode (s_C, IDIO_C_FUNC_LOCATION (), "not well-formed");

	    return idio_S_notreached;
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
    }

    if (reqd_bytes < blen) {
	fprintf (stderr, "string: allocating %zu REQD bytes for blen %zu??\n", reqd_bytes, blen);
    }
    IDIO_GC_ALLOC (IDIO_STRING_S (so), reqd_bytes + 1);
    IDIO_STRING_BLEN (so) = reqd_bytes;

    switch (flags) {
    case IDIO_STRING_FLAG_1BYTE:
	memcpy (IDIO_STRING_S (so), s_C, blen);
	IDIO_STRING_S (so)[blen] = '\0';
	IDIO_STRING_LEN (so) = blen;
	break;
    case IDIO_STRING_FLAG_2BYTE:
    case IDIO_STRING_FLAG_4BYTE:
	{
	    state = IDIO_UTF8_ACCEPT;

	    cp_count = 0;
	    uint16_t *s16 = (uint16_t *) IDIO_STRING_S (so);
	    uint32_t *s32 = (uint32_t *) IDIO_STRING_S (so);

	    s8 = (unsigned char *) s_C;
	    for (i = 0; i < blen; cp_count++, s8++, i++) {
		for (; i < blen; s8++, i++) {
		    if (idio_utf8_decode (&state, &codepoint, *s8) == IDIO_UTF8_ACCEPT) {
			break;
		    }
		}

		/* can this happen, now? */
		if (state != IDIO_UTF8_ACCEPT) {
		    fprintf (stderr, "The (UTF-8) string is not well-formed\n");
		    /*
		     * XXX passing the not-well-formed string into the
		     * error handler will result it it trying to be
		     * printed (to a output string handle) which means
		     * it will come through this code...only to fail
		     * again!
		     */
		    idio_string_error_utf8_decode ("dummy", IDIO_C_FUNC_LOCATION (), "not well-formed");

		    return idio_S_notreached;
		}

		switch (flags) {
		case IDIO_STRING_FLAG_2BYTE:
		    s16[cp_count] = (uint16_t) codepoint;
		    break;
		case IDIO_STRING_FLAG_4BYTE:
		    s32[cp_count] = (uint32_t) codepoint;
		    break;
		}
	    }
	    IDIO_STRING_LEN (so) = cp_count;
	}
	break;
    default:
	fprintf (stderr, "unexpected flag %x\n", flags);
	idio_string_error_utf8_decode (s_C, IDIO_C_FUNC_LOCATION (), "unexpected flag");

	return idio_S_notreached;
    }

    IDIO_STRING_FLAGS (so) = flags;

    return so;
}

IDIO idio_string_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_string_C_len (s_C, strlen (s_C));
}

IDIO idio_string_C_array (size_t ns, char *a_C[])
{
    IDIO_C_ASSERT (a_C);

    IDIO so;

    so = idio_gc_get (IDIO_TYPE_STRING);

    size_t ablen = 0;		/* sum of array byte length */
    size_t ai;

    IDIO_FLAGS_T flags = IDIO_STRING_FLAG_1BYTE;
    size_t cp_count = 0;

    idio_unicode_t codepoint;
    idio_unicode_t state = IDIO_UTF8_ACCEPT;
    uint8_t *s8;

    for (ai = 0; ai < ns; ai++) {
	size_t blen = strlen (a_C[ai]);
	ablen += blen;

	s8 = (unsigned char *) a_C[ai];
	for (size_t i = 0; i < blen; s8++, i++) {
	    for ( ; i < blen; s8++, i++) {
		if (idio_utf8_decode (&state, &codepoint, *s8) == IDIO_UTF8_ACCEPT) {
		    break;
		}
	    }

	    if (state != IDIO_UTF8_ACCEPT) {
		fprintf (stderr, "The (UTF-8) string [%s] is not well-formed at ['%c' %u %xu %x] %zu/%zu\n", a_C[ai], *s8, *s8, *s8, *s8, i, blen);
		IDIO_C_ASSERT (0);
		exit (3);
		/*
		 * XXX passing the not-well-formed string into the error
		 * handler will result it it trying to be printed (to a
		 * output string handle) which means it will come through
		 * this code...only to fail again!
		 */
		idio_string_error_utf8_decode (a_C[ai], IDIO_C_FUNC_LOCATION (), "not well-formed");

		return idio_S_notreached;
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
    }

    if (reqd_bytes < ablen) {
	fprintf (stderr, "string: allocating %zu REQD bytes for ablen %zu??\n", reqd_bytes, ablen);
    }

    IDIO_GC_ALLOC (IDIO_STRING_S (so), ablen + 1);

    cp_count = 0;
    uint16_t *s16 = (uint16_t *) IDIO_STRING_S (so);
    uint32_t *s32 = (uint32_t *) IDIO_STRING_S (so);

    size_t ao = 0;
    for (ai = 0; ai < ns; ai++) {
	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    {
		size_t sl = strlen (a_C[ai]);
		memcpy (IDIO_STRING_S (so) + ao, a_C[ai], sl);
		ao += sl;
		cp_count += sl;
	    }
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	case IDIO_STRING_FLAG_4BYTE:
	    {
		size_t blen = strlen (a_C[ai]);
		state = IDIO_UTF8_ACCEPT;

		s8 = (unsigned char *) a_C[ai];
		for (size_t i = 0; i < blen; cp_count++, s8++, i++) {
		    for (; i < blen; s8++, i++) {
			if (idio_utf8_decode (&state, &codepoint, *s8) == IDIO_UTF8_ACCEPT) {
			    break;
			}
		    }

		    /* can this happen, now? */
		    if (state != IDIO_UTF8_ACCEPT) {
			fprintf (stderr, "The (UTF-8) string is not well-formed\n");
			/*
			 * XXX passing the not-well-formed string into the
			 * error handler will result it it trying to be
			 * printed (to a output string handle) which means
			 * it will come through this code...only to fail
			 * again!
			 */
			idio_string_error_utf8_decode ("dummy", IDIO_C_FUNC_LOCATION (), "not well-formed");

			return idio_S_notreached;
		    }

		    switch (flags) {
		    case IDIO_STRING_FLAG_2BYTE:
			s16[cp_count] = (uint16_t) codepoint;
			break;
		    case IDIO_STRING_FLAG_4BYTE:
			s32[cp_count] = (uint32_t) codepoint;
			break;
		    }
		}
	    }
	    break;
	default:
	    fprintf (stderr, "unexpected flag %x\n", flags);
	    idio_string_error_utf8_decode (a_C[ai], IDIO_C_FUNC_LOCATION (), "unexpected flag");

	    return idio_S_notreached;
	}
    }

    IDIO_STRING_S (so)[ablen] = '\0';
    IDIO_STRING_BLEN (so) = ablen;
    IDIO_STRING_FLAGS (so) = flags;
    IDIO_STRING_LEN (so) = cp_count;

    return so;
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
	    IDIO_STRING_BLEN (copy) = blen;
	    IDIO_GC_ALLOC (IDIO_STRING_S (copy), blen + 1);
	    memcpy (IDIO_STRING_S (copy), IDIO_STRING_S (string), blen);
	    IDIO_STRING_LEN (copy) = IDIO_STRING_LEN (string);
	    IDIO_STRING_FLAGS (copy) = IDIO_STRING_FLAGS (string);
	    break;
	}
    case IDIO_TYPE_SUBSTRING:
	{
	    copy = idio_substring_offset (IDIO_SUBSTRING_PARENT (string),
					  IDIO_SUBSTRING_S (string) - IDIO_STRING_S (IDIO_SUBSTRING_PARENT (string)),
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

void idio_free_string (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    idio_gc_stats_free (IDIO_STRING_BLEN (so));

    free (IDIO_STRING_S (so));
}

/*
 * Watch out for substrings of substrings.
 *
 * If SUB1 is a substring of X and we want SUB2 to be a substring of
 * SUB1 then SUB2 is just another substring of X.
 */
IDIO idio_substring_offset (IDIO str, size_t offset, size_t len)
{
    IDIO_ASSERT (str);
    IDIO_C_ASSERT (offset >= 0);
    IDIO_C_ASSERT (len >= 0);

    IDIO_FLAGS_T flags = IDIO_STRING_FLAG_NONE;

    if (idio_isa (str, IDIO_TYPE_SUBSTRING)) {
	IDIO parent = IDIO_SUBSTRING_PARENT (str);
	size_t prospective_len = (IDIO_SUBSTRING_S (str) - IDIO_STRING_S (parent)) + offset + len;
	if (IDIO_STRING_LEN (parent) < prospective_len) {
	    idio_string_error_length ("substring extends beyong parent string length", parent, prospective_len, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	flags = IDIO_STRING_FLAGS (parent);
    } else {
	size_t prospective_len = offset + len;
	if (IDIO_STRING_LEN (str) < prospective_len) {
	    idio_string_error_length ("substring extends beyong parent string length", str, prospective_len, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	flags = IDIO_STRING_FLAGS (str);
    }

    IDIO so = idio_gc_get (IDIO_TYPE_SUBSTRING);
    IDIO_SUBSTRING_LEN (so) = len;

    size_t width = 0;
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
    default:
	fprintf (stderr, "unexpected flag %x\n", flags);
	idio_string_error_utf8_decode ("dummy", IDIO_C_FUNC_LOCATION (), "unexpected flag");

	return idio_S_notreached;
    }

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

int idio_string_cmp_C (IDIO so, char *s_C)
{
    IDIO_ASSERT (so);
    IDIO_C_ASSERT (s_C);

    IDIO_TYPE_ASSERT (string, so);

    if (idio_S_nil == so) {
	return 0;
    }

    int blen = strlen (s_C);
    IDIO_C_ASSERT (blen);

    if (IDIO_STRING_BLEN (so) < blen) {
	blen = IDIO_STRING_BLEN (so);
    }

    char *sso = idio_string_as_C (so);
    int r = strncmp (sso, s_C, blen);
    free (sso);
    return r;
}

size_t idio_string_blen (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    size_t blen = 0;

    switch (idio_type (so)) {
    case IDIO_TYPE_STRING:
	blen = IDIO_STRING_BLEN (so);
	break;
    case IDIO_TYPE_SUBSTRING:
	blen = IDIO_SUBSTRING_LEN (so);
	break;
    default:
	{
	    idio_error_C ("unexpected string type", so, IDIO_C_FUNC_LOCATION ());
	}
    }

    return blen;
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
    default:
	{
	    idio_error_C ("unexpected string type", so, IDIO_C_FUNC_LOCATION ());
	}
    }

    return blen;
}

char *idio_string_s (IDIO so)
{
    IDIO_ASSERT (so);

    idio_debug ("idio_string_s %s\n", so);

    return idio_string_as_C (so);
}

/*
 * caller must free(3) this string
 */
char *idio_string_as_C (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    return idio_utf8_string (so, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED);
}

IDIO_DEFINE_PRIMITIVE1 ("string?", string_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_string (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1V ("make-string", make_string, (IDIO size, IDIO args))
{
    IDIO_ASSERT (size);
    IDIO_ASSERT (args);

    ptrdiff_t clen = -1;

    if (idio_isa_fixnum (size)) {
	clen = IDIO_FIXNUM_VAL (size);
    } else if (idio_isa_bignum (size)) {
	if (IDIO_BIGNUM_INTEGER_P (size)) {
	    clen = idio_bignum_ptrdiff_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		idio_error_param_type ("number", size, IDIO_C_FUNC_LOCATION ());
	    } else {
		clen = idio_bignum_ptrdiff_value (size_i);
	    }
	}
    } else {
	idio_error_param_type ("number", size, IDIO_C_FUNC_LOCATION ());
    }

    if (clen < 0) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "invalid length: %zd", clen);

	return idio_S_notreached;
    }

    IDIO_VERIFY_PARAM_TYPE (list, args);

    size_t bpc = 0;
    
    idio_unicode_t fillc = ' ';
    char fills[5];
    if (idio_S_nil != args) {
	IDIO fill = IDIO_PAIR_H (args);
	IDIO_VERIFY_PARAM_TYPE (unicode, fill);

	fillc = IDIO_UNICODE_VAL (fill);
	if (fillc > 0x10ffff) {
	    fprintf (stderr, "make-string: oops fillc=%x > 0x10ffff\n", fillc);
	} else if (fillc >= 0x10000) {
	    bpc = 4;
	    sprintf (fills, "%c%c%c%c",
		     0xf0 | ((fillc & (0x07 << 18)) >> 18),
		     0x80 | ((fillc & (0x3f << 12)) >> 12),
		     0x80 | ((fillc & (0x3f << 6)) >> 6),
		     0x80 | ((fillc & (0x3f << 0)) >> 0));
	} else if (fillc >= 0x0800) {
	    bpc = 3;
	    sprintf (fills, "%c%c%c",
		     0xe0 | ((fillc & (0x0f << 12)) >> 12),
		     0x80 | ((fillc & (0x3f << 6)) >> 6),
		     0x80 | ((fillc & (0x3f << 0)) >> 0));
	} else if (fillc >= 0x0080) {
	    bpc = 2;
	    sprintf (fills, "%c%c",
		     0xc0 | ((fillc & (0x1f << 6)) >> 6),
		     0x80 | ((fillc & (0x3f << 0)) >> 0));
	} else {
	    bpc = 1;
	    sprintf (fills, "%c",
		     fillc & 0x7f);
	}
    } else {
	bpc = 1;
	sprintf (fills, "%c", fillc & 0x7f);
    }

    ptrdiff_t blen = clen * bpc;
    char *sC = idio_alloc (blen + 1);
    sC[0] = '\0';
    
    size_t i;
    for (i = 0; i < blen; i++) {
	strcat (sC, fills);
    }
    sC[blen] = '\0';

    IDIO s = idio_string_C (sC);

    free (sC);

    return s;
}

IDIO_DEFINE_PRIMITIVE1 ("string->list", string2list, (IDIO s))
{
    IDIO_ASSERT (s);
    IDIO_VERIFY_PARAM_TYPE (string, s);

    char *sC = idio_string_as_C (s);
    size_t sl = strlen (sC);

    ptrdiff_t si;

    IDIO r = idio_S_nil;

    for (si = sl - 1; si >=0; si--) {
	r = idio_pair (IDIO_UNICODE (sC[si]), r);
    }

    free (sC);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("string->symbol", string2symbol, (IDIO s))
{
    IDIO_ASSERT (s);
    IDIO_VERIFY_PARAM_TYPE (string, s);

    char *sC = idio_string_as_C (s);

    IDIO r = idio_symbols_C_intern (sC);

    free (sC);

    return r;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("append-string", append_string, (IDIO args), "[args]", "\
append strings								\n\
									\n\
:param args: strings to append together					\n\
:type args: list							\n\
:return: string	(\"\" if no ``args`` supplied)				\n\
									\n\
``append-string`` takes multiple arguments each of which is		\n\
a string.								\n\
									\n\
Compare to ``concatenate-string`` which takes a single argument,	\n\
which is a list of strings.						\n\
")
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO r = idio_S_nil;

    ptrdiff_t n = idio_list_length (args);
    if (n) {
	char *copies[n];

	ptrdiff_t i = 0;

	for (; idio_S_nil != args; i++) {
	    IDIO_VERIFY_PARAM_TYPE (string, IDIO_PAIR_H (args));

	    copies[i] = idio_string_as_C (IDIO_PAIR_H (args));

	    args = IDIO_PAIR_T (args);
	}

	r = idio_string_C_array (n, copies);

	for (i = 0; i < n; i++) {
	    free (copies[i]);
	}
    } else {
	r = idio_string_C ("");
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("concatenate-string", concatenate_string, (IDIO args), "ls", "\
concatenate strings in list ``ls``					\n\
									\n\
:param ls: list of strings to concatenate together			\n\
:type ls: list								\n\
:return: string	("" if no ``args`` supplied)				\n\
									\n\
``concatenate-string`` takes a single argument,				\n\
which is a list of strings.  It is roughly comparable to		\n\
									\n\
``apply append-string ls``						\n\
									\n\
Compare to ``append-string`` takes multiple arguments each of which is	\n\
a string.								\n\
")
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO r = idio_S_nil;

    ptrdiff_t n = idio_list_length (args);
    if (n) {
	char *copies[n];

	ptrdiff_t i = 0;

	for (; idio_S_nil != args; i++) {
	    IDIO_VERIFY_PARAM_TYPE (string, IDIO_PAIR_H (args));

	    copies[i] = idio_string_as_C (IDIO_PAIR_H (args));

	    args = IDIO_PAIR_T (args);
	}

	r = idio_string_C_array (n, copies);

	for (i = 0; i < n; i++) {
	    free (copies[i]);
	}
    } else {
	r = idio_string_C ("");
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("copy-string", copy_string, (IDIO s))
{
    IDIO_ASSERT (s);
    IDIO_VERIFY_PARAM_TYPE (string, s);

    return idio_copy_string (s);
}

IDIO_DEFINE_PRIMITIVE1 ("string-length", string_length, (IDIO s))
{
    IDIO_ASSERT (s);
    IDIO_VERIFY_PARAM_TYPE (string, s);

    return idio_fixnum (idio_string_len (s));
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
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("number", index, IDIO_C_FUNC_LOCATION ());
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("number", index, IDIO_C_FUNC_LOCATION ());
    }

    uint8_t *s8;
    uint16_t *s16;
    uint32_t *s32;
    size_t slen = -1;
    size_t width;
    
    if (idio_isa_substring (s)) {
	slen = IDIO_SUBSTRING_LEN (s);
	switch (IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (s))) {
	case IDIO_STRING_FLAG_1BYTE:
	    width = 1;
	    s8 = (uint8_t *) IDIO_SUBSTRING_S (s);
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    width = 2;
	    s16 = (uint16_t *) IDIO_SUBSTRING_S (s);
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    width = 4;
	    s32 = (uint32_t *) IDIO_SUBSTRING_S (s);
	    break;
	}
    } else {
	slen = IDIO_STRING_LEN (s);
	switch (IDIO_STRING_FLAGS (s)) {
	case IDIO_STRING_FLAG_1BYTE:
	    width = 1;
	    s8 = (uint8_t *) IDIO_STRING_S (s);
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    width = 2;
	    s16 = (uint16_t *) IDIO_STRING_S (s);
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    width = 4;
	    s32 = (uint32_t *) IDIO_STRING_S (s);
	    break;
	}
    }

    if (i < 0 ||
	i >= slen) {
	idio_string_error_length ("out of bounds", s, i, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
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

IDIO_DEFINE_PRIMITIVE2 ("string-ref", string_ref, (IDIO s, IDIO index))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);
    IDIO_VERIFY_PARAM_TYPE (string, s);

    return idio_string_ref (s, index);
}

IDIO idio_string_set (IDIO s, IDIO index, IDIO c)
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);
    IDIO_ASSERT (c);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (unicode, c);

    IDIO_ASSERT_NOT_CONST (string, s);

    ptrdiff_t i = -1;

    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("number", index, IDIO_C_FUNC_LOCATION ());
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("number", index, IDIO_C_FUNC_LOCATION ());
    }

    size_t cwidth;
    idio_unicode_t uc = IDIO_UNICODE_VAL (c);
    if (uc <= 0xff) {
	cwidth = 1;
    } else if (uc <= 0xffff) {
	cwidth = 2;
    } else if (uc > 0x10ffff) {
	fprintf (stderr, "string-set: oops c=%x > 0x10ffff\n", uc);
    } else {
	cwidth = 4;
    }

    uint8_t *s8;
    uint16_t *s16;
    uint32_t *s32;
    size_t slen = -1;
    size_t width;
    
    if (idio_isa_substring (s)) {
	slen = IDIO_SUBSTRING_LEN (s);
	switch (IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (s))) {
	case IDIO_STRING_FLAG_1BYTE:
	    width = 1;
	    s8 = (uint8_t *) IDIO_SUBSTRING_S (s);
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    width = 2;
	    s16 = (uint16_t *) IDIO_SUBSTRING_S (s);
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    width = 4;
	    s32 = (uint32_t *) IDIO_SUBSTRING_S (s);
	    break;
	}
    } else {
	slen = IDIO_STRING_LEN (s);
	switch (IDIO_STRING_FLAGS (s)) {
	case IDIO_STRING_FLAG_1BYTE:
	    width = 1;
	    s8 = (uint8_t *) IDIO_STRING_S (s);
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    width = 2;
	    s16 = (uint16_t *) IDIO_STRING_S (s);
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    width = 4;
	    s32 = (uint32_t *) IDIO_STRING_S (s);
	    break;
	}
    }

    if (cwidth > width) {
	idio_string_error_length ("replacement char too wide", s, i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    
    if (i < 0 ||
	i >= slen) {
	idio_string_error_length ("out of bounds", s, i, IDIO_C_FUNC_LOCATION ());

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

    return s;
}

IDIO_DEFINE_PRIMITIVE3 ("string-set!", string_set, (IDIO s, IDIO index, IDIO c))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);
    IDIO_ASSERT (c);
    IDIO_VERIFY_PARAM_TYPE (string, s);
    IDIO_VERIFY_PARAM_TYPE (unicode, c);

    return idio_string_set (s, index, c);
}

IDIO_DEFINE_PRIMITIVE2 ("string-fill!", string_fill, (IDIO s, IDIO fill))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (fill);
    IDIO_VERIFY_PARAM_TYPE (string, s);
    IDIO_VERIFY_PARAM_TYPE (unicode, fill);

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
 * The (nost common) alternative is: substring str offset len
 */
IDIO_DEFINE_PRIMITIVE3 ("substring", substring, (IDIO s, IDIO p0, IDIO pn))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (p0);
    IDIO_ASSERT (pn);
    IDIO_VERIFY_PARAM_TYPE (string, s);

    ptrdiff_t ip0 = -1;
    ptrdiff_t ipn = -1;

    if (idio_isa_fixnum (p0)) {
	ip0 = IDIO_FIXNUM_VAL (p0);
    } else if (idio_isa_bignum (p0)) {
	if (IDIO_BIGNUM_INTEGER_P (p0)) {
	    ip0 = idio_bignum_ptrdiff_value (p0);
	} else {
	    IDIO p0_i = idio_bignum_real_to_integer (p0);
	    if (idio_S_nil == p0_i) {
		idio_error_param_type ("number", p0, IDIO_C_FUNC_LOCATION ());
	    } else {
		ip0 = idio_bignum_ptrdiff_value (p0_i);
	    }
	}
    } else {
	idio_error_param_type ("number", p0, IDIO_C_FUNC_LOCATION ());
    }

    if (idio_isa_fixnum (pn)) {
	ipn = IDIO_FIXNUM_VAL (pn);
    } else if (idio_isa_bignum (pn)) {
	if (IDIO_BIGNUM_INTEGER_P (pn)) {
	    ipn = idio_bignum_ptrdiff_value (pn);
	} else {
	    IDIO pn_i = idio_bignum_real_to_integer (pn);
	    if (idio_S_nil == pn_i) {
		idio_error_param_type ("number", pn, IDIO_C_FUNC_LOCATION ());
	    } else {
		ipn = idio_bignum_ptrdiff_value (pn_i);
	    }
	}
    } else {
	idio_error_param_type ("number", pn, IDIO_C_FUNC_LOCATION ());
    }

    size_t l = idio_string_len (s);

    if (ip0 < 0 ||
	ip0 > l ||
	ipn < 0 ||
	ipn > l ||
	ipn < ip0) {
	idio_substring_error_index ("out of bounds", s, ip0, ipn, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }

    IDIO r = idio_S_unspec;
    ptrdiff_t rl = ipn - ip0;

    if (rl) {
	switch (s->type) {
	case IDIO_TYPE_STRING:
	    r = idio_substring_offset (s, ip0, rl);
	    break;
	case IDIO_TYPE_SUBSTRING:
	    r = idio_substring_offset (IDIO_SUBSTRING_PARENT (s), ip0, rl);
	    /* r's s is now s' parent, not s */
	    IDIO_SUBSTRING_S (r) = IDIO_SUBSTRING_S (s);
	    break;
	default:
	    idio_error_C ("unexpected string type", s, IDIO_C_FUNC_LOCATION ());
	}
    } else {
	r = idio_string_C ("");
    }

    return r;
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
#define IDIO_DEFINE_STRING_PRIMITIVE2V(name,cname,cmp,scmp)		\
    IDIO_DEFINE_PRIMITIVE2V (name, cname, (IDIO s1, IDIO s2, IDIO args)) \
    {									\
	IDIO_ASSERT (s1);						\
	IDIO_ASSERT (s2);						\
	IDIO_VERIFY_PARAM_TYPE (string, s1);				\
	IDIO_VERIFY_PARAM_TYPE (string, s2);				\
	IDIO_VERIFY_PARAM_TYPE (list, args);				\
									\
	args = idio_pair (s2, args);					\
									\
	char *C1 = idio_string_as_C (s1);				\
	size_t l1 = idio_string_len (s1);				\
									\
	while (idio_S_nil != args) {					\
	    s2 = IDIO_PAIR_H (args);					\
	    char *C2 = idio_string_as_C (s2);				\
	    size_t l2 = idio_string_len (s2);				\
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
		free (C1);						\
		free (C2);						\
		return idio_S_false;					\
	    }								\
									\
	    free (C1);							\
	    C1 = C2;							\
	    l1 = l2;							\
	    args = IDIO_PAIR_T (args);					\
	}								\
									\
	free (C1);							\
	return idio_S_true;						\
    }

#define IDIO_DEFINE_STRING_CS_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_STRING_PRIMITIVE2V (name, string_ci_ ## cname ## _p, cmp, strncmp)

#define IDIO_DEFINE_STRING_CI_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_STRING_PRIMITIVE2V (name, string_ ## cname ## _p, cmp, strncasecmp)

IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci<=?", le, <=)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci<?", lt, <)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci=?", eq, ==)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci>=?", ge, >=)
IDIO_DEFINE_STRING_CI_PRIMITIVE2V ("string-ci>?", gt, >)

IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string<=?", le, <=)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string<?", lt, <)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string=?", eq, ==)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string>=?", ge, >=)
IDIO_DEFINE_STRING_CS_PRIMITIVE2V ("string>?", gt, >)

/*
 * Tokenizing Strings
 *
 * strtok(3)/strtok_r(3)/strsep(3) all tokenize a string but modify
 * the original string at the same time (actually, they not only
 * insert NUL but therefore lose the identity of the character that
 * caused the tokenization).
 *
 * Here in Idio we want to take advantage of Idio substrings being
 * true substrings of a parent string.  The reason they exist, in
 * fact, is for string-splitting.
 *
 * Clearly we can't run around destroying the original string as
 * anyone referencing it will be "disappointed."  Hence, we need to
 * have a tokenizer that will return both start and end (preferably
 * length) of the token.  Clearly we can't return two things in C so
 * we'll have to pass in a modifyable length parameter.  Noting the
 * re-entrancy issues with strtok (and presumably strsep) we should
 * presumably pass in a modifyable "saved" parameter too.
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
 * NB We use char *lim rather than a maxlen as the mechanics of this
 * code (strspn()) shift the start without decrementing a maxlen.
 */

static char *idio_string_token (char *in, char *delim, int flags, char **saved, size_t *blen, char *lim)
{
    char prev_delim = '\0';

    if (NULL == in) {
	in = *saved;
	if (NULL == in) {
	    *blen = 0;
	    return NULL;
	}
	prev_delim = *(in - 1);
    }

    if (IDIO_STRING_TOKEN_INEXACT (flags)) {
	in += strspn (in, delim);
    } else if (0 &&
	       '\0' != prev_delim) {
	char *start = in;
	char *end = in + strspn (in, delim);
	for (; in < end; in++) {
	    if (prev_delim != *in) {
		*blen = start - in;
		return in;
	    }
	}
    }

    if (in >= lim) {
	*saved = NULL;
	*blen = 0;
	return NULL;
    }

    size_t maxlen = lim - in;

    char *start = strpbrk (in, delim);

    if ('\0' == *in) {
	*saved = NULL;
	*blen = 0;
	return NULL;
    }

    if (NULL == start) {
	*saved = NULL;
	*blen = strnlen (in, maxlen);
    } else {
	if (start < lim) {
	    *saved = start + 1;
	    *blen = start - in;
	} else {
	    *saved = NULL;
	    *blen = lim - in;
	}
    }

    return in;
}

IDIO idio_split_string (IDIO iin, IDIO idelim, int flags)
{
    IDIO_ASSERT (iin);
    IDIO_ASSERT (idelim);
    IDIO_TYPE_ASSERT (string, iin);
    IDIO_TYPE_ASSERT (string, idelim);

    IDIO r = idio_S_nil;

    char *in = idio_string_as_C (iin);
    char *in0 = in;
    char *delim = idio_string_as_C (idelim);

    char *saved;
    size_t blen = strlen (in);

    char *lim = in + blen;

    if (blen > 0) {
	for (; ; in = NULL) {
	    char *start = idio_string_token (in, delim, flags, &saved, &blen, lim);

	    if (NULL == start) {
		break;
	    }

	    if (IDIO_STRING_TOKEN_INEXACT (flags) &&
		0 == blen) {
		break;
	    }

	    IDIO subs = idio_substring_offset (iin, start - in0, blen);
	    r = idio_pair (subs, r);
	}
    } else {
	r = idio_pair (iin, r);
    }

    return idio_list_reverse (r);
}

IDIO_DEFINE_PRIMITIVE2_DS ("split-string", split_string, (IDIO in, IDIO delim), "in delim", "\
split string ``in`` using characters from ``delim``	\n\
into a list	 of strings				\n\
							\n\
:param in: string to split				\n\
:type in: string					\n\
:param delim: string containing delimiter characters	\n\
:type delim: string					\n\
:return: list (of strings)				\n\
")
{
    IDIO_ASSERT (in);
    IDIO_ASSERT (delim);
    IDIO_VERIFY_PARAM_TYPE (string, in);
    IDIO_VERIFY_PARAM_TYPE (string, delim);

    return idio_split_string (in, delim, IDIO_STRING_TOKEN_FLAG_NONE);
}

IDIO_DEFINE_PRIMITIVE2_DS ("split-string-exactly", split_string_exactly, (IDIO in, IDIO delim), "in delim", "\
split string ``in`` using characters from ``delim``	\n\
into a list	 of strings				\n\
							\n\
:param in: string to split				\n\
:type in: string					\n\
:param delim: string containing delimiter characters	\n\
:type delim: string					\n\
:return: list (of strings)				\n\
")
{
    IDIO_ASSERT (in);
    IDIO_ASSERT (delim);
    IDIO_VERIFY_PARAM_TYPE (string, in);
    IDIO_VERIFY_PARAM_TYPE (string, delim);

    return idio_split_string (in, delim, IDIO_STRING_TOKEN_FLAG_EXACT);
}

IDIO idio_join_string (IDIO delim, IDIO args)
{
    IDIO_ASSERT (delim);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (string, delim);
    IDIO_TYPE_ASSERT (list, args);

    if (idio_S_nil == args) {
	return idio_string_C_len ("", 0);
    }

    ptrdiff_t n = 2 * idio_list_length (args) - 1;
    char *copies[n];

    ptrdiff_t i;
    for (i = 0; idio_S_nil != args; i += 2) {
	IDIO_VERIFY_PARAM_TYPE (string, IDIO_PAIR_H (args));

	copies[i] = idio_string_as_C (IDIO_PAIR_H (args));
	copies[i+1] = idio_string_as_C (delim);

	args = IDIO_PAIR_T (args);
    }

    IDIO r = idio_string_C_array (n, copies);

    for (i = 0; i < n; i++) {
	free (copies[i]);
    }

    return r;
}


IDIO_DEFINE_PRIMITIVE2 ("join-string", join_string, (IDIO delim, IDIO args))
{
    IDIO_ASSERT (delim);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (string, delim);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_join_string (delim, args);
}

void idio_init_string ()
{
}

void idio_string_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (string_p);
    IDIO_ADD_PRIMITIVE (make_string);
    IDIO_ADD_PRIMITIVE (string2list);
    IDIO_ADD_PRIMITIVE (string2symbol);
    IDIO_ADD_PRIMITIVE (append_string);
    IDIO_ADD_PRIMITIVE (concatenate_string);
    IDIO_ADD_PRIMITIVE (copy_string);
    IDIO_ADD_PRIMITIVE (string_length);
    IDIO_ADD_PRIMITIVE (string_ref);
    IDIO_ADD_PRIMITIVE (string_set);
    IDIO_ADD_PRIMITIVE (string_fill);
    IDIO_ADD_PRIMITIVE (substring);

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
    IDIO_ADD_PRIMITIVE (join_string);
}

void idio_final_string ()
{
}
