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
 * fixnum.c
 * 
 */

#include "idio.h"

static void idio_error_fixnum_divide_by_zero ()
{
    idio_error_message ("divide by zero");
}

IDIO idio_fixnum_C (char *str, int base)
{
    char *end;
    errno = 0;
    intptr_t val = strtoll (str, &end, base);

    if ((errno == ERANGE &&
	 (val == LLONG_MAX ||
	  val == LLONG_MIN)) ||
	(errno != 0 &&
	 val == 0)) {
	idio_error_message ("idio_fixnum_C: strtoll (%s) = %ld: %s", str, val, strerror (errno));
	return idio_S_nil;
    }

    if (end == str) {
	idio_error_message ("idio_fixnum_C: strtoll (%s): No digits?", str);
	return idio_S_nil;
    }
	
    if ('\0' == *end) {
	return IDIO_FIXNUM (val);
    } else {
	idio_error_message ("idio_fixnum_C: strtoll (%s) = %ld", str, val);
	return idio_S_nil;
    }
}    

int idio_isa_fixnum (IDIO o)
{
    IDIO_ASSERT (o);

    return (((intptr_t) o & 3) == IDIO_TYPE_FIXNUM_MARK);
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
	    IDIO bn_args = IDIO_LIST1 (idio_bignum_integer_int64 (ir));
	    args = IDIO_PAIR_T (args);
	    while (idio_S_nil != args) {
		IDIO h = IDIO_PAIR_H (args);
		IDIO_TYPE_ASSERT (fixnum, h);
	
		bn_args = idio_pair (idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (h)), bn_args);
		args = IDIO_PAIR_T (args);
	    }
	    
	    return idio_bignum_primitive_add (idio_list_reverse (bn_args));
	}

	args = IDIO_PAIR_T (args);
    }

    return IDIO_FIXNUM (ir);
}

IDIO idio_fixnum_primitive_subtract (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    intptr_t ir = 0;

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	IDIO_TYPE_ASSERT (fixnum, h);
	
	intptr_t ih = IDIO_FIXNUM_VAL (h);

	ir = ir - ih;
	if (ir > IDIO_FIXNUM_MAX ||
	    ir < IDIO_FIXNUM_MIN) {
	    /*
	     * Shift everything to bignums and pass the calculation on
	     * to the bignum code
	     */
	    IDIO bn_args = IDIO_LIST1 (idio_bignum_integer_int64 (ir));
	    args = IDIO_PAIR_T (args);
	    while (idio_S_nil != args) {
		IDIO h = IDIO_PAIR_H (args);
		IDIO_TYPE_ASSERT (fixnum, h);
	
		bn_args = idio_pair (idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (h)), bn_args);
		args = IDIO_PAIR_T (args);
	    }
	    
	    return idio_bignum_primitive_add (idio_list_reverse (bn_args));
	}

	args = IDIO_PAIR_T (args);
    }

    return IDIO_FIXNUM (ir);
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

    intptr_t ir = 0;

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	IDIO_TYPE_ASSERT (fixnum, h);
	
	intptr_t ih = IDIO_FIXNUM_VAL (h);

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
	    while (itmp >>= 1) {
		bits++;
	    }
	    itmp = ih;
	    while (itmp >>= 1) {
		bits++;
	    }

	    if (bits > ((sizeof (intptr_t) * 8) - 2)) {
		/*
		 * Definitely overflowed!  Probably.
		 */
		IDIO bn_args = IDIO_LIST2 (idio_bignum_integer_int64 (ir), idio_bignum_integer_int64 (ih));
		args = IDIO_PAIR_T (args);
		while (idio_S_nil != args) {
		    IDIO h = IDIO_PAIR_H (args);
		    IDIO_TYPE_ASSERT (fixnum, h);
	
		    bn_args = idio_pair (idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (h)), bn_args);
		    args = IDIO_PAIR_T (args);
		}
	    
		return idio_bignum_primitive_add (idio_list_reverse (bn_args));
	    } else {
		ir = ir * ih;
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return IDIO_FIXNUM (ir);
}

IDIO idio_fixnum_primitive_divide (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    intptr_t ir = 1;

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	IDIO_TYPE_ASSERT (fixnum, h);
	
	intptr_t ih = IDIO_FIXNUM_VAL (h);

	if (0 == ih) {
	    idio_error_fixnum_divide_by_zero ();
	    return idio_S_unspec;
	}
    
	ir = ir / ih;

	args = IDIO_PAIR_T (args);
    }

    return IDIO_FIXNUM (ir);
}

