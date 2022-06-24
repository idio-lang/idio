/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "bitset.h"
#include "c-type.h"
#include "closure.h"
#include "codegen.h"
#include "continuation.h"
#include "error.h"
#include "evaluate.h"
#include "file-handle.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "keyword.h"
#include "module.h"
#include "object.h"
#include "pair.h"
#include "path.h"
#include "primitive.h"
#include "read.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

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

char const *idio_type_enum2string (idio_type_e type)
{
    switch (type) {
    case IDIO_TYPE_NONE:		return "NONE";
    case IDIO_TYPE_FIXNUM:		return "fixnum";
    case IDIO_TYPE_CONSTANT_IDIO:	return "constant";
    case IDIO_TYPE_CONSTANT_TOKEN:	return "constant (token)";
    case IDIO_TYPE_CONSTANT_I_CODE:	return "constant (I_CODE)";
    case IDIO_TYPE_CONSTANT_UNICODE:	return "unicode";
    case IDIO_TYPE_PLACEHOLDER:		return "PLACEHOLDER";
    case IDIO_TYPE_STRING:		return "string";
    case IDIO_TYPE_SUBSTRING:		return "substring";
    case IDIO_TYPE_SYMBOL:		return "symbol";
    case IDIO_TYPE_KEYWORD:		return "keyword";
    case IDIO_TYPE_PAIR:		return "pair";
    case IDIO_TYPE_ARRAY:		return "array";
    case IDIO_TYPE_HASH:		return "hash";
    case IDIO_TYPE_CLOSURE:		return "closure";
    case IDIO_TYPE_PRIMITIVE:		return "primitive";
    case IDIO_TYPE_BIGNUM:		return "bignum";
    case IDIO_TYPE_MODULE:		return "module";
    case IDIO_TYPE_FRAME:		return "FRAME";
    case IDIO_TYPE_HANDLE:		return "handle";
    case IDIO_TYPE_STRUCT_TYPE:		return "struct-type";
    case IDIO_TYPE_STRUCT_INSTANCE:	return "struct-instance";
    case IDIO_TYPE_THREAD:		return "THREAD";
    case IDIO_TYPE_CONTINUATION:	return "CONTINUATION";
    case IDIO_TYPE_BITSET:		return "bitset";

    case IDIO_TYPE_C_CHAR:		return "C/char";
    case IDIO_TYPE_C_SCHAR:		return "C/schar";
    case IDIO_TYPE_C_UCHAR:		return "C/uchar";
    case IDIO_TYPE_C_SHORT:		return "C/short";
    case IDIO_TYPE_C_USHORT:		return "C/ushort";
    case IDIO_TYPE_C_INT:		return "C/int";
    case IDIO_TYPE_C_UINT:		return "C/uint";
    case IDIO_TYPE_C_LONG:		return "C/long";
    case IDIO_TYPE_C_ULONG:		return "C/ulong";
    case IDIO_TYPE_C_LONGLONG:		return "C/longlong";
    case IDIO_TYPE_C_ULONGLONG:		return "C/ulonglong";
    case IDIO_TYPE_C_FLOAT:		return "C/float";
    case IDIO_TYPE_C_DOUBLE:		return "C/double";
    case IDIO_TYPE_C_LONGDOUBLE:	return "C/longdouble";
    case IDIO_TYPE_C_POINTER:		return "C/pointer";
    case IDIO_TYPE_C_VOID:		return "C/void";

    default:
	return "NOT KNOWN";
    }
}

char const *idio_type2string (IDIO o)
{
    return idio_type_enum2string (idio_type (o));
}

IDIO_DEFINE_PRIMITIVE1_DS ("type->string", type_string, (IDIO o), "o", "\
return the type of `o` as a string		\n\
						\n\
:param o: object				\n\
:return: a string representation of the type of `o`	\n\
")
{
    IDIO_ASSERT (o);

    return idio_string_C (idio_type2string (o));
}

