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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
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

#include "bignum.h"
#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static IDIO idio_C_pointer_type_vtables;

static IDIO idio_C_module = idio_S_nil;

static void idio_C_conversion_error (char const *msg, IDIO n, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (n);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_C_conversion_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       n));

    /* notreached */
}

IDIO idio_C_char (char const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_CHAR);
    co->vtable = idio_vtable (IDIO_TYPE_C_CHAR);

    IDIO_C_TYPE_char (co) = v;

    return co;
}

int idio_isa_C_char (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_CHAR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("char?", C_charp, (IDIO o), "o", "\
test if `o` is a C/char				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/char, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_char (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_schar (signed char const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_SCHAR);
    co->vtable = idio_vtable (IDIO_TYPE_C_SCHAR);

    IDIO_C_TYPE_schar (co) = v;

    return co;
}

int idio_isa_C_schar (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_SCHAR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("schar?", C_scharp, (IDIO o), "o", "\
test if `o` is a C/schar				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/schar, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_schar (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_uchar (unsigned char const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_UCHAR);
    co->vtable = idio_vtable (IDIO_TYPE_C_UCHAR);

    IDIO_C_TYPE_uchar (co) = v;

    return co;
}

int idio_isa_C_uchar (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UCHAR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("uchar?", C_ucharp, (IDIO o), "o", "\
test if `o` is a C/uchar				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/uchar, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_uchar (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_short (short const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_SHORT);
    co->vtable = idio_vtable (IDIO_TYPE_C_SHORT);

    IDIO_C_TYPE_short (co) = v;

    return co;
}

int idio_isa_C_short (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_SHORT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("short?", C_shortp, (IDIO o), "o", "\
test if `o` is a C/short				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/short, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_short (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_ushort (unsigned short const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_USHORT);
    co->vtable = idio_vtable (IDIO_TYPE_C_USHORT);

    IDIO_C_TYPE_ushort (co) = v;

    return co;
}

int idio_isa_C_ushort (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_USHORT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ushort?", C_ushortp, (IDIO o), "o", "\
test if `o` is a C/ushort			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/ushort, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_ushort (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_int (int const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_INT);
    co->vtable = idio_vtable (IDIO_TYPE_C_INT);

    IDIO_C_TYPE_int (co) = v;

    return co;
}

int idio_isa_C_int (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_INT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("int?", C_intp, (IDIO o), "o", "\
test if `o` is a C/int				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/int, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_int (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_uint (unsigned int const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_UINT);
    co->vtable = idio_vtable (IDIO_TYPE_C_UINT);

    IDIO_C_TYPE_uint (co) = v;

    return co;
}

int idio_isa_C_uint (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UINT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("uint?", C_uintp, (IDIO o), "o", "\
test if `o` is a C/uint			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/uint, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_uint (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_long (long const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_LONG);
    co->vtable = idio_vtable (IDIO_TYPE_C_LONG);

    IDIO_C_TYPE_long (co) = v;

    return co;
}

int idio_isa_C_long (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_LONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("long?", C_longp, (IDIO o), "o", "\
test if `o` is a C/long				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/long, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_long (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_ulong (unsigned long const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_ULONG);
    co->vtable = idio_vtable (IDIO_TYPE_C_ULONG);

    IDIO_C_TYPE_ulong (co) = v;

    return co;
}

int idio_isa_C_ulong (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_ULONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ulong?", C_ulongp, (IDIO o), "o", "\
test if `o` is a C/ulong			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/ulong, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_ulong (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_longlong (long long const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_LONGLONG);
    co->vtable = idio_vtable (IDIO_TYPE_C_LONGLONG);

    IDIO_C_TYPE_longlong (co) = v;

    return co;
}

int idio_isa_C_longlong (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_LONGLONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("longlong?", C_longlongp, (IDIO o), "o", "\
test if `o` is a C/longlong				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/longlong, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_longlong (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_ulonglong (unsigned long long const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_ULONGLONG);
    co->vtable = idio_vtable (IDIO_TYPE_C_ULONGLONG);

    IDIO_C_TYPE_ulonglong (co) = v;

    return co;
}

int idio_isa_C_ulonglong (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_ULONGLONG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ulonglong?", C_ulonglongp, (IDIO o), "o", "\
test if `o` is a C/ulonglong			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/ulonglong, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_ulonglong (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_float (float const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_FLOAT);
    co->vtable = idio_vtable (IDIO_TYPE_C_FLOAT);

    IDIO_C_TYPE_float (co) = v;

    return co;
}

int idio_isa_C_float (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_FLOAT);
}

IDIO_DEFINE_PRIMITIVE1_DS ("float?", C_floatp, (IDIO o), "o", "\
test if `o` is a C/float			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/float, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_float (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_double (double const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_DOUBLE);
    co->vtable = idio_vtable (IDIO_TYPE_C_DOUBLE);

    IDIO_C_TYPE_double (co) = v;

    return co;
}

int idio_isa_C_double (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_DOUBLE);
}

IDIO_DEFINE_PRIMITIVE1_DS ("double?", C_doublep, (IDIO o), "o", "\
test if `o` is a C/double			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/double, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_double (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_longdouble (long double const v)
{
    IDIO co = idio_gc_get (IDIO_TYPE_C_LONGDOUBLE);
    co->vtable = idio_vtable (IDIO_TYPE_C_LONGDOUBLE);

    IDIO_C_TYPE_longdouble (co) = v;

    return co;
}

int idio_isa_C_longdouble (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_LONGDOUBLE);
}

IDIO_DEFINE_PRIMITIVE1_DS ("longdouble?", C_longdoublep, (IDIO o), "o", "\
test if `o` is a C/longdouble			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/longdouble, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_longdouble (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_C_pointer (void * v)
{
    /*
     * NB Don't IDIO_C_ASSERT (v) as we could be instantiating with
     * NULL
     */

    IDIO co = idio_gc_get (IDIO_TYPE_C_POINTER);
    co->vtable = idio_vtable (IDIO_TYPE_C_POINTER);

    IDIO_GC_ALLOC (co->u.C_type.u.C_pointer, sizeof (idio_C_pointer_t));

    IDIO_C_TYPE_POINTER_PTYPE (co) = idio_S_nil;
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

IDIO idio_C_pointer_type_add_vtable (IDIO t)
{
    IDIO_ASSERT (t);

    IDIO_TYPE_ASSERT (pair, t);

    /*
     * The type, usually an idio_CSI_X, should be a tuple of
     * (typename, members, value-index-function)
     */
    idio_vtable_t *Cp_vt = idio_vtable (0);
    IDIO_VTABLE_PARENT (Cp_vt) = idio_vtable (IDIO_TYPE_C_POINTER);

    idio_vtable_add_method (Cp_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     IDIO_PAIR_H (t)));

    if (idio_isa_pair (IDIO_PAIR_T (t))) {
	idio_vtable_add_method (Cp_vt,
				idio_S_members,
				idio_vtable_create_method_value (idio_util_method_members,
								 IDIO_PAIR_HT (t)));

	if (idio_isa_pair (IDIO_PAIR_TT (t)) &&
	    idio_S_nil != IDIO_PAIR_HTT (t)) {
	    idio_vtable_add_method (Cp_vt,
				    idio_S_value_index,
				    idio_vtable_create_method_value (idio_util_method_value_index,
								     IDIO_PAIR_HTT (t)));

	    /*
	     * Any [mod/]struct-X-set! will be arranged as a setter.
	     */
	}
    }

    /*
     * {Cp_vt} is already on the idio_vtables array, no need to mark
     * it to be freed.
     */
    IDIO I_vt = idio_C_pointer (Cp_vt);
    idio_hash_set (idio_C_pointer_type_vtables, t, I_vt);

    return I_vt;
}

IDIO idio_C_pointer_type (IDIO t, void * v)
{
    IDIO_ASSERT (t);

    IDIO_TYPE_ASSERT (list, t);
    /*
     * NB We must IDIO_C_ASSERT (v) as we will be trying to free v
     */
    IDIO_C_ASSERT (v);

    IDIO co = idio_C_pointer_free_me (v);

    /*
     * C/pointers with a type need some vtable management:
     *
     * Find any existing vtable for pointer-type {t} or create a new
     * one.
     *
     * The new one should refer, in turn, to the generic C/pointer
     * vtable.
     */
    IDIO I_vt = idio_hash_reference (idio_C_pointer_type_vtables, t, IDIO_LIST1 (idio_S_false));
    if (idio_S_false == I_vt) {
	I_vt = idio_C_pointer_type_add_vtable (t);
    }
    co->vtable = IDIO_C_TYPE_POINTER_P (I_vt);

    IDIO_C_TYPE_POINTER_PTYPE (co) = t;

    return co;
}

int idio_isa_C_pointer (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_POINTER);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pointer?", C_pointerp, (IDIO o), "o", "\
test if `o` is a C/pointer			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C/pointer, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_pointer (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pointer-name", C_pointer_name, (IDIO p), "p", "\
Return the CSI name of C/pointer `p`		\n\
						\n\
:param p: C/pointer to query			\n\
:type p: C/pointer				\n\
:return: CSI name or ``#n``			\n\
:rtype: symbol or ``#n``			\n\
						\n\
The CSI name is descriptive only.		\n\
")
{
    IDIO_ASSERT (p);

    IDIO r = idio_S_nil;

    if (idio_isa_C_pointer (p)) {
	IDIO pt = IDIO_C_TYPE_POINTER_PTYPE (p);
	if (idio_isa_pair (pt)) {
	    r = IDIO_PAIR_H (pt);
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pointer-members", C_pointer_members, (IDIO p), "p", "\
Return the CSI members of C/pointer `p`		\n\
						\n\
:param p: C/pointer to query			\n\
:type p: C/pointer				\n\
:return: CSI members or ``#n``			\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (p);

    IDIO r = idio_S_nil;

    if (idio_isa_C_pointer (p)) {
	IDIO pt = IDIO_C_TYPE_POINTER_PTYPE (p);
	if (idio_isa_pair (pt) &&
	    idio_list_length (pt) > 1) {
	    r = IDIO_PAIR_HT (pt);
	}
    }

    return r;
}

void idio_free_C_pointer (IDIO co)
{
    IDIO_ASSERT (co);

    if (IDIO_C_TYPE_POINTER_FREEP (co)) {
	idio_free (IDIO_C_TYPE_POINTER_P (co));
    }

    IDIO_GC_FREE (co->u.C_type.u.C_pointer, sizeof (idio_C_pointer_t));
}

int idio_isa_C_type (IDIO o)
{
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

IDIO_DEFINE_PRIMITIVE1_DS ("type?", C_typep, (IDIO o), "o", "\
test if `o` is any C type			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is any C type, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_type (o)) {
	r = idio_S_true;
    }

    return r;
}

/*
 * XXX a char is not a number -- try using a signed char!
 */
int idio_isa_C_number (IDIO o)
{
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

IDIO_DEFINE_PRIMITIVE1_DS ("number?", C_numberp, (IDIO o), "o", "\
test if `o` is any C numeric type		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is any C numeric type, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_number (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_C_integral (IDIO o)
{
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
	return 1;
    default:
	return 0;
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("integral?", C_integralp, (IDIO o), "o", "\
test if `o` is any C integral numeric type	\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is any C integral numeric type, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_integral (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_C_unsigned (IDIO o)
{
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

IDIO_DEFINE_PRIMITIVE1_DS ("unsigned?", C_unsignedp, (IDIO o), "o", "\
test if `o` is any C unsigned numeric type	\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is any C unsigned numeric type, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_unsigned (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_C_floating (IDIO o)
{
    switch (idio_type (o)) {
    case IDIO_TYPE_C_FLOAT:
    case IDIO_TYPE_C_DOUBLE:
    case IDIO_TYPE_C_LONGDOUBLE:
	return 1;
    default:
	return 0;
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("floating?", C_floatingp, (IDIO o), "o", "\
test if `o` is a C floating point type		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a C floating point type, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_C_floating (o)) {
	r = idio_S_true;
    }

    return r;
}

/*
 * Printing C types is a little more intricate than at first blush.
 * Not because calling snprintf(3) is hard but because the chances are
 * we've been called from %format which has potentially set the
 * conversion precision and specifier.
 *
 * The conversion specifier is handled below.
 *
 * For integral types (and 'char, above) the conversion precision is
 * delegated back to %format as it will be applied to the string that
 * we return.
 *
 * For floating point values that doesn't work as %format just has a
 * string in its hands and the precision affects the significant
 * figures after the decimal point so we need to figure out the
 * precision here.
 *
 *
 * Broadly, we gradually build a format specification in {fmt} based
 * on whatever is relevant.
 *
 * {fmt} needs to be big enough to hold the largest format string we
 * need.  The conversion precision is a fixnum which can reach 19
 * digits and be negative giving something like "%.-{19}le" or 24
 * characters.
 *
 * 30 should cover it!
 */
char *idio_C_type_format_string (int type)
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
		idio_error_param_value_msg_only ("idio_C_type_format_string", "idio-print-conversion-format", "should be unicode", IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }
    ipcf = idio_S_false;

    char *fmt = idio_alloc (30);
    size_t fmt_len = 0;

    switch (type) {
    case IDIO_TYPE_C_CHAR:
	fmt_len = 2;
	memcpy (fmt, "%c", fmt_len);
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
	{

	    switch (type) {
	    case IDIO_TYPE_C_SCHAR:
	    case IDIO_TYPE_C_UCHAR:
		fmt_len = 3;
		memcpy (fmt, "%hh", fmt_len);
		break;
	    case IDIO_TYPE_C_SHORT:
	    case IDIO_TYPE_C_USHORT:
		fmt_len = 2;
		memcpy (fmt, "%h", fmt_len);
		break;
	    case IDIO_TYPE_C_INT:
	    case IDIO_TYPE_C_UINT:
		fmt_len = 1;
		memcpy (fmt, "%", fmt_len);
		break;
	    case IDIO_TYPE_C_LONG:
	    case IDIO_TYPE_C_ULONG:
		fmt_len = 2;
		memcpy (fmt, "%l", fmt_len);
		break;
	    case IDIO_TYPE_C_LONGLONG:
	    case IDIO_TYPE_C_ULONGLONG:
		fmt_len = 3;
		memcpy (fmt, "%ll", fmt_len);
		break;
	    }

	    switch (type) {
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
		if (idio_S_false != ipcf) {
		    idio_unicode_t f = IDIO_UNICODE_VAL (ipcf);
		    switch (type) {
		    case IDIO_TYPE_C_SCHAR:
		    case IDIO_TYPE_C_SHORT:
		    case IDIO_TYPE_C_INT:
		    case IDIO_TYPE_C_LONG:
		    case IDIO_TYPE_C_LONGLONG:
			switch (f) {
			case IDIO_PRINT_CONVERSION_FORMAT_d:
			    memcpy (fmt + fmt_len++, "d", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_s:
			    /*
			     * A generic: printf "%s" e
			     */
			    memcpy (fmt + fmt_len++, "d", 1);
			    break;
			default:
			    /*
			     * Code coverage:
			     *
			     * format "%4q" 10		; "  10"
			     */
#ifdef IDIO_DEBUG
			    fprintf (stderr, "signed C type as-string: unexpected conversion format: '%c' (%#x).  Using 'd'\n", (int) f, (int) f);
#endif
			    memcpy (fmt + fmt_len++, "d", 1);
			    break;
			}
			break;
		    case IDIO_TYPE_C_UCHAR:
		    case IDIO_TYPE_C_USHORT:
		    case IDIO_TYPE_C_UINT:
		    case IDIO_TYPE_C_ULONG:
		    case IDIO_TYPE_C_ULONGLONG:
			switch (f) {
			case IDIO_PRINT_CONVERSION_FORMAT_X:
			    memcpy (fmt + fmt_len++, "X", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_o:
			    memcpy (fmt + fmt_len++, "o", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_u:
			    memcpy (fmt + fmt_len++, "u", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_x:
			    memcpy (fmt + fmt_len++, "x", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_s:
			    /*
			     * A generic: printf "%s" e
			     */
			    memcpy (fmt + fmt_len++, "u", 1);
			    break;
			default:
			    /*
			     * Code coverage:
			     *
			     * format "%4q" 10		; "  10"
			     */
#ifdef IDIO_DEBUG
			    fprintf (stderr, "idio_C_type_format-string: unexpected conversion format: '%c' (%#x).  Using 'u'\n", (int) f, (int) f);
#endif
			    memcpy (fmt + fmt_len++, "u", 1);
			    break;
			}
			break;
		    }
		} else {
		    switch (type) {
		    case IDIO_TYPE_C_SCHAR:
		    case IDIO_TYPE_C_SHORT:
		    case IDIO_TYPE_C_INT:
		    case IDIO_TYPE_C_LONG:
		    case IDIO_TYPE_C_LONGLONG:
			memcpy (fmt + fmt_len++, "d", 1);
			break;
		    case IDIO_TYPE_C_UCHAR:
		    case IDIO_TYPE_C_USHORT:
		    case IDIO_TYPE_C_UINT:
		    case IDIO_TYPE_C_ULONG:
		    case IDIO_TYPE_C_ULONGLONG:
			memcpy (fmt + fmt_len++, "u", 1);
			break;
		    }
		}
		break;
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_LONGDOUBLE:
		{
		    /*
		     * The default precision for both e, f and
		     * g formats is 6
		     */
		    int prec = 6;
		    if (idio_S_nil != idio_print_conversion_precision_sym) {
			IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
							      idio_Idio_module,
							      IDIO_LIST1 (idio_S_false));

			if (idio_S_false != ipcp) {
			    if (idio_isa_fixnum (ipcp)) {
				prec = IDIO_FIXNUM_VAL (ipcp);
			    } else {
				/*
				 * Test Case: ??
				 *
				 * If we set idio-print-conversion-precision to
				 * something not a fixnum (nor #f) then it affects
				 * *everything* in the codebase that uses
				 * idio-print-conversion-precision before we get here.
				 */
				idio_error_param_type ("fixnum", ipcp, IDIO_C_FUNC_LOCATION ());

				/* notreached */
				return NULL;
			    }
			}
		    }
		    fmt_len = idio_snprintf (fmt, 30, "%%.%d", prec);

		    switch (type) {
		    case IDIO_TYPE_C_DOUBLE:
			memcpy (fmt + fmt_len++, "l", 1);
			break;
		    case IDIO_TYPE_C_LONGDOUBLE:
			memcpy (fmt + fmt_len++, "L", 1);
			break;
		    }

		    if (idio_S_false != ipcf) {
			idio_unicode_t f = IDIO_UNICODE_VAL (ipcf);
			switch (f) {
			case IDIO_PRINT_CONVERSION_FORMAT_e:
			    memcpy (fmt + fmt_len++, "e", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_f:
			    memcpy (fmt + fmt_len++, "f", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_g:
			    memcpy (fmt + fmt_len++, "g", 1);
			    break;
			case IDIO_PRINT_CONVERSION_FORMAT_s:
			    /*
			     * A generic: printf "%s" e
			     */
			    memcpy (fmt + fmt_len++, "g", 1);
			    break;
			default:
			    /*
			     * Code coverage:
			     *
			     * format "%4q" 10		; "  10"
			     */
#ifdef IDIO_DEBUG
			    fprintf (stderr, "idio_C_type_format_string: unexpected conversion format: '%c' (%#x).  Using 'd'\n", (int) f, (int) f);
#endif
			    memcpy (fmt + fmt_len++, "g", 1);
			    break;
			}
		    } else {
			memcpy (fmt + fmt_len++, "g", 1);
		    }
		}
		break;
	    }
	}
	break;
    default:
	{
	    fprintf (stderr, "idio_C_type_format_string: unexpected C base_type %d\n", type);
	    IDIO_C_ASSERT (0);
	}
	break;
    }
    fmt[fmt_len] = '\0';

    return fmt;
}

/*
 * Comparing IEEE 754 numbers is not easy.  See
 * https://floating-point-gui.de/errors/comparison/ and
 * https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
 *
 * These often fall back to the Units in Last Place (ULP) comparisons.
 */
int idio_C_float_C_eq_ULP (float o1, float o2, unsigned int max)
{
    idio_C_float_ULP_t uo1;
    uo1.f = o1;
    idio_C_float_ULP_t uo2;
    uo2.f = o2;

    if ((uo1.i < 0) != (uo2.i < 0)) {
	/*
	 * This == is for picking up +0 == -0
	 */
	if (o1 == o2) {
	    return 1;
	} else {
	    return 0;
	}
    }

    int32_t diff = uo1.i - uo2.i;
    if (diff < 0) {
	diff = -diff;
    }

    if (diff <= (int) max) {
	return 1;
    } else {
	return 0;
    }
}

int idio_C_float_C_ne_ULP (float o1, float o2, unsigned int max)
{
    return (idio_C_float_C_eq_ULP (o1, o2, max) == 0);
}

int idio_C_double_C_eq_ULP (double o1, double o2, unsigned int max)
{
    idio_C_double_ULP_t uo1;
    uo1.i = 0;
    uo1.d = o1;
    idio_C_double_ULP_t uo2;
    uo2.i = 0;
    uo2.d = o2;

    if ((uo1.i < 0) != (uo2.i < 0)) {
	/*
	 * This == is for picking up +0 == -0
	 */
	if (o1 == o2) {
	    return 1;
	} else {
	    return 0;
	}
    }

    int64_t diff = uo1.i - uo2.i;
    if (diff < 0) {
	diff = -diff;
    }

    if (diff <= max) {
	return 1;
    } else {
	return 0;
    }
}

int idio_C_double_C_ne_ULP (double o1, double o2, unsigned int max)
{
    return (idio_C_double_C_eq_ULP (o1, o2, max) == 0);
}

/*
 * XXX I'm pretty convinced this long double comparison is entirely
 * wrong.  And if Paul Dirac was conducting code review: not even
 * wrong.
 *
 * The problems are:
 *
 * 1. A long double will not always use 16 bytes.  It might only use
 *    twelve -- it might even be just a double.  Hopefully by
 *    assigning 0 to bit int64_t parts we zero out the differences.
 *
 * 2. I'm guessing at the use of i1 and i2.  That if the most
 *    significant one has any differences then they are definitively
 *    not the same and that we can do something useful with the diff
 *    of i2.
 *
 *    Except we can't, of course, if the implementation has only used
 *    a partial word -- what should be a diff of 1 bit is now a diff
 *    of 1 followed by 32 0s.  We could compare the mantissa elements
 *    of the parts struct but given there are two variants of long
 *    doubles and we don't know which one we're using then that's off
 *    as well.
 *
 * I think I'm saying don't try to compare long doubles!
 */
/*
 * Code coverage:
 *
 * The call in idio_equal() is guarded by the same message.
 */
int idio_C_longdouble_C_eq_ULP (long double o1, long double o2, unsigned int max)
{
    /*
     * Test Case: ??
     *
     * The call in idio_equal() is guarded by the same message.
     */
    idio_error_param_type_msg ("equality for C long double is not supported", IDIO_C_FUNC_LOCATION ());

    /* notreached */
    return 0;

    idio_C_longdouble_ULP_t uo1;
    uo1.i.i1 = 0;
    uo1.i.i2 = 0;
    uo1.ld = o1;
    idio_C_longdouble_ULP_t uo2;
    uo2.i.i1 = 0;
    uo2.i.i2 = 0;
    uo2.ld = o2;

    if ((uo1.i.i1 < 0) != (uo2.i.i1 < 0)) {
	/*
	 * This == is for picking up +0 == -0
	 */
	if (o1 == o2) {
	    return 1;
	} else {
	    return 0;
	}
    }

    int64_t diff1 = uo1.i.i1 - uo2.i.i1;
    if (diff1) {
	return 0;
    }

    int64_t diff2 = uo1.i.i2 - uo2.i.i2;
    if (diff2 < 0) {
	diff2 = -diff2;
    }

    if (diff2 <= max) {
	return 1;
    } else {
	return 0;
    }
}

int idio_C_longdouble_C_ne_ULP (long double o1, long double o2, unsigned int max)
{
    /*
     * Test Case: ??
     */
    idio_error_param_type_msg ("inequality for C long double is not supported", IDIO_C_FUNC_LOCATION ());

    /* notreached */
    return 0;

    return (idio_C_longdouble_C_eq_ULP (o1, o2, max) == 0);
}

/*
 * Code coverage: the n1 type test is actually done in the generic
 * primitive in fixnum.c so we can't raise that error without calling
 * the primitive directly from C
 */
#define IDIO_DEFINE_C_ARITHMETIC_PRIMITIVE(cname,op)			\
    IDIO idio_C_primitive_binary_ ## cname (IDIO n1, IDIO n2)		\
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	if (! idio_isa_C_number (n1)) {					\
	    idio_error_param_value_exp ("C/" #cname, "n1", n1, "C numeric type", IDIO_C_FUNC_LOCATION ()); \
	    return idio_S_notreached;					\
	}								\
	if (! idio_isa_C_number (n2)) {					\
	    idio_error_param_value_exp ("C/" #cname, "n2", n2, "C numeric type", IDIO_C_FUNC_LOCATION ()); \
	    return idio_S_notreached;					\
	}								\
	int t1 = idio_type (n1);					\
	int t2 = idio_type (n2);					\
	if (t1 != t2) {							\
	    idio_error_param_value_msg ("C/" #cname, "n1, n2 types", IDIO_LIST2 (idio_string_C (idio_type2string (n1)), idio_string_C (idio_type2string (n2))), "should be the same C numeric type for C arithmetic " #cname, IDIO_C_FUNC_LOCATION ()); \
	    return idio_S_notreached;					\
	}								\
	switch (t1) {							\
	case IDIO_TYPE_C_SCHAR:						\
	    return idio_C_schar (IDIO_C_TYPE_schar (n1) op IDIO_C_TYPE_schar (n2)); \
	case IDIO_TYPE_C_UCHAR:						\
	    return idio_C_uchar (IDIO_C_TYPE_uchar (n1) op IDIO_C_TYPE_uchar (n2)); \
	case IDIO_TYPE_C_SHORT:						\
	    return idio_C_short (IDIO_C_TYPE_short (n1) op IDIO_C_TYPE_short (n2)); \
	case IDIO_TYPE_C_USHORT:					\
	    return idio_C_ushort (IDIO_C_TYPE_ushort (n1) op IDIO_C_TYPE_ushort (n2)); \
	case IDIO_TYPE_C_INT:						\
	    return idio_C_int (IDIO_C_TYPE_int (n1) op IDIO_C_TYPE_int (n2)); \
	case IDIO_TYPE_C_UINT:						\
	    return idio_C_uint (IDIO_C_TYPE_uint (n1) op IDIO_C_TYPE_uint (n2)); \
	case IDIO_TYPE_C_LONG:						\
	    return idio_C_long (IDIO_C_TYPE_long (n1) op IDIO_C_TYPE_long (n2)); \
	case IDIO_TYPE_C_ULONG:						\
	    return idio_C_ulong (IDIO_C_TYPE_ulong (n1) op IDIO_C_TYPE_ulong (n2)); \
	case IDIO_TYPE_C_LONGLONG:					\
	    return idio_C_longlong (IDIO_C_TYPE_longlong (n1) op IDIO_C_TYPE_longlong (n2)); \
	case IDIO_TYPE_C_ULONGLONG:					\
	    return idio_C_ulonglong (IDIO_C_TYPE_ulonglong (n1) op IDIO_C_TYPE_ulonglong (n2)); \
	case IDIO_TYPE_C_FLOAT:						\
	    return idio_C_float (IDIO_C_TYPE_float (n1) op IDIO_C_TYPE_float (n2)); \
	case IDIO_TYPE_C_DOUBLE:					\
	    return idio_C_double (IDIO_C_TYPE_double (n1) op IDIO_C_TYPE_double (n2)); \
	case IDIO_TYPE_C_LONGDOUBLE:					\
	    return idio_C_longdouble (IDIO_C_TYPE_longdouble (n1) op IDIO_C_TYPE_longdouble (n2)); \
	default:							\
	    return 0;							\
	}								\
	return idio_S_notreached;					\
    }

IDIO_DEFINE_C_ARITHMETIC_PRIMITIVE(add,+)
IDIO_DEFINE_C_ARITHMETIC_PRIMITIVE(subtract,-)
IDIO_DEFINE_C_ARITHMETIC_PRIMITIVE(multiply,*)
IDIO_DEFINE_C_ARITHMETIC_PRIMITIVE(divide,/)

/*
 * Code coverage:
 *
 * Called from idio_C_fields_array() in c-struct.c ... which isn't
 * used.
 */
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

/*
 * For < and > the comparison operators work for all types
 */
#define IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE(name,cname,cmp)		\
    IDIO_DEFINE_PRIMITIVE2_DS (name, cname, (IDIO n1, IDIO n2), "n1 n2", "") \
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	int result;							\
									\
	if (idio_type (n1) != idio_type (n2)) {				\
	    idio_error_param_value_msg ("C/" name, "n1, n2 types", IDIO_LIST2 (idio_string_C (idio_type2string (n1)), idio_string_C (idio_type2string (n2))), "should be the same C numeric type for C arithmetic cmp " name, IDIO_C_FUNC_LOCATION ()); \
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
		idio_error_param_value_msg ("C/" name, "n1 type", IDIO_LIST2 (n1, n2), "unexpected", IDIO_C_FUNC_LOCATION ()); \
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

/*
 * For the equality operators we need to be careful with floating
 * point types
 */
#define IDIO_DEFINE_C_ARITHMETIC_EQ_PRIMITIVE(name,cname,cmp)		\
    IDIO_DEFINE_PRIMITIVE2_DS (name, cname, (IDIO n1, IDIO n2), "n1 n2", "") \
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	int result;							\
									\
	if (idio_type (n1) != idio_type (n2)) {				\
	    idio_error_param_value_msg ("C/" name, "n1, n2 types", IDIO_LIST2 (idio_string_C (idio_type2string (n1)), idio_string_C (idio_type2string (n2))), "should be the same C numeric type for C arithmetic eq " name, IDIO_C_FUNC_LOCATION ()); \
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
		result = idio_C_float_ ## cname ## _ULP (IDIO_C_TYPE_float (n1), IDIO_C_TYPE_float (n2), 1); \
		break;							\
	    case IDIO_TYPE_C_DOUBLE:					\
		result = idio_C_double_ ## cname ## _ULP (IDIO_C_TYPE_double (n1), IDIO_C_TYPE_double (n2), 1); \
		break;							\
	    case IDIO_TYPE_C_LONGDOUBLE:				\
		result = idio_C_longdouble_ ## cname ## _ULP (IDIO_C_TYPE_longdouble (n1), IDIO_C_TYPE_longdouble (n2), 1); \
		break;							\
	    case IDIO_TYPE_C_POINTER:					\
		result = (IDIO_C_TYPE_POINTER_P (n1) cmp IDIO_C_TYPE_POINTER_P (n2)); \
		break;							\
	    default:							\
		idio_error_param_value_msg ("C/" name, "n1 type", IDIO_LIST2 (n1, n2), "unexpected", IDIO_C_FUNC_LOCATION ()); \
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

/*
 * Finally for the alternate comparision operators <=, >= we, again,
 * need to be careful with floating point types -- and pass the < or >
 * operator so we can distinguish the tests
 */
#define IDIO_DEFINE_C_ARITHMETIC_CMP_EQ_PRIMITIVE(name,cname,cmp,op1)	\
    IDIO_DEFINE_PRIMITIVE2_DS (name, cname, (IDIO n1, IDIO n2), "n1 n2", "") \
    {									\
	IDIO_ASSERT (n1);						\
	IDIO_ASSERT (n2);						\
									\
	int result;							\
									\
	if (idio_type (n1) != idio_type (n2)) {				\
	    char em[BUFSIZ];						\
	    snprintf (em, BUFSIZ, "C/%s: n1, n2 types should be the same C numeric type for C arithmetic cmp eq %s", name, name); \
	    idio_error_param_type_msg_args (em, IDIO_LIST2 (idio_string_C (idio_type2string (n1)), idio_string_C (idio_type2string (n2))), IDIO_C_FUNC_LOCATION ()); \
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
		result = ((IDIO_C_TYPE_float (n1) op1 IDIO_C_TYPE_float (n2)) || \
			  idio_C_float_C_eq_ULP (IDIO_C_TYPE_float (n1), IDIO_C_TYPE_float (n2), 1)); \
		break;							\
	    case IDIO_TYPE_C_DOUBLE:					\
		result = ((IDIO_C_TYPE_double (n1) op1 IDIO_C_TYPE_double (n2)) || \
			  idio_C_double_C_eq_ULP (IDIO_C_TYPE_double (n1), IDIO_C_TYPE_double (n2), 1)); \
		break;							\
	    case IDIO_TYPE_C_LONGDOUBLE:					\
		result = ((IDIO_C_TYPE_longdouble (n1) op1 IDIO_C_TYPE_longdouble (n2)) || \
			  idio_C_longdouble_C_eq_ULP (IDIO_C_TYPE_longdouble (n1), IDIO_C_TYPE_longdouble (n2), 1)); \
		break;							\
	    case IDIO_TYPE_C_POINTER:					\
		result = (IDIO_C_TYPE_POINTER_P (n1) cmp IDIO_C_TYPE_POINTER_P (n2)); \
		break;							\
	    default:							\
		{							\
		    char em[BUFSIZ];					\
		    snprintf (em, BUFSIZ, "C/%s: n1 type unexpected", name); \
		    idio_error_param_type_msg_args (em, IDIO_LIST2 (n1, n2), IDIO_C_FUNC_LOCATION ()); \
									\
		    return idio_S_notreached;				\
		}							\
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


IDIO_DEFINE_C_ARITHMETIC_CMP_EQ_PRIMITIVE ("<=", C_le, <=, <)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE ("<", C_lt, <)
IDIO_DEFINE_C_ARITHMETIC_EQ_PRIMITIVE ("==", C_eq, ==)
IDIO_DEFINE_C_ARITHMETIC_EQ_PRIMITIVE ("!=", C_ne, !=)
IDIO_DEFINE_C_ARITHMETIC_CMP_EQ_PRIMITIVE (">=", C_ge, >=, >)
IDIO_DEFINE_C_ARITHMETIC_CMP_PRIMITIVE (">", C_gt, >)

IDIO_DEFINE_PRIMITIVE1_DS ("->integer", C_to_integer, (IDIO i), "i", "\
convert C integer `i` to an Idio integer	\n\
						\n\
:param o: C integer to convert			\n\
:type o: C/ integer type			\n\
:return: Idio integer				\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (i);

    if (idio_isa_C_char (i)) {
	return idio_integer (IDIO_C_TYPE_char (i));
    } else if (idio_isa_C_schar (i)) {
	return idio_integer (IDIO_C_TYPE_schar (i));
    } else if (idio_isa_C_uchar (i)) {
	return idio_uinteger (IDIO_C_TYPE_uchar (i));
    } else if (idio_isa_C_short (i)) {
	return idio_integer (IDIO_C_TYPE_short (i));
    } else if (idio_isa_C_ushort (i)) {
	return idio_uinteger (IDIO_C_TYPE_ushort (i));
    } else if (idio_isa_C_int (i)) {
	return idio_integer (IDIO_C_TYPE_int (i));
    } else if (idio_isa_C_uint (i)) {
	return idio_uinteger (IDIO_C_TYPE_uint (i));
    } else if (idio_isa_C_long (i)) {
	return idio_integer (IDIO_C_TYPE_long (i));
    } else if (idio_isa_C_ulong (i)) {
	return idio_uinteger (IDIO_C_TYPE_ulong (i));
    } else if (idio_isa_C_longlong (i)) {
	return idio_integer (IDIO_C_TYPE_longlong (i));
    } else if (idio_isa_C_ulonglong (i)) {
	return idio_uinteger (IDIO_C_TYPE_ulonglong (i));
    } else {
	/*
	 * Test Case: c-type-errors/c-type-2integer-bad-type.idio
	 *
	 * C/->integer #t
	 */
	idio_error_param_type ("C integral type", i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

IDIO idio_C_integer2 (IDIO i, IDIO t)
{
    IDIO_ASSERT (i);
    IDIO_ASSERT (t);

    IDIO_TYPE_ASSERT (symbol, t);

    intmax_t C_i;

    if (idio_isa_fixnum (i)) {
	C_i = IDIO_FIXNUM_VAL (i);
    } else if (idio_isa_integer_bignum (i)) {
	if (! IDIO_BIGNUM_INTEGER_P (i)) {
	    /*
	     * Annoyingly, idio_isa_integer_bignum() has probably just
	     * run idio_bignum_real_to_integer()...
	     */
	    i = idio_bignum_real_to_integer (i);
	}
	/*
	 * ALERT ALERT
	 *
	 * We'll get a ^rt-bignum-conversion-error from
	 * idio_bignum_intmax_t_value() before several of the following
	 * tests fire!
	 *
	 * Indeed, by definition, for the (u)long (64-bit systems) and
	 * (u)longlong (all systems?) tests, we cannot represent a
	 * testable value in an intmax_t.
	 *
	 * I've tried to annotate them below.
	 */
	C_i = idio_bignum_intmax_t_value (i);
    } else {
	/*
	 * Test Case: c-type-errors/c-type-integer2-bad-type.idio
	 *
	 * C/integer-> #t
	 */
	idio_error_param_type ("integer", i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (idio_S_char == t) {
	if (C_i > CHAR_MAX) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2-char-range.idio
	     *
	     * C/integer-> ((C/->number libc/CHAR_MAX) + 1) 'char
	     */
	    idio_C_conversion_error ("char: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_char ((char) C_i);
	}
    } else if (idio_S_schar == t) {
	if (C_i > SCHAR_MAX ||
	    C_i < SCHAR_MIN) {
	    /*
	     * Test Case(s):
	     *
	     *  c-type-errors/c-type-integer2-schar-range-min.idio
	     *  c-type-errors/c-type-integer2-schar-range-max.idio
	     *
	     * C/integer-> ((C/->number libc/SCHAR_MIN) - 1) 'schar
	     * C/integer-> ((C/->number libc/SCHAR_MAX) + 1) 'schar
	     */
	    idio_C_conversion_error ("schar: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_schar ((signed char) C_i);
	}
    } else if (idio_S_uchar == t) {
	if (C_i > UCHAR_MAX) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2-uchar-range.idio
	     *
	     * C/integer-> ((C/->number libc/UCHAR_MAX) + 1) 'uchar
	     */
	    idio_C_conversion_error ("uchar: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_uchar ((unsigned char) C_i);
	}
    } else if (idio_S_short == t) {
	if (C_i > SHRT_MAX ||
	    C_i < SHRT_MIN) {
	    /*
	     * Test Case(s):
	     *
	     *  c-type-errors/c-type-integer2-short-range-min.idio
	     *  c-type-errors/c-type-integer2-short-range-max.idio
	     *
	     * C/integer-> ((C/->number libc/SHRT_MIN) - 1) 'short
	     * C/integer-> ((C/->number libc/SHRT_MAX) + 1) 'short
	     */
	    idio_C_conversion_error ("short: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_short ((short) C_i);
	}
    } else if (idio_S_ushort == t) {
	if (C_i > USHRT_MAX) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2-ushort-range.idio
	     *
	     * C/integer-> ((C/->number libc/USHRT_MAX) + 1) 'ushort
	     */
	    idio_C_conversion_error ("ushort: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_ushort ((unsigned short) C_i);
	}
    } else if (idio_S_int == t) {
	if (C_i > INT_MAX ||
	    C_i < INT_MIN) {
	    /*
	     * Test Case(s):
	     *
	     *  c-type-errors/c-type-integer2-int-range-min.idio
	     *  c-type-errors/c-type-integer2-int-range-max.idio
	     *
	     * C/integer-> ((C/->number libc/INT_MIN) - 1) 'int
	     * C/integer-> ((C/->number libc/INT_MAX) + 1) 'int
	     */
	    idio_C_conversion_error ("int: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_int ((int) C_i);
	}
    } else if (idio_S_uint == t) {
	if (C_i > UINT_MAX) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2-uint-range.idio
	     *
	     * C/integer-> ((C/->number libc/UINT_MAX) + 1) 'uint
	     */
	    idio_C_conversion_error ("uint: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_uint ((unsigned int) C_i);
	}
    } else if (idio_S_long == t) {
	if (C_i > LONG_MAX ||
	    C_i < LONG_MIN) {
	    /*
	     * Test Case(s): maybe on 32-bit
	     *
	     *  Technically:
	     *
	     *  c-type-errors/c-type-integer2-long-range-min.idio
	     *  c-type-errors/c-type-integer2-long-range-max.idio
	     *
	     * C/integer-> ((C/->number libc/LONG_MIN) - 1) 'long
	     * C/integer-> ((C/->number libc/LONG_MAX) + 1) 'long
	     *
	     * But in practice an intmax_t is a long on 64-bit systems
	     * and therefore we cannot test this.
	     */
	    idio_C_conversion_error ("long: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_long ((long) C_i);
	}
    } else if (idio_S_ulong == t) {
	if ((uintmax_t) C_i > ULONG_MAX) {
	    /*
	     * Test Case: maybe on 32-bit -- c-type-errors/c-type-integer2-ulong-range.idio
	     *
	     * C/integer-> ((C/->number libc/ULONG_MAX) + 1) 'ulong
	     *
	     * See note above.
	     */
	    idio_C_conversion_error ("ulong: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_ulong ((unsigned long) C_i);
	}
    } else if (idio_S_longlong == t) {
	if (C_i > LLONG_MAX ||
	    C_i < LLONG_MIN) {
	    /*
	     * Test Case(s): ??
	     *
	     * Not possible as an intmax_t is a long long int
	     * therefore we will have triggered the
	     * ^rt-bignum-conversion-error above.
	     *
	     * However, for reference.
	     *
	     *  c-type-errors/c-type-integer2-longlong-range-min.idio
	     *  c-type-errors/c-type-integer2-longlong-range-max.idio
	     *
	     * C/integer-> ((C/->number libc/LLONG_MIN) - 1) 'longlong
	     * C/integer-> ((C/->number libc/LLONG_MAX) + 1) 'longlong
	     */
	    idio_C_conversion_error ("longlong: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_longlong ((long long) C_i);
	}
    } else if (idio_S_ulonglong == t) {
	if ((uintmax_t) C_i > ULLONG_MAX) {
	    /*
	     * Test Case: ?? -- not c-type-errors/c-type-integer2-ulonglong-range.idio
	     *
	     * See above.
	     *
	     * C/integer-> ((C/->number libc/ULLONG_MAX) + 1) 'ulonglong
	     */
	    idio_C_conversion_error ("ulonglong: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_ulonglong ((unsigned long long) C_i);
	}
    } else {
	/*
	 * Test Case: c-type-errors/c-type-integer2-bad-fixnum-c-type.idio
	 *
	 * C/integer-> 1 'float
	 */
	idio_error_param_value_exp ("C/integer->", "type", t, "C integral type", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("integer->", C_integer_to, (IDIO i, IDIO args), "i [type]", "\
convert Idio integer `i` to a C integer		\n\
						\n\
If `i` is a fixnum then use `type` for		\n\
a more specific C type.				\n\
						\n\
An integer bignum is converted to a ``libc/intmax_t``.	\n\
						\n\
						\n\
:param i: Idio integer to convert		\n\
:type i: fixnum or bignum			\n\
:param type: C type to create, defaults to ``'int``	\n\
:type type: symbol, optional			\n\
:return: C integer				\n\
:rtype: according to `type`			\n\
:raises: ^rt-C-conversion-error:		\n\
						\n\
`type` defaults to ``'int`` and can be one of:	\n\
``'char``					\n\
``'schar``					\n\
``'uchar``					\n\
``'short``					\n\
``'ushort``					\n\
``'int``					\n\
``'uint``					\n\
``'long``					\n\
``'ulong``					\n\
``'longlong``					\n\
``'ulonglong``					\n\
or an alias thereof, eg. ``libc/pid_t``.	\n\
						\n\
`i` is range-checked for `type`			\n\
						\n\
``C/integer->`` is limited to a C ``intmax_t``,	\n\
see :ref:`C/integer->unsigned <C/integer->unsigned>`	\n\
")
{
    IDIO_ASSERT (i);

    if (idio_isa_C_integral (i)) {
	return i;
    }

    IDIO t = idio_S_int;

    if (idio_S_nil != args) {
	t = IDIO_PAIR_H (args);
    }

    return idio_C_integer2 (i, t);
}

IDIO idio_C_integer2unsigned (IDIO i, IDIO t)
{
    IDIO_ASSERT (i);
    IDIO_ASSERT (t);

    IDIO_TYPE_ASSERT (symbol, t);

    uintmax_t u;

    if (idio_isa_fixnum (i)) {
	intmax_t C_i = IDIO_FIXNUM_VAL (i);
	if (C_i < 0) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2unsigned-fixnum-negative.idio
	     *
	     * C/integer->unsigned -1
	     */
	    idio_error_param_value_msg ("C/integer->unsigned", "i", i, "should be a positive integer", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	u = C_i;
    } else if (idio_isa_integer_bignum (i)) {
	if (! IDIO_BIGNUM_INTEGER_P (i)) {
	    /*
	     * Annoyingly, idio_isa_integer_bignum() has probably just
	     * run idio_bignum_real_to_integer()...
	     */
	    i = idio_bignum_real_to_integer (i);
	}
	if (idio_bignum_negative_p (i)) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2unsigned-bignum-negative.idio
	     *
	     * C/integer->unsigned -1.0
	     */
	    idio_error_param_value_msg ("C/integer->unsigned", "i", i, "should be a positive integer", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	/*
	 * ALERT ALERT
	 *
	 * We'll get a ^rt-bignum-conversion-error from
	 * idio_bignum_uintmax_t_value() before several of the following
	 * tests fire!
	 *
	 * Indeed, by definition, for the ulong (64-bit systems) and
	 * ulonglong (all systems?) tests, we cannot represent a
	 * testable value in an uintmax_t.
	 *
	 * I've tried to annotate them below.
	 */
	u = idio_bignum_uintmax_t_value (i);
    } else {
	/*
	 * Test Case: c-type-errors/c-type-integer2unsigned-bad-type.idio
	 *
	 * C/integer->unsigned #t
	 */
	idio_error_param_type ("positive integer", i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (idio_S_uchar == t) {
	if (u > UCHAR_MAX) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2unsigned-uchar-range.idio
	     *
	     * C/integer->unsigned ((C/->number libc/UCHAR_MAX) + 1) 'uchar
	     */
	    idio_C_conversion_error ("uchar: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_uchar ((unsigned char) u);
	}
    } else if (idio_S_ushort == t) {
	if (u > USHRT_MAX) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2unsigned-ushort-range.idio
	     *
	     * C/integer->unsigned ((C/->number libc/USHRT_MAX) + 1) 'ushort
	     */
	    idio_C_conversion_error ("ushort: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_ushort ((unsigned short) u);
	}
    } else if (idio_S_uint == t) {
	if (u > UINT_MAX) {
	    /*
	     * Test Case: c-type-errors/c-type-integer2unsigned-uint-range.idio
	     *
	     * C/integer->unsigned ((C/->number libc/UINT_MAX) + 1) 'uint
	     */
	    idio_C_conversion_error ("uint: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_uint ((unsigned int) u);
	}
    } else if (idio_S_ulong == t) {
	if (u > ULONG_MAX) {
	    /*
	     * Test Case: maybe on 32-bit -- c-type-errors/c-type-integer2unsigned-ulong-range.idio
	     *
	     * C/integer->unsigned ((C/->number libc/ULONG_MAX) + 1) 'ulong
	     *
	     * See note above.
	     */
	    idio_C_conversion_error ("ulong: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_ulong ((unsigned long) u);
	}
    } else if (idio_S_ulonglong == t) {
	if (u > ULLONG_MAX) {
	    /*
	     * Test Case: ?? -- not c-type-errors/c-type-integer2unsigned-ulonglong-range.idio
	     *
	     * See above.
	     *
	     * C/integer->unsigned ((C/->number libc/ULLONG_MAX) + 1) 'ulonglong
	     */
	    idio_C_conversion_error ("ulonglong: range error", i, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return idio_C_ulonglong ((unsigned long long) u);
	}
    } else {
	/*
	 * Test Case: c-type-errors/c-type-integer2unsigned-bad-fixnum-c-type.idio
	 *
	 * C/integer->unsigned 1 'float
	 */
	idio_error_param_value_exp ("C/integer->unsigned", "type", t, "C unsigned integral type", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

}

IDIO_DEFINE_PRIMITIVE1V_DS ("integer->unsigned", C_integer_to_unsigned, (IDIO i, IDIO args), "i [C/unsigned type]", "\
convert Idio integer `i` to a C unsigned integer\n\
						\n\
If `i` is a fixnum then use `type` for		\n\
a more specific C type.				\n\
						\n\
An integer bignum is converted to a ``libc/uintmax_t``.	\n\
						\n\
:param i: Idio integer to convert		\n\
:type i: bignum or fixnum			\n\
:param type: C type to create, defaults to ``'uint``	\n\
:type type: symbol, optional			\n\
:return: C unsigned integer			\n\
:rtype: according to `type`			\n\
:raises: ^rt-C-conversion-error:		\n\
						\n\
`type` defaults to ``'uint`` and can be one of:	\n\
``'uchar``					\n\
``'ushort``					\n\
``'uint``					\n\
``'ulong``					\n\
``'ulonglong``					\n\
or an alias thereof, eg. ``libc/size_t``.	\n\
						\n\
`i` is range-checked for `type`			\n\
						\n\
``C/integer->unsigned`` is limited to a C ``uintmax_t``	\n\
")
{
    IDIO_ASSERT (i);

    if (idio_isa_C_unsigned (i)) {
	return i;
    }

    IDIO t = idio_S_uint;

    if (idio_S_nil != args) {
	t = IDIO_PAIR_H (args);
    }

    return idio_C_integer2unsigned (i, t);
}

IDIO_DEFINE_PRIMITIVE1_DS ("->number", C_to_number, (IDIO i), "i", "\
convert C number `i` to an Idio number	\n\
						\n\
:param o: C number to convert			\n\
:type o: a C/ type				\n\
:return: Idio number				\n\
:rtype: number					\n\
						\n\
supported C types are:				\n\
``C/char``					\n\
``C/schar``					\n\
``C/uchar``					\n\
``C/short``					\n\
``C/ushort``					\n\
``C/int``					\n\
``C/uint``					\n\
``C/long``					\n\
``C/ulong``					\n\
``C/longlong``					\n\
``C/ulonglong``					\n\
``C/float``					\n\
``C/double``					\n\
or an alias thereof, eg. ``libc/size_t``.	\n\
						\n\
the following types are NOT supported:		\n\
``C/longdouble``				\n\
")
{
    IDIO_ASSERT (i);

    if (idio_isa_C_char (i)) {
	return idio_integer (IDIO_C_TYPE_char (i));
    } else if (idio_isa_C_schar (i)) {
	return idio_integer (IDIO_C_TYPE_schar (i));
    } else if (idio_isa_C_uchar (i)) {
	return idio_uinteger (IDIO_C_TYPE_uchar (i));
    } else if (idio_isa_C_short (i)) {
	return idio_integer (IDIO_C_TYPE_short (i));
    } else if (idio_isa_C_ushort (i)) {
	return idio_uinteger (IDIO_C_TYPE_ushort (i));
    } else if (idio_isa_C_int (i)) {
	return idio_integer (IDIO_C_TYPE_int (i));
    } else if (idio_isa_C_uint (i)) {
	return idio_uinteger (IDIO_C_TYPE_uint (i));
    } else if (idio_isa_C_long (i)) {
	return idio_integer (IDIO_C_TYPE_long (i));
    } else if (idio_isa_C_ulong (i)) {
	return idio_uinteger (IDIO_C_TYPE_ulong (i));
    } else if (idio_isa_C_longlong (i)) {
	return idio_integer (IDIO_C_TYPE_longlong (i));
    } else if (idio_isa_C_ulonglong (i)) {
	return idio_uinteger (IDIO_C_TYPE_ulonglong (i));
    } else if (idio_isa_C_float (i)) {
	return idio_bignum_float (IDIO_C_TYPE_float (i));
    } else if (idio_isa_C_double (i)) {
	return idio_bignum_double (IDIO_C_TYPE_double (i));
    } else if (idio_isa_C_longdouble (i)) {
	/*
	 * Test Case: c-type-errors/c-type-2number-long-double.idio
	 *
	 * C/->number (C/number-> 123.456 'longdouble)
	 */
	idio_error_param_type_msg ("C/->number for C long double is not supported", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
	return idio_bignum_longdouble (IDIO_C_TYPE_longdouble (i));
    } else {
	/*
	 * Test Case: c-type-errors/c-type-2number-bad-type.idio
	 *
	 * C/->number #t
	 */
	idio_error_param_type ("C number type", i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2_DS ("number->", C_number_to, (IDIO n, IDIO t), "n type", "\
convert Idio number `n` to a C number using `type`	\n\
for the specific C type.				\n\
						\n\
bignums must be a C floating type		\n\
						\n\
:param n: Idio number to convert		\n\
:type n: fixnum or bignum			\n\
:param type: C type to create			\n\
:type type: symbol				\n\
:return: C number				\n\
:rtype: according to `type`			\n\
:raises: ^rt-C-conversion-error:		\n\
						\n\
`type` can be one of:				\n\
``'char``					\n\
``'schar``					\n\
``'uchar``					\n\
``'short``					\n\
``'ushort``					\n\
``'int``					\n\
``'uint``					\n\
``'long``					\n\
``'ulong``					\n\
``'longlong``					\n\
``'ulonglong``					\n\
``'float``					\n\
``'double``					\n\
``'longdouble``					\n\
or an alias thereof, eg. ``libc/size_t``.	\n\
						\n\
`n` is range-checked for integral `type`\\ s	\n\
						\n\
Where `type` is:				\n\
						\n\
* signed, :ref:`C/integer-> <C/integer-\\>>` is called,		\n\
* unsigned, :ref:`C/integer->unsigned <C/integer->unsigned>` is called,	\n\
						\n\
.. warning:: If `n` is a bignum then `type` can	\n\
	only be a floating point type.		\n\
")
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (t);

    /*
     * Test Case: c-type-errors/c-type-number2-bad-type-type.idio
     *
     * C/number-> #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, t);

    if (idio_isa_fixnum (n)) {
	if (idio_S_char == t) {
	    return idio_C_char ((char) IDIO_FIXNUM_VAL (n));
	} else 	if (idio_S_schar == t ||
		    idio_S_short == t ||
		    idio_S_int == t ||
		    idio_S_long == t ||
		    idio_S_longlong == t) {
	    return idio_C_integer2 (n, t);
	} else 	if (idio_S_uchar == t ||
		    idio_S_ushort == t ||
		    idio_S_uint == t ||
		    idio_S_ulong == t ||
		    idio_S_ulonglong == t) {
	    return idio_C_integer2unsigned (n, t);
	} else 	if (idio_S_float == t) {
	    return idio_C_float ((float) IDIO_FIXNUM_VAL (n));
	} else 	if (idio_S_double == t) {
	    return idio_C_double ((double) IDIO_FIXNUM_VAL (n));
	} else 	if (idio_S_longdouble == t) {
	    return idio_C_longdouble ((long double) IDIO_FIXNUM_VAL (n));
	} else {
	    /*
	     * Test Case: c-type-errors/c-type-number2-bad-fixnum-c-type.idio
	     *
	     * C/number-> 1 'number
	     */
	    idio_error_param_value_exp ("number->", "type", t, "C integral type", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_isa_bignum (n)) {
	if (idio_S_float == t) {
	    return idio_C_float (idio_bignum_float_value (n));
	} else 	if (idio_S_double == t) {
	    return idio_C_double (idio_bignum_double_value (n));
	} else 	if (idio_S_longdouble == t) {
	    return idio_C_longdouble (idio_bignum_longdouble_value (n));
	} else {
	    /*
	     * Test Case: c-type-errors/c-type-number2-bad-bignum-c-type.idio
	     *
	     * C/number-> 1.0 'int
	     */
	    idio_error_param_value_exp ("number->", "type", t, "C floating type", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: c-type-errors/c-type-number2-bad-type.idio
	 *
	 * C/number-> #t 'int
	 */
	idio_error_param_type ("number", n, IDIO_C_FUNC_LOCATION ());
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("|", C_bw_ior, (IDIO v1, IDIO args), "v1 [...]", "\
perform a C bitwise-OR on `v1` etc.	\n\
					\n\
:param v1: C integer			\n\
:type v1: C/int				\n\
:return: C integer			\n\
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
:param v1: C integer			\n\
:type v1: C/int				\n\
:return: C integer			\n\
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
:param v1: C integer			\n\
:type v1: C/int				\n\
:return: C integer			\n\
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
:param v1: C integer			\n\
:type v1: C/int				\n\
:return: C integer			\n\
:rtype: C/int				\n\
")
{
    IDIO_ASSERT (v1);
    IDIO_USER_C_TYPE_ASSERT (int, v1);

    int v = IDIO_C_TYPE_int (v1);

    return idio_C_int (~ v);
}

char *idio_C_char_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (C_char, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "%c", IDIO_C_TYPE_char (v));

    return r;
}

IDIO idio_C_char_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_C_char_as_C_string (v, sizep, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

char *idio_C_number_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (C_number, v);

    char *r = NULL;

    int type = idio_type (v);

    /*
     * Printing C types is a little more intricate than at first
     * blush.  Not because calling sprintf(3) is hard but because the
     * chances are we've been called from %format which has
     * potentially set the conversion precision and specifier.
     *
     * The conversion specifier is handled below.
     *
     * For integral types (and 'char, above) the conversion precision
     * is delegated back to %format as it will be applied to the
     * string that we return.
     *
     * For floating point values that doesn't work as %format just has
     * a string in its hands and the precision affects the significant
     * figures after the decimal point so we need to figure out the
     * precision here.
     */

    /*
     * Broadly, we gradually build a format specification in {fmt}
     * based on whatever is relevant before a final switch statement
     * which invokes that format with the type-specific accessor.
     *
     * {fmt} needs to be big enough to hold the largest format string
     * we need.  The conversion precision is a fixnum which can reach
     * 19 digits and be negative giving something like "%.-{19}le" or
     * 24 characters.
     *
     * 30 should cover it!
     */
    char fmt[30];
    size_t fmt_len = 0;
    switch (type) {
    case IDIO_TYPE_C_SCHAR:
    case IDIO_TYPE_C_UCHAR:
	fmt_len = 3;
	memcpy (fmt, "%hh", fmt_len);
	break;
    case IDIO_TYPE_C_SHORT:
    case IDIO_TYPE_C_USHORT:
	fmt_len = 2;
	memcpy (fmt, "%h", fmt_len);
	break;
    case IDIO_TYPE_C_INT:
    case IDIO_TYPE_C_UINT:
	fmt_len = 1;
	memcpy (fmt, "%", fmt_len);
	break;
    case IDIO_TYPE_C_LONG:
    case IDIO_TYPE_C_ULONG:
	fmt_len = 2;
	memcpy (fmt, "%l", fmt_len);
	break;
    case IDIO_TYPE_C_LONGLONG:
    case IDIO_TYPE_C_ULONGLONG:
	fmt_len = 3;
	memcpy (fmt, "%ll", fmt_len);
	break;
    }

    int printed = 0;
    switch (type) {
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
	switch (type) {
	case IDIO_TYPE_C_SCHAR:
	case IDIO_TYPE_C_SHORT:
	case IDIO_TYPE_C_INT:
	case IDIO_TYPE_C_LONG:
	case IDIO_TYPE_C_LONGLONG:
	    switch (format) {
	    case IDIO_PRINT_CONVERSION_FORMAT_d:
		memcpy (fmt + fmt_len++, "d", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_s:
		/*
		 * A generic: printf "%s" e
		 */
		memcpy (fmt + fmt_len++, "d", 1);
		break;
	    default:
		/*
		 * Code coverage:
		 *
		 * format "%4q" 10		; "  10"
		 */
#ifdef IDIO_DEBUG
		fprintf (stderr, "signed C number as-string: unexpected conversion format: '%c' (%#x).  Using 'd'.\n", (int) format, (int) format);
#endif
		memcpy (fmt + fmt_len++, "d", 1);
		break;
	    }
	    break;
	case IDIO_TYPE_C_UCHAR:
	case IDIO_TYPE_C_USHORT:
	case IDIO_TYPE_C_UINT:
	case IDIO_TYPE_C_ULONG:
	case IDIO_TYPE_C_ULONGLONG:
	    switch (format) {
	    case IDIO_PRINT_CONVERSION_FORMAT_X:
		memcpy (fmt + fmt_len++, "X", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_o:
		memcpy (fmt + fmt_len++, "o", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_u:
		memcpy (fmt + fmt_len++, "u", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_x:
		memcpy (fmt + fmt_len++, "x", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_b:
		{
		    char bits[sizeof (uintmax_t) * CHAR_BIT + 1];
		    uintmax_t ui;
		    int j;
		    switch (type) {
		    case IDIO_TYPE_C_UCHAR:
			ui = (uintmax_t) IDIO_C_TYPE_uchar (v);
			j = sizeof (unsigned char) * CHAR_BIT;
			break;
		    case IDIO_TYPE_C_USHORT:
			ui = (uintmax_t) IDIO_C_TYPE_ushort (v);
			j = sizeof (unsigned short) * CHAR_BIT;
			break;
		    case IDIO_TYPE_C_UINT:
			ui = (uintmax_t) IDIO_C_TYPE_uint (v);
			j = sizeof (unsigned int) * CHAR_BIT;
			break;
		    case IDIO_TYPE_C_ULONG:
			ui = (uintmax_t) IDIO_C_TYPE_ulong (v);
			j = sizeof (unsigned long) * CHAR_BIT;
			break;
		    case IDIO_TYPE_C_ULONGLONG:
			ui = (uintmax_t) IDIO_C_TYPE_ulonglong (v);
			j = sizeof (unsigned long long) * CHAR_BIT;
			break;
		    default:
			fprintf (stderr, "unexpected type=%#x\n", type);
			ui = 0;
			j = 0;
			break;
		    }
		    int seen_bit = 0;
		    for (int i = 0; i < j; i++) {
			if (ui & (1UL << (j - 1 - i))) {
			    bits[i] = '1';
			    if (0 == seen_bit) {
				seen_bit = i;
			    }
			} else {
			    bits[i] = '0';
			}
		    }
		    bits[j] = '\0';
		    *sizep = idio_asprintf (&r, "%s", bits + seen_bit);
		    printed = 1;
		}
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_s:
		/*
		 * A generic: printf "%s" e
		 */
		memcpy (fmt + fmt_len++, "u", 1);
		break;
	    default:
		/*
		 * Code coverage:
		 *
		 * format "%4q" 10		; "  10"
		 */
#ifdef IDIO_DEBUG
		fprintf (stderr, "unsigned C number as-string: unexpected conversion format: '%c' (%#x).  Using 'u'.\n", (int) format, (int) format);
#endif
		memcpy (fmt + fmt_len++, "u", 1);
		break;
	    }
	    break;
	}
	break;
    case IDIO_TYPE_C_FLOAT:
    case IDIO_TYPE_C_DOUBLE:
    case IDIO_TYPE_C_LONGDOUBLE:
	{
	    /*
	     * The default precision for both e, f and
	     * g formats is 6
	     */
	    int prec = 6;
	    if (idio_S_nil != idio_print_conversion_precision_sym) {
		IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
						      idio_Idio_module,
						      IDIO_LIST1 (idio_S_false));

		if (idio_S_false != ipcp) {
		    if (idio_isa_fixnum (ipcp)) {
			prec = IDIO_FIXNUM_VAL (ipcp);
		    } else {
			/*
			 * Test Case: ??
			 *
			 * If we set idio-print-conversion-precision to
			 * something not a fixnum (nor #f) then it affects
			 * *everything* in the codebase that uses
			 * idio-print-conversion-precision before we get here.
			 */
			idio_error_param_type ("fixnum", ipcp, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }
		}
	    }
	    fmt_len = idio_snprintf (fmt, 30, "%%.%d", prec);

	    switch (type) {
	    case IDIO_TYPE_C_LONGDOUBLE:
		memcpy (fmt + fmt_len++, "L", 1);
		break;
	    }

	    switch (format) {
	    case IDIO_PRINT_CONVERSION_FORMAT_e:
		memcpy (fmt + fmt_len++, "e", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_f:
		memcpy (fmt + fmt_len++, "f", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_g:
		memcpy (fmt + fmt_len++, "g", 1);
		break;
	    case IDIO_PRINT_CONVERSION_FORMAT_s:
		/*
		 * A generic: printf "%s" e
		 */
		memcpy (fmt + fmt_len++, "g", 1);
		break;
	    default:
		/*
		 * Code coverage:
		 *
		 * format "%4q" 10		; "  10"
		 */
#ifdef IDIO_DEBUG
		fprintf (stderr, "as-string: unexpected conversion format: '%c' (%#x).  Using 'g'.\n", (int) format, (int) format);
#endif
		memcpy (fmt + fmt_len++, "g", 1);
		break;
	    }
	}
	break;
    }
    fmt[fmt_len] = '\0';

    if (! printed) {
	switch (type) {
	case IDIO_TYPE_C_SCHAR:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_schar (v));
	    break;
	case IDIO_TYPE_C_UCHAR:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_uchar (v));
	    break;
	case IDIO_TYPE_C_SHORT:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_short (v));
	    break;
	case IDIO_TYPE_C_USHORT:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_ushort (v));
	    break;
	case IDIO_TYPE_C_INT:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_int (v));
	    break;
	case IDIO_TYPE_C_UINT:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_uint (v));
	    break;
	case IDIO_TYPE_C_LONG:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_long (v));
	    break;
	case IDIO_TYPE_C_ULONG:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_ulong (v));
	    break;
	case IDIO_TYPE_C_LONGLONG:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_longlong (v));
	    break;
	case IDIO_TYPE_C_ULONGLONG:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_ulonglong (v));
	    break;
	case IDIO_TYPE_C_FLOAT:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_float (v));
	    break;
	case IDIO_TYPE_C_DOUBLE:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_double (v));
	    break;
	case IDIO_TYPE_C_LONGDOUBLE:
	    *sizep = idio_asprintf (&r, fmt, IDIO_C_TYPE_longdouble (v));
	    break;
	}
    }

    return r;
}

IDIO idio_C_number_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    IDIO seen = va_arg (ap, IDIO);
    int depth = va_arg (ap, int);
    int first = va_arg (ap, int);
    va_end (ap);

    idio_unicode_t format = IDIO_PRINT_CONVERSION_FORMAT_s;
    int type = idio_type (v);

    switch (type) {
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
    default:
	idio_error_param_value_msg_only ("C-as-string", "value", "should be a C/ type", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (first) {
	/*
	 * NB There's little point in us doing verification checks on
	 * format here as the function we're about to call has to do
	 * those checks just in case it was called with duff values
	 * directly.
	 */
	format = idio_util_string_format (format);
    }

    char *C_r = idio_C_number_as_C_string (v, sizep, format, seen, depth);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

IDIO idio_C_pointer_method_2string (idio_vtable_method_t *m, IDIO v, ...);

char *idio_C_pointer_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (C_pointer, v);

    idio_vtable_method_t *m = idio_vtable_lookup_method (idio_value_vtable (v), v, idio_S_typename, 1);

    IDIO typename = IDIO_VTABLE_METHOD_FUNC (m) (m, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#<C/*");

    if (idio_isa_symbol (typename)) {
	char *t;
	size_t t_size = idio_asprintf (&t, " %s", IDIO_SYMBOL_S (typename));
	IDIO_STRCAT_FREE (r, sizep, t, t_size);
    }

    IDIO_STRCAT (r, sizep, ">");

    return r;
}

char *idio_C_pointer_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (C_pointer, v);

    /*
     * Prefer a ->string method
     */
    idio_vtable_method_t *m = idio_vtable_flat_lookup_method (idio_value_vtable (v), v, idio_S_2string, 0);

    /*
     * We can be called from idio_util_string (from almost anywhere in
     * the C code base) and by the generic ->string method,
     * idio_C_pointer_method_2string(), below.
     *
     * We want to avoid recursing if it is the latter.
     */
    if (NULL != m &&
	idio_C_pointer_method_2string != IDIO_VTABLE_METHOD_FUNC (m)) {
	IDIO s = IDIO_VTABLE_METHOD_FUNC (m) (m, v, sizep, seen, depth);

	if (idio_isa_string (s)) {
	    return idio_utf8_string (s, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED, IDIO_UTF8_STRING_NOPREC);
	} else if (0 == idio_vm_reporting) {
	    /*
	     * Test Case: util-errors/C-pointer-printer-bad-return-type.idio
	     *
	     * ... return #t
	     */
#ifdef IDIO_DEBUG
	    idio_debug ("C/pointer printer => %s (not a STRING)\n", s);
#endif
	    idio_error_param_value_msg ("C/pointer-as-string", "C/pointer printer", s, "should return a string", IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}
    }

    return idio_C_pointer_report_string (v, sizep, format, seen, depth);
}

IDIO idio_C_pointer_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_C_pointer_as_C_string (v, sizep, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_c_type_add_primitives ()
{
    /*
     * NB As "<", "==", ">" etc. conflict with operators or primitives
     * in the Idio module then you should access these directly as
     * "C/>", "C/==", etc..
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
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_pointer_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_pointer_members);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_typep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_numberp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_integralp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_unsignedp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_floatingp);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_le);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_lt);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_eq);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ne);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_ge);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_gt);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_to_integer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_integer_to);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_integer_to_unsigned);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_to_number);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_number_to);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_ior);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_and);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_xor);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_C_module, C_bw_complement);
}

void idio_init_c_type ()
{
    idio_module_table_register (idio_c_type_add_primitives, NULL, NULL);

    idio_C_module = idio_module (IDIO_SYMBOLS_C_INTERN ("C"));

    idio_module_export_symbol_value (IDIO_SYMBOLS_C_INTERN ("0u"), idio_C_uint (0U), idio_C_module);
    idio_module_export_symbol_value (IDIO_SYMBOLS_C_INTERN ("0i"), idio_C_int (0), idio_C_module);

    idio_vtable_t *Cchar_vt        = idio_vtable (IDIO_TYPE_C_CHAR);
    idio_vtable_t *Cschar_vt       = idio_vtable (IDIO_TYPE_C_SCHAR);
    idio_vtable_t *Cuchar_vt       = idio_vtable (IDIO_TYPE_C_UCHAR);
    idio_vtable_t *Cshort_vt       = idio_vtable (IDIO_TYPE_C_SHORT);
    idio_vtable_t *Cushort_vt      = idio_vtable (IDIO_TYPE_C_USHORT);
    idio_vtable_t *Cint_vt         = idio_vtable (IDIO_TYPE_C_INT);
    idio_vtable_t *Cuint_vt        = idio_vtable (IDIO_TYPE_C_UINT);
    idio_vtable_t *Clong_vt        = idio_vtable (IDIO_TYPE_C_LONG);
    idio_vtable_t *Culong_vt       = idio_vtable (IDIO_TYPE_C_ULONG);
    idio_vtable_t *Clonglong_vt    = idio_vtable (IDIO_TYPE_C_LONGLONG);
    idio_vtable_t *Culonglong_vt   = idio_vtable (IDIO_TYPE_C_ULONGLONG);
    idio_vtable_t *Cfloat_vt       = idio_vtable (IDIO_TYPE_C_FLOAT);
    idio_vtable_t *Cdouble_vt      = idio_vtable (IDIO_TYPE_C_DOUBLE);
    idio_vtable_t *Clongdouble_vt  = idio_vtable (IDIO_TYPE_C_LONGDOUBLE);
    idio_vtable_t *Cpointer_vt     = idio_vtable (IDIO_TYPE_C_POINTER);
    idio_vtable_t *Cvoid_vt        = idio_vtable (IDIO_TYPE_C_VOID);

    idio_vtable_add_method (Cchar_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_char));

    idio_vtable_add_method (Cchar_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_char_method_2string));

    idio_vtable_add_method (Cschar_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_schar));

    idio_vtable_add_method (Cschar_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cuchar_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_uchar));

    idio_vtable_add_method (Cuchar_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cshort_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_short));

    idio_vtable_add_method (Cshort_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cushort_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_ushort));

    idio_vtable_add_method (Cushort_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cint_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_int));

    idio_vtable_add_method (Cint_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cuint_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_uint));

    idio_vtable_add_method (Cuint_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Clong_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_long));

    idio_vtable_add_method (Clong_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Culong_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_ulong));

    idio_vtable_add_method (Culong_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Clonglong_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_longlong));

    idio_vtable_add_method (Clonglong_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Culonglong_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_ulonglong));

    idio_vtable_add_method (Culonglong_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cfloat_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_float));

    idio_vtable_add_method (Cfloat_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cdouble_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_double));

    idio_vtable_add_method (Cdouble_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Clongdouble_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_longdouble));

    idio_vtable_add_method (Clongdouble_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_number_method_2string));

    idio_vtable_add_method (Cpointer_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_pointer));

    idio_vtable_add_method (Cpointer_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_C_pointer_method_2string));

    idio_C_pointer_type_vtables = IDIO_HASH_EQP (10);
    idio_gc_protect_auto (idio_C_pointer_type_vtables);

    idio_vtable_add_method (Cvoid_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_c_void));

    idio_vtable_add_method (Cvoid_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_util_method_2string));
}
