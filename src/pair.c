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

IDIO idio_pair (IDIO f, IDIO h, IDIO t)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    IDIO_ASSERT (t);

    IDIO p = idio_get (f, IDIO_TYPE_PAIR);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_pair: %10p = (%10p %10p)\n", p, h, t);

    IDIO_ALLOC (f, p->u.pair, sizeof (idio_pair_t));

    IDIO_PAIR_GREY (p) = NULL;
    IDIO_PAIR_H (p) = h;
    IDIO_PAIR_T (p) = t;

    return p;
}

int idio_isa_pair (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);

    return idio_isa (f, p, IDIO_TYPE_PAIR);
}

void idio_free_pair (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);
    
    IDIO_C_ASSERT (idio_isa_pair (f, p));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_pair_t);

    free (p->u.pair);
}

IDIO idio_pair_head (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (pair, p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }
    
    return IDIO_PAIR_H (p);
}

IDIO idio_pair_tail (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (pair, p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }
    
    return IDIO_PAIR_T (p);
}

void idio_list_bind (IDIO f, IDIO *list, size_t nargs, ...)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (*list);

    IDIO_C_ASSERT (idio_isa_pair (f, *list));

    va_list ap;
    va_start (ap, nargs);

    size_t i;
    for (i = 0; i < nargs; i++) {
	IDIO *arg = va_arg (ap, IDIO *);
	*arg = idio_pair_head (f, *list);
	*list = idio_pair_tail (f, *list);
	IDIO_ASSERT (*arg);
    }
    
    va_end (ap);
}

IDIO idio_list_reverse (IDIO f, IDIO l)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (l);
    
    if (idio_S_nil == l) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, l);

    IDIO r = idio_S_nil;
    
    while (idio_S_nil != l) {
	IDIO h = idio_pair_head (f, l);
	r = idio_pair (f, h, r);
	
	l = idio_pair_tail (f, l);
    }

    return r;
}

IDIO idio_list_to_array (IDIO f, IDIO l)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (l);
    
    IDIO_TYPE_ASSERT (pair, l);
    
    size_t ll = idio_list_length (f, l);
    
    IDIO r = idio_array (f, ll);

    size_t li = 0;
    while (idio_S_nil != l) {
	idio_array_insert_index (f, r, idio_pair_head (f, l), li);
	l = idio_pair_tail (f, l);
	li++;
    }

    return r;
}

size_t idio_list_length (IDIO f, IDIO l)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (l);
    
    if (idio_S_nil == l) {
	return 0;
    }

    IDIO_TYPE_ASSERT (pair, l);

    size_t len = 0;
    
    while (idio_S_nil != l) {
	len++;
	l = idio_pair_tail (f, l);
    }
    
    return len;
}

IDIO idio_list_copy (IDIO f, IDIO l)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (l);
    
    if (idio_S_nil == l) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, l);

    IDIO r = idio_S_nil;
    
    while (idio_S_nil != l) {
	IDIO h = idio_pair_head (f, l);
	IDIO t = idio_pair_tail (f, l);

	if (idio_S_nil == h &&
	    idio_S_nil == t) {
	    /* (1 2 3 #nil) */
	    break;
	}

	r = idio_pair (f, h, r);

	l = t;
    }
    
    r = idio_list_reverse (f, r);

    return r;
}

IDIO idio_list_append (IDIO f, IDIO l1, IDIO l2)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (l1);
    IDIO_ASSERT (l2);
    
    if (idio_S_nil == l1) {
	return l2;
    }

    IDIO_TYPE_ASSERT (pair, l1);

    IDIO r = idio_pair (f,
			idio_pair_head (f, l1),
			idio_list_append (f,
					  idio_pair_tail (f, l1),
					  l2));
    
    return r;
}