IDIO idio_util_method_typename (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    IDIO data = (IDIO) IDIO_VTABLE_METHOD_DATA (m);

    if (idio_isa_symbol (data) == 0) {
	idio_error_param_value_msg_only ("typename", "method->data", "should be a symbol", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return data;
}

IDIO_DEFINE_PRIMITIVE1_DS ("typename", typename, (IDIO o), "o", "\
return the type name of `o`	\n\
				\n\
:param o: object		\n\
:return: the type of `o`	\n\
")
{
    IDIO_ASSERT (o);

    idio_vtable_method_t *m = idio_vtable_lookup_method (idio_value_vtable (o), o, idio_S_typename, 1);

    return IDIO_VTABLE_METHOD_FUNC (m) (m, o);
}

IDIO idio_util_method_members (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    IDIO data = (IDIO) IDIO_VTABLE_METHOD_DATA (m);

    if (idio_isa_list (data) == 0) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_msg_only ("members", "method->data", "should be a list", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return data;
}

IDIO_DEFINE_PRIMITIVE1_DS ("members", members, (IDIO o), "o", "\
return the members of `o` as a list		\n\
						\n\
:param o: object				\n\
:return: a list of the members of `o`		\n\
						\n\
`o` should be an object with members such as a	\n\
:ref:`struct-instance <struct type>` or		\n\
:ref:`C/pointer <c module types>`.		\n\
")
{
    IDIO_ASSERT (o);

    idio_vtable_method_t *m = idio_vtable_lookup_method (idio_value_vtable (o), o, idio_S_members, 1);

    return IDIO_VTABLE_METHOD_FUNC (m) (m, o);
}

IDIO_DEFINE_PRIMITIVE1_DS ("zero?", zerop, (IDIO o), "o", "\
test if `o` is the numeric value zero		\n\
						\n\
a fixnum or a bignum with the value zero	\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is zero, ``#f`` otherwise	\n\
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
test if `o` is ``#n``				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is ``#n``, ``#f`` otherwise	\n\
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
:return: ``#<void>``			\n\
					\n\
Somewhat disingenuous as the act of calling	\n\
a function cannot be no computation...	\n\
")
{
    return idio_S_void;
}

IDIO_DEFINE_PRIMITIVE1_DS ("void?", voidp, (IDIO o), "o", "\
test if `o` is void (``#<void>``)		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is ``#<void>``, ``#f`` otherwise	\n\
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
 * Hmm, an (undef) primitive is ripe to abuse but evaluate.idio needs
 * to give dynamic and environ values #<undef> to mark them as
 * non-existent.
 */
IDIO_DEFINE_PRIMITIVE0_DS ("undef", undef, (void), "", "\
:return: ``#<undef>``			\n\
					\n\
The use of ``undef`` may result in	\n\
unexpected errors.			\n\
")
{
    return idio_S_undef;
}

IDIO_DEFINE_PRIMITIVE1_DS ("undef?", undefp, (IDIO o), "o", "\
test if `o` is undef (``#<undef>``)		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is ``#<undef>``, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_undef == o) {
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
:return: ``#t`` if `s` is defined in this environment, ``#f`` otherwise	\n\
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

    IDIO si = idio_module_env_symbol_recurse (s);

    if (idio_S_false != si) {
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
:return: ``#t`` if `o` is a boolean			\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_boolean (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("identity", identity, (IDIO e), "e", "\
return `e`					\n\
						\n\
:param e: expression to return			\n\
:type e: any					\n\
:return: `e`					\n\
")
{
    IDIO_ASSERT (e);

    return e;
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

int idio_eqp (void const *o1, void const *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQP);
}

int idio_eqvp (void const *o1, void const *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQVP);
}

int idio_equalp (void const *o1, void const *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQUALP);
}

IDIO_DEFINE_PRIMITIVE2_DS ("eq?", eqp, (IDIO o1, IDIO o2), "o1 o2", "\
test if `o1` and `o2` are indistinguishable in memory	\n\
						\n\
:param o1: object to test			\n\
:type o1: any					\n\
:param o2: object to test			\n\
:type o2: any					\n\
:return: ``#t`` if `o1` is eq? to `o2`, ``#f`` otherwise\n\
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
test if `o1` and `o2` are equivalent		\n\
						\n\
Numbers and strings can be different in memory	\n\
but have the same value.			\n\
						\n\
:param o1: object to test			\n\
:type o1: any					\n\
:param o2: object to test			\n\
:type o2: any					\n\
:return: ``#t`` if `o1` is eqv? to `o2`, ``#f`` otherwise\n\
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
test if `o1` and `o2` are semantically the same	\n\
						\n\
R5RS suggests that the values *print* the same.	\n\
						\n\
:param o1: object to test			\n\
:type o1: any					\n\
:param o2: object to test			\n\
:type o2: any					\n\
:return: ``#t`` if `o1` is equal? to `o2`, ``#f`` otherwise\n\
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

int idio_equal (IDIO o1, IDIO o2, idio_equal_enum eqp)
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
		r = idio_vm_invoke_C (IDIO_LIST3 (idio_module_symbol_value (idio_S_num_eq,
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
			r = idio_vm_invoke_C (IDIO_LIST3 (idio_module_symbol_value (idio_S_num_eq,
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

		/*
		 * string flags is a bit squirrelly.  We've tacked
		 * pathnames on the back of strings but strings can
		 * have different widths and be The Same.
		 *
		 * So really we want to verify that if one is a
		 * pathname then the other is too.
		 */
		if (((IDIO_STRING_FLAGS (o1) & IDIO_STRING_FLAG_OCTET) !=
		     (IDIO_STRING_FLAGS (o2) & IDIO_STRING_FLAG_OCTET)) ||
		    ((IDIO_STRING_FLAGS (o1) & IDIO_STRING_FLAG_PATHNAME) !=
		     (IDIO_STRING_FLAGS (o2) & IDIO_STRING_FLAG_PATHNAME)) ||
		    ((IDIO_STRING_FLAGS (o1) & IDIO_STRING_FLAG_FD_PATHNAME) !=
		     (IDIO_STRING_FLAGS (o2) & IDIO_STRING_FLAG_FD_PATHNAME)) ||
		    ((IDIO_STRING_FLAGS (o1) & IDIO_STRING_FLAG_FIFO_PATHNAME) !=
		     (IDIO_STRING_FLAGS (o2) & IDIO_STRING_FLAG_FIFO_PATHNAME))) {
		    /*
		     * Code coverage:
		     *
		     * eq? "hello" %P"hello"
		     */
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
	    case IDIO_TYPE_C_CHAR:
		return (IDIO_C_TYPE_char (o1) == IDIO_C_TYPE_char (o2));
	    case IDIO_TYPE_C_SCHAR:
		return (IDIO_C_TYPE_schar (o1) == IDIO_C_TYPE_schar (o2));
	    case IDIO_TYPE_C_UCHAR:
		return (IDIO_C_TYPE_uchar (o1) == IDIO_C_TYPE_uchar (o2));
	    case IDIO_TYPE_C_SHORT:
		return (IDIO_C_TYPE_short (o1) == IDIO_C_TYPE_short (o2));
	    case IDIO_TYPE_C_USHORT:
		return (IDIO_C_TYPE_ushort (o1) == IDIO_C_TYPE_ushort (o2));
	    case IDIO_TYPE_C_INT:
		return (IDIO_C_TYPE_int (o1) == IDIO_C_TYPE_int (o2));
	    case IDIO_TYPE_C_UINT:
		return (IDIO_C_TYPE_uint (o1) == IDIO_C_TYPE_uint (o2));
	    case IDIO_TYPE_C_LONG:
		return (IDIO_C_TYPE_long (o1) == IDIO_C_TYPE_long (o2));
	    case IDIO_TYPE_C_ULONG:
		return (IDIO_C_TYPE_ulong (o1) == IDIO_C_TYPE_ulong (o2));
	    case IDIO_TYPE_C_LONGLONG:
		return (IDIO_C_TYPE_longlong (o1) == IDIO_C_TYPE_longlong (o2));
	    case IDIO_TYPE_C_ULONGLONG:
		return (IDIO_C_TYPE_ulonglong (o1) == IDIO_C_TYPE_ulonglong (o2));
	    case IDIO_TYPE_C_FLOAT:
		return (idio_C_float_C_eq_ULP(IDIO_C_TYPE_float (o1), IDIO_C_TYPE_float (o2), 1));
	    case IDIO_TYPE_C_DOUBLE:
		return (idio_C_double_C_eq_ULP (IDIO_C_TYPE_double (o1), IDIO_C_TYPE_double (o2), 1));
	    case IDIO_TYPE_C_LONGDOUBLE:
		/*
		 * Test Case: util-errors/equal-c-long-double.idio
		 *
		 * equal? (C/number-> 1.0 'longdouble) (C/number-> 2.0 'longdouble)
		 */
		idio_error_param_type_msg ("equality for C long double is not supported", IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return 0;
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
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_coding_error_C ("IDIO_TYPE_POINTER_MARK: o1->type unexpected", o1, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return 0;
	    }
	}
	break;
    default:
	/* inconceivable! */
	idio_coding_error_C ("o1->type unexpected", o1, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    return 1;
}

/*
 * A significant annoyance is that printing functions recurse which
 * means that any idio-print-conversion-format which was valid for the
 * initial call is now invalid for the recursed variants -- which
 * should use a default (%s, usually or %d %u %g %p as below).
 *
 * We can toggle ipcf with a flag.
 */
idio_unicode_t idio_util_string_format (idio_unicode_t format)
{
    IDIO ipcf = idio_S_false;

    if (idio_S_nil != idio_print_conversion_format_sym) {
	ipcf = idio_module_symbol_value (idio_print_conversion_format_sym,
					 idio_Idio_module,
					 IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcf) {
	    if (! idio_isa_unicode (ipcf)) {
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
		size_t size = 0;
		char *ipcf_C = idio_as_string (ipcf, &size, 1, idio_S_nil, 0);
		fprintf (stderr, "ipcf isa %s %s\n", idio_type2string (ipcf), ipcf_C);
		idio_gc_free (ipcf_C, size);

		idio_error_param_value_msg_only ("idio_util_string_format", "idio-print-conversion-format", "should be unicode", IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return -1;
	    }

	    format = IDIO_UNICODE_VAL (ipcf);
	}
    }

    return format;
}

/*
  Scheme-ish write -- internal representation (where appropriate)
  suitable for (read).  Primarily:

  UNICODE #\a:		#\a
  UNICODE #\ħ:		#U+0127		; if fails isgraph()
  STRING "foo":		"foo"

  A significant annoyance is that this function recurses which means
  that any idio-print-conversion-format which was valid for the
  initial call is now invalid for the recursed variants.

  We can escape that with a first tag.
 */
char *idio_as_string (IDIO o, size_t *sizep, int depth, IDIO seen, int first)
{
    char *r = NULL;

    IDIO_C_ASSERT (depth >= -10000);

    idio_unicode_t format = IDIO_PRINT_CONVERSION_FORMAT_s;
    switch (idio_type (o)) {
    case IDIO_TYPE_FIXNUM:		format = IDIO_PRINT_CONVERSION_FORMAT_d; break;
    case IDIO_TYPE_C_CHAR:		format = IDIO_PRINT_CONVERSION_FORMAT_d; break;
    case IDIO_TYPE_C_SCHAR:		format = IDIO_PRINT_CONVERSION_FORMAT_d; break;
    case IDIO_TYPE_C_UCHAR:		format = IDIO_PRINT_CONVERSION_FORMAT_u; break;
    case IDIO_TYPE_C_SHORT:		format = IDIO_PRINT_CONVERSION_FORMAT_d; break;
    case IDIO_TYPE_C_USHORT:		format = IDIO_PRINT_CONVERSION_FORMAT_u; break;
    case IDIO_TYPE_C_INT:		format = IDIO_PRINT_CONVERSION_FORMAT_d; break;
    case IDIO_TYPE_C_UINT:		format = IDIO_PRINT_CONVERSION_FORMAT_u; break;
    case IDIO_TYPE_C_LONG:		format = IDIO_PRINT_CONVERSION_FORMAT_d; break;
    case IDIO_TYPE_C_ULONG:		format = IDIO_PRINT_CONVERSION_FORMAT_u; break;
    case IDIO_TYPE_C_LONGLONG:		format = IDIO_PRINT_CONVERSION_FORMAT_d; break;
    case IDIO_TYPE_C_ULONGLONG:		format = IDIO_PRINT_CONVERSION_FORMAT_u; break;
    case IDIO_TYPE_C_FLOAT:		format = IDIO_PRINT_CONVERSION_FORMAT_g; break;
    case IDIO_TYPE_C_DOUBLE:		format = IDIO_PRINT_CONVERSION_FORMAT_g; break;
    case IDIO_TYPE_C_LONGDOUBLE:	format = IDIO_PRINT_CONVERSION_FORMAT_g; break;
    case IDIO_TYPE_C_POINTER:		format = IDIO_PRINT_CONVERSION_FORMAT_p; break;
    }

    IDIO ipcf = idio_S_false;

    if (first &&
	idio_S_nil != idio_print_conversion_format_sym) {
	ipcf = idio_module_symbol_value (idio_print_conversion_format_sym,
					 idio_Idio_module,
					 IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcf) {
	    if (! idio_isa_unicode (ipcf)) {
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
		size_t size = 0;
		char *ipcf_C = idio_as_string (ipcf, &size, 1, seen, 0);
		fprintf (stderr, "ipcf isa %s %s\n", idio_type2string (ipcf), ipcf_C);
		idio_gc_free (ipcf_C, size);

		idio_error_param_value_msg_only ("idio_as_string", "idio-print-conversion-format", "should be unicode", IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }

	    format = IDIO_UNICODE_VAL (ipcf);
	}
    }

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	{
	    r = idio_fixnum_as_C_string (o, sizep, format, seen, depth);
	    break;
	}
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		/* util.c */
		r = idio_constant_idio_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
		/* read.c */
		r = idio_constant_token_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		/* codegen.c */
		r = idio_constant_i_code_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		/* unicode.c */
		r = idio_constant_unicode_as_C_string (o, sizep, format, seen, depth);
		break;
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.  There should be a case clause above.
		 */
		*sizep = idio_asprintf (&r, "#<type/constant/?? o=%10p VAL(o)=%#x>", o, IDIO_CONSTANT_IDIO_VAL (o));
		idio_error_warning_message ("unhandled constant type: %s\n", r);
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
	*sizep = idio_asprintf (&r, "#<type/placecholder?? %10p>", o);
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    idio_type_e type = idio_type (o);

	    if (depth <= 0) {
		*sizep = idio_asprintf (&r, "..");
		return r;
	    }

	    if (idio_S_false != idio_list_memq (o, seen)) {
		*sizep = idio_asprintf (&r, "#<^{%s@%p}>", idio_type2string (o), o);
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
		*sizep = idio_asprintf (&r, "#<NONE!! -none- %10p>", o);
		break;
	    case IDIO_TYPE_STRING:
	    case IDIO_TYPE_SUBSTRING:
		r = idio_string_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_SYMBOL:
		r = idio_symbol_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_KEYWORD:
		r = idio_keyword_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_PAIR:
		r = idio_pair_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_ARRAY:
		r = idio_array_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_HASH:
		r = idio_hash_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CLOSURE:
		r = idio_closure_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_PRIMITIVE:
		r = idio_primitive_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_BIGNUM:
		r = idio_bignum_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_MODULE:
		r = idio_module_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_FRAME:
		r = idio_frame_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_HANDLE:
		r = idio_handle_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_C_CHAR:
		r = idio_C_char_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_C_SCHAR:
	    case IDIO_TYPE_C_UCHAR:
	    case IDIO_TYPE_C_SHORT:
	    case IDIO_TYPE_C_USHORT:
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
	    case IDIO_TYPE_C_LONG:
	    case IDIO_TYPE_C_ULONG:
	    case IDIO_TYPE_C_LONGLONG:
	    case IDIO_TYPE_C_ULONGLONG:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_LONGDOUBLE:
		r = idio_C_number_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_C_POINTER:
		r = idio_C_pointer_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		r = idio_struct_type_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		r = idio_struct_instance_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_THREAD:
		r = idio_thread_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONTINUATION:
		r = idio_continuation_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_BITSET:
		r = idio_bitset_as_C_string (o, sizep, format, seen, depth);
		break;
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
		     *
		     * strnlen rather than idio_strnlen as we're
		     * already teetering and don't need a distracting
		     * (virtually certain) condition being raised.
		     */
		    size_t n = strnlen ((char *) o, 40);
		    if (40 == n) {
			*sizep = idio_asprintf (&r, "#<void?? %10p>", o);
		    } else {
			*sizep = idio_asprintf (&r, "#<string?? \"%s\">", (char *) o);
		    }
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
	*sizep = idio_asprintf (&r, "#<type?? %10p>", o);
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
 * Scheme-ish display -- no internal representation (where
 * appropriate).  Unsuitable for (read).  Primarily:
 *
 * UNICODE #\a:		a
 * UNICODE #U+FFFD	�
 * STRING "foo":	foo
 *
 * Most non-data types will still come out as some internal
 * representation.  (Still unsuitable for (read) as it doesn't know
 * about them.)
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
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		{
		    idio_unicode_t u = IDIO_UNICODE_VAL (o);
		    if (idio_unicode_valid_code_point (u)) {
			if (u >= 0x10000) {
			    *sizep = idio_asprintf (&r, "%c%c%c%c",
						    0xf0 | ((u & (0x07 << 18)) >> 18),
						    0x80 | ((u & (0x3f << 12)) >> 12),
						    0x80 | ((u & (0x3f << 6)) >> 6),
						    0x80 | ((u & (0x3f << 0)) >> 0));
			} else if (u >= 0x0800) {
			    *sizep = idio_asprintf (&r, "%c%c%c",
						    0xe0 | ((u & (0x0f << 12)) >> 12),
						    0x80 | ((u & (0x3f << 6)) >> 6),
						    0x80 | ((u & (0x3f << 0)) >> 0));
			} else if (u >= 0x0080) {
			    *sizep = idio_asprintf (&r, "%c%c",
						    0xc0 | ((u & (0x1f << 6)) >> 6),
						    0x80 | ((u & (0x3f << 0)) >> 0));
			} else {
			    *sizep = idio_asprintf (&r, "%c",
						    u & 0x7f);
			}
		    } else {
			/*
			 * Test Case: ??
			 *
			 * Coding error.
			 */
			/*
			 * Hopefully, this is guarded against elsewhere
			 */
			/*
			 * XXX do not pass {o} to idio_error_*(),
			 * here, as the engine will try to print out
			 * its value which will bring us back here...
			 */
			char em[30];
			snprintf (em, 30, "code point #U+%X", u);
			idio_error_param_value_msg ("idio_display_string", em, idio_S_nil, "invalid", IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }
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
		    return idio_utf8_string (o, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED, IDIO_UTF8_STRING_USEPREC);
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		{
		    return idio_utf8_string (o, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED, IDIO_UTF8_STRING_USEPREC);
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
	*sizep = idio_asprintf (&r, "type %d n/k", o->type);
	break;
    }

    return r;
}

/*
 * reporting string for use in, uh, reports
 *
 * here we want to use the default string of "simple" values and a
 * reduced string for complex values that might call a printer (which
 * messes up any report)
 */
char *idio_report_string (IDIO o, size_t *sizep, int depth, IDIO seen, int first)
{
    char *r = NULL;

    IDIO_C_ASSERT (depth >= -10000);

    idio_unicode_t format = IDIO_PRINT_CONVERSION_FORMAT_s;

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	{
	    r = idio_fixnum_as_C_string (o, sizep, format, seen, depth);
	    break;
	}
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		/* util.c */
		r = idio_constant_idio_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
		/* read.c */
		r = idio_constant_token_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		/* codegen.c */
		r = idio_constant_i_code_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		/* unicode.c */
		r = idio_constant_unicode_as_C_string (o, sizep, format, seen, depth);
		break;
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.  There should be a case clause above.
		 */
		*sizep = idio_asprintf (&r, "#<type/constant/?? o=%10p VAL(o)=%#x>", o, IDIO_CONSTANT_IDIO_VAL (o));
		idio_error_warning_message ("unhandled constant type: %s\n", r);
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
	*sizep = idio_asprintf (&r, "#<type/placecholder?? %10p>", o);
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    idio_type_e type = idio_type (o);

	    if (depth <= 0) {
		*sizep = idio_asprintf (&r, "..");
		return r;
	    }

	    if (idio_S_false != idio_list_memq (o, seen)) {
		*sizep = idio_asprintf (&r, "#<^{%s@%p}>", idio_type2string (o), o);
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
		*sizep = idio_asprintf (&r, "#<NONE!! -none- %10p>", o);
		break;
	    case IDIO_TYPE_STRING:
	    case IDIO_TYPE_SUBSTRING:
		r = idio_string_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_SYMBOL:
		r = idio_symbol_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_KEYWORD:
		r = idio_keyword_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_PAIR:
		r = idio_pair_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_ARRAY:
		r = idio_array_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_HASH:
		r = idio_hash_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CLOSURE:
		r = idio_closure_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_PRIMITIVE:
		r = idio_primitive_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_BIGNUM:
		r = idio_bignum_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_MODULE:
		r = idio_module_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_FRAME:
		r = idio_frame_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_HANDLE:
		r = idio_handle_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_C_CHAR:
		r = idio_C_char_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_C_SCHAR:
	    case IDIO_TYPE_C_UCHAR:
	    case IDIO_TYPE_C_SHORT:
	    case IDIO_TYPE_C_USHORT:
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
	    case IDIO_TYPE_C_LONG:
	    case IDIO_TYPE_C_ULONG:
	    case IDIO_TYPE_C_LONGLONG:
	    case IDIO_TYPE_C_ULONGLONG:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_LONGDOUBLE:
		r = idio_C_number_as_C_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_C_POINTER:
		r = idio_C_pointer_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		r = idio_struct_type_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		r = idio_struct_instance_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_THREAD:
		r = idio_thread_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_CONTINUATION:
		r = idio_continuation_report_string (o, sizep, format, seen, depth);
		break;
	    case IDIO_TYPE_BITSET:
		r = idio_bitset_report_string (o, sizep, format, seen, depth);
		break;
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
		     *
		     * strnlen rather than idio_strnlen as we're
		     * already teetering and don't need a distracting
		     * (virtually certain) condition being raised.
		     */
		    size_t n = strnlen ((char *) o, 40);
		    if (40 == n) {
			*sizep = idio_asprintf (&r, "#<void?? %10p>", o);
		    } else {
			*sizep = idio_asprintf (&r, "#<string?? \"%s\">", (char *) o);
		    }
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
	*sizep = idio_asprintf (&r, "#<type?? %10p>", o);
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

    idio_gc_free (str, size);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("string", string, (IDIO o), "o", "\
convert `o` to a string				\n\
						\n\
:param o: object to convert			\n\
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
:return: a string representation of `o`		\n\
						\n\
``->string`` differs from :ref:`string <string>`	\n\
in that it won't stringify a string!		\n\
")
{
    IDIO_ASSERT (o);

    idio_vtable_method_t *m = idio_vtable_lookup_method (idio_value_vtable (o), o, idio_S_2string, 1);

    size_t size = 0;

    return IDIO_VTABLE_METHOD_FUNC (m) (m, o, &size, idio_S_nil, 40, 1);
}

/*
 * The generic ->string vtable method simply calls the old
 * idio_util_string() function.
 */
IDIO idio_util_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    return idio_util_string (v);
}

/*
 * NB %format calls display-string
 */
IDIO_DEFINE_PRIMITIVE1_DS ("display-string", display_string, (IDIO o), "o", "\
convert `o` to a display string			\n\
						\n\
:param o: object to convert			\n\
:return: a string representation of `o`		\n\
")
{
    IDIO_ASSERT (o);

    /*
     * lookup ->display-string or ->string as a fallback
     */
    idio_vtable_t *vt = idio_value_vtable (o);
    idio_vtable_method_t *m = idio_vtable_lookup_method (vt, o, idio_S_2display_string, 0);

    int inherit = 0;
    if (NULL == m) {
	/*
	 * NB don't throw on the lookup of ->string as, technically,
	 * this is a call for ->display-string
	 *
	 * Although the error is still confusing.
	 */
	m = idio_vtable_lookup_method (vt, o, idio_S_2string, 0);

	if (NULL == m) {
	    idio_debug ("o ->string from ->display-string for %s\n", o);
	    fprintf (stderr, "which is a %s with vtable %p\n", idio_type2string (o), vt);
	    idio_dump_vtable (vt);
	    /*
	     * Test Case: how did we lose ->string??
	     */

	    /*
	     * NB raise the condition for the original method name, ->display-string
	     */
	    idio_vtable_method_unbound_error (o, idio_S_2display_string, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	inherit = 1;
    }

    size_t size = 0;

    IDIO s = IDIO_VTABLE_METHOD_FUNC (m) (m, o, &size, idio_S_nil, 40, 1);

    if (idio_isa_string (s)) {
	if (inherit) {
	    /*
	     * Don't "inherit" the ->string method as our ->display-string
	     * method (avoiding a missed lookup for ->display-string next
	     * time) until we're sure the method is sound.
	     *
	     * Otherwise we'll have mistakenly inherited a broken method
	     * under a different name that won't be changed until a
	     * generational change occurs.
	     */
	    idio_vtable_inherit_method (vt, idio_S_2display_string, m);
	}

	return s;
    } else if (0 == idio_vm_reporting) {
	/*
	 * Test Case: util-errors/C-pointer-printer-bad-return-type.idio
	 *
	 * ... return #t
	 */
#ifdef IDIO_DEBUG
	idio_debug ("method printer => %s (not a STRING)\n", s);
#endif
	idio_error_param_value_msg ("display-string", "method printer", s, "should return a string", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    return s;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%%add-as-string", add_as_string, (IDIO o, IDIO f), "o f", "\
add `f` as a printer for instances of `o`	\n\
						\n\
:param o: object (type) to be printed		\n\
:param f: printer				\n\
:type f: function				\n\
						\n\
`f` will be invoked with the value and ``#n``	\n\
						\n\
valid object/object types are:			\n\
struct type					\n\
struct instance					\n\
C/pointer (with CSI)				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (f);

    IDIO_USER_TYPE_ASSERT (function, f);

    /*
     * The nominal method
     */
    IDIO m_name = idio_S_2string;

    int type = idio_type (o);
    switch (type) {
    case IDIO_TYPE_STRUCT_TYPE:
    case IDIO_TYPE_STRUCT_INSTANCE:
	m_name = idio_S_struct_instance_2string;
	break;
    }

    idio_vtable_add_method (idio_value_vtable (o),
			    m_name,
			    idio_vtable_create_method_value (idio_util_method_run,
							     IDIO_LIST2 (f, idio_S_nil)));

    return idio_S_unspec;
}

IDIO idio_util_method_value_index (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    IDIO member = va_arg (ap, IDIO);
    va_end (ap);

    IDIO_ASSERT (member);

    IDIO func = (IDIO) IDIO_VTABLE_METHOD_DATA (m);

    if (idio_isa_function (func) == 0) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_msg_only ("value-index", "method->data", "should be a function", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO cmd = IDIO_LIST3 (func, v, member);

    IDIO r = idio_vm_invoke_C (cmd);

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("value-index", value_index, (IDIO o, IDIO i), "o i", "\
if `i` is a function then invoke (`i` `o`)	\n\
						\n\
otherwise index the object `o` by `i`		\n\
						\n\
:param o: object to index			\n\
:type o: any					\n\
:param i: index					\n\
:type i: any					\n\
:return: the indexed value			\n\
:raises ^rt-parameter-value-error:		\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (i);

    if (idio_isa_function (i)) {
	IDIO cmd = IDIO_LIST2 (i, o);

	IDIO r = idio_vm_invoke_C (cmd);

	return r;
    }

    idio_vtable_method_t *m = idio_vtable_lookup_method (idio_value_vtable (o), o, idio_S_value_index, 0);

    if (NULL == m) {
	/*
	 * Test Case: util-errors/value-index-bad-type.idio
	 *
	 * 1 . 2
	 */
	idio_error_param_value_msg ("value-index", "value", o, "is non-indexable", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_VTABLE_METHOD_FUNC (m) (m, o, i);
}

IDIO idio_util_method_set_value_index (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    IDIO member = va_arg (ap, IDIO);
    IDIO value = va_arg (ap, IDIO);
    va_end (ap);

    IDIO_ASSERT (member);

    IDIO func = (IDIO) IDIO_VTABLE_METHOD_DATA (m);

    if (idio_isa_function (func) == 0) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_msg_only ("set_value-index!", "method->data", "should be a function", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO cmd = IDIO_LIST4 (func, v, member, value);

    IDIO r = idio_vm_invoke_C (cmd);

    return r;
}

IDIO_DEFINE_PRIMITIVE3_DS ("set-value-index!", set_value_index, (IDIO o, IDIO i, IDIO v), "o i v", "\
set value of the object `o` indexed by `i` to `v`	\n\
						\n\
:param o: object to index			\n\
:type o: any					\n\
:param i: index					\n\
:type i: any					\n\
:param v: value					\n\
:type v: any					\n\
:return: the indexed value			\n\
:raises ^rt-parameter-value-error:		\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    /*
     * Strictly speaking we should be looking up the setter of the
     * getter but we can fall back to the set-value-index! method (if
     * defined).
     */

    idio_vtable_method_t *get_m = idio_vtable_lookup_method (idio_value_vtable (o), o, idio_S_value_index, 0);

    if (NULL != get_m) {
	IDIO get_func = (IDIO) IDIO_VTABLE_METHOD_DATA (get_m);

	/*
	 * We want: (setter get_func) o i v
	 *
	 * but we have to invoke by stage:
	 *
	 * XXX dirty hack alert!
	 *
	 * If we call (setter get_func) and get_func doesn't have a
	 * setter then we'll get a condition raised.  We don't want
	 * that because we have a fallback position.
	 *
	 * It transpires that the implementation of (setter get_func)
	 * is idio_ref_property (get_func, idio_KW_setter, [def]) for
	 * which the default {def} isn't set, hence the condition.
	 *
	 * So, we can pass a {def} and not get an error.
	 *
	 * Let's hope no-one redefines (setter X)...
	 */
	IDIO setter_func = idio_ref_property (get_func, idio_KW_setter, IDIO_LIST1 (idio_S_false));

	if (idio_isa_function (setter_func)) {
	    IDIO set_cmd = IDIO_LIST4 (setter_func, o, i, v);

	    IDIO set_r = idio_vm_invoke_C (set_cmd);

	    return set_r;
	}
    }

    /*
     * Fallback position for values with no setter
     */
    idio_vtable_method_t *set_m = idio_vtable_lookup_method (idio_value_vtable (o), o, idio_S_set_value_index, 0);

    if (NULL == set_m) {
	/*
	 * Test Case: util-errors/set-value-index-bad-type.idio
	 *
	 * 1 . 2 = 3
	 */
	idio_error_param_value_msg ("set-value-index!", "value", o, "is non-indexable", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_VTABLE_METHOD_FUNC (set_m) (set_m, o, i, v);
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
	idio_coding_error_C ("invalid type", o, IDIO_C_FUNC_LOCATION_S ("placeholder"));

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
		if (idio_isa_instance (o)) {
		    return o;
		} else if (idio_struct_instance_isa (o, idio_path_type) ||
			   idio_struct_instance_isa (o, idio_lexobj_type) ||
			   idio_struct_instance_isa (o, idio_evaluate_eenv_type)) {
		    return idio_struct_instance_copy (o);
		} else {
		    /*
		     * Test Case: util-errors/copy-value-bad-struct-instance.idio
		     *
		     * si := make-struct-instance (define-struct foo x y) 1 2
		     * copy-value si
		     */
#ifdef IDIO_DEBUG
		    idio_debug ("idio_copy: unexpected ST: %s\n", o);
#endif
		    idio_error_param_value_msg ("copy-value", "struct instance", o, "not of a valid struct type", IDIO_C_FUNC_LOCATION ());

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
	    case IDIO_TYPE_C_CHAR:
	    case IDIO_TYPE_C_SCHAR:
	    case IDIO_TYPE_C_UCHAR:
	    case IDIO_TYPE_C_SHORT:
	    case IDIO_TYPE_C_USHORT:
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
	    case IDIO_TYPE_C_LONG:
	    case IDIO_TYPE_C_ULONG:
	    case IDIO_TYPE_C_LONGLONG:
	    case IDIO_TYPE_C_ULONGLONG:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_LONGDOUBLE:
	    case IDIO_TYPE_C_POINTER:
		/*
		 * Test Case: util-errors/copy-value-bad-type.idio
		 *
		 * copy-value list
		 */
#ifdef IDIO_DEBUG
		fprintf (stderr, "idio-copy: cannot copy a %s\n", idio_type2string (o));
		idio_debug ("%s\n", o);
#endif
		idio_error_param_value_msg ("copy-value", "value", o, "invalid type", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_coding_error_C ("unimplemented type", o, IDIO_C_FUNC_LOCATION ());

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
    idio_coding_error_C ("failed to copy", o, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("copy-value", copy_value, (IDIO v, IDIO args), "v [depth]", "\
copy `v` to `depth`					\n\
							\n\
:param v: value to copy					\n\
:type v: any						\n\
:param depth: ``'shallow`` or ``'deep`` (default)	\n\
:type depth: symbol, optional				\n\
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

    if (idio_isa_pair (args)) {
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
		idio_error_param_value_msg ("copy-value", "depth", idepth, "should be 'deep or 'shallow", IDIO_C_FUNC_LOCATION ());

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
		    fprintf (stderr, "size=%zd/%zd \n", IDIO_ARRAY_USIZE (o), IDIO_ARRAY_ASIZE (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			    if (idio_S_nil != IDIO_ARRAY_AE (o, i) ||
				detail > 3) {
				size_t size = 0;
				char *s = idio_as_string_safe (IDIO_ARRAY_AE (o, i), &size, 4, 1);
				fprintf (stderr, "\t%3zu: %10p %10s\n", i, IDIO_ARRAY_AE (o, i), s);

				idio_gc_free (s, size);
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
					idio_gc_free (s, size);
				    }
				    if (IDIO_HASH_HE_VALUE (he)) {
					size = 0;
					s = idio_as_string_safe (IDIO_HASH_HE_VALUE (he), &size, 4, 1);
				    } else {
					size = idio_asprintf (&s, "-");
				    }
				    fprintf (stderr, "%-10s\n", s);

				    idio_gc_free (s, size);
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
	    case IDIO_TYPE_C_CHAR:
	    case IDIO_TYPE_C_SCHAR:
	    case IDIO_TYPE_C_UCHAR:
	    case IDIO_TYPE_C_SHORT:
	    case IDIO_TYPE_C_USHORT:
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
	    case IDIO_TYPE_C_LONG:
	    case IDIO_TYPE_C_ULONG:
	    case IDIO_TYPE_C_LONGLONG:
	    case IDIO_TYPE_C_ULONGLONG:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_LONGDOUBLE:
	    case IDIO_TYPE_C_POINTER:
		break;
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_coding_error_C ("unimplemented type", o, IDIO_C_FUNC_LOCATION ());

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
print the internal details of `o` to *stderr*		\n\
							\n\
:param o: value to dump					\n\
:type o: any						\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (o);

    idio_dump (o, 16);

    return idio_S_unspec;
}

void idio_debug_FILE (FILE *file, char const *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    /* fprintf (file, "[%d]", getpid()); */

    size_t size = 0;
    char *os = idio_as_string_safe (o, &size, 40, 1);
    fprintf (file, fmt, os);

    idio_gc_free (os, size);
}

void idio_debug (char const *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    idio_debug_FILE (stderr, fmt, o);
}

IDIO_DEFINE_PRIMITIVE2_DS ("idio-debug", idio_debug, (IDIO fmt, IDIO o), "fmt o", "\
print the string value of `o` according to `fmt` to	\n\
*stderr*						\n\
							\n\
There must be a single %s conversion specification	\n\
							\n\
idio-debug \"foo is %20s\n\" foo			\n\
							\n\
:param fmt: :manpage:`printf(3)` format string		\n\
:type fmt: string					\n\
:param o: value to dump					\n\
:type o: any						\n\
:return: ``#<unspec>``					\n\
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

    /*
     * Use size + 1 to avoid a truncation warning -- we're just seeing
     * if sfmt includes a NUL
     */
    size_t C_size = idio_strnlen (sfmt, size + 1);
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
	idio_gc_free (sfmt, size);

	idio_error_param_value_msg ("idio-debug", "fmt", fmt, "contains an ASCII NUL", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_debug (sfmt, o);

    idio_gc_free (sfmt, size);

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

    /*
     * Not Art but SRFI-0 has done the grep for us
     */
    if (idio_S_virtualisation_WSL == f) {
	idio_vm_virtualisation_WSL = 1;
    }

    IDIO fs = idio_module_symbol_value (idio_features, idio_Idio_module, idio_S_nil);

    return idio_module_set_symbol_value (idio_features, idio_pair (f, fs), idio_Idio_module);
}

IDIO_DEFINE_PRIMITIVE1_DS ("%add-feature", add_feature, (IDIO f), "f", "\
add feature `f` to Idio features			\n\
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

IDIO idio_add_feature_ps (char const *p, size_t const plen, char const *s, size_t const slen)
{
    IDIO_C_ASSERT (p);
    IDIO_C_ASSERT (s);

    size_t buflen = plen + slen + 1;
    char *buf;
    IDIO_GC_ALLOC (buf, buflen);
    size_t blen = idio_snprintf (buf, buflen, "%s%s", p, s);
    IDIO r = idio_add_feature (idio_string_C_len (buf, blen));

    IDIO_GC_FREE (buf, buflen);

    return r;
}

IDIO idio_add_feature_pi (char const *p, size_t const plen, size_t const size)
{
    IDIO_C_ASSERT (p);

    size_t buflen = plen + 20;
    char *buf;
    IDIO_GC_ALLOC (buf, buflen);
    size_t blen = idio_snprintf (buf, buflen, "%s%zu", p, size);
    IDIO r = idio_add_feature (idio_symbols_C_intern (buf, blen));

    IDIO_GC_FREE (buf, buflen);

    return r;
}

IDIO idio_add_module_feature (IDIO m, IDIO f)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (f);

    IDIO_TYPE_ASSERT (module, m);

    IDIO mname = IDIO_MODULE_NAME (m);
    IDIO_TYPE_ASSERT (symbol, mname);

    size_t mlen = IDIO_SYMBOL_BLEN (mname);
    IDIO r = idio_S_nil;

    if (idio_isa_symbol (f)) {
	size_t flen = IDIO_SYMBOL_BLEN (f);

	size_t buflen = mlen + 1 + flen + 1;
	char *buf;
	IDIO_GC_ALLOC (buf, buflen);
	size_t blen = idio_snprintf (buf, buflen, "%s/%s", IDIO_SYMBOL_S (mname), IDIO_SYMBOL_S (f));
	r = idio_add_feature (idio_symbols_C_intern (buf, blen));

	IDIO_GC_FREE (buf, buflen);
    } else if (idio_isa_string (f)) {
	size_t flen = 0;
	char *C_f = idio_string_as_C (f, &flen);

	size_t buflen = 1 + mlen + 1 + flen + 1 + 1;
	char *buf;
	IDIO_GC_ALLOC (buf, buflen);
	size_t blen = idio_snprintf (buf, buflen, "\"%s/%s\"", IDIO_SYMBOL_S (mname), C_f);
	r = idio_add_feature (idio_string_C_len (buf, blen));

	IDIO_GC_FREE (C_f, flen);
	IDIO_GC_FREE (buf, buflen);
    } else {
	/*
	 * Test Case: ??
	 *
	 * The user interface is protected so this would be a coding
	 * error.
	 */
	idio_error_param_type ("symbol|string",  f, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return r;
}

#if ! defined (IDIO_HAVE_STRNLEN)
/*
 * strnlen is missing up to at least Mac OS X 10.5.8 -- at some later
 * point strnlen was added
 */
size_t strnlen (char const *s, size_t maxlen)
{
    size_t n = 0;
    while (*s++ &&
	   n < maxlen) {
	n++;
    }

    return n;
}
#endif

#if ! defined (IDIO_HAVE_MEMRCHR)
/*
 * SunOS / Mac OS X
 */
void *memrchr (void const *s, int const c, size_t n)
{
    char const *ss = s;
    char cc = (char) c;

    while (n--) {
	if (cc == ss[n]) {
	    return (void *) (ss + n);
	}
    }

    return NULL;
}
#endif

#if ! defined (IDIO_HAVE_CLOCK_GETTIME)
#ifdef __MACH__
/*
 * Originally https://gist.github.com/jbenet/1087739 from
 * https://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x
 *
 * Also https://dshil.github.io/blog/missed-os-x-clock-guide/ which
 * leads to https://dshil.github.io/blog/missed-os-x-clock-guide/
 *
 * None of the functions have man pages...
 */
#include <mach/clock.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sched.h>

static mach_timebase_info_data_t __clock_gettime_inf;

int clock_gettime (clockid_t clk_id, struct timespec *tp)
{
    clock_serv_t cs;
    mach_timespec_t mts;

    clockid_t clk_serv_id;

    uint64_t start, end, delta, nano;

    int retval = -1;
    int ret;

    switch (clk_id) {
    case CLOCK_REALTIME:
    case CLOCK_MONOTONIC:
	clk_serv_id = (clk_id == CLOCK_REALTIME ? CALENDAR_CLOCK : SYSTEM_CLOCK);
	ret = host_get_clock_service (mach_host_self (), clk_serv_id, &cs);
	if (0 == ret) {
	    ret = clock_get_time (cs, &mts);
	    if (0 == ret) {
		tp->tv_sec = mts.tv_sec;
		tp->tv_nsec = mts.tv_nsec;
		retval = 0;
	    }
	}
	mach_port_deallocate (mach_task_self (), cs);
	if (ret) {
	    errno = EINVAL;
	}
	break;

    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
	start = mach_absolute_time ();
	if (clk_id == CLOCK_PROCESS_CPUTIME_ID) {
	    getpid ();
	} else {
	    sched_yield ();
	}
	end = mach_absolute_time ();
	delta = end - start;
	if (0 == __clock_gettime_inf.denom) {
	    mach_timebase_info (&__clock_gettime_inf);
	}
	nano = delta * __clock_gettime_inf.numer / __clock_gettime_inf.denom;
	tp->tv_sec = nano * 1e-9;
	tp->tv_nsec = nano - (tp->tv_sec * 1e9);
	retval = 0;
	break;

    default:
	errno = EINVAL;
	break;
    }

    return retval;
}
#endif
#endif

/*
 * idio_strnlen is strnlen with an idio_error_* clause
 */
size_t idio_strnlen (char const *s, size_t const maxlen)
{
    size_t n = 0;
    while (*s++ &&
	   n < maxlen) {
	n++;
    }

    if (maxlen == n) {
	/*
	 * As we're including the input string in our helpful error
	 * message then we don't want BUFSIZ chars filling the screen
	 * so limit the entire error message to 80 bytes.
	 */
	char em[81];
	snprintf (em, 80, "idio_strnlen: truncated to %zd: \"%s\"", maxlen, s);
	em[80] = '\0';

	idio_string_error_C (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    return n;
}

/*
 * idio_snprintf is snprintf with an idio_error_* clause
 */
int idio_snprintf (char *str, size_t const size, char const *format, ...)
{
    IDIO_C_ASSERT (str);
    IDIO_C_ASSERT (size > 0);
    IDIO_C_ASSERT (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    int plen = vsnprintf (str, size, format, fmt_args);
    va_end (fmt_args);

    /*
     * plen is "the number of characters (excluding the terminating
     * null byte) which would have been written to the final string if
     * enough space had been available."
     */
    if (plen >= (ssize_t) size ||
	plen < 0) {
	str[size] = '\0';

	/*
	 * As we're including the input string in our helpful error
	 * message then we don't want BUFSIZ chars filling the screen
	 * so limit the entire error message to 80 bytes.
	 */
	char em[81];
	snprintf (em, 80, "idio_snprintf: reqd %d in %zd available: \"%s\"", plen, size, str);
	em[80] = '\0';

	idio_string_error_C (em, idio_integer (plen), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    return plen;
}

char *idio_constant_idio_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    char *r = NULL;
    char *t = NULL;

    intptr_t C_v = IDIO_CONSTANT_IDIO_VAL (v);

    switch (C_v) {
    case IDIO_CONSTANT_NIL:				t = "#n";				break;
    case IDIO_CONSTANT_UNDEF:				t = "#<undef>";				break;
    case IDIO_CONSTANT_UNSPEC:				t = "#<unspec>";			break;
    case IDIO_CONSTANT_EOF:				t = "#<eof>";				break;
    case IDIO_CONSTANT_TRUE:				t = "#t";				break;
    case IDIO_CONSTANT_FALSE:				t = "#f";				break;
    case IDIO_CONSTANT_VOID:				t = "(void)";				break;
    case IDIO_CONSTANT_NAN:				t = "#<NaN>";				break;

	/*
	 * We shouldn't really see any of the following constants but
	 * they leak out especially when the code errors.
	 *
	 * It's then easier to debug if we can read "PREDEFINED"
	 * rather than "C=2001"
	 */
    case IDIO_STACK_MARKER_PRESERVE_STATE:		t = "#<MARK preserve-state>";		break;
    case IDIO_STACK_MARKER_PRESERVE_ALL_STATE:		t = "#<MARK preserve-all-state>";	break;
    case IDIO_STACK_MARKER_TRAP:			t = "#<MARK trap>";			break;
    case IDIO_STACK_MARKER_PRESERVE_CONTINUATION:	t = "#<MARK preserve-continuation>";	break;
    case IDIO_STACK_MARKER_RETURN:			t = "#<MARK return>";			break;
    case IDIO_STACK_MARKER_DYNAMIC:			t = "#<MARK dynamic>";			break;
    case IDIO_STACK_MARKER_ENVIRON:			t = "#<MARK environ>";			break;

	/*
	 * There's a pretty strong argument that if idio_S_notreached
	 * is in *anything* then something has gone very badly wrong.
	 *
	 * It should only be used to shut the compiler up after where
	 * an error function will have invoked a non-local exit.
	 */
    case IDIO_CONSTANT_NOTREACHED:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.  A panic seem a bit extreme but obviously
	     * an internal error condition *has* returned and the next
	     * C source statement is (should be!)  "return
	     * idio_S_notreached".
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
	*sizep = idio_asprintf (&r, "#<type/constant/idio v=%10p VAL(v)=%#x>", v, C_v);
	idio_error_warning_message ("unhandled Idio constant: %s\n", r);
	break;
    }

    if (NULL == t) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There should be a case
	 * clause above.
	 */
	*sizep = idio_asprintf (&r, "#<CONSTANT? %10p>", v);
    } else {
	*sizep = idio_asprintf (&r, "%s", t);
    }

    return r;
}

IDIO idio_constant_idio_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_constant_idio_as_C_string (v, sizep, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

IDIO idio_util_method_run0 (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    IDIO func = (IDIO) IDIO_VTABLE_METHOD_DATA (m);

    if (idio_isa_function (func) == 0) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_msg_only ("method-run0", "method->data", "should be a function", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO cmd = IDIO_LIST2 (func, v);

    IDIO r = idio_vm_invoke_C (cmd);

    return r;
}

IDIO idio_util_method_run (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    IDIO data = (IDIO) IDIO_VTABLE_METHOD_DATA (m);

    if (idio_isa_pair (data) == 0) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_msg_only ("method-run", "method->data", "should be a tuple of (function arg ...)", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO func = IDIO_PAIR_H (data);
    IDIO args = IDIO_PAIR_T (data);

    if (idio_isa_function (func) == 0) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_msg_only ("method-run", "method->data", "should be a tuple of (function arg ...)", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO cmd = idio_list_append2 (IDIO_LIST2 (func, v), args);

    IDIO r = idio_vm_invoke_C (cmd);

    return r;
}

void idio_util_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (type_string);
    IDIO_ADD_PRIMITIVE (typename);
    IDIO_ADD_PRIMITIVE (members);
    IDIO_ADD_PRIMITIVE (zerop);
    IDIO_ADD_PRIMITIVE (nullp);
    IDIO_ADD_PRIMITIVE (void);
    IDIO_ADD_PRIMITIVE (voidp);
    IDIO_ADD_PRIMITIVE (undef);
    IDIO_ADD_PRIMITIVE (undefp);
    IDIO_ADD_PRIMITIVE (definedp);
    IDIO_ADD_PRIMITIVE (booleanp);
    IDIO_ADD_PRIMITIVE (identity);
    IDIO_ADD_PRIMITIVE (eqp);
    IDIO_ADD_PRIMITIVE (eqvp);
    IDIO_ADD_PRIMITIVE (equalp);
    IDIO_ADD_PRIMITIVE (add_as_string);
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

    /*
     * Printing stuff during shutdown (never a good idea but...) will
     * end up querying these two values.  At least the code checks
     * whether they are set first -- a side-effect from printing
     * during startup.
     */
    idio_print_conversion_format_sym = idio_S_nil;
    idio_print_conversion_precision_sym = idio_S_nil;
}

void idio_init_util ()
{
    idio_module_table_register (idio_util_add_primitives, idio_final_util, NULL);

    idio_print_conversion_format_sym = IDIO_SYMBOL ("idio-print-conversion-format");
    idio_print_conversion_precision_sym = IDIO_SYMBOL ("idio-print-conversion-precision");

    idio_features = IDIO_SYMBOL ("*idio-features*");
    idio_module_set_symbol_value (idio_features, idio_S_nil, idio_Idio_module);

#ifdef IDIO_DEBUG
    idio_add_feature (IDIO_SYMBOL ("IDIO_DEBUG"));
#endif

#ifdef __SANITIZE_ADDRESS__
    idio_add_feature (IDIO_SYMBOL ("__SANITIZE_ADDRESS__"));
#endif

#ifdef IDIO_EQUAL_DEBUG
    for (int i = 0; i < IDIO_TYPE_MAX; i++) {
	idio_equal_stats[i].count = 0;
	idio_equal_stats[i].duration.tv_sec = 0;
	idio_equal_stats[i].duration.tv_usec = 0;
	idio_equal_stats[i].mixed = 0;
    }
#endif

    /*
     * IDIO_TYPE_CONSTANT_IDIO isn't handled anywhere
     */
    idio_vtable_t *ci_vt = idio_vtable (IDIO_TYPE_CONSTANT_IDIO);

    idio_vtable_add_method (ci_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_constant_idio));

    idio_vtable_add_method (ci_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_constant_idio_method_2string));
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
