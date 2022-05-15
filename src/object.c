/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * object.c
 *
 * IOS is derived from Gregor Kiczales' Tiny CLOS with ideas from
 * Erick Gallesio's STklos and Eli Barzilay's Swindle
 *
 * The Tiny CLOS copyright is:

; **********************************************************************
; Copyright (c) 1992 Xerox Corporation.
; All Rights Reserved.
;
; Use, reproduction, and preparation of derivative works are permitted.
; Any copy of this software or of any derivative work must include the
; above copyright notice of Xerox Corporation, this paragraph and the
; one after it.  Any distribution of this software or derivative works
; must comply with all applicable United States export control laws.
;
; This software is made available AS IS, and XEROX CORPORATION DISCLAIMS
; ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE
; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
; PURPOSE, AND NOTWITHSTANDING ANY OTHER PROVISION CONTAINED HEREIN, ANY
; LIABILITY FOR DAMAGES RESULTING FROM THE SOFTWARE OR ITS USE IS
; EXPRESSLY DISCLAIMED, WHETHER ARISING IN CONTRACT, TORT (INCLUDING
; NEGLIGENCE) OR STRICT LIABILITY, EVEN IF XEROX CORPORATION IS ADVISED
; OF THE POSSIBILITY OF SUCH DAMAGES.
; **********************************************************************

 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "bignum.h"
#include "c-type.h"
#include "closure.h"
#include "command.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "keyword.h"
#include "libc-wrap.h"
#include "module.h"
#include "object.h"
#include "pair.h"
#include "path.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vars.h"
#include "vm.h"
#include "vtable.h"

#include "libc-api.h"

IDIO idio_object_module = idio_S_nil;
IDIO idio_class_struct_type;
IDIO idio_class_sym;
IDIO idio_class_class_sym;
IDIO idio_class_slot_names;
IDIO idio_class_inst;
IDIO idio_top_inst;
IDIO idio_object_inst;
IDIO idio_generic_sym;
IDIO idio_generic_class_sym;
IDIO idio_generic_inst;
IDIO idio_method_sym;
IDIO idio_method_class_sym;
IDIO idio_method_inst;
IDIO idio_fixnum_inst;
IDIO idio_constant_inst;
IDIO idio_constant_token_inst;
IDIO idio_constant_i_code_inst;
IDIO idio_unicode_inst;
IDIO idio_PLACEHOLDER_inst;
IDIO idio_string_inst;
IDIO idio_substring_inst;
IDIO idio_symbol_inst;
IDIO idio_keyword_inst;
IDIO idio_pair_inst;
IDIO idio_array_inst;
IDIO idio_hash_inst;
IDIO idio_closure_inst;
IDIO idio_primitive_inst;
IDIO idio_bignum_inst;
IDIO idio_module_inst;
IDIO idio_frame_inst;
IDIO idio_handle_inst;
IDIO idio_C_char_inst;
IDIO idio_C_schar_inst;
IDIO idio_C_uchar_inst;
IDIO idio_C_short_inst;
IDIO idio_C_ushort_inst;
IDIO idio_C_int_inst;
IDIO idio_C_uint_inst;
IDIO idio_C_long_inst;
IDIO idio_C_ulong_inst;
IDIO idio_C_longlong_inst;
IDIO idio_C_ulonglong_inst;
IDIO idio_C_float_inst;
IDIO idio_C_double_inst;
IDIO idio_C_longdouble_inst;
IDIO idio_C_pointer_inst;
IDIO idio_struct_type_inst;
IDIO idio_struct_instance_inst;
IDIO idio_thread_inst;
IDIO idio_continuation_inst;
IDIO idio_bitset_inst;

static IDIO idio_object_invoke_instance_in_error;
static IDIO idio_object_invoke_entity_in_error;
static IDIO idio_object_default_slot_value;

