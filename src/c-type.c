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

IDIO_DEFINE_PRIMITIVE1 ("c/int?", C_intp, (IDIO o))
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

IDIO_DEFINE_PRIMITIVE1 ("c/uint?", C_uintp, (IDIO o))
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

IDIO_DEFINE_PRIMITIVE1 ("c/float?", C_floatp, (IDIO o))
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

IDIO_DEFINE_PRIMITIVE1 ("c/double?", C_doublep, (IDIO o))
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

IDIO_DEFINE_PRIMITIVE1 ("c/pointer?", C_pointerp, (IDIO o))
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
	free (IDIO_C_TYPE_POINTER_P (co));
    }

    free (co->u.C_type.u.C_pointer);
}

IDIO idio_C_number_cast (IDIO co, idio_type_e type)
{
    IDIO_ASSERT (co);
    IDIO_C_ASSERT (type);

    if (! IDIO_TYPE_POINTERP (co)) {
	idio_error_printf (IDIO_C_LOCATION ("idio_C_number_cast"), "conversion not possible from %s %d to %d", idio_type2string (co), idio_type (co), type);

	/* notreached */
	return idio_S_nil;
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
	    case IDIO_TYPE_STRING: r = idio_C_pointer ((void *) IDIO_STRING_S (co)); break;
	    case IDIO_TYPE_SUBSTRING: r = idio_C_pointer ((void *) IDIO_SUBSTRING_S (co)); break;
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
	idio_error_printf (IDIO_C_LOCATION ("idio_C_number_cast"), "conversion not possible from %s %d to %d", idio_type2string (co), idio_type (co), type);
    }

    return r;
}

#define IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE(name,cname,cmp)	\
    IDIO_DEFINE_PRIMITIVE2 (name, cname, (IDIO n1, IDIO n2))	\
    {								\
	IDIO_ASSERT (n1);					\
	IDIO_ASSERT (n2);					\
								\
	int result;						\
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
		    idio_error_C ("n2->type unexpected", n2, idio_string_C (#name)); \
									\
		    /* notreached */					\
		    return 0;						\
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
		idio_error_C ("n1->type unexpected", n1, idio_string_C (#name)); \
									\
		/* notreached */					\
		return 0;						\
	    }								\
	} else if (idio_type (n1) != idio_type (n2)) {			\
	    idio_error_C ("n1->type != n2->type", IDIO_LIST2 (n1, n2), idio_string_C (#name)); \
									\
	    /* notreached */						\
	    return 0;							\
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
		idio_error_C ("n1->type unexpected", n1, idio_string_C (#name)); \
									\
		/* notreached */					\
		return 0;						\
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


IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("c/<=", C_le, <=)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("c/<", C_lt, <)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("c/==", C_eq, ==)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("c/>=", C_ge, >=)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("c/>", C_gt, >)

void idio_init_c_type ()
{
}

void idio_c_type_add_primtives ()
{
    IDIO_ADD_PRIMITIVE (C_intp);
    IDIO_ADD_PRIMITIVE (C_uintp);
    IDIO_ADD_PRIMITIVE (C_floatp);
    IDIO_ADD_PRIMITIVE (C_doublep);
    IDIO_ADD_PRIMITIVE (C_pointerp);
    IDIO_ADD_PRIMITIVE (C_le);
    IDIO_ADD_PRIMITIVE (C_lt);
    IDIO_ADD_PRIMITIVE (C_eq);
    IDIO_ADD_PRIMITIVE (C_ge);
    IDIO_ADD_PRIMITIVE (C_gt);
}

void idio_final_c_type ()
{
}

