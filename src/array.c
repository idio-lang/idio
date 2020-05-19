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
 * array.c
 *
 */

/**
 * DOC: Idio ``array`` type
 *
 * An Idio ``array`` is an array of ``IDIO`` values.  It will
 * dynamically grow.  It may shrink.
 *
 * An array holds two sizes: the actual allocation size and the
 * "used" size (being the highest accessed index plus one).
 *
 * You can access indexes up to twice the allocated size and the array
 * will automatically grow.  Accessing an index more than twice the
 * allocated size will result in a bounds error.
 *
 * You can access negative indexes up to the used size which will
 * access elements indexed from the last used index backwards.
 *
 * You can access the array as a stack by using push/pop and
 * shift/unshift.  These use the used size of the array.
 *
 * You can find indexes of elements in the array: either the first
 * index with the default value or the first index where the specified
 * value is idio_eqp() to the element's value.
 *
 * You can delete elements from the array.  Technically, you set the
 * indexed element back to the default value.
 */

#include "idio.h"

void idio_array_error_length (char *m, idio_ai_t i, IDIO loc)
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

    sprintf (em, "array bounds error: abs (%td) > #elem %td", index, size);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (em, sh);

    IDIO c = idio_struct_instance (idio_condition_rt_array_bounds_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       idio_integer (index)));

    idio_raise_condition (idio_S_true, c);
}

/**
 * idio_assign_array() - base function for initialising arrays
 * @a: pre-allocated ``IDIO`` value
 * @asize: array size
 * @dv: default ``IDIO`` value for each element
 *
 * The ``IDIO`` value @a should have been allocated with
 * idio_gc_get().
 *
 * It is nominally called by idio_array_default() and
 * idio_array_resize().
 *
 * Return:
 * void
 */
void idio_assign_array (IDIO a, idio_ai_t asize, IDIO dv)
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
    IDIO_ARRAY_DV (a) = dv;
    IDIO_ARRAY_FLAGS (a) = IDIO_ARRAY_FLAG_NONE;

    idio_ai_t i;
    for (i = 0; i < asize; i++) {
	IDIO_ARRAY_AE (a, i) = dv;
    }
}

/**
 * idio_array_dv() - normal function for initialising arrays
 * @size: array size
 * @dv: default ``IDIO`` value
 *
 * It is nominally called by idio_array() and I(make-array)
 *
 * Return:
 * Returns the initialised array.
 */
IDIO idio_array_dv (idio_ai_t size, IDIO dv)
{
    if (0 == size) {
	size = 1;
    }

    IDIO a = idio_gc_get (IDIO_TYPE_ARRAY);
    idio_assign_array (a, size, dv);

    return a;
}

/**
 * idio_array() - array constructor
 * @size: array size
 *
 * The default value is %idio_S_nil I(#n).
 *
 * Return:
 * The initialised array.
 */
IDIO idio_array (idio_ai_t size)
{
    return idio_array_dv (size, idio_S_nil);
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

    IDIO_ASSERT_NOT_CONST (array, a);

    idio_array_t *oarray = a->u.array;
    idio_ai_t oasize = IDIO_ARRAY_ASIZE (a);
    idio_ai_t ousize = IDIO_ARRAY_USIZE (a);
    idio_ai_t nsize = oasize << 1;

    IDIO_FPRINTF (stderr, "idio_array_resize: %10p = {%d} -> {%d}\n", a, oasize, nsize);
    idio_assign_array (a, nsize, IDIO_ARRAY_DV (a));

    idio_ai_t i;
    for (i = 0 ; i < oarray->usize; i++) {
	idio_array_insert_index (a, oarray->ae[i], i);
    }

    /* pad with default value */
    for (;i < IDIO_ARRAY_ASIZE (a); i++) {
	idio_array_insert_index (a, IDIO_ARRAY_DV (a), i);
    }

    /* all the inserts above will have mucked usize up */
    IDIO_ARRAY_USIZE (a) = ousize;

    idio_gc_stats_free (sizeof (idio_array_t) + oasize * sizeof (idio_t));
    idio_gc_stats_free (sizeof (idio_array_t) + oasize * sizeof (idio_t));

    free (oarray->ae);
    free (oarray);
}

/**
 * idio_array_size() - return the array used size
 * @a: array
 *
 * Return:
 * The used size of the array.
 */