/* result: (a/b . a%b) */
IDIO idio_fixnum_primitive_remainder (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (fixnum, a);
    IDIO_TYPE_ASSERT (fixnum, b);

    intptr_t ia = IDIO_FIXNUM_VAL (a);
    intptr_t ib = IDIO_FIXNUM_VAL (b);

    intptr_t r_div = ia / ib;
    intptr_t r_mod = ia % ib;
    
    return idio_pair (IDIO_FIXNUM (r_div), IDIO_FIXNUM (r_mod));
}

IDIO_DEFINE_PRIMITIVE2 ("remainder", remainder, (IDIO a, IDIO b))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    
    if (idio_isa_fixnum (a) &&
	idio_isa_fixnum (b)) {
	return idio_fixnum_primitive_remainder (a, b);
    } else if (idio_isa_bignum (a) &&
				   idio_isa_bignum (b)) {
	return idio_bignum_divide (a, b);
    } else {
	if (idio_isa_fixnum (b) ||
	    idio_isa_bignum (b)) {
	    idio_error_param_type ("number", a);
	} else {
	    idio_error_param_type ("number", b);
	}
	return idio_S_unspec;
    }
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
	return idio_bignum_primitive_floor (a);
    } else {
	idio_error_param_type ("number", a);
	return idio_S_unspec;
    }
}

IDIO idio_fixnum_primitive_quotient (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (fixnum, a);
    IDIO_TYPE_ASSERT (fixnum, b);

    intptr_t ib = IDIO_FIXNUM_VAL (b);

    if (0 == ib) {
	idio_error_fixnum_divide_by_zero ();
	return idio_S_unspec;
    }

    return IDIO_FIXNUM (IDIO_FIXNUM_VAL (a) / ib);
}

IDIO_DEFINE_PRIMITIVE2 ("quotient", quotient, (IDIO a, IDIO b))
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    
    if (idio_isa_fixnum (a) &&
	idio_isa_fixnum (b)) {
	return idio_fixnum_primitive_quotient (a, b);
    } else if (idio_isa_bignum (a) &&
	       idio_isa_bignum (b)) {
	return idio_bignum_primitive_quotient (a, b);
    } else {
	if (idio_isa_fixnum (b) ||
	    idio_isa_bignum (b)) {
	    idio_error_param_type ("number", a);
	} else {
	    idio_error_param_type ("number", b);
	}
	return idio_S_unspec;
    }
}

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


#define IDIO_DEFINE_ARITHMETIC_PRIMITIVE0V(name,cname)			\
    IDIO_DEFINE_PRIMITIVE1V (name, cname, (IDIO args))			\
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
		    idio_error_param_type ("number", h);		\
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
		    bn_args = idio_pair (idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (h)), bn_args); \
		} else {						\
		    bn_args = idio_pair (h, bn_args);			\
		}							\
	    }								\
									\
	    return idio_bignum_primitive_ ## cname (idio_list_reverse (bn_args)); \
	} else {							\
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
		idio_error_param_type ("number", n1);		\
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
			idio_error_param_type ("number", h);	\
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
		    bn_args = idio_pair (idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (h)), bn_args); \
		} else {					\
		    bn_args = idio_pair (h, bn_args);		\
		}						\
								\
		args = IDIO_PAIR_T (args);			\
	    }							\
								\
	    return idio_bignum_primitive_ ## cname (idio_list_reverse (bn_args)); \
	} else {						\
	    return idio_fixnum_primitive_ ## cname (args);	\
        }							\
    }


IDIO_DEFINE_ARITHMETIC_PRIMITIVE0V ("+", add)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V ("-", subtract)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE0V ("*", multiply)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V ("/", divide)

IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V ("<=", le)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V ("<", lt)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V ("=", eq)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V (">=", ge)
IDIO_DEFINE_ARITHMETIC_PRIMITIVE1V (">", gt)

void idio_init_fixnum ()
{
}

void idio_fixnum_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (fixnump);
    IDIO_ADD_PRIMITIVE (eq);

    IDIO_ADD_PRIMITIVE (add);
    IDIO_ADD_PRIMITIVE (subtract);
    IDIO_ADD_PRIMITIVE (multiply);
    IDIO_ADD_PRIMITIVE (divide);
    IDIO_ADD_PRIMITIVE (remainder);
    IDIO_ADD_PRIMITIVE (floor);
    IDIO_ADD_PRIMITIVE (quotient);

    IDIO_ADD_PRIMITIVE (le);
    IDIO_ADD_PRIMITIVE (lt);
    IDIO_ADD_PRIMITIVE (eq);
    IDIO_ADD_PRIMITIVE (ge);
    IDIO_ADD_PRIMITIVE (gt);
}

void idio_final_fixnum ()
{
}

