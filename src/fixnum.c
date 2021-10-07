/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * fixnum.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <errno.h>
#include <ffi.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "bignum.h"
#include "c-type.h"
#include "character.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "handle.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"

static void idio_fixnum_divide_by_zero_error (IDIO nums, IDIO c_location)
{
    IDIO_ASSERT (nums);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_divide_by_zero ("fixnum divide by zero", nums, c_location);

    /* notreached */
}

static void idio_fixnum_conversion_error (char const *msg, IDIO num, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (num);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_fixnum_conversion_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       num));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_fixnum_number_error (char const *msg, IDIO num, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (num);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_fixnum_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       num));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

IDIO idio_integer (intmax_t const i)
{
    if (i <= IDIO_FIXNUM_MAX &&
	i >= IDIO_FIXNUM_MIN) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return IDIO_FIXNUM ((intptr_t) i);
    } else {
	return idio_bignum_integer_intmax_t (i);
    }
}

/*
 * Code coverage:
 *
 * This is called from ->integer in c-type.c
 */
IDIO idio_uinteger (uintmax_t const ui)
{
    if (ui <= IDIO_FIXNUM_MAX) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return IDIO_FIXNUM ((uintptr_t) ui);
    } else {
	return idio_bignum_integer_uintmax_t (ui);
    }
}

IDIO idio_fixnum (intptr_t const i)
{
    if (i <= IDIO_FIXNUM_MAX &&
	i >= IDIO_FIXNUM_MIN) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return IDIO_FIXNUM (i);
    } else {
	/*
	 * Test Case: ??
	 *
	 * I think this requires a coding error.
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "%" PRIdPTR " too large", i);

	IDIO_C_ASSERT (0);
	idio_fixnum_conversion_error (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

IDIO idio_fixnum_C (char const *str, int const base)
{
    char *end;
    errno = 0;
    long long int val = strtoll (str, &end, base);

    if ((errno == ERANGE &&
	 (val == LLONG_MAX ||
	  val == LLONG_MIN)) ||
	(errno != 0 &&
	 val == 0)) {
	/*
	 * Test Case: ??
	 *
	 * This requires a coding error.
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "idio_fixnum_C: strtoll (%s) = %lld", str, val);

	idio_fixnum_conversion_error (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (end == str) {
	/*
	 * Test Case: ??
	 *
	 * This requires a coding error.
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "idio_fixnum_C: strtoll (%s): No digits?", str);

	idio_fixnum_conversion_error (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if ('\0' == *end) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return idio_fixnum ((intptr_t) val);
    } else {
	/*
	 * Test Case: ??
	 *
	 * This requires a coding error.
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "idio_fixnum_C: strtoll (%s) = %lld", str, val);

	idio_fixnum_conversion_error (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

int idio_isa_fixnum (IDIO o)
{
    IDIO_ASSERT (o);

    return (((intptr_t) o & IDIO_TYPE_MASK) == IDIO_TYPE_FIXNUM_MARK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("fixnum?", fixnump, (IDIO o), "o", "\
test if `o` is a fixnum				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an fixnum, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_fixnum (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_integer (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_fixnum (o)) {
	return 1;
    } else if (idio_isa_bignum (o)) {
	if (IDIO_BIGNUM_INTEGER_P (o)) {
	    return 1;
	} else {
	    IDIO i = idio_bignum_real_to_integer (o);
	    if (idio_S_nil != i) {
		return 1;
	    }
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("integer?", integerp, (IDIO o), "o", "\
test if `o` is an integer			\n\
						\n\
a fixnum or an integer bignum			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an integer, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_integer (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_number (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_fixnum (o)) {
	return 1;
    } else if (idio_isa_bignum (o)) {
	return 1;
    } else if (idio_isa_C_number (o)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("number?", numberp, (IDIO o), "o", "\
test if `o` is a number				\n\
						\n\
fixnum or bignum				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an number, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_number (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_fixnum_primitive_add (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    intptr_t ir = 0;

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	IDIO_TYPE_ASSERT (fixnum, h);

	intptr_t ih = IDIO_FIXNUM_VAL (h);

	ir = ir + ih;
	if (ir > IDIO_FIXNUM_MAX ||
	    ir < IDIO_FIXNUM_MIN) {
	    /*
	     * Code coverage:
	     *
	     * To get here, of course, we need to have tripped over
	     * IDIO_FIXNUM_MAX and have more args to come
	     *
	     * + FIXNUM-MAX 1 1
	     */
	    /*
	     * Shift everything to bignums and pass the calculation on
	     * to the bignum code
	     */
	    IDIO bn_args = IDIO_LIST1 (idio_bignum_integer_intmax_t (ir));
	    args = IDIO_PAIR_T (args);
	    while (idio_S_nil != args) {
		IDIO h = IDIO_PAIR_H (args);
		IDIO_TYPE_ASSERT (fixnum, h);

		bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args);
		args = IDIO_PAIR_T (args);
	    }

	    return idio_bignum_primitive_add (idio_list_reverse (bn_args));
	}

	args = IDIO_PAIR_T (args);
    }

    return idio_fixnum (ir);
}

