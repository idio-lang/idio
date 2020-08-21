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
 * util.c
 *
 */

#include "idio.h"

static IDIO idio_util_value_as_string;
IDIO idio_print_conversion_format_sym = idio_S_nil;
IDIO idio_print_conversion_precision_sym = idio_S_nil;

#ifdef IDIO_DEBUG
#undef IDIO_EQUAL_DEBUG
#endif

#ifdef IDIO_EQUAL_DEBUG
typedef struct idio_equal_stat_s {
    /*
     * We *could* have an o1 and o2 type array but we don't really
     * care about the specifics of mixed types equality
     */
    unsigned long count;
    struct timeval duration;
    unsigned long mixed;
} idio_equal_stat_t;
static idio_equal_stat_t idio_equal_stats[IDIO_TYPE_MAX];

static void idio_equal_stat_increment (IDIO o1, IDIO o2, struct timeval t0)
{
    struct timeval te;
    if (gettimeofday (&te, NULL) == -1) {
	perror ("gettimeofday");
    }

    int t1 = (intptr_t) o1 & IDIO_TYPE_MASK;
    if (0 == t1) {
	t1 = o1->type;
    }
    idio_equal_stats[t1].count++;
    struct timeval td;
    td.tv_sec = te.tv_sec - t0.tv_sec;
    td.tv_usec = te.tv_usec - t0.tv_usec;
    if (td.tv_usec < 0) {
	td.tv_usec += 1000000;
	td.tv_sec -= 1;
    }
    idio_equal_stats[t1].duration.tv_sec += td.tv_sec;
    idio_equal_stats[t1].duration.tv_usec += td.tv_usec;
    if (idio_equal_stats[t1].duration.tv_usec > 1000000) {
	idio_equal_stats[t1].duration.tv_usec -= 1000000;
	idio_equal_stats[t1].duration.tv_sec += 1;
    }
    int t2 = (intptr_t) o2 & IDIO_TYPE_MASK;
    if (0 == t2) {
	t2 = o2->type;
    }
    if (t1 != t2) {
	idio_equal_stats[t1].mixed++;
    }
}
#endif

void idio_util_error_format (char *m, IDIO s, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (s);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_C (m, s, c_location);
}

int idio_type (IDIO o)
{
    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	return IDIO_TYPE_FIXNUM;
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		return IDIO_TYPE_CONSTANT_IDIO;
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
		return IDIO_TYPE_CONSTANT_TOKEN;
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		return IDIO_TYPE_CONSTANT_I_CODE;
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		return IDIO_TYPE_CONSTANT_CHARACTER;
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		return IDIO_TYPE_CONSTANT_UNICODE;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT"), "type: unexpected object type %#x", o);

		/* notreached */
		return IDIO_TYPE_NONE;
	    }
	}
    case IDIO_TYPE_PLACEHOLDER_MARK:
	return IDIO_TYPE_PLACEHOLDER;
    case IDIO_TYPE_POINTER_MARK:
	return o->type;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "type: unexpected object type %#x", o);

	/* notreached */
	return IDIO_TYPE_NONE;
    }
}

const char *idio_type_enum2string (idio_type_e type)
{
    switch (type) {
    case IDIO_TYPE_NONE:		return "NONE";
    case IDIO_TYPE_FIXNUM:		return "FIXNUM";
    case IDIO_TYPE_CONSTANT_IDIO:	return "CONSTANT_IDIO";
    case IDIO_TYPE_CONSTANT_TOKEN:	return "CONSTANT_TOKEN";
    case IDIO_TYPE_CONSTANT_I_CODE:	return "CONSTANT_I_CODE";
    case IDIO_TYPE_CONSTANT_CHARACTER:	return "CONSTANT_CHARACTER";
    case IDIO_TYPE_CONSTANT_UNICODE:	return "CONSTANT_UNICODE";
    case IDIO_TYPE_PLACEHOLDER:		return "PLACEHOLDER";
    case IDIO_TYPE_STRING:		return "STRING";
    case IDIO_TYPE_SUBSTRING:		return "SUBSTRING";
    case IDIO_TYPE_SYMBOL:		return "SYMBOL";
    case IDIO_TYPE_KEYWORD:		return "KEYWORD";
    case IDIO_TYPE_PAIR:		return "PAIR";
    case IDIO_TYPE_ARRAY:		return "ARRAY";
    case IDIO_TYPE_HASH:		return "HASH";
    case IDIO_TYPE_CLOSURE:		return "CLOSURE";
    case IDIO_TYPE_PRIMITIVE:		return "PRIMITIVE";
    case IDIO_TYPE_BIGNUM:		return "BIGNUM";
    case IDIO_TYPE_MODULE:		return "MODULE";
    case IDIO_TYPE_FRAME:		return "FRAME";
    case IDIO_TYPE_HANDLE:		return "HANDLE";
    case IDIO_TYPE_STRUCT_TYPE:		return "STRUCT_TYPE";
    case IDIO_TYPE_STRUCT_INSTANCE:	return "STRUCT_INSTANCE";
    case IDIO_TYPE_THREAD:		return "THREAD";
    case IDIO_TYPE_CONTINUATION:	return "CONTINUATION";
    case IDIO_TYPE_BITSET:		return "BITSET";

    case IDIO_TYPE_C_INT:		return "C_INT";
    case IDIO_TYPE_C_UINT:		return "C_UINT";
    case IDIO_TYPE_C_FLOAT:		return "C_FLOAT";
    case IDIO_TYPE_C_DOUBLE:		return "C_DOUBLE";
    case IDIO_TYPE_C_POINTER:		return "C_POINTER";
    case IDIO_TYPE_C_VOID:		return "C_VOID";

    case IDIO_TYPE_C_TYPEDEF:		return "TAG";
    case IDIO_TYPE_C_STRUCT:		return "C_STRUCT";
    case IDIO_TYPE_C_INSTANCE:		return "C_INSTANCE";
    case IDIO_TYPE_C_FFI:		return "C_FFI";
    case IDIO_TYPE_OPAQUE:		return "OPAQUE";
    default:
	return "NOT KNOWN";
    }
}

const char *idio_type2string (IDIO o)
{
    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	return "FIXNUM";
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:		return "CONSTANT_IDIO";
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:		return "CONSTANT_TOKEN";
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:	return "CONSTANT_I_CODE";
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:	return "CONSTANT_CHARACTER";
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:	return "CONSTANT_UNICODE";
	    default:
		idio_error_C ("idio_type2string: unexpected type", o, IDIO_C_FUNC_LOCATION_S ("CONSTANT"));

		/* notreached */
		return "!!NOT-REACHED!!";
	    }
	}
    case IDIO_TYPE_PLACEHOLDER_MARK:
	return "PLACEHOLDER";
    case IDIO_TYPE_POINTER_MARK:
	return idio_type_enum2string (o->type);
    default:
	idio_error_C ("idio_type2string: unexpected type", o, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return "!!NOT-REACHED!!";
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("type-string", type_string, (IDIO o), "o", "\
return the type of `o` as a string		\n\
						\n\
:param o: object				\n\
						\n\
:return: a string representation of the type of `o`	\n\
")
{
    IDIO_ASSERT (o);

    return idio_string_C (idio_type2string (o));
}

IDIO_DEFINE_PRIMITIVE1 ("zero?", zerop, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if ((idio_isa_fixnum (o) &&
	 0 == IDIO_FIXNUM_VAL (o)) ||
	(idio_isa_bignum (o) &&
	 idio_bignum_zero_p (o))) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_nil (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_S_nil == o);
}

IDIO_DEFINE_PRIMITIVE1 ("null?", nullp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_nil == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("unset?", unsetp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_unset == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("undef?", undefp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_undef == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("unspec?", unspecp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_unspec == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("void?", voidp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_void == o) {
	r = idio_S_true;
    }

    return r;
}

/*
 * Unrelated to undef?, %undefined? tests whether a non-local symbol
 * is defined in this environment.
 */
IDIO_DEFINE_PRIMITIVE1 ("%defined?", definedp, (IDIO s))
{
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    IDIO r = idio_S_false;

    IDIO sk = idio_module_env_symbol_recurse (s);

    if (idio_S_unspec != sk) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_boolean (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_S_true == o ||
	    idio_S_false == o);
}

IDIO_DEFINE_PRIMITIVE1 ("boolean?", booleanp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_boolean (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("not", not, (IDIO e))
{
    IDIO_ASSERT (e);

    IDIO r = idio_S_false;

    if (idio_S_false == e) {
	r = idio_S_true;
    }

    return r;
}

/*
 * Equality -- what does that mean?  RnRS defines the equivalence
 * predicates -- albeit allowing for implementation dependent
 * variances,
 * eg. https://www.cs.cmu.edu/Groups/AI/html/r4rs/r4rs_8.html#SEC47
 *
 * Naturally, we'll vary a little:
 *
 * In all cases, if the two objects are C/== then they are equal.  Did
 * I need to spell that out?
 *
 * For most object types (that is, largely non-user-visible) the
 * *only* equality is C/==.  An example is creating two closures from
 * the same source code -- they will always be different.  Two
 * references to the same closure will always be the same.
 *
 * eq?
 *
 * For non-pointer types (fixnum & characters) then return true if the
 * values are C/== (the default case, above) and for pointers the
 * malloc()'d object is C/==
 *
 * In other words, the two objects are indistinguishable for any
 * practical purpose.
 *
 * eqv?
 *
 * For non-pointer types (fixnum & characters) then return true if the
 * values are C/== (the default case, above).
 *
 * For strings, if the contents are the same.
 *
 * For bignums, if the values are the same -- including exactness.
 *
 * Generally, for non-compound objects, if the values are the same.
 *
 * equal?
 *
 * A recursive descent through objects verifying that each element is
 * equal?  RnRS suggests: "A rule of thumb is that objects are
 * generally equal? if they print the same."
 *
 * For non-pointer types (fixnum & characters) then return true if the
 * values are C/== (the default case, above).
 *
 * equal? for a string is the same as eqv?
 *
 * equal? for a bignum is the same as eqv?
 *
 * For compound objects (arrays, hashes) then the array/hash sizes are
 * compared and then each element is compared with equal?.
 *
 */
#define IDIO_EQUAL_EQP		1
#define IDIO_EQUAL_EQVP		2
#define IDIO_EQUAL_EQUALP	3

int idio_eqp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQP);
}

int idio_eqvp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQVP);
}

int idio_equalp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQUALP);
}

