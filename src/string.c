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
 * string.c
 * 
 */

#include "idio.h"

void idio_string_error_length (char *m, IDIO s, ptrdiff_t i, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (s);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (string, loc);

    char em[BUFSIZ];
    sprintf (em, "%s: %td", m, i);
    idio_error_printf (loc, em, s);
}

void idio_substring_error_index (char *m, IDIO s, ptrdiff_t ip0, ptrdiff_t ipn, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (s);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (string, loc);

    char em[BUFSIZ];
    sprintf (em, "%s: %td %td", m, ip0, ipn);
    idio_error_printf (loc, em, s);
}

IDIO idio_string_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);
    
    IDIO_FPRINTF (stderr, "idio_string_C: %10p = '%s'\n", so, s_C);

    size_t blen = strlen (s_C);

    IDIO_GC_ALLOC (IDIO_STRING_S (so), blen + 1);
    
    memcpy (IDIO_STRING_S (so), s_C, blen);
    IDIO_STRING_S (so)[blen] = '\0';
    IDIO_STRING_BLEN (so) = blen;

    return so;
}

IDIO idio_string_C_len (const char *s_C, size_t blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);
    
    IDIO_FPRINTF (stderr, "idio_string_C: %10p = '%s'\n", so, s_C);

    IDIO_GC_ALLOC (IDIO_STRING_S (so), blen + 1);
    
    memcpy (IDIO_STRING_S (so), s_C, blen);
    IDIO_STRING_S (so)[blen] = '\0';
    IDIO_STRING_BLEN (so) = blen;

    return so;
}

IDIO idio_string_C_array (size_t ns, char *a_C[])
{
    IDIO_C_ASSERT (a_C);

    IDIO so;

    so = idio_gc_get (IDIO_TYPE_STRING);
    
    size_t blen = 0;
    size_t ai;

    for (ai = 0; ai < ns; ai++) {
	blen += strlen (a_C[ai]);
    }

    IDIO_GC_ALLOC (IDIO_STRING_S (so), blen + 1);
    
    size_t ao = 0;
    for (ai = 0; ai < ns; ai++) {
	size_t sl = strlen (a_C[ai]);
	memcpy (IDIO_STRING_S (so) + ao, a_C[ai], sl);
	ao += sl;
    }
    
    IDIO_STRING_S (so)[blen] = '\0';
    IDIO_STRING_BLEN (so) = blen;

    return so;
}

