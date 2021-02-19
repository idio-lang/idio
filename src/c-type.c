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

/*
 * C base types
 *
 * https://en.wikipedia.org/wiki/C_data_types
 *
 * In practice we care about matching what the C API generator code is
 * going to do.
 *
 * I'm seeing fourteen base types:
 *
 * char				; XXX can be signed or unsigned
 * signed char
 * unsigned char		; at least 8 bits
 * short int
 * short unsigned int		; at least 16 bits
 * int
 * unsigned int			; at least 16 bits
 * long int
 * long unsigned int		; at least 32 bits
 * long long int
 * long long unsigned int	; at least 64 bits
 * float
 * double
 * (long double)
 */

#include "idio.h"

static IDIO idio_C_module = idio_S_nil;

IDIO idio_C_char (char v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_CHAR);

    IDIO_C_TYPE_char (co) = v;

    return co;
}

int idio_isa_C_char (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_CHAR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("char?", C_charp, (IDIO o), "o", "\
test if `o` is an C/char				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/char, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_char (o)) {
	r = idio_S_true;
    }

    return r;
}

char idio_C_char_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_char, co);

    return IDIO_C_TYPE_char (co);
}

IDIO idio_C_schar (signed char v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_SCHAR);

    IDIO_C_TYPE_schar (co) = v;

    return co;
}

int idio_isa_C_schar (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_SCHAR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("schar?", C_scharp, (IDIO o), "o", "\
test if `o` is an C/schar				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/schar, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_schar (o)) {
	r = idio_S_true;
    }

    return r;
}

signed char idio_C_schar_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_schar, co);

    return IDIO_C_TYPE_schar (co);
}

IDIO idio_C_uchar (unsigned char v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_UCHAR);

    IDIO_C_TYPE_uchar (co) = v;

    return co;
}

int idio_isa_C_uchar (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UCHAR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("uchar?", C_ucharp, (IDIO o), "o", "\
test if `o` is an C/uchar				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/uchar, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_uchar (o)) {
	r = idio_S_true;
    }

    return r;
}

unsigned char idio_C_uchar_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_uchar, co);

    return IDIO_C_TYPE_uchar (co);
}

IDIO idio_C_short (short v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_SHORT);

    IDIO_C_TYPE_short (co) = v;

    return co;
}

int idio_isa_C_short (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_SHORT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("short?", C_shortp, (IDIO o), "o", "\
test if `o` is an C/short				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/short, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_short (o)) {
	r = idio_S_true;
    }

    return r;
}

short idio_C_short_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_short, co);

    return IDIO_C_TYPE_short (co);
}

IDIO idio_C_ushort (unsigned short v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_USHORT);

    IDIO_C_TYPE_ushort (co) = v;

    return co;
}

int idio_isa_C_ushort (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_USHORT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ushort?", C_ushortp, (IDIO o), "o", "\
test if `o` is an C/ushort			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/ushort, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_ushort (o)) {
	r = idio_S_true;
    }

    return r;
}

unsigned short idio_C_ushort_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_ushort, co);

    return IDIO_C_TYPE_ushort (co);
}

IDIO idio_C_int (int v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_INT);

    IDIO_C_TYPE_int (co) = v;

    return co;
}

int idio_isa_C_int (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_INT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("int?", C_intp, (IDIO o), "o", "\
test if `o` is an C/int				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/int, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_int (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_C_int_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_int, co);

    return IDIO_C_TYPE_int (co);
}

IDIO idio_C_uint (unsigned int v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_UINT);

    IDIO_C_TYPE_uint (co) = v;

    return co;
}

int idio_isa_C_uint (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UINT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("uint?", C_uintp, (IDIO o), "o", "\
test if `o` is an C/uint			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/uint, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_uint (o)) {
	r = idio_S_true;
    }

    return r;
}

unsigned int idio_C_uint_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_uint, co);

    return IDIO_C_TYPE_uint (co);
}

IDIO idio_C_long (long v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_LONG);

    IDIO_C_TYPE_long (co) = v;

    return co;
}

