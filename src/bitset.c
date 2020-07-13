/*
 * Copyright (c) 2020 Ian Fitchet <idf(at)idio-lang.org>
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

#include "idio.h"

static void idio_bitset_error_bounds (size_t bit, size_t size, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    char em[BUFSIZ];

    sprintf (em, "bitset bounds error: %zu >= size %zu", bit, size);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (em, sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_bitset_bounds_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       idio_integer (bit)));

    idio_raise_condition (idio_S_true, c);
}

IDIO idio_bitset (size_t size)
{
    IDIO bs = idio_gc_get (IDIO_TYPE_BITSET);

    IDIO_BITSET_SIZE (bs) = size;
    bs->u.bitset.bits = NULL;
    
    if (size) {
	size_t n = size / IDIO_BITS_PER_LONG + 1;
	IDIO_GC_ALLOC (bs->u.bitset.bits, n * sizeof (unsigned long));
	memset (bs->u.bitset.bits, 0UL, n * sizeof (unsigned long));
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

    idio_gc_stats_free (sizeof (idio_bitset_t) + IDIO_BITSET_SIZE (bs) / sizeof (unsigned long) + 1);

    free (bs->u.bitset.bits);
}

IDIO idio_set_bitset (IDIO bs, size_t bit)
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (bitset, bs);

    if (bit >= IDIO_BITSET_SIZE (bs)) {
	idio_bitset_error_bounds (bit, IDIO_BITSET_SIZE (bs), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    size_t i = bit / IDIO_BITS_PER_LONG;

    IDIO_BITSET_BITS (bs, i) |= 1UL << (bit % IDIO_BITS_PER_LONG);
    
    return idio_S_unspec;
}

IDIO idio_get_bitset (IDIO bs, size_t bit)
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (bitset, bs);

    if (bit >= IDIO_BITSET_SIZE (bs)) {
	idio_bitset_error_bounds (bit, IDIO_BITSET_SIZE (bs), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;
    
    size_t i = bit / IDIO_BITS_PER_LONG;

    if (IDIO_BITSET_BITS (bs, i) & (1UL << (bit % IDIO_BITS_PER_LONG))) {
	r = idio_S_true;
    }
    
    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("bitset?", bitset_p, (IDIO o), "o", "\
test if `o` is an bitset				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an bitset, #f otherwise	\n\
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
	    bs_size = idio_bignum_ptrdiff_value (size);
	} else {
	    IDIO size_i = idio_bignum_real_to_integer (size);
	    if (idio_S_nil == size_i) {
		idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		bs_size = idio_bignum_ptrdiff_value (size_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", size, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_bitset (bs_size);
}

IDIO_DEFINE_PRIMITIVE1_DS ("bitset-size", bitset_size, (IDIO bs), "bs", "\
return the size of bitset `bs`			\n\
						\n\
:param bs: bitset				\n\
:type size: bitset				\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (bitset, bs);

    return idio_integer (IDIO_BITSET_SIZE (bs));
}

IDIO_DEFINE_PRIMITIVE2_DS ("set-bitset", set_bitset, (IDIO bs, IDIO bit), "bs bit", "\
set bit `bit` in bitset `bs`			\n\
						\n\
:param size: bitset				\n\
:type size: bitset				\n\
:param size: bit				\n\
:type size: integer				\n\
:rtype: bitset					\n\
")
{
    IDIO_ASSERT (bs);
    IDIO_ASSERT (bit);

    IDIO_TYPE_ASSERT (bitset, bs);

    ptrdiff_t bs_bit = -1;

    if (idio_isa_fixnum (bit)) {
	bs_bit = IDIO_FIXNUM_VAL (bit);
    } else if (idio_isa_bignum (bit)) {
	if (IDIO_BIGNUM_INTEGER_P (bit)) {
	    bs_bit = idio_bignum_ptrdiff_value (bit);
	} else {
	    IDIO bit_i = idio_bignum_real_to_integer (bit);
	    if (idio_S_nil == bit_i) {
		idio_error_param_type ("integer", bit, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		bs_bit = idio_bignum_ptrdiff_value (bit_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", bit, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_set_bitset (bs, bs_bit);
}

IDIO_DEFINE_PRIMITIVE2_DS ("get-bitset", get_bitset, (IDIO bs, IDIO bit), "bs bit", "\
get bit `bit` in bitset `bs`			\n\
						\n\
:param size: bitset				\n\
:type size: bitset				\n\
:param size: bit				\n\
:type size: integer				\n\
:rtype: bitset					\n\
")
{
    IDIO_ASSERT (bs);
    IDIO_ASSERT (bit);

    IDIO_TYPE_ASSERT (bitset, bs);

    ptrdiff_t bs_bit = -1;

    if (idio_isa_fixnum (bit)) {
	bs_bit = IDIO_FIXNUM_VAL (bit);
    } else if (idio_isa_bignum (bit)) {
	if (IDIO_BIGNUM_INTEGER_P (bit)) {
	    bs_bit = idio_bignum_ptrdiff_value (bit);
	} else {
	    IDIO bit_i = idio_bignum_real_to_integer (bit);
	    if (idio_S_nil == bit_i) {
		idio_error_param_type ("integer", bit, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		bs_bit = idio_bignum_ptrdiff_value (bit_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", bit, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_get_bitset (bs, bs_bit);
}

void idio_init_bitset ()
{
}

void idio_bitset_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (bitset_p);
    IDIO_ADD_PRIMITIVE (make_bitset);
    IDIO_ADD_PRIMITIVE (bitset_size);
    IDIO_ADD_PRIMITIVE (set_bitset);
    IDIO_ADD_PRIMITIVE (get_bitset);
}

void idio_final_bitset ()
{
}