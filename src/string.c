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

void idio_error_string_length (char *m, IDIO s, intptr_t i)
{
    idio_error_message ("%s: \"%s\" %zd", m, idio_as_string (s, 1), i);
}

void idio_error_substring_index (char *m, IDIO s, intptr_t ip0, intptr_t ipn)
{
    idio_error_message ("%s: \"%s\" %zd %zd", m, idio_as_string (s, 1), ip0, ipn);
}

IDIO idio_string_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);
    
    IDIO_FPRINTF (stderr, "idio_string_C: %10p = '%s'\n", so, s_C);

    size_t blen = strlen (s_C);

    IDIO_GC_ALLOC (so->u.string, sizeof (idio_string_t));
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

    IDIO_GC_ALLOC (so->u.string, sizeof (idio_string_t));
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

    idio_gc_stats_free (sizeof (idio_string_t) + IDIO_STRING_BLEN (so));

    free (IDIO_STRING_S (so));
    free (so->u.string);
}

IDIO idio_substring_offset (IDIO p, size_t offset, size_t blen)
{
    IDIO_ASSERT (p);
    IDIO_C_ASSERT (blen);
            
    IDIO so = idio_gc_get (IDIO_TYPE_SUBSTRING);

    IDIO_FPRINTF (stderr, "idio_substring_offset: %10p = '%.*s'\n", so, blen, IDIO_STRING_S (p) + offset);

    IDIO_GC_ALLOC (so->u.substring, sizeof (idio_substring_t));

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

    idio_gc_stats_free (sizeof (idio_substring_t));

    free (so->u.substring);
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_type_string: unexpected string type %d", idio_type (so));
	    idio_error_add_C (em);
	    break;
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_type_string: unexpected string type %d", idio_type (so));
	    idio_error_add_C (em);
	    break;
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_type_string: unexpected string type %d", idio_type (so));
	    idio_error_add_C (em);
	    break;
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

IDIO_DEFINE_PRIMITIVE1 ("string->list", string2list, (IDIO s))
{
    IDIO_ASSERT (s);

    IDIO_VERIFY_PARAM_TYPE (string, s);
    
    char *sC = idio_string_s (s);
    size_t sl = idio_string_blen (s);

    size_t si;
    
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

IDIO_DEFINE_PRIMITIVE2 ("string-fill", string_fill, (IDIO s, IDIO fill))
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
	Cs[i] = IDIO_CHARACTER_VAL (fill);
    }

    return s;
}

IDIO_DEFINE_PRIMITIVE1 ("string-length", string_length, (IDIO s))
{
    IDIO_ASSERT (s);

    IDIO_VERIFY_PARAM_TYPE (string, s);

    return IDIO_FIXNUM (idio_string_blen (s));
}

IDIO_DEFINE_PRIMITIVE2 ("string-ref", string_ref, (IDIO s, IDIO index))
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (index);

    IDIO_VERIFY_PARAM_TYPE (string, s);
    IDIO_VERIFY_PARAM_TYPE (fixnum, index);

    char *Cs = idio_string_s (s);
    size_t l = idio_string_blen (s);

    intptr_t i = IDIO_FIXNUM_VAL (index);

    if (i < 0 ||
	i > l) {
	idio_error_string_length ("string-ref: out of bounds", s, i);
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
    IDIO_VERIFY_PARAM_TYPE (fixnum, index);
    IDIO_VERIFY_PARAM_TYPE (character, c);

    if (idio_isa_substring (s)) {
	s = idio_string_copy (s);
    }

    char *Cs = idio_string_s (s);
    size_t l = idio_string_blen (s);

    intptr_t i = IDIO_FIXNUM_VAL (index);

    if (i < 0 ||
	i > l) {
	idio_error_string_length ("string-set: out of bounds", s, i);
	return idio_S_unspec;
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
    IDIO_VERIFY_PARAM_TYPE (fixnum, p0);
    IDIO_VERIFY_PARAM_TYPE (fixnum, pn);

    size_t l = idio_string_blen (s);

    intptr_t ip0 = IDIO_FIXNUM_VAL (p0);
    intptr_t ipn = IDIO_FIXNUM_VAL (pn);

    if (ip0 < 0 ||
	ip0 > l ||
	ipn < 0 ||
	ipn > l ||
	ipn < ip0) {
	idio_error_substring_index ("substring: out of bounds", s, ip0, ipn);
	return idio_S_unspec;
    }

    IDIO r = idio_S_unspec;
    intptr_t rl = ipn - ip0;

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
	    IDIO_C_ASSERT (0);
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
#define IDIO_DEFINE_STRING_PRIMITIVE2(name,cname,cmp,scmp)		\
    IDIO_DEFINE_PRIMITIVE2 (name, cname, (IDIO s1, IDIO s2))		\
    {									\
	IDIO_ASSERT (s1);						\
	IDIO_ASSERT (s2);						\
									\
	IDIO_VERIFY_PARAM_TYPE (string, s1);				\
	IDIO_VERIFY_PARAM_TYPE (string, s2);				\
									\
	char *C1 = idio_string_s (s1);					\
	size_t l1 = idio_string_blen (s1);				\
	char *C2 = idio_string_s (s2);					\
	size_t l2 = idio_string_blen (s2);				\
									\
	int l = l1;							\
	if (l2 < l1) {							\
	    l = l2;							\
	}								\
									\
	int sc = scmp (C1, C2, l);					\
									\
	/* C result */							\
	int cr = (sc cmp 0);						\
									\
	if (0 == sc) {							\
	    cr = (l1 cmp l2);						\
	}								\
									\
	IDIO r = idio_S_false;						\
									\
	if (cr) {							\
	    r = idio_S_true;						\
	}								\
									\
	return r;							\
    }

#define IDIO_DEFINE_STRING_CS_PRIMITIVE2(name,cname,cmp)		\
    IDIO_DEFINE_STRING_PRIMITIVE2 (name, string_ci_ ## cname ## _p, cmp, strncasecmp)

#define IDIO_DEFINE_STRING_CI_PRIMITIVE2(name,cname,cmp)		\
    IDIO_DEFINE_STRING_PRIMITIVE2 (name, string_ ## cname ## _p, cmp, strncmp)

IDIO_DEFINE_STRING_CI_PRIMITIVE2 ("string-ci<=?", le, <=)
IDIO_DEFINE_STRING_CI_PRIMITIVE2 ("string-ci<?", lt, <)
IDIO_DEFINE_STRING_CI_PRIMITIVE2 ("string-ci=?", eq, ==)
IDIO_DEFINE_STRING_CI_PRIMITIVE2 ("string-ci>=?", ge, >=)
IDIO_DEFINE_STRING_CI_PRIMITIVE2 ("string-ci>?", gt, >)

IDIO_DEFINE_STRING_CS_PRIMITIVE2 ("string<=?", le, <=)
IDIO_DEFINE_STRING_CS_PRIMITIVE2 ("string<?", lt, <)
IDIO_DEFINE_STRING_CS_PRIMITIVE2 ("string=?", eq, ==)
IDIO_DEFINE_STRING_CS_PRIMITIVE2 ("string>=?", ge, >=)
IDIO_DEFINE_STRING_CS_PRIMITIVE2 ("string>?", gt, >)

void idio_init_string ()
{
}

void idio_string_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (string_p);
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