int idio_isa_C_long (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_LONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("long?", C_longp, (IDIO o), "o", "\
test if `o` is an C/long				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/long, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_long (o)) {
	r = idio_S_true;
    }

    return r;
}

long idio_C_long_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_long, co);

    return IDIO_C_TYPE_long (co);
}

IDIO idio_C_ulong (unsigned long v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_ULONG);

    IDIO_C_TYPE_ulong (co) = v;

    return co;
}

int idio_isa_C_ulong (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_ULONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ulong?", C_ulongp, (IDIO o), "o", "\
test if `o` is an C/ulong			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/ulong, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_ulong (o)) {
	r = idio_S_true;
    }

    return r;
}

unsigned long idio_C_ulong_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_ulong, co);

    return IDIO_C_TYPE_ulong (co);
}

IDIO idio_C_longlong (long long v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_LONGLONG);

    IDIO_C_TYPE_longlong (co) = v;

    return co;
}

int idio_isa_C_longlong (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_LONGLONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("longlong?", C_longlongp, (IDIO o), "o", "\
test if `o` is an C/longlong				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/longlong, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_longlong (o)) {
	r = idio_S_true;
    }

    return r;
}

long long idio_C_longlong_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_longlong, co);

    return IDIO_C_TYPE_longlong (co);
}

IDIO idio_C_ulonglong (unsigned long long v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_ULONGLONG);

    IDIO_C_TYPE_ulonglong (co) = v;

    return co;
}

int idio_isa_C_ulonglong (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_ULONGLONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ulonglong?", C_ulonglongp, (IDIO o), "o", "\
test if `o` is an C/ulonglong			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/ulonglong, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_ulonglong (o)) {
	r = idio_S_true;
    }

    return r;
}

unsigned long long idio_C_ulonglong_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_ulonglong, co);

    return IDIO_C_TYPE_ulonglong (co);
}

IDIO idio_C_float (float v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_FLOAT);

    IDIO_C_TYPE_float (co) = v;

    return co;
}

int idio_isa_C_float (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_FLOAT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("float?", C_floatp, (IDIO o), "o", "\
test if `o` is an C/float			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/float, #f otherwise	\n\
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

    return IDIO_C_TYPE_float (co);
}

IDIO idio_C_double (double v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_DOUBLE);

    IDIO_C_TYPE_double (co) = v;

    return co;
}

int idio_isa_C_double (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_DOUBLE);
}

IDIO_DEFINE_PRIMITIVE1_DS ("double?", C_doublep, (IDIO o), "o", "\
test if `o` is an C/double			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/double, #f otherwise	\n\
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

    return IDIO_C_TYPE_double (co);
}

IDIO idio_C_longdouble (long double v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_LONGDOUBLE);

    IDIO_C_TYPE_longdouble (co) = v;

    return co;
}

int idio_isa_C_longdouble (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_LONGDOUBLE);
}

IDIO_DEFINE_PRIMITIVE1_DS ("longdouble?", C_longdoublep, (IDIO o), "o", "\
test if `o` is an C/longdouble			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/longdouble, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_longdouble (o)) {
	r = idio_S_true;
    }

    return r;
}

