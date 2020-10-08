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
 * fixnum.c
 *
 */

#include "idio.h"

static void idio_fixnum_error_divide_by_zero (IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_divide_by_zero ("fixnum divide by zero", c_location);
}

static void idio_fixnum_error_conversion (char *msg, IDIO fn, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (fn);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (fixnum, fn);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_fixnum_conversion_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       c_location,
					       fn));
    idio_raise_condition (idio_S_true, c);
}

IDIO idio_integer (intmax_t i)
{
    if (i <= IDIO_FIXNUM_MAX &&
	i >= IDIO_FIXNUM_MIN) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return IDIO_FIXNUM ((intptr_t) i);
    } else {
	return idio_bignum_integer_intmax_t (i);
    }
}

IDIO idio_uinteger (uintmax_t ui)
{
    if (ui <= IDIO_FIXNUM_MAX) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return IDIO_FIXNUM ((uintptr_t) ui);
    } else {
	return idio_bignum_integer_uintmax_t (ui);
    }
}

IDIO idio_fixnum (intptr_t i)
{
    if (i <= IDIO_FIXNUM_MAX &&
	i >= IDIO_FIXNUM_MIN) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return IDIO_FIXNUM (i);
    } else {
	char em[BUFSIZ];

	sprintf (em, "%" PRIdPTR " too large", i);
	idio_fixnum_error_conversion (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO idio_fixnum_C (char *str, int base)
{
    char *end;
    errno = 0;
    long long int val = strtoll (str, &end, base);

    if ((errno == ERANGE &&
	 (val == LLONG_MAX ||
	  val == LLONG_MIN)) ||
	(errno != 0 &&
	 val == 0)) {
	char em[BUFSIZ];
	sprintf (em, "idio_fixnum_C: strtoll (%s) = %lld", str, val);
	idio_fixnum_error_conversion (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (end == str) {
	char em[BUFSIZ];
	sprintf (em, "idio_fixnum_C: strtoll (%s): No digits?", str);
	idio_fixnum_error_conversion (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if ('\0' == *end) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return idio_fixnum ((intptr_t) val);
    } else {
	char em[BUFSIZ];
	sprintf (em, "idio_fixnum_C: strtoll (%s) = %lld", str, val);
	idio_fixnum_error_conversion (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

int idio_isa_fixnum (IDIO o)
{
    IDIO_ASSERT (o);

    return (((intptr_t) o & IDIO_TYPE_MASK) == IDIO_TYPE_FIXNUM_MARK);
}

IDIO_DEFINE_PRIMITIVE1 ("fixnum?", fixnump, (IDIO o))
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

IDIO_DEFINE_PRIMITIVE1 ("integer?", integerp, (IDIO o))
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
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1 ("number?", numberp, (IDIO o))
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
	    return idio_bignum_primitive_add (bn_args);
	}

	args = IDIO_PAIR_T (args);
    }

    return idio_fixnum (ir);
}

IDIO idio_fixnum_primitive_divide (IDIO args)
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

	    /*
	      a bit of magic for divide:

	      (/ 6)   => 1/6 => 1/6
	      (/ 6 2) => 6/2 => 3
	    */

	    IDIO t = IDIO_PAIR_T (args);
	    if (idio_S_nil != t) {
		ir = ih;
		args = t;
		continue;
	    }
	}

	if (0 == ih) {
	    idio_fixnum_error_divide_by_zero (IDIO_C_FUNC_LOCATION ());
	    return idio_S_unspec;
	}

	ir = ir / ih;

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

IDIO_DEFINE_PRIMITIVE1 ("floor", floor, (IDIO a))
{
    IDIO_ASSERT (a);

    if (idio_isa_fixnum (a)) {
	return idio_fixnum_primitive_floor (a);
    } else if (idio_isa_bignum (a)) {
	IDIO num = idio_bignum_primitive_floor (a);

	/* convert to a fixnum if possible */
	IDIO fn = idio_bignum_to_fixnum (num);
	if (idio_S_nil != fn) {
	    num = fn;
	}

	return num;
    } else {
	idio_error_param_type ("number", a, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }
}

IDIO idio_fixnum_primitive_remainder (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (fixnum, a);
    IDIO_TYPE_ASSERT (fixnum, b);

    intptr_t ia = IDIO_FIXNUM_VAL (a);
    intptr_t ib = IDIO_FIXNUM_VAL (b);

    intptr_t r_mod = ia % ib;

    return idio_fixnum (r_mod);
}

IDIO_DEFINE_PRIMITIVE2 ("remainder", remainder, (IDIO a, IDIO b))
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
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());
	}
    } else if (idio_isa_bignum (a)) {
	if (idio_isa_fixnum (b)) {
	    num = idio_bignum_primitive_remainder (a, idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (b)));
	} else if (idio_isa_bignum (b)) {
	    num = idio_bignum_primitive_remainder (a, b);
	} else {
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());
	}
    } else {
	idio_error_param_type ("number", a, IDIO_C_FUNC_LOCATION ());
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
	idio_fixnum_error_divide_by_zero (IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }

    /* idio_debug ("fixnum quotient: %s", a); */
    /* idio_debug (" / %s", b); */
    /* idio_debug (" = %s\n", idio_fixnum (IDIO_FIXNUM_VAL (a) / ib)); */
    return idio_fixnum (IDIO_FIXNUM_VAL (a) / ib);
}