static void idio_object_instance_error (char *msg, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_instance_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_object_instance_invocation_error (char *msg, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_instance_invocation_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_object_slot_not_found_error (IDIO obj, IDIO cl, IDIO slot, IDIO c_location)
{
    IDIO_ASSERT (obj);
    IDIO_ASSERT (cl);
    IDIO_ASSERT (slot);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (instance, obj);
    IDIO_TYPE_ASSERT (class, cl);
    IDIO_TYPE_ASSERT (symbol, slot);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("slot ", msh);
    idio_display (slot, msh);
    idio_display_C (" not found in class ", msh);
    idio_display (cl, msh);

    idio_error_raise_cont (idio_condition_rt_slot_not_found_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       IDIO_LIST3 (obj, cl, slot)));

    /* notreached */
}

static void idio_dump_instance (IDIO o);

/*
 * We need some base functions for bootstrap that, if invoked, say
 * "you've done the wrong thing".
 */
IDIO_DEFINE_PRIMITIVE0V_DS ("invoke-instance-in-error", invoke_instance_in_error, (IDIO args), "[args]", "\
raise an error condition			\n\
						\n\
:raises ^rt-instance-invocation-error:		\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: ??
     *
     * I think we need a partially-constructed instance to get here.
     */
    idio_object_instance_invocation_error ("an instance isn't a procedure -- can't apply it", IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("invoke-entity-in-error", invoke_entity_in_error, (IDIO args), "[args]", "\
raise an error condition			\n\
						\n\
:raises ^rt-instance-invocation-error:		\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: ??
     *
     * I think we need a partially-constructed instance to get here.
     */
    idio_object_instance_invocation_error ("tried to call an entity before its proc is set", IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

/*
 * We need a default-value function for init-func in getters-n-setters
 */
IDIO_DEFINE_PRIMITIVE0_DS ("default-slot-value", default_slot_value, (void), "", "\
return a default slot value			\n\
						\n\
:return: ``#f``					\n\
:rtype: boolean					\n\
")
{
    return idio_S_false;
}

static IDIO idio_allocate_instance (IDIO cl, size_t nfields)
{
    IDIO_ASSERT (cl);

    /*
     * There should be a vanilla IDIO_TYPE_ASSERT (class, cl) but the
     * bootstrap calls us with #f and then not quite right classes.
     *
     * IDIO_TYPE_ASSERT() is only useful with IDIO_DEBUG
     */

    /*
     * cl will be #f when bootstrapping <class>
     */
    IDIO inst = idio_allocate_struct_instance_size (idio_class_struct_type, IDIO_CLASS_ST_MAX + nfields, 1);
    idio_struct_instance_set_direct (inst, IDIO_CLASS_ST_CLASS, cl);
    idio_struct_instance_set_direct (inst, IDIO_CLASS_ST_PROC, idio_object_invoke_instance_in_error);

    return inst;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%allocate-instance", allocate_instance, (IDIO cl, IDIO nfields), "cl nfields", "\
primitive allocator of an instance		\n\
						\n\
:param cl: class				\n\
:type cl: instance				\n\
:param nfields: number of fields		\n\
:type nfields: non-negative integer		\n\
:return: instance				\n\
:rtype: instance				\n\
")
{
    IDIO_ASSERT (cl);
    IDIO_ASSERT (nfields);

    /*
     * Test Case: object-errors/allocate-instance-bad-class-type.idio
     *
     * %allocate-instance #t #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    /*
     * Test Case: object-errors/allocate-instance-bad-nfields-type.idio
     *
     * %allocate-instance <class> #t
     */
    IDIO_USER_TYPE_ASSERT (integer, nfields);

    ssize_t C_nfields = -1;
    if (idio_isa_fixnum (nfields)) {
	C_nfields = IDIO_FIXNUM_VAL (nfields);
    } else if (idio_isa_bignum (nfields)) {
	if (IDIO_BIGNUM_INTEGER_P (nfields)) {
	    C_nfields = idio_bignum_ptrdiff_t_value (nfields);
	} else {
	    IDIO i = idio_bignum_real_to_integer (nfields);
	    if (idio_S_nil != i) {
		C_nfields = idio_bignum_ptrdiff_t_value (i);
	    }
	}
    }

    /*
     * Test Case: object-errors/allocate-instance-bad-nfields-value.idio
     *
     * %allocate-instance <class> -1
     */
    if (C_nfields < 0) {
	idio_error_param_value_msg ("%allocate-instance", "nfields", nfields, "should be non-negative", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_allocate_instance (cl, C_nfields);
}

static IDIO idio_allocate_entity (IDIO cl, size_t nfields)
{
    IDIO_ASSERT (cl);

    IDIO_TYPE_ASSERT (class, cl);

    IDIO ent = idio_allocate_struct_instance_size (idio_class_struct_type, IDIO_CLASS_ST_MAX + nfields, 1);
    idio_struct_instance_set_direct (ent, IDIO_CLASS_ST_CLASS, cl);
    idio_struct_instance_set_direct (ent, IDIO_CLASS_ST_PROC, idio_object_invoke_entity_in_error);

    return ent;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%allocate-entity", allocate_entity, (IDIO cl, IDIO nfields), "cl nfields", "\
primitive allocator of an entity		\n\
						\n\
:param cl: class				\n\
:type cl: instance				\n\
:param nfields: number of fields		\n\
:type nfields: non-negative integer		\n\
:return: instance				\n\
:rtype: instance				\n\
")
{
    IDIO_ASSERT (cl);
    IDIO_ASSERT (nfields);

    /*
     * Test Case: object-errors/allocate-entity-bad-class-type.idio
     *
     * %allocate-entity #t #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    /*
     * Test Case: object-errors/allocate-entity-bad-nfields-type.idio
     *
     * %allocate-entity <class> #t
     */
    IDIO_USER_TYPE_ASSERT (integer, nfields);

    ssize_t C_nfields = -1;
    if (idio_isa_fixnum (nfields)) {
	C_nfields = IDIO_FIXNUM_VAL (nfields);
    } else if (idio_isa_bignum (nfields)) {
	if (IDIO_BIGNUM_INTEGER_P (nfields)) {
	    C_nfields = idio_bignum_ptrdiff_t_value (nfields);
	} else {
	    IDIO i = idio_bignum_real_to_integer (nfields);
	    if (idio_S_nil != i) {
		C_nfields = idio_bignum_ptrdiff_t_value (i);
	    }
	}
    }

    /*
     * Test Case: object-errors/allocate-entity-bad-nfields-value.idio
     *
     * %allocate-entity <class> -1
     */
    if (C_nfields < 0) {
	idio_error_param_value_msg ("%allocate-entity", "nfields", nfields, "should be non-negative", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_allocate_entity (cl, C_nfields);
}

int idio_isa_instance (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_struct_instance (o)) {
	return idio_struct_instance_isa (o, idio_class_struct_type);
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("instance?", instancep, (IDIO o), "o", "\
test if `o` is a instance			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a instance, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_instance (o)) {
	r = idio_S_true;
    }

    return r;
}

static int idio_isa_class (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_instance (o)) {
	IDIO coo = idio_struct_instance_ref_direct (o, IDIO_CLASS_ST_CLASS);
	IDIO_TYPE_ASSERT (struct_instance, coo);

	IDIO coo_cpl = idio_struct_instance_ref_direct (coo, IDIO_CLASS_SLOT_CPL);
	IDIO r = idio_list_memq (idio_class_inst, coo_cpl);
	if (idio_S_false != r) {
	    return 1;
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("class?", classp, (IDIO o), "o", "\
test if `o` is a class				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a class, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_class (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_generic (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_instance (o)) {
	IDIO coo = idio_struct_instance_ref_direct (o, IDIO_CLASS_ST_CLASS);
	IDIO_TYPE_ASSERT (struct_instance, coo);

	IDIO coo_cpl = idio_struct_instance_ref_direct (coo, IDIO_CLASS_SLOT_CPL);
	IDIO r = idio_list_memq (idio_generic_inst, coo_cpl);
	if (idio_S_false != r) {
	    return 1;
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("generic?", genericp, (IDIO o), "o", "\
test if `o` is a generic			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a generic, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_generic (o)) {
	r = idio_S_true;
    }

    return r;
}

static int idio_isa_method (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_instance (o)) {
	IDIO coo = idio_struct_instance_ref_direct (o, IDIO_CLASS_ST_CLASS);
	IDIO_TYPE_ASSERT (struct_instance, coo);

	IDIO coo_cpl = idio_struct_instance_ref_direct (coo, IDIO_CLASS_SLOT_CPL);
	IDIO r = idio_list_memq (idio_method_inst, coo_cpl);
	if (idio_S_false != r) {
	    return 1;
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("method?", methodp, (IDIO o), "o", "\
test if `o` is a method				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a method, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_method (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_object_class_of (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_instance (o)) {
	return idio_struct_instance_ref_direct (o, IDIO_CLASS_ST_CLASS);
    }

    int type = idio_type (o);

    switch (type) {
    case IDIO_TYPE_FIXNUM:		return idio_fixnum_inst;
    case IDIO_TYPE_CONSTANT_IDIO:	return idio_constant_inst;
    case IDIO_TYPE_CONSTANT_TOKEN:	return idio_constant_token_inst;
    case IDIO_TYPE_CONSTANT_I_CODE:	return idio_constant_i_code_inst;
    case IDIO_TYPE_CONSTANT_UNICODE:	return idio_unicode_inst;
    case IDIO_TYPE_PLACEHOLDER:		return idio_PLACEHOLDER_inst;
    case IDIO_TYPE_STRING:		return idio_string_inst;
    case IDIO_TYPE_SUBSTRING:		return idio_substring_inst;
    case IDIO_TYPE_SYMBOL:		return idio_symbol_inst;
    case IDIO_TYPE_KEYWORD:		return idio_keyword_inst;
    case IDIO_TYPE_PAIR:		return idio_pair_inst;
    case IDIO_TYPE_ARRAY:		return idio_array_inst;
    case IDIO_TYPE_HASH:		return idio_hash_inst;
    case IDIO_TYPE_CLOSURE:		return idio_closure_inst;
    case IDIO_TYPE_PRIMITIVE:		return idio_primitive_inst;
    case IDIO_TYPE_BIGNUM:		return idio_bignum_inst;
    case IDIO_TYPE_MODULE:		return idio_module_inst;
    case IDIO_TYPE_FRAME:		return idio_frame_inst;
    case IDIO_TYPE_HANDLE:		return idio_handle_inst;
    case IDIO_TYPE_STRUCT_TYPE:		return idio_struct_type_inst;
    case IDIO_TYPE_STRUCT_INSTANCE:	return idio_struct_instance_inst;
    case IDIO_TYPE_THREAD:		return idio_thread_inst;
    case IDIO_TYPE_CONTINUATION:	return idio_continuation_inst;
    case IDIO_TYPE_BITSET:		return idio_bitset_inst;

    case IDIO_TYPE_C_CHAR:		return idio_C_char_inst;
    case IDIO_TYPE_C_SCHAR:		return idio_C_schar_inst;
    case IDIO_TYPE_C_UCHAR:		return idio_C_uchar_inst;
    case IDIO_TYPE_C_SHORT:		return idio_C_short_inst;
    case IDIO_TYPE_C_USHORT:		return idio_C_ushort_inst;
    case IDIO_TYPE_C_INT:		return idio_C_int_inst;
    case IDIO_TYPE_C_UINT:		return idio_C_uint_inst;
    case IDIO_TYPE_C_LONG:		return idio_C_long_inst;
    case IDIO_TYPE_C_ULONG:		return idio_C_ulong_inst;
    case IDIO_TYPE_C_LONGLONG:		return idio_C_longlong_inst;
    case IDIO_TYPE_C_ULONGLONG:		return idio_C_ulonglong_inst;
    case IDIO_TYPE_C_FLOAT:		return idio_C_float_inst;
    case IDIO_TYPE_C_DOUBLE:		return idio_C_double_inst;
    case IDIO_TYPE_C_LONGDOUBLE:	return idio_C_longdouble_inst;
    case IDIO_TYPE_C_POINTER:		return idio_C_pointer_inst;

    default:
	/*
	 * Test Case: coding error?
	 */
	idio_object_instance_error ("unknown type", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}


IDIO_DEFINE_PRIMITIVE1_DS ("class-of", class_of, (IDIO o), "o", "\
return the class of `o`				\n\
						\n\
:param o: object to query			\n\
:return: class of `o`				\n\
")
{
    IDIO_ASSERT (o);

    if (idio_isa_instance (o)) {
	return idio_struct_instance_ref_direct (o, IDIO_CLASS_ST_CLASS);
    }

    return idio_object_class_of (o);
}

static int idio_object_instance_of (IDIO o, IDIO cl)
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (cl);

    IDIO_TYPE_ASSERT (class, cl);

    if (idio_isa_instance (o)) {
	IDIO coo = idio_object_class_of (o);
	IDIO coo_class = idio_struct_instance_ref_direct (coo, IDIO_CLASS_ST_CLASS);
	IDIO coo_class_cpl = idio_struct_instance_ref_direct (coo_class, IDIO_CLASS_SLOT_CPL);

	IDIO cl_class = idio_struct_instance_ref_direct (cl, IDIO_CLASS_ST_CLASS);

	IDIO r = idio_list_memq (cl_class, coo_class_cpl);
	if (idio_S_false != r) {
	    return 1;
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE2_DS ("instance-of?", instance_ofp, (IDIO o, IDIO cl), "o cl", "\
test if `o` is an instance of class `cl`	\n\
						\n\
:param o: object to test			\n\
:param cl: class to test			\n\
:type cl: class					\n\
:return: ``#t`` if `o` is an instance of `cl`, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/instance-of-bad-class-type.idio
     *
     * instance-of? #t #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    IDIO r = idio_S_false;

    if (idio_object_instance_of (o, cl)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%set-instance-proc!", set_instance_proc, (IDIO gf, IDIO proc), "gf proc", "\
set the instance procedure of `gf` to `proc`	\n\
						\n\
:param gf: generic function to modify		\n\
:type gf: generic				\n\
:param proc: function to use			\n\
:type proc: function				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (gf);
    IDIO_ASSERT (proc);

    /*
     * Test Case: object-errors/set-instance-proc-bad-gf-type.idio
     *
     * %set-instance-proc! #t #t
     */
    IDIO_USER_TYPE_ASSERT (generic, gf);

    /*
     * Test Case: object-errors/set-instance-proc-bad-proc-type.idio
     *
     * %set-instance-proc! initialize #t
     */
    IDIO_USER_TYPE_ASSERT (function, proc);

    return idio_struct_instance_set_direct (gf, IDIO_CLASS_ST_PROC, proc);
}

/*
 * Simple functions for simple things
 */

IDIO idio_simple_getters_n_setters (IDIO slots)
{
    IDIO_ASSERT (slots);

    IDIO_TYPE_ASSERT (list, slots);

    IDIO r = idio_S_nil;
    size_t i = 0;

    while (idio_S_nil != slots) {
	/*
	 * The getters-n-setters tuple is (name init-function getter)
	 * where we use an integer for getter to indicate a "fast"
	 * dereference of the slot directly.
	 *
	 * In general, getter can be a unary function.
	 */
	IDIO name = IDIO_PAIR_H (slots);
	r = idio_pair (IDIO_LIST3 (name,
				   idio_object_default_slot_value,
				   idio_integer (i++)),
		       r);

	slots = IDIO_PAIR_T (slots);
    }

    return r;
}

IDIO idio_simple_cpl (IDIO supers, IDIO r)
{
    IDIO_ASSERT (supers);
    IDIO_ASSERT (r);

    IDIO_TYPE_ASSERT (list, supers);
    IDIO_TYPE_ASSERT (list, r);

    if (idio_S_nil == supers) {
	return idio_list_nreverse (r);
    }

    /*
     * Only handles single inheritance: (ph supers)
     */
    return idio_simple_cpl (idio_struct_instance_ref_direct (IDIO_PAIR_H (supers), IDIO_CLASS_SLOT_DIRECT_SUPERS),
			    idio_pair (IDIO_PAIR_H (supers), r));
}

IDIO idio_simple_slots (IDIO slots, IDIO cpl)
{
    IDIO_ASSERT (slots);
    IDIO_ASSERT (cpl);

    IDIO_TYPE_ASSERT (list, slots);

    while (idio_S_nil != cpl) {
	slots = idio_list_append2 (idio_struct_instance_ref_direct (IDIO_PAIR_H (cpl), IDIO_CLASS_SLOT_DIRECT_SLOTS),
				   slots);

	cpl = IDIO_PAIR_T (cpl);
    }

    return slots;
}

IDIO idio_simple_make_class (IDIO cl, IDIO name, IDIO direct_supers, IDIO direct_slots)
{
    IDIO_ASSERT (cl);
    IDIO_ASSERT (name);
    IDIO_ASSERT (direct_supers);
    IDIO_ASSERT (direct_slots);

    IDIO_TYPE_ASSERT (instance, cl);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, direct_supers);
    IDIO_TYPE_ASSERT (list, direct_slots);

    IDIO inst = idio_allocate_instance (cl, IDIO_CLASS_SLOT_MAX);

    IDIO cpl = idio_simple_cpl (direct_supers, IDIO_LIST1 (inst));
    IDIO slots = idio_simple_slots (direct_slots, cpl);
    IDIO gns = idio_simple_getters_n_setters (slots);

    idio_struct_instance_set_direct (inst, IDIO_CLASS_SLOT_NAME, name);
    idio_struct_instance_set_direct (inst, IDIO_CLASS_SLOT_DIRECT_SUPERS, direct_supers);
    idio_struct_instance_set_direct (inst, IDIO_CLASS_SLOT_DIRECT_SLOTS, direct_slots);
    idio_struct_instance_set_direct (inst, IDIO_CLASS_SLOT_CPL, cpl);
    idio_struct_instance_set_direct (inst, IDIO_CLASS_SLOT_SLOTS, slots);
    idio_struct_instance_set_direct (inst, IDIO_CLASS_SLOT_NFIELDS, idio_integer (idio_list_length (slots)));
    idio_struct_instance_set_direct (inst, IDIO_CLASS_SLOT_GETTERS_N_SETTERS, gns);

    return inst;
}

IDIO_DEFINE_PRIMITIVE1_DS ("class-name", class_name, (IDIO cl), "cl", "\
return the name of class `cl`		\n\
					\n\
:param cl: class			\n\
:type cl: instance			\n\
:return: name of class `cl`		\n\
")
{
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/class-name-bad-class-type.idio
     *
     * class-name #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    return idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME);
}

IDIO_DEFINE_PRIMITIVE1_DS ("class-direct-supers", class_direct_supers, (IDIO cl), "cl", "\
return the direct-supers of class `cl`	\n\
					\n\
:param cl: class			\n\
:type cl: instance			\n\
:return: direct supers of class `cl`	\n\
")
{
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/class-direct-supers-bad-class-type.idio
     *
     * class-direct-supers #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    return idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_DIRECT_SUPERS);
}

IDIO_DEFINE_PRIMITIVE1_DS ("class-direct-slots", class_direct_slots, (IDIO cl), "cl", "\
return the direct-slots of class `cl`	\n\
					\n\
:param cl: class			\n\
:type cl: instance			\n\
:return: direct slots of class `cl`	\n\
")
{
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/class-direct-slots-bad-class-type.idio
     *
     * class-direct-slots #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    return idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_DIRECT_SLOTS);
}

IDIO_DEFINE_PRIMITIVE1_DS ("class-cpl", class_cpl, (IDIO cl), "cl", "\
return the cpl of class `cl`		\n\
					\n\
:param cl: class			\n\
:type cl: instance			\n\
:return: cpl of class `cl`		\n\
")
{
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/class-cpl-bad-class-type.idio
     *
     * class-cpl #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    return idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_CPL);
}

IDIO_DEFINE_PRIMITIVE1_DS ("class-slots", class_slots, (IDIO cl), "cl", "\
return the slots of class `cl`		\n\
					\n\
:param cl: class			\n\
:type cl: instance			\n\
:return: slots of class `cl`		\n\
")
{
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/class-slots-bad-class-type.idio
     *
     * class-slots #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    return idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_SLOTS);
}

IDIO_DEFINE_PRIMITIVE1_DS ("class-nfields", class_nfields, (IDIO cl), "cl", "\
return the nfields of class `cl`	\n\
					\n\
:param cl: class			\n\
:type cl: instance			\n\
:return: nfields of class `cl`		\n\
")
{
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/class-nfields-bad-class-type.idio
     *
     * class-nfields #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    return idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NFIELDS);
}

IDIO_DEFINE_PRIMITIVE1_DS ("class-getters-n-setters", class_getters_n_setters, (IDIO cl), "cl", "\
return the getters-n-setters of class `cl`	\n\
						\n\
:param cl: class				\n\
:type cl: instance				\n\
:return: getters-n-setters of class `cl`	\n\
")
{
    IDIO_ASSERT (cl);

    /*
     * Test Case: object-errors/class-getters-n-setters-bad-class-type.idio
     *
     * class-getters-n-setters #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    return idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_GETTERS_N_SETTERS);
}

IDIO_DEFINE_PRIMITIVE1_DS ("generic-name", generic_name, (IDIO gf), "gf", "\
return the name of generic function `gf`	\n\
						\n\
:param gf: generic function			\n\
:type gf: instance				\n\
:return: name of generic function `gf`		\n\
")
{
    IDIO_ASSERT (gf);

    /*
     * Test Case: object-errors/generic-name-bad-gf-type.idio
     *
     * generic-name #t
     */
    IDIO_USER_TYPE_ASSERT (generic, gf);

    return idio_struct_instance_ref_direct (gf, IDIO_GENERIC_SLOT_NAME);
}

IDIO_DEFINE_PRIMITIVE1_DS ("generic-documentation", generic_documentation, (IDIO gf), "gf", "\
return the documentation of generic function `gf`	\n\
						\n\
:param gf: generic function			\n\
:type gf: instance				\n\
:return: documentation of generic function `gf`	\n\
")
{
    IDIO_ASSERT (gf);

    /*
     * Test Case: object-errors/generic-documentation-bad-gf-type.idio
     *
     * generic-documentation #t
     */
    IDIO_USER_TYPE_ASSERT (generic, gf);

    return idio_struct_instance_ref_direct (gf, IDIO_GENERIC_SLOT_DOCUMENTATION);
}

IDIO_DEFINE_PRIMITIVE1_DS ("generic-methods", generic_methods, (IDIO gf), "gf", "\
return the methods of generic function `gf`	\n\
						\n\
:param gf: generic function			\n\
:type gf: instance				\n\
:return: methods of generic function `gf`	\n\
")
{
    IDIO_ASSERT (gf);

    /*
     * Test Case: object-errors/generic-methods-bad-gf-type.idio
     *
     * generic-methods #t
     */
    IDIO_USER_TYPE_ASSERT (generic, gf);

    return idio_struct_instance_ref_direct (gf, IDIO_GENERIC_SLOT_METHODS);
}

IDIO_DEFINE_PRIMITIVE1_DS ("method-generic-function", method_generic_function, (IDIO m), "m", "\
return the generic function of method function `m`	\n\
						\n\
:param m: method function			\n\
:type m: instance				\n\
:return: generic function of method function `m`	\n\
")
{
    IDIO_ASSERT (m);

    /*
     * Test Case: object-errors/method-generic-function-bad-m-type.idio
     *
     * method-generic-function #t
     */
    IDIO_USER_TYPE_ASSERT (method, m);

    return idio_struct_instance_ref_direct (m, IDIO_METHOD_SLOT_GENERIC_FUNCTION);
}

IDIO_DEFINE_PRIMITIVE1_DS ("method-specializers", method_specializers, (IDIO m), "m", "\
return the specializers of method function `m`	\n\
						\n\
:param m: method function			\n\
:type m: instance				\n\
:return: specializers of method function `m`	\n\
")
{
    IDIO_ASSERT (m);

    /*
     * Test Case: object-errors/method-specializers-bad-m-type.idio
     *
     * method-specializers #t
     */
    IDIO_USER_TYPE_ASSERT (method, m);

    return idio_struct_instance_ref_direct (m, IDIO_METHOD_SLOT_SPECIALIZERS);
}

IDIO_DEFINE_PRIMITIVE1_DS ("method-procedure", method_procedure, (IDIO m), "m", "\
return the procedure of method function `m`	\n\
						\n\
:param m: method function			\n\
:type m: instance				\n\
:return: procedure of method function `m`	\n\
")
{
    IDIO_ASSERT (m);

    /*
     * Test Case: object-errors/method-procedure-bad-m-type.idio
     *
     * method-procedure #t
     */
    IDIO_USER_TYPE_ASSERT (method, m);

    return idio_struct_instance_ref_direct (m, IDIO_METHOD_SLOT_PROCEDURE);
}

IDIO_DEFINE_PRIMITIVE2V_DS ("%make-instance", make_instance, (IDIO cl, IDIO kind, IDIO args), "cl kind [args]", "\
A primitive instance creator			\n\
						\n\
:param cl: class				\n\
:type cl: instance				\n\
:param kind: see below				\n\
:type kind: symbol				\n\
:param args: see below				\n\
:type args: see below				\n\
:return: instance				\n\
:raises ^rt-invalid-class-error:		\n\
						\n\
                                                                          \n\
``%make-instance`` is a bootstrap function for the creation of		  \n\
``<generic>``, ``<method>`` and ``<class>`` instances.  See		  \n\
:ref:`make <make>` for a user-facing function.				  \n\
                                                                          \n\
`kind` should one of the *symbols*: ``generic``, ``method`` or ``class``. \n\
                                                                          \n\
`args` is dependent on the value of `kind`:                               \n\
                                                                          \n\
``generic``                                                               \n\
                                                                          \n\
  :param name: name of the generic function                               \n\
  :type name: symbol                                                      \n\
  :param docstr: documentation string, defaults to ``#n``                 \n\
  :type docstr: string                                                    \n\
                                                                          \n\
``method``                                                                \n\
                                                                          \n\
  :param gf: the generic function                                         \n\
  :type gf: instance                                                      \n\
  :param spec: specializers of the method                                 \n\
  :type spec:                                                             \n\
  :param proc: procedure                                                  \n\
  :type proc: function                                                    \n\
                                                                          \n\
``class``                                                                 \n\
                                                                          \n\
  :param name: name of the class                                          \n\
  :type name: symbol                                                      \n\
  :param supers: direct supers of the class                               \n\
  :type supers: list of instances or ``#n``                               \n\
  :param slots: slots of the class                                        \n\
  :type slots: list of symbols or ``#n``                                  \n\
")
{
    IDIO_ASSERT (cl);
    IDIO_ASSERT (kind);
    IDIO_ASSERT (args);

    /*
     * Test Case: object-errors/pct-make-instance-bad-class-type.idio
     *
     * %make-instance #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (class, cl);

    /*
     * Test Case: object-errors/pct-make-instance-bad-kind-type.idio
     *
     * %make-instance <class> #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, kind);

    if (idio_class_sym == kind) {
	if (idio_list_length (args) < 3) {
	    /*
	     * Test Case: object-errors/pct-make-instance-too-few-class-args.idio
	     *
	     * %make-instance <class> 'class #t
	     */
	    idio_error_param_value_exp ("%make-instance class", "args", args, "list of (name supers slots)", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	IDIO name = IDIO_PAIR_H (args);
	IDIO supers = IDIO_PAIR_HT (args);
	IDIO dslots = IDIO_PAIR_HTT (args);

	return idio_simple_make_class (cl, name, supers, dslots);
    } else if (idio_generic_sym == kind) {
	if (idio_list_length (args) < 1) {
	    /*
	     * Test Case: object-errors/pct-make-instance-too-few-generic-args.idio
	     *
	     * %make-instance <class> 'generic
	     */
	    idio_error_param_value_exp ("%make-instance generic", "args", args, "list of (name [docstr])", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	IDIO name = IDIO_PAIR_H (args);
	IDIO docstr = idio_S_nil;
	if (idio_isa_pair (IDIO_PAIR_T (args))) {
	    docstr = IDIO_PAIR_HT (args);
	}

	IDIO cl_slots = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_SLOTS);
	IDIO ent = idio_allocate_entity (cl, idio_list_length (cl_slots));
	idio_struct_instance_set_direct (ent, IDIO_GENERIC_SLOT_NAME, name);
	idio_struct_instance_set_direct (ent, IDIO_GENERIC_SLOT_DOCUMENTATION, docstr);
	idio_struct_instance_set_direct (ent, IDIO_GENERIC_SLOT_METHODS, idio_S_nil);
	return ent;
    } else if (idio_method_sym == kind) {
	if (idio_list_length (args) < 3) {
	    /*
	     * Test Case: object-errors/pct-make-instance-too-few-method-args.idio
	     *
	     * %make-instance <class> 'method #t
	     */
	    idio_error_param_value_exp ("%make-instance method", "args", args, "list of (gf spec proc)", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	IDIO gf = IDIO_PAIR_H (args);
	IDIO spec = IDIO_PAIR_HT (args);
	IDIO proc = IDIO_PAIR_HTT (args);

	IDIO cl_slots = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_SLOTS);
	IDIO m = idio_allocate_entity (cl, idio_list_length (cl_slots));
	idio_struct_instance_set_direct (m, IDIO_METHOD_SLOT_GENERIC_FUNCTION, gf);
	idio_struct_instance_set_direct (m, IDIO_METHOD_SLOT_SPECIALIZERS, spec);
	idio_struct_instance_set_direct (m, IDIO_METHOD_SLOT_PROCEDURE, proc);
	return m;
    } else {
	/*
	 * Test Case: object-errors/pct-make-instance-bad-kind-value.idio
	 *
	 * %make-instance <class> 'instance #t
	 */
	idio_error_param_value_exp ("%make-instance", "kind", kind, "generic|method|class", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

IDIO idio_object_slot_ref (IDIO obj, IDIO slot_name)
{
    IDIO_ASSERT (obj);
    IDIO_ASSERT (slot_name);

    IDIO_TYPE_ASSERT (instance, obj);
    IDIO_TYPE_ASSERT (symbol, slot_name);

    IDIO cl = idio_object_class_of (obj);
    IDIO gns = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_GETTERS_N_SETTERS);

    IDIO slot_info = idio_list_assq (slot_name, gns);

    if (idio_S_false != slot_info) {
	/*
	 * (name init-function getter)
	 */
	IDIO slot_getter = IDIO_PAIR_HTT (slot_info);
	if (idio_isa_integer (slot_getter)) {
	    return idio_struct_instance_ref_direct (obj, IDIO_CLASS_ST_MAX + IDIO_FIXNUM_VAL (slot_getter));
	} else {
	    IDIO cmd = IDIO_LIST2 (slot_getter, obj);
	    return idio_vm_invoke_C (idio_thread_current_thread (), cmd);
	}
    }

    /*
     * Test Case: object-errors/slot-ref-slot-not-found.idio
     *
     * slot-ref <class> 'unknown
     */
    idio_object_slot_not_found_error (obj, cl, slot_name, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2_DS ("slot-ref", slot_ref, (IDIO obj, IDIO slot), "obj slot", "\
return the value of slot `slot` in `obj`	\n\
						\n\
:param obj: object				\n\
:type obj: instance				\n\
:param slot: slot name				\n\
:type slot: symbol				\n\
:return: value					\n\
:raises ^rt-slot-not-found-error:		\n\
")
{
    IDIO_ASSERT (obj);
    IDIO_ASSERT (slot);

    /*
     * Test Case: object-errors/slot-ref-bad-obj-type.idio
     *
     * slot-ref #t #t
     */
    IDIO_USER_TYPE_ASSERT (instance, obj);

    /*
     * Test Case: object-errors/slot-ref-bad-slot-type.idio
     *
     * slot-ref <class> #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, slot);

    return idio_object_slot_ref (obj, slot);
}

IDIO idio_object_slot_set (IDIO obj, IDIO slot, IDIO val)
{
    IDIO_ASSERT (obj);
    IDIO_ASSERT (slot);
    IDIO_ASSERT (val);

    IDIO_TYPE_ASSERT (instance, obj);
    IDIO_TYPE_ASSERT (symbol, slot);

    IDIO cl = idio_object_class_of (obj);
    IDIO gns = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_GETTERS_N_SETTERS);

    IDIO slot_info = idio_list_assq (slot, gns);

    if (idio_S_false != slot_info) {
	/*
	 * (name init-function getter)
	 */
	IDIO slot_getter = IDIO_PAIR_HTT (slot_info);
	if (idio_isa_integer (slot_getter)) {
	    return idio_struct_instance_set_direct (obj, IDIO_CLASS_ST_MAX + IDIO_FIXNUM_VAL (slot_getter), val);
	} else {
	    IDIO cmd = IDIO_LIST3 (slot_getter, obj, val);
	    return idio_vm_invoke_C (idio_thread_current_thread (), cmd);
	}
    }

    /*
     * Test Case: object-errors/slot-set!-slot-not-found.idio
     *
     * slot-set! <class> 'unknown #t
     */
    idio_object_slot_not_found_error (obj, cl, slot, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE3_DS ("slot-set!", slot_set, (IDIO obj, IDIO slot, IDIO val), "obj slot val", "\
set the value of slot `slot` in `obj` to `val`	\n\
						\n\
:param obj: object				\n\
:type obj: instance				\n\
:param slot: slot name				\n\
:type slot: symbol				\n\
:param val: value				\n\
:type val: any					\n\
:return: ``#<unspec>>``				\n\
:raises ^rt-slot-not-found-error:		\n\
")
{
    IDIO_ASSERT (obj);
    IDIO_ASSERT (slot);
    IDIO_ASSERT (val);

    /*
     * Test Case: object-errors/slot-set!-bad-obj-type.idio
     *
     * slot-set! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (instance, obj);

    /*
     * Test Case: object-errors/slot-set!-bad-slot-type.idio
     *
     * slot-set! <class> #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, slot);

    return idio_object_slot_set (obj, slot, val);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%slot-set-direct!", slot_set_direct, (IDIO obj, IDIO index, IDIO val), "obj index val", "\
set the value of the `index`\\ :sup:`th` slot in `obj` to `val`	\n\
						\n\
:param obj: object				\n\
:type obj: instance				\n\
:param index: slot index			\n\
:type index: non-negative integer		\n\
:param val: value				\n\
:type val: any					\n\
:return: ``#<unspec>>``				\n\
:raises ^rt-slot-not-found-error:		\n\
")
{
    IDIO_ASSERT (obj);
    IDIO_ASSERT (index);
    IDIO_ASSERT (val);

    /*
     * Test Case: object-errors/slot-set-direct!-bad-obj-type.idio
     *
     * slot-set! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (instance, obj);

    /*
     * Test Case: object-errors/slot-set-direct!-bad-index-type.idio
     *
     * slot-set! <class> #t #t
     */
    IDIO_USER_TYPE_ASSERT (integer, index);

    ssize_t C_index = -1;
    if (idio_isa_fixnum (index)) {
	C_index = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    C_index = idio_bignum_ptrdiff_t_value (index);
	} else {
	    IDIO i = idio_bignum_real_to_integer (index);
	    if (idio_S_nil != i) {
		C_index = idio_bignum_ptrdiff_t_value (i);
	    }
	}
    }

    /*
     * Test Case: object-errors/slot-set-direct-bad-index-value.idio
     *
     * %slot-set-direct! <class> -1 #t
     */
    if (C_index < 0) {
	idio_error_param_value_msg ("%slot-set-direct!", "index", index, "should be non-negative", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_struct_instance_set_direct (obj, IDIO_CLASS_ST_MAX + C_index, val);
}

static void idio_dump_instance (IDIO o)
{
    IDIO_ASSERT (o);

    IDIO cl = o;

    if (idio_isa_method (o)) {
	cl = idio_object_class_of (o);
	idio_debug ("method of %s:\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME));

	idio_debug (" generic-function: %s\n", idio_struct_instance_ref_direct (o, IDIO_METHOD_SLOT_GENERIC_FUNCTION));
	idio_debug (" specializers:     %s\n", idio_struct_instance_ref_direct (o, IDIO_METHOD_SLOT_SPECIALIZERS));
    } else if (idio_isa_generic (o)) {
	cl = idio_object_class_of (o);
	idio_debug ("generic of %s:\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME));

	idio_debug (" name:          %s\n", idio_struct_instance_ref_direct (o, IDIO_GENERIC_SLOT_NAME));
	idio_debug (" documentation: %s\n", idio_struct_instance_ref_direct (o, IDIO_GENERIC_SLOT_DOCUMENTATION));
	IDIO ilist = idio_struct_instance_ref_direct (o, IDIO_GENERIC_SLOT_METHODS);
	if (idio_isa_pair (ilist) ||
	    idio_S_nil == ilist) {
	    int first = 1;
	    while (idio_S_nil != ilist) {
		if (first) {
		    first = 0;
		    idio_debug (" methods:       %s\n", idio_struct_instance_ref_direct (IDIO_PAIR_H (ilist), IDIO_METHOD_SLOT_SPECIALIZERS));
		} else {
		    idio_debug ("                %s\n", idio_struct_instance_ref_direct (IDIO_PAIR_H (ilist), IDIO_METHOD_SLOT_SPECIALIZERS));
		}

		ilist = IDIO_PAIR_T (ilist);
	    }
	} else {
	    idio_debug (" methods:       %s ??\n", ilist);
	}
    } else if (idio_isa_class (o)) {
	idio_debug ("class %s:\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME));

	IDIO name = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME);
	idio_debug ("   class:%s\n", name);

	IDIO ilist = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_DIRECT_SUPERS);
	IDIO names = idio_S_nil;
	if (idio_isa_pair (ilist) ||
	    idio_S_nil == ilist) {
	    while (idio_S_nil != ilist) {
		names = idio_pair (idio_struct_instance_ref_direct (IDIO_PAIR_H (ilist), IDIO_CLASS_SLOT_NAME), names);

		ilist = IDIO_PAIR_T (ilist);
	    }
	    idio_debug ("  supers:%s\n", idio_list_nreverse (names));
	} else {
	    idio_debug ("  supers:%s ??\n", ilist);
	}
	idio_debug (" d-slots:%s\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_DIRECT_SLOTS));

	ilist = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_CPL);
	names = idio_S_nil;
	if (idio_isa_pair (ilist)) {
	    while (idio_S_nil != ilist) {
		names = idio_pair (idio_struct_instance_ref_direct (IDIO_PAIR_H (ilist), IDIO_CLASS_SLOT_NAME), names);

		ilist = IDIO_PAIR_T (ilist);
	    }
	    idio_debug ("     cpl:%s\n", idio_list_reverse (names));
	} else {
	    idio_debug ("     cpl:%s ??\n", ilist);
	}
	idio_debug ("   slots:%s\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_SLOTS));
	idio_debug (" nfields:%s\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NFIELDS));
	idio_debug ("     gns:%s\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_GETTERS_N_SETTERS));
    } else if (idio_isa_instance (o)) {
	cl = idio_object_class_of (o);
	idio_debug ("instance of %s:\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME));

	IDIO nfields = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NFIELDS);
	ssize_t C_nfields = -1;
	if (idio_isa_fixnum (nfields)) {
	    C_nfields = IDIO_FIXNUM_VAL (nfields);
	} else if (idio_isa_bignum (nfields)) {
	    if (IDIO_BIGNUM_INTEGER_P (nfields)) {
		C_nfields = idio_bignum_ptrdiff_t_value (nfields);
	    } else {
		IDIO i = idio_bignum_real_to_integer (nfields);
		if (idio_S_nil != i) {
		    C_nfields = idio_bignum_ptrdiff_t_value (i);
		}
	    }
	}

	if (C_nfields) {
	    IDIO ilist = idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_GETTERS_N_SETTERS);
	    if (idio_isa_pair (ilist)) {
		while (idio_S_nil != ilist) {
		    IDIO name = IDIO_PAIR_HH (ilist);
		    idio_debug (" %-20s:", name);
		    idio_debug (" %s\n", idio_object_slot_ref (o, name));

		    ilist = IDIO_PAIR_T (ilist);
		}
	    } else {
		idio_debug (" g-n-s is %s ??\n", ilist);
		idio_debug (" slots    %s\n", idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_SLOTS));
	    }
	}
    } else {
	idio_debug ("dump-instance of (unknown): %s\n", o);
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("dump-instance", dump_instance, (IDIO o), "o", "\
dump instance `o`				\n\
						\n\
:param o: object				\n\
:type o: instance				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (o);

    /*
     * Test Case: object-errors/dump-instance-bad-instance-type.idio
     *
     * dump-instance #t
     */
    IDIO_USER_TYPE_ASSERT (instance, o);

    idio_dump_instance (o);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%cpl-args", cpl_args, (IDIO args), "args", "\
An accelerator for				\n\
						\n\
.. code-block:: idio				\n\
						\n\
   map (function (arg) (class-cpl (class-of arg))) args	\n\
						\n\
:param args: method arguments			\n\
:type args: list				\n\
:return: list of argument CPLs			\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: object-errors/cpl-args-bad-args-type.idio
     *
     * %cpl-args #t
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_S_nil;

    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO co = idio_object_class_of (arg);
	IDIO cpl = idio_struct_instance_ref_direct (co, IDIO_CLASS_SLOT_CPL);

	r = idio_pair (cpl, r);

	args = IDIO_PAIR_T (args);
    }

    return idio_list_nreverse (r);
}

char *idio_instance_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (instance, v);

    char *r = NULL;
    *sizep = idio_asprintf (&r, "");

    size_t n_size = 0;
    char *ns = idio_as_string (idio_struct_instance_ref_direct (v, IDIO_CLASS_SLOT_NAME), &n_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, ns, n_size);

    return r;
}

char *idio_instance_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (instance, v);

    /*
     * Prefer a struct-instance->string method in our immediate struct
     * type (ie. don't recurse for a method as it won't be for us).
     */
    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (v);
    idio_vtable_method_t *st_m = idio_vtable_flat_lookup_method (idio_value_vtable (st), st, idio_S_struct_instance_2string, 0);

    if (NULL != st_m &&
	idio_instance_method_2string != IDIO_VTABLE_METHOD_FUNC (st_m)) {
	IDIO s = IDIO_VTABLE_METHOD_FUNC (st_m) (st_m, v, sizep, seen, depth);

	if (idio_isa_string (s)) {
	    return idio_utf8_string (s, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED, IDIO_UTF8_STRING_NOPREC);
	} else if (0 == idio_vm_reporting) {
	    /*
	     * Test Case: util-errors/struct-instance-printer-bad-return-type.idio
	     *
	     * ... return #t
	     */
#ifdef IDIO_DEBUG
	    idio_debug ("struct-instance printer => %s (not a STRING)\n", s);
#endif
	    idio_error_param_value_msg ("struct-instance-as-string", "struct-instance printer", s, "should return a string", IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}
    }

    /*
     * Otherwise, a basic printer
     */
    char *r = NULL;
    *sizep = idio_asprintf (&r, "");
    size_t n_size = 0;
    char *ns;

    IDIO cl = v;

    if (idio_isa_method (v)) {
	cl = idio_object_class_of (v);
	IDIO_STRCAT (r, sizep, "#<METHOD ");
	ns = idio_as_string (idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME), &n_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, ns, n_size);
	IDIO_STRCAT (r, sizep, ">");
    } else if (idio_isa_generic (v)) {
	cl = idio_object_class_of (v);
	IDIO_STRCAT (r, sizep, "#<GENERIC ");
	ns = idio_as_string (idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME), &n_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, ns, n_size);
	IDIO_STRCAT (r, sizep, ">");
    } else if (idio_isa_class (v)) {
	ns = idio_as_string (idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME), &n_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, ns, n_size);
    } else if (idio_isa_instance (v)) {
	cl = idio_object_class_of (v);
	IDIO_STRCAT (r, sizep, "#<INSTANCE ");
	ns = idio_as_string (idio_struct_instance_ref_direct (cl, IDIO_CLASS_SLOT_NAME), &n_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, ns, n_size);
	IDIO_STRCAT (r, sizep, ">");
    } else {
	*sizep = idio_asprintf (&r, "#<INSTANCE %10p>", v);
    }

    return r;
}

IDIO idio_instance_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (instance, v);

    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    IDIO seen = va_arg (ap, IDIO);
    int depth = va_arg (ap, int);
    va_end (ap);

    idio_vtable_method_t *st_m = idio_vtable_lookup_method (idio_value_vtable (st), v, idio_S_struct_instance_2string, 0);

    if (NULL != st_m &&
	idio_instance_method_2string != IDIO_VTABLE_METHOD_FUNC (st_m)) {
	IDIO r = IDIO_VTABLE_METHOD_FUNC (st_m) (st_m, v, sizep, seen, depth);

	return r;
    }

    char *C_r = idio_instance_as_C_string (v, sizep, 0, seen, depth);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

char *idio_class_struct_type_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (struct_type, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#<CLASS ");

    size_t stn_size = 0;
    char *stn = idio_as_string (IDIO_STRUCT_TYPE_NAME (v), &stn_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, stn, stn_size);

    IDIO_STRCAT (r, sizep, ">");

    return r;
}

char *idio_class_struct_type_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    /*
     * struct-instances are anomalous because they are a true Idio
     * type yet don't have an idio_struct_instance_vtable catering for
     * them (anymore).  That's because the vast majority of uses are
     * actually those of the struct-type.
     *
     * Printing is one where we do want to distinguish and call the
     * struct-instance printer.
     */
    if (idio_isa_struct_instance (v)) {
	return idio_struct_instance_as_C_string (v, sizep, format, seen, depth);
    }
    IDIO_TYPE_ASSERT (struct_type, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#<CLASS>");

    return r;
}

IDIO idio_class_struct_type_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_class_struct_type_as_C_string (v, sizep, 0, idio_S_nil, 40);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_object_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, allocate_instance);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, allocate_entity);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, instancep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, classp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, genericp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, methodp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_of);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, instance_ofp);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, set_instance_proc);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_direct_supers);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_direct_slots);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_cpl);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_slots);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_nfields);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, class_getters_n_setters);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, generic_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, generic_documentation);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, generic_methods);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, method_generic_function);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, method_specializers);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, method_procedure);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, make_instance);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, slot_ref);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, slot_set);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, slot_set_direct);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, dump_instance);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_object_module, cpl_args);
}

void idio_final_object ()
{
}

void idio_init_object ()
{
    idio_module_table_register (idio_object_add_primitives, idio_final_object, NULL);

    idio_object_module = idio_module (IDIO_SYMBOLS_C_INTERN ("object"));

    IDIO vi;
    vi = IDIO_ADD_MODULE_PRIMITIVE (idio_object_module, invoke_instance_in_error);
    idio_object_invoke_instance_in_error = idio_vm_values_ref (IDIO_FIXNUM_VAL (vi));
    vi = IDIO_ADD_MODULE_PRIMITIVE (idio_object_module, invoke_entity_in_error);
    idio_object_invoke_entity_in_error = idio_vm_values_ref (IDIO_FIXNUM_VAL (vi));

    vi = IDIO_ADD_MODULE_PRIMITIVE (idio_object_module, default_slot_value);
    idio_object_default_slot_value = idio_vm_values_ref (IDIO_FIXNUM_VAL (vi));

    /*
     * We need an implementation to (nearly silently) do all the heavy
     * lifting.  We might not see it again!
     *
     * Here we use a struct-type and everything, including Classes are
     * instances of it.
     */
    idio_class_sym = IDIO_SYMBOLS_C_INTERN ("class");
    idio_class_class_sym = IDIO_SYMBOLS_C_INTERN ("<class>");
    IDIO class_st_names = idio_pair (idio_class_sym,
                          idio_pair (IDIO_SYMBOLS_C_INTERN ("proc"),
				     idio_S_nil));

    idio_class_struct_type = idio_struct_type (idio_class_class_sym,
					       idio_S_nil,
					       class_st_names);

    /*
     * Careful, though, IOS instances are recursive by design so we
     * need printers that are aware of that.
     */
    idio_vtable_add_method (idio_class_struct_type->vtable,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_class_struct_type_method_2string));

    idio_vtable_add_method (idio_class_struct_type->vtable,
			    idio_S_struct_instance_2string,
			    idio_vtable_create_method_simple (idio_instance_method_2string));

    /*
     * Enough detail, already.  Now let's bootstrap IOS.
     *
     * These are the standard slots for a Class.
     */
    idio_class_slot_names = idio_pair (IDIO_SYMBOLS_C_INTERN ("name"),
                            idio_pair (IDIO_SYMBOLS_C_INTERN ("direct-supers"),
                            idio_pair (IDIO_SYMBOLS_C_INTERN ("direct-slots"),
                            idio_pair (IDIO_SYMBOLS_C_INTERN ("cpl"),
                            idio_pair (IDIO_SYMBOLS_C_INTERN ("slots"),
                            idio_pair (IDIO_SYMBOLS_C_INTERN ("nfields"),
                            idio_pair (IDIO_SYMBOLS_C_INTERN ("getters-n-setters"),
				       idio_S_nil)))))));

    /*
     * First up: *allocate* <class> (a struct-instance of our
     * implementation struct-type) and then immediately set its own
     * class to be itself.
     */
    idio_class_inst = idio_allocate_instance (idio_S_false, IDIO_CLASS_SLOT_MAX);
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_ST_CLASS, idio_class_inst);

    /*
     * Give the slots of <class> some values -- a couple to be reset
     * after we create <top> and <object>
     */
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_NAME, idio_class_class_sym);
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_DIRECT_SUPERS, idio_S_nil);
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_DIRECT_SLOTS, idio_class_slot_names);
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_CPL, idio_S_nil);
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_SLOTS, idio_class_slot_names);
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_NFIELDS, idio_integer (IDIO_CLASS_SLOT_MAX - IDIO_CLASS_ST_MAX));
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_GETTERS_N_SETTERS, idio_simple_getters_n_setters (idio_class_slot_names));

    idio_module_export_symbol_value (idio_class_class_sym, idio_class_inst, idio_object_module);

    IDIO top_class_sym = IDIO_SYMBOLS_C_INTERN ("<top>");
    idio_top_inst = idio_simple_make_class (idio_class_inst,
					    top_class_sym,
					    idio_S_nil,
					    idio_S_nil);

    idio_module_export_symbol_value (top_class_sym, idio_top_inst, idio_object_module);

    IDIO object_class_sym = IDIO_SYMBOLS_C_INTERN ("<object>");
    idio_object_inst = idio_simple_make_class (idio_class_inst,
					       object_class_sym,
					       IDIO_LIST1 (idio_top_inst),
					       idio_S_nil);

    idio_module_export_symbol_value (object_class_sym, idio_object_inst, idio_object_module);

    /*
     * Patch up <class> with <object> and <top>
     */
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_DIRECT_SUPERS, IDIO_LIST1 (idio_object_inst));
    idio_struct_instance_set_direct (idio_class_inst, IDIO_CLASS_SLOT_CPL, IDIO_LIST3 (idio_class_inst,
										       idio_object_inst,
										       idio_top_inst));

    /*
     * Create standard IOS Classes
     */
    IDIO class_sym;

