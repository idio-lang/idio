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
 * util.c
 *
 */

#include "idio.h"

static IDIO idio_util_value_as_string;
IDIO idio_print_conversion_format_sym = idio_S_nil;
IDIO idio_print_conversion_precision_sym = idio_S_nil;
static IDIO idio_features;

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
		/* Code coverage: not user-visible */
		return IDIO_TYPE_CONSTANT_TOKEN;
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		/* Code coverage: evaluate.idio */
		return IDIO_TYPE_CONSTANT_I_CODE;
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		/* Code coverage: deprecated */
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
	/* inconceivable! */
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
    return idio_type_enum2string (idio_type (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("type->string", type_string, (IDIO o), "o", "\
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

IDIO_DEFINE_PRIMITIVE1_DS ("zero?", zerop, (IDIO o), "o", "\
test if `o` is the numeric value zero		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is zero, #f otherwise	\n\
")
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

IDIO_DEFINE_PRIMITIVE1_DS ("null?", nullp, (IDIO o), "o", "\
test if `o` is #n				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is #n, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_nil == o) {
	r = idio_S_true;
    }

    return r;
}

/*
 * The result of the successful application of nothing...
 */
IDIO_DEFINE_PRIMITIVE0_DS ("void", void, (void), "", "\
:return: #<void>	\n\
")
{
    return idio_S_void;
}

IDIO_DEFINE_PRIMITIVE1_DS ("void?", voidp, (IDIO o), "o", "\
test if `o` is void (#void)			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is void, #f otherwise	\n\
")
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
IDIO_DEFINE_PRIMITIVE1_DS ("%defined?", definedp, (IDIO s), "s", "\
test if symbol `s` is defined in this environment	\n\
						\n\
:param s: symbol to test			\n\
:type s: symbol					\n\
						\n\
:return: #t if `s` is defined in this environment, #f otherwise	\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: util-errors/defined-bad-type.idio
     *
     * %defined? #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, s);

    IDIO r = idio_S_false;

    IDIO sk = idio_module_env_symbol_recurse (s);

    if (idio_S_false != sk) {
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

IDIO_DEFINE_PRIMITIVE1_DS ("boolean?", booleanp, (IDIO o), "o", "\
test if `o` is a boolean			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a boolean			\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_boolean (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("not", not, (IDIO e), "e", "\
return the boolean inverse of `e`		\n\
						\n\
:param e: expression to invert			\n\
:type e: any					\n\
						\n\
:return: #t if `s` is #f, #f otherwise		\n\
")
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

IDIO_DEFINE_PRIMITIVE2_DS ("eq?", eqp, (IDIO o1, IDIO o2), "o1 o2", "\
test if `o1` and `o2` are eq?			\n\
						\n\
:param o1: object to test			\n\
:type o1: any					\n\
:param o2: object to test			\n\
:type o2: any					\n\
						\n\
:return: #t if `o1` is eq? to `o2`, #f otherwise\n\
")
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_eqp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("eqv?", eqvp, (IDIO o1, IDIO o2), "o1 o2", "\
test if `o1` and `o2` are eqv?			\n\
						\n\
:param o1: object to test			\n\
:type o1: any					\n\
:param o2: object to test			\n\
:type o2: any					\n\
						\n\
:return: #t if `o1` is eqv? to `o2`, #f otherwise\n\
")
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

IDIO_DEFINE_PRIMITIVE2_DS ("equal?", equalp, (IDIO o1, IDIO o2), "o1 o2", "\
test if `o1` and `o2` are equal?			\n\
						\n\
:param o1: object to test			\n\
:type o1: any					\n\
:param o2: object to test			\n\
:type o2: any					\n\
						\n\
:return: #t if `o1` is equal? to `o2`, #f otherwise\n\
")
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
				      IDIO_LIST3 (idio_module_symbol_value (idio_S_num_eq,
									    idio_primitives_module,
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
					      IDIO_LIST3 (idio_module_symbol_value (idio_S_num_eq,
										    idio_primitives_module,
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
	    if (IDIO_TYPE_NONE == o1->type ||
		IDIO_GC_FLAG_FREE_SET (o1) ||
		IDIO_GC_FLAG_FREE_SET (o2)) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
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
		    /*
		     * Code coverage:
		     *
		     * eq? (substring ...) (substring ...)
		     */
		    return (o1 == o2);
		}

		if (IDIO_SUBSTRING_LEN (o1) != IDIO_SUBSTRING_LEN (o2)) {
		    /*
		     * Code coverage:
		     *
		     * equal? (substring "hello" 0 1) (substring "hello" 0 2)
		     */
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

		if (IDIO_HASH_COUNT (o1) != IDIO_HASH_COUNT (o2)) {
		    return 0;
		}

		{
		    /*
		     * We could have two hashes of different sizes
		     * with the same key/value tuples -- but the
		     * orderings will be different
		     *
		     * So, if the keys are the same then if each value
		     * is the same against each key...
		     *
		     * NB We need to do a little list-of-keys
		     * protection as we're calling idio_equal() which
		     * can invoke the VM.
		     */

		    IDIO o1k = idio_hash_keys_to_list (o1);
		    idio_gc_protect (o1k);
		    IDIO kl = o1k;
		    while (idio_S_nil != kl) {
			if (! idio_hash_exists_key (o2, IDIO_PAIR_H (kl))) {
			    /*
			     * Code coverage:
			     *
			     * key not present in o2 -- this is
			     * covered by rote
			     */
			    idio_gc_expose (o1k);
			    return 0;
			}
			if (! idio_equal (idio_hash_ref (o1, IDIO_PAIR_H (kl)),
					  idio_hash_ref (o2, IDIO_PAIR_H (kl)),
					  eqp)) {
			    /*
			     * Code coverage:
			     *
			     * values must differ for the same key
			     */
			    idio_gc_expose (o1k);
			    return 0;
			}
			kl = IDIO_PAIR_T (kl);
		    }
		    idio_gc_expose (o1k);

		    IDIO o2k = idio_hash_keys_to_list (o2);
		    idio_gc_protect (o2k);
		    kl = o2k;
		    while (idio_S_nil != kl) {
			if (! idio_hash_exists_key (o1, IDIO_PAIR_H (kl))) {
			    /*
			     * Code coverage:
			     *
			     * Hmm, not sure this is possible as we've
			     * asserted 1) we have the same number of
			     * entries in each hash table and 2) that
			     * all of the keys in o1 exist in o2.
			     *
			     * Coding error?
			     */
			    idio_gc_expose (o2k);
			    idio_error_warning_message ("idio_equal: key mismatch");
			    idio_dump (o1, 1);
			    idio_dump (o2, 1);
			    return 0;
			}
			if (! idio_equal (idio_hash_ref (o1, IDIO_PAIR_H (kl)),
					  idio_hash_ref (o2, IDIO_PAIR_H (kl)),
					  eqp)) {
			    /*
			     * Code coverage:
			     *
			     * Hmm, not sure this is possible as we've
			     * asserted 1) we have the same number of
			     * entries in each hash table, 2) that all
			     * of the keys in o1 exist in o2 and 3)
			     * that all the values of the keys were
			     * equal.
			     *
			     * Coding error?
			     */
			    idio_gc_expose (o2k);
			    idio_error_warning_message ("idio_equal: value mismatch");
			    idio_dump (o1, 2);
			    idio_dump (o2, 2);
			    return 0;
			}
			kl = IDIO_PAIR_T (kl);
		    }
		    idio_gc_expose (o2k);
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
		/*  Code coverage: not user-visible */
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

		    /*
		     * Code coverage:
		     *
		     * We can have two struct types with the same name
		     * if the one is referred to and then the name is
		     * redefined.
		     *
		     * st1 := make-struct-type 'st1 #n '(x y)
		     * st2 := st1
		     * st1 := make-struct-type 'st1 #n '(x y)
		     */
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
		/* Code coverage: semi-non-user-visible */
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
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_C ("IDIO_TYPE_POINTER_MARK: o1->type unexpected", o1, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return 0;
	    }
	}
	break;
    default:
	/* inconceivable! */
	idio_error_C ("o1->type unexpected", o1, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    return 1;
}

/*
  Scheme-ish write -- internal representation (where appropriate)
  suitable for (read).  Primarily:

  UNICODE #\a:		#\a
  UNICODE #\ħ:		#U+0127		; if fails isgraph()
  STRING "foo":		"foo"

  A significant annoyance is that this function recurses which means
  that any idio-print-conversion-format which was valid for the
  initial call is now invalid for the recursed varaints.

  We can escape that with a first tag.
 */
#define IDIO_FIXNUM_CONVERSION_FORMAT_X		0x58
#define IDIO_FIXNUM_CONVERSION_FORMAT_b		0x62
#define IDIO_FIXNUM_CONVERSION_FORMAT_d		0x64
#define IDIO_FIXNUM_CONVERSION_FORMAT_o		0x6F
#define IDIO_FIXNUM_CONVERSION_FORMAT_x		0x78

#define IDIO_FIXNUM_CONVERSION_FORMAT_s		0x73

char *idio_as_string (IDIO o, size_t *sizep, int depth, IDIO seen, int first)
{
    char *r = NULL;
    size_t i;

    IDIO_C_ASSERT (depth >= -10000);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	{
	    idio_unicode_t format = IDIO_FIXNUM_CONVERSION_FORMAT_d;
	    if (first &&
		idio_S_nil != idio_print_conversion_format_sym) {
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
			case IDIO_FIXNUM_CONVERSION_FORMAT_s:
			    /*
			     * A generic: printf "%s" e
			     */
			    format = IDIO_FIXNUM_CONVERSION_FORMAT_d;
			    break;
			default:
			    /*
			     * Code coverage:
			     *
			     * format "%4q" 10		; "  10"
			     */
#ifdef IDIO_DEBUG
			    fprintf (stderr, "fixnum-as-string: unexpected conversion format: '%c' (%#x).  Using 'd' for %" PRIdPTR " @%d.\n", (int) f, (int) f, IDIO_FIXNUM_VAL (o), depth);
#endif
			    format = IDIO_FIXNUM_CONVERSION_FORMAT_d;
			    break;
			}
		    } else {
			/*
			 * Test Case: ??
			 *
			 * %format should have set
			 * idio-print-conversion-format to the results
			 * of a string-ref of the format string.
			 * string-ref returns a unicode type.
			 *
			 * That leaves someone forcing
			 * idio-print-conversion-format which is very
			 * hard to test.
			 *
			 * Coding error.
			 */
			size_t s = 0;
			fprintf (stderr, "ipcf isa %s %s\n", idio_type2string (ipcf), idio_as_string (ipcf, &s, 1, seen, 0));

			idio_error_param_value ("idio-print-conversion-format", "should be unicode", IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }
		}
	    }

	    switch (format) {
	    case IDIO_FIXNUM_CONVERSION_FORMAT_X:
		idio_asprintf (&r, "%" PRIXPTR, (uintptr_t) IDIO_FIXNUM_VAL (o));
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
		    idio_asprintf (&r, "%s", bits + seen_bit);
		}
		break;
	    case IDIO_FIXNUM_CONVERSION_FORMAT_d:
		idio_asprintf (&r, "%" PRIdPTR, IDIO_FIXNUM_VAL (o));
		break;
	    case IDIO_FIXNUM_CONVERSION_FORMAT_o:
		idio_asprintf (&r, "%" PRIoPTR, (uintptr_t) IDIO_FIXNUM_VAL (o));
		break;
	    case IDIO_FIXNUM_CONVERSION_FORMAT_x:
		idio_asprintf (&r, "%" PRIxPTR, (uintptr_t) IDIO_FIXNUM_VAL (o));
		break;
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.  We should have limited the available
		 * options above.
		 */
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
		    /*
		     * NB IDIO_CONSTANT_TOKEN_VAL() is as good as any,
		     * they all shift the same number of bits
		     */
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
		    case IDIO_STACK_MARKER_TRAP:			t = "#<MARK trap>";			break;
		    case IDIO_STACK_MARKER_PRESERVE_CONTINUATION:	t = "#<MARK preserve-continuation>";	break;
		    case IDIO_STACK_MARKER_RETURN:			t = "#<MARK return>";			break;
		    case IDIO_STACK_MARKER_DYNAMIC:			t = "#<MARK dynamic>";			break;
		    case IDIO_STACK_MARKER_ENVIRON:			t = "#<MARK environ>";			break;

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
			    /*
			     * Test Case: ??
			     *
			     * Coding error.  A panic seem a bit
			     * extreme but obviously an internal error
			     * condition *has* returned and the next C
			     * source statement is (should be!)
			     * "return idio_S_notreached".
			     *
			     * So we're in trouble.
			     */
			    idio_vm_panic (idio_thread_current_thread (), "idio_S_notreached has appeared in userspace!");

			    /* notreached :) */
			    return NULL;
			    break;
			}

		    default:
			/*
			 * Test Case: ??
			 *
			 * Coding error.  There should be a case
			 * clause above.
			 */
			idio_asprintf (&r, "#<type/constant/idio?? %10p>", o);
			*sizep = strlen (r);
			break;
		    }

		    if (NULL == t) {
			/*
			 * Test Case: ??
			 *
			 * Coding error.  There should be a case
			 * clause above.
			 */
			idio_asprintf (&r, "#<CONST? %10p>", o);
		    } else {
			idio_asprintf (&r, "%s", t);
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
			/*
			 * Test Case: ??
			 *
			 * Coding error.  There should be a case
			 * clause above.
			 */
			sprintf (m, "#<type/constant/token?? %10p>", o);
			t = m;
			break;
		    }

		    if (NULL == t) {
			/*
			 * Test Case: ??
			 *
			 * Coding error.  There should be a case
			 * clause above.
			 */
			idio_asprintf (&r, "#<TOKEN=%" PRIdPTR ">", v);
		    } else {
			idio_asprintf (&r, "%s", t);
		    }
		    *sizep = strlen (r);
		}
		break;

	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		{
		    intptr_t v = IDIO_CONSTANT_I_CODE_VAL (o);
		    char m[BUFSIZ];

		    switch (v) {

		    case IDIO_I_CODE_SHALLOW_ARGUMENT_REF:		t = "I-SHALLOW-ARGUMENT-REF";		break;
		    case IDIO_I_CODE_DEEP_ARGUMENT_REF:			t = "I-DEEP-ARGUMENT-REF";		break;

		    case IDIO_I_CODE_SHALLOW_ARGUMENT_SET:		t = "I-SHALLOW-ARGUMENT-SET";		break;
		    case IDIO_I_CODE_DEEP_ARGUMENT_SET:			t = "I-DEEP-ARGUMENT-SET";		break;

		    case IDIO_I_CODE_GLOBAL_SYM_REF:			t = "I-GLOBAL-SYM-REF";			break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_SYM_REF:		t = "I-CHECKED-GLOBAL-SYM-REF";		break;
		    case IDIO_I_CODE_GLOBAL_FUNCTION_SYM_REF:		t = "I-GLOBAL-FUNCTION-SYM-REF";	break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_SYM_REF:	t = "I-CHECKED-GLOBAL-FUNCTION-SYM-REF";break;
		    case IDIO_I_CODE_CONSTANT_SYM_REF:			t = "I-CONSTANT";			break;
		    case IDIO_I_CODE_COMPUTED_SYM_REF:			t = "I-COMPUTED-SYM-REF";		break;

		    case IDIO_I_CODE_GLOBAL_SYM_DEF:			t = "I-GLOBAL-SYM-DEF";			break;
		    case IDIO_I_CODE_GLOBAL_SYM_SET:			t = "I-GLOBAL-SYM-SET";			break;
		    case IDIO_I_CODE_COMPUTED_SYM_SET:			t = "I-COMPUTED-SYM-SET";		break;
		    case IDIO_I_CODE_COMPUTED_SYM_DEF:			t = "I-COMPUTED-SYM-DEF";		break;

		    case IDIO_I_CODE_GLOBAL_VAL_REF:			t = "I-GLOBAL-VAL-REF";			break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_VAL_REF:		t = "I-CHECKED-GLOBAL-VAL-REF";		break;
		    case IDIO_I_CODE_GLOBAL_FUNCTION_VAL_REF:		t = "I-GLOBAL-FUNCTION-VAL-REF";	break;
		    case IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_VAL_REF:	t = "I-CHECKED-GLOBAL-FUNCTION-VAL-REF";break;
		    case IDIO_I_CODE_CONSTANT_VAL_REF:			t = "I-CONSTANT";			break;
		    case IDIO_I_CODE_COMPUTED_VAL_REF:			t = "I-COMPUTED-VAL-REF";		break;

		    case IDIO_I_CODE_GLOBAL_VAL_DEF:			t = "I-GLOBAL-VAL-DEF";			break;
		    case IDIO_I_CODE_GLOBAL_VAL_SET:			t = "I-GLOBAL-VAL-SET";			break;
		    case IDIO_I_CODE_COMPUTED_VAL_SET:			t = "I-COMPUTED-VAL-SET";		break;
		    case IDIO_I_CODE_COMPUTED_VAL_DEF:			t = "I-COMPUTED-VAL-DEF";		break;

		    case IDIO_I_CODE_PREDEFINED:			t = "I-PREDEFINED";			break;
		    case IDIO_I_CODE_ALTERNATIVE:			t = "I-ALTERNATIVE";			break;
		    case IDIO_I_CODE_SEQUENCE:				t = "I-SEQUENCE";			break;
		    case IDIO_I_CODE_TR_FIX_LET:			t = "I-TR-FIX-LET";			break;
		    case IDIO_I_CODE_FIX_LET:				t = "I-FIX-LET";			break;

		    case IDIO_I_CODE_PRIMCALL0:				t = "I-PRIMCALL0";			break;
		    case IDIO_I_CODE_PRIMCALL1:				t = "I-PRIMCALL1";			break;
		    case IDIO_I_CODE_PRIMCALL2:				t = "I-PRIMCALL2";			break;
		    case IDIO_I_CODE_PRIMCALL3:				t = "I-PRIMCALL3";			break;
		    case IDIO_I_CODE_TR_REGULAR_CALL:			t = "I-TR-REGULAR-CALL";		break;
		    case IDIO_I_CODE_REGULAR_CALL:			t = "I-REGULAR-CALL";			break;

		    case IDIO_I_CODE_FIX_CLOSURE:			t = "I-FIX-CLOSURE";			break;
		    case IDIO_I_CODE_NARY_CLOSURE:			t = "I-NARY-CLOSURE";			break;

		    case IDIO_I_CODE_STORE_ARGUMENT:			t = "I-STORE-ARGUMENT";			break;
		    case IDIO_I_CODE_LIST_ARGUMENT:			t = "I-LIST-ARGUMENT";			break;

		    case IDIO_I_CODE_ALLOCATE_FRAME:			t = "I-ALLOCATE-FRAME";			break;
		    case IDIO_I_CODE_ALLOCATE_DOTTED_FRAME:		t = "I-ALLOCATE-DOTTED-FRAME";		break;
		    case IDIO_I_CODE_REUSE_FRAME:			t = "I-REUSE-FRAME";			break;

		    case IDIO_I_CODE_DYNAMIC_SYM_REF:			t = "I-DYNAMIC-SYM-REF";		break;
		    case IDIO_I_CODE_DYNAMIC_FUNCTION_SYM_REF:		t = "I-DYNAMIC-FUNCTION-SYM-REF";	break;
		    case IDIO_I_CODE_PUSH_DYNAMIC:			t = "I-PUSH-DYNAMIC";			break;
		    case IDIO_I_CODE_POP_DYNAMIC:			t = "I-POP-DYNAMIC";			break;

		    case IDIO_I_CODE_ENVIRON_SYM_REF:			t = "I-ENVIRON-SYM-REF";		break;
		    case IDIO_I_CODE_PUSH_ENVIRON:			t = "I-PUSH-ENVIRON";			break;
		    case IDIO_I_CODE_POP_ENVIRON:			t = "I-POP-ENVIRON";			break;

		    case IDIO_I_CODE_PUSH_TRAP:				t = "I-PUSH-TRAP";			break;
		    case IDIO_I_CODE_POP_TRAP:				t = "I-POP-TRAP";			break;

		    case IDIO_I_CODE_AND:				t = "I-AND";				break;
		    case IDIO_I_CODE_OR:				t = "I-OR";				break;
		    case IDIO_I_CODE_BEGIN:				t = "I-BEGIN";				break;

		    case IDIO_I_CODE_EXPANDER:				t = "I-EXPANDER";			break;
		    case IDIO_I_CODE_INFIX_OPERATOR:			t = "I-INFIX-OPERATOR";			break;
		    case IDIO_I_CODE_POSTFIX_OPERATOR:			t = "I-POSTFIX-OPERATOR";		break;

		    case IDIO_I_CODE_RETURN:				t = "I-RETURN";				break;
		    case IDIO_I_CODE_FINISH:				t = "I-FINISH";				break;
		    case IDIO_I_CODE_ABORT:				t = "I-ABORT";				break;
		    case IDIO_I_CODE_NOP:				t = "I-NOP";				break;

		    default:
			/*
			 * Test Case: ??
			 *
			 * Coding error.  There should be a case
			 * clause above.
			 */
			sprintf (m, "#<type/constant/vm_code?? o=%10p v=%tx>", o, v);
			t = m;
			break;
		    }

		    if (NULL == t) {
			/*
			 * Test Case: ??
			 *
			 * Coding error.  There should be a case
			 * clause above.
			 */
			idio_asprintf (&r, "#<I-CODE? %10p>", o);
		    } else {
			idio_asprintf (&r, "%s", t);
		    }
		    *sizep = strlen (r);
		}
		break;

	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		{
		    /*
		     * XXX There is no character reader format -- as
		     * the type is deprecated.
		     *
		     * Printing a character will result in a Unicode
		     * form.
		     */
		    intptr_t c = IDIO_CHARACTER_VAL (o);
		    if (c <= 0x7f &&
			isgraph (c)) {
			idio_asprintf (&r, "#\\%c", (char) c);
		    } else {
			idio_asprintf (&r, "#U+%04X", c);
		    }
		    *sizep = strlen (r);
		    break;
		}
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		{
		    idio_unicode_t u = IDIO_UNICODE_VAL (o);
		    if (u <= 0x7f &&
			isgraph (u)) {
			idio_asprintf (&r, "#\\%c", u);
		    } else {
			idio_asprintf (&r, "#U+%04X", u);
		    }
		    *sizep = strlen (r);
		    break;
		}
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.  There should be a case clause above.
		 */
		idio_asprintf (&r, "#<type/constant?? %10p>", o);
		*sizep = strlen (r);
		break;
	    }
	}
	break;
    case IDIO_TYPE_PLACEHOLDER_MARK:
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_asprintf (&r, "#<type/placecholder?? %10p>", o);
	*sizep = strlen (r);
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    idio_type_e type = idio_type (o);

	    if (depth <= 0) {
		idio_asprintf (&r, "..");
		*sizep = strlen (r);
		return r;
	    }

	    if (idio_S_false != idio_list_memq (o, seen)) {
		idio_asprintf (&r, "#<^{%s@%p}>", idio_type2string (o), o);
		*sizep = strlen (r);
		return r;
	    }

	    switch (type) {
	    case IDIO_TYPE_NONE:
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		fprintf (stderr, "TYPE_NONE in flight :(\n");
		idio_asprintf (&r, "#<NONE!! -none- %10p>", o);
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_STRING:
		r = idio_utf8_string (o, sizep, IDIO_UTF8_STRING_ESCAPES, IDIO_UTF8_STRING_QUOTED);
		break;
	    case IDIO_TYPE_SUBSTRING:
		r = idio_utf8_string (o, sizep, IDIO_UTF8_STRING_ESCAPES, IDIO_UTF8_STRING_QUOTED);
		break;
	    case IDIO_TYPE_SYMBOL:
		idio_asprintf (&r, "%s", IDIO_SYMBOL_S (o));
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_KEYWORD:
		idio_asprintf (&r, ":%s", IDIO_KEYWORD_S (o));
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
		    seen = idio_pair (o, seen);
		    if (idio_isa_symbol (IDIO_PAIR_H (o))) {
			int special = 0;
			char *trail = NULL;

			if (idio_S_quote == IDIO_PAIR_H (o)) {
			    special = 1;
			    idio_asprintf (&r, "'");
			} else if (idio_S_unquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    idio_asprintf (&r, "$");
			} else if (idio_S_unquotesplicing == IDIO_PAIR_H (o)) {
			    special = 1;
			    idio_asprintf (&r, "$@");
			} else if (idio_S_quasiquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    trail = " }";
			    idio_asprintf (&r, "#T{ ");
			}
			if (NULL != r) {
			    *sizep = strlen (r);
			}

			if (special) {
			    if (idio_isa_pair (IDIO_PAIR_T (o))) {
				size_t hs_size = 0;
				char *hs = idio_as_string (idio_list_head (IDIO_PAIR_T (o)), &hs_size, depth - 1, seen, 0);
				IDIO_STRCAT_FREE (r, sizep, hs, hs_size);
			    } else {
				/*
				 * Code coverage:
				 *
				 * Probably shouldn't be called.  It
				 * would require an improper list in a
				 * template.  Coding error?
				 */
				size_t ts_size = 0;
				char *ts = idio_as_string (IDIO_PAIR_T (o), &ts_size, depth - 1, seen, 0);
				IDIO_STRCAT_FREE (r, sizep, ts, ts_size);
			    }

			    if (NULL != trail) {
				IDIO_STRCAT (r, sizep, trail);
			    }
			    break;
			}
		    }

		    idio_asprintf (&r, "(");
		    *sizep = strlen (r);

		    while (1) {
			size_t hs_size = 0;
			char *hs = idio_as_string (IDIO_PAIR_H (o), &hs_size, depth - 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, hs, hs_size);

			o = IDIO_PAIR_T (o);
			if (idio_type (o) != IDIO_TYPE_PAIR) {
			    if (idio_S_nil != o) {
				char *ps;
				idio_asprintf (&ps, " %c ", IDIO_PAIR_SEPARATOR);
				/* assumming IDIO_PAIR_SEPARATOR is 1 byte */
				size_t ps_size = strlen (ps);
				IDIO_STRCAT_FREE (r, sizep, ps, ps_size);

				size_t t_size = 0;
				char *t = idio_as_string (o, &t_size, depth - 1, seen, 0);
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
		/*
		 * XXX This 40 element break should be revisited.  I
		 * guess I'm less likely to be printing huge internal
		 * arrays as the code matures.
		 */
		seen = idio_pair (o, seen);
		idio_asprintf (&r, "#[ ");
		*sizep = strlen (r);
		if (depth > 0) {
		    if (IDIO_ARRAY_USIZE (o) <= 40) {
			for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			    size_t t_size = 0;
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), &t_size, depth - 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);
			    IDIO_STRCAT (r, sizep, " ");
			}
		    } else {
			for (i = 0; i < 20; i++) {
			    size_t t_size = 0;
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), &t_size, depth - 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);
			    IDIO_STRCAT (r, sizep, " ");
			}
			char *aei;
			idio_asprintf (&aei, "..[%zd] ", IDIO_ARRAY_USIZE (o) - 20);
			size_t aei_size = strlen (aei);
			IDIO_STRCAT_FREE (r, sizep, aei, aei_size);
			for (i = IDIO_ARRAY_USIZE (o) - 20; i < IDIO_ARRAY_USIZE (o); i++) {
			    size_t t_size = 0;
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), &t_size, depth - 1, seen, 0);
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
		break;
	    case IDIO_TYPE_HASH:
		seen = idio_pair (o, seen);
		idio_asprintf (&r, "#{ ");
		*sizep = strlen (r);
		if (depth > 0) {
		    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			idio_hash_entry_t *he = IDIO_HASH_HA (o, i);
			for (; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
			    if (idio_S_nil != IDIO_HASH_HE_KEY (he)) {
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
				    /*
				     * Code coverage:
				     *
				     * No user-facing string keys
				     * tables.
				     */
				    idio_asprintf (&t, "%s", (char *) IDIO_HASH_HE_KEY (he));
				    t = (char *) IDIO_HASH_HE_KEY (he);
				    t_size = strlen (t);
				} else {
				    t = idio_as_string (IDIO_HASH_HE_KEY (he), &t_size, depth - 1, seen, 0);
				}
				IDIO_STRCAT_FREE (r, sizep, t, t_size);

				char *hes;
				idio_asprintf (&hes, " %c ", IDIO_PAIR_SEPARATOR);
				size_t hes_size = strlen (hes);
				IDIO_STRCAT_FREE (r, sizep, hes, hes_size);

				if (IDIO_HASH_HE_VALUE (he)) {
				    t_size = 0;
				    t = idio_as_string (IDIO_HASH_HE_VALUE (he), &t_size, depth - 1, seen, 0);
				    IDIO_STRCAT_FREE (r, sizep, t, t_size);
				} else {
				    /*
				     * Code coverage:
				     *
				     * Probably shouldn't happen.  It
				     * requires we have a NULL for
				     * IDIO_HASH_KEY_VALUE().
				     */
				    IDIO_STRCAT (r, sizep, "-");
				}
				IDIO_STRCAT (r, sizep, ")");
			    }
			}
		    }
		} else {
		    /*
		     * Code coverage:
		     *
		     * Complicated structures are contracted.
		     */
		    IDIO_STRCAT (r, sizep, "..");
		}
		IDIO_STRCAT (r, sizep, "}");
		break;
	    case IDIO_TYPE_CLOSURE:
		{
		    seen = idio_pair (o, seen);
		    idio_asprintf (&r, "#<CLOS ");
		    *sizep = strlen (r);

		    IDIO name = idio_ref_property (o, idio_KW_name, IDIO_LIST1 (idio_S_nil));
		    if (idio_S_nil != name) {
			char *name_C;
			idio_asprintf (&name_C, "%s ", IDIO_SYMBOL_S (name));
			size_t name_size = strlen (name_C);
			IDIO_STRCAT_FREE (r, sizep, name_C, name_size);
		    } else {
			IDIO_STRCAT (r, sizep, "- ");
		    }

		    char *t;
		    idio_asprintf (&t, "@%zd/%p/", IDIO_CLOSURE_CODE_PC (o), IDIO_CLOSURE_FRAME (o));
		    size_t t_size = strlen (t);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    size_t mn_size = 0;
		    char *mn = idio_as_string (IDIO_MODULE_NAME (IDIO_CLOSURE_ENV (o)), &mn_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, mn, mn_size);

		    IDIO_STRCAT (r, sizep, ">");
		    break;
		}
	    case IDIO_TYPE_PRIMITIVE:
		idio_asprintf (&r, "#<PRIM %s>", IDIO_PRIMITIVE_NAME (o));
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_BIGNUM:
		{
		    r = idio_bignum_as_string (o, sizep);
		    break;
		}
	    case IDIO_TYPE_MODULE:
		{
		    idio_asprintf (&r, "#<MOD ");
		    *sizep = strlen (r);

		    if (idio_S_nil == IDIO_MODULE_NAME (o)) {
			/*
			 * Code coverage:
			 *
			 * Coding error?
			 */
			IDIO_STRCAT (r, sizep, "(nil)");
		    } else {
			size_t mn_size = 0;
			char *mn = idio_as_string (IDIO_MODULE_NAME (o), &mn_size, depth - 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, mn, mn_size);
		    }
		    if (0 && depth > 0) {
			IDIO_STRCAT (r, sizep, " exports=");
			if (idio_S_nil == IDIO_MODULE_EXPORTS (o)) {
			    IDIO_STRCAT (r, sizep, "(nil)");
			} else {
			    size_t e_size = 0;
			    char *es = idio_as_string (IDIO_MODULE_EXPORTS (o), &e_size, depth - 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, es, e_size);
			}
			IDIO_STRCAT (r, sizep, " imports=");
			if (idio_S_nil == IDIO_MODULE_IMPORTS (o)) {
			    IDIO_STRCAT (r, sizep, "(nil)");
			} else {
			    size_t i_size = 0;
			    char *is = idio_as_string (IDIO_MODULE_IMPORTS (o), &i_size, 0, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, is, i_size);
			}
			IDIO_STRCAT (r, sizep, " symbols=");
			if (idio_S_nil == IDIO_MODULE_SYMBOLS (o)) {
			    IDIO_STRCAT (r, sizep, "(nil)");
			} else {
			    size_t s_size = 0;
			    char *ss = idio_as_string (IDIO_MODULE_SYMBOLS (o), &s_size, depth - 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, ss, s_size);
			}
		    }
		    IDIO_STRCAT (r, sizep, ">");
		    break;
		}
	    case IDIO_TYPE_FRAME:
		{
		    /*
		     * Code coverage:
		     *
		     * Not user-visible.
		     */
		    seen = idio_pair (o, seen);
		    idio_asprintf (&r, "#<FRAME %p n=%d/%d [ ", o, IDIO_FRAME_NPARAMS (o), IDIO_FRAME_NALLOC (o));
		    *sizep = strlen (r);

		    for (i = 0; i < IDIO_FRAME_NALLOC (o); i++) {
			size_t t_size = 0;
			char *t = idio_as_string (IDIO_FRAME_ARGS (o, i), &t_size, depth - 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, t, t_size);
			IDIO_STRCAT (r, sizep, " ");
		    }

		    if (idio_S_nil != IDIO_FRAME_NAMES (o)) {
			size_t n_size = 0;
			char *n = idio_as_string (IDIO_FRAME_NAMES (o), &n_size, depth - 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, n, n_size);
		    }

		    IDIO_STRCAT (r, sizep, "]>");
		    break;
		}
	    case IDIO_TYPE_HANDLE:
		{
		    idio_asprintf (&r, "#<H ");
		    *sizep = strlen (r);

		    IDIO_FLAGS_T h_flags = IDIO_HANDLE_FLAGS (o);
		    if (h_flags & IDIO_HANDLE_FLAG_CLOSED) {
			IDIO_STRCAT (r, sizep, "c");
		    } else {
			IDIO_STRCAT (r, sizep, "o");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_STRING) {
			IDIO_STRCAT (r, sizep, "S"); /* s? is for S_ISSOCK */
		    } else if (h_flags & IDIO_HANDLE_FLAG_FILE) {
			IDIO_STRCAT (r, sizep, "f"); /* cf. f? */
		    } else if (h_flags & IDIO_HANDLE_FLAG_PIPE) {
			IDIO_STRCAT (r, sizep, "p"); /* cf. p? */
		    }

		    if (h_flags & IDIO_HANDLE_FLAG_READ) {
			IDIO_STRCAT (r, sizep, "r");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_WRITE) {
			IDIO_STRCAT (r, sizep, "w");
		    }
		    if (h_flags & IDIO_HANDLE_FLAG_FILE ||
			h_flags & IDIO_HANDLE_FLAG_PIPE) {

			IDIO_FLAGS_T s_flags = IDIO_FILE_HANDLE_FLAGS (o);
			if (s_flags & IDIO_FILE_HANDLE_FLAG_CLOEXEC) {
			    IDIO_STRCAT (r, sizep, "e"); /* same as mode string */
			} else {
			    IDIO_STRCAT (r, sizep, "!");
			}
			if (s_flags & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
			    IDIO_STRCAT (r, sizep, "i");
			}
			if (s_flags & IDIO_FILE_HANDLE_FLAG_STDIO) {
			    IDIO_STRCAT (r, sizep, "F"); /* F for FILE* */
			}
			if (s_flags & IDIO_FILE_HANDLE_FLAG_EOF) {
			    IDIO_STRCAT (r, sizep, "E");
			}

			char *fds;
			idio_asprintf (&fds, "%4d", IDIO_FILE_HANDLE_FD (o));
			size_t fds_size = strlen (fds);
			IDIO_STRCAT_FREE (r, sizep, fds, fds_size);
		    }

		    /*
		     * XXX can a handle name contain a NUL?
		     */
		    char *sname = idio_handle_name_as_C (o);
		    char *info;
		    idio_asprintf (&info, ":\"%s\":%jd:%jd>", sname, (intmax_t) IDIO_HANDLE_LINE (o), (intmax_t) IDIO_HANDLE_POS (o));
		    idio_gc_free (sname);
		    size_t info_size = strlen (info);
		    IDIO_STRCAT_FREE (r, sizep, info, info_size);
		}
		break;
	    case IDIO_TYPE_C_INT:
		idio_asprintf (&r, "%jd", IDIO_C_TYPE_INT (o));
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_UINT:
		idio_asprintf (&r, "%ju", IDIO_C_TYPE_UINT (o));
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_FLOAT:
		idio_asprintf (&r, "%g", IDIO_C_TYPE_FLOAT (o));
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_DOUBLE:
		idio_asprintf (&r, "%g", IDIO_C_TYPE_DOUBLE (o));
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_C_POINTER:
		if (NULL != IDIO_C_TYPE_POINTER_PRINTER (o)) {
		    char *s = IDIO_C_TYPE_POINTER_PRINTER (o) (o);
		    *sizep = strlen (s);
		    return s;
		} else {
		    idio_asprintf (&r, "#<C/* %p%s>", IDIO_C_TYPE_POINTER_P (o), IDIO_C_TYPE_POINTER_FREEP (o) ? " free" : "");
		}
		*sizep = strlen (r);
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		{
#ifdef IDIO_DEBUG
		    idio_asprintf (&r, "#<ST %10p ", o);
#else
		    idio_asprintf (&r, "#<ST ");
#endif
		    *sizep = strlen (r);

		    size_t stn_size = 0;
		    char *stn = idio_as_string (IDIO_STRUCT_TYPE_NAME (o), &stn_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, stn, stn_size);
		    IDIO_STRCAT (r, sizep, " ");

		    size_t stp_size = 0;
		    char *stp = idio_as_string (IDIO_STRUCT_TYPE_PARENT (o), &stp_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, stp, stp_size);

		    size_t size = IDIO_STRUCT_TYPE_SIZE (o);
		    size_t i;
		    for (i = 0; i < size; i++) {
			IDIO_STRCAT (r, sizep, " ");
			size_t f_size = 0;
			char *fs = idio_as_string (IDIO_STRUCT_TYPE_FIELDS (o, i), &f_size, 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, fs, f_size);
		    }

		    IDIO_STRCAT (r, sizep, ">");
		}
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		{
		    seen = idio_pair (o, seen);
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
			    /*
			     * NB call the display_string variant at
			     * this point as {s} is now a string and
			     * returning as_string ({s}) => "<#SI
			     * ...>" (ie. with double-quotes) rather
			     * than the <#SI ...> we expect from a
			     * struct instance.
			     *
			     * It confused me...
			     */
			    return idio_display_string (s, sizep);
			}
		    }

#ifdef IDIO_DEBUG
		    idio_asprintf (&r, "#<SI %10p ", o);
#else
		    idio_asprintf (&r, "#<SI ");
#endif
		    *sizep = strlen (r);

		    size_t n_size = 0;
		    char *ns = idio_as_string (IDIO_STRUCT_TYPE_NAME (sit), &n_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, ns, n_size);

		    size_t size = IDIO_STRUCT_TYPE_SIZE (sit);
		    size_t i;
		    for (i = 0; i < size; i++) {
			IDIO_STRCAT (r, sizep, " ");
			size_t fn_size = 0;
			char *fns = idio_as_string (IDIO_STRUCT_TYPE_FIELDS (sit, i), &fn_size, 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, fns, fn_size);
			IDIO_STRCAT (r, sizep, ":");
			size_t fv_size = 0;
			char *fvs = idio_as_string (IDIO_STRUCT_INSTANCE_FIELDS (o, i), &fv_size, depth - 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, fvs, fv_size);
		    }

		    IDIO_STRCAT (r, sizep, ">");
		}
		break;
	    case IDIO_TYPE_THREAD:
		{
		    /*
		     * Code coverage:
		     *
		     * Not usually user-visible.
		     */
		    seen = idio_pair (o, seen);
		    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (o));
		    idio_asprintf (&r, "#<THR %10p\n  pc=%6zd\n  sp/top=%2zd/",
				   o,
				   IDIO_THREAD_PC (o),
				   sp - 1);
		    *sizep = strlen (r);

		    size_t t_size = 0;
		    char *t = idio_as_string (idio_array_top (IDIO_THREAD_STACK (o)), &t_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  val=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_VAL (o), &t_size, 2, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  func=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_FUNC (o), &t_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    if (1 == depth) {
			IDIO frame = IDIO_THREAD_FRAME (o);

			if (idio_S_nil == frame) {
			    IDIO_STRCAT (r, sizep, "\n  fr=nil");
			} else {
			    char *es;
			    idio_asprintf (&es, "\n  fr=%10p n=%td ", frame, IDIO_FRAME_NPARAMS (frame));
			    size_t es_size = strlen (es);
			    IDIO_STRCAT_FREE (r, sizep, es, es_size);

			    size_t f_size = 0;
			    char *fs = idio_as_string (frame, &f_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, fs, f_size);
			}
		    }

		    IDIO_STRCAT (r, sizep, "\n  env=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_ENV (o), &t_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

#ifdef IDIO_VM_DYNAMIC_REGISTERS
		    IDIO_STRCAT (r, sizep, "\n  t/sp=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_TRAP_SP (o), &t_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  d/sp=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_DYNAMIC_SP (o), &t_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    IDIO_STRCAT (r, sizep, "\n  e/sp=");
		    t_size = 0;
		    t = idio_as_string (IDIO_THREAD_ENVIRON_SP (o), &t_size, 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
#endif
		    if (depth > 1) {
			IDIO_STRCAT (r, sizep, "\n  fr=");
			t_size = 0;
			t = idio_as_string (IDIO_THREAD_FRAME (o), &t_size, 1, seen, 0);
			IDIO_STRCAT_FREE (r, sizep, t, t_size);

			if (depth > 2) {
			    IDIO_STRCAT (r, sizep, "\n  reg1=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_REG1 (o), &t_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  reg2=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_REG2 (o), &t_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  expr=");
			    IDIO fmci = IDIO_THREAD_EXPR (o);
			    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
			    idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

			    IDIO src = idio_vm_constants_ref (gci);

			    t_size = 0;
			    t = idio_as_string (src, &t_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  input_handle=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_INPUT_HANDLE (o), &t_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  output_handle=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_OUTPUT_HANDLE (o), &t_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  error_handle=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_ERROR_HANDLE (o), &t_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);

			    IDIO_STRCAT (r, sizep, "\n  module=");
			    t_size = 0;
			    t = idio_as_string (IDIO_THREAD_MODULE (o), &t_size, 1, seen, 0);
			    IDIO_STRCAT_FREE (r, sizep, t, t_size);
			}
		    }
		    IDIO_STRCAT (r, sizep, ">");
		}
		break;
	    case IDIO_TYPE_CONTINUATION:
		{
		    seen = idio_pair (o, seen);
		    IDIO ks = IDIO_CONTINUATION_STACK (o);
		    idio_ai_t kss = idio_array_size (ks);
		    IDIO pc_I = idio_array_ref_index (ks, kss - 3);

		    /*
		     * We preserved some of the continuation state on
		     * the continuation stack so the actual
		     * continuation stack size is kss minus eight.
		     *
		     * Check idio_continuation() for pc_I above.
		     */
#ifdef IDIO_DEBUG
		    idio_asprintf (&r, "#<K %10p ss=%zu PC=%td>", o, kss - 8, IDIO_FIXNUM_VAL (pc_I));
#else
		    idio_asprintf (&r, "#<K ss=%zu PC=%td>", kss - 8, IDIO_FIXNUM_VAL (pc_I));
#endif
		    *sizep = strlen (r);
		}
		break;
	    case IDIO_TYPE_BITSET:
		{
		    idio_asprintf (&r, "#B{ %zu ", IDIO_BITSET_SIZE (o));
		    *sizep = strlen (r);

		    size_t bs_size = IDIO_BITSET_SIZE (o);
		    size_t count = 0;
		    int print_lead = 0;
		    int in_range = 0;
		    size_t range_start = 0;
		    size_t n_ul = bs_size / IDIO_BITSET_BITS_PER_WORD + 1;
		    for (i = 0; i < n_ul; i++) {
			/*
			 * Native format is chunked into
			 * idio_bitset_word_t
			 */
			idio_bitset_word_t ul_bits = IDIO_BITSET_WORDS (o, i);

			if (ul_bits) {
			    int b;
			    for (b = 0; count < bs_size &&
				     b < sizeof (idio_bitset_word_t); b++) {
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
				idio_bitset_word_t mask = UCHAR_MAX;
				mask <<= offset;
				idio_bitset_word_t byte_bits = ul_bits & mask;

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
					    /*
					     * Code coverage:
					     *
					     * We get here if a byte
					     * was partially set after
					     * one or more entire
					     * bytes being set.
					     */
					    in_range = 0;
					    idio_asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT);
					    size_t lead_size = strlen (lead);
					    IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
					} else {
					    idio_asprintf (&lead, "%zx:", count);
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
					    /*
					     * Code coverage:
					     *
					     * This is run if the last
					     * byte has some bits set.
					     */
					    break;
					}
				    }
				    bits[j] = '\0';

				    IDIO_STRCAT (r, sizep, bits);
				    IDIO_STRCAT (r, sizep, " ");
				} else {
				    char *lead;
				    if (in_range) {
					/*
					 * Code coverage:
					 *
					 * We get here if an entire
					 * byte was unset after one or
					 * more entire bytes being
					 * set.
					 */
					in_range = 0;
					idio_asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT);
					size_t lead_size = strlen (lead);
					IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
				    }

				    count += CHAR_BIT;
				    print_lead = 1;
				}
			    }
			} else {
			    if (in_range) {
				/*
				 * Code coverage:
				 *
				 * We get here if an entire word was
				 * unset after one or more entire
				 * bytes being set up to the end of a
				 * word.
				 */
				in_range = 0;
				char *lead;
				idio_asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT);
				size_t lead_size = strlen (lead);
				IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
			    }

			    count += IDIO_BITSET_BITS_PER_WORD;
			    print_lead = 1;
			}
		    }
		    if (in_range) {
			/*
			 * Code coverage:
			 *
			 * A range left hanging in the last word.
			 */
			in_range = 0;
			char *lead;
			idio_asprintf (&lead, "%zx-%zx ", range_start, count - CHAR_BIT);
			size_t lead_size = strlen (lead);
			IDIO_STRCAT_FREE (r, sizep, lead, lead_size);
		    }
		    IDIO_STRCAT (r, sizep, "}");
		}
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		{
		    idio_asprintf (&r, "#<C/typedef %10p>", IDIO_C_TYPEDEF_SYM (o));
		    *sizep = strlen (r);
		    break;
		}
	    case IDIO_TYPE_C_STRUCT:
		{
#ifdef IDIO_DEBUG
		    idio_asprintf (&r, "#<C/struct %10p ", o);
#else
		    idio_asprintf (&r, "#<C/struct ");
#endif
		    *sizep = strlen (r);

		    IDIO_STRCAT (r, sizep, "\n\tfields: ");
		    size_t f_size = 0;
		    char *fs = idio_as_string (IDIO_C_STRUCT_FIELDS (o), &f_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, fs, f_size);

		    IDIO mh = IDIO_C_STRUCT_METHODS (o);

		    IDIO_STRCAT (r, sizep, "\n\tmethods: ");
		    if (idio_S_nil != mh) {
			for (i = 0; i < IDIO_HASH_SIZE (mh); i++) {
			    idio_hash_entry_t *he = IDIO_HASH_HA (mh, i);
			    for (; NULL != he ; he = IDIO_HASH_HE_NEXT (he)) {
				if (idio_S_nil != IDIO_HASH_HE_KEY (he)) {
				    IDIO_STRCAT (r, sizep, "\n\t");
				    size_t t_size = 0;
				    char *t = idio_as_string (IDIO_HASH_HE_KEY (he), &t_size, depth - 1, seen, 0);
				    IDIO_STRCAT_FREE (r, sizep, t, t_size);
				    IDIO_STRCAT (r, sizep, ":");
				    if (IDIO_HASH_HE_VALUE (he)) {
					t_size = 0;
					t = idio_as_string (IDIO_HASH_HE_VALUE (he), &t_size, depth - 1, seen, 0);
				    } else {
					idio_asprintf (&t, "-");
					t_size = strlen (t);
				    }
				    IDIO_STRCAT_FREE (r, sizep, t, t_size);
				    IDIO_STRCAT (r, sizep, " ");
				}
			    }
			}
		    }
		    IDIO_STRCAT (r, sizep, "\n\tframe: ");
		    f_size = 0;
		    fs = idio_as_string (IDIO_C_STRUCT_FRAME (o), &f_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, fs, f_size);
		    IDIO_STRCAT (r, sizep, "\n>");
		    break;
		}
	    case IDIO_TYPE_C_INSTANCE:
		{
#ifdef IDIO_DEBUG
		    idio_asprintf (&r, "#<C/instance %10p C/*=%10p C/struct=%10p>", o, IDIO_C_INSTANCE_P (o), IDIO_C_INSTANCE_C_STRUCT (o));
#else
		    idio_asprintf (&r, "#<C/instance C/*=%10p C/struct=%10p>", IDIO_C_INSTANCE_P (o), IDIO_C_INSTANCE_C_STRUCT (o));
#endif
		    *sizep = strlen (r);
		    break;
		}
	    case IDIO_TYPE_C_FFI:
		{
		    idio_asprintf (&r, "#<CFFI ");
		    *sizep = strlen (r);

		    size_t t_size = 0;
		    char *t = idio_as_string (IDIO_C_FFI_NAME (o), &t_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_SYMBOL (o), &t_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " ");

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_ARGS (o), &t_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " ");

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_RESULT (o), &t_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " ");

		    t_size = 0;
		    t = idio_as_string (IDIO_C_FFI_NAME (o), &t_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		    IDIO_STRCAT (r, sizep, " >");
		    break;
		}
	    case IDIO_TYPE_OPAQUE:
		{
		    idio_asprintf (&r, "#<O %10p ", IDIO_OPAQUE_P (o));
		    *sizep = strlen (r);

		    size_t t_size = 0;
		    char *t = idio_as_string (IDIO_OPAQUE_ARGS (o), &t_size, depth - 1, seen, 0);
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
			idio_asprintf (&r, "#<void?? %10p>", o);
		    } else {
			idio_asprintf (&r, "#<string?? \"%s\">", (char *) o);
		    }
		    *sizep = strlen (r);
		    IDIO_C_ASSERT (0);
		}
		break;
	    }
	}
	break;
    default:
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_asprintf (&r, "#<type?? %10p>", o);
	*sizep = strlen (r);
	break;
    }

    return r;
}

