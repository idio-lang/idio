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
 * pair.c
 * 
 */

#include "idio.h"

IDIO idio_pair (IDIO h, IDIO t)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (t);

    IDIO p = idio_gc_get (IDIO_TYPE_PAIR);

    IDIO_FPRINTF (stderr, "idio_pair: %10p = (%10p %10p)\n", p, h, t);

    /* IDIO_GC_ALLOC (p->u.pair, sizeof (idio_pair_t)); */

    IDIO_PAIR_GREY (p) = NULL;
    IDIO_PAIR_H (p) = h;
    IDIO_PAIR_T (p) = t;

    return p;
}

IDIO_DEFINE_PRIMITIVE2 ("pair", pair, (IDIO h, IDIO t))
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (t);

    return idio_pair (h, t);
}

int idio_isa_pair (IDIO p)
{
    IDIO_ASSERT (p);

    return idio_isa (p, IDIO_TYPE_PAIR);
}

IDIO_DEFINE_PRIMITIVE1 ("pair?", pair_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_pair (o)) {
	r = idio_S_true;
    }
    
    return r;
}

int idio_isa_list (IDIO p)
{
    IDIO_ASSERT (p);

    return (idio_isa_pair (p) ||
	    idio_S_nil == p);
}

void idio_free_pair (IDIO p)
{
    IDIO_ASSERT (p);
    IDIO_TYPE_ASSERT (pair, p);

    /* idio_gc_stats_free (sizeof (idio_pair_t)); */

    /* free (p->u.pair); */
}

IDIO idio_pair_set_head (IDIO p, IDIO v)
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (pair, p);

    IDIO_PAIR_H (p) = v;
    return idio_S_unspec;
}

IDIO idio_pair_set_tail (IDIO p, IDIO v)
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (pair, p);

    IDIO_PAIR_T (p) = v;
    return idio_S_unspec;
}

IDIO idio_list_head (IDIO p)
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }
    
    IDIO_TYPE_ASSERT (pair, p);

    return IDIO_PAIR_H (p);
}

IDIO_DEFINE_PRIMITIVE1 ("ph", pair_head, (IDIO p))
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_VERIFY_PARAM_TYPE (pair, p);

    return idio_list_head (p);
}

IDIO_DEFINE_PRIMITIVE2 ("set-ph!", set_pair_head, (IDIO p, IDIO v))
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (pair, p);

    IDIO_PAIR_H (p) = v;

    return idio_S_unspec;
}

IDIO idio_list_tail (IDIO p)
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, p);

    return IDIO_PAIR_T (p);
}

IDIO_DEFINE_PRIMITIVE1 ("pt", pair_tail, (IDIO p))
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_VERIFY_PARAM_TYPE (pair, p);

    return idio_list_tail (p);
}

IDIO_DEFINE_PRIMITIVE2 ("set-pt!", set_pair_tail, (IDIO p, IDIO v))
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (pair, p);

    IDIO_PAIR_T (p) = v;

    return idio_S_unspec;
}

void idio_list_bind (IDIO *list, size_t nargs, ...)
{
    IDIO_ASSERT (*list);

    IDIO_TYPE_ASSERT (pair, *list);

    va_list ap;
    va_start (ap, nargs);

    size_t i;
    for (i = 0; i < nargs; i++) {
	IDIO *arg = va_arg (ap, IDIO *);
	*arg = idio_list_head (*list);
	*list = idio_list_tail (*list);
	IDIO_ASSERT (*arg);
    }
    
    va_end (ap);
}

IDIO idio_improper_list_reverse (IDIO l, IDIO last)
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (last);
    
    if (idio_S_nil == l) {
	/*
	 * An empty improper list, ie. "( . last)" is invalid and we
	 * shouldn't have gotten here, otherwise we're here because
	 * we're reversing an ordinary list in which case the result
	 * is nil
	 */
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, l);

    IDIO r = last;
    
    while (idio_S_nil != l) {
	IDIO h = idio_list_head (l);
	r = idio_pair (h, r);
	
	l = idio_list_tail (l);
    }

    return r;
}

IDIO idio_list_reverse (IDIO l)
{
    IDIO_ASSERT (l);
    
    return idio_improper_list_reverse (l, idio_S_nil);
}

IDIO_DEFINE_PRIMITIVE1 ("reverse", list_reverse, (IDIO l))
{
    IDIO_ASSERT (l);
    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_reverse (l);
}