#define IDIO_EXPORT_SIMPLE_CLASS(v,cname,cl,super,slots)			\
    class_sym = IDIO_SYMBOLS_C_INTERN (cname);				\
    IDIO v = idio_simple_make_class (cl, class_sym, IDIO_LIST1 (super), slots); \
    idio_module_export_symbol_value (class_sym, v, idio_object_module);

    IDIO_EXPORT_SIMPLE_CLASS (proc_class_inst, "<procedure-class>", idio_class_inst, idio_class_inst, idio_S_nil);
    IDIO_EXPORT_SIMPLE_CLASS (entity_class_inst, "<entity-class>", idio_class_inst, proc_class_inst, idio_S_nil);

    IDIO generic_slots = IDIO_LIST3 (IDIO_SYMBOLS_C_INTERN ("name"),
				     IDIO_SYMBOLS_C_INTERN ("documentation"),
				     IDIO_SYMBOLS_C_INTERN ("methods"));

    idio_generic_sym = IDIO_SYMBOLS_C_INTERN ("generic");
    idio_generic_class_sym = IDIO_SYMBOLS_C_INTERN ("<generic>");
    idio_generic_inst = idio_simple_make_class (entity_class_inst, idio_generic_class_sym, IDIO_LIST1 (idio_object_inst), generic_slots);
    idio_module_export_symbol_value (idio_generic_class_sym, idio_generic_inst, idio_object_module);

    IDIO method_slots = IDIO_LIST3 (IDIO_SYMBOLS_C_INTERN ("generic-function"),
				    IDIO_SYMBOLS_C_INTERN ("specializers"),
				    IDIO_SYMBOLS_C_INTERN ("procedure"));

    idio_method_sym = IDIO_SYMBOLS_C_INTERN ("method");
    idio_method_class_sym = IDIO_SYMBOLS_C_INTERN ("<method>");
    idio_method_inst = idio_simple_make_class (idio_class_inst, idio_method_class_sym, IDIO_LIST1 (idio_object_inst), method_slots);
    idio_module_export_symbol_value (idio_method_class_sym, idio_method_inst, idio_object_module);

    /*
     * Create Classes for regular Idio types - no slots and all
     * instances of <builtin-class> and with <builtin-class> as the
     * superclass.
     *
     * We could throw in some derived types like <number> and
     * <integer> too but, as with all class hierarchies, they are
     * never quite right.
     */
    IDIO_EXPORT_SIMPLE_CLASS (builtin_class_inst, "<builtin-class>", idio_class_inst, idio_top_inst, idio_S_nil);

