/*
 * Copyright (c) 2015, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * c-type.c
 *
 * Handling C types is a mess.  When comparing (or printing) arbitrary
 * C integral types (char, long, pid_t, off_t etc.) we end up with a
 * combinatorial explosion of potential cases such that we can
 * correctly decode the original types and leave the C compiler to
 * perform integer promotion as it sees fit.
 *
 * Alternatively, C integral types are dropped into either an intmax_t
 * or uintmax_t hopefully minimising the size of the explosion.
 */


#include "idio.h"

static IDIO idio_C_module = idio_S_nil;

IDIO idio_C_int (intmax_t v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_INT);

    IDIO_C_TYPE_INT (co) = v;

    return co;
}

int idio_isa_C_int (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_INT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("int?", C_intp, (IDIO o), "o", "\
test if `o` is an C-int				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C-int, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_int (o)) {
	r = idio_S_true;
    }

    return r;
}

intmax_t idio_C_int_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_int, co);

    return IDIO_C_TYPE_INT (co);
}

IDIO idio_C_uint (uintmax_t v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_UINT);

    IDIO_C_TYPE_UINT (co) = v;

    return co;
}

int idio_isa_C_uint (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UINT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("uint?", C_uintp, (IDIO o), "o", "\
test if `o` is an C-uint			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C-uint, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_uint (o)) {
	r = idio_S_true;
    }

    return r;
}

uintmax_t idio_C_uint_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_uint, co);

    return IDIO_C_TYPE_UINT (co);
}

IDIO idio_C_float (float v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_FLOAT);

    IDIO_C_TYPE_FLOAT (co) = v;

    return co;
}

int idio_isa_C_float (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_FLOAT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("float?", C_floatp, (IDIO o), "o", "\
test if `o` is an C-float			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C-float, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_float (o)) {
	r = idio_S_true;
    }

    return r;
}

float idio_C_float_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_float, co);

    return IDIO_C_TYPE_FLOAT (co);
}

IDIO idio_C_double (double v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_DOUBLE);

    IDIO_C_TYPE_DOUBLE (co) = v;

    return co;
}

int idio_isa_C_double (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_DOUBLE);
}

IDIO_DEFINE_PRIMITIVE1_DS ("double?", C_doublep, (IDIO o), "o", "\
test if `o` is an C-double			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C-double, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_double (o)) {
	r = idio_S_true;
    }

    return r;
}

double idio_C_double_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_double, co);

    return IDIO_C_TYPE_DOUBLE (co);
}

IDIO idio_C_pointer (void * v)
{
    /*
     * NB Don't IDIO_C_ASSERT (v) as we could be instantiating with
     * NULL
     */

    IDIO co = idio_gc_get (IDIO_TYPE_C_POINTER);

    IDIO_GC_ALLOC (co->u.C_type.u.C_pointer, sizeof (idio_C_pointer_t));

    IDIO_C_TYPE_POINTER_P (co) = v;
    IDIO_C_TYPE_POINTER_PRINTER (co) = NULL;
    IDIO_C_TYPE_POINTER_FREEP (co) = 0;

    return co;
}

IDIO idio_C_pointer_free_me (void * v)
{
    /*
     * NB We must IDIO_C_ASSERT (v) as we will be trying to free v
     */
    IDIO_C_ASSERT (v);

    IDIO co = idio_C_pointer (v);

    IDIO_C_TYPE_POINTER_FREEP (co) = 1;

    return co;
}

int idio_isa_C_pointer (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_POINTER);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pointer?", C_pointerp, (IDIO o), "o", "\
test if `o` is an C-pointer			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C-pointer, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_pointer (o)) {
	r = idio_S_true;
    }

    return r;
}

void * idio_C_pointer_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_pointer, co);

    return IDIO_C_TYPE_POINTER_P (co);
}

void idio_free_C_pointer (IDIO co)
{
    IDIO_ASSERT (co);

    if (IDIO_C_TYPE_POINTER_FREEP (co)) {
	IDIO_GC_FREE (IDIO_C_TYPE_POINTER_P (co));
    }

    IDIO_GC_FREE (co->u.C_type.u.C_pointer);
}