IDIO_DEFINE_PRIMITIVE2 ("quotient", quotient, (IDIO a, IDIO b))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    /* idio_debug ("primitive: quotient: %s", a); */
    /* idio_debug (" / %s\n", b); */

    IDIO num = idio_S_unspec;

    if (idio_isa_fixnum (a)) {
	if (idio_isa_fixnum (b)) {
	    return idio_fixnum_primitive_quotient (a, b);
	} else if (idio_isa_bignum (b)) {
	    num = idio_bignum_primitive_quotient (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (a)), b);
	} else {
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());
	}
    } else if (idio_isa_bignum (a)) {
	if (idio_isa_fixnum (b)) {
	    num = idio_bignum_primitive_quotient (a, idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (b)));
	} else if (idio_isa_bignum (b)) {
	    num = idio_bignum_primitive_quotient (a, b);
	} else {
	    idio_error_param_type ("number", b, IDIO_C_FUNC_LOCATION ());
	}
    } else {
	idio_error_param_type ("number", a, IDIO_C_FUNC_LOCATION ());
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

IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(le, <=)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(lt, <)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(eq, ==)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(ge, >=)
IDIO_DEFINE_FIXNUM_CMP_PRIMITIVE_(gt, >)

/*
 * Second, define some generic primitives that look out for bignum
 * arguments
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
		    idio_error_param_type ("number", h, idio_string_C ("arithmetic " name));	\
		    return idio_S_unspec;				\
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
	    /* idio_debug ("primitive: " #cname ": -> bignum: %s\n", bn_args); */ \
	    IDIO num = idio_bignum_primitive_ ## cname (bn_args); \
									\
	    /* convert to a fixnum if possible */			\
	    IDIO fn = idio_bignum_to_fixnum (num);			\
	    if (idio_S_nil != fn) {					\
		num = fn;						\
	    }								\
									\
	    return num;							\
									\
	} else {							\
	    /* idio_debug ("primitive: " #cname ": -> fixnum: %s\n", args); */ \
	    return idio_fixnum_primitive_ ## cname (args);		\
        }								\
    }

#define IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V(name,cname)		\
    IDIO_DEFINE_PRIMITIVE1V (name, cname, (IDIO n1, IDIO args))	\
    {								\
	IDIO_ASSERT (n1);					\
	IDIO_ASSERT (args);					\
								\
	int ibn = 0;						\
	if (! idio_isa_fixnum (n1)) {				\
	    ibn = idio_isa_bignum (n1);				\
								\
	    if (0 == ibn) {					\
		idio_error_param_type ("number", n1, idio_string_C ("arithmetic " name)); \
		return idio_S_unspec;				\
	    }							\
	}							\
								\
	if (0 == ibn) {						\
	    IDIO a = args;					\
								\
	    while (idio_S_nil != a) {				\
		IDIO h = IDIO_PAIR_H (a);			\
		ibn = idio_isa_bignum (h);			\
								\
		if (ibn) {					\
		    break;					\
		} else {					\
		    if (! idio_isa_fixnum (h)) {		\
			idio_error_param_type ("number", h, idio_string_C ("arithmetic " name)); \
			return idio_S_unspec;			\
		    }						\
		}						\
								\
		a = IDIO_PAIR_T (a);				\
	    }							\
	}							\
								\
	args = idio_pair (n1, args);				\
								\
	if (ibn) {						\
	    IDIO bn_args = idio_S_nil;				\
	    							\
	    while (idio_S_nil != args) {			\
		IDIO h = IDIO_PAIR_H (args);			\
								\
		if (idio_isa_fixnum (h)) {			\
		    bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args); \
		} else {					\
		    bn_args = idio_pair (h, bn_args);		\
		}						\
								\
		args = IDIO_PAIR_T (args);				\
	    }								\
	    								\
	    bn_args = idio_list_reverse (bn_args);			\
	    /* idio_debug ("primitive: " #cname ": -> bignum: %s\n", bn_args); */ \
	    IDIO num = idio_bignum_primitive_ ## cname (bn_args); \
									\
	    /* convert to a fixnum if possible */			\
	    IDIO fn = idio_bignum_to_fixnum (num);			\
	    if (idio_S_nil != fn) {					\
		num = fn;						\
	    }								\
	    								\
	    return num;							\
	    								\
	} else {							\
	    /* idio_debug ("primitive: " #cname ": -> fixnum: %s\n", args); */ \
	    return idio_fixnum_primitive_ ## cname (args);		\
        }								\
    }

/*
 * For divide we should always convert to bignums: 1 / 3 is 0; 9 / 2
 * is 4 in fixnums; 10 / 2 will be converted back to a fixnum.
 */