#define IDIO_EXPORT_PRIMITIVE_CLASS(v,cname)				\
    class_sym = IDIO_SYMBOLS_C_INTERN (cname);				\
    v = idio_simple_make_class (idio_class_inst, class_sym, IDIO_LIST1 (builtin_class_inst), idio_S_nil); \
    idio_module_export_symbol_value (class_sym, v, idio_object_module);

    IDIO_EXPORT_PRIMITIVE_CLASS (idio_fixnum_inst,          "<fixnum>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_constant_inst,        "<constant>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_constant_token_inst,  "<constant-token>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_constant_i_code_inst, "<constant-i-code>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_unicode_inst,         "<unicode>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_PLACEHOLDER_inst,     "<PLACEHOLDER>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_string_inst,          "<string>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_substring_inst,       "<substring>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_symbol_inst,          "<symbol>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_keyword_inst,         "<keyword>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_pair_inst,            "<pair>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_array_inst,           "<array>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_hash_inst,            "<hash>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_bignum_inst,          "<bignum>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_module_inst,          "<module>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_frame_inst,           "<frame>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_handle_inst,          "<handle>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_char_inst,          "<C/char>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_schar_inst,         "<C/schar>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_uchar_inst,         "<C/uchar>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_short_inst,         "<C/short>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_ushort_inst,        "<C/ushort>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_int_inst,           "<C/int>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_uint_inst,          "<C/uint>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_long_inst,          "<C/long>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_ulong_inst,         "<C/ulong>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_longlong_inst,      "<C/longlong>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_ulonglong_inst,     "<C/ulonglong>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_float_inst,         "<C/float>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_double_inst,        "<C/double>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_longdouble_inst,    "<C/longdouble>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_C_pointer_inst,       "<C/pointer>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_struct_type_inst,     "<struct-type>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_struct_instance_inst, "<struct-instance>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_thread_inst,          "<thread>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_continuation_inst,    "<continuation>");
    IDIO_EXPORT_PRIMITIVE_CLASS (idio_bitset_inst,          "<bitset>");

#define IDIO_EXPORT_PROCEDURE_CLASS(v,cname)				\
    class_sym = IDIO_SYMBOLS_C_INTERN (cname);				\
    v = idio_simple_make_class (proc_class_inst, class_sym, IDIO_LIST1 (builtin_class_inst), idio_S_nil); \
    idio_module_export_symbol_value (class_sym, v, idio_object_module);

    IDIO_EXPORT_PROCEDURE_CLASS (idio_closure_inst, "<closure>");
    IDIO_EXPORT_PROCEDURE_CLASS (idio_primitive_inst, "<primitive>");
}
