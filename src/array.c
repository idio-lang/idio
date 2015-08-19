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

void idio_error_array_length (char *m, idio_ai_t i, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);
    
    idio_error_printf (loc, "%s: %zd", m, i);
}

static void idio_array_error_bounds (idio_ai_t index, idio_ai_t size, IDIO loc)
{
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    char em[BUFSIZ];

    sprintf (em, "array bounds error: %td > %td", index, size);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (em, sh);
    IDIO c = idio_struct_instance (idio_condition_rt_array_bounds_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil));
    idio_raise_condition (idio_S_true, c);
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
	    idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_insert_index"));
	}
    } else if (index >= IDIO_ARRAY_ASIZE (a)) {
	/*
	  positive indexes can't be too large...
	*/
	if (index < (IDIO_ARRAY_ASIZE (a) * 2)) {
	    idio_array_resize (a);
	} else {	
	    idio_array_error_bounds (index, IDIO_ARRAY_ASIZE (a), IDIO_C_LOCATION ("idio_array_insert_index"));
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
	    idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_get_index"));
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_get_index"));
    }

    return IDIO_ARRAY_AE (a, index);
}

idio_ai_t idio_array_find_free_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);
    
    if (index < 0) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_find_free_index"));
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_find_free_index"));
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
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_find_eqp"));
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_find_eqp"));
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
	idio_error_C ("too many args", a, IDIO_C_LOCATION ("idio_array_bind"));
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
	    idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_delete_index"));
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_LOCATION ("idio_array_delete_index"));
    }

    IDIO_ARRAY_AE (a, index) = idio_S_nil;

    return 1;
}

IDIO_DEFINE_PRIMITIVE1 ("array?", array_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_array (o)) {
	r = idio_S_true;
    }
    
    return r;
}

IDIO_DEFINE_PRIMITIVE1V ("make-array", make_array, (IDIO size, IDIO args))
{
    IDIO_ASSERT (size);
    IDIO_ASSERT (args);

    ptrdiff_t vlen = -1;
    
    if (idio_isa_fixnum (size)) {
	vlen = IDIO_FIXNUM_VAL (size);
    } else if (idio_isa_bignum (size)) {
	if (IDIO_BIGNUM_INTEGER_P (size)) {
	    vlen = idio_bignum_ptrdiff_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		idio_error_param_type ("number", size, IDIO_C_LOCATION ("make-array"));
	    } else {
		vlen = idio_bignum_ptrdiff_value (size_i);
	    }
	}
    } else {
	idio_error_param_type ("number", size, IDIO_C_LOCATION ("make-array"));
    }

    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO dv = idio_S_false; /* S9fES -- Scheme specs say unspecified */

    if (idio_S_nil != args) {
	dv = IDIO_PAIR_H (args);
    }

    if (vlen < 0) {
	idio_error_printf (IDIO_C_LOCATION ("make-array"), "invalid length: %zd", vlen);
    }
    
    IDIO a = idio_array (vlen);
    
    size_t i;
    for (i = 0; i < vlen; i++) {
	idio_array_insert_index (a, dv, i);
    }

    return a;
}

IDIO_DEFINE_PRIMITIVE2 ("array-fill!", array_fill, (IDIO a, IDIO fill))
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

IDIO_DEFINE_PRIMITIVE1 ("array-length", array_length, (IDIO a))
{
    IDIO_ASSERT (a);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    return idio_fixnum (idio_array_size (a));
}

IDIO_DEFINE_PRIMITIVE2 ("array-ref", array_ref, (IDIO a, IDIO index))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    ptrdiff_t i = -1;
    
    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("number", index, IDIO_C_LOCATION ("array-ref"));
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("number", index, IDIO_C_LOCATION ("array-ref"));
    }

    idio_ai_t al = idio_array_size (a);

    if (i < 0) {
	i += al;
	if (i < 0) {
	    i -= al;
	    idio_error_array_length ("out of bounds", i, IDIO_C_LOCATION ("array-ref"));
	    return idio_S_unspec;
	}
    } else if (i >= al) {
	idio_error_array_length ("out of bounds", i, IDIO_C_LOCATION ("array-ref"));
	return idio_S_unspec;
    }

    return idio_array_get_index (a, i);
}

IDIO_DEFINE_PRIMITIVE3 ("array-set!", array_set, (IDIO a, IDIO index, IDIO v))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    ptrdiff_t i = -1;
    
    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("number", index, IDIO_C_LOCATION ("array-set!"));
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("number", index, IDIO_C_LOCATION ("array-set!"));
    }

    idio_ai_t al = idio_array_size (a);

    if (i < 0) {
	i += al;
	if (i < 0) {
	    i -= al;
	    idio_error_array_length ("out of bounds", i, IDIO_C_LOCATION ("array-set!"));
	    return idio_S_unspec;
	}
    } else if (i >= al) {
	idio_error_array_length ("out of bounds", i, IDIO_C_LOCATION ("array-set!"));
	return idio_S_unspec;
    }

    idio_array_insert_index (a, v, i);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2 ("array-push!", array_push, (IDIO a, IDIO v))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    idio_array_push (a, v);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("array-pop!", array_pop, (IDIO a))
{
    IDIO_ASSERT (a);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    IDIO v = idio_array_pop (a);

    return v;
}

IDIO_DEFINE_PRIMITIVE2 ("array-unshift!", array_unshift, (IDIO a, IDIO v))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    idio_array_unshift (a, v);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("array-shift!", array_shift, (IDIO a))
{
    IDIO_ASSERT (a);

    IDIO_VERIFY_PARAM_TYPE (array, a);

    IDIO v = idio_array_shift (a);

    return v;
}

IDIO_DEFINE_PRIMITIVE1 ("array->list", array2list, (IDIO a))
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
    IDIO_ADD_PRIMITIVE (array_p);
    IDIO_ADD_PRIMITIVE (make_array);
    IDIO_ADD_PRIMITIVE (array_fill);
    IDIO_ADD_PRIMITIVE (array_length);
    IDIO_ADD_PRIMITIVE (array_ref);
    IDIO_ADD_PRIMITIVE (array_set);
    IDIO_ADD_PRIMITIVE (array_push);
    IDIO_ADD_PRIMITIVE (array_pop);
    IDIO_ADD_PRIMITIVE (array_unshift);
    IDIO_ADD_PRIMITIVE (array_shift);
    IDIO_ADD_PRIMITIVE (array2list);
}

void idio_final_array ()
{
}

