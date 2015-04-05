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
 * array.c
 * 
 */

#include "idio.h"

void idio_error_vector_length (char *m, idio_ai_t i)
{
    idio_error_message ("%s: %zd", m, i);
}

void idio_assign_array (IDIO a, idio_ai_t asize)
{
    IDIO_ASSERT (a);
    IDIO_C_ASSERT (asize);

    IDIO_TYPE_ASSERT (array, a);

    IDIO_FPRINTF (stderr, "idio_assign_array: %10p = [%d]\n", a, asize);

    IDIO_GC_ALLOC (a->u.array, sizeof (idio_array_t));
    IDIO_GC_ALLOC (a->u.array->ae, asize * sizeof (idio_t));
    
    IDIO_ARRAY_GREY (a) = NULL;
    IDIO_ARRAY_ASIZE (a) = asize;
    IDIO_ARRAY_USIZE (a) = 0;

    idio_ai_t i;
    for (i = 0; i < asize; i++) {
	IDIO_ARRAY_AE (a, i) = idio_S_nil;
    }
}

IDIO idio_array (idio_ai_t size)
{

    if (0 == size) {
	size = 1;
    }
    
    IDIO a = idio_gc_get (IDIO_TYPE_ARRAY);
    idio_assign_array (a, size);
    return a;
}

int idio_isa_array (IDIO a)
{
    IDIO_ASSERT (a);

    return idio_isa (a, IDIO_TYPE_ARRAY);
}

void idio_free_array (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    idio_gc_stats_free (sizeof (idio_array_t) + IDIO_ARRAY_ASIZE (a) * sizeof (idio_t));

    free (a->u.array->ae);
    free (a->u.array);
}

void idio_array_resize (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    idio_array_t *oarray = a->u.array;
    idio_ai_t oasize = IDIO_ARRAY_ASIZE (a);
    idio_ai_t ousize = IDIO_ARRAY_USIZE (a);
    idio_ai_t nsize = oasize << 1;

    IDIO_FPRINTF (stderr, "idio_array_resize: %10p = {%d} -> {%d}\n", a, oasize, nsize);
    idio_assign_array (a, nsize);

    idio_ai_t i;
    for (i = 0 ; i < oarray->usize; i++) {
	idio_array_insert_index (a, oarray->ae[i], i);
    }

    /* pad with idio_S_nil */
    for (;i < IDIO_ARRAY_ASIZE (a); i++) {
	idio_array_insert_index (a, idio_S_nil, i);
    }

    /* all the inserts above will have mucked usize up */
    IDIO_ARRAY_USIZE (a) = ousize;
    
    idio_gc_stats_free (sizeof (idio_array_t) + oasize * sizeof (idio_t));
    idio_gc_stats_free (sizeof (idio_array_t) + oasize * sizeof (idio_t));

    free (oarray->ae);
    free (oarray);
}

idio_ai_t idio_array_size (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    return IDIO_ARRAY_USIZE (a);
}

void idio_array_insert_index (IDIO a, IDIO o, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	/*
	  negative indexes cannot be larger than the size of the
	  existing array
	*/
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    fprintf (stderr, "idio_array_insert_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	    IDIO_C_EXIT (1);
	}
    } else if (index >= IDIO_ARRAY_ASIZE (a)) {
	/*
	  positive indexes can't be too large...
	*/
	if (index < (IDIO_ARRAY_ASIZE (a) * 2)) {
	    idio_array_resize (a);
	} else {	
	    fprintf (stderr, "idio_array_insert_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_ASIZE (a));
	    IDIO_C_EXIT (1);
	}
    }

    IDIO_ARRAY_AE (a, index) = o;

    /* index is 0+, usize is 1+ */
    index++;
    if (index > IDIO_ARRAY_USIZE (a)) {
	IDIO_ARRAY_USIZE (a) = index;
    }

    IDIO_C_ASSERT (IDIO_ARRAY_USIZE (a) <= IDIO_ARRAY_ASIZE (a));
}

void idio_array_push (IDIO a, IDIO o)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (array, a);

    idio_array_insert_index (a, o, IDIO_ARRAY_USIZE (a));
}

IDIO idio_array_pop (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }

    IDIO e = idio_array_get_index (a, IDIO_ARRAY_USIZE (a) - 1);
    idio_array_delete_index (a, IDIO_ARRAY_USIZE (a) - 1);
    IDIO_ARRAY_USIZE (a)--;

    IDIO_ASSERT (e);
    IDIO_ASSERT_NOT_FREED (e);
    
    return e;
}

IDIO idio_array_shift (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }

    IDIO e = idio_array_get_index (a, 0);
    idio_ai_t i;
    for (i = 0 ; i < IDIO_ARRAY_USIZE (a) - 1; i++) {
	IDIO e = idio_array_get_index (a, i + 1);
	IDIO_ASSERT (e);
	
	idio_array_insert_index (a, e, i);
    }

    idio_array_pop (a);

    return e;
}

void idio_array_unshift (IDIO a, IDIO o)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (array, a);

    idio_ai_t i;
    if (IDIO_ARRAY_USIZE (a) > 0) {
	for (i = IDIO_ARRAY_USIZE (a); i > 0; i--) {
	    IDIO e = idio_array_get_index (a, i - 1);
	    IDIO_ASSERT (e);
	    
	    idio_array_insert_index (a, e, i);
	}
    }
    idio_array_insert_index (a, o, 0);

    IDIO_C_ASSERT (IDIO_ARRAY_USIZE (a) <= IDIO_ARRAY_ASIZE (a));
}

