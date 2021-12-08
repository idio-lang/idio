/*
 * Copyright (c) 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * bitset.c
 *
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
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "bignum.h"
#include "bitset.h"
#include "closure.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"

static void idio_bitset_bounds_error (size_t const bit, size_t const size, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "bitset bounds error: %zu >= size %zu", bit, size);
    idio_display_C_len (em, eml, msh);

    idio_error_raise_cont (idio_condition_rt_bitset_bounds_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       idio_integer (bit)));

    /* notreached */
}

static void idio_bitset_size_mismatch_error (size_t const s1, size_t const s2, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "bitset size mistmatch: %zu != %zu", s1, s2);
    idio_display_C_len (em, eml, msh);

    idio_error_raise_cont (idio_condition_rt_bitset_size_mismatch_error_type,
			   IDIO_LIST5 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       idio_integer (s1),
				       idio_integer (s2)));

    /* notreached */
}

IDIO idio_bitset (size_t const size)
{
    IDIO bs = idio_gc_get (IDIO_TYPE_BITSET);

    IDIO_BITSET_SIZE (bs) = size;
    bs->u.bitset.words = NULL;

    if (size) {
	size_t n = size / IDIO_BITSET_BITS_PER_WORD + 1;
	IDIO_GC_ALLOC (bs->u.bitset.words, n * sizeof (idio_bitset_word_t));
	memset (bs->u.bitset.words, 0UL, n * sizeof (idio_bitset_word_t));
    }

    return bs;
}

int idio_isa_bitset (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_BITSET);
}

void idio_free_bitset (IDIO bs)
{
    IDIO_ASSERT (bs);
    IDIO_TYPE_ASSERT (bitset, bs);

    IDIO_GC_FREE (bs->u.bitset.words, IDIO_BITSET_SIZE (bs) / sizeof (idio_bitset_word_t) + 1);
}