long double idio_C_longdouble_get (IDIO co)
{
    IDIO_ASSERT (co);
    IDIO_TYPE_ASSERT (C_longdouble, co);

    return IDIO_C_TYPE_longdouble (co);
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
test if `o` is an C/pointer			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an C/pointer, #f otherwise	\n\
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

int idio_isa_c_type (IDIO o)
{
    IDIO_ASSERT (o);

    switch (idio_type (o)) {
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
	return 1;
    default:
	return 0;
    }
}

int idio_isa_c_numeric_type (IDIO o)
{
    IDIO_ASSERT (o);

    switch (idio_type (o)) {
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
	return 1;
    default:
	return 0;
    }
}

int idio_isa_c_signed_integral_type (IDIO o)
{
    IDIO_ASSERT (o);

    switch (idio_type (o)) {
    case IDIO_TYPE_C_SCHAR:
    case IDIO_TYPE_C_SHORT:
    case IDIO_TYPE_C_INT:
    case IDIO_TYPE_C_LONG:
    case IDIO_TYPE_C_LONGLONG:
	return 1;
    default:
	return 0;
    }
}

int idio_isa_c_unsigned_integral_type (IDIO o)
{
    IDIO_ASSERT (o);

    switch (idio_type (o)) {
    case IDIO_TYPE_C_UCHAR:
    case IDIO_TYPE_C_USHORT:
    case IDIO_TYPE_C_UINT:
    case IDIO_TYPE_C_ULONG:
    case IDIO_TYPE_C_ULONGLONG:
	return 1;
    default:
	return 0;
    }
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
	    case IDIO_TYPE_C_INT: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_int (co)); break;
	    case IDIO_TYPE_C_UINT: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_uint (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_float (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_uint ((uint64_t) IDIO_C_TYPE_double (co)); break;
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
		case IDIO_TYPE_C_CHAR:					\
		    result = (v1 cmp IDIO_C_TYPE_char (n2));		\
		    break;						\
		case IDIO_TYPE_C_SCHAR:					\
		    result = (v1 cmp IDIO_C_TYPE_schar (n2));		\
		    break;						\
		case IDIO_TYPE_C_UCHAR:					\
		    result = (v1 cmp IDIO_C_TYPE_uchar (n2));		\
		    break;						\
		case IDIO_TYPE_C_SHORT:					\
		    result = (v1 cmp IDIO_C_TYPE_short (n2));		\
		    break;						\
		case IDIO_TYPE_C_USHORT:				\
		    result = (v1 cmp IDIO_C_TYPE_ushort (n2));		\
		    break;						\
		case IDIO_TYPE_C_INT:					\
		    result = (v1 cmp IDIO_C_TYPE_int (n2));		\
		    break;						\
		case IDIO_TYPE_C_UINT:					\
		    result = (v1 cmp IDIO_C_TYPE_uint (n2));		\
		    break;						\
		case IDIO_TYPE_C_LONG:					\
		    result = (v1 cmp IDIO_C_TYPE_long (n2)); \
		    break;						\
		case IDIO_TYPE_C_ULONG:					\
		    result = (v1 cmp IDIO_C_TYPE_ulong (n2));		\
		    break;						\
		case IDIO_TYPE_C_LONGLONG:				\
		    result = (v1 cmp IDIO_C_TYPE_longlong (n2));	\
		    break;						\
		case IDIO_TYPE_C_ULONGLONG:				\
		    result = (v1 cmp IDIO_C_TYPE_ulonglong (n2));	\
		    break;						\
		case IDIO_TYPE_C_FLOAT:					\
		    result = (v1 cmp IDIO_C_TYPE_float (n2));		\
		    break;						\
		case IDIO_TYPE_C_DOUBLE:				\
		    result = (v1 cmp IDIO_C_TYPE_double (n2));		\
		    break;						\
		case IDIO_TYPE_C_LONGDOUBLE:				\
		    result = (v1 cmp IDIO_C_TYPE_longdouble (n2));	\
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
	    case IDIO_TYPE_C_CHAR:					\
		result = (IDIO_C_TYPE_char (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_SCHAR:					\
		result = (IDIO_C_TYPE_schar (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_UCHAR:					\
		result = (IDIO_C_TYPE_uchar (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_SHORT:					\
		result = (IDIO_C_TYPE_short (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_USHORT:					\
		result = (IDIO_C_TYPE_ushort (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_INT:					\
		result = (IDIO_C_TYPE_int (n1) cmp v2);			\
		break;							\
	    case IDIO_TYPE_C_UINT:					\
		result = (IDIO_C_TYPE_uint (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_LONG:					\
		result = (IDIO_C_TYPE_long (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_ULONG:					\
		result = (IDIO_C_TYPE_ulong (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_LONGLONG:					\
		result = (IDIO_C_TYPE_longlong (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_ULONGLONG:					\
		result = (IDIO_C_TYPE_ulonglong (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_FLOAT:					\
		result = (IDIO_C_TYPE_float (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_DOUBLE:					\
		result = (IDIO_C_TYPE_double (n1) cmp v2);		\
		break;							\
	    case IDIO_TYPE_C_LONGDOUBLE:				\
		result = (IDIO_C_TYPE_longdouble (n1) cmp v2);		\
		break;							\
	    default:							\
		idio_error_C ("C/" name ": n1->type unexpected (n2:fixnum)", IDIO_LIST2 (n1, n2), idio_string_C ("C arithmetic cmp " name)); \
									\
		return idio_S_notreached;				\
	    }								\
	} else if (idio_type (n1) != idio_type (n2)) {			\
	    idio_error_C ("C/" name ": n1->type != n2->type", IDIO_LIST2 (idio_string_C (idio_type2string (n1)), idio_string_C (idio_type2string (n2))), idio_string_C ("C arithmetic cmp " name)); \
									\
	    return idio_S_notreached;					\
	} else {							\
	    switch (idio_type (n1)) {					\
	    case IDIO_TYPE_C_CHAR:					\
		result = (IDIO_C_TYPE_char (n1) cmp IDIO_C_TYPE_char (n2)); \
		break;							\
	    case IDIO_TYPE_C_SCHAR:					\
		result = (IDIO_C_TYPE_schar (n1) cmp IDIO_C_TYPE_schar (n2)); \
		break;							\
	    case IDIO_TYPE_C_UCHAR:					\
		result = (IDIO_C_TYPE_uchar (n1) cmp IDIO_C_TYPE_uchar (n2)); \
		break;							\
	    case IDIO_TYPE_C_SHORT:					\
		result = (IDIO_C_TYPE_short (n1) cmp IDIO_C_TYPE_short (n2)); \
		break;							\
	    case IDIO_TYPE_C_USHORT:					\
		result = (IDIO_C_TYPE_ushort (n1) cmp IDIO_C_TYPE_ushort (n2)); \
		break;							\
	    case IDIO_TYPE_C_INT:					\
		result = (IDIO_C_TYPE_int (n1) cmp IDIO_C_TYPE_int (n2)); \
		break;							\
	    case IDIO_TYPE_C_UINT:					\
		result = (IDIO_C_TYPE_uint (n1) cmp IDIO_C_TYPE_uint (n2)); \
		break;							\
	    case IDIO_TYPE_C_LONG:					\
		result = (IDIO_C_TYPE_long (n1) cmp IDIO_C_TYPE_long (n2)); \
		break;							\
	    case IDIO_TYPE_C_ULONG:					\
		result = (IDIO_C_TYPE_ulong (n1) cmp IDIO_C_TYPE_ulong (n2)); \
		break;							\
	    case IDIO_TYPE_C_LONGLONG:					\
		result = (IDIO_C_TYPE_longlong (n1) cmp IDIO_C_TYPE_longlong (n2)); \
		break;							\
	    case IDIO_TYPE_C_ULONGLONG:					\
		result = (IDIO_C_TYPE_ulonglong (n1) cmp IDIO_C_TYPE_ulonglong (n2)); \
		break;							\
	    case IDIO_TYPE_C_FLOAT:					\
		result = (IDIO_C_TYPE_float (n1) cmp IDIO_C_TYPE_float (n2)); \
		break;							\
	    case IDIO_TYPE_C_DOUBLE:					\
		result = (IDIO_C_TYPE_double (n1) cmp IDIO_C_TYPE_double (n2)); \
		break;							\
	    case IDIO_TYPE_C_LONGDOUBLE:					\
		result = (IDIO_C_TYPE_longdouble (n1) cmp IDIO_C_TYPE_longdouble (n2)); \
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
:type o: C/int, C/uint				\n\
						\n\
:return: Idio integer				\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (inum);

    if (idio_isa_C_char (inum)) {
	return idio_integer (IDIO_C_TYPE_char (inum));
    } else if (idio_isa_C_schar (inum)) {
	return idio_integer (IDIO_C_TYPE_schar (inum));
    } else if (idio_isa_C_uchar (inum)) {
	return idio_uinteger (IDIO_C_TYPE_uchar (inum));
    } else if (idio_isa_C_short (inum)) {
	return idio_integer (IDIO_C_TYPE_short (inum));
    } else if (idio_isa_C_ushort (inum)) {
	return idio_uinteger (IDIO_C_TYPE_ushort (inum));
    } else if (idio_isa_C_int (inum)) {
	return idio_integer (IDIO_C_TYPE_int (inum));
    } else if (idio_isa_C_uint (inum)) {
	return idio_uinteger (IDIO_C_TYPE_uint (inum));
    } else if (idio_isa_C_long (inum)) {
	return idio_integer (IDIO_C_TYPE_long (inum));
    } else if (idio_isa_C_ulong (inum)) {
	return idio_uinteger (IDIO_C_TYPE_ulong (inum));
    } else if (idio_isa_C_longlong (inum)) {
	return idio_integer (IDIO_C_TYPE_longlong (inum));
    } else if (idio_isa_C_ulonglong (inum)) {
	return idio_uinteger (IDIO_C_TYPE_ulonglong (inum));
    } else {
	idio_error_param_type ("C integral type", inum, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("integer->", C_integer_to, (IDIO inum, IDIO args), "i [type]", "\
convert Idio integer `i` to a C integer		\n\
						\n\
If `i` is a fixnum then use `type` for		\n\
a more specific C type.				\n\
						\n\
An integer bignum is converted to an intmax_t.	\n\
						\n\
						\n\
:param o: Idio integer to convert		\n\
:type o: fixnum or bignum			\n\
:param type: (optional) C type to use		\n\
:type type: symbol				\n\
						\n\
`type` defaults to 'int and can be one of:	\n\
char						\n\
schar						\n\
uchar						\n\
short						\n\
ushort						\n\
int						\n\
uint						\n\
long						\n\
ulong						\n\
longlong					\n\
ulonglong					\n\
						\n\
or an alias thereof, eg. libc/pid_t		\n\
						\n\
:return: C integer				\n\
:rtype: C/int etc.				\n\
")
{
    IDIO_ASSERT (inum);

    IDIO t = idio_S_int;

    if (idio_S_nil != args) {
	t = IDIO_PAIR_H (args);
    }

    if (idio_isa_fixnum (inum)) {
	if (idio_S_char == t) {
	    return idio_C_char ((char) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_schar == t) {
	    return idio_C_schar ((signed char) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_uchar == t) {
	    return idio_C_uchar ((unsigned char) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_short == t) {
	    return idio_C_short ((short) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_ushort == t) {
	    return idio_C_ushort ((unsigned short) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_int == t) {
	    return idio_C_int ((int) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_uint == t) {
	    return idio_C_uint ((unsigned int) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_long == t) {
	    return idio_C_long ((long) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_ulong == t) {
	    return idio_C_ulong ((unsigned long) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_longlong == t) {
	    return idio_C_longlong ((long long) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_ulonglong == t) {
	    return idio_C_ulonglong ((unsigned long long) IDIO_FIXNUM_VAL (inum));
	} else {
	    idio_error_param_value ("type", "C integral type", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_isa_integer_bignum (inum)) {
	return idio_libc_intmax_t (idio_bignum_intmax_value (inum));
    } else {
	idio_error_param_type ("integer", inum, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("integer->unsigned", C_integer_to_unsigned, (IDIO inum, IDIO args), "i [C/unsigned type]", "\
convert Idio integer `i` to a C unsigned integer\n\
						\n\
If `i` is a fixnum then use `type` for		\n\
a more specific C type.				\n\
						\n\
An integer bignum is converted to a uintmax_t.	\n\
						\n\
:param o: Idio integer to convert		\n\
:type o: bignum or fixnum			\n\
:param type: (optional) C type to use		\n\
:type type: symbol				\n\
						\n\
`type` defaults to 'uint and can be one of:	\n\
uchar						\n\
ushort						\n\
uint						\n\
ulong						\n\
ulonglong					\n\
						\n\
:return: C unsigned integer			\n\
:rtype: C/uint					\n\
")
{
    IDIO_ASSERT (inum);

    IDIO t = idio_S_uint;

    if (idio_S_nil != args) {
	t = IDIO_PAIR_H (args);
    }

    if (idio_isa_fixnum (inum)) {
	if (idio_S_uchar == t) {
	    return idio_C_uchar ((unsigned char) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_ushort == t) {
	    return idio_C_ushort ((unsigned short) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_uint == t) {
	    return idio_C_uint ((unsigned int) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_ulong == t) {
	    return idio_C_ulong ((unsigned long) IDIO_FIXNUM_VAL (inum));
	} else 	if (idio_S_ulonglong == t) {
	    return idio_C_ulonglong ((unsigned long long) IDIO_FIXNUM_VAL (inum));
	} else {
	    idio_error_param_value ("type", "C unsigned integral type", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_isa_integer_bignum (inum)) {
	return idio_libc_uintmax_t (idio_bignum_uintmax_value (inum));
    } else {
	idio_error_param_type ("positive integer", inum, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("|", C_bw_ior, (IDIO v1, IDIO args), "v1 [...]", "\
perform a C bitwise-OR on `v1` etc.	\n\
					\n\
:param v1: C unsigned integer		\n\
:type v1: C/int			\n\
					\n\
:return: C unsigned integer		\n\
:rtype: C/int				\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_USER_C_TYPE_ASSERT (int, v1);

    int r = IDIO_C_TYPE_int (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_USER_C_TYPE_ASSERT (int, arg);
	r = r | IDIO_C_TYPE_int (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("&", C_bw_and, (IDIO v1, IDIO args), "v1 [...]", "\
perform a C bitwise-AND on `v1` etc.	\n\
					\n\
:param v1: C unsigned integer		\n\
:type v1: C/int			\n\
					\n\
:return: C unsigned integer		\n\
:rtype: C/int				\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_USER_C_TYPE_ASSERT (int, v1);

    int r = IDIO_C_TYPE_int (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_USER_C_TYPE_ASSERT (int, arg);
	r = r & IDIO_C_TYPE_int (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("^", C_bw_xor, (IDIO v1, IDIO args), "v1 [...]", "\
perform a C bitwise-XOR on `v1` etc.	\n\
					\n\
:param v1: C unsigned integer		\n\
:type v1: C/int			\n\
					\n\
:return: C unsigned integer		\n\
:rtype: C/int				\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_USER_C_TYPE_ASSERT (int, v1);

    int r = IDIO_C_TYPE_int (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_USER_C_TYPE_ASSERT (int, arg);
	r = r ^ IDIO_C_TYPE_int (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("~", C_bw_complement, (IDIO v1), "v1", "\
perform a C bitwise-complement on `v1`	\n\
					\n\
:param v1: C unsigned integer		\n\
:type v1: C/int			\n\
					\n\
:return: C unsigned integer		\n\
:rtype: C/int				\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_USER_C_TYPE_ASSERT (int, v1);

    int v = IDIO_C_TYPE_int (v1);

    return idio_C_int (~ v);
}

void idio_c_type_add_primitives ()
{
    /*
     * NB As "<", "==", ">" etc. conflict with operators or primitives
     * in the *primitives* module then you should access these
     * directly as "C/>"
     */
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_charp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_scharp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ucharp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_shortp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ushortp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_intp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_uintp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_longp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ulongp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_longlongp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ulonglongp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_floatp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_doublep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_longdoublep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_pointerp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_le);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_lt);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_eq);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ge);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_gt);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_to_integer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_integer_to);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_integer_to_unsigned);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_ior);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_and);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_xor);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_complement);
}

void idio_init_c_type ()
{
    idio_module_table_register (idio_c_type_add_primitives, NULL);

    idio_C_module = idio_module (idio_symbols_C_intern ("C"));

    idio_module_export_symbol_value (idio_symbols_C_intern ("0u"), idio_C_uint (0U), idio_C_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("0i"), idio_C_int (0), idio_C_module);
}