idio_ai_t idio_array_size (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    return IDIO_ARRAY_USIZE (a);
}

/**
 * idio_array_insert_index() - insert value into array at a specific index
 * @a: array
 * @o: ``IDIO`` value to be inserted
 * @index: array index
 *
 * The index @index can be:
 *
 * * negative - but cannot be larger than the size of the existing
 *   array
 * * up to twice the size of the existing array *allocation*.
 *   The array will be resized.
 *
 * Return:
 * void
 */
void idio_array_insert_index (IDIO a, IDIO o, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);
    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

    if (index < 0) {
	/*
	  negative indexes cannot be larger than the size of the
	  existing array
	*/
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    } else if (index >= IDIO_ARRAY_ASIZE (a)) {
	/*
	  positive indexes can't be too large...
	*/
	if (index < (IDIO_ARRAY_ASIZE (a) * 2)) {
	    idio_array_resize (a);
	} else {
	    idio_array_error_bounds (index, IDIO_ARRAY_ASIZE (a), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
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

/**
 * idio_array_push() - push value onto the end of an array
 * @a: array
 * @o: ``IDIO`` value to be pushed
 *
 * idio_array_insert_index() is called with &idio_array_s.usize.
 */
void idio_array_push (IDIO a, IDIO o)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);
    IDIO_TYPE_ASSERT (array, a);

    idio_array_insert_index (a, o, IDIO_ARRAY_USIZE (a));
}

/**
 * idio_array_pop() - pop value off the end of an array
 * @a: array
 *
 * The popped element is replaced with %idio_S_nil.
 *
 * Return:
 * ``IDIO`` value or %idio_S_nil if the array is empty.
 */
IDIO idio_array_pop (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

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

/**
 * idio_array_shift() - pop value off the front of an array
 * @a: array
 *
 * Return:
 * ``IDIO`` value or %idio_S_nil if the array is empty.
 */
IDIO idio_array_shift (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

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

/**
 * idio_array_unshift() - push value onto the front of an array
 * @a: array
 * @o: ``IDIO`` value to be pushed
 *
 * idio_array_insert_index() is called with &idio_array_s.usize.
 *
 * Return:
 * void
 */
void idio_array_unshift (IDIO a, IDIO o)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);
    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

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

/**
 * idio_array_head() - return the value at the front of an array
 * @a: array
 *
 * Return:
 * ``IDIO`` value or %idio_S_nil if the array is empty.
 */
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

/**
 * idio_array_top() - return the value at the end of an array
 * @a: array
 *
 * Return:
 * ``IDIO`` value or %idio_S_nil if the array is empty.
 */
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

/**
 * idio_array_get_index() - return the value at the given index of an array
 * @a: array
 * @index: the index
 *
 * Return:
 * ``IDIO`` value.
 */
IDIO idio_array_get_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_ARRAY_AE (a, index);
}

/**
 * idio_array_find_free_index() - return the index of the first default value element
 * @a: array
 * @index: starting index
 *
 * Return:
 * The index of the first element matching the default value or -1.
 */
idio_ai_t idio_array_find_free_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    for (; index < IDIO_ARRAY_USIZE (a); index++) {
	if (IDIO_ARRAY_DV (a) == IDIO_ARRAY_AE (a, index)) {
	    return index;
	}
    }

    return -1;
}

/**
 * idio_array_find_eqp() - return the index of the first element eqp to e
 * @a: array
 * @e: ``IDIO`` value to match
 * @index: starting index
 *
 * Return:
 * The index of the first matching element or -1.
 */
idio_ai_t idio_array_find_eqp (IDIO a, IDIO e, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    for (; index < IDIO_ARRAY_USIZE (a); index++) {
	if (idio_eqp (IDIO_ARRAY_AE (a, index), e)) {
	    return index;
	}
    }

    return -1;
}

/**
 * idio_array_bind() - assign values from array to arglist
 * @a: array
 * @nargs: number of argument to assign
 * @...: var args
 *
 * Return:
 * void
 */