#define IDIO_DEFINE_ARITHMETIC_BIGNUM_PRIMITIVE1V(name,cname)	\
    IDIO_DEFINE_PRIMITIVE1V (name, cname, (IDIO n1, IDIO args))	\
    {								\
	IDIO_ASSERT (n1);					\
	IDIO_ASSERT (args);					\
								\
	args = idio_pair (n1, args);				\
	IDIO a = args;						\
	IDIO bn_args = idio_S_nil;				\
								\
	while (idio_S_nil != a) {				\
	    IDIO h = IDIO_PAIR_H (a);				\
	    							\
	    if (idio_isa_fixnum (h)) {					\
		bn_args = idio_pair (idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (h)), bn_args); \
	    } else if (idio_isa_bignum (h)) {				\
		bn_args = idio_pair (h, bn_args);			\
	    } else {							\
		idio_error_param_type ("number", h, idio_string_C ("arithmetic bignum " name)); \
		return idio_S_unspec;					\
	    }								\
	    								\
	    a = IDIO_PAIR_T (a);					\
	}								\
	bn_args = idio_list_reverse (bn_args);				\
									\
	/* idio_debug ("primitive: " #cname ": -> bignum: %s\n", bn_args); */ \
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

#define IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V(name,cname)	\
    IDIO_DEFINE_PRIMITIVE1V (name, cname, (IDIO n1, IDIO args))	\
    {								\
	IDIO_ASSERT (n1);					\
	IDIO_ASSERT (args);					\
								\
	int ibn = 0;						\
	if (! idio_isa_fixnum (n1)) {				\
	    ibn = idio_isa_bignum (n1);				\
								\
	    if (0 == ibn) {					\
		idio_error_param_type ("number", n1, idio_string_C ("arithmetic cmp " name)); \
		return idio_S_unspec;				\
	    }							\
	}							\
								\
	if (0 == ibn) {						\
	    IDIO a = args;					\
								\
	    while (idio_S_nil != a) {				\
		IDIO h = IDIO_PAIR_H (a);			\
		ibn = idio_isa_bignum (h);			\
								\
		if (ibn) {					\
		    break;					\
		} else {					\
		    if (! idio_isa_fixnum (h)) {		\
			idio_error_param_type ("number", h, idio_string_C ("arithmetic cmp " name)); \
			return idio_S_unspec;			\
		    }						\
		}						\
								\
		a = IDIO_PAIR_T (a);				\
	    }							\
	}							\
								\
	args = idio_pair (n1, args);				\
								\
	if (ibn) {						\
	    IDIO bn_args = idio_S_nil;				\
	    							\
	    while (idio_S_nil != args) {			\
		IDIO h = IDIO_PAIR_H (args);			\
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

IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("le", le)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("lt", lt)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("eq", eq)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("ge", ge)
IDIO_DEFINE_ARITHMETIC_CMP_PRIMITIVE1V ("gt", gt)

IDIO_DEFINE_PRIMITIVE1 ("integer->char", integer2char, (IDIO i))
{
    IDIO_ASSERT (i);

    IDIO c = idio_S_unspec;

    if (idio_isa_fixnum (i)) {
	c = IDIO_CHARACTER (IDIO_FIXNUM_VAL (i));
    } else if (idio_isa_bignum (i)) {
	intptr_t iv = idio_bignum_intptr_value (i);

	if (iv >= 0 &&
	    iv <= IDIO_FIXNUM_MAX) {
	    c = IDIO_CHARACTER (iv);
	}
    }

    if (! idio_isa_character (c)) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "invalid integer");
	return idio_S_unspec;
    }

    return c;
}

IDIO_DEFINE_PRIMITIVE1 ("integer->unicode", integer2unicode, (IDIO i))
{
    IDIO_ASSERT (i);

    IDIO u = idio_S_unspec;

    if (idio_isa_fixnum (i)) {
	u = IDIO_UNICODE (IDIO_FIXNUM_VAL (i));
    } else if (idio_isa_bignum (i)) {
	intptr_t iv = idio_bignum_intptr_value (i);

	if (iv >= 0 &&
	    iv <= 0x10ffff) {
	    u = IDIO_UNICODE (iv);
	}
    }

    if (! idio_isa_unicode (u)) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "invalid integer");
	return idio_S_unspec;
    }

    return u;
}

void idio_init_fixnum ()
{
}

void idio_fixnum_add_primitives ()
{
    idio_module_set_symbol_value (idio_symbols_C_intern ("FIXNUM-MAX"), idio_fixnum (IDIO_FIXNUM_MAX), idio_Idio_module_instance ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("FIXNUM-MIN"), idio_fixnum (IDIO_FIXNUM_MIN), idio_Idio_module_instance ());

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

    IDIO_ADD_PRIMITIVE (integer2char);
    IDIO_ADD_PRIMITIVE (integer2unicode);
}

void idio_final_fixnum ()
{
}