IDIO idio_array_head (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }
    
    return idio_array_get_index (a, 0);
}

IDIO idio_array_top (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }
    
    return idio_array_get_index (a, IDIO_ARRAY_USIZE (a) - 1);
}

IDIO idio_array_get_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    fprintf (stderr, "idio_array_get_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	    IDIO_C_EXIT (1);
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_get_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    return IDIO_ARRAY_AE (a, index);
}

idio_ai_t idio_array_find_free_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);
    
    if (index < 0) {
	fprintf (stderr, "idio_array_find_free_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_find_free_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    for (; index < IDIO_ARRAY_USIZE (a); index++) {
	if (idio_S_nil == IDIO_ARRAY_AE (a, index)) {
	    return index;
	}
    }

    return -1;
}

idio_ai_t idio_array_find_eqp (IDIO a, IDIO e, idio_ai_t index)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);
    
    if (index < 0) {
	fprintf (stderr, "idio_array_find_eqp: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_find_eqp: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    for (; index < IDIO_ARRAY_USIZE (a); index++) {
	if (idio_eqp (IDIO_ARRAY_AE (a, index), e)) {
	    return index;
	}
    }

    return -1;
}

void idio_array_bind (IDIO a, idio_ai_t nargs, ...)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < nargs) {
	char *os = idio_display_string (a);
	fprintf (stderr, "idio_array_bind: o=%s\n", os);
	free (os);
	idio_expr_dump (a);
	IDIO_C_ASSERT (0);
    }

    va_list ap;
    va_start (ap, nargs);

    idio_ai_t i;
    for (i = 0; i < nargs; i++) {
	IDIO *arg = va_arg (ap, IDIO *);
	*arg = idio_array_get_index (a, i);
	IDIO_ASSERT (*arg);
    }
    
    va_end (ap);
}

IDIO idio_array_copy (IDIO a, idio_ai_t extra)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    idio_ai_t osz = IDIO_ARRAY_USIZE (a);
    idio_ai_t nsz = osz + extra;
    IDIO na = idio_array (nsz);

    idio_ai_t i;
    for (i = 0; i < osz; i++) {
	idio_array_insert_index (na,
				 idio_array_get_index (a, i),
				 i);
    }

    return na;
}

IDIO idio_array_to_list (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    idio_ai_t al = IDIO_ARRAY_USIZE (a);
    idio_ai_t ai;
    
    IDIO r = idio_S_nil;

    for (ai = al -1; ai >= 0; ai--) {
	r = idio_pair (idio_array_get_index (a, ai),
		       r);
    }

    return r;
}

int idio_array_delete_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    fprintf (stderr, "idio_array_delete_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	    IDIO_C_EXIT (1);
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	fprintf (stderr, "idio_array_delete_index: index %3zd is OOB (%zd)\n", index, IDIO_ARRAY_USIZE (a));
	IDIO_C_EXIT (1);
    }

    IDIO_ARRAY_AE (a, index) = idio_S_nil;

    return 1;
}

IDIO_DEFINE_PRIMITIVE1 ("vector?", vector_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_array (o)) {
	r = idio_S_true;
    }
    
    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("vector-fill!", vector_fill, (IDIO a, IDIO fill))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (fill);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    idio_ai_t al = idio_array_size (a);
    idio_ai_t ai;

    for (ai = 0; ai < al ; ai++) {
	idio_array_insert_index (a, fill, ai);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("vector-length", vector_length, (IDIO a))
{
    IDIO_ASSERT (a);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    return IDIO_FIXNUM (idio_array_size (a));
}

IDIO_DEFINE_PRIMITIVE2 ("vector-ref", vector_ref, (IDIO a, IDIO index))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);

    IDIO_VERIFY_PARAM_TYPE (array, a);
    IDIO_VERIFY_PARAM_TYPE (fixnum, index);

    idio_ai_t al = idio_array_size (a);

    idio_ai_t i = IDIO_FIXNUM_VAL (index);

    if (i < 0) {
	i += al;
	if (i < 0) {
	    i -= al;
	    idio_error_vector_length ("vector-ref: out of bounds", i);
	    return idio_S_unspec;
	}
    } else if (i >= al) {
	idio_error_vector_length ("vector-ref: out of bounds", i);
	return idio_S_unspec;
    }

    return idio_array_get_index (a, i);
}

IDIO_DEFINE_PRIMITIVE3 ("vector-set!", vector_set, (IDIO a, IDIO index, IDIO v))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (array, a);
    IDIO_VERIFY_PARAM_TYPE (fixnum, index);

    idio_ai_t al = idio_array_size (a);

    idio_ai_t i = IDIO_FIXNUM_VAL (index);

    if (i < 0) {
	i += al;
	if (i < 0) {
	    i -= al;
	    idio_error_vector_length ("vector-set: out of bounds", i);
	    return idio_S_unspec;
	}
    } else if (i >= al) {
	idio_error_vector_length ("vector-set: out of bounds", i);
	return idio_S_unspec;
    }

    idio_array_insert_index (a, v, i);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("vector->list", vector2list, (IDIO a))
{
    IDIO_ASSERT (a);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    return idio_array_to_list (a);
}

void idio_init_array ()
{
}

void idio_array_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (vector_p);
    IDIO_ADD_PRIMITIVE (vector_fill);
    IDIO_ADD_PRIMITIVE (vector_length);
    IDIO_ADD_PRIMITIVE (vector_ref);
    IDIO_ADD_PRIMITIVE (vector_set);
    IDIO_ADD_PRIMITIVE (vector2list);
}

void idio_final_array ()
{
}