char *idio_as_string_safe (IDIO o, size_t *sizep, int depth, int first)
{
    idio_gc_pause ("idio-as-string-safe");
    char *s = idio_as_string (o, sizep, depth, idio_S_nil, first);
    idio_gc_resume ("idio-as-string-safe");

    return s;
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
	r = idio_as_string_safe (o, sizep, 4, 1);
	break;
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		r = idio_as_string_safe (o, sizep, 4, 1);
		break;
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		{
		    intptr_t c = IDIO_CHARACTER_VAL (o);
		    idio_asprintf (&r, "%c", (char) c);
		    *sizep = strlen (r);
		}
		break;
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		{
		    idio_unicode_t u = IDIO_UNICODE_VAL (o);
		    if (u > 0x10ffff) {
			/*
			 * Test Case: ??
			 *
			 * Coding error.
			 */
			/*
			 * Hopefully, this is guarded against elsewhere
			 */
			fprintf (stderr, "display-string: oops u=%x > 0x10ffff\n", u);
			idio_error_param_value ("codepoint", "out of bounds", IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    } else if (u >= 0x10000) {
			idio_asprintf (&r, "%c%c%c%c",
				       0xf0 | ((u & (0x07 << 18)) >> 18),
				       0x80 | ((u & (0x3f << 12)) >> 12),
				       0x80 | ((u & (0x3f << 6)) >> 6),
				       0x80 | ((u & (0x3f << 0)) >> 0));
		    } else if (u >= 0x0800) {
			idio_asprintf (&r, "%c%c%c",
				       0xe0 | ((u & (0x0f << 12)) >> 12),
				       0x80 | ((u & (0x3f << 6)) >> 6),
				       0x80 | ((u & (0x3f << 0)) >> 0));
		    } else if (u >= 0x0080) {
			idio_asprintf (&r, "%c%c",
				       0xc0 | ((u & (0x1f << 6)) >> 6),
				       0x80 | ((u & (0x3f << 0)) >> 0));
		    } else {
			idio_asprintf (&r, "%c",
				       u & 0x7f);
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
		r = idio_as_string_safe (o, sizep, 40, 1);
		break;
	    }
	}
	break;
    default:
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_asprintf (&r, "type %d n/k", o->type);
	*sizep = strlen (r);
	break;
    }

    return r;
}