IDIO idio_fixnum_primitive_subtract (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    intptr_t ir = 0;

    int first = 1;
    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	IDIO_TYPE_ASSERT (fixnum, h);

	intptr_t ih = IDIO_FIXNUM_VAL (h);

	if (first) {
	    first = 0;

	    /*
	      a bit of magic for subtract:

	      (- 6)   => 0-6 => -6
	      (- 6 2) => 6-2 => 4
	    */

	    IDIO t = IDIO_PAIR_T (args);
	    if (idio_S_nil == t) {
		ir = -ih;
		break;
	    } else {
		ir = ih;
		args = t;
		continue;
	    }
	}

	ir = ir - ih;
	if (ir > IDIO_FIXNUM_MAX ||
	    ir < IDIO_FIXNUM_MIN) {
	    /*
	     * Shift everything to bignums and pass the calculation on
	     * to the bignum code
	     */
	    /*
	     * Special case, FIXNUM-MIN - 2, where ir is now out of
	     * bounds so we gather ir and the remaining args into
	     * bn_args and call idio_bignum_primitive_subtract().
	     * Except bn_args is a single number and (- N) is -N,
	     * ie. we'll negate our out of bounds number.
	     */
	    IDIO t = IDIO_PAIR_T (args);
	    if (idio_S_nil == t) {
		return idio_bignum_integer_intmax_t (ir);
	    } else {
		IDIO bn_args = IDIO_LIST1 (idio_bignum_integer_intmax_t (ir));
		args = t;
		while (idio_S_nil != args) {
		    IDIO h = IDIO_PAIR_H (args);
		    IDIO_TYPE_ASSERT (fixnum, h);

		    bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args);
		    args = IDIO_PAIR_T (args);
		}

		return idio_bignum_primitive_subtract (idio_list_reverse (bn_args));
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    /*
     * Careful: (- FIXNUM-MIN) -- ie a single argument -- can still be
     * out of bounds for a fixnum as we did ir = -ih, above
     */
    if (ir > IDIO_FIXNUM_MAX ||
	ir < IDIO_FIXNUM_MIN) {
	return idio_bignum_integer_intmax_t (ir);
    } else {
	return idio_fixnum (ir);
    }
}

#ifdef __LP64__
#define IDIO_FIXNUM_HALFWORD_MASK (~ 0xffffffffL)
#else
#define IDIO_FIXNUM_HALFWORD_MASK (~ 0xffffL)
#endif

IDIO idio_fixnum_primitive_multiply (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    intptr_t ir = 1;

    int first = 1;
    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	IDIO_TYPE_ASSERT (fixnum, h);

	intptr_t ih = IDIO_FIXNUM_VAL (h);

	if (first) {
	    first = 0;

	    IDIO t = IDIO_PAIR_T (args);
	    if (idio_S_nil == t) {
		ir = ih;
		break;
	    } else {
		ir = ih;
		args = t;
		continue;
	    }
	}

	/*
	 * Prechecks for potential multiplication overflow.
	 *
	 * NB According to the C specification, if integer
	 * multiplication overflowed the resultant value is
	 * *undefined*, ie. you cannot say ir = ir * ih and then use
	 * ir for any kind of test.  You have to check before you
	 * multiply.
	 *
	 * Or you could add in some non-portable ASM to dig out the
	 * overflow flow from the CPU...
	 *
	 * if the MSB of ir is mr and the MSB of ih is mh then the MSB
	 * of ir*ih will be at most mr+mh.
	 *
	 * If both mr and mh are less than half a wordsize then we cannot
	 * overflow.
	 *
	 *
	 * MSB (3*3) == MSB (9) == MSB (8+1) == 3
	 * is <=
	 * MSB (3) + MSB (3) == MSB (2+1) + MSB (2+1) == 2 + 2 == 4
	 *
	 * MSB (3*9) == MSB (27) == MSB (16+8+2+1) == 5
	 * is <=
	 * MSB(3) + MSB (9) == MSB (2+1) + MSB (8+1) == 2 + 4 == 6
	 *
	 */
	if (ir & IDIO_FIXNUM_HALFWORD_MASK ||
	    ih & IDIO_FIXNUM_HALFWORD_MASK) {
	    /*
	     * We *could* overflow as one of mr/mh is large -- time to
	     * do some legwork
	     */
	    int bits = 0;
	    intptr_t itmp = ir;
	    if (itmp < 0) {
		itmp = -itmp;
	    }
	    while (itmp >>= 1) {
		bits++;
	    }
	    itmp = ih;
	    if (itmp < 0) {
		itmp = -itmp;
	    }
	    while (itmp >>= 1) {
		bits++;
	    }

	    if (bits >= ((sizeof (intptr_t) * 8) - 2)) {
		/*
		 * Definitely overflowed!  Probably.
		 */
		IDIO bn_args = IDIO_LIST2 (idio_bignum_integer_intmax_t (ir), idio_bignum_integer_intmax_t (ih));
		args = IDIO_PAIR_T (args);
		while (idio_S_nil != args) {
		    /*
		     * Code coverage:
		     *
		     * We need to have overflowed and have at least
		     * three args.
		     *
		     * * FIXNUM-MAX FIXNUM-MAX FIXNUM-MAX
		     */
		    IDIO h = IDIO_PAIR_H (args);
		    IDIO_TYPE_ASSERT (fixnum, h);

		    bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args);
		    args = IDIO_PAIR_T (args);
		}

		bn_args = idio_list_reverse (bn_args);
		return idio_bignum_primitive_multiply (bn_args);
	    } else {
		ir = ir * ih;
	    }
	} else {
	    ir = ir * ih;
	}

	if (ir > IDIO_FIXNUM_MAX ||
	    ir < IDIO_FIXNUM_MIN) {
	    /*
	     * Shift everything to bignums and pass the calculation on
	     * to the bignum code
	     */
	    IDIO bn_args = IDIO_LIST1 (idio_bignum_integer_intmax_t (ir));
	    args = IDIO_PAIR_T (args);
	    while (idio_S_nil != args) {
		IDIO h = IDIO_PAIR_H (args);
		IDIO_TYPE_ASSERT (fixnum, h);

		bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args);
		args = IDIO_PAIR_T (args);
	    }

	    bn_args = idio_list_reverse (bn_args);
	    return idio_bignum_primitive_multiply (bn_args);
	}

	args = IDIO_PAIR_T (args);
    }

    return idio_fixnum (ir);
}