IDIO idio_string_copy (IDIO string)
{
    IDIO_ASSERT (string);

    IDIO_TYPE_ASSERT (string, string);

    IDIO copy;

    char *s = idio_string_s (string);

    switch (idio_type (string)) {
    case IDIO_TYPE_STRING:
	{
	    copy = idio_string_C (s);
	    break;
	}
    case IDIO_TYPE_SUBSTRING:
	{
	    copy = idio_substring_offset (IDIO_SUBSTRING_PARENT (string),
					  IDIO_SUBSTRING_S (string) - IDIO_STRING_S (IDIO_SUBSTRING_PARENT (string)),
					  IDIO_SUBSTRING_BLEN (string));
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

IDIO idio_substring_offset (IDIO p, size_t offset, size_t blen)
{
    IDIO_ASSERT (p);
    IDIO_C_ASSERT (blen);
            
    IDIO so = idio_gc_get (IDIO_TYPE_SUBSTRING);

    IDIO_FPRINTF (stderr, "idio_substring_offset: %10p = '%.*s'\n", so, blen, IDIO_STRING_S (p) + offset);

    IDIO_FPRINTF (stderr, "idio_substring_offset: %d@%d in '%.*s' -> '%.*s'\n", blen, offset, IDIO_STRING_BLEN (p), IDIO_STRING_S (p), blen, IDIO_STRING_S (p) + offset);

    IDIO_SUBSTRING_BLEN (so) = blen;
    IDIO_SUBSTRING_S (so) = IDIO_STRING_S (p) + offset;
    IDIO_SUBSTRING_PARENT (so) = p;

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
    
    return strncmp (IDIO_STRING_S (so), s_C, blen);
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
	blen = IDIO_SUBSTRING_BLEN (so);
	break;
    default:
	{
	    idio_error_C ("unexpected string type", so, IDIO_C_LOCATION ("idio_string_blen"));
	}
    }

    return blen;
}

char *idio_string_s (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    char *s = NULL;

    switch (idio_type (so)) {
    case IDIO_TYPE_STRING:
	s = IDIO_STRING_S (so);
	break;
    case IDIO_TYPE_SUBSTRING:
	s = IDIO_SUBSTRING_S (so);
	break;
    default:
	{
	    idio_error_C ("unexpected string type", so, IDIO_C_LOCATION ("idio_string_s"));
	}
    }

    return s;
}

/*
 * caller must free(3) this string
 */
char *idio_string_as_C (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    char *s = NULL;

    switch (idio_type (so)) {
    case IDIO_TYPE_STRING:
	s = IDIO_STRING_S (so);
	break;
    case IDIO_TYPE_SUBSTRING:
	s = IDIO_SUBSTRING_S (so);
	break;
    default:
	{
	    idio_error_C ("unexpected string type", so, IDIO_C_LOCATION ("idio_string_as_C"));
	}
    }

    size_t blen = idio_string_blen (so);
    char *s_C = idio_alloc (blen + 1);

    memcpy (s_C, s, blen);
    s_C[blen] = '\0';

    return s_C;
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

    ptrdiff_t blen = -1;
    
    if (idio_isa_fixnum (size)) {
	blen = IDIO_FIXNUM_VAL (size);
    } else if (idio_isa_bignum (size)) {
	if (IDIO_BIGNUM_INTEGER_P (size)) {
	    blen = idio_bignum_ptrdiff_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		idio_error_param_type ("number", size, IDIO_C_LOCATION ("make-string"));
	    } else {
		blen = idio_bignum_ptrdiff_value (size_i);
	    }
	}
    } else {
	idio_error_param_type ("number", size, IDIO_C_LOCATION ("make-string"));
    }

    IDIO_VERIFY_PARAM_TYPE (list, args);

    char fillc = ' ';
    if (idio_S_nil != args) {
	IDIO fill = IDIO_PAIR_H (args);
	IDIO_VERIFY_PARAM_TYPE (character, fill);
	if (IDIO_CHARACTER_VAL (fill) > INT8_MAX ||
	    IDIO_CHARACTER_VAL (fill) < INT8_MIN) {
	    idio_error_C ("fill character too large", fill, IDIO_C_LOCATION ("make-string"));
	}

	fillc = IDIO_CHARACTER_VAL (fill);
    }

    if (blen < 0) {
	idio_error_printf (IDIO_C_LOCATION ("make-string"), "invalid length: %zd", blen);
    }
    
    char *sC = idio_alloc (blen + 1);

    size_t i;
    for (i = 0; i < blen; i++) {
	sC[i] = fillc;
    }
    sC[i] = '\0';

    IDIO s = idio_string_C (sC);

    free (sC);
    
    return s;
}

IDIO_DEFINE_PRIMITIVE1 ("string->list", string2list, (IDIO s))
{
    IDIO_ASSERT (s);

    IDIO_VERIFY_PARAM_TYPE (string, s);
    
    char *sC = idio_string_s (s);
    size_t sl = idio_string_blen (s);

    ptrdiff_t si;
    
    IDIO r = idio_S_nil;

    for (si = sl - 1; si >=0; si--) {
	r = idio_pair (IDIO_CHARACTER (sC[si]), r);
    }

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

IDIO_DEFINE_PRIMITIVE1V ("string-append", string_append, (IDIO s, IDIO args))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (args);

    IDIO_VERIFY_PARAM_TYPE (string, s);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    int n = 1 + idio_list_length (args);
    char *copies[n];

    int i = 0;
    copies[i] = idio_string_as_C (s);

    for (i++; idio_S_nil != args; i++) {
	IDIO_VERIFY_PARAM_TYPE (string, IDIO_PAIR_H (args));
	
	copies[i] = idio_string_as_C (IDIO_PAIR_H (args));

	args = IDIO_PAIR_T (args);
    }
    
    IDIO r = idio_string_C_array (n, copies);

    for (i = 0; i < n; i++) {
	free (copies[i]);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("string-copy", string_copy, (IDIO s))
{
    IDIO_ASSERT (s);

    IDIO_VERIFY_PARAM_TYPE (string, s);

    return idio_string_copy (s);
}

IDIO_DEFINE_PRIMITIVE2 ("string-fill!", string_fill, (IDIO s, IDIO fill))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (fill);

    IDIO_VERIFY_PARAM_TYPE (string, s);
    IDIO_VERIFY_PARAM_TYPE (character, fill);

    if (idio_isa_substring (s)) {
	s = idio_string_copy (s);
    }

    char *Cs = idio_string_s (s);
    size_t l = idio_string_blen (s);

    size_t i;
    for (i = 0; i < l; i++) {
	if (IDIO_CHARACTER_VAL (fill) > INT8_MAX ||
	    IDIO_CHARACTER_VAL (fill) < INT8_MIN) {
	    idio_error_C ("fill character too large", fill, IDIO_C_LOCATION ("string-fill!"));
	}

	Cs[i] = IDIO_CHARACTER_VAL (fill);
    }

    return s;
}

IDIO_DEFINE_PRIMITIVE1 ("string-length", string_length, (IDIO s))
{
    IDIO_ASSERT (s);

    IDIO_VERIFY_PARAM_TYPE (string, s);

    return idio_fixnum (idio_string_blen (s));
}

IDIO_DEFINE_PRIMITIVE2 ("string-ref", string_ref, (IDIO s, IDIO index))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);

    IDIO_VERIFY_PARAM_TYPE (string, s);

    ptrdiff_t i = -1;
    
    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("number", index, IDIO_C_LOCATION ("string-ref"));
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("number", index, IDIO_C_LOCATION ("string-ref"));
    }


    char *Cs = idio_string_s (s);
    size_t l = idio_string_blen (s);

    if (i < 0 ||
	i > l) {
	idio_string_error_length ("out of bounds", s, i, IDIO_C_LOCATION ("string-ref"));
	return idio_S_unspec;
    }

    return IDIO_CHARACTER (Cs[i]);
}