IDIO idio_bitset_set (IDIO bs, size_t const bit)
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (bitset, bs);

    if (bit >= IDIO_BITSET_SIZE (bs)) {
	/*
	 * Test Case: bitset-errors/bitset-set-bounds.idio
	 *
	 * bs := #B{ 3 }
	 * bitset-set! bs 5
	 */
	idio_bitset_bounds_error (bit, IDIO_BITSET_SIZE (bs), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    size_t i = bit / IDIO_BITSET_BITS_PER_WORD;

    IDIO_BITSET_WORDS (bs, i) |= 1UL << (bit % IDIO_BITSET_BITS_PER_WORD);

    return idio_S_unspec;
}

IDIO idio_bitset_clear (IDIO bs, size_t const bit)
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (bitset, bs);

    if (bit >= IDIO_BITSET_SIZE (bs)) {
	/*
	 * Test Case: bitset-errors/bitset-set-bounds.idio
	 *
	 * bs := #B{ 3 }
	 * bitset-clear! bs 5
	 */
	idio_bitset_bounds_error (bit, IDIO_BITSET_SIZE (bs), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    size_t i = bit / IDIO_BITSET_BITS_PER_WORD;

    idio_bitset_word_t mask = 1UL << (bit % IDIO_BITSET_BITS_PER_WORD);
    IDIO_BITSET_WORDS (bs, i) &= ~ mask;

    return idio_S_unspec;
}

IDIO idio_bitset_ref (IDIO bs, size_t const bit)
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (bitset, bs);

    if (bit >= IDIO_BITSET_SIZE (bs)) {
	/*
	 * Test Case: bitset-errors/bitset-set-bounds.idio
	 *
	 * bs := #B{ 3 }
	 * bitset-ref bs 5
	 */
	idio_bitset_bounds_error (bit, IDIO_BITSET_SIZE (bs), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    size_t i = bit / IDIO_BITSET_BITS_PER_WORD;

    if (IDIO_BITSET_WORDS (bs, i) & (1UL << (bit % IDIO_BITSET_BITS_PER_WORD))) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_copy_bitset (IDIO obs)
{
    IDIO_ASSERT (obs);

    IDIO_TYPE_ASSERT (bitset, obs);

    size_t size = IDIO_BITSET_SIZE (obs);

    IDIO nbs = idio_bitset (size);

    size_t n = size / IDIO_BITSET_BITS_PER_WORD + 1;
    size_t i;
    for (i = 0; i < n; i++) {
	IDIO_BITSET_WORDS (nbs, i) = IDIO_BITSET_WORDS (obs, i);
    }

    return nbs;
}

IDIO_DEFINE_PRIMITIVE1_DS ("bitset?", bitset_p, (IDIO o), "o", "\
test if `o` is an bitset				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an bitset, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_bitset (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("make-bitset", make_bitset, (IDIO size), "size", "\
create an bitset with a size of `size`		\n\
						\n\
:param size: initial bitset size		\n\
:type size: integer				\n\
:rtype: bitset					\n\
")
{
    IDIO_ASSERT (size);

    ptrdiff_t bs_size = -1;

    if (idio_isa_fixnum (size)) {
	bs_size = IDIO_FIXNUM_VAL (size);
    } else if (idio_isa_bignum (size)) {
	if (IDIO_BIGNUM_INTEGER_P (size)) {
	    /*
	     * Code coverage: requires a large bitset
	     */
	    bs_size = idio_bignum_ptrdiff_t_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		/*
		 * Test Case: bitset-errors/make-bitset-size-float.idio
		 *
		 * make-bitset 1.1
		 */
		idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		/*
		 * Code coverage:
		 *
		 * make-bitset 1e1
		 */
		bs_size = idio_bignum_ptrdiff_t_value (size_i);
	    }
	}
    } else {
	/*
	 * Test Case: bitset-errors/make-bitset-size-not-integer.idio
	 *
	 * make-bitset #f
	 */
	idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_bitset (bs_size);
}

IDIO_DEFINE_PRIMITIVE1_DS ("bitset-size", bitset_size, (IDIO bs), "bs", "\
return the size of bitset `bs`			\n\
						\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (bs);

    /*
     * Test Case: bitset-errors/bitset-size-bad-type.idio
     *
     * bitset-size #t
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);

    return idio_integer (IDIO_BITSET_SIZE (bs));
}

IDIO_DEFINE_PRIMITIVE2_DS ("bitset-set!", bitset_set, (IDIO bs, IDIO bit), "bs bit", "\
set bit `bit` in bitset `bs`			\n\
						\n\
:param bs: bitset				\n\
:type bs: bitset				\n\
:param bit: bit					\n\
:type bit: unicode or integer			\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (bs);
    IDIO_ASSERT (bit);

    /*
     * Test Case: bitset-errors/bitset-set-bad-type.idio
     *
     * bitset-set #t 0
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);

    ptrdiff_t bs_bit = -1;

    if (idio_isa_unicode (bit)) {
	bs_bit = IDIO_UNICODE_VAL (bit);
    } else if (idio_isa_fixnum (bit)) {
	bs_bit = IDIO_FIXNUM_VAL (bit);
    } else if (idio_isa_bignum (bit)) {
	if (IDIO_BIGNUM_INTEGER_P (bit)) {
	    /*
	     * Code coverage: requires a large bitset
	     */
	    bs_bit = idio_bignum_ptrdiff_t_value (bit);
	} else {
	    IDIO bit_i = idio_bignum_real_to_integer (bit);
	    if (idio_S_nil == bit_i) {
		/*
		 * Test Case: bitset-errors/bitset-set-float.idio
		 *
		 * bitset-set! bs 1.1
		 */
		idio_error_param_type ("integer", bit, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		/*
		 * Code coverage:
		 *
		 * bitset-set! 1e1
		 */
		bs_bit = idio_bignum_ptrdiff_t_value (bit_i);
	    }
	}
    } else {
	/*
	 * Test Case: bitset-errors/bitset-set-non-numeric.idio
	 *
	 * bitset-set! bs #f
	 */
	idio_error_param_type ("unicode|integer", bit, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_bitset_set (bs, bs_bit);
}

IDIO_DEFINE_PRIMITIVE2_DS ("bitset-clear!", bitset_clear, (IDIO bs, IDIO bit), "bs bit", "\
clear bit `bit` in bitset `bs`			\n\
						\n\
:param bs: bitset				\n\
:type bs: bitset				\n\
:param bit: bit					\n\
:type bit: unicode or integer			\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (bs);
    IDIO_ASSERT (bit);

    /*
     * Test Case: bitset-errors/bitset-clear-bad-type.idio
     *
     * bitset-clear #t 0
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);

    ptrdiff_t bs_bit = -1;

    if (idio_isa_unicode (bit)) {
	bs_bit = IDIO_UNICODE_VAL (bit);
    } else if (idio_isa_fixnum (bit)) {
	bs_bit = IDIO_FIXNUM_VAL (bit);
    } else if (idio_isa_bignum (bit)) {
	if (IDIO_BIGNUM_INTEGER_P (bit)) {
	    /*
	     * Code coverage: requires a large bitset
	     */
	    bs_bit = idio_bignum_ptrdiff_t_value (bit);
	} else {
	    IDIO bit_i = idio_bignum_real_to_integer (bit);
	    if (idio_S_nil == bit_i) {
		/*
		 * Test Case: bitset-errors/bitset-clear-float.idio
		 *
		 * bitset-clear! bs 1.1
		 */
		idio_error_param_type ("integer", bit, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		/*
		 * Code coverage:
		 *
		 * bitset-clear! 1e1
		 */
		bs_bit = idio_bignum_ptrdiff_t_value (bit_i);
	    }
	}
    } else {
	/*
	 * Test Case: bitset-errors/bitset-clear-non-numeric.idio
	 *
	 * bitset-clear! bs #f
	 */
	idio_error_param_type ("unicode|integer", bit, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_bitset_clear (bs, bs_bit);
}

IDIO_DEFINE_PRIMITIVE2_DS ("bitset-ref", bitset_ref, (IDIO bs, IDIO bit), "bs bit", "\
get bit `bit` in bitset `bs`			\n\
						\n\
:param bs: bitset				\n\
:type bs: bitset				\n\
:param bit: bit					\n\
:type bit: unicode or integer			\n\
:rtype: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (bs);
    IDIO_ASSERT (bit);

    /*
     * Test Case: bitset-errors/bitset-ref-bad-type.idio
     *
     * bitset-ref #t 0
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);

    ptrdiff_t bs_bit = -1;

    if (idio_isa_unicode (bit)) {
	bs_bit = IDIO_UNICODE_VAL (bit);
    } else if (idio_isa_fixnum (bit)) {
	bs_bit = IDIO_FIXNUM_VAL (bit);
    } else if (idio_isa_bignum (bit)) {
	if (IDIO_BIGNUM_INTEGER_P (bit)) {
	    /*
	     * Code coverage: requires a large bitset
	     */
	    bs_bit = idio_bignum_ptrdiff_t_value (bit);
	} else {
	    IDIO bit_i = idio_bignum_real_to_integer (bit);
	    if (idio_S_nil == bit_i) {
		/*
		 * Test Case: bitset-errors/bitset-ref-float.idio
		 *
		 * bitset-ref bs 1.1
		 */
		idio_error_param_type ("integer", bit, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		/*
		 * Code coverage:
		 *
		 * bitset-ref 1e1
		 */
		bs_bit = idio_bignum_ptrdiff_t_value (bit_i);
	    }
	}
    } else {
	/*
	 * Test Case: bitset-errors/bitset-ref-non-numeric.idio
	 *
	 * bitset-ref bs #f
	 */
	idio_error_param_type ("unicode|integer", bit, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_bitset_ref (bs, bs_bit);
}

IDIO_DEFINE_PRIMITIVE1_DS ("copy-bitset", copy_bitset, (IDIO bs), "bs", "\
copy the bitset					\n\
						\n\
:param args: bitset to be copied		\n\
:type args: bitset				\n\
:rtype: bitset					\n\
")
{
    IDIO_ASSERT (bs);

    /*
     * Test Case: bitset-errors/copy-bitset-bad-type.idio
     *
     * copy-bitset #t
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);

    return idio_copy (bs, IDIO_COPY_SHALLOW);
}

IDIO_DEFINE_PRIMITIVE0V_DS ("merge-bitset", merge_bitset, (IDIO args), "[bs ...]", "\
merge the bitsets				\n\
						\n\
:param args: bitsets to be merged		\n\
:type args: list				\n\
:rtype: bitset or ``#n`` if no bitsets supplied	\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_S_nil;

    while (idio_S_nil != args) {
	IDIO bs = IDIO_PAIR_H (args);

	/*
	 * Test Case: bitset-errors/merge-bitset-bad-arg-type.idio
	 *
	 * merge-bitset #t
	 */
	IDIO_USER_TYPE_ASSERT (bitset, bs);

	if (idio_S_nil == r) {
	    r = idio_copy (bs, IDIO_COPY_SHALLOW);
	} else {
	    size_t bs_size = IDIO_BITSET_SIZE (bs);

	    if (bs_size != IDIO_BITSET_SIZE (r)) {
		/*
		 * Test Case: bitset-errors/merge-bitset-non-matching-sizes.idio
		 *
		 * merge-bitset #B{ 3 } #B{ 4 }
		 */
		idio_bitset_size_mismatch_error (bs_size, IDIO_BITSET_SIZE (r), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
	    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
	    if (excess) {
		n_ul += 1;
	    }
	    size_t i;
	    for (i = 0; i < n_ul; i++) {
		IDIO_BITSET_WORDS (r, i) |= IDIO_BITSET_WORDS (bs, i);
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("and-bitset", and_bitset, (IDIO args), "[bs ...]", "\
logical AND the bitsets				\n\
						\n\
:param args: bitsets to be operated on		\n\
:type args: list				\n\
:rtype: bitset or ``#n`` if no bitsets supplied	\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_S_nil;

    while (idio_S_nil != args) {
	IDIO bs = IDIO_PAIR_H (args);

	/*
	 * Test Case: bitset-errors/and-bitset-bad-arg-type.idio
	 *
	 * and-bitset #t
	 */
	IDIO_USER_TYPE_ASSERT (bitset, bs);

	if (idio_S_nil == r) {
	    r = idio_copy (bs, IDIO_COPY_SHALLOW);
	} else {
	    size_t bs_size = IDIO_BITSET_SIZE (bs);

	    if (bs_size != IDIO_BITSET_SIZE (r)) {
		/*
		 * Test Case: bitset-errors/and-bitset-non-matching-sizes.idio
		 *
		 * and-bitset #B{ 3 } #B{ 4 }
		 */
		idio_bitset_size_mismatch_error (bs_size, IDIO_BITSET_SIZE (r), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
	    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
	    if (excess) {
		n_ul += 1;
	    }
	    size_t i;
	    for (i = 0; i < n_ul; i++) {
		IDIO_BITSET_WORDS (r, i) &= IDIO_BITSET_WORDS (bs, i);
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("ior-bitset", ior_bitset, (IDIO args), "[bs ...]", "\
logical Inclusive OR the bitsets		\n\
						\n\
:param args: bitsets to be operated on		\n\
:type args: list				\n\
:rtype: bitset or ``#n`` if no bitsets supplied	\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_S_nil;

    while (idio_S_nil != args) {
	IDIO bs = IDIO_PAIR_H (args);

	/*
	 * Test Case: bitset-errors/ior-bitset-bad-arg-type.idio
	 *
	 * ior-bitset #t
	 */
	IDIO_USER_TYPE_ASSERT (bitset, bs);

	if (idio_S_nil == r) {
	    r = idio_copy (bs, IDIO_COPY_SHALLOW);
	} else {
	    size_t bs_size = IDIO_BITSET_SIZE (bs);

	    if (bs_size != IDIO_BITSET_SIZE (r)) {
		/*
		 * Test Case: bitset-errors/ior-bitset-non-matching-sizes.idio
		 *
		 * ior-bitset #B{ 3 } #B{ 4 }
		 */
		idio_bitset_size_mismatch_error (bs_size, IDIO_BITSET_SIZE (r), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
	    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
	    if (excess) {
		n_ul += 1;
	    }
	    size_t i;
	    for (i = 0; i < n_ul; i++) {
		IDIO_BITSET_WORDS (r, i) |= IDIO_BITSET_WORDS (bs, i);
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("xor-bitset", xor_bitset, (IDIO args), "[bs ...]", "\
logical eXclusive OR the bitsets		\n\
						\n\
:param args: bitsets to be operated on		\n\
:type args: list				\n\
:rtype: bitset or ``#n`` if no bitsets supplied	\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_S_nil;

    while (idio_S_nil != args) {
	IDIO bs = IDIO_PAIR_H (args);

	/*
	 * Test Case: bitset-errors/xor-bitset-bad-arg-type.idio
	 *
	 * xor-bitset #t
	 */
	IDIO_USER_TYPE_ASSERT (bitset, bs);

	if (idio_S_nil == r) {
	    r = idio_copy (bs, IDIO_COPY_SHALLOW);
	} else {
	    size_t bs_size = IDIO_BITSET_SIZE (bs);

	    if (bs_size != IDIO_BITSET_SIZE (r)) {
		/*
		 * Test Case: bitset-errors/xor-bitset-non-matching-sizes.idio
		 *
		 * xor-bitset #B{ 3 } #B{ 4 }
		 */
		idio_bitset_size_mismatch_error (bs_size, IDIO_BITSET_SIZE (r), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
	    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
	    if (excess) {
		n_ul += 1;
	    }
	    size_t i;
	    for (i = 0; i < n_ul; i++) {
		IDIO_BITSET_WORDS (r, i) ^= IDIO_BITSET_WORDS (bs, i);
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO idio_not_bitset (IDIO bs)
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (bitset, bs);

    IDIO r = idio_copy (bs, IDIO_COPY_SHALLOW);

    size_t bs_size = IDIO_BITSET_SIZE (bs);

    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
    if (excess) {
	n_ul += 1;
    }
    size_t i;
    for (i = 0; i < n_ul; i++) {
	IDIO_BITSET_WORDS (r, i) = ~ IDIO_BITSET_WORDS (bs, i);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("not-bitset", not_bitset, (IDIO bs), "bs", "\
logical complement of the bitset		\n\
						\n\
:param args: bitset to be operated on		\n\
:type args: bitset				\n\
:rtype: bitset					\n\
")
{
    IDIO_ASSERT (bs);

    /*
     * Test Case: bitset-errors/not-bitset-bad-arg-type.idio
     *
     * not-bitset #t
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);

    return idio_not_bitset (bs);
}

IDIO_DEFINE_PRIMITIVE0V_DS ("subtract-bitset", subtract_bitset, (IDIO args), "[bs ...]", "\
subtract the bitsets				\n\
						\n\
:param args: bitsets to be operated on		\n\
:type args: list				\n\
:rtype: bitset or ``#n`` if no bitsets supplied	\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_S_nil;

    while (idio_S_nil != args) {
	IDIO bs = IDIO_PAIR_H (args);

	/*
	 * Test Case: bitset-errors/subtract-bitset-bad-arg-type.idio
	 *
	 * subtract-bitset #t
	 */
	IDIO_USER_TYPE_ASSERT (bitset, bs);

	if (idio_S_nil == r) {
	    r = idio_copy (bs, IDIO_COPY_SHALLOW);
	} else {
	    size_t bs_size = IDIO_BITSET_SIZE (bs);

	    if (bs_size != IDIO_BITSET_SIZE (r)) {
		/*
		 * Test Case: bitset-errors/subtract-bitset-non-matching-sizes.idio
		 *
		 * subtract-bitset #B{ 3 } #B{ 4 }
		 */
		idio_bitset_size_mismatch_error (bs_size, IDIO_BITSET_SIZE (r), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
	    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
	    if (excess) {
		n_ul += 1;
	    }
	    size_t i;
	    for (i = 0; i < n_ul; i++) {
		if (IDIO_BITSET_WORDS (bs, i)) {
		    IDIO_BITSET_WORDS (r, i) &= (~ IDIO_BITSET_WORDS (bs, i));
		}
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return r;
}

int idio_equal_bitsetp (IDIO args)
{
    IDIO_ASSERT (args);

    /*
     * Test Case: ??
     *
     * The user interface passed its varargs argument as {args},
     * ie. always a list, but idio_equal_bitsetp() is also called from
     * idio_equal() leading to a coding error.
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO bs = idio_S_nil;

    if (idio_S_nil != args) {
	bs = IDIO_PAIR_H (args);
	if (idio_isa_bitset (bs) == 0) {
	    /*
	     * Test Case: bitset-errors/bitset-equal-not-bitset-1.idio
	     *
	     * equal-bitset? #f #B{ 3 }
	     *
	     * equal? a b will verify that the types of a and b match
	     * so we need to invoke the bespoke equal-bitset? function
	     * and this clause is for the first argument
	     */
	    idio_error_param_type ("bitset", bs, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return 0;
	}

	args = IDIO_PAIR_T (args);
    }

    while (idio_S_nil != args) {
	IDIO bs2 = IDIO_PAIR_H (args);

	/*
	 * Test Case: bitset-errors/bitset-equal-not-bitset-2.idio
	 *
	 * equal-bitset? #B{ 3 } #f
	 *
	 * equal? a b will verify that the types of a and b match so
	 * we need to invoke the bespoke equal-bitset? function and
	 * this clause is for the second argument
	 */
	IDIO_USER_TYPE_ASSERT (bitset, bs2);

	size_t bs_size = IDIO_BITSET_SIZE (bs);

	if (bs_size != IDIO_BITSET_SIZE (bs2)) {
	    return 0;
	}

	/*
	 * We been a bit[sic] casual in our bit flipping using C's
	 * bitwise primitives.  If the number of bits is not a
	 * multiple of IDIO_BITSET_BITS_PER_WORD then we have no real idea
	 * what the state of the upper bits in the last idio_bitset_word_t
	 * are going to be.
	 *
	 * For example, (not-bitset #B{ 3 110 }) leaves the upper
	 * 29/61 bits in the idio_bitset_word_t as 1 as all not-bitset did
	 * was use C's ~ operator on the whole idio_bitset_word_t.
	 *
	 * The upshot of which is that we can't casually compare
	 * against another bitset, #B{ 3 001 }, say, where the top
	 * bits are (probably) all 0.  We need to do some masking.
	 */
	size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
	int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
	if (excess) {
	    n_ul += 1;
	}
	size_t i;
	for (i = 0; i < n_ul; i++) {
	    if (excess &&
		i == (n_ul - 1)) {
		idio_bitset_word_t mask = (idio_bitset_word_t) -1;
		mask >>= (IDIO_BITSET_BITS_PER_WORD - excess);
		if ((IDIO_BITSET_WORDS (bs, i) & mask ) !=
		    (IDIO_BITSET_WORDS (bs2, i) & mask)) {
		    return 0;
		}
	    } else {
		if (IDIO_BITSET_WORDS (bs, i) != IDIO_BITSET_WORDS (bs2, i)) {
		    /* fprintf (stderr, "bs-eq? %zd %d %#lx %#lx\n", i, IDIO_BITSET_BITS_PER_WORD, IDIO_BITSET_WORDS (bs, i), IDIO_BITSET_WORDS (bs2, i)); */
		    return 0;
		}
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return 1;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("equal-bitset?", equal_bitsetp, (IDIO args), "[bs ...]", "\
are the bitsets equal				\n\
						\n\
:param args: bitsets to be operated on		\n\
:type args: list				\n\
:rtype: bitset or ``#f`` if no bitsets supplied	\n\
")
{
    return idio_equal_bitsetp (args) ? idio_S_true : idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("bitset-for-each-set", bitset_for_each_set, (IDIO bs, IDIO f), "bs f", "\
invoke `f` on each bit in bitset `bs` that is set\n\
						\n\
:param bs: bitset to be operated on		\n\
:type bs: bitset				\n\
:param f: function to invoke on each set bit	\n\
:type f: function of 1 arg			\n\
:rtype: ``#<unspec>``				\n\
						\n\
The argument to `f` will be the index of the bit	\n\
")
{
    IDIO_ASSERT (bs);
    IDIO_ASSERT (f);

    /*
     * Test Case: bitset-errors/bitset-for-each-set-bad-type.idio
     *
     * bitset-for-each-set #t #t
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);
    /*
     * Test Case: bitset-errors/bitset-for-each-set-bad-func-type.idio
     *
     * bitset-for-each-set #B{ 1 } #t
     */
    IDIO_USER_TYPE_ASSERT (function, f);

    IDIO thr = idio_thread_current_thread ();

    size_t bs_size = IDIO_BITSET_SIZE (bs);

    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
    if (excess) {
	n_ul += 1;
    }
    size_t i;
    for (i = 0; i < n_ul; i++) {
	idio_bitset_word_t ul = IDIO_BITSET_WORDS (bs, i);
	if (ul) {
	    size_t j;
	    for (j = 0 ; j < IDIO_BITSET_BITS_PER_WORD; j++) {
		if (ul & (1UL << j)) {
		    IDIO cmd = IDIO_LIST2 (f, idio_fixnum (i * IDIO_BITSET_BITS_PER_WORD + j));

		    idio_vm_invoke_C (thr, cmd);
		}
	    }
	}
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("fold-bitset", fold_bitset, (IDIO bs, IDIO f, IDIO v), "bs f v", "\
invoke `f` on each bit in bitset `bs` that is set\n\
accumulating the result in `v`			\n\
						\n\
:param bs: bitset to be operated on		\n\
:type bs: bitset				\n\
:param f: function to invoke on each set bit	\n\
:type f: function of 2 args			\n\
:param v: accumulated value			\n\
:type v: any					\n\
:return: the accumulated value			\n\
:rtype: any					\n\
						\n\
For each set bit, the arguments to `f` will be	\n\
the index of the bit and `v` and `v` is		\n\
subsequently set to the result of `f`.		\n\
")
{
    IDIO_ASSERT (bs);
    IDIO_ASSERT (f);
    IDIO_ASSERT (v);

    /*
     * Test Case: bitset-errors/fold-bitset-bad-type.idio
     *
     * fold-bitset #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (bitset, bs);
    /*
     * Test Case: bitset-errors/fold-bitset-bad-func-type.idio
     *
     * fold-bitset #B{ 1 } #t #t
     */
    IDIO_USER_TYPE_ASSERT (function, f);

    IDIO thr = idio_thread_current_thread ();

    size_t bs_size = IDIO_BITSET_SIZE (bs);

    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD;
    int excess = bs_size % IDIO_BITSET_BITS_PER_WORD;
    if (excess) {
	n_ul += 1;
    }
    size_t i;
    for (i = 0; i < n_ul; i++) {
	idio_bitset_word_t ul = IDIO_BITSET_WORDS (bs, i);
	if (ul) {
	    size_t j;
	    for (j = 0 ; j < IDIO_BITSET_BITS_PER_WORD; j++) {
		if (ul & (1UL << j)) {
		    IDIO cmd = IDIO_LIST3 (f, idio_fixnum (i * IDIO_BITSET_BITS_PER_WORD + j), v);

		    v = idio_vm_invoke_C (thr, cmd);
		}
	    }
	}
    }

    return v;
}

void idio_bitset_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (bitset_p);
    IDIO_ADD_PRIMITIVE (make_bitset);
    IDIO_ADD_PRIMITIVE (bitset_size);
    IDIO_ADD_PRIMITIVE (bitset_set);
    IDIO_ADD_PRIMITIVE (bitset_clear);
    IDIO_ADD_PRIMITIVE (bitset_ref);
    IDIO_ADD_PRIMITIVE (copy_bitset);
    IDIO_ADD_PRIMITIVE (merge_bitset);
    IDIO_ADD_PRIMITIVE (and_bitset);
    IDIO_ADD_PRIMITIVE (ior_bitset);
    IDIO_ADD_PRIMITIVE (xor_bitset);
    IDIO_ADD_PRIMITIVE (not_bitset);
    IDIO_ADD_PRIMITIVE (subtract_bitset);
    IDIO_ADD_PRIMITIVE (equal_bitsetp);
    IDIO_ADD_PRIMITIVE (bitset_for_each_set);
    IDIO_ADD_PRIMITIVE (fold_bitset);
}

void idio_init_bitset ()
{
    idio_module_table_register (idio_bitset_add_primitives, NULL, NULL);
}