IDIO_DEFINE_PRIMITIVE2 ("eq?", eqp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_eqp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("eqv?", eqvp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_eqvp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

/*
 * s9.scm redefines equal? from eq? and eqv? and recurses on itself --
 * or it will if we do not define a primitive equal? which would be
 * used in its definition
 */

IDIO_DEFINE_PRIMITIVE2 ("equal?", equalp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_equalp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

int idio_equal (IDIO o1, IDIO o2, int eqp)
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    /*
     * eq?
     *
     * NB For FIXNUM/CONSTANT/PLACEHOLDER types then this (integer
     * equality test) implicitly tests their type as well.
     */
    if (o1 == o2) {
#ifdef IDIO_EQUAL_DEBUG
	int t1 = (intptr_t) o1 & IDIO_TYPE_MASK;
	if (0 == t1) {
	    t1 = o1->type;
	}
	idio_equal_stats[t1].count++;
#endif
	return 1;
    }

    int m1 = (intptr_t) o1 & IDIO_TYPE_MASK;
    int m2 = (intptr_t) o2 & IDIO_TYPE_MASK;

    /*
     * Careful!  Before we fail if the two objects are not of the same
     * type, handle sibling object types.  Notably:
     *
     * strings and substrings
     *
     * fixnums and bignums
     *
     * NB This was originally a test here but there are so many
     * comparisons and so many of them should be very quick that the
     * code has been interspersed below.
     */

#ifdef IDIO_EQUAL_DEBUG
    struct timeval t0;
    if (gettimeofday (&t0, NULL) == -1) {
	perror ("gettimeofday");
    }
#endif
    switch (m1) {
    case IDIO_TYPE_FIXNUM_MARK:
	if (IDIO_EQUAL_EQP != eqp) {
	    IDIO r = idio_S_false;

	    if (IDIO_TYPE_FIXNUM_MARK == m2) {
		return (o1 == o2);
	    } else if (idio_isa_number (o2)) {
		r = idio_vm_invoke_C (idio_thread_current_thread (),
				      IDIO_LIST3 (idio_module_symbol_value (idio_symbols_C_intern ("=="),
									    idio_Idio_module,
									    idio_S_nil),
						  o1,
						  o2));
	    }

#ifdef IDIO_EQUAL_DEBUG
	    idio_equal_stat_increment (o1, o2, t0);
#endif
	    return (idio_S_true == r);
	} else {
	    return 0;
	}
	break;
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	/*
	 * If they were the same type then we've already tested for
	 * equality above!
	 */
	return 0;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (m2) {
	    case IDIO_TYPE_FIXNUM_MARK:
		if (IDIO_EQUAL_EQP != eqp) {
		    IDIO r = idio_S_false;

		    if (idio_isa_bignum (o1)) {
			r = idio_vm_invoke_C (idio_thread_current_thread (),
					      IDIO_LIST3 (idio_module_symbol_value (idio_symbols_C_intern ("=="),
										    idio_Idio_module,
										    idio_S_nil),
							  o1,
							  o2));
		    }

#ifdef IDIO_EQUAL_DEBUG
		    idio_equal_stat_increment (o1, o2, t0);
#endif
		    return (idio_S_true == r);
		} else {
		    return 0;
		}
		break;
	    case IDIO_TYPE_CONSTANT_MARK:
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		/* we would have matched at the top */
		return 0;
	    default:
		break;
	    }

	    /*
	     * Do the non-number siblings test before the type
	     * equality test (next).
	     *
	     * As a hint we've figured out by now that both o1 and o2
	     * are POINTER_MARK types, ie. we can safely access ->type
	     * etc.
	     */

	    if (IDIO_EQUAL_EQP != eqp) {
		switch (o1->type) {
		case IDIO_TYPE_STRING:
		    {
			switch (o2->type) {
			case IDIO_TYPE_SUBSTRING:
			    if (IDIO_STRING_LEN (o1) != IDIO_SUBSTRING_LEN (o2)) {
				return 0;
			    }

#ifdef IDIO_EQUAL_DEBUG
			    idio_equal_stat_increment (o1, o2, t0);
#endif
			    return idio_string_equal (o1, o2);
			}
		    }
		    break;
		case IDIO_TYPE_SUBSTRING:
		    {
			switch (o2->type) {
			case IDIO_TYPE_STRING:
			    if (IDIO_SUBSTRING_LEN (o1) != IDIO_STRING_LEN (o2)) {
				return 0;
			    }

#ifdef IDIO_EQUAL_DEBUG
			    idio_equal_stat_increment (o1, o2, t0);
#endif
			    return idio_string_equal (o1, o2);
			}
		    }
		    break;
		}
	    }

	    /*
	     * Type equality (having done siblings, above).  If
	     * they're not the same type then they can't be any form
	     * of equal.
	     */
	    if (o1->type != o2->type) {
		return 0;
	    }

	    /*
	     * Goofs.  Should raise an error!
	     */
	    if (0 == o1->type ||
		IDIO_GC_FLAG_FREE_SET (o1) ||
		IDIO_GC_FLAG_FREE_SET (o2)) {
		fprintf (stderr, "equal?: GC issue\n");
		idio_dump (o1, 1);
		idio_dump (o2, 1);
		return 0;
	    }

	    size_t i;

	    /*
	     * Now the nitty gritty of type equality comparisons.
	     *
	     * Note that many EQP tests are (o1 == o2) which we could
	     * replace with return 0 as we already did this test at
	     * the top of the function.  It failed then so it should
	     * fail now!
	     */
	    switch (o1->type) {
	    case IDIO_TYPE_STRING:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1 == o2);
		}

		if (IDIO_STRING_LEN (o1) != IDIO_STRING_LEN (o2)) {
		    return 0;
		}

#ifdef IDIO_EQUAL_DEBUG
		idio_equal_stat_increment (o1, o2, t0);
#endif
		return idio_string_equal (o1, o2);

		break;
	    case IDIO_TYPE_SUBSTRING:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1 == o2);
		}

		if (IDIO_SUBSTRING_LEN (o1) != IDIO_SUBSTRING_LEN (o2)) {
		    return 0;
		}

#ifdef IDIO_EQUAL_DEBUG
		idio_equal_stat_increment (o1, o2, t0);
#endif
		return idio_string_equal (o1, o2);

		break;
	    case IDIO_TYPE_SYMBOL:
		return (o1 == o2);

		break;
	    case IDIO_TYPE_KEYWORD:
		return (o1 == o2);

		break;
	    case IDIO_TYPE_PAIR:
		if (IDIO_EQUAL_EQP == eqp ||
		    IDIO_EQUAL_EQVP == eqp) {
		    return (o1 == o2);
		}

		return (idio_equal (IDIO_PAIR_H (o1), IDIO_PAIR_H (o2), eqp) &&
			idio_equal (IDIO_PAIR_T (o1), IDIO_PAIR_T (o2), eqp));
	    case IDIO_TYPE_ARRAY:
		if (IDIO_EQUAL_EQP == eqp ||
		    IDIO_EQUAL_EQVP == eqp) {
		    return (o1->u.array == o2->u.array);
		}

		if (IDIO_ARRAY_USIZE (o1) != IDIO_ARRAY_USIZE (o2)) {
		    return 0;
		}

		for (i = 0; i < IDIO_ARRAY_USIZE (o1); i++) {
		    if (! idio_equal (IDIO_ARRAY_AE (o1, i), IDIO_ARRAY_AE (o2, i), eqp)) {
			return 0;
		    }
		}

		/*
		 * Compare default values?
		 */

		return 1;
	    case IDIO_TYPE_HASH:
		if (IDIO_EQUAL_EQP == eqp ||
		    IDIO_EQUAL_EQVP == eqp) {
		    return (o1->u.hash == o2->u.hash);
		}

		if (IDIO_HASH_SIZE (o1) != IDIO_HASH_SIZE (o2)) {
		    return 0;
		}

		for (i = 0; i < IDIO_HASH_SIZE (o1); i++) {
		    if (! idio_equal (IDIO_HASH_HE_KEY (o1, i), IDIO_HASH_HE_KEY (o2, i), eqp) ||
			! idio_equal (IDIO_HASH_HE_VALUE (o1, i), IDIO_HASH_HE_VALUE (o2, i), eqp)) {
			return 0;
		    }
		}
		return 1;
	    case IDIO_TYPE_CLOSURE:
		return (o1 == o2);
	    case IDIO_TYPE_PRIMITIVE:
		return (o1 == o2);
	    case IDIO_TYPE_BIGNUM:
		if (IDIO_EQUAL_EQP == eqp) {
		    /*
		     * u.bignum is part of the idio_s union so check
		     * the malloc'd sig
		     */
		    return (IDIO_BIGNUM_SIG (o1) == IDIO_BIGNUM_SIG (o2));
		}

#ifdef IDIO_EQUAL_DEBUG
		    idio_equal_stat_increment (o1, o2, t0);
#endif
		return idio_bignum_real_equal_p (o1, o2);
	    case IDIO_TYPE_MODULE:
		return (o1 == o2);
	    case IDIO_TYPE_FRAME:
		return (o1 == o2);
	    case IDIO_TYPE_HANDLE:
		return (o1->u.handle == o2->u.handle);
	    case IDIO_TYPE_C_INT:
		return (IDIO_C_TYPE_INT (o1) == IDIO_C_TYPE_INT (o2));
	    case IDIO_TYPE_C_UINT:
		return (IDIO_C_TYPE_UINT (o1) == IDIO_C_TYPE_UINT (o2));
	    case IDIO_TYPE_C_FLOAT:
		return (IDIO_C_TYPE_FLOAT (o1) == IDIO_C_TYPE_FLOAT (o2));
	    case IDIO_TYPE_C_DOUBLE:
		return (IDIO_C_TYPE_DOUBLE (o1) == IDIO_C_TYPE_DOUBLE (o2));
	    case IDIO_TYPE_C_POINTER:
		return (IDIO_C_TYPE_POINTER_P (o1) == IDIO_C_TYPE_POINTER_P (o2));
	    case IDIO_TYPE_STRUCT_TYPE:
		{
		    if (IDIO_EQUAL_EQP == eqp ||
			IDIO_EQUAL_EQVP == eqp) {
			return (o1->u.struct_type == o2->u.struct_type);
		    }

		    if (! idio_equal (IDIO_STRUCT_TYPE_NAME (o1), IDIO_STRUCT_TYPE_NAME (o2), eqp) ||
			! idio_equal (IDIO_STRUCT_TYPE_PARENT (o1), IDIO_STRUCT_TYPE_PARENT (o2), eqp)) {
			return 0;
		    }

		    size_t s1 = IDIO_STRUCT_TYPE_SIZE (o1);
		    if (s1 != IDIO_STRUCT_TYPE_SIZE (o2)) {
			return 0;
		    }

		    /*
		     * We're now at the stage of having two
		     * identically named and sized struct types.
		     * Check each field.
		     */
		    size_t i;
		    for (i = 0 ; i < s1 ; i++) {
			if (! idio_equal (IDIO_STRUCT_TYPE_FIELDS (o1, i), IDIO_STRUCT_TYPE_FIELDS (o2, i), eqp)) {
			    return 0;
			}
		    }
		}
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		{
		    if (IDIO_EQUAL_EQP == eqp ||
			IDIO_EQUAL_EQVP == eqp) {
			return (o1 == o2);
		    }

		    if (! idio_equal (IDIO_STRUCT_INSTANCE_TYPE (o1), IDIO_STRUCT_INSTANCE_TYPE (o2), eqp)) {
			return 0;
		    }

		    /*
		     * These struct instances are the same type
		     * therefore must have the same size.  Check each
		     * value.
		     */
		    size_t s1 = IDIO_STRUCT_INSTANCE_SIZE (o1);
		    size_t i;
		    for (i = 0 ; i < s1 ; i++) {
			if (! idio_equal (IDIO_STRUCT_INSTANCE_FIELDS (o1, i), IDIO_STRUCT_INSTANCE_FIELDS (o2, i), eqp)) {
			    return 0;
			}
		    }
		}
		break;
	    case IDIO_TYPE_THREAD:
		return (o1->u.thread == o2->u.thread);
	    case IDIO_TYPE_CONTINUATION:
		return (o1->u.continuation == o2->u.continuation);
	    case IDIO_TYPE_BITSET:
		if (IDIO_EQUAL_EQP == eqp ||
		    IDIO_EQUAL_EQVP == eqp) {
		    return (o1 == o2);
		}

#ifdef IDIO_EQUAL_DEBUG
		    idio_equal_stat_increment (o1, o2, t0);
#endif
		return idio_equal_bitsetp (IDIO_LIST2 (o1, o2));
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		return (o1->u.C_typedef == o2->u.C_typedef);
	    case IDIO_TYPE_C_STRUCT:
		return (o1->u.C_struct == o2->u.C_struct);
	    case IDIO_TYPE_C_INSTANCE:
		return o1->u.C_instance == o2->u.C_instance;
	    case IDIO_TYPE_C_FFI:
		return (o1->u.C_FFI == o2->u.C_FFI);
	    case IDIO_TYPE_OPAQUE:
		return (o1->u.opaque == o2->u.opaque);
	    default:
		idio_error_C ("IDIO_TYPE_POINTER_MARK: o1->type unexpected", o1, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return 0;
	    }
	}
    default:
	idio_error_C ("o1->type unexpected", o1, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    return 1;
}

static IDIO idio_util_as_string_symbol (IDIO o)
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_nil;

    if (idio_isa_struct_instance (o)) {
	IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (o);
	IDIO stn = IDIO_STRUCT_TYPE_NAME (sit);

	size_t stnl = strlen (IDIO_SYMBOL_S (stn));
	size_t blen = stnl + 10;
	char *sym_name = idio_alloc (blen + 1);
	memcpy (sym_name, IDIO_SYMBOL_S (stn), stnl);
	memcpy (sym_name + stnl, "-as-string", 10);
	sym_name[blen] = '\0';

	r = idio_symbols_C_intern (sym_name);

	free (sym_name);
    }

    return r;
}

/*
  Scheme-ish write -- internal representation (where appropriate)
  suitable for (read).  Primarily:

  CHARACTER #\a:	#\a
  STRING "foo":		"foo"
 */
#define IDIO_FIXNUM_CONVERSION_FORMAT_X		0x58
#define IDIO_FIXNUM_CONVERSION_FORMAT_b		0x62
#define IDIO_FIXNUM_CONVERSION_FORMAT_d		0x64
#define IDIO_FIXNUM_CONVERSION_FORMAT_o		0x6F
#define IDIO_FIXNUM_CONVERSION_FORMAT_x		0x78