IDIO_DEFINE_PRIMITIVE3 ("string-set!", string_set, (IDIO s, IDIO index, IDIO c))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (string, s);
    IDIO_VERIFY_PARAM_TYPE (character, c);

    ptrdiff_t i = -1;
    
    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("number", index, IDIO_C_LOCATION ("string-set!"));
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("number", index, IDIO_C_LOCATION ("string-set!"));
    }

    if (idio_isa_substring (s)) {
	s = idio_string_copy (s);
    }

    char *Cs = idio_string_s (s);
    size_t l = idio_string_blen (s);

    if (i < 0 ||
	i > l) {
	idio_string_error_length ("out of bounds", s, i, IDIO_C_LOCATION ("string-set!"));
	return idio_S_unspec;
    }

    if (IDIO_CHARACTER_VAL (c) > INT8_MAX ||
	IDIO_CHARACTER_VAL (c) < INT8_MIN) {
	idio_error_C ("character too large for C string", c, IDIO_C_LOCATION ("string-set!"));
    }

    Cs[i] = IDIO_CHARACTER_VAL (c);

    return s;
}

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
		idio_error_param_type ("number", p0, IDIO_C_LOCATION ("substring"));
	    } else {
		ip0 = idio_bignum_ptrdiff_value (p0_i);
	    }
	}
    } else {
	idio_error_param_type ("number", p0, IDIO_C_LOCATION ("substring"));
    }

    if (idio_isa_fixnum (pn)) {
	ipn = IDIO_FIXNUM_VAL (pn);
    } else if (idio_isa_bignum (pn)) {
	if (IDIO_BIGNUM_INTEGER_P (pn)) {
	    ipn = idio_bignum_ptrdiff_value (pn);
	} else {
	    IDIO pn_i = idio_bignum_real_to_integer (pn);
	    if (idio_S_nil == pn_i) {
		idio_error_param_type ("number", pn, IDIO_C_LOCATION ("substring"));
	    } else {
		ipn = idio_bignum_ptrdiff_value (pn_i);
	    }
	}
    } else {
	idio_error_param_type ("number", pn, IDIO_C_LOCATION ("substring"));
    }

    size_t l = idio_string_blen (s);

    if (ip0 < 0 ||
	ip0 > l ||
	ipn < 0 ||
	ipn > l ||
	ipn < ip0) {
	idio_substring_error_index ("out of bounds", s, ip0, ipn, IDIO_C_LOCATION ("substring"));
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
	    idio_error_C ("unexpected string type", s, IDIO_C_LOCATION ("substring"));
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
									\
	IDIO_VERIFY_PARAM_TYPE (string, s1);				\
	IDIO_VERIFY_PARAM_TYPE (string, s2);				\
	IDIO_VERIFY_PARAM_TYPE (list, args);				\
									\
	args = idio_pair (s2, args);					\
									\
	char *C1 = idio_string_s (s1);					\
	size_t l1 = idio_string_blen (s1);				\
									\
	while (idio_S_nil != args) {					\
	    s2 = IDIO_PAIR_H (args);					\
	    char *C2 = idio_string_s (s2);				\
	    size_t l2 = idio_string_blen (s2);				\
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
		return idio_S_false;					\
	    }								\
									\
	    C1 = C2;							\
	    l1 = l2;							\
	    args = IDIO_PAIR_T (args);					\
	}								\
									\
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

void idio_init_string ()
{
}

void idio_string_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (string_p);
    IDIO_ADD_PRIMITIVE (make_string);
    IDIO_ADD_PRIMITIVE (string2list);
    IDIO_ADD_PRIMITIVE (string2symbol);
    IDIO_ADD_PRIMITIVE (string_append);
    IDIO_ADD_PRIMITIVE (string_copy);
    IDIO_ADD_PRIMITIVE (string_fill);
    IDIO_ADD_PRIMITIVE (string_length);
    IDIO_ADD_PRIMITIVE (string_ref);
    IDIO_ADD_PRIMITIVE (string_set);
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
}

void idio_final_string ()
{
}