IDIO idio_fixnum_primitive_floor (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (fixnum, a);

    return a;
}

IDIO_DEFINE_PRIMITIVE1_DS ("floor", floor, (IDIO a), "a", "\
return the floor of `a`				\n\
						\n\
:param a: number				\n\
:type a: fixnum or bignum			\n\
:return: floor of `a`				\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (a);

    if (idio_isa_fixnum (a)) {
	return idio_fixnum_primitive_floor (a);
    } else if (idio_isa_bignum (a)) {
	return idio_bignum_primitive_floor (a);
    } else {
	/*
	 * Test Case: fixnum-errors/floor-bad-type.idio
	 *
	 * floor #t
	 */
	idio_error_param_type ("number", a, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

IDIO idio_fixnum_primitive_remainder (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (fixnum, a);
    IDIO_TYPE_ASSERT (fixnum, b);

    intptr_t ib = IDIO_FIXNUM_VAL (b);

    if (0 == ib) {
	/*
	 * Test Case: fixnum-errors/remainder-divide-by-zero.idio
	 *
	 * remainder 1 0
	 */
	idio_fixnum_divide_by_zero_error (IDIO_LIST2 (a, b), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_fixnum (IDIO_FIXNUM_VAL (a) % ib);
}

IDIO_DEFINE_PRIMITIVE2_DS ("remainder", remainder, (IDIO a, IDIO b), "a b", "\
return the remainder of `a` less `floor (b)`	\n\
						\n\
:param a: number				\n\
:type a: fixnum or bignum			\n\
:param b: number				\n\
:type b: fixnum or bignum			\n\
:return: remainder of `a` modulo `floor(b)`	\n\
:rtype: fixnum or bignum			\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO num = idio_S_unspec;

    if (idio_isa_fixnum (a)) {
	if (idio_isa_fixnum (b)) {
	    return idio_fixnum_primitive_remainder (a, b);
	} if (idio_isa_bignum (b)) {
	    num = idio_bignum_primitive_remainder (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (a)), b);
	} else {
	    /*
	     * Test Case: fixnum-errors/remainder-fixnum-bad-type.idio
	     *
	     * remainder 1 #t
	     */
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_isa_bignum (a)) {
	if (idio_isa_fixnum (b)) {
	    num = idio_bignum_primitive_remainder (a, idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (b)));
	} else if (idio_isa_bignum (b)) {
	    num = idio_bignum_primitive_remainder (a, b);
	} else {
	    /*
	     * Test Case: fixnum-errors/remainder-bignum-bad-type.idio
	     *
	     * remainder 1.0 #t
	     */
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: fixnum-errors/remainder-bad-type.idio
	 *
	 * remainder #t 1
	 */
	idio_error_param_type ("number", a, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /* convert to a fixnum if possible */
    IDIO fn = idio_bignum_to_fixnum (num);
    if (idio_S_nil != fn) {
	num = fn;
    }

    return num;
}

IDIO idio_fixnum_primitive_quotient (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (fixnum, a);
    IDIO_TYPE_ASSERT (fixnum, b);

    intptr_t ib = IDIO_FIXNUM_VAL (b);

    if (0 == ib) {
	/*
	 * Test Case: fixnum-errors/quotient-divide-by-zero.idio
	 *
	 * quotient 1 0
	 */
	idio_fixnum_divide_by_zero_error (IDIO_LIST2 (a, b), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_fixnum (IDIO_FIXNUM_VAL (a) / ib);
}

IDIO_DEFINE_PRIMITIVE2_DS ("quotient", quotient, (IDIO a, IDIO b), "a b", "\
return the quotient `a / b`			\n\
						\n\
:param a: number				\n\
:type a: fixnum or bignum			\n\
:param b: number				\n\
:type b: fixnum or bignum			\n\
:return: quotient of `a / b`			\n\
:rtype: fixnum or bignum			\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO num = idio_S_unspec;

    if (idio_isa_fixnum (a)) {
	if (idio_isa_fixnum (b)) {
	    return idio_fixnum_primitive_quotient (a, b);
	} else if (idio_isa_bignum (b)) {
	    num = idio_bignum_primitive_quotient (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (a)), b);
	} else {
	    /*
	     * Test Case: fixnum-errors/quotient-fixnum-bad-type.idio
	     *
	     * quotient 1 #t
	     */
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_isa_bignum (a)) {
	if (idio_isa_fixnum (b)) {
	    num = idio_bignum_primitive_quotient (a, idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (b)));
	} else if (idio_isa_bignum (b)) {
	    num = idio_bignum_primitive_quotient (a, b);
	} else {
	    /*
	     * Test Case: fixnum-errors/quotient-bignum-bad-type.idio
	     *
	     * quotient 1.0 #t
	     */
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: fixnum-errors/quotient-bad-type.idio
	 *
	 * quotient #t 1
	 */
	idio_error_param_type ("number", a, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /* convert to a fixnum if possible */
    IDIO fn = idio_bignum_to_fixnum (num);
    if (idio_S_nil != fn) {
	num = fn;
    }

    return num;
}

/*
 * First up define some fixnum comparators
 */
#define IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(cname,cmp)			\
    IDIO idio_fixnum_primitive_ ## cname (IDIO args)			\
    {									\
	IDIO_ASSERT (args);						\
	IDIO_TYPE_ASSERT (pair, args);					\
									\
	IDIO c = IDIO_PAIR_H (args);					\
									\
	args = IDIO_PAIR_T (args);					\
									\
	while (idio_S_nil != args) {					\
	    IDIO h = IDIO_PAIR_H (args);				\
									\
	    if (! (IDIO_FIXNUM_VAL (c) cmp IDIO_FIXNUM_VAL (h))) {	\
		return idio_S_false;					\
	    }								\
									\
	    c = h;							\
	    args = IDIO_PAIR_T (args);					\
	}								\
									\
	return idio_S_true;						\
    }

IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(lt, <)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(le, <=)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(eq, ==)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(ge, >=)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(gt, >)

/*
 * Second, define some generic primitives that look out for bignum and
 * C type arguments
 */
#define IDIO_DEFINE_ARITHMETIC_PRIMITIVE0V(name,cname)			\
    IDIO_DEFINE_PRIMITIVE0V (name, cname, (IDIO args))			\
    {									\
	IDIO_ASSERT (args);						\
									\
	int ibn = 0;							\
									\
        IDIO a = args;							\
									\
	while (idio_S_nil != a) {					\
	    IDIO h = IDIO_PAIR_H (a);					\
	    ibn = idio_isa_bignum (h);					\
									\
	    if (ibn) {							\
		break;							\
	    } else {							\
		if (! idio_isa_fixnum (h)) {				\
		    idio_error_param_type ("number", h, idio_string_C ("arithmetic " name)); \
		    return idio_S_notreached;				\
		}							\
	    }								\
									\
	    a = IDIO_PAIR_T (a);					\
	}								\
									\
	if (ibn) {							\
	    IDIO bn_args = idio_S_nil;					\
									\
	    while (idio_S_nil != args) {				\
		IDIO h = IDIO_PAIR_H (args);				\
									\
		if (idio_isa_fixnum (h)) {				\
		    bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args); \
		} else {						\
		    bn_args = idio_pair (h, bn_args);			\
		}							\
									\
		args = IDIO_PAIR_T (args);				\
	    }								\
									\
	    bn_args = idio_list_reverse (bn_args);			\
	    IDIO num = idio_bignum_primitive_ ## cname (bn_args);	\
									\
	    /* convert to a fixnum if possible */			\
	    IDIO fn = idio_bignum_to_fixnum (num);			\
	    if (idio_S_nil != fn) {					\
		num = fn;						\
	    }								\
									\
	    return num;							\
	} else {							\
	    return idio_fixnum_primitive_ ## cname (args);		\
        }								\
    }

#define IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V(name,cname)			\
    IDIO_DEFINE_PRIMITIVE1V (name, cname, (IDIO n1, IDIO args))		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (args);						\
									\
	int ibn = 0;							\
	if (! idio_isa_fixnum (n1)) {					\
	    ibn = idio_isa_bignum (n1);					\
									\
	    if (0 == ibn) {						\
		idio_error_param_type ("number", n1, idio_string_C ("arithmetic " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	if (0 == ibn) {							\
	    IDIO a = args;						\
									\
	    while (idio_S_nil != a) {					\
		IDIO h = IDIO_PAIR_H (a);				\
		ibn = idio_isa_bignum (h);				\
									\
		if (ibn) {						\
		    break;						\
		} else {						\
		    if (! idio_isa_fixnum (h)) {			\
			idio_error_param_type ("number", h, idio_string_C ("arithmetic " name)); \
			return idio_S_notreached;			\
		    }							\
		}							\
									\
		a = IDIO_PAIR_T (a);					\
	    }								\
	}								\
									\
	args = idio_pair (n1, args);					\
									\
	if (ibn) {							\
	    IDIO bn_args = idio_S_nil;					\
									\
	    while (idio_S_nil != args) {				\
		IDIO h = IDIO_PAIR_H (args);				\
									\
		if (idio_isa_fixnum (h)) {				\
		    bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args); \
		} else {						\
		    bn_args = idio_pair (h, bn_args);			\
		}							\
									\
		args = IDIO_PAIR_T (args);				\
	    }								\
	    								\
	    bn_args = idio_list_reverse (bn_args);			\
	    IDIO num = idio_bignum_primitive_ ## cname (bn_args);	\
									\
	    /* convert to a fixnum if possible */			\
	    IDIO fn = idio_bignum_to_fixnum (num);			\
	    if (idio_S_nil != fn) {					\
		num = fn;						\
	    }								\
	    								\
	    return num;							\
	} else {							\
	    return idio_fixnum_primitive_ ## cname (args);		\
        }								\
    }

/*
 * For divide we should always convert fixnums to bignums: 1 / 3 is 0;
 * 9 / 2 is 4 in fixnums; 10 / 2 will be converted back to a fixnum.
 */
#define IDIO_DEFINE_ARITHMETIC_BIGNUM_PRIMITIVE1V(name,cname)		\
    IDIO_DEFINE_PRIMITIVE1V (name, cname, (IDIO n1, IDIO args))		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (args);						\
									\
	args = idio_pair (n1, args);					\
	IDIO a = args;							\
	IDIO bn_args = idio_S_nil;					\
									\
	while (idio_S_nil != a) {					\
	    IDIO h = IDIO_PAIR_H (a);					\
									\
	    if (idio_isa_fixnum (h)) {					\
		bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args); \
	    } else if (idio_isa_bignum (h)) {				\
		bn_args = idio_pair (h, bn_args);			\
	    } else {							\
		idio_error_param_type ("number", h, idio_string_C ("arithmetic bignum " name)); \
		return idio_S_notreached;				\
	    }								\
	    								\
	    a = IDIO_PAIR_T (a);					\
	}								\
	bn_args = idio_list_reverse (bn_args);				\
									\
	IDIO num = idio_bignum_primitive_ ## cname (bn_args);		\
									\
	/* convert to a fixnum if possible */				\
	IDIO fn = idio_bignum_to_fixnum (num);				\
	if (idio_S_nil != fn) {						\
	    num = fn;							\
	}								\
									\
	return num;							\
    }

#define IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V(name,cname)		\
    IDIO_DEFINE_PRIMITIVE1V (name, cname, (IDIO n1, IDIO args))		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (args);						\
									\
	int ibn = 0;							\
	if (! idio_isa_fixnum (n1)) {					\
	    ibn = idio_isa_bignum (n1);					\
									\
	    if (0 == ibn) {						\
		idio_error_param_type ("number", n1, idio_string_C ("arithmetic cmp " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	if (0 == ibn) {							\
	    IDIO a = args;						\
									\
	    while (idio_S_nil != a) {					\
		IDIO h = IDIO_PAIR_H (a);				\
		ibn = idio_isa_bignum (h);				\
									\
		if (ibn) {						\
		    break;						\
		} else {						\
		    if (! idio_isa_fixnum (h)) {			\
			idio_error_param_type ("number", h, idio_string_C ("arithmetic cmp " name)); \
			return idio_S_notreached;			\
		    }							\
		}							\
									\
		a = IDIO_PAIR_T (a);					\
	    }								\
	}								\
									\
	args = idio_pair (n1, args);					\
									\
	if (ibn) {							\
	    IDIO bn_args = idio_S_nil;					\
									\
	    while (idio_S_nil != args) {				\
		IDIO h = IDIO_PAIR_H (args);				\
									\
		if (idio_isa_fixnum (h)) {				\
		    bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args); \
		} else {						\
		    bn_args = idio_pair (h, bn_args);			\
		}							\
									\
		args = IDIO_PAIR_T (args);				\
	    }								\
	    								\
	    bn_args = idio_list_reverse (bn_args);			\
	    return idio_bignum_primitive_ ## cname (bn_args);		\
	} else {							\
	    return idio_fixnum_primitive_ ## cname (args);		\
        }								\
    }


IDIO_DEFINE_ARITHMETIC_PRIMITIVE0V ("+", add)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V ("-", subtract)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE0V ("*", multiply)
IDIO_DEFINE_ARITHMETIC_BIGNUM_PRIMITIVE1V ("/", divide)

IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("lt", lt)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("le", le)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("eq", eq)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("ge", ge)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("gt", gt)

/*
 * In particular the infix operators (a + b) are binary so we can call
 * the *_primitive variant directly and save some list-shuffling in
 * type-checking args.
 *
 * We can even call the underlying implementation directly rather than
 * going through the *_primitive variants (which use lists).
 *
 * Division is always converted to bignums then potentially converted
 * back.
 *
 * Quotient might be a better bet.
 */

#define IDIO_DEFINE_ARITHMETIC_BINARY_PRIMITIVE(name,cname,icname)	\
    IDIO_DEFINE_PRIMITIVE2 (name, cname, (IDIO n1, IDIO n2))		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	if (idio_isa_C_number (n1)) {					\
	    return idio_C_primitive_ ## cname (n1, n2);			\
	}								\
									\
	IDIO args = IDIO_LIST2 (n1, n2);				\
									\
	int ibn = 0;							\
	if (idio_isa_bignum (n1)) {					\
	    ibn |= 1;							\
	} else {							\
	    if (! idio_isa_fixnum (n1)) {				\
		idio_error_param_type ("number", n1, idio_string_C ("binary op " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	if (idio_isa_bignum (n2)) {					\
	    ibn |= 2;							\
	} else {							\
	    if (! idio_isa_fixnum (n2)) {				\
		idio_error_param_type ("number", n2, idio_string_C ("binary op " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	if (ibn) {							\
	    if (0 == (ibn & 1)) {					\
		IDIO_PAIR_H (args) = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n1)); \
	    }								\
	    if (0 == (ibn & 2)) {					\
		IDIO_PAIR_HT (args) = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n2)); \
	    }								\
									\
	    IDIO num = idio_bignum_primitive_ ## icname (args);		\
									\
	    /* convert to a fixnum if possible */			\
	    IDIO fn = idio_bignum_to_fixnum (num);			\
	    if (idio_S_nil != fn) {					\
		num = fn;						\
	    }								\
									\
	    return num;							\
	} else {							\
	    return idio_fixnum_primitive_ ## icname (args);		\
        }								\
    }

#define IDIO_DEFINE_ARITHMETIC_BINDIV_PRIMITIVE(name,cname,icname)	\
    IDIO_DEFINE_PRIMITIVE2 (name, cname, (IDIO n1, IDIO n2))		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	if (idio_isa_C_number (n1)) {					\
	    return idio_C_primitive_ ## cname (n1, n2);			\
	}								\
	int ibn = 0;							\
	if (idio_isa_bignum (n1)) {					\
	    ibn |= 1;							\
	} else {							\
	    if (! idio_isa_fixnum (n1)) {				\
		idio_error_param_type ("number", n1, idio_string_C ("binary op " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	if (idio_isa_bignum (n2)) {					\
	    ibn |= 2;							\
	} else {							\
	    if (! idio_isa_fixnum (n2)) {				\
		idio_error_param_type ("number", n2, idio_string_C ("binary op " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	IDIO bn_args = IDIO_LIST2 (n1, n2);				\
									\
	if (0 == (ibn & 1)) {						\
	    IDIO_PAIR_H (bn_args) = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n1)); \
	}								\
	if (0 == (ibn & 2)) {						\
	    IDIO_PAIR_HT (bn_args) = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n2)); \
	}								\
									\
	IDIO num = idio_bignum_primitive_ ## icname (bn_args);		\
									\
	/* convert to a fixnum if possible */				\
	IDIO fn = idio_bignum_to_fixnum (num);				\
	if (idio_S_nil != fn) {						\
	    num = fn;							\
	}								\
									\
	return num;							\
    }

#define IDIO_DEFINE_ARITHMETIC_BINARY_CMP_PRIMITIVE(name,cname,icname)	\
    IDIO_DEFINE_PRIMITIVE2 (name, cname, (IDIO n1, IDIO n2))		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	int ibn = 0;							\
	if (idio_isa_bignum (n1)) {					\
	    ibn |= 1;							\
	} else {							\
	    if (! idio_isa_fixnum (n1)) {				\
		idio_error_param_type ("number", n1, idio_string_C ("binary op " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	if (idio_isa_bignum (n2)) {					\
	    ibn |= 2;							\
	} else {							\
	    if (! idio_isa_fixnum (n2)) {				\
		idio_error_param_type ("number", n2, idio_string_C ("binary op " name)); \
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	if (ibn) {							\
	    IDIO bn_args = IDIO_LIST2 (n1, n2);				\
									\
	    if (0 == (ibn & 1)) {					\
		IDIO_PAIR_H (bn_args) = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n1)); \
	    }								\
	    if (0 == (ibn & 2)) {					\
		IDIO_PAIR_HT (bn_args) = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n2)); \
	    }								\
									\
	    return idio_bignum_primitive_ ## icname (bn_args);		\
	} else {							\
	    return idio_fixnum_primitive_ ## icname (IDIO_LIST2 (n1, n2)); \
        }								\
    }

IDIO_DEFINE_ARITHMETIC_BINARY_PRIMITIVE ("binary-+", binary_add, add)
IDIO_DEFINE_ARITHMETIC_BINARY_PRIMITIVE ("binary--", binary_subtract, subtract)
IDIO_DEFINE_ARITHMETIC_BINARY_PRIMITIVE ("binary-*", binary_multiply, multiply)
IDIO_DEFINE_ARITHMETIC_BINDIV_PRIMITIVE ("binary-/", binary_divide, divide)

IDIO_DEFINE_ARITHMETIC_BINARY_CMP_PRIMITIVE ("binary-le", binary_le, le)
IDIO_DEFINE_ARITHMETIC_BINARY_CMP_PRIMITIVE ("binary-lt", binary_lt, lt)
IDIO_DEFINE_ARITHMETIC_BINARY_CMP_PRIMITIVE ("binary-eq", binary_eq, eq)
IDIO_DEFINE_ARITHMETIC_BINARY_CMP_PRIMITIVE ("binary-ge", binary_ge, ge)
IDIO_DEFINE_ARITHMETIC_BINARY_CMP_PRIMITIVE ("binary-gt", binary_gt, gt)

IDIO_DEFINE_PRIMITIVE1_DS ("integer->char", integer2char, (IDIO i), "i", "\
[deprecated]					\n\
						\n\
convert integer `i` to a character		\n\
						\n\
:param i: number				\n\
:type i: integer				\n\
:return: character				\n\
:rtype: character				\n\
"
)
{
    IDIO_ASSERT (i);

    IDIO c = idio_S_unspec;

    if (idio_isa_fixnum (i)) {
	c = IDIO_CHARACTER (IDIO_FIXNUM_VAL (i));
    } else if (idio_isa_bignum (i)) {
	intptr_t iv = idio_bignum_intptr_t_value (i);

	if (iv >= 0 &&
	    iv <= IDIO_FIXNUM_MAX) {
	    c = IDIO_CHARACTER (iv);
	}
    } else {
	/*
	 * Test Case: fixnum-errors/integer2char-bad-type.idio
	 *
	 * integer->char #t
	 */
	idio_error_param_type ("integer", i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (! idio_isa_character (c)) {
	/*
	 * Test Case: fixnum-errors/integer2char-bignum-range.idio
	 *
	 * integer->char -1.0
	 */
	idio_fixnum_number_error ("invalid integer", i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return c;
}

IDIO_DEFINE_PRIMITIVE1_DS ("integer->unicode", integer2unicode, (IDIO i), "i", "\
convert integer `i` to a Unicode code point	\n\
						\n\
:param i: number				\n\
:type i: integer				\n\
:return: Unicode code point			\n\
:rtype: unicode					\n\
")
{
    IDIO_ASSERT (i);

    IDIO u = idio_S_unspec;

    if (idio_isa_fixnum (i)) {
	intptr_t iv = IDIO_FIXNUM_VAL (i);

	if (idio_unicode_valid_code_point (iv)) {
	    u = IDIO_UNICODE (iv);
	} else {
	    char em[BUFSIZ];
	    if (iv < 0) {
		/*
		 * Test Case: fixnum-errors/integer2unicode-fixnum-range.idio
		 *
		 * integer->unicode -1
		 */
		idio_snprintf (em, BUFSIZ, "U+%" PRIdPTR ": cannot be negative", iv);
	    } else {
		/*
		 * Test Case: fixnum-errors/integer2unicode-fixnum-invalid-code-point.idio
		 *
		 * integer->unicode #xd800
		 */
		idio_snprintf (em, BUFSIZ, "U+%04" PRIXPTR ": is invalid", iv);
	    }
	    idio_error_param_value_msg_only ("integer->unicode", "code point", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_isa_bignum (i)) {
	intptr_t iv = idio_bignum_intptr_t_value (i);

	if (idio_unicode_valid_code_point (iv)) {
	    u = IDIO_UNICODE (iv);
	} else {
	    char em[BUFSIZ];
	    if (iv < 0) {
		/*
		 * Test Case: fixnum-errors/integer2unicode-bignum-range.idio
		 *
		 * integer->unicode -1.0
		 */
		idio_snprintf (em, BUFSIZ, "U+%" PRIdPTR ": cannot be negative", iv);
	    } else {
		/*
		 * Test Case: fixnum-errors/integer2unicode-bignum-invalid-code-point.idio
		 *
		 * integer->unicode 55296e0
		 */
		idio_snprintf (em, BUFSIZ, "U+%04" PRIXPTR ": is invalid", iv);
	    }
	    idio_error_param_value_msg_only ("integer->unicode", "code point", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: fixnum-errors/integer2unicode-bad-type.idio
	 *
	 * integer->unicode #t
	 */
	idio_error_param_type ("integer", i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return u;
}

void idio_fixnum_add_primitives ()
{
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("FIXNUM-MAX"), idio_fixnum (IDIO_FIXNUM_MAX), idio_Idio_module_instance ());
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("FIXNUM-MIN"), idio_fixnum (IDIO_FIXNUM_MIN), idio_Idio_module_instance ());

    IDIO_ADD_PRIMITIVE (fixnump);
    IDIO_ADD_PRIMITIVE (integerp);
    IDIO_ADD_PRIMITIVE (numberp);
    IDIO_ADD_PRIMITIVE (floor);
    IDIO_ADD_PRIMITIVE (remainder);
    IDIO_ADD_PRIMITIVE (quotient);

    IDIO_ADD_PRIMITIVE (add);
    IDIO_ADD_PRIMITIVE (subtract);
    IDIO_ADD_PRIMITIVE (multiply);
    IDIO_ADD_PRIMITIVE (divide);

    IDIO_ADD_PRIMITIVE (le);
    IDIO_ADD_PRIMITIVE (lt);
    IDIO_ADD_PRIMITIVE (eq);
    IDIO_ADD_PRIMITIVE (ge);
    IDIO_ADD_PRIMITIVE (gt);

    IDIO_ADD_PRIMITIVE (binary_add);
    IDIO_ADD_PRIMITIVE (binary_subtract);
    IDIO_ADD_PRIMITIVE (binary_multiply);
    IDIO_ADD_PRIMITIVE (binary_divide);

    IDIO_ADD_PRIMITIVE (binary_le);
    IDIO_ADD_PRIMITIVE (binary_lt);
    IDIO_ADD_PRIMITIVE (binary_eq);
    IDIO_ADD_PRIMITIVE (binary_ge);
    IDIO_ADD_PRIMITIVE (binary_gt);

    IDIO_ADD_PRIMITIVE (integer2char);
    IDIO_ADD_PRIMITIVE (integer2unicode);
}

void idio_init_fixnum ()
{
    idio_module_table_register (idio_fixnum_add_primitives, NULL, NULL);
}