char *idio_as_string (IDIO o, size_t *sizep, int depth)
{
    char *r = NULL;
    size_t i;

    IDIO_C_ASSERT (depth >= -10000);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	{
	    idio_unicode_t format = IDIO_FIXNUM_CONVERSION_FORMAT_d;
	    if (idio_S_nil != idio_print_conversion_format_sym) {
		IDIO ipcf = idio_module_symbol_value (idio_print_conversion_format_sym,
						      idio_Idio_module,
						      IDIO_LIST1 (idio_S_false));

		if (idio_S_false != ipcf) {
		    if (idio_isa_unicode (ipcf)) {
			idio_unicode_t f = IDIO_UNICODE_VAL (ipcf);
			switch (f) {
			case IDIO_FIXNUM_CONVERSION_FORMAT_X:
			case IDIO_FIXNUM_CONVERSION_FORMAT_b:
			case IDIO_FIXNUM_CONVERSION_FORMAT_d:
			case IDIO_FIXNUM_CONVERSION_FORMAT_o:
			case IDIO_FIXNUM_CONVERSION_FORMAT_x:
			    format = f;
			    break;
			default:
			    fprintf (stderr, "fixnum-as-string: unexpected conversion format: %c (%#x).  Using 'd'.\n", (int) f, (int) f);
			    format = IDIO_FIXNUM_CONVERSION_FORMAT_d;
			    break;
			}
		    } else {
			size_t s = 0;
			fprintf (stderr, "ipcf isa %s %s\n", idio_type2string (ipcf), idio_as_string (ipcf, &s, 1));
			if (0) {
			IDIO_C_ASSERT (0);
			idio_error_param_type ("unicode", ipcf, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
			}
		    }
		}
	    }

	    switch (format) {
	    case IDIO_FIXNUM_CONVERSION_FORMAT_X:
		if (asprintf (&r, "%" PRIXPTR, (uintptr_t) IDIO_FIXNUM_VAL (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		break;
	    case IDIO_FIXNUM_CONVERSION_FORMAT_b:
		{
		    unsigned long ul = (unsigned long) IDIO_FIXNUM_VAL (o);
		    int j = sizeof (unsigned long)* CHAR_BIT;
		    char bits[j+1];
		    int seen_bit = 0;
		    for (int i = 0; i < j; i++) {
			if (ul & (1UL << (j - 1 - i))) {
			    bits[i] = '1';
			    if (0 == seen_bit) {
				seen_bit = i;
			    }
			} else {
			    bits[i] = '0';
			}
		    }
		    bits[j] = '\0';
		    if (asprintf (&r, "%s", bits + seen_bit) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		}
		break;
	    case IDIO_FIXNUM_CONVERSION_FORMAT_d:
		if (asprintf (&r, "%" PRIdPTR, IDIO_FIXNUM_VAL (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		break;
	    case IDIO_FIXNUM_CONVERSION_FORMAT_o:
		if (asprintf (&r, "%" PRIoPTR, (uintptr_t) IDIO_FIXNUM_VAL (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		break;
	    case IDIO_FIXNUM_CONVERSION_FORMAT_x:
		if (asprintf (&r, "%" PRIxPTR, (uintptr_t) IDIO_FIXNUM_VAL (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		break;
	    default:
		{
		    fprintf (stderr, "fixnum-as-string: unimplemented conversion format: %c (%#x)\n", (int) format, (int) format);
		    idio_error_printf (IDIO_C_FUNC_LOCATION (), "fixnum-as-string unimplemented conversion format");

		    /* notreached */
		    return NULL;
		}
		break;
	    }
	    *sizep = strlen (r);
	    break;
	}
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    /*
	     * character will set r directly but constants will point
	     * t to a fixed string (which gets copied)
	     */
	    char *t = NULL;

	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		{
		    intptr_t v = IDIO_CONSTANT_TOKEN_VAL (o);

		    switch (v) {
		    case IDIO_CONSTANT_NIL:				t = "#n";				break;
		    case IDIO_CONSTANT_UNDEF:				t = "#<undef>";				break;
		    case IDIO_CONSTANT_UNSPEC:				t = "#<unspec>";			break;
		    case IDIO_CONSTANT_EOF:				t = "#<eof>";				break;
		    case IDIO_CONSTANT_TRUE:				t = "#t";				break;
		    case IDIO_CONSTANT_FALSE:				t = "#f";				break;
		    case IDIO_CONSTANT_VOID:				t = "#<void>";				break;
		    case IDIO_CONSTANT_NAN:				t = "#<NaN>";				break;

			/*
			 * We shouldn't really see any of the
			 * following constants but they leak out
			 * especially when the code errors.
			 *
			 * It's then easier to debug if we can read
			 * "PREDEFINED" rather than "C=2001"
			 */
		    case IDIO_STACK_MARKER_PRESERVE_STATE:		t = "#<MARK preserve-state>";		break;
		    case IDIO_STACK_MARKER_PRESERVE_ALL_STATE:		t = "#<MARK preserve-all-state>";	break;
		    case IDIO_STACK_MARKER_PUSH_TRAP:			t = "#<MARK push-trap>";		break;
		    case IDIO_STACK_MARKER_PRESERVE_CONTINUATION:	t = "#<MARK preserve-continuation>";	break;
		    case IDIO_STACK_MARKER_RETURN:			t = "#<MARK return>";			break;
		    case IDIO_STACK_MARKER_DYNAMIC:			t = "#<MARK dynamic>";			break;
		    case IDIO_STACK_MARKER_ENVIRON:			t = "#<MARK environ>";			break;

		    case IDIO_CONSTANT_TOPLEVEL:			t = "#<CONST toplevel>";		break;
		    case IDIO_CONSTANT_PREDEF:				t = "#<CONST predef>";			break;
		    case IDIO_CONSTANT_LOCAL:				t = "#<CONST local>";			break;
		    case IDIO_CONSTANT_ENVIRON:				t = "#<CONST environ>";			break;
		    case IDIO_CONSTANT_COMPUTED:			t = "#<CONST computed>";		break;

			/*
			 * There's a pretty strong argument that if
			 * idio_S_notreached is in *anything* then
			 * something has gone very badly wrong.
			 *
			 * It should only be used to shut the compiler
			 * up after where an error function will have
			 * invoked a non-local exit.
			 */
		    case IDIO_CONSTANT_NOTREACHED:
			{
			    idio_vm_panic (idio_thread_current_thread (), "idio_S_notreached has appeared in userspace!");

			    /* notreached :) */
			    return NULL;
			    break;
			}

		    default:
			if (asprintf (&r, "#<type/constant/idio?? %10p>", o) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			*sizep = strlen (r);
			break;
		    }

		    if (NULL == t) {
			if (asprintf (&r, "#<CONST? %p>", o) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else {
			if (asprintf (&r, "%s", t) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    }
		    *sizep = strlen (r);
		}
		break;
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
		{
		    intptr_t v = IDIO_CONSTANT_IDIO_VAL (o);
		    char m[BUFSIZ];

		    switch (v) {

		    case IDIO_TOKEN_DOT:                           t = "T/.";                         break;
		    case IDIO_TOKEN_LPAREN:                        t = "T/(";                         break;
		    case IDIO_TOKEN_RPAREN:                        t = "T/)";                         break;
		    case IDIO_TOKEN_LBRACE:                        t = "T/{";                         break;
		    case IDIO_TOKEN_RBRACE:                        t = "T/}";                         break;
		    case IDIO_TOKEN_LBRACKET:                      t = "T/[";                         break;
		    case IDIO_TOKEN_RBRACKET:                      t = "T/]";                         break;
		    case IDIO_TOKEN_LANGLE:                        t = "T/<";                         break;
		    case IDIO_TOKEN_RANGLE:                        t = "T/>";                         break;
		    case IDIO_TOKEN_EOL:                           t = "T/EOL";                       break;
		    case IDIO_TOKEN_PAIR_SEPARATOR:                t = "&";                           break;

		    default:
			if (sprintf (m, "#<type/constant/token?? %10p>", o) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			t = m;
			break;
		    }

		    if (NULL == t) {
			if (asprintf (&r, "#<TOKEN=%" PRIdPTR ">", v) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else {
			if (asprintf (&r, "%s", t) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    }
		    *sizep = strlen (r);
		}
		break;

	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		{
		    intptr_t v = IDIO_CONSTANT_I_CODE_VAL (o);
		    char m[BUFSIZ];

		    switch (v) {

		    case IDIO_I_CODE_SHALLOW_ARGUMENT_REF:		t = "SHALLOW-ARGUMENT-REF";		break;
		    case IDIO_I_CODE_DEEP_ARGUMENT_REF:			t = "DEEP-ARGUMENT-REF";		break;

		    case IDIO_I_CODE_SHALLOW_ARGUMENT_SET:		t = "SHALLOW-ARGUMENT-SET";		break;
		    case IDIO_I_CODE_DEEP_ARGUMENT_SET:			t = "DEEP-ARGUMENT-SET";		break;

		    case IDIO_I_CODE_GLOBAL_SYM_REF:			t = "GLOBAL-SYM-REF";			break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_SYM_REF:		t = "CHECKED-GLOBAL-SYM-REF";		break;
		    case IDIO_I_CODE_GLOBAL_FUNCTION_SYM_REF:		t  = "GLOBAL-FUNCTION-SYM-REF";		break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_SYM_REF:	t = "CHECKED-GLOBAL-FUNCTION-SYM-REF";	break;
		    case IDIO_I_CODE_CONSTANT_SYM_REF:			t = "CONSTANT";				break;
		    case IDIO_I_CODE_COMPUTED_SYM_REF:			t = "COMPUTED-SYM-REF";			break;

		    case IDIO_I_CODE_GLOBAL_SYM_DEF:			t = "GLOBAL-SYM-DEF";			break;
		    case IDIO_I_CODE_GLOBAL_SYM_SET:			t = "GLOBAL-SYM-SET";			break;
		    case IDIO_I_CODE_COMPUTED_SYM_SET:			t = "COMPUTED-SYM-SET";			break;
		    case IDIO_I_CODE_COMPUTED_SYM_DEFINE:		t = "COMPUTED-SYM-DEFINE";			break;

		    case IDIO_I_CODE_GLOBAL_VAL_REF:			t = "GLOBAL-VAL-REF";			break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_VAL_REF:		t = "CHECKED-GLOBAL-VAL-REF";		break;
		    case IDIO_I_CODE_GLOBAL_FUNCTION_VAL_REF:		t  = "GLOBAL-FUNCTION-VAL-REF";		break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_VAL_REF:	t = "CHECKED-GLOBAL-FUNCTION-VAL-REF";	break;
		    case IDIO_I_CODE_CONSTANT_VAL_REF:			t = "CONSTANT";				break;
		    case IDIO_I_CODE_COMPUTED_VAL_REF:			t = "COMPUTED-VAL-REF";			break;

		    case IDIO_I_CODE_GLOBAL_VAL_DEF:			t = "GLOBAL-VAL-DEF";			break;
		    case IDIO_I_CODE_GLOBAL_VAL_SET:			t = "GLOBAL-VAL-SET";			break;
		    case IDIO_I_CODE_COMPUTED_VAL_SET:			t = "COMPUTED-VAL-SET";			break;
		    case IDIO_I_CODE_COMPUTED_VAL_DEFINE:		t = "COMPUTED-VAL-DEFINE";			break;

		    case IDIO_I_CODE_PREDEFINED:			t = "PREDEFINED";			break;
		    case IDIO_I_CODE_ALTERNATIVE:			t = "ALTERNATIVE";			break;
		    case IDIO_I_CODE_SEQUENCE:				t = "SEQUENCE";				break;
		    case IDIO_I_CODE_TR_FIX_LET:			t = "TR-FIX-LET";			break;
		    case IDIO_I_CODE_FIX_LET:				t = "FIX-LET";				break;

		    case IDIO_I_CODE_PRIMCALL0:				t = "PRIMCALL0";			break;
		    case IDIO_I_CODE_PRIMCALL1:				t = "PRIMCALL1";			break;
		    case IDIO_I_CODE_PRIMCALL2:				t = "PRIMCALL2";			break;
		    case IDIO_I_CODE_PRIMCALL3:				t = "PRIMCALL3";			break;
		    case IDIO_I_CODE_TR_REGULAR_CALL:			t = "TR-REGULAR-CALL";			break;
		    case IDIO_I_CODE_REGULAR_CALL:			t = "REGULAR-CALL";			break;

		    case IDIO_I_CODE_FIX_CLOSURE:			t = "FIX-CLOSURE";			break;
		    case IDIO_I_CODE_NARY_CLOSURE:			t = "NARY-CLOSURE";			break;

		    case IDIO_I_CODE_STORE_ARGUMENT:			t = "STORE-ARGUMENT";			break;
		    case IDIO_I_CODE_CONS_ARGUMENT:			t = "CONS-ARGUMENT";			break;

		    case IDIO_I_CODE_ALLOCATE_FRAME:			t = "ALLOCATE-FRAME";			break;
		    case IDIO_I_CODE_ALLOCATE_DOTTED_FRAME:		t = "ALLOCATE-DOTTED-FRAME";		break;
		    case IDIO_I_CODE_REUSE_FRAME:			t = "REUSE-FRAME";			break;

		    case IDIO_I_CODE_DYNAMIC_SYM_REF:			t = "DYNAMIC-SYM-REF";			break;
		    case IDIO_I_CODE_DYNAMIC_FUNCTION_SYM_REF:		t = "DYNAMIC-FUNCTION-SYM-REF";		break;
		    case IDIO_I_CODE_DYNAMIC_VAL_REF:			t = "DYNAMIC-VAL-REF";			break;
		    case IDIO_I_CODE_DYNAMIC_FUNCTION_VAL_REF:		t = "DYNAMIC-FUNCTION-VAL-REF";		break;
		    case IDIO_I_CODE_PUSH_DYNAMIC:			t = "PUSH-DYNAMIC";			break;
		    case IDIO_I_CODE_POP_DYNAMIC:			t = "POP-DYNAMIC";			break;

		    case IDIO_I_CODE_ENVIRON_SYM_REF:			t = "ENVIRON-SYM-REF";			break;
		    case IDIO_I_CODE_ENVIRON_VAL_REF:			t = "ENVIRON-VAL-REF";			break;
		    case IDIO_I_CODE_PUSH_ENVIRON:			t = "PUSH-ENVIRON";			break;
		    case IDIO_I_CODE_POP_ENVIRON:			t = "POP-ENVIRON";			break;

		    case IDIO_I_CODE_PUSH_TRAP:				t = "PUSH-TRAP";			break;
		    case IDIO_I_CODE_POP_TRAP:				t = "POP-TRAP";				break;

		    case IDIO_I_CODE_AND:				t = "AND";				break;
		    case IDIO_I_CODE_OR:				t = "OR";				break;
		    case IDIO_I_CODE_BEGIN:				t = "BEGIN";				break;

		    case IDIO_I_CODE_EXPANDER:				t = "EXPANDER";				break;
		    case IDIO_I_CODE_INFIX_OPERATOR:			t = "INFIX-OPERATOR";			break;
		    case IDIO_I_CODE_POSTFIX_OPERATOR:			t = "POSTFIX-OPERATOR";			break;

		    case IDIO_I_CODE_ABORT:				t = "ABORT";				break;
		    case IDIO_I_CODE_FINISH:				t = "FINISH";				break;
		    case IDIO_I_CODE_NOP:				t = "NOP";				break;

		    default:
			if (sprintf (m, "#<type/constant/vm_code?? o=%10p v=%tx>", o, v) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			t = m;
			break;
		    }

		    if (NULL == t) {
			if (asprintf (&r, "#<I-CODE? %p>", o) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else {
			if (asprintf (&r, "%s", t) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    }
		    *sizep = strlen (r);
		}
		break;

	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		{
		    intptr_t c = IDIO_CHARACTER_VAL (o);
		    switch (c) {
		    case ' ':
			if (asprintf (&r, "#\\space") == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			break;
		    case '\n':
			if (asprintf (&r, "#\\newline") == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			break;
		    default:
			if (isprint (c)) {
			    if (asprintf (&r, "#\\%c", (char) c) == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			} else {
			    if (asprintf (&r, "#\\%#" PRIxPTR, c) == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			}
			break;
		    }
		    *sizep = strlen (r);
		    break;
		}
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		{
		    idio_unicode_t u = IDIO_UNICODE_VAL (o);
		    if (u <= 0x7f &&
			isgraph (u)) {
			if (asprintf (&r, "#\\%c", u) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else {
			if (asprintf (&r, "#U+%04X", u) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    }
		    *sizep = strlen (r);
		    break;
		}
	    default:
		if (asprintf (&r, "#<type/constant?? %10p>", o) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    }
	}
	break;
    case IDIO_TYPE_PLACEHOLDER_MARK:
	if (asprintf (&r, "#<type/placecholder?? %10p>", o) == -1) {
	    idio_error_alloc ("asprintf");

	    /* notreached */
	    return NULL;
	}
	*sizep = strlen (r);
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    idio_type_e type = idio_type (o);

	    if (depth <= 0) {
		if (asprintf (&r, "..") == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		return r;
	    }

	    switch (type) {
	    case IDIO_TYPE_NONE:
		fprintf (stderr, "TYPE_NONE in flight :(\n");
		if (asprintf (&r, "#<!! -none- %p>", o) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_STRING:
		r = idio_utf8_string (o, sizep, IDIO_UTF8_STRING_ESCAPES, IDIO_UTF8_STRING_QUOTED);
		break;
	    case IDIO_TYPE_SUBSTRING:
		r = idio_utf8_string (o, sizep, IDIO_UTF8_STRING_ESCAPES, IDIO_UTF8_STRING_QUOTED);
		break;
	    case IDIO_TYPE_SYMBOL:
		if (asprintf (&r, "%s", IDIO_SYMBOL_S (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_KEYWORD:
		if (asprintf (&r, ":%s", IDIO_KEYWORD_S (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_PAIR:
		/*
		  Technically a list (of pairs) should look like:

		  "(a . (b . (c . (d . nil))))"

		  but tradition dictates that we should flatten
		  the list to:

		  "(a b c d)"

		  hence the while loop which continues if the tail is
		  itself a pair
		*/
		{
		    if (idio_isa_symbol (IDIO_PAIR_H (o))) {
			int special = 0;
			char *trail = NULL;

			if (idio_S_quote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, "'") == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			} else if (idio_S_unquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, "$") == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			} else if (idio_S_unquotesplicing == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, "$@") == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			} else if (idio_S_quasiquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    trail = " }";
			    if (asprintf (&r, "#T{ ") == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			}
			if (NULL != r) {
			    *sizep = strlen (r);
			}

			if (special) {
			    if (idio_isa_pair (IDIO_PAIR_T (o))) {
				size_t hs_size = 0;
				char *hs = idio_as_string (idio_list_head (IDIO_PAIR_T (o)), &hs_size, depth - 1);
				IDIO_STRCAT_FREE (r, sizep, hs, hs_size);
			    } else {
				size_t ts_size = 0;
				char *ts = idio_as_string (IDIO_PAIR_T (o), &ts_size, depth - 1);
				IDIO_STRCAT_FREE (r, sizep, ts, ts_size);
			    }

			    if (NULL != trail) {
				IDIO_STRCAT (r, sizep, trail);
			    }
			    break;
			}
		    }

		    if (asprintf (&r, "(") == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    while (1) {
			size_t hs_size = 0;
			char *hs = idio_as_string (IDIO_PAIR_H (o), &hs_size, depth - 1);
			IDIO_STRCAT_FREE (r, sizep, hs, hs_size);

			o = IDIO_PAIR_T (o);
			if (idio_type (o) != IDIO_TYPE_PAIR) {
			    if (idio_S_nil != o) {
				char *ps;
				if (asprintf (&ps, " %c ", IDIO_PAIR_SEPARATOR) == -1) {
				    free (r);
				    idio_error_alloc ("asprintf");

				    /* notreached */
				    return NULL;
				}
				/* assumming IDIO_PAIR_SEPARATOR is 1 byte */
				size_t ps_size = strlen (ps);
				IDIO_STRCAT_FREE (r, sizep, ps, ps_size);

				size_t t_size = 0;
				char *t = idio_as_string (o, &t_size, depth - 1);
				IDIO_STRCAT_FREE (r, sizep, t, t_size);
			    }
			    break;
			} else {
			    IDIO_STRCAT (r, sizep, " ");
			}
		    }
		    IDIO_STRCAT (r, sizep, ")");
		}
		break;
	    case IDIO_TYPE_ARRAY:
		if (asprintf (&r, "#[ ") == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		if (depth > 0) {
		    if (IDIO_ARRAY_USIZE (o) < 40) {
			for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			    size_t t_size = 0;
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), &t_size, depth - 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);
			    IDIO_STRCAT (r, sizep, " ");
			}
		    } else {
			for (i = 0; i < 20; i++) {
			    size_t t_size = 0;
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), &t_size, depth - 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);
			    IDIO_STRCAT (r, sizep, " ");
			}
			char *aei;
			if (asprintf (&aei, "..[%zd] ", IDIO_ARRAY_USIZE (o) - 20) == -1) {
			    free (r);
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			size_t aei_size = strlen (aei);
			IDIO_STRCAT_FREE (r, sizep, aei, aei_size);
			for (i = IDIO_ARRAY_USIZE (o) - 20; i < IDIO_ARRAY_USIZE (o); i++) {
			    size_t t_size = 0;
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), &t_size, depth - 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);
			    IDIO_STRCAT (r, sizep, " ");
			}
		    }
		} else {
		    IDIO_STRCAT (r, sizep, ".. ");
		}
		IDIO_STRCAT (r, sizep, "]");
		break;
	    case IDIO_TYPE_HASH:
		if (asprintf (&r, "#{ ") == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		if (depth > 0) {
		    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
			    /*
			     * We're looking to generate:
			     *
			     * (k & v)
			     *
			     */
			    IDIO_STRCAT (r, sizep, "(");

			    size_t t_size = 0;
			    char *t;
			    if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) {
				if (asprintf (&t, "%s", (char *) IDIO_HASH_HE_KEY (o, i)) == -1) {
				    free (r);
				    idio_error_alloc ("asprintf");

				    /* notreached */
				    return NULL;
				}

				t = (char *) IDIO_HASH_HE_KEY (o, i);
				t_size = strlen (t);
			    } else {
				t = idio_as_string (IDIO_HASH_HE_KEY (o, i), &t_size, depth - 1);
			    }
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    char *hes;
			    if (asprintf (&hes, " %c ", IDIO_PAIR_SEPARATOR) == -1) {
				free (r);
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			    size_t hes_size = strlen (hes);
			    IDIO_STRCAT_FREE (r, sizep, hes, hes_size);

			    if (IDIO_HASH_HE_VALUE (o, i)) {
				t_size = 0;
				t = idio_as_string (IDIO_HASH_HE_VALUE (o, i), &t_size, depth - 1);
				IDIO_STRCAT_FREE (r, sizep, t, t_size);
			    } else {
				IDIO_STRCAT (r, sizep, "-");
			    }
			    IDIO_STRCAT (r, sizep, ")");
			}
		    }
		} else {
		    IDIO_STRCAT (r, sizep, "..");
		}
		IDIO_STRCAT (r, sizep, "}");
		break;
	    case IDIO_TYPE_CLOSURE:
		{
		    if (asprintf (&r, "#<CLOS ") == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    IDIO name = idio_get_property (o, idio_KW_name, IDIO_LIST1 (idio_S_nil));
		    if (idio_S_nil != name) {
			char *name_C;
			if (asprintf (&name_C, "%s ", IDIO_SYMBOL_S (name)) == -1) {
			    free (r);
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			size_t name_size = strlen (name_C);
			IDIO_STRCAT_FREE (r, sizep, name_C, name_size);
		    } else {
			IDIO_STRCAT (r, sizep, "- ");
		    }

		    char *t;
		    if (asprintf (&t, "@%zd/%p/", IDIO_CLOSURE_CODE_PC (o), IDIO_CLOSURE_FRAME (o)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    size_t t_size = strlen (t);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    size_t mn_size = 0;
		    char *mn = idio_as_string (IDIO_MODULE_NAME (IDIO_CLOSURE_ENV (o)), &mn_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, mn, mn_size);

		    IDIO_STRCAT (r, sizep, ">");
		    break;
		}
	    case IDIO_TYPE_PRIMITIVE:
		if (asprintf (&r, "#<PRIM %s>", IDIO_PRIMITIVE_NAME (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_BIGNUM:
		{
		    r = idio_bignum_as_string (o, sizep);
		    break;
		}
	    case IDIO_TYPE_MODULE:
		{
		    if (asprintf (&r, "#<module ") == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    if (idio_S_nil == IDIO_MODULE_NAME (o)) {
			IDIO_STRCAT (r, sizep, "(nil)");
		    } else {
			size_t mn_size = 0;
			char *mn = idio_as_string (IDIO_MODULE_NAME (o), &mn_size, depth - 1);
			IDIO_STRCAT_FREE (r, sizep, mn, mn_size);
		    }
		    if (0 && depth > 0) {
			IDIO_STRCAT (r, sizep, " exports=");
			if (idio_S_nil == IDIO_MODULE_EXPORTS (o)) {
			    IDIO_STRCAT (r, sizep, "(nil)");
			} else {
			    size_t e_size = 0;
			    char *es = idio_as_string (IDIO_MODULE_EXPORTS (o), &e_size, depth - 1);
			    IDIO_STRCAT_FREE (r, sizep, es, e_size);
			}
			IDIO_STRCAT (r, sizep, " imports=");
			if (idio_S_nil == IDIO_MODULE_IMPORTS (o)) {
			    IDIO_STRCAT (r, sizep, "(nil)");
			} else {
			    size_t i_size = 0;
			    char *is = idio_as_string (IDIO_MODULE_IMPORTS (o), &i_size, 0);
			    IDIO_STRCAT_FREE (r, sizep, is, i_size);
			}
			IDIO_STRCAT (r, sizep, " symbols=");
			if (idio_S_nil == IDIO_MODULE_SYMBOLS (o)) {
			    IDIO_STRCAT (r, sizep, "(nil)");
			} else {
			    size_t s_size = 0;
			    char *ss = idio_as_string (IDIO_MODULE_SYMBOLS (o), &s_size, depth - 1);
			    IDIO_STRCAT_FREE (r, sizep, ss, s_size);
			}
		    }
		    IDIO_STRCAT (r, sizep, ">");
		    break;
		}
	    case IDIO_TYPE_FRAME:
		{
		    if (asprintf (&r, "#<FRAME %p n=%td [ ", o, IDIO_FRAME_NARGS (o)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    for (i = 0; i < IDIO_FRAME_NARGS (o); i++) {
			size_t t_size = 0;
			char *t = idio_as_string (IDIO_FRAME_ARGS (o, i), &t_size, depth - 1);
			IDIO_STRCAT_FREE (r, sizep, t, t_size);
			IDIO_STRCAT (r, sizep, " ");
		    }
		    IDIO_STRCAT (r, sizep, "]>");
		    break;
		}
	    case IDIO_TYPE_HANDLE:
		{
		    if (asprintf (&r, "#<H ") == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    IDIO_FLAGS_T h_flags = IDIO_HANDLE_FLAGS (o);
		    if (h_flags & IDIO_HANDLE_FLAG_CLOSED) {
			IDIO_STRCAT (r, sizep, "c");
		    } else {
			IDIO_STRCAT (r, sizep, "o");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_STRING) {
			IDIO_STRCAT (r, sizep, "s");
		    } else if (h_flags & IDIO_HANDLE_FLAG_FILE) {
			IDIO_STRCAT (r, sizep, "f");
		    }

		    if (h_flags & IDIO_HANDLE_FLAG_READ) {
			IDIO_STRCAT (r, sizep, "r");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_WRITE) {
			IDIO_STRCAT (r, sizep, "w");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_FILE) {

			IDIO_FLAGS_T s_flags = IDIO_FILE_HANDLE_FLAGS (o);
			if (s_flags & IDIO_FILE_HANDLE_FLAG_CLOEXEC) {
			    IDIO_STRCAT (r, sizep, "E");
			} else {
			    IDIO_STRCAT (r, sizep, "!");
			}

			char *fds;
			if (asprintf (&fds, "%4d", idio_file_handle_fd (o)) == -1) {
			    free (r);
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			size_t fds_size = strlen (fds);
			IDIO_STRCAT_FREE (r, sizep, fds, fds_size);
		    }

		    /*
		     * XXX can a handle name contain a NUL?
		     */
		    char *sname = idio_handle_name_as_C (o);
		    char *info;
		    if (asprintf (&info, ":\"%s\":%jd:%jd>", sname, (intmax_t) IDIO_HANDLE_LINE (o), (intmax_t) IDIO_HANDLE_POS (o)) == -1) {
			free (sname);
			free (r);
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    free (sname);
		    size_t info_size = strlen (info);
		    IDIO_STRCAT_FREE (r, sizep, info, info_size);
		}
		break;
	    case IDIO_TYPE_C_INT:
		if (asprintf (&r, "%jd", IDIO_C_TYPE_INT (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_UINT:
		if (asprintf (&r, "%ju", IDIO_C_TYPE_UINT (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_FLOAT:
		if (asprintf (&r, "%g", IDIO_C_TYPE_FLOAT (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_DOUBLE:
		if (asprintf (&r, "%g", IDIO_C_TYPE_DOUBLE (o)) == -1) {
		    idio_error_alloc ("asprintf");

		    /* notreached */
		    return NULL;
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_POINTER:
		if (NULL != IDIO_C_TYPE_POINTER_PRINTER (o)) {
		    char *s = IDIO_C_TYPE_POINTER_PRINTER (o) (o);
		    *sizep = strlen (s);
		    return s;
		} else {
		    if (asprintf (&r, "#<C/* %p%s>", IDIO_C_TYPE_POINTER_P (o), IDIO_C_TYPE_POINTER_FREEP (o) ? " free" : "") == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		{
		    if (asprintf (&r, "#<ST %p ", o) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    size_t stn_size = 0;
		    char *stn = idio_as_string (IDIO_STRUCT_TYPE_NAME (o), &stn_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, stn, stn_size);
		    IDIO_STRCAT (r, sizep, " ");

		    size_t stp_size = 0;
		    char *stp = idio_as_string (IDIO_STRUCT_TYPE_PARENT (o), &stp_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, stp, stp_size);

		    size_t size = IDIO_STRUCT_TYPE_SIZE (o);
		    size_t i;
		    for (i = 0; i < size; i++) {
			IDIO_STRCAT (r, sizep, " ");
			size_t f_size = 0;
			char *fs = idio_as_string (IDIO_STRUCT_TYPE_FIELDS (o, i), &f_size, 1);
			IDIO_STRCAT_FREE (r, sizep, fs, f_size);
		    }

		    IDIO_STRCAT (r, sizep, ">");
		}
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		{
		    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (o);
		    IDIO value_as_string = idio_module_symbol_value (idio_util_value_as_string, idio_Idio_module, idio_S_nil);

		    if (idio_S_nil != value_as_string) {
			IDIO s = idio_S_nil;
			IDIO l = idio_list_assq (sit, value_as_string);
			if (idio_S_false != l) {
			    IDIO thr = idio_thread_current_thread ();

			    IDIO cmd = IDIO_LIST3 (IDIO_PAIR_HT (l), o, idio_S_nil);

			    s = idio_vm_invoke_C (thr, cmd);
			}

			if (idio_S_nil != s) {
			    return idio_as_string (s, sizep, 1);
			}
		    }

		    if (asprintf (&r, "#<SI %p ", o) == -1) {
			idio_error_alloc ("asprintf");
			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    size_t n_size = 0;
		    char *ns = idio_as_string (IDIO_STRUCT_TYPE_NAME (sit), &n_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, ns, n_size);

		    size_t size = IDIO_STRUCT_TYPE_SIZE (sit);
		    size_t i;
		    for (i = 0; i < size; i++) {
			IDIO_STRCAT (r, sizep, " ");
			size_t fn_size = 0;
			char *fns = idio_as_string (IDIO_STRUCT_TYPE_FIELDS (sit, i), &fn_size, 1);
			IDIO_STRCAT_FREE (r, sizep, fns, fn_size);
			IDIO_STRCAT (r, sizep, ":");
			size_t fv_size = 0;
			char *fvs = idio_as_string (IDIO_STRUCT_INSTANCE_FIELDS (o, i), &fv_size, depth - 1);
			IDIO_STRCAT_FREE (r, sizep, fvs, fv_size);
		    }

		    IDIO_STRCAT (r, sizep, ">");
		}
		break;
	    case IDIO_TYPE_THREAD:
		{
		    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (o));
		    if (asprintf (&r, "#<THREAD %p\n  pc=%6zd\n  sp/top=%2zd/",
				  o,
				  IDIO_THREAD_PC (o),
				  sp - 1) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    size_t t_size = 0;
		    char *t = idio_as_string (idio_array_top (IDIO_THREAD_STACK (o)), &t_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  val=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_VAL (o), &t_size, 2);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  func=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_FUNC (o), &t_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    if (1 == depth) {
			IDIO frame = IDIO_THREAD_FRAME (o);

			if (idio_S_nil == frame) {
			    IDIO_STRCAT (r, sizep, "\n  fr=nil");
			} else {
			    char *es;
			    if (asprintf (&es, "\n  fr=%p n=%td ", frame, IDIO_FRAME_NARGS (frame)) == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			    size_t es_size = strlen (es);
			    IDIO_STRCAT_FREE (r, sizep, es, es_size);

			    size_t f_size = 0;
			    char *fs = idio_as_string (frame, &f_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, fs, f_size);
			}
		    }

		    IDIO_STRCAT (r, sizep, "\n  env=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_ENV (o), &t_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  t/sp=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_TRAP_SP (o), &t_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  d/sp=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_DYNAMIC_SP (o), &t_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  e/sp=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_ENVIRON_SP (o), &t_size, 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    if (depth > 1) {
			IDIO_STRCAT (r, sizep, "\n  fr=");
			t_size = 0;
			t = idio_as_string (IDIO_THREAD_FRAME (o), &t_size, 1);
			IDIO_STRCAT_FREE (r, sizep, t, t_size);

			if (depth > 2) {
			    IDIO_STRCAT (r, sizep, "\n  reg1=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_REG1 (o), &t_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  reg2=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_REG2 (o), &t_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  expr=");
			    IDIO fmci = IDIO_THREAD_EXPR (o);
			    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
			    idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

			    IDIO src = idio_vm_constants_ref (gci);

			    t_size = 0;
			    t = idio_as_string (src, &t_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  input_handle=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_INPUT_HANDLE (o), &t_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  output_handle=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_OUTPUT_HANDLE (o), &t_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  error_handle=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_ERROR_HANDLE (o), &t_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  module=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_MODULE (o), &t_size, 1);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);
			}
		    }
		    IDIO_STRCAT (r, sizep, ">");
		}
		break;
	    case IDIO_TYPE_CONTINUATION:
		{
		    IDIO ks = IDIO_CONTINUATION_STACK (o);
		    idio_ai_t kss = idio_array_size (ks);
		    IDIO pc_I = idio_array_get_index (ks, kss - 2);

		    /*
		     * We preserved some of the continuation state on
		     * the continuation stack so the actual
		     * continuation stack size is kss minus eight.
		     * Check idio_continuation().
		     */
		    if (asprintf (&r, "#<K %p ss=%zu PC=%td>", o, kss - 8, IDIO_FIXNUM_VAL (pc_I)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);
		}
		break;
	    case IDIO_TYPE_BITSET:
		{
		    if (asprintf (&r, "#B{ %zu ", IDIO_BITSET_SIZE (o)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    size_t bs_size = IDIO_BITSET_SIZE (o);
		    size_t count = 0;
		    int print_lead = 0;
		    int in_range = 0;
		    size_t range_start = 0;
		    size_t n_ul = bs_size / IDIO_BITS_PER_LONG + 1;
		    for (i = 0; i < n_ul; i++) {
			/*
			 * Native format is chunked into unsigned
			 * longs
			 */
			unsigned long ul_bits = IDIO_BITSET_BITS (o, i);

			if (ul_bits) {
			    int b;
			    for (b = 0; count < bs_size &&
				     b < sizeof (unsigned long); b++) {
				/*
				 * Portable format is chunked into bytes
				 */
				/*
				 * XXX can we rely on UCHAR_MAX
				 * (255 if CHAR_BIT is 8) meaning
				 * all bits are set?  Does it
				 * assume two's complement?
				 */
				size_t offset = b * CHAR_BIT;
				unsigned long mask = UCHAR_MAX;
				mask <<= offset;
				unsigned long byte_bits = ul_bits & mask;

				if (byte_bits) {
				    if (byte_bits == mask) {
					if (0 == in_range) {
					    in_range = 1;
					    range_start = count;
					}
					count += CHAR_BIT;
					print_lead = 1;
					continue;
				    }
				    if (print_lead) {
					print_lead = 0;
					char *lead;
					if (in_range) {
					    if (asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT) == -1) {
						idio_error_alloc ("asprintf");

						/* notreached */
						return NULL;
					    }
					    size_t lead_size = strlen (lead);
					    IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
					    in_range = 0;
					} else {
					    if (asprintf (&lead, "%zx:", count) == -1) {
						idio_error_alloc ("asprintf");

						/* notreached */
						return NULL;
					    }
					    size_t lead_size = strlen (lead);
					    IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
					}
				    }
				    char bits[CHAR_BIT + 1];
				    unsigned int j;
				    for (j = 0; j < CHAR_BIT; j++) {
					if (ul_bits & (1UL << (offset + j))) {
					    bits[j] = '1';
					} else {
					    bits[j] = '0';
					}
					count++;
					if (count > bs_size){
					    break;
					}
				    }
				    bits[j] = '\0';

				    IDIO_STRCAT (r, sizep, bits);
				    IDIO_STRCAT (r, sizep, " ");
				} else {
				    char *lead;
				    if (in_range) {
					if (asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT) == -1) {
					    idio_error_alloc ("asprintf");

					    /* notreached */
					    return NULL;
					}
					size_t lead_size = strlen (lead);
					IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
					in_range = 0;
				    }

				    count += CHAR_BIT;
				    print_lead = 1;
				}
			    }
			} else {
			    if (in_range) {
				char *lead;
				if (asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT) == -1) {
				    idio_error_alloc ("asprintf");

				    /* notreached */
				    return NULL;
				}
				size_t lead_size = strlen (lead);
				IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
				in_range = 0;
			    }

			    count += IDIO_BITS_PER_LONG;
			    print_lead = 1;
			}
		    }
		    if (in_range) {
			char *lead;
			if (asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
			size_t lead_size = strlen (lead);
			IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
			in_range = 0;
		    }
		    IDIO_STRCAT (r, sizep, "}");
		}
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		{
		    if (asprintf (&r, "#<C/typedef %10p>", IDIO_C_TYPEDEF_SYM (o)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);
		    break;
		}
	    case IDIO_TYPE_C_STRUCT:
		{
		    if (asprintf (&r, "#<C/struct %10p ", o) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    IDIO_STRCAT (r, sizep, "\n\tfields: ");
		    size_t f_size = 0;
		    char *fs = idio_as_string (IDIO_C_STRUCT_FIELDS (o), &f_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, fs, f_size);

		    IDIO mh = IDIO_C_STRUCT_METHODS (o);

		    IDIO_STRCAT (r, sizep, "\n\tmethods: ");
		    if (idio_S_nil != mh) {
			for (i = 0; i < IDIO_HASH_SIZE (mh); i++) {
			    if (idio_S_nil != IDIO_HASH_HE_KEY (mh, i)) {
				IDIO_STRCAT (r, sizep, "\n\t");
				size_t t_size = 0;
				char *t = idio_as_string (IDIO_HASH_HE_KEY (mh, i), &t_size, depth - 1);
				IDIO_STRCAT_FREE (r, sizep, t, t_size);
				IDIO_STRCAT (r, sizep, ":");
				if (IDIO_HASH_HE_VALUE (mh, i)) {
				    t_size = 0;
				    t = idio_as_string (IDIO_HASH_HE_VALUE (mh, i), &t_size, depth - 1);
				} else {
				    if (asprintf (&t, "-") == -1) {
					free (r);
					idio_error_alloc ("asprintf");

					/* notreached */
					return NULL;
				    }
				    t_size = strlen (t);
				}
				IDIO_STRCAT_FREE (r, sizep, t, t_size);
				IDIO_STRCAT (r, sizep, " ");
			    }
			}
		    }
		    IDIO_STRCAT (r, sizep, "\n\tframe: ");
		    f_size = 0;
		    fs = idio_as_string (IDIO_C_STRUCT_FRAME (o), &f_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, fs, f_size);
		    IDIO_STRCAT (r, sizep, "\n>");
		    break;
		}
	    case IDIO_TYPE_C_INSTANCE:
		{
		    if (asprintf (&r, "#<C/instance %10p C/*=%10p C/struct=%10p>", o, IDIO_C_INSTANCE_P (o), IDIO_C_INSTANCE_C_STRUCT (o)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);
		    break;
		}
	    case IDIO_TYPE_C_FFI:
		{
		    if (asprintf (&r, "#<CFFI ") == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    size_t t_size = 0;
		    char *t = idio_as_string (IDIO_C_FFI_NAME (o), &t_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_SYMBOL (o), &t_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " ");

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_ARGS (o), &t_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " ");

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_RESULT (o), &t_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " ");

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_NAME (o), &t_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " >");
		    break;
		}
	    case IDIO_TYPE_OPAQUE:
		{
		    if (asprintf (&r, "#<O %10p ", IDIO_OPAQUE_P (o)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);

		    size_t t_size = 0;
		    char *t = idio_as_string (IDIO_OPAQUE_ARGS (o), &t_size, depth - 1);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, ">");
		    break;
		}
	    default:
		{
		    fprintf (stderr, "idio_as_string: unexpected type %d\n", type);
		    /*
		     * Oh dear, bad data.  Can we dump any clues?
		     *
		     * If it's a short enough string then its length
		     * will be less than 40 chars, otherwise we can
		     * only dump out a C pointer.
		     *
		     * Of course the string can still be gobbledegook
		     * but it's something to go on if we've trampled
		     * on a hash's string key.
		     */
		    size_t n = strnlen ((char *) o, 40);
		    if (40 == n) {
			if (asprintf (&r, "#<void?? %10p>", o) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else {
			if (asprintf (&r, "#<string?? \"%s\">", (char *) o) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    }
		    *sizep = strlen (r);
		    IDIO_C_ASSERT (0);
		}
		break;
	    }
	}
	break;
    default:
	if (asprintf (&r, "#<type?? %10p>", o) == -1) {
	    idio_error_alloc ("asprintf");

	    /* notreached */
	    return NULL;
	}
	*sizep = strlen (r);
	break;
    }

    return r;
}

/*
  Scheme-ish display -- no internal representation (where
  appropriate).  Unsuitable for (read).  Primarily:

  UNICODE #\a:		a
  UNICODE #U+FFFD	�
  STRING "foo":		foo

  Most non-data types will still come out as some internal
  representation.  (Still unsuitable for (read) as it doesn't know
  about them.)
 */
char *idio_display_string (IDIO o, size_t *sizep)
{
    char *r = NULL;

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	r = idio_as_string (o, sizep, 4);
	break;
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		r = idio_as_string (o, sizep, 4);
		break;
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		{
		    intptr_t c = IDIO_CHARACTER_VAL (o);
		    if (asprintf (&r, "%c", (char) c) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    *sizep = strlen (r);
		}
		break;
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		{
		    idio_unicode_t u = IDIO_UNICODE_VAL (o);
		    if (u > 0x10ffff) {
			fprintf (stderr, "display-string: oops u=%x > 0x10ffff\n", u);
		    } else if (u >= 0x10000) {
			if (asprintf (&r, "%c%c%c%c",
				      0xf0 | ((u & (0x07 << 18)) >> 18),
				      0x80 | ((u & (0x3f << 12)) >> 12),
				      0x80 | ((u & (0x3f << 6)) >> 6),
				      0x80 | ((u & (0x3f << 0)) >> 0)) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else if (u >= 0x0800) {
			if (asprintf (&r, "%c%c%c",
				      0xe0 | ((u & (0x0f << 12)) >> 12),
				      0x80 | ((u & (0x3f << 6)) >> 6),
				      0x80 | ((u & (0x3f << 0)) >> 0)) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else if (u >= 0x0080) {
			if (asprintf (&r, "%c%c",
				      0xc0 | ((u & (0x1f << 6)) >> 6),
				      0x80 | ((u & (0x3f << 0)) >> 0)) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    } else {
			if (asprintf (&r, "%c",
				      u & 0x7f) == -1) {
			    idio_error_alloc ("asprintf");

			    /* notreached */
			    return NULL;
			}
		    }
		    *sizep = strlen (r);
		}
		break;
	    }
	}
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_STRING:
		{
		    return idio_utf8_string (o, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED);
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		{
		    return idio_utf8_string (o, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED);
		}
		break;
	    default:
		r = idio_as_string (o, sizep, 40);
		break;
	    }
	}
	break;
    default:
	if (asprintf (&r, "type %d n/k", o->type) == -1) {
	    idio_error_alloc ("asprintf");

	    /* notreached */
	    return NULL;
	}
	*sizep = strlen (r);
	break;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("string", string, (IDIO o), "o", "\
convert `o` to a string				\n\
						\n\
:param o: object to convert			\n\
						\n\
:return: a string representation of `o`	\n\
")
{
    IDIO_ASSERT (o);

    size_t size = 0;
    char *str = idio_as_string (o, &size, 40);
    IDIO r = idio_string_C_len (str, size);
    free (str);
    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("display-string", display_string, (IDIO o), "o", "\
convert `o` to a display string			\n\
						\n\
:param o: object to convert			\n\
						\n\
:return: a string representation of `o`	\n\
")
{
    IDIO_ASSERT (o);

    size_t size = 0;
    char *str = idio_display_string (o, &size);
    IDIO r = idio_string_C_len (str, size);
    free (str);
    return r;
}

const char *idio_vm_bytecode2string (int code)
{
    char *r;

    switch (code) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:			r = "SHALLOW-ARGUMENT-REF0";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:			r = "SHALLOW-ARGUMENT-REF1";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:			r = "SHALLOW-ARGUMENT-REF2";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:			r = "SHALLOW-ARGUMENT-REF3";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:			r = "SHALLOW-ARGUMENT-REF";		break;
    case IDIO_A_DEEP_ARGUMENT_REF:			r = "DEEP-ARGUMENT-REF";		break;

    case IDIO_A_SHALLOW_ARGUMENT_SET0:			r = "SHALLOW-ARGUMENT-SET0";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:			r = "SHALLOW-ARGUMENT-SET1";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:			r = "SHALLOW-ARGUMENT-SET2";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:			r = "SHALLOW-ARGUMENT-SET3";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:			r = "SHALLOW-ARGUMENT-SET";		break;
    case IDIO_A_DEEP_ARGUMENT_SET:			r = "DEEP-ARGUMENT-SET";		break;

    case IDIO_A_GLOBAL_SYM_REF:				r = "GLOBAL-SYM-REF";			break;
    case IDIO_A_CHECKED_GLOBAL_SYM_REF:			r = "CHECKED-GLOBAL-SYM-REF";		break;
    case IDIO_A_GLOBAL_FUNCTION_SYM_REF:		r = "GLOBAL-FUNCTION-SYM-REF";		break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF:	r = "CHECKED-GLOBAL-FUNCTION-SYM-REF";	break;
    case IDIO_A_CONSTANT_SYM_REF:			r = "CONSTANT-SYM-REF";			break;
    case IDIO_A_COMPUTED_SYM_REF:			r = "COMPUTED-SYM-REF";			break;

    case IDIO_A_GLOBAL_SYM_DEF:				r = "GLOBAL-SYM-DEF";			break;
    case IDIO_A_GLOBAL_SYM_SET:				r = "GLOBAL-SYM-SET";			break;
    case IDIO_A_COMPUTED_SYM_SET:			r = "COMPUTED-SYM-SET";			break;
    case IDIO_A_COMPUTED_SYM_DEFINE:			r = "COMPUTED-SYM-DEFINE";		break;

    case IDIO_A_GLOBAL_VAL_REF:				r = "GLOBAL-VAL-REF";			break;
    case IDIO_A_CHECKED_GLOBAL_VAL_REF:			r = "CHECKED-GLOBAL-VAL-REF";		break;
    case IDIO_A_GLOBAL_FUNCTION_VAL_REF:		r = "GLOBAL-FUNCTION-VAL-REF";		break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF:	r = "CHECKED-GLOBAL-FUNCTION-VAL-REF";	break;
    case IDIO_A_CONSTANT_VAL_REF:			r = "CONSTANT-VAL-REF";			break;
    case IDIO_A_COMPUTED_VAL_REF:			r = "COMPUTED-VAL-REF";			break;

    case IDIO_A_GLOBAL_VAL_DEF:				r = "GLOBAL-VAL-DEF";			break;
    case IDIO_A_GLOBAL_VAL_SET:				r = "GLOBAL-VAL-SET";			break;
    case IDIO_A_COMPUTED_VAL_SET:			r = "COMPUTED-VAL-SET";			break;
    case IDIO_A_COMPUTED_VAL_DEFINE:			r = "COMPUTED-VAL-DEFINE";		break;

    case IDIO_A_PREDEFINED0:				r = "PREDEFINED0";			break;
    case IDIO_A_PREDEFINED1:				r = "PREDEFINED1";			break;
    case IDIO_A_PREDEFINED2:				r = "PREDEFINED2";			break;
    case IDIO_A_PREDEFINED3:				r = "PREDEFINED3";			break;
    case IDIO_A_PREDEFINED4:				r = "PREDEFINED4";			break;
    case IDIO_A_PREDEFINED5:				r = "PREDEFINED5";			break;
    case IDIO_A_PREDEFINED6:				r = "PREDEFINED6";			break;
    case IDIO_A_PREDEFINED7:				r = "PREDEFINED7";			break;
    case IDIO_A_PREDEFINED8:				r = "PREDEFINED8";			break;
    case IDIO_A_PREDEFINED:				r = "PREDEFINED";			break;

    case IDIO_A_LONG_GOTO:				r = "LONG-GOTO";			break;
    case IDIO_A_LONG_JUMP_FALSE:			r = "LONG-JUMP-FALSE";			break;
    case IDIO_A_LONG_JUMP_TRUE:				r = "LONG-JUMP-TRUE";			break;
    case IDIO_A_SHORT_GOTO:				r = "SHORT-GOTO";			break;
    case IDIO_A_SHORT_JUMP_FALSE:			r = "SHORT-JUMP-FALSE";			break;
    case IDIO_A_SHORT_JUMP_TRUE:			r = "SHORT-JUMP-TRUE";			break;

    case IDIO_A_PUSH_VALUE:				r = "PUSH-VALUE";			break;
    case IDIO_A_POP_VALUE:				r = "POP-VALUE";			break;
    case IDIO_A_POP_REG1:				r = "POP-REG1";				break;
    case IDIO_A_POP_REG2:				r = "POP-REG2";				break;
    case IDIO_A_POP_EXPR:				r = "POP-EXPR";				break;
    case IDIO_A_POP_FUNCTION:				r = "POP-FUNCTION";			break;
    case IDIO_A_PRESERVE_STATE:				r = "PRESERVE-STATE";			break;
    case IDIO_A_RESTORE_STATE:				r = "RESTORE-STATE";			break;
    case IDIO_A_RESTORE_ALL_STATE:			r = "RESTORE-ALL-STATE";		break;

    case IDIO_A_CREATE_CLOSURE:				r = "CREATE-CLOSURE";			break;
    case IDIO_A_FUNCTION_INVOKE:			r = "FUNCTION-INVOKE";			break;
    case IDIO_A_FUNCTION_GOTO:				r = "FUNCTION-GOTO";			break;
    case IDIO_A_RETURN:					r = "RETURN";				break;
    case IDIO_A_FINISH:					r = "FINISH";				break;
    case IDIO_A_ABORT:					r = "ABORT";				break;

    case IDIO_A_ALLOCATE_FRAME1:			r = "ALLOCATE-FRAME1";			break;
    case IDIO_A_ALLOCATE_FRAME2:			r = "ALLOCATE-FRAME2";			break;
    case IDIO_A_ALLOCATE_FRAME3:			r = "ALLOCATE-FRAME3";			break;
    case IDIO_A_ALLOCATE_FRAME4:			r = "ALLOCATE-FRAME4";			break;
    case IDIO_A_ALLOCATE_FRAME5:			r = "ALLOCATE-FRAME5";			break;
    case IDIO_A_ALLOCATE_FRAME:				r = "ALLOCATE-FRAME";			break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:			r = "ALLOCATE-DOTTED-FRAME";		break;
    case IDIO_A_REUSE_FRAME:				r = "REUSE-FRAME";			break;

    case IDIO_A_POP_FRAME0:				r = "POP-FRAME0";			break;
    case IDIO_A_POP_FRAME1:				r = "POP-FRAME1";			break;
    case IDIO_A_POP_FRAME2:				r = "POP-FRAME2";			break;
    case IDIO_A_POP_FRAME3:				r = "POP-FRAME3";			break;
    case IDIO_A_POP_FRAME:				r = "POP-FRAME";			break;

    case IDIO_A_EXTEND_FRAME:				r = "EXTEND-FRAME";			break;
    case IDIO_A_UNLINK_FRAME:				r = "UNLINK-FRAME";			break;
    case IDIO_A_PACK_FRAME:				r = "PACK-FRAME";			break;
    case IDIO_A_POP_CONS_FRAME:				r = "POP-CONS-FRAME";			break;

    case IDIO_A_ARITY1P:				r = "ARITY1P";				break;
    case IDIO_A_ARITY2P:				r = "ARITY2P";				break;
    case IDIO_A_ARITY3P:				r = "ARITY3P";				break;
    case IDIO_A_ARITY4P:				r = "ARITY4P";				break;
    case IDIO_A_ARITYEQP:				r = "ARITYEQP";				break;
    case IDIO_A_ARITYGEP:				r = "ARITYGEP";				break;

    case IDIO_A_SHORT_NUMBER:				r = "SHORT-NUMBER";			break;
    case IDIO_A_SHORT_NEG_NUMBER:			r = "SHORT-NEG-NUMBER";			break;
    case IDIO_A_CONSTANT_0:				r = "CONSTANT-0";			break;
    case IDIO_A_CONSTANT_1:				r = "CONSTANT-1";			break;
    case IDIO_A_CONSTANT_2:				r = "CONSTANT-2";			break;
    case IDIO_A_CONSTANT_3:				r = "CONSTANT-3";			break;
    case IDIO_A_CONSTANT_4:				r = "CONSTANT-4";			break;
    case IDIO_A_FIXNUM:					r = "FIXNUM";				break;
    case IDIO_A_NEG_FIXNUM:				r = "NEG-FIXNUM";			break;
    case IDIO_A_CHARACTER:				r = "CHARACTER";			break;
    case IDIO_A_NEG_CHARACTER:				r = "NEG-CHARACTER";			break;
    case IDIO_A_CONSTANT:				r = "CONSTANT";				break;
    case IDIO_A_NEG_CONSTANT:				r = "NEG-CONSTANT";			break;
    case IDIO_A_UNICODE:				r = "UNICODE";				break;

    case IDIO_A_NOP:					r = "NOP";				break;
    case IDIO_A_PRIMCALL0:				r = "PRIMCALL0";			break;
    case IDIO_A_PRIMCALL0_NEWLINE:			r = "PRIMCALL0-NEWLINE";		break;
    case IDIO_A_PRIMCALL0_READ:				r = "PRIMCALL0-READ";			break;
    case IDIO_A_PRIMCALL1:				r = "PRIMCALL1";			break;
    case IDIO_A_PRIMCALL1_HEAD:				r = "PRIMCALL1-HEAD";			break;
    case IDIO_A_PRIMCALL1_TAIL:				r = "PRIMCALL1-TAIL";			break;
    case IDIO_A_PRIMCALL1_PAIRP:			r = "PRIMCALL1-PAIRP";			break;
    case IDIO_A_PRIMCALL1_SYMBOLP:			r = "PRIMCALL1-SYMBOLP";		break;
    case IDIO_A_PRIMCALL1_DISPLAY:			r = "PRIMCALL1-DISPLAY";		break;
    case IDIO_A_PRIMCALL1_PRIMITIVEP:			r = "PRIMCALL1-PRIMITIVEP";		break;
    case IDIO_A_PRIMCALL1_NULLP:			r = "PRIMCALL1-NULLP";			break;
    case IDIO_A_PRIMCALL1_CONTINUATIONP:		r = "PRIMCALL1-CONTINUATIONP";		break;
    case IDIO_A_PRIMCALL1_EOFP:				r = "PRIMCALL1-EOFP";			break;
    case IDIO_A_PRIMCALL1_SET_CUR_MOD:			r = "PRIMCALL1-SET-CUR-MOD";		break;
    case IDIO_A_PRIMCALL2:				r = "PRIMCALL2";			break;
    case IDIO_A_PRIMCALL2_PAIR:				r = "PRIMCALL2-PAIR";			break;
    case IDIO_A_PRIMCALL2_EQP:				r = "PRIMCALL2-EQP";			break;
    case IDIO_A_PRIMCALL2_SET_HEAD:			r = "PRIMCALL2-SET-HEAD";		break;
    case IDIO_A_PRIMCALL2_SET_TAIL:			r = "PRIMCALL2-SET-TAIL";		break;
    case IDIO_A_PRIMCALL2_ADD:				r = "PRIMCALL2-ADD";			break;
    case IDIO_A_PRIMCALL2_SUBTRACT:			r = "PRIMCALL2-SUBTRACT";		break;
    case IDIO_A_PRIMCALL2_EQ:				r = "PRIMCALL2-EQ";			break;
    case IDIO_A_PRIMCALL2_LT:				r = "PRIMCALL2-LT";			break;
    case IDIO_A_PRIMCALL2_GT:				r = "PRIMCALL2-GT";			break;
    case IDIO_A_PRIMCALL2_MULTIPLY:			r = "PRIMCALL2-MULTIPLY";		break;
    case IDIO_A_PRIMCALL2_LE:				r = "PRIMCALL2-LE";			break;
    case IDIO_A_PRIMCALL2_GE:				r = "PRIMCALL2-GE";			break;
    case IDIO_A_PRIMCALL2_REMAINDER:			r = "PRIMCALL2-REMAINDER";		break;

    case IDIO_A_PRIMCALL3:				r = "PRIMCALL3";			break;
    case IDIO_A_PRIMCALL:				r = "PRIMCALL";				break;

    case IDIO_A_EXPANDER:				r = "EXPANDER";				break;
    case IDIO_A_INFIX_OPERATOR:				r = "INFIX-OPERATOR";			break;
    case IDIO_A_POSTFIX_OPERATOR:			r = "POSTFIX-OPERATOR";			break;

    case IDIO_A_PUSH_DYNAMIC:				r = "PUSH-DYNAMIC";			break;
    case IDIO_A_POP_DYNAMIC:				r = "POP-DYNAMIC";			break;
    case IDIO_A_DYNAMIC_SYM_REF:			r = "DYNAMIC-SYM-REF";			break;
    case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:		r = "DYNAMIC-FUNCTION-SYM-REF";		break;
    case IDIO_A_DYNAMIC_VAL_REF:			r = "DYNAMIC-VAL-REF";			break;
    case IDIO_A_DYNAMIC_FUNCTION_VAL_REF:		r = "DYNAMIC-FUNCTION-VAL-REF";		break;

    case IDIO_A_PUSH_ENVIRON:				r = "PUSH-ENVIRON";			break;
    case IDIO_A_POP_ENVIRON:				r = "POP-ENVIRON";			break;
    case IDIO_A_ENVIRON_SYM_REF:			r = "ENVIRON-SYM-REF";			break;
    case IDIO_A_ENVIRON_VAL_REF:			r = "ENVIRON-VAL-REF";			break;

    case IDIO_A_NON_CONT_ERR:				r = "NON-CONT-ERR";			break;
    case IDIO_A_PUSH_TRAP:				r = "PUSH-TRAP";			break;
    case IDIO_A_POP_TRAP:				r = "POP-TRAP";				break;
    case IDIO_A_RESTORE_TRAP:				r = "RESTORE-TRAP";			break;

    case IDIO_A_PUSH_ESCAPER:				r = "PUSH-ESCAPER";			break;
    case IDIO_A_POP_ESCAPER:				r = "POP-ESCAPER";			break;

    default:
	/* fprintf (stderr, "idio_vm_bytecode2string: unexpected bytecode %d\n", code); */
	r = "Unknown bytecode";
	break;
    }

    return r;
}

/*
 * Basic map -- only one list and the function must be a primitive so
 * we can call it directly.  We can't apply a closure as apply only
 * changes the PC, it doesn't actually do anything!
 *
 * The real map is defined early on in bootstrap.
 */
IDIO_DEFINE_PRIMITIVE2 ("%map1", map1, (IDIO fn, IDIO list))
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (list);

    IDIO_VERIFY_PARAM_TYPE (primitive, fn);
    IDIO_VERIFY_PARAM_TYPE (list, list);

    IDIO r = idio_S_nil;

    /* idio_debug ("map: in: %s\n", list); */

    while (idio_S_nil != list) {
	r = idio_pair (IDIO_PRIMITIVE_F (fn) (IDIO_PAIR_H (list)), r);
	list = IDIO_PAIR_T (list);
    }

    r = idio_list_reverse (r);
    /* idio_debug ("map => %s\n", r); */
    return r;
}

IDIO idio_list_map_ph (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_H (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_map_pt (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_T (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_memq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	if (idio_eqp (k, IDIO_PAIR_H (l))) {
	    return l;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2 ("memq", memq, (IDIO k, IDIO l))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_memq (k, l);
}

IDIO idio_list_assq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    /* fprintf (stderr, "assq: k=%s in l=%s\n", idio_as_string (k, 1), idio_as_string (idio_list_map_ph (l), 4)); */

    while (idio_S_nil != l) {
	IDIO p = IDIO_PAIR_H (l);

	if (idio_S_nil == p) {
	    return idio_S_false;
	}

	if (! idio_isa_pair (p)) {
	    idio_error_C ("not a pair in list", IDIO_LIST2 (p, l), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	if (idio_eqp (k, IDIO_PAIR_H (p))) {
	    return p;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2 ("assq", assq, (IDIO k, IDIO l))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_assq (k, l);
}

IDIO idio_list_set_difference (IDIO set1, IDIO set2)
{
    if (idio_isa_pair (set1)) {
	if (idio_S_false != idio_list_memq (IDIO_PAIR_H (set1), set2)) {
	    return idio_list_set_difference (IDIO_PAIR_T (set1), set2);
	} else {
	    return idio_pair (IDIO_PAIR_H (set1),
			      idio_list_set_difference (IDIO_PAIR_T (set1), set2));
	}
    } else {
	if (idio_S_nil != set1) {
	    idio_error_C ("set1", set1, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	return idio_S_nil;
    }
}

IDIO_DEFINE_PRIMITIVE2 ("value-index", value_index, (IDIO o, IDIO i))
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (i);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_PAIR:
		return idio_list_nth (o, i, idio_S_nil);
	    case IDIO_TYPE_SUBSTRING:
	    case IDIO_TYPE_STRING:
		return idio_string_ref (o, i);
	    case IDIO_TYPE_ARRAY:
		return idio_array_ref (o, i);
	    case IDIO_TYPE_HASH:
		return idio_hash_ref (o, i, idio_S_nil);
	    case IDIO_TYPE_STRUCT_INSTANCE:
		return idio_struct_instance_ref (o, i);
	    default:
		break;
	    }
	}
    default:
	break;
    }

    idio_error_C ("non-indexable object", IDIO_LIST2 (o, i), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE3 ("set-value-index!", set_value_index, (IDIO o, IDIO i, IDIO v))
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_SUBSTRING:
	    case IDIO_TYPE_STRING:
		return idio_string_set (o, i, v);
	    case IDIO_TYPE_ARRAY:
		return idio_array_set (o, i, v);
	    case IDIO_TYPE_HASH:
		return idio_hash_set (o, i, v);
	    case IDIO_TYPE_STRUCT_INSTANCE:
		return idio_struct_instance_set (o, i, v);
	    default:
		break;
	    }
	}
    default:
	break;
    }

    idio_error_C ("non-indexable object", IDIO_LIST2 (o, i), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1 ("identity", identity, (IDIO o))
{
    IDIO_ASSERT (o);

    return o;
}

IDIO idio_copy (IDIO o, int depth)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	return o;
    case IDIO_TYPE_PLACEHOLDER_MARK:
	idio_error_C ("invalid type", o, IDIO_C_FUNC_LOCATION_S ("placeholder"));

	return idio_S_notreached;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_STRING:
	    case IDIO_TYPE_SUBSTRING:
		return idio_copy_string (o);
	    case IDIO_TYPE_SYMBOL:
	    case IDIO_TYPE_KEYWORD:
		return o;
	    case IDIO_TYPE_PAIR:
		return idio_copy_pair (o, depth);
	    case IDIO_TYPE_ARRAY:
		return idio_array_copy (o, depth, 0);
	    case IDIO_TYPE_HASH:
		return idio_copy_hash (o, depth);
	    case IDIO_TYPE_BIGNUM:
		return idio_bignum_copy (o);
	    case IDIO_TYPE_BITSET:
		return idio_copy_bitset (o);

	    case IDIO_TYPE_STRUCT_INSTANCE:
		if (idio_struct_instance_isa (o, idio_path_type) ||
		    idio_struct_instance_isa (o, idio_lexobj_type)) {
		    return idio_struct_instance_copy (o);
		} else {
		    idio_error_C ("invalid struct type", o, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}

	    case IDIO_TYPE_CLOSURE:
	    case IDIO_TYPE_PRIMITIVE:
	    case IDIO_TYPE_MODULE:
	    case IDIO_TYPE_FRAME:
	    case IDIO_TYPE_HANDLE:
	    case IDIO_TYPE_STRUCT_TYPE:
	    case IDIO_TYPE_THREAD:
	    case IDIO_TYPE_CONTINUATION:
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_POINTER:
	    case IDIO_TYPE_C_TYPEDEF:
	    case IDIO_TYPE_C_STRUCT:
	    case IDIO_TYPE_C_INSTANCE:
	    case IDIO_TYPE_C_FFI:
	    case IDIO_TYPE_OPAQUE:
		idio_error_C ("invalid type", o, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    default:
		idio_error_C ("unimplemented type", o, IDIO_C_FUNC_LOCATION ());
		break;
	    }
	}
	break;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", o, (intptr_t) o & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

	break;
    }

    idio_error_C ("failed to copy", o, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}


void idio_dump (IDIO o, int detail)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    if (detail > 0) {
		fprintf (stderr, "%10p ", o);
		if (detail > 4) {
		    fprintf (stderr, "-> %10p ", o->next);
		}
		fprintf (stderr, "t=%2d/%4.4s f=%2x gcf=%2x ", o->type, idio_type2string (o), o->flags, o->gc_flags);
	    }

	    switch (o->type) {
	    case IDIO_TYPE_STRING:
		if (detail) {
		    fprintf (stderr, "len=%4zu blen=%4zu fl=%x s=", IDIO_STRING_LEN (o), IDIO_STRING_BLEN (o), IDIO_STRING_FLAGS (o));
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (detail) {
		    fprintf (stderr, "len=%4zu parent=%10p subs=", IDIO_SUBSTRING_LEN (o), IDIO_SUBSTRING_PARENT (o));
		}
		break;
	    case IDIO_TYPE_SYMBOL:
		fprintf (stderr, "sym=");
		break;
	    case IDIO_TYPE_KEYWORD:
		fprintf (stderr, "key=");
		break;
	    case IDIO_TYPE_PAIR:
		if (detail > 1) {
		    fprintf (stderr, "head=%10p tail=%10p p=", IDIO_PAIR_H (o), IDIO_PAIR_T (o));
		}
		break;
	    case IDIO_TYPE_ARRAY:
		if (detail) {
		    fprintf (stderr, "size=%td/%td \n", IDIO_ARRAY_USIZE (o), IDIO_ARRAY_ASIZE (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			    if (idio_S_nil != IDIO_ARRAY_AE (o, i) ||
				detail > 3) {
				size_t size = 0;
				char *s = idio_as_string (IDIO_ARRAY_AE (o, i), &size, 4);
				fprintf (stderr, "\t%3zu: %10p %10s\n", i, IDIO_ARRAY_AE (o, i), s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_HASH:
		if (detail) {
		    fprintf (stderr, "hsz=%zu hm=%zx hc=%zu hst=%zu\n", IDIO_HASH_SIZE (o), IDIO_HASH_MASK (o), IDIO_HASH_COUNT (o), IDIO_HASH_START (o));
		    if (IDIO_HASH_COMP_C (o) != NULL) {
			if (IDIO_HASH_COMP_C (o) == idio_eqp) {
			    fprintf (stderr, "eq=idio_S_eqp");;
			} else if (IDIO_HASH_COMP_C (o) == idio_eqvp) {
			    fprintf (stderr, "eq=idio_S_eqvp");
			} else if (IDIO_HASH_COMP_C (o) == idio_equalp) {
			    fprintf (stderr, "eq=idio_S_equalp");
			}
		    } else {
			idio_debug ("eq=%s", IDIO_HASH_COMP (o));
		    }
		    if (IDIO_HASH_HASH_C (o) != NULL) {
			if (IDIO_HASH_HASH_C (o) == idio_hash_default_hash_C) {
			    fprintf (stderr, " hf=<default>");
			}
		    } else {
			idio_debug (" hf=%s", IDIO_HASH_HASH (o));
		    }
		    fprintf (stderr, "\n");
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			    if (idio_S_nil == IDIO_HASH_HE_KEY (o, i)) {
				continue;
			    } else {
				size_t size = 0;
				char *s;
				if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) {
				    s = (char *) IDIO_HASH_HE_KEY (o, i);
				} else {
				    s = idio_as_string (IDIO_HASH_HE_KEY (o, i), &size, 4);
				}
				if (detail & 0x4) {
				    fprintf (stderr, "\t%30s : ", s);
				} else {
				    fprintf (stderr, "\t%3zu: k=%10p v=%10p n=%3zu %10s : ",
							i,
							IDIO_HASH_HE_KEY (o, i),
							IDIO_HASH_HE_VALUE (o, i),
							IDIO_HASH_HE_NEXT (o, i),
							s);
				}
				if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
				    free (s);
				}
				if (IDIO_HASH_HE_VALUE (o, i)) {
				    size = 0;
				    s = idio_as_string (IDIO_HASH_HE_VALUE (o, i), &size, 4);
				} else {
				    if (asprintf (&s, "-") == -1) {
					idio_error_alloc ("asprintf");

					/* notreached */
					return;
				    }
				    size = strlen (s);
				}
				fprintf (stderr, "%-10s\n", s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_CLOSURE:
	    case IDIO_TYPE_PRIMITIVE:
	    case IDIO_TYPE_BIGNUM:
	    case IDIO_TYPE_MODULE:
	    case IDIO_TYPE_FRAME:
	    case IDIO_TYPE_HANDLE:
	    case IDIO_TYPE_STRUCT_TYPE:
	    case IDIO_TYPE_STRUCT_INSTANCE:
	    case IDIO_TYPE_THREAD:
	    case IDIO_TYPE_CONTINUATION:
	    case IDIO_TYPE_BITSET:
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_POINTER:
	    case IDIO_TYPE_C_TYPEDEF:
	    case IDIO_TYPE_C_STRUCT:
	    case IDIO_TYPE_C_INSTANCE:
	    case IDIO_TYPE_C_FFI:
	    case IDIO_TYPE_OPAQUE:
		break;
	    default:
		idio_error_C ("unimplemented type", o, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
		break;
	    }
	}
	break;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", o, (intptr_t) o & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

	/* notreached */
	return;
	break;
    }

    if (detail &&
	!(detail & 0x4)) {
	idio_debug ("%s", o);
    }

    fprintf (stderr, "\n");
}

IDIO_DEFINE_PRIMITIVE1 ("idio-dump", idio_dump, (IDIO o))
{
    IDIO_ASSERT (o);

    idio_dump (o, 16);

    return idio_S_unspec;
}

void idio_debug_FILE (FILE *file, const char *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    /* fprintf (file, "[%d]", getpid()); */

    size_t size = 0;
    char *os = idio_as_string (o, &size, 40);
    fprintf (file, fmt, os);
    free (os);
}

void idio_debug (const char *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    idio_debug_FILE (stderr, fmt, o);
}

IDIO_DEFINE_PRIMITIVE2 ("idio-debug", idio_debug, (IDIO fmt, IDIO o))
{
    IDIO_ASSERT (fmt);
    IDIO_ASSERT (o);
    IDIO_TYPE_ASSERT (string, fmt);

    size_t size = 0;
    char *sfmt = idio_string_as_C (fmt, &size);
    size_t C_size = strlen (sfmt);
    if (C_size != size) {
	free (sfmt);

	idio_util_error_format ("idio-debug: fmt contains an ASCII NUL", fmt, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_debug (sfmt, o);

    free (sfmt);

    return idio_S_unspec;
}

#if ! defined (strnlen)
/*
 * Mac OS X - 10.5.8
 */
size_t strnlen (const char *s, size_t maxlen)
{
    size_t n = 0;
    while (*s &&
	   n < maxlen) {
	n++;
    }

    return n;
}
#endif

void idio_init_util ()
{
    idio_util_value_as_string = idio_symbols_C_intern ("%%value-as-string");
    idio_module_set_symbol_value (idio_util_value_as_string, idio_S_nil, idio_Idio_module);
    idio_print_conversion_format_sym = idio_symbols_C_intern ("idio-print-conversion-format");
    idio_print_conversion_precision_sym = idio_symbols_C_intern ("idio-print-conversion-precision");

#ifdef IDIO_EQUAL_DEBUG
    for (int i = 0; i < IDIO_TYPE_MAX; i++) {
	idio_equal_stats[i].count = 0;
	idio_equal_stats[i].duration.tv_sec = 0;
	idio_equal_stats[i].duration.tv_usec = 0;
	idio_equal_stats[i].mixed = 0;
    }
#endif
}

void idio_util_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (type_string);
    IDIO_ADD_PRIMITIVE (zerop);
    IDIO_ADD_PRIMITIVE (nullp);
    IDIO_ADD_PRIMITIVE (unsetp);
    IDIO_ADD_PRIMITIVE (undefp);
    IDIO_ADD_PRIMITIVE (unspecp);
    IDIO_ADD_PRIMITIVE (voidp);
    IDIO_ADD_PRIMITIVE (definedp);
    IDIO_ADD_PRIMITIVE (booleanp);
    IDIO_ADD_PRIMITIVE (not);
    IDIO_ADD_PRIMITIVE (eqp);
    IDIO_ADD_PRIMITIVE (eqvp);
    IDIO_ADD_PRIMITIVE (equalp);
    IDIO_ADD_PRIMITIVE (string);
    IDIO_ADD_PRIMITIVE (display_string);
    IDIO_ADD_PRIMITIVE (map1);
    IDIO_ADD_PRIMITIVE (memq);
    IDIO_ADD_PRIMITIVE (assq);
    IDIO_ADD_PRIMITIVE (value_index);
    IDIO_ADD_PRIMITIVE (set_value_index);
    IDIO_ADD_PRIMITIVE (identity);
    IDIO_ADD_PRIMITIVE (idio_dump);
    IDIO_ADD_PRIMITIVE (idio_debug);
}

void idio_final_util ()
{
#ifdef IDIO_EQUAL_DEBUG
    for (int i = 0; i < IDIO_TYPE_MAX; i++) {
	if (idio_equal_stats[i].count) {
	    fprintf (stderr, "equal %3d %15s %9lu %5ld.%06ld %5lu\n", i, idio_type_enum2string (i), idio_equal_stats[i].count, idio_equal_stats[i].duration.tv_sec, idio_equal_stats[i].duration.tv_usec, idio_equal_stats[i].mixed);
	}
    }
#endif
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
