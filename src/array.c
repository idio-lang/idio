/*
 * Copyright (c) 2015-2022 Ian Fitchet
 * <idf(at)idio-lang.org>
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
 * You can access negative indexes up to the used size which will
 * access elements indexed from the last used index backwards.
 *
 * You can access the array as a stack by using push/pop and
 * shift/unshift.  These use the used size of the array.  These are
 * the only way to grow the size of an array.
 *
 * You can find indexes of elements in the array: either the first
 * index with the default value or the first index where the specified
 * value is idio_eqp() to the element's value.
 *
 * You can delete elements from the array.  Technically, you set the
 * indexed element back to the default value.
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "closure.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio.h"
#include "idio-string.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static IDIO idio_array_default_value = idio_S_false;

static void idio_array_length_error (char const *msg, idio_ai_t size, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "%s: size %zd", msg, size);
    idio_display_C_len (em, eml, msh);

    idio_error_raise_cont (idio_condition_rt_array_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       idio_S_nil));

    /* notreached */
}

static void idio_array_bounds_error (idio_ai_t index, idio_ai_t size, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "array bounds error: abs (%zd) >= #elem %zd", index, size);
    idio_display_C_len (em, eml, msh);

    idio_error_raise_cont (idio_condition_rt_array_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       idio_integer (index)));

    /* notreached */
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
 * idio_resize_array().
 *
 * Return:
 * void
 */