IDIO idio_C_number_cast (IDIO co, idio_type_e type)
{
    IDIO_ASSERT (co);
    IDIO_C_ASSERT (type);

    if (! IDIO_TYPE_POINTERP (co)) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "conversion not possible from %s %d to %d", idio_type2string (co), idio_type (co), type);

	return idio_S_notreached;
    }

    IDIO r = idio_S_nil;
    int fail = 0;

    switch (type) {
    case IDIO_TYPE_C_UINT:
	{
	    switch (idio_type (co)) {
	    case IDIO_TYPE_C_INT: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_INT (co)); break;
	    case IDIO_TYPE_C_UINT: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_UINT (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_STRING:
	{
	    switch (idio_type (co)) {
	    case IDIO_TYPE_C_POINTER: r = idio_C_pointer (IDIO_C_TYPE_POINTER_P (co)); break;
	    case IDIO_TYPE_STRING:
		{
		    size_t size = 0;
		    char *sco = idio_string_as_C (co, &size);
		    /*
		     * XXX NULs in co??
		     *
		     * Maybe this should store a pointer + length
		     */
		    r = idio_C_pointer_free_me ((void *) sco);
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		{
		    size_t size = 0;
		    char *sco = idio_string_as_C (co, &size);
		    /*
		     * XXX see above
		     */
		    r = idio_C_pointer_free_me ((void *) sco);
		}
		break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    default:
	fail = 1;
	break;
    }

    if (fail) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "conversion not possible from %s %d to %d", idio_type2string (co), idio_type (co), type);
    }

    return r;
}

#define IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE(name,cname,cmp)		\
    IDIO_DEFINE_PRIMITIVE2 (name, cname, (IDIO n1, IDIO n2))		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	int result;							\
									\
	if (idio_isa_fixnum (n1)) {					\
	    if (idio_isa_fixnum (n2)) {					\
		result = idio_eqp (n1, n2);				\
	    } else {							\
		intptr_t v1 = IDIO_FIXNUM_VAL (n1);			\
		switch (idio_type (n2)) {				\
		case IDIO_TYPE_C_INT:					\
		    result = (v1 cmp IDIO_C_TYPE_INT (n2));		\
		    break;						\
		case IDIO_TYPE_C_UINT:					\
		    result = (v1 cmp IDIO_C_TYPE_UINT (n2));		\
		    break;						\
		default:						\
		    idio_error_C ("C/" name ": n2->type unexpected", IDIO_LIST2 (n1, n2), idio_string_C ("C arithmetic cmp " name)); \
									\
		    return idio_S_notreached;				\
		}							\
	    }								\
	} else if (idio_isa_fixnum (n2)) {				\
	    intptr_t v2 = IDIO_FIXNUM_VAL (n2);				\
	    switch (idio_type (n1)) {					\
	    case IDIO_TYPE_C_INT:					\
		result = (IDIO_C_TYPE_INT (n1) cmp v2);			\
		break;							\
	    case IDIO_TYPE_C_UINT:					\
		result = (IDIO_C_TYPE_UINT (n1) cmp v2);		\
		break;							\
	    default:							\
		idio_error_C ("C/" name ": n1->type unexpected (n2:fixnum)", IDIO_LIST2 (n1, n2), idio_string_C ("C arithmetic cmp " name)); \
									\
		return idio_S_notreached;				\
	    }								\
	} else if (idio_type (n1) != idio_type (n2)) {			\
	    idio_error_C ("C/" name ": n1->type != n2->type", IDIO_LIST2 (n1, n2), idio_string_C ("C arithmetic cmp " name)); \
									\
	    return idio_S_notreached;					\
	} else {							\
	    switch (idio_type (n1)) {					\
	    case IDIO_TYPE_C_INT:					\
		result = (IDIO_C_TYPE_INT (n1) cmp IDIO_C_TYPE_INT (n2)); \
		break;							\
	    case IDIO_TYPE_C_UINT:					\
		result = (IDIO_C_TYPE_UINT (n1) cmp IDIO_C_TYPE_UINT (n2)); \
		break;							\
	    case IDIO_TYPE_C_FLOAT:					\
		result = (IDIO_C_TYPE_FLOAT (n1) cmp IDIO_C_TYPE_FLOAT (n2)); \
		break;							\
	    case IDIO_TYPE_C_DOUBLE:					\
		result = (IDIO_C_TYPE_DOUBLE (n1) cmp IDIO_C_TYPE_DOUBLE (n2)); \
		break;							\
	    case IDIO_TYPE_C_POINTER:					\
		result = (IDIO_C_TYPE_POINTER_P (n1) cmp IDIO_C_TYPE_POINTER_P (n2)); \
		break;							\
	    default:							\
		idio_error_C ("C/" name ": n1->type unexpected", IDIO_LIST2 (n1, n2), idio_string_C ("C arithmetic cmp " name)); \
									\
		return idio_S_notreached;				\
	    }								\
	}								\
									\
	IDIO r = idio_S_false;						\
									\
	if (result) {							\
	    r = idio_S_true;						\
	}								\
									\
	return r;							\
    }


IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("<=", C_le, <=)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("<", C_lt, <)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("==", C_eq, ==)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE (">=", C_ge, >=)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE (">", C_gt, >)

IDIO_DEFINE_PRIMITIVE1_DS ("->integer", C_to_integer, (IDIO inum), "i", "\
convert C integer `i` to an Idio integer	\n\
						\n\
:param o: C integer to convert			\n\
:type o: C-int, C-uint				\n\
						\n\
:return: Idio integer				\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (inum);

    if (idio_isa_C_uint (inum)) {
	return idio_uinteger (IDIO_C_TYPE_UINT (inum));
    } else if (idio_isa_C_int (inum)) {
	return idio_integer (IDIO_C_TYPE_INT (inum));
    } else {
	idio_error_param_type ("C_int|C_uint", inum, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("integer->", C_integer_to, (IDIO inum), "i", "\
convert Idio integer `i` to a C integer		\n\
						\n\
:param o: Idio integer to convert		\n\
:type o: bignum or fixnum			\n\
						\n\
:return: C integer				\n\
:rtype: C-int					\n\
")
{
    IDIO_ASSERT (inum);

    if (idio_isa_fixnum (inum)) {
	return idio_C_int (IDIO_FIXNUM_VAL (inum));
    } else if (idio_isa_integer_bignum (inum)) {
	return idio_C_int (idio_bignum_intmax_value (inum));
    } else {
	idio_error_param_type ("integer", inum, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("integer->unsigned", C_integer_to_unsigned, (IDIO inum), "i", "\
convert Idio integer `i` to a C unsigned integer\n\
						\n\
:param o: Idio integer to convert		\n\
:type o: bignum or fixnum			\n\
						\n\
:return: C unsigned integer			\n\
:rtype: C-uint					\n\
")
{
    IDIO_ASSERT (inum);

    if (idio_isa_fixnum (inum)) {
	return idio_C_uint (IDIO_FIXNUM_VAL (inum));
    } else if (idio_isa_integer_bignum (inum)) {
	return idio_C_uint (idio_bignum_uint64_value (inum));
    } else {
	idio_error_param_type ("positive integer", inum, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("|", C_bw_or, (IDIO v1, IDIO args), "v1 [...]", "\
perform a C bitwise-OR on `v1` etc.		\n\
						\n\
:param v1: C integer				\n\
:type v1: C-int					\n\
						\n\
:return: C integer				\n\
:rtype: C-int					\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_USER_TYPE_ASSERT (C_int, v1);

    int r = IDIO_C_TYPE_INT (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_USER_TYPE_ASSERT (C_int, arg);
	r = r | IDIO_C_TYPE_INT (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("&", C_bw_and, (IDIO v1, IDIO args), "v1 [...]", "\
perform a C bitwise-AND on `v1` etc.		\n\
						\n\
:param v1: C integer				\n\
:type v1: C-int					\n\
						\n\
:return: C integer				\n\
:rtype: C-int					\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_USER_TYPE_ASSERT (C_int, v1);

    int r = IDIO_C_TYPE_INT (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_USER_TYPE_ASSERT (C_int, arg);
	r = r & IDIO_C_TYPE_INT (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("^", C_bw_xor, (IDIO v1, IDIO args), "v1 [...]", "\
perform a C bitwise-XOR on `v1` etc.		\n\
						\n\
:param v1: C integer				\n\
:type v1: C-int					\n\
						\n\
:return: C integer				\n\
:rtype: C-int					\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_USER_TYPE_ASSERT (C_int, v1);

    int r = IDIO_C_TYPE_INT (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_USER_TYPE_ASSERT (C_int, arg);
	r = r ^ IDIO_C_TYPE_INT (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("~", C_bw_complement, (IDIO v1), "v1", "\
perform a C bitwise-complement on `v1`		\n\
						\n\
:param v1: C integer				\n\
:type v1: C-int					\n\
						\n\
:return: C integer				\n\
:rtype: C-int					\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_USER_TYPE_ASSERT (C_int, v1);

    int v = IDIO_C_TYPE_INT (v1);

    return idio_C_int (~ v);
}

void idio_c_type_add_primitives ()
{
    /*
     * NB As "<", "==", ">" etc. conflict with operators or primitives
     * in the *primitives* module then you should access these
     * directly as "C/>"
     */
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_intp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_uintp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_floatp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_doublep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_pointerp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_le);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_lt);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_eq);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ge);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_gt);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_to_integer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_integer_to);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_integer_to_unsigned);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_or);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_and);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_xor);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_complement);
}

void idio_init_c_type ()
{
    idio_module_table_register (idio_c_type_add_primitives, NULL);

    idio_C_module = idio_module (idio_symbols_C_intern ("C"));
}