IDIO idio_util_string (IDIO o)
{
    IDIO_ASSERT (o);

    size_t size = 0;
    char *str = idio_as_string_safe (o, &size, 40, 1);
    IDIO r = idio_string_C_len (str, size);
    idio_gc_free (str);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("string", string, (IDIO o), "o", "\
convert `o` to a string				\n\
						\n\
:param o: object to convert			\n\
						\n\
:return: a string representation of `o`		\n\
")
{
    IDIO_ASSERT (o);

    return idio_util_string (o);
}

IDIO_DEFINE_PRIMITIVE1_DS ("->string", 2string, (IDIO o), "o", "\
convert `o` to a string unless it already is	\n\
a string					\n\
						\n\
:param o: object to convert			\n\
						\n\
:return: a string representation of `o`		\n\
")
{
    IDIO_ASSERT (o);

    if (idio_isa_string (o)) {
	return o;
    } else {
	return idio_util_string (o);
    }
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
    idio_gc_free (str);

    return r;
}

/*
 * Code coverage:
 *
 * Used by the disassembler.
 *
 * Compile a debug build then run with --vm-reports
 */
const char *idio_vm_bytecode2string (int code)
{
    char *r;

    switch (code) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:		r = "A-SHALLOW-ARGUMENT-REF0";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:		r = "A-SHALLOW-ARGUMENT-REF1";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:		r = "A-SHALLOW-ARGUMENT-REF2";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:		r = "A-SHALLOW-ARGUMENT-REF3";		break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:		r = "A-SHALLOW-ARGUMENT-REF";		break;
    case IDIO_A_DEEP_ARGUMENT_REF:		r = "A-DEEP-ARGUMENT-REF";		break;

    case IDIO_A_SHALLOW_ARGUMENT_SET0:		r = "A-SHALLOW-ARGUMENT-SET0";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:		r = "A-SHALLOW-ARGUMENT-SET1";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:		r = "A-SHALLOW-ARGUMENT-SET2";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:		r = "A-SHALLOW-ARGUMENT-SET3";		break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:		r = "A-SHALLOW-ARGUMENT-SET";		break;
    case IDIO_A_DEEP_ARGUMENT_SET:		r = "A-DEEP-ARGUMENT-SET";		break;

    case IDIO_A_GLOBAL_SYM_REF:			r = "A-GLOBAL-SYM-REF";			break;
    case IDIO_A_CHECKED_GLOBAL_SYM_REF:		r = "A-CHECKED-GLOBAL-SYM-REF";		break;
    case IDIO_A_GLOBAL_FUNCTION_SYM_REF:	r = "A-GLOBAL-FUNCTION-SYM-REF";	break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF:r = "A-CHECKED-GLOBAL-FUNCTION-SYM-REF";break;
    case IDIO_A_CONSTANT_SYM_REF:		r = "A-CONSTANT-SYM-REF";		break;
    case IDIO_A_COMPUTED_SYM_REF:		r = "A-COMPUTED-SYM-REF";		break;

    case IDIO_A_GLOBAL_SYM_DEF:			r = "A-GLOBAL-SYM-DEF";			break;
    case IDIO_A_GLOBAL_SYM_SET:			r = "A-GLOBAL-SYM-SET";			break;
    case IDIO_A_COMPUTED_SYM_SET:		r = "A-COMPUTED-SYM-SET";		break;
    case IDIO_A_COMPUTED_SYM_DEF:		r = "A-COMPUTED-SYM-DEF";		break;

    case IDIO_A_GLOBAL_VAL_REF:			r = "A-GLOBAL-VAL-REF";			break;
    case IDIO_A_CHECKED_GLOBAL_VAL_REF:		r = "A-CHECKED-GLOBAL-VAL-REF";		break;
    case IDIO_A_GLOBAL_FUNCTION_VAL_REF:	r = "A-GLOBAL-FUNCTION-VAL-REF";	break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF:r = "A-CHECKED-GLOBAL-FUNCTION-VAL-REF";break;
    case IDIO_A_CONSTANT_VAL_REF:		r = "A-CONSTANT-VAL-REF";		break;
    case IDIO_A_COMPUTED_VAL_REF:		r = "A-COMPUTED-VAL-REF";		break;

    case IDIO_A_GLOBAL_VAL_DEF:			r = "A-GLOBAL-VAL-DEF";			break;
    case IDIO_A_GLOBAL_VAL_SET:			r = "A-GLOBAL-VAL-SET";			break;
    case IDIO_A_COMPUTED_VAL_SET:		r = "A-COMPUTED-VAL-SET";		break;
    case IDIO_A_COMPUTED_VAL_DEF:		r = "A-COMPUTED-VAL-DEF";		break;

    case IDIO_A_PREDEFINED0:			r = "A-PREDEFINED0";			break;
    case IDIO_A_PREDEFINED1:			r = "A-PREDEFINED1";			break;
    case IDIO_A_PREDEFINED2:			r = "A-PREDEFINED2";			break;
    case IDIO_A_PREDEFINED3:			r = "A-PREDEFINED3";			break;
    case IDIO_A_PREDEFINED4:			r = "A-PREDEFINED4";			break;
    case IDIO_A_PREDEFINED5:			r = "A-PREDEFINED5";			break;
    case IDIO_A_PREDEFINED6:			r = "A-PREDEFINED6";			break;
    case IDIO_A_PREDEFINED7:			r = "A-PREDEFINED7";			break;
    case IDIO_A_PREDEFINED8:			r = "A-PREDEFINED8";			break;
    case IDIO_A_PREDEFINED:			r = "A-PREDEFINED";			break;

    case IDIO_A_LONG_GOTO:			r = "A-LONG-GOTO";			break;
    case IDIO_A_LONG_JUMP_FALSE:		r = "A-LONG-JUMP-FALSE";		break;
    case IDIO_A_LONG_JUMP_TRUE:			r = "A-LONG-JUMP-TRUE";			break;
    case IDIO_A_SHORT_GOTO:			r = "A-SHORT-GOTO";			break;
    case IDIO_A_SHORT_JUMP_FALSE:		r = "A-SHORT-JUMP-FALSE";		break;
    case IDIO_A_SHORT_JUMP_TRUE:		r = "A-SHORT-JUMP-TRUE";		break;

    case IDIO_A_PUSH_VALUE:			r = "A-PUSH-VALUE";			break;
    case IDIO_A_POP_VALUE:			r = "A-POP-VALUE";			break;
    case IDIO_A_POP_REG1:			r = "A-POP-REG1";			break;
    case IDIO_A_POP_REG2:			r = "A-POP-REG2";			break;
    case IDIO_A_SRC_EXPR:			r = "A-SRC-EXPR";			break;
    case IDIO_A_POP_FUNCTION:			r = "A-POP-FUNCTION";			break;
    case IDIO_A_PRESERVE_STATE:			r = "A-PRESERVE-STATE";			break;
    case IDIO_A_RESTORE_STATE:			r = "A-RESTORE-STATE";			break;
    case IDIO_A_RESTORE_ALL_STATE:		r = "A-RESTORE-ALL-STATE";		break;

    case IDIO_A_CREATE_CLOSURE:			r = "A-CREATE-CLOSURE";			break;
    case IDIO_A_FUNCTION_INVOKE:		r = "A-FUNCTION-INVOKE";		break;
    case IDIO_A_FUNCTION_GOTO:			r = "A-FUNCTION-GOTO";			break;
    case IDIO_A_RETURN:				r = "A-RETURN";				break;
    case IDIO_A_FINISH:				r = "A-FINISH";				break;
    case IDIO_A_ABORT:				r = "A-ABORT";				break;

    case IDIO_A_ALLOCATE_FRAME1:		r = "A-ALLOCATE-FRAME1";		break;
    case IDIO_A_ALLOCATE_FRAME2:		r = "A-ALLOCATE-FRAME2";		break;
    case IDIO_A_ALLOCATE_FRAME3:		r = "A-ALLOCATE-FRAME3";		break;
    case IDIO_A_ALLOCATE_FRAME4:		r = "A-ALLOCATE-FRAME4";		break;
    case IDIO_A_ALLOCATE_FRAME5:		r = "A-ALLOCATE-FRAME5";		break;
    case IDIO_A_ALLOCATE_FRAME:			r = "A-ALLOCATE-FRAME";			break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:		r = "A-ALLOCATE-DOTTED-FRAME";		break;
    case IDIO_A_REUSE_FRAME:			r = "A-REUSE-FRAME";			break;

    case IDIO_A_POP_FRAME0:			r = "A-POP-FRAME0";			break;
    case IDIO_A_POP_FRAME1:			r = "A-POP-FRAME1";			break;
    case IDIO_A_POP_FRAME2:			r = "A-POP-FRAME2";			break;
    case IDIO_A_POP_FRAME3:			r = "A-POP-FRAME3";			break;
    case IDIO_A_POP_FRAME:			r = "A-POP-FRAME";			break;

    case IDIO_A_LINK_FRAME:			r = "A-LINK-FRAME";			break;
    case IDIO_A_UNLINK_FRAME:			r = "A-UNLINK-FRAME";			break;
    case IDIO_A_PACK_FRAME:			r = "A-PACK-FRAME";			break;
    case IDIO_A_POP_LIST_FRAME:			r = "A-POP-LIST-FRAME";			break;

    case IDIO_A_ARITY1P:			r = "A-ARITY1P";			break;
    case IDIO_A_ARITY2P:			r = "A-ARITY2P";			break;
    case IDIO_A_ARITY3P:			r = "A-ARITY3P";			break;
    case IDIO_A_ARITY4P:			r = "A-ARITY4P";			break;
    case IDIO_A_ARITYEQP:			r = "A-ARITYEQP";			break;
    case IDIO_A_ARITYGEP:			r = "A-ARITYGEP";			break;

    case IDIO_A_SHORT_NUMBER:			r = "A-SHORT-NUMBER";			break;
    case IDIO_A_SHORT_NEG_NUMBER:		r = "A-SHORT-NEG-NUMBER";		break;
    case IDIO_A_CONSTANT_0:			r = "A-CONSTANT-0";			break;
    case IDIO_A_CONSTANT_1:			r = "A-CONSTANT-1";			break;
    case IDIO_A_CONSTANT_2:			r = "A-CONSTANT-2";			break;
    case IDIO_A_CONSTANT_3:			r = "A-CONSTANT-3";			break;
    case IDIO_A_CONSTANT_4:			r = "A-CONSTANT-4";			break;
    case IDIO_A_FIXNUM:				r = "A-FIXNUM";				break;
    case IDIO_A_NEG_FIXNUM:			r = "A-NEG-FIXNUM";			break;
    case IDIO_A_CHARACTER:			r = "A-CHARACTER";			break;
    case IDIO_A_NEG_CHARACTER:			r = "A-NEG-CHARACTER";			break;
    case IDIO_A_CONSTANT:			r = "A-CONSTANT";			break;
    case IDIO_A_NEG_CONSTANT:			r = "A-NEG-CONSTANT";			break;
    case IDIO_A_UNICODE:			r = "A-UNICODE";			break;

    case IDIO_A_NOP:				r = "A-NOP";				break;
    case IDIO_A_PRIMCALL0:			r = "A-PRIMCALL0";			break;
    case IDIO_A_PRIMCALL1:			r = "A-PRIMCALL1";			break;
    case IDIO_A_PRIMCALL2:			r = "A-PRIMCALL2";			break;

    case IDIO_A_PRIMCALL3:			r = "A-PRIMCALL3";			break;
    case IDIO_A_PRIMCALL:			r = "A-PRIMCALL";			break;

    case IDIO_A_EXPANDER:			r = "A-EXPANDER";			break;
    case IDIO_A_INFIX_OPERATOR:			r = "A-INFIX-OPERATOR";			break;
    case IDIO_A_POSTFIX_OPERATOR:		r = "A-POSTFIX-OPERATOR";		break;

    case IDIO_A_PUSH_DYNAMIC:			r = "A-PUSH-DYNAMIC";			break;
    case IDIO_A_POP_DYNAMIC:			r = "A-POP-DYNAMIC";			break;
    case IDIO_A_DYNAMIC_SYM_REF:		r = "A-DYNAMIC-SYM-REF";		break;
    case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:	r = "A-DYNAMIC-FUNCTION-SYM-REF";	break;
    case IDIO_A_DYNAMIC_VAL_REF:		r = "A-DYNAMIC-VAL-REF";		break;
    case IDIO_A_DYNAMIC_FUNCTION_VAL_REF:	r = "A-DYNAMIC-FUNCTION-VAL-REF";	break;

    case IDIO_A_PUSH_ENVIRON:			r = "A-PUSH-ENVIRON";			break;
    case IDIO_A_POP_ENVIRON:			r = "A-POP-ENVIRON";			break;
    case IDIO_A_ENVIRON_SYM_REF:		r = "A-ENVIRON-SYM-REF";		break;
    case IDIO_A_ENVIRON_VAL_REF:		r = "A-ENVIRON-VAL-REF";		break;

    case IDIO_A_NON_CONT_ERR:			r = "A-NON-CONT-ERR";			break;
    case IDIO_A_PUSH_TRAP:			r = "A-PUSH-TRAP";			break;
    case IDIO_A_POP_TRAP:			r = "A-POP-TRAP";			break;
    case IDIO_A_RESTORE_TRAP:			r = "A-RESTORE-TRAP";			break;

    case IDIO_A_PUSH_ESCAPER:			r = "A-PUSH-ESCAPER";			break;
    case IDIO_A_POP_ESCAPER:			r = "A-POP-ESCAPER";			break;

    default:
	/* fprintf (stderr, "idio_vm_bytecode2string: unexpected bytecode %d\n", code); */
	r = "Unknown bytecode";
	break;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("value-index", value_index, (IDIO o, IDIO i), "o i", "\
index the object `o` by `i`			\n\
						\n\
indexable object types are:			\n\
pair:			(nth o i)		\n\
string:			(string-ref o i)	\n\
array:			(array-ref o i)		\n\
hash:			(hash-ref o i)		\n\
struct instance:	(struct-instance-ref o i)	\n\
						\n\
Note that, in particular for struct instance,	\n\
the symbol used for a symbolic field name may	\n\
have been evaluated to a value.			\n\
						\n\
Quote if necessary: o.'i			\n\
						\n\
value-index is not efficient			\n\
						\n\
:param o: object to index			\n\
:type o: any					\n\
:param i: index					\n\
:type i: any					\n\
						\n\
:return: the indexed value			\n\
")
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
		return idio_hash_reference (o, i, idio_S_nil);
	    case IDIO_TYPE_STRUCT_INSTANCE:
		return idio_struct_instance_ref (o, i);
	    default:
		break;
	    }
	}
    default:
	break;
    }

    /*
     * Test Case: util-errors/value-index-bad-type.idio
     *
     * 1 . 2
     */
    idio_error_param_value ("value", "is non-indexable", IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE3_DS ("set-value-index!", set_value_index, (IDIO o, IDIO i, IDIO v), "o i v", "\
set value of the object `o` indexed by `i` to `v`	\n\
						\n\
indexable object types are:			\n\
pair:			(nth o i)		\n\
string:			(string-ref o i)	\n\
array:			(array-ref o i)		\n\
hash:			(hash-ref o i)		\n\
struct instance:	(struct-instance-ref o i)	\n\
						\n\
Note that, in particular for struct instance,	\n\
the symbol used for a symbolic field name may	\n\
have been evaluated to a value.			\n\
						\n\
Quote if necessary: o.'i			\n\
						\n\
value-index is not efficient			\n\
						\n\
:param o: object to index			\n\
:type o: any					\n\
:param i: index					\n\
:type i: any					\n\
:param v: value					\n\
:type v: any					\n\
						\n\
:return: the indexed value			\n\
")
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

    /*
     * Test Case: util-errors/set-value-index-bad-type.idio
     *
     * 1 . 2 = 3
     */
    idio_error_param_value ("value", "is non-indexable", IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO idio_copy (IDIO o, int depth)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	return o;
    case IDIO_TYPE_PLACEHOLDER_MARK:
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
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
		return idio_copy_array (o, depth, 0);
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
		    /*
		     * Test Case: util-errors/copy-value-bad-struct-instance.idio
		     *
		     * si := make-struct-instance (define-struct foo x y) 1 2
		     * copy-value si
		     */
		    idio_error_param_value ("struct instance", "not of a valid struct type", IDIO_C_FUNC_LOCATION ());

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
		/*
		 * Test Case: util-errors/copy-value-bad-type.idio
		 *
		 * copy-value list
		 */
		idio_error_param_value ("value", "invalid type", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_C ("unimplemented type", o, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
		break;
	    }
	}
	break;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", o, (intptr_t) o & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

	return idio_S_notreached;
	break;
    }

    /*
     * Test Case: ??
     *
     * Coding error.  Is this reachable?
     */
    idio_error_C ("failed to copy", o, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("copy-value", copy_value, (IDIO v, IDIO args), "v [depth]", "\
copy ``v`` to `depth`					\n\
							\n\
:param v: value to copy					\n\
:type v: any						\n\
:param depth: (optional) 'shallow or 'deep (default)	\n\
							\n\
:return: copy of `v`					\n\
")
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    int depth = IDIO_COPY_DEEP;

    if (idio_S_nil != args) {
	IDIO idepth = IDIO_PAIR_H (args);

	if (idio_isa_symbol (idepth)) {
	    if (idio_S_deep == idepth) {
		depth = IDIO_COPY_DEEP;
	    } else if (idio_S_shallow == idepth) {
		depth = IDIO_COPY_SHALLOW;
	    } else {
		/*
		 * Test Case: util-errors/copy-value-bad-depth.idio
		 *
		 * copy-value "hello" 'badly
		 */
		idio_error_param_value ("depth", "should be 'deep or 'shallow", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: util-errors/copy-value-bad-depth-type.idio
	     *
	     * copy-value "hello" #t
	     */
	    idio_error_param_type ("symbol", idepth, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO c = idio_copy (v, depth);

    return c;
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
	    }
	    fprintf (stderr, "t=%2d/%4.4s f=%2x gcf=%2x ", o->type, idio_type2string (o), o->flags, o->gc_flags);

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
				char *s = idio_as_string_safe (IDIO_ARRAY_AE (o, i), &size, 4, 1);
				fprintf (stderr, "\t%3zu: %10p %10s\n", i, IDIO_ARRAY_AE (o, i), s);
				idio_gc_free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_HASH:
		if (detail) {
		    fprintf (stderr, "hsz=%zu hm=%zx hc=%zu\n", IDIO_HASH_SIZE (o), IDIO_HASH_MASK (o), IDIO_HASH_COUNT (o));
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
			    idio_hash_entry_t *he = IDIO_HASH_HA (o, i);
			    for (; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
				if (idio_S_nil == IDIO_HASH_HE_KEY (he)) {
				    continue;
				} else {
				    size_t size = 0;
				    char *s;
				    if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) {
					s = (char *) IDIO_HASH_HE_KEY (he);
				    } else {
					s = idio_as_string_safe (IDIO_HASH_HE_KEY (he), &size, 4, 1);
				    }
				    if (detail & 0x4) {
					fprintf (stderr, "\t%30s : ", s);
				    } else {
					fprintf (stderr, "\t%3zu: k=%10p v=%10p %10s : ",
						 i,
						 IDIO_HASH_HE_KEY (he),
						 IDIO_HASH_HE_VALUE (he),
						 s);
				    }
				    if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
					idio_gc_free (s);
				    }
				    if (IDIO_HASH_HE_VALUE (he)) {
					size = 0;
					s = idio_as_string_safe (IDIO_HASH_HE_VALUE (he), &size, 4, 1);
				    } else {
					idio_asprintf (&s, "-");
					size = strlen (s);
				    }
				    fprintf (stderr, "%-10s\n", s);
				    idio_gc_free (s);
				}
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
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
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

IDIO_DEFINE_PRIMITIVE1_DS ("idio-dump", idio_dump, (IDIO o), "o", "\
print the internal details of ``o`` to *stderr*		\n\
							\n\
:param o: value to dump					\n\
:type o: any						\n\
							\n\
:return: #unspec					\n\
")
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
    char *os = idio_as_string_safe (o, &size, 40, 1);
    fprintf (file, fmt, os);
    idio_gc_free (os);
}

void idio_debug (const char *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    idio_debug_FILE (stderr, fmt, o);
}

IDIO_DEFINE_PRIMITIVE2_DS ("idio-debug", idio_debug, (IDIO fmt, IDIO o), "fmt o", "\
print the string value of ``o`` according to `fmt` to	\n\
*stderr*						\n\
							\n\
There must be a single %s conversion specification	\n\
							\n\
idio-debug \"foo is %20s\n\" foo			\n\
							\n\
:param fmt: printf(3) format string			\n\
:type fmt: string					\n\
:param o: value to dump					\n\
:type o: any						\n\
							\n\
:return: #unspec					\n\
")
{
    IDIO_ASSERT (fmt);
    IDIO_ASSERT (o);

    /*
     * Test Case: util-errors/idio-debug-bad-fmt-type.idio
     *
     * idio-debug #t #t
     */
    IDIO_USER_TYPE_ASSERT (string, fmt);

    size_t size = 0;
    char *sfmt = idio_string_as_C (fmt, &size);
    size_t C_size = strlen (sfmt);
    if (C_size != size) {
	/*
	 * Test Case: util-errors/idio-debug-format.idio
	 *
	 * idio-debug (join-string (make-string 1 #U+0) '("hello" "%s")) 1
	 *
	 * I'm not sure I particularly care that idio-debug has been
	 * given a string with an embedded ASCII NUL, after all, it is
	 * unlikely to break anything other than expectation.
	 *
	 * However, we should be protective of the careless user.
	 */
	idio_gc_free (sfmt);

	idio_error_param_value ("fmt", "contains an ASCII NUL", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_debug (sfmt, o);

    idio_gc_free (sfmt);

    return idio_S_unspec;
}

IDIO idio_add_feature (IDIO f)
{
    IDIO_ASSERT (f);

    if (!(idio_isa_symbol (f) ||
	  idio_isa_string (f))) {
	/*
	 * Test Case: ??
	 *
	 * The user interface is protected so this would be a coding
	 * error.
	 */
	idio_error_param_type ("symbol|string",  f, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (idio_isa_string (f)) {
	size_t size = 0;
	f = idio_symbols_C_intern (idio_display_string (f, &size));
    }

    IDIO fs = idio_module_symbol_value (idio_features, idio_Idio_module, idio_S_nil);

    return idio_module_set_symbol_value (idio_features, idio_pair (f, fs), idio_Idio_module);
}

IDIO_DEFINE_PRIMITIVE1_DS ("%add-feature", add_feature, (IDIO f), "f", "\
add feature ``f`` to Idio features			\n\
							\n\
:param f: feature to add				\n\
:type f: symbol or string				\n\
:return: new list of features				\n\
")
{
    IDIO_ASSERT (f);

    if (idio_isa_symbol (f) ||
	idio_isa_string (f)) {
	return idio_add_feature (f);
    } else {
	/*
	 * Test Case: util-errors/add-feature-bad-type.idio
	 *
	 * %add-feature #t
	 */
	idio_error_param_type ("symbol|string",  f, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

IDIO idio_add_feature_ps (char *p, char *s)
{
    IDIO_C_ASSERT (p);
    IDIO_C_ASSERT (s);

    size_t buflen = strlen (p) + strlen (s) + 1;
    char *buf;
    IDIO_GC_ALLOC (buf, buflen);
    sprintf (buf, "%s%s", p, s);
    IDIO r = idio_add_feature (idio_symbols_C_intern (buf));
    IDIO_GC_FREE (buf);
    return r;
}

IDIO idio_add_feature_pi (char *p, size_t size)
{
    IDIO_C_ASSERT (p);

    size_t buflen = strlen (p) + 20;
    char *buf;
    IDIO_GC_ALLOC (buf, buflen);
    sprintf (buf, "%s%zu", p, size);
    IDIO r = idio_add_feature (idio_symbols_C_intern (buf));
    IDIO_GC_FREE (buf);
    return r;
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

void idio_util_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (type_string);
    IDIO_ADD_PRIMITIVE (zerop);
    IDIO_ADD_PRIMITIVE (nullp);
    IDIO_ADD_PRIMITIVE (void);
    IDIO_ADD_PRIMITIVE (voidp);
    IDIO_ADD_PRIMITIVE (definedp);
    IDIO_ADD_PRIMITIVE (booleanp);
    IDIO_ADD_PRIMITIVE (not);
    IDIO_ADD_PRIMITIVE (eqp);
    IDIO_ADD_PRIMITIVE (eqvp);
    IDIO_ADD_PRIMITIVE (equalp);
    IDIO_ADD_PRIMITIVE (string);
    IDIO_ADD_PRIMITIVE (2string);
    IDIO_ADD_PRIMITIVE (display_string);
    IDIO_ADD_PRIMITIVE (value_index);
    IDIO_ADD_PRIMITIVE (set_value_index);
    IDIO_ADD_PRIMITIVE (copy_value);
    IDIO_ADD_PRIMITIVE (idio_dump);
    IDIO_ADD_PRIMITIVE (idio_debug);
    IDIO_ADD_PRIMITIVE (add_feature);
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

void idio_init_util ()
{
    idio_module_table_register (idio_util_add_primitives, idio_final_util);

    idio_util_value_as_string = idio_symbols_C_intern ("%%value-as-string");
    idio_module_set_symbol_value (idio_util_value_as_string, idio_S_nil, idio_Idio_module);
    idio_print_conversion_format_sym = idio_symbols_C_intern ("idio-print-conversion-format");
    idio_print_conversion_precision_sym = idio_symbols_C_intern ("idio-print-conversion-precision");

    idio_features = idio_symbols_C_intern ("*idio-features*");
    idio_module_set_symbol_value (idio_features, idio_S_nil, idio_Idio_module);

#ifdef IDIO_DEBUG
    idio_add_feature (idio_symbols_C_intern ("IDIO_DEBUG"));
#endif

#ifdef IDIO_EQUAL_DEBUG
    for (int i = 0; i < IDIO_TYPE_MAX; i++) {
	idio_equal_stats[i].count = 0;
	idio_equal_stats[i].duration.tv_sec = 0;
	idio_equal_stats[i].duration.tv_usec = 0;
	idio_equal_stats[i].mixed = 0;
    }
#endif
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
