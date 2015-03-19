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

    IDIO_GC_ALLOC (p->u.pair, sizeof (idio_pair_t));

    IDIO_PAIR_GREY (p) = NULL;
    IDIO_PAIR_H (p) = h;
    IDIO_PAIR_T (p) = t;

    return p;
}

int idio_isa_pair (IDIO p)
{
    IDIO_ASSERT (p);

    return idio_isa (p, IDIO_TYPE_PAIR);
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

    idio_gc_stats_free (sizeof (idio_pair_t));

    free (p->u.pair);
}

IDIO idio_pair_head (IDIO p)
{
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (pair, p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }
    
    return IDIO_PAIR_H (p);
}

IDIO idio_pair_tail (IDIO p)
{
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (pair, p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }
    
    return IDIO_PAIR_T (p);
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
	*arg = idio_pair_head (*list);
	*list = idio_pair_tail (*list);
	IDIO_ASSERT (*arg);
    }
    
    va_end (ap);
}

IDIO idio_list_reverse (IDIO l)
{
    IDIO_ASSERT (l);
    
    if (idio_S_nil == l) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, l);

    IDIO r = idio_S_nil;
    
    while (idio_S_nil != l) {
	IDIO h = idio_pair_head (l);
	r = idio_pair (h, r);
	
	l = idio_pair_tail (l);
    }

    return r;
}

IDIO idio_list_to_array (IDIO l)
{
    IDIO_ASSERT (l);
    
    IDIO_TYPE_ASSERT (pair, l);
    
    size_t ll = idio_list_length (l);
    
    IDIO r = idio_array (ll);

    size_t li = 0;
    while (idio_S_nil != l) {
	idio_array_insert_index (r, idio_pair_head (l), li);
	l = idio_pair_tail (l);
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
	l = idio_pair_tail (l);
    }
    
    return len;
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
	IDIO h = idio_pair_head (l);
	IDIO t = idio_pair_tail (l);

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

IDIO idio_list_append (IDIO l1, IDIO l2)
{
    IDIO_ASSERT (l1);
    IDIO_ASSERT (l2);
    
    if (idio_S_nil == l1) {
	return l2;
    }

    IDIO_TYPE_ASSERT (pair, l1);

    IDIO r = idio_pair (idio_pair_head (l1),
			idio_list_append (idio_pair_tail (l1),
					  l2));
    
    return r;
}