IDIO idio_list_to_array (IDIO l)
{
    IDIO_ASSERT (l);
    
    IDIO_TYPE_ASSERT (list, l);
    
    size_t ll = idio_list_length (l);
    
    IDIO r = idio_array (ll);

    size_t li = 0;
    while (idio_S_nil != l) {
	idio_array_insert_index (r, idio_list_head (l), li);
	l = idio_list_tail (l);
	li++;
    }

    return r;
}

size_t idio_list_length (IDIO l)
{
    IDIO_ASSERT (l);
    
    if (idio_S_nil == l) {
	return 0;
    }

    IDIO_TYPE_ASSERT (pair, l);

    size_t len = 0;
    
    while (idio_S_nil != l) {
	len++;
	l = idio_list_tail (l);
    }
    
    return len;
}

IDIO_DEFINE_PRIMITIVE1 ("length", list_length, (IDIO o))
{
    IDIO_ASSERT (o);
    IDIO_VERIFY_PARAM_TYPE (list, o);

    size_t len = idio_list_length (o);

    return idio_integer (len);
}

IDIO idio_list_copy (IDIO l)
{
    IDIO_ASSERT (l);
    
    if (idio_S_nil == l) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, l);

    IDIO r = idio_S_nil;
    
    while (idio_S_nil != l) {
	IDIO h = idio_list_head (l);
	IDIO t = idio_list_tail (l);

	if (idio_S_nil == h &&
	    idio_S_nil == t) {
	    /* (1 2 3 #nil) */
	    break;
	}

	r = idio_pair (h, r);

	l = t;
    }
    
    r = idio_list_reverse (r);

    return r;
}

IDIO idio_list_append2 (IDIO l1, IDIO l2)
{
    IDIO_ASSERT (l1);
    IDIO_ASSERT (l2);
    
    if (idio_S_nil == l1) {
	return l2;
    }

    IDIO_TYPE_ASSERT (pair, l1);

    IDIO r = idio_S_nil;
    IDIO p = idio_S_nil;

    for (;;l1 = IDIO_PAIR_T (l1)) {
	if (idio_S_nil == l1) {
	    break;
	}
	if (! idio_isa_pair (l1)) {
	    idio_error_C ("not a list:", l1, IDIO_C_LOCATION ("idio_list_append2"));
	}

	IDIO t = idio_pair (IDIO_PAIR_H (l1), idio_S_nil);

	if (idio_S_nil == r) {
	    r = t;
	} else {
	    IDIO_PAIR_T (p) = t;
	}
	p = t;
    }

    IDIO_PAIR_T (p) = l2;
    
    return r;
}

IDIO_DEFINE_PRIMITIVE0V ("list", list, (IDIO args))
{
    IDIO_ASSERT (args);

    return args;
}

IDIO_DEFINE_PRIMITIVE2 ("append", append, (IDIO a, IDIO b))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    if (idio_S_nil == a) {
	return b;
    }

    IDIO_VERIFY_PARAM_TYPE (list, a);

    if (idio_S_nil == b) {
	return a;
    }

    return idio_list_append2 (a, b);
}

IDIO idio_list_list2string (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    size_t ll = idio_list_length (l);
    char s[ll+1];
    size_t i = 0;

    while (idio_S_nil != l) {
	IDIO h = IDIO_PAIR_H (l);

	if (idio_isa_character (h)) {
	    s[i] = IDIO_CHARACTER_VAL (h);
	} else {
	    idio_error_param_type ("character", h, IDIO_C_LOCATION ("idio_list_list2string"));
	    return idio_S_unspec;
	}

	l = IDIO_PAIR_T (l);
	i++;
    }

    s[i] = '\0';
    
    return idio_string_C (s);
}

IDIO_DEFINE_PRIMITIVE1 ("list->string", list2string, (IDIO l))
{
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_list2string (l);
}

IDIO_DEFINE_PRIMITIVE1 ("list->array", list2array, (IDIO l))
{
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_to_array (l);
}

void idio_init_pair ()
{
}

void idio_pair_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (pair_p);
    IDIO_ADD_PRIMITIVE (pair);
    IDIO_ADD_PRIMITIVE (pair_head);
    IDIO_ADD_PRIMITIVE (pair_tail);
    IDIO_ADD_PRIMITIVE (set_pair_head);
    IDIO_ADD_PRIMITIVE (set_pair_tail);

    IDIO_ADD_PRIMITIVE (list_reverse);
    IDIO_ADD_PRIMITIVE (list_length);
    IDIO_ADD_PRIMITIVE (list);
    IDIO_ADD_PRIMITIVE (append);
    IDIO_ADD_PRIMITIVE (list2string);
    IDIO_ADD_PRIMITIVE (list2array);
}

void idio_final_pair ()
{
}