void idio_array_bind (IDIO a, idio_ai_t nargs, ...)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < nargs) {
	idio_error_C ("too many args", a, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
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

/**
 * idio_array_copy() - copy an array
 * @a: array
 * @depth: shallow or deep
 * @extra: size of the new array beyond the original
 *
 * Return:
 * The new array.
 */
IDIO idio_array_copy (IDIO a, int depth, idio_ai_t extra)
{
    IDIO_ASSERT (a);
    IDIO_C_ASSERT (depth);
    IDIO_TYPE_ASSERT (array, a);

    idio_ai_t osz = IDIO_ARRAY_USIZE (a);
    idio_ai_t nsz = osz + extra;
    IDIO na = idio_array (nsz);

    idio_ai_t i;
    for (i = 0; i < osz; i++) {
	IDIO e = idio_array_get_index (a, i);
	if (IDIO_COPY_DEEP == depth) {
	    e = idio_copy (e, depth);
	}
	idio_array_insert_index (na,
				 e,
				 i);
    }

    return na;
}

/**
 * idio_array_to_list() - convert an array to a list
 * @a: array
 *
 * Return:
 * A list.
 */
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

/**
 * idio_array_delete_index() - delete an array element
 * @a: array
 * @index: the index to delete
 *
 * As you can't delete an array element it actually sets the index to
 * the default array value.
 *
 * Return:
 * 1.
 */
int idio_array_delete_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

    if (index < 0) {
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    index -= IDIO_ARRAY_USIZE (a);
	    idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return 0;
	}
    }

    if (index >= IDIO_ARRAY_USIZE (a)) {
	idio_array_error_bounds (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    IDIO_ARRAY_AE (a, index) = IDIO_ARRAY_DV (a);

    return 1;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array?", array_p, (IDIO o), "o", "\
test if `o` is an array				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an array, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_array (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("make-array", make_array, (IDIO size, IDIO args), "size [default]", "\
create an array with an initial allocation size of `size`\n\
						\n\
:param size: initial array size			\n\
:type size: integer				\n\
:param default: default element value		\n\
:return: the new array				\n\
:rtype: array					\n\
						\n\
If no default value is supplied #f is used.	\n\
")
{
    IDIO_ASSERT (size);
    IDIO_ASSERT (args);

    ptrdiff_t alen = -1;

    if (idio_isa_fixnum (size)) {
	alen = IDIO_FIXNUM_VAL (size);
    } else if (idio_isa_bignum (size)) {
	if (IDIO_BIGNUM_INTEGER_P (size)) {
	    alen = idio_bignum_ptrdiff_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		alen = idio_bignum_ptrdiff_value (size_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO dv = idio_S_false; /* S9fES -- Scheme specs say unspecified */

    if (idio_S_nil != args) {
	dv = IDIO_PAIR_H (args);
    }

    if (alen < 0) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "invalid length: %zd", alen);

	return idio_S_notreached;
    }

    IDIO a = idio_array_dv (alen, dv);
    IDIO_ARRAY_USIZE (a) = alen;

    return a;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("copy-array", copy_array, (IDIO orig, IDIO args), "orig [depth [extra]]", "\
copy array `orig` and add an optional `extra` elements	\n\
							\n\
:param orig: initial array				\n\
:type orig: array					\n\
:param depth: (optional) 'shallow or 'deep (default)	\n\
:param extra: (optional) extra elements			\n\
:return: the new array					\n\
:rtype: array						\n\
")
{
    IDIO_ASSERT (orig);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (array, orig);

    IDIO_VERIFY_PARAM_TYPE (list, args);

    idio_ai_t extra = 0;
    int depth = IDIO_COPY_DEEP;

    if (idio_S_nil != args) {
	IDIO idepth = IDIO_PAIR_H (args);
	IDIO iextra = idio_S_nil;
	if (idio_isa_pair (IDIO_PAIR_T (args))) {
	    iextra = IDIO_PAIR_HT (args);
	}

	if (idio_isa_symbol (idepth)) {
	    if (idio_S_deep == idepth) {
		depth = IDIO_COPY_DEEP;
	    } else if (idio_S_shallow == idepth) {
		depth = IDIO_COPY_SHALLOW;
	    } else {
		idio_error_param_type ("'deep or 'shallow", idepth, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    idio_error_param_type ("symbol", idepth, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (idio_isa_fixnum (iextra)) {
	    extra = IDIO_FIXNUM_VAL (iextra);
	} else if (idio_isa_bignum (iextra)) {
	    if (IDIO_BIGNUM_INTEGER_P (iextra)) {
		extra = idio_bignum_ptrdiff_value (iextra);
	    } else {
		IDIO iextra_i = idio_bignum_real_to_integer (iextra);
		if (idio_S_nil == iextra_i) {
		    idio_error_param_type ("integer", iextra, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		} else {
		    extra = idio_bignum_ptrdiff_value (iextra_i);
		}
	    }
	} else {
	    idio_error_param_type ("integer", iextra, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (extra < 0) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "invalid length: %zd", extra);

	return idio_S_notreached;
    }

    IDIO a = idio_array_copy (orig, depth, extra);

    return a;
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-fill!", array_fill, (IDIO a, IDIO fill), "a fill", "\
set all the elements of `a` to `fill`		\n\
						\n\
:param a: the array to fill			\n\
:type a: array					\n\
:param fill: value to use for fill		\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (fill);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

    idio_ai_t al = idio_array_size (a);
    idio_ai_t ai;

    for (ai = 0; ai < al ; ai++) {
	idio_array_insert_index (a, fill, ai);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array-length", array_length, (IDIO a), "a", "\
return the used length of `a`			\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:return: the length of the array		\n\
:rtype: integer					\n\
						\n\
The used length is the highest accessed index plus one\n\
")
{
    IDIO_ASSERT (a);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    return idio_fixnum (idio_array_size (a));
}

IDIO idio_array_ref (IDIO a, IDIO index)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);
    IDIO_TYPE_ASSERT (array, a);

    ptrdiff_t i = -1;

    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_ai_t al = idio_array_size (a);

    if (i < 0) {
	i += al;
	if (i < 0) {
	    i -= al;
	    idio_array_error_bounds (i, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (i >= al) {
	idio_array_error_bounds (i, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_array_get_index (a, i);
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-ref", array_ref, (IDIO a, IDIO index), "a index", "\
return the value at `index` of `a`		\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:param index: index				\n\
:type index: integer				\n\
:return: the value at index of the array	\n\
:rtype: integer					\n\
:raises: ^rt-array-bounds-error			\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    return idio_array_ref (a, index);
}

IDIO idio_array_set (IDIO a, IDIO index, IDIO v)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

    ptrdiff_t i = -1;

    if (idio_isa_fixnum (index)) {
	i = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    i = idio_bignum_ptrdiff_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		i = idio_bignum_ptrdiff_value (index_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_ai_t al = idio_array_size (a);

    if (i < 0) {
	i += al;
	if (i < 0) {
	    i -= al;
	    idio_array_error_bounds (i, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (i >= al) {
	idio_array_error_bounds (i, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_array_insert_index (a, v, i);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("array-set!", array_set, (IDIO a, IDIO index, IDIO v), "a index v", "\
set the `index` of `a` to `v`			\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:param index: index				\n\
:type index: integer				\n\
:param v: value 				\n\
:return: #<unspec>				\n\
:raises: ^rt-array-bounds-error			\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);
    IDIO_ASSERT (v);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    return idio_array_set (a, index, v);
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-push!", array_push, (IDIO a, IDIO v), "a v", "\
append `v` to `a`				\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:param v: value 				\n\
:return: #<unspec>				\n\
:raises: ^rt-array-bounds-error			\n\
						\n\
Treats `a` as a stack and appends `v` to the end\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (v);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    idio_array_push (a, v);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array-pop!", array_pop, (IDIO a), "a", "\
pop the last value off `a`			\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:return: value					\n\
:raises: ^rt-array-bounds-error			\n\
						\n\
Treats `a` as a stack and pops a value off the end\n\
")
{
    IDIO_ASSERT (a);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    IDIO v = idio_array_pop (a);

    return v;
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-unshift!", array_unshift, (IDIO a, IDIO v), "a v", "\
unshifts `v` onto `a`				\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:param v: value 				\n\
:return: #<unspec>				\n\
						\n\
Treats `a` as a stack and unshifts (prepends) `v` to the start\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (v);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    idio_array_unshift (a, v);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array-shift!", array_shift, (IDIO a), "a", "\
shifts the first value off `a`			\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:return: value					\n\
						\n\
Treats `a` as a stack and shifts a value off the start\n\
")
{
    IDIO_ASSERT (a);
    IDIO_VERIFY_PARAM_TYPE (array, a);

    IDIO v = idio_array_shift (a);

    return v;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array->list", array2list, (IDIO a), "a", "\
convert `a` to a list				\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:return: list					\n\
")
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
    IDIO_ADD_PRIMITIVE (copy_array);
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