void idio_assign_array (IDIO a, idio_as_t asize, IDIO dv)
{
    IDIO_ASSERT (a);
    IDIO_C_ASSERT (asize);
    IDIO_ASSERT (dv);

    IDIO_TYPE_ASSERT (array, a);

    IDIO_GC_ALLOC (a->u.array, sizeof (idio_array_t));
    IDIO_GC_ALLOC (a->u.array->ae, asize * sizeof (IDIO));

    IDIO_ARRAY_GREY (a) = NULL;
    IDIO_ARRAY_ASIZE (a) = asize;
    IDIO_ARRAY_USIZE (a) = 0;
    IDIO_ARRAY_DV (a) = dv;
    IDIO_ARRAY_FLAGS (a) = IDIO_ARRAY_FLAG_NONE;

    idio_as_t i;
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
IDIO idio_array_dv (idio_as_t size0, IDIO dv)
{
    idio_as_t size = size0;

    if (0 == size) {
	size = 1;
    }

    IDIO a = idio_gc_get (IDIO_TYPE_ARRAY);
    a->vtable = idio_vtable (IDIO_TYPE_ARRAY);
    idio_assign_array (a, size, dv);

    return a;
}

/**
 * idio_array() - array constructor
 * @size: array size
 *
 * The default value is %idio_S_false I(#f).
 *
 * Return:
 * The initialised array.
 */
IDIO idio_array (idio_as_t size)
{
    return idio_array_dv (size, idio_array_default_value);
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

    IDIO_GC_FREE (a->u.array->ae, IDIO_ARRAY_ASIZE (a) * sizeof (IDIO));
    IDIO_GC_FREE (a->u.array, sizeof (idio_array_t));
}

void idio_resize_array_to (IDIO a, idio_as_t nsize)
{
    IDIO_ASSERT (a);
    IDIO_C_ASSERT (nsize);

    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

    IDIO_GC_REALLOC (a->u.array->ae, nsize * sizeof (IDIO));

    IDIO_ARRAY_ASIZE (a) = nsize;

    idio_as_t i;
    for (i = IDIO_ARRAY_USIZE (a); i < nsize; i++) {
	IDIO_ARRAY_AE (a, i) = IDIO_ARRAY_DV (a);
    }
}

void idio_resize_array (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

    idio_as_t oasize = IDIO_ARRAY_ASIZE (a);
    idio_as_t nsize = oasize + 1024;
    if (oasize < 1024) {
	nsize = oasize << 1;
    }

    idio_resize_array_to (a, nsize);
}

/**
 * idio_array_size() - return the array used size
 * @a: array
 *
 * Return:
 * The used size of the array.
 */
idio_as_t idio_array_size (IDIO a)
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
 *
 * * up to the size of the existing array *allocation* plus one
 *   (ie. push).  The array will be resized.
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
	 * XXX no C code uses negative indexes!
	 *
	 * negative indexes cannot be larger than the size of the
	 * existing array
	 */
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    /*
	     * Test Case: ??
	     *
	     * The primitive handles this test for user-code.
	     */
	    index -= IDIO_ARRAY_USIZE (a);
	    idio_array_bounds_error (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    } else if (index >= (idio_ai_t) IDIO_ARRAY_ASIZE (a)) {
	if (index < ((idio_ai_t) IDIO_ARRAY_ASIZE (a) + 1)) {
	    idio_resize_array (a);
	} else {
	    /*
	     * Test Case: ??
	     *
	     * The primitive handles a more restrictive case for
	     * user-code using USIZE not ASIZE.
	     *
	     * Requires developer "vision" to get here otherwise.
	     */
	    idio_array_bounds_error (index, IDIO_ARRAY_ASIZE (a), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    IDIO_ARRAY_AE (a, index) = o;

    /* index is 0+, usize is 1+ */
    index++;
    if (index > (idio_ai_t) IDIO_ARRAY_USIZE (a)) {
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

void idio_array_push_n (IDIO a, idio_as_t const nargs, ...)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);
    IDIO_C_ASSERT (nargs);

    IDIO_ASSERT_NOT_CONST (array, a);

    idio_as_t index = IDIO_ARRAY_USIZE (a);

    while ((index + nargs) >= IDIO_ARRAY_ASIZE (a)) {
	idio_resize_array (a);
    }

    va_list ap;
    va_start (ap, nargs);

    idio_as_t i;
    for (i = 0; i < nargs; i++) {
	IDIO arg = va_arg (ap, IDIO);
	/* IDIO_ASSERT (*arg); */
	IDIO_ARRAY_AE (a, index + i) = arg;
    }

    va_end (ap);


    /* index is 0+, usize is 1+ */
    index += nargs;
    if (index > IDIO_ARRAY_USIZE (a)) {
	IDIO_ARRAY_USIZE (a) = index;
    }

    IDIO_C_ASSERT (IDIO_ARRAY_USIZE (a) <= IDIO_ARRAY_ASIZE (a));
}

/**
 * idio_array_pop() - pop value off the end of an array
 * @a: array
 *
 * The popped element is replaced with the array's default value.
 *
 * Return:
 * ``IDIO`` value or %idio_S_nil if the array is empty.
 */
IDIO idio_array_pop (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    IDIO_ASSERT_NOT_CONST (array, a);

    idio_ai_t index = IDIO_ARRAY_USIZE (a);
    if (index < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }

    /*
     * The function idio_array_ref_index is defensive in the face of a
     * negative index etc..  We know we have a positive index that is
     * not beyond the end of the array.  So we can dive right in.
     */
    index--;
    IDIO e = IDIO_ARRAY_AE (a, index);
    IDIO_ARRAY_AE (a, index) = IDIO_ARRAY_DV (a);
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

    IDIO e = idio_array_ref_index (a, 0);
    idio_as_t i;
    for (i = 0 ; i < (IDIO_ARRAY_USIZE (a) - 1); i++) {
	IDIO e = idio_array_ref_index (a, i + 1);
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
	    IDIO e = idio_array_ref_index (a, i - 1);
	    IDIO_ASSERT (e);

	    idio_array_insert_index (a, e, i);
	}
    }
    idio_array_insert_index (a, o, 0);

    IDIO_C_ASSERT (IDIO_ARRAY_USIZE (a) <= IDIO_ARRAY_ASIZE (a));
}

/**
 * idio_array_top() - return the value at the end of an array
 * @a: array
 *
 * Return:
 * ``IDIO`` value or %idio_S_nil if the array is empty.
 *
 *
 * XXX idio_array_top() is referenced by the code in idio_as_string()
 * to print the top-most stack entry for a thread.  So, unless you
 * print a thread object out this code won't be called.
 */
IDIO idio_array_top (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    if (IDIO_ARRAY_USIZE (a) < 1) {
	IDIO_ARRAY_USIZE (a) = 0;
	return idio_S_nil;
    }

    return idio_array_ref_index (a, IDIO_ARRAY_USIZE (a) - 1);
}

/**
 * idio_array_ref_index() - return the value at the given index of an array
 * @a: array
 * @index: the index
 *
 * Return:
 * ``IDIO`` value.
 */
IDIO idio_array_ref_index (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    if (index < 0) {
	index += IDIO_ARRAY_USIZE (a);
	if (index < 0) {
	    /*
	     * Test Case: array-errors/array-ref-negative-bounds.idio
	     *
	     * a := #[ 1 2 3 ]
	     * array-ref a -4
	     */
	    index -= IDIO_ARRAY_USIZE (a);
	    idio_array_bounds_error (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (index >= (idio_ai_t) IDIO_ARRAY_USIZE (a)) {
	/*
	 * Test Case: array-errors/array-ref-positive-bounds.idio
	 *
	 * a := #[ 1 2 3 ]
	 * array-ref a 5
	 */
	idio_array_bounds_error (index, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_ARRAY_AE (a, index);
}

/**
 * idio_array_find() - return the index of the first element eqp to e
 * @a: array
 * @eqp: IDIO_EQUAL_EQP etc.
 * @e: ``IDIO`` value to match
 * @index: starting index
 *
 * Return:
 * The index of the first matching element or -1.
 */
static idio_ai_t idio_array_find (IDIO a, idio_equal_enum eqp, IDIO e, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    idio_as_t as = IDIO_ARRAY_USIZE (a);

    if (0 == as) {
	return -1;
    }

    if (index < 0) {
	/*
	 * Test Case: ??
	 *
	 * Used by the codegen constants lookup code
	 */
	idio_array_bounds_error (index, as, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    if (index >= (idio_ai_t) as) {
	/*
	 * Test Case: ??
	 *
	 * Used by the codegen constants lookup code
	 */
	idio_array_bounds_error (index, as, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    for (; index < (idio_ai_t) as; index++) {
	if (idio_equal (IDIO_ARRAY_AE (a, index), e, eqp)) {
	    return index;
	}
    }

    return -1;
}

/**
 * idio_array_find_eqp() - return the index of the first element eq? to e
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

    return idio_array_find (a, IDIO_EQUAL_EQP, e, index);
}

/**
 * idio_array_find_equalp() - return the index of the first element eq? to e
 * @a: array
 * @e: ``IDIO`` value to match
 * @index: starting index
 *
 * Return:
 * The index of the first matching element or -1.
 */
idio_ai_t idio_array_find_equalp (IDIO a, IDIO e, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    return idio_array_find (a, IDIO_EQUAL_EQUALP, e, index);
}

IDIO_DEFINE_PRIMITIVE2V_DS ("array-find-eq?", array_find_eqp, (IDIO a, IDIO v, IDIO args), "a v [index]", "\
return the first index of `v` in `a` starting	\n\
at `index` or ``-1``				\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:param v: the value to search for		\n\
:type v: any					\n\
:param index: starting index, defaults to ``0``	\n\
:type index: integer, optional			\n\
:return: the index of the first `v` in `a`	\n\
:rtype: integer					\n\
:raises: ^rt-array-bounds-error			\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (v);
    IDIO_ASSERT (args);

    /*
     * Test Case: array-errors/array-find-eqp-bad-array-type.idio
     *
     * array-find-eqp #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);

    idio_ai_t index = 0;

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    if (idio_isa_pair (args)) {
	IDIO iv = IDIO_PAIR_H (args);

	if (idio_isa_fixnum (iv)) {
	    index = IDIO_FIXNUM_VAL (iv);
	} else if (idio_isa_bignum (iv)) {
	    if (IDIO_BIGNUM_INTEGER_P (iv)) {
		/*
		 * Code coverage: to get here we'd need to pass
		 * FIXNUM-MAX+1 and that is to too big to allocate...
		 */
		index = idio_bignum_ptrdiff_t_value (iv);
	    } else {
		IDIO iv_i = idio_bignum_real_to_integer (iv);
		if (idio_S_nil == iv_i) {
		    /*
		     * Test Case: array-errors/array-find-eqp-index-not-integer.idio
		     *
		     * array-find-eqp #[] #t 1.1
		     */
		    idio_error_param_type ("integer", iv, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		} else {
		    index = idio_bignum_ptrdiff_t_value (iv_i);
		}
	    }
	} else {
	    /*
	     * Test Case: array-errors/array-find-eqp-bad-index-type.idio
	     *
	     * array-find-eqp #[] #t #t
	     */
	    idio_error_param_type ("integer", iv, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (index < 0) {
	/*
	 * Test Case: array-errors/array-find-eqp-negative-index.idio
	 *
	 * array-find-eqp #[] #t -1
	 */
	idio_array_length_error ("invalid length", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_ai_t fi = idio_array_find_eqp (a, v, index);

    return idio_integer (fi);
}

/**
 * idio_copy_array() - copy an array
 * @a: array
 * @depth: shallow or deep
 * @extra: size of the new array beyond the original
 *
 * Return:
 * The new array.
 */
IDIO idio_copy_array (IDIO a, int depth, idio_as_t extra)
{
    IDIO_ASSERT (a);
    IDIO_C_ASSERT (depth);
    IDIO_TYPE_ASSERT (array, a);

    idio_as_t osz = IDIO_ARRAY_USIZE (a);
    idio_as_t nsz = osz + extra;

    IDIO na = idio_array_dv (nsz, IDIO_ARRAY_DV (a));

    idio_as_t i;
    for (i = 0; i < osz; i++) {
	IDIO e = idio_array_ref_index (a, i);
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
 * idio_duplicate_array() - duplicate an array
 * @a: this array
 * @o: that array
 * @n: this many
 * @depth: shallow or deep
 *
 * Duplicate the contents of that array in this.
 */
void idio_duplicate_array (IDIO a, IDIO o, idio_as_t n, int depth)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (depth);

    IDIO_TYPE_ASSERT (array, a);
    IDIO_TYPE_ASSERT (array, o);

    idio_as_t osz = IDIO_ARRAY_USIZE (o);
    if (n) {
	osz = n;
    }
    if (osz > IDIO_ARRAY_ASIZE (a)) {
	fprintf (stderr, "dupe %zu -> %zu\n", IDIO_ARRAY_ASIZE (a), osz);
	IDIO_GC_FREE (a->u.array->ae, IDIO_ARRAY_ASIZE (a) * sizeof (IDIO));
	IDIO_GC_ALLOC (a->u.array->ae, osz * sizeof (IDIO));
    }
    IDIO_ARRAY_USIZE (a) = osz;

    idio_as_t i;
    for (i = 0; i < osz; i++) {
	IDIO e = idio_array_ref_index (o, i);
	if (IDIO_COPY_DEEP == depth) {
	    e = idio_copy (e, depth);
	}
	idio_array_insert_index (a, e, i);
    }
}

/**
 * idio_array_to_list_from() - convert an array to a list from index
 * @a: array
 *
 * Return:
 * A list.
 */
IDIO idio_array_to_list_from (IDIO a, idio_ai_t index)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (array, a);

    idio_ai_t al = IDIO_ARRAY_USIZE (a);
    idio_ai_t ai;

    IDIO r = idio_S_nil;

    for (ai = al -1; ai >= index; ai--) {
	r = idio_pair (idio_array_ref_index (a, ai),
		       r);
    }

    return r;
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

    return idio_array_to_list_from (a, 0);
}

IDIO_DEFINE_PRIMITIVE1_DS ("array?", arrayp, (IDIO o), "o", "\
test if `o` is an array				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an array, ``#f`` otherwise	\n\
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
:param default: default array element value, defaults to ``#f``	\n\
:type default: value, optional			\n\
:return: the new array				\n\
:rtype: array					\n\
")
{
    IDIO_ASSERT (size);
    IDIO_ASSERT (args);

    ptrdiff_t alen = -1;

    if (idio_isa_fixnum (size)) {
	alen = IDIO_FIXNUM_VAL (size);
    } else if (idio_isa_bignum (size)) {
	if (IDIO_BIGNUM_INTEGER_P (size)) {
	    /*
	     * Code coverage: to get here we'd need to pass
	     * FIXNUM-MAX+1 and that is to too big to allocate...
	     */
	    alen = idio_bignum_ptrdiff_t_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		/*
		 * Test Case: array-errors/make-array-size-not-integer.idio
		 *
		 * make-array 1.1
		 */
		idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		alen = idio_bignum_ptrdiff_t_value (size_i);
	    }
	}
    } else {
	/*
	 * Test Case: array-errors/make-array-size-not-integer.idio
	 *
	 * make-array #t
	 */
	idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    /*
     * S9fES -- Scheme specs say unspecified
     */
    IDIO dv = idio_array_default_value;

    if (idio_isa_pair (args)) {
	dv = IDIO_PAIR_H (args);
    }

    if (alen < 0) {
	/*
	 * Test Case: array-errors/make-array-size-negative-integer.idio
	 *
	 * make-array -1
	 */
	idio_array_length_error ("invalid length", alen, IDIO_C_FUNC_LOCATION ());

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
:param depth: ``'shallow`` or ``'deep`` (default)	\n\
:type depth: symbol, optional				\n\
:param extra: how many extra elements, defaults to 0 (zero)	\n\
:type extra: integer, optional				\n\
:return: the new array					\n\
:rtype: array						\n\
")
{
    IDIO_ASSERT (orig);
    IDIO_ASSERT (args);

    /*
     * Test Case: array-errors/copy-array-bad-type.idio
     *
     * copy-array #t
     */
    IDIO_USER_TYPE_ASSERT (array, orig);
    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    idio_ai_t extra = 0;
    int depth = IDIO_COPY_DEEP;

    if (idio_isa_pair (args)) {
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
		/*
		 * Test Case: array-errors/copy-array-bad-depth-symbol.idio
		 *
		 * copy-array #[ 1 2 3 ] 'fully
		 */
		idio_error_param_type ("'deep or 'shallow", idepth, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: array-errors/copy-array-bad-depth-type.idio
	     *
	     * copy-array #[ 1 2 3 ] #t
	     */
	    idio_error_param_type ("symbol", idepth, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (idio_S_nil != iextra) {
	    if (idio_isa_fixnum (iextra)) {
		extra = IDIO_FIXNUM_VAL (iextra);
	    } else if (idio_isa_bignum (iextra)) {
		if (IDIO_BIGNUM_INTEGER_P (iextra)) {
		    /*
		     * Code coverage: to get here we'd need to pass
		     * FIXNUM-MAX+1 and that is to too big to
		     * allocate...
		     */
		    extra = idio_bignum_ptrdiff_t_value (iextra);
		} else {
		    IDIO iextra_i = idio_bignum_real_to_integer (iextra);
		    if (idio_S_nil == iextra_i) {
			/*
			 * Test Case: array-errors/copy-array-extra-float.idio
			 *
			 * copy-array #[ 1 2 3 ] 'deep 1.1
			 */
			idio_error_param_type ("integer", iextra, IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    } else {
			extra = idio_bignum_ptrdiff_t_value (iextra_i);
		    }
		}
	    } else {
		/*
		 * Test Case: array-errors/copy-array-bad-extra-type.idio
		 *
		 * copy-array #[ 1 2 3 ] 'deep #t
		 */
		idio_error_param_type ("integer", iextra, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}
    }

    if (extra < 0) {
	/*
	 * Test Case: array-errors/copy-array-extra-negative-integer.idio
	 *
	 * copy-array #[ 1 2 3 ] 'deep -1
	 */
	idio_array_length_error ("invalid length", extra, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO a = idio_copy_array (orig, depth, extra);

    return a;
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-fill!", array_fill, (IDIO a, IDIO fill), "a fill", "\
set all the elements of `a` to `fill`		\n\
						\n\
:param a: the array to fill			\n\
:type a: array					\n\
:param fill: value to use for fill		\n\
:type fill: any					\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (fill);

    /*
     * Test Case: array-errors/array-fill!-bad-type.idio
     *
     * array-fill! #t #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);
    /*
     * Test Case: n/a
     *
     * The VM returns a copy of any constant array when referenced.
     */
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

    /*
     * Test Case: array-errors/array-length-bad-type.idio
     *
     * array-length #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);

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
	    /*
	     * Code coverage: to get here we'd need to pass
	     * FIXNUM-MAX+1 and that is to too big to allocate...
	     */
	    i = idio_bignum_ptrdiff_t_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		/*
		 * Test Case: array-errors/array-ref-float.idio
		 *
		 * a := #[ 1 2 3 ]
		 * array-ref a 1.1
		 */
		idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		i = idio_bignum_ptrdiff_t_value (index_i);
	    }
	}
    } else {
	/*
	 * Test Case: array-errors/array-ref-not-integer.idio
	 *
	 * a := #[ 1 2 3 ]
	 * array-ref a #t
	 */
	idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_array_ref_index (a, i);
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

    /*
     * Test Case: array-errors/array-ref-bad-type.idio
     *
     * array-ref #t 0
     */
    IDIO_USER_TYPE_ASSERT (array, a);

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
	    /*
	     * Code coverage: to get here we'd need to pass
	     * FIXNUM-MAX+1 and that is to too big to allocate...
	     */
	    i = idio_bignum_ptrdiff_t_value (index);
	} else {
	    IDIO index_i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == index_i) {
		/*
		 * Test Case: array-errors/array-set-float.idio
		 *
		 * a := #[ 1 2 3 ]
		 * array-set! a 1.1 0
		 */
		idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		i = idio_bignum_ptrdiff_t_value (index_i);
	    }
	}
    } else {
	/*
	 * Test Case: array-errors/array-set-not-integer.idio
	 *
	 * a := #[ 1 2 3 ]
	 * array-set! a #t 0
	 */
	idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_ai_t al = idio_array_size (a);

    if (i < 0) {
	i += al;
	if (i < 0) {
	    /*
	     * Test Case: array-errors/array-set-negative-bounds.idio
	     *
	     * a := #[ 1 2 3 ]
	     * array-set! a -4 4
	     */
	    i -= al;
	    idio_array_bounds_error (i, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (i >= (al + 1)) {
	/*
	 * Test Case: array-errors/array-set-positive-bounds.idio
	 *
	 * a := #[ 1 2 3 ]
	 * array-set! a 5 5
	 *
	 * XXX This is more restrictive as the user is limited to
	 * USIZE+1 (for push) not ASIZE for a pre-allocated array.
	 */
	idio_array_bounds_error (i, IDIO_ARRAY_USIZE (a), IDIO_C_FUNC_LOCATION ());

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
:type v: any	 				\n\
:return: ``#<unspec>``				\n\
:raises: ^rt-array-bounds-error			\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (index);
    IDIO_ASSERT (v);

    /*
     * Test Case: array-errors/array-set!-bad-type.idio
     *
     * array-set! #t 0 0
     */
    IDIO_USER_TYPE_ASSERT (array, a);

    return idio_array_set (a, index, v);
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-push!", array_push, (IDIO a, IDIO v), "a v", "\
append `v` to `a`				\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:param v: value 				\n\
:type v: any	 				\n\
:return: ``#<unspec>``				\n\
:raises: ^rt-array-bounds-error			\n\
						\n\
Treats `a` as a stack and appends `v` to the end\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (v);

    /*
     * Test Case: array-errors/array-push!-bad-type.idio
     *
     * array-push! #t 0
     */
    IDIO_USER_TYPE_ASSERT (array, a);

    idio_array_push (a, v);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array-pop!", array_pop, (IDIO a), "a", "\
pop the last value off `a`			\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:return: value					\n\
:rtype: any	 				\n\
:raises: ^rt-array-bounds-error			\n\
						\n\
Treats `a` as a stack and pops a value off the end\n\
")
{
    IDIO_ASSERT (a);

    /*
     * Test Case: array-errors/array-pop!-bad-type.idio
     *
     * array-pop! #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);

    IDIO v = idio_array_pop (a);

    return v;
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-unshift!", array_unshift, (IDIO a, IDIO v), "a v", "\
unshifts `v` onto `a`				\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:param v: value 				\n\
:rtype: any	 				\n\
:return: ``#<unspec>``				\n\
						\n\
Treats `a` as a stack and unshifts (prepends) `v` to the start\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (v);

    /*
     * Test Case: array-errors/array-unshift!-bad-type.idio
     *
     * array-unshift! #t 0
     */
    IDIO_USER_TYPE_ASSERT (array, a);

    idio_array_unshift (a, v);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array-shift!", array_shift, (IDIO a), "a", "\
shifts the first value off `a`			\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:return: value					\n\
:rtype: any	 				\n\
						\n\
Treats `a` as a stack and shifts a value off the start\n\
")
{
    IDIO_ASSERT (a);

    /*
     * Test Case: array-errors/array-shift!-bad-type.idio
     *
     * array-shift! #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);

    IDIO v = idio_array_shift (a);

    return v;
}

IDIO_DEFINE_PRIMITIVE1_DS ("array->list", array2list, (IDIO a), "a", "\
convert `a` to a list				\n\
						\n\
:param a: the array				\n\
:type a: array					\n\
:return: list					\n\
:rtype: list	 				\n\
")
{
    IDIO_ASSERT (a);

    /*
     * Test Case: array-errors/array->list-bad-type.idio
     *
     * array->list #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);

    return idio_array_to_list (a);
}

IDIO_DEFINE_PRIMITIVE2_DS ("array-for-each-set", array_for_each_set, (IDIO a, IDIO func), "a func", "\
call `func` for each element in array `a` with a non-default	\n\
value with arguments: `index` the value at that	index		\n\
								\n\
:param a: array							\n\
:type a: array							\n\
:param func: func to be called with each index, value tuple	\n\
:type func: 2-ary function					\n\
:return: ``#<unspec>``						\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (func);

    /*
     * Test Case: array-errors/array-for-each-set-bad-array-type.idio
     *
     * array-for-each-set #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);
    /*
     * Test Case: array-errors/array-for-each-set-bad-func-type.idio
     *
     * array-for-each-set #[] #t #t
     */
    IDIO_USER_TYPE_ASSERT (function, func);

    idio_ai_t al = IDIO_ARRAY_USIZE (a);
    idio_ai_t ai;

    for (ai = 0; ai < al; ai++) {
	IDIO v = idio_array_ref_index (a, ai);
	if (! idio_equal (v, IDIO_ARRAY_DV (a), IDIO_EQUAL_EQUALP)) {
	    idio_vm_invoke_C (IDIO_LIST3 (func, idio_integer (ai), v));
	}
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("fold-array", fold_array, (IDIO a, IDIO func, IDIO val), "a func val", "\
call `func` for each element in array `a` with arguments:	\n\
`index`, the value at that index and `val`			\n\
								\n\
`val` is updated to the value returned by `func`.		\n\
								\n\
The final value of `val` is returned.				\n\
								\n\
:param a: array							\n\
:type a: array							\n\
:param func: func to be called with each index, value, val tuple	\n\
:type func: 3-ary function					\n\
:param val: initial value for `val`				\n\
:type val: any							\n\
:return: final value of `val`					\n\
:rtype: any			 				\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (func);
    IDIO_ASSERT (val);

    /*
     * Test Case: array-errors/fold-array-bad-array-type.idio
     *
     * fold-array #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (array, a);
    /*
     * Test Case: array-errors/fold-array-bad-func-type.idio
     *
     * fold-array #[] #t #t
     */
    IDIO_USER_TYPE_ASSERT (function, func);

    idio_ai_t al = IDIO_ARRAY_USIZE (a);
    idio_ai_t ai;

    for (ai = 0; ai < al; ai++) {
	IDIO v = idio_array_ref_index (a, ai);
	val = idio_vm_invoke_C (IDIO_LIST4 (func, idio_integer (ai), v, val));
    }

    return val;
}

char *idio_array_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (array, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#[");

#ifdef IDIO_DEBUG
    int printed = 0;
    for (idio_as_t i = 0; i < IDIO_ARRAY_USIZE (v); i++) {
	IDIO_STRCAT (r, sizep, " ");
	size_t t_size = 0;
	char *t = idio_report_string (IDIO_ARRAY_AE (v, i), &t_size, depth - 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, t, t_size);
	printed = 1;
    }
    if (printed) {
	IDIO_STRCAT (r, sizep, " ");
    }
#else
    char *t;
    size_t t_size = idio_asprintf (&t, " /%zu ", IDIO_ARRAY_USIZE (v));
    IDIO_STRCAT_FREE (r, sizep, t, t_size);
#endif

    IDIO_STRCAT (r, sizep, "]");

    return r;
}

char *idio_array_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (array, v);

    char *r = NULL;

    /*
     * XXX This 40 element break should be revisited.  I guess I'm
     * less likely to be printing huge internal arrays as the code
     * matures.
     */
    seen = idio_pair (v, seen);
    *sizep = idio_asprintf (&r, "#[ ");
    if (depth > 0) {
	if (IDIO_ARRAY_USIZE (v) <= 40) {
	    for (size_t i = 0; i < IDIO_ARRAY_USIZE (v); i++) {
		size_t t_size = 0;
		char *t = idio_as_string (IDIO_ARRAY_AE (v, i), &t_size, depth - 1, seen, 0);
		IDIO_STRCAT_FREE (r, sizep, t, t_size);
		IDIO_STRCAT (r, sizep, " ");
	    }
	} else {
	    for (idio_ai_t i = 0; i < 20; i++) {
		size_t t_size = 0;
		char *t = idio_as_string (IDIO_ARRAY_AE (v, i), &t_size, depth - 1, seen, 0);
		IDIO_STRCAT_FREE (r, sizep, t, t_size);
		IDIO_STRCAT (r, sizep, " ");
	    }
	    char *aei;
	    size_t aei_size = idio_asprintf (&aei, "..[%zu] ", IDIO_ARRAY_USIZE (v) - 20);
	    IDIO_STRCAT_FREE (r, sizep, aei, aei_size);
	    for (size_t i = IDIO_ARRAY_USIZE (v) - 20; i < IDIO_ARRAY_USIZE (v); i++) {
		size_t t_size = 0;
		char *t = idio_as_string (IDIO_ARRAY_AE (v, i), &t_size, depth - 1, seen, 0);
		IDIO_STRCAT_FREE (r, sizep, t, t_size);
		IDIO_STRCAT (r, sizep, " ");
	    }
	}
    } else {
	/*
	 * Code coverage:
	 *
	 * Complicated structures are contracted.
	 */
	IDIO_STRCAT (r, sizep, ".. ");
    }
    IDIO_STRCAT (r, sizep, "]");

    return r;
}

IDIO idio_array_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    IDIO seen = va_arg (ap, IDIO);
    int depth = va_arg (ap, int);
    va_end (ap);

    IDIO_ASSERT (seen);

    char *C_r = idio_array_as_C_string (v, sizep, 0, seen, depth);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_array_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (array_find_eqp);
    IDIO_ADD_PRIMITIVE (arrayp);
    IDIO_ADD_PRIMITIVE (make_array);
    IDIO_ADD_PRIMITIVE (copy_array);
    IDIO_ADD_PRIMITIVE (array_fill);
    IDIO_ADD_PRIMITIVE (array_length);

    IDIO ref = IDIO_ADD_PRIMITIVE (array_ref);
    idio_vtable_t *a_vt = idio_vtable (IDIO_TYPE_ARRAY);
    idio_vtable_add_method (a_vt,
			    idio_S_value_index,
			    idio_vtable_create_method_value (idio_util_method_value_index,
							     idio_vm_default_values_ref (IDIO_FIXNUM_VAL (ref))));

    IDIO set = IDIO_ADD_PRIMITIVE (array_set);
    idio_vtable_add_method (a_vt,
			    idio_S_set_value_index,
			    idio_vtable_create_method_value (idio_util_method_set_value_index,
							     idio_vm_default_values_ref (IDIO_FIXNUM_VAL (set))));

    IDIO_ADD_PRIMITIVE (array_push);
    IDIO_ADD_PRIMITIVE (array_pop);
    IDIO_ADD_PRIMITIVE (array_unshift);
    IDIO_ADD_PRIMITIVE (array_shift);
    IDIO_ADD_PRIMITIVE (array2list);
    IDIO_ADD_PRIMITIVE (array_for_each_set);
    IDIO_ADD_PRIMITIVE (fold_array);
}

void idio_init_array ()
{
    idio_module_table_register (idio_array_add_primitives, NULL, NULL);

    idio_vtable_t *a_vt = idio_vtable (IDIO_TYPE_ARRAY);

    idio_vtable_add_method (a_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_array));

    idio_vtable_add_method (a_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_array_method_2string));
}
