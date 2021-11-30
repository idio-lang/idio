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
 * error.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "handle.h"
#include "idio-string.h"
#include "malloc.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"

static IDIO idio_S_coding;
static IDIO idio_S_internal;
static IDIO idio_S_user;
static IDIO idio_S_user_code;

/*
 * Code coverage:
 *
 * These first three, ultimately invoking vfprintf() with some varargs
 * is mostly really just to remind me how fprintf() works with
 * varargs.
 *
 * In practice only the error variant is called and only in times of
 * great stress -- ie. immediately followed by abort().
 */
int idio_error_vfprintf (char const *format, va_list argp)
{
    return vfprintf (stderr, format, argp);
}

void idio_error_error_message (char const *format, ...)
{
    fprintf (stderr, "ERROR: ");

    va_list fmt_args;
    va_start (fmt_args, format);
    int flen = idio_error_vfprintf (format, fmt_args);
    va_end (fmt_args);

    switch (format[flen-1]) {
    case '\n':
	break;
    default:
	fprintf (stderr, "\n");
    }
}

void idio_error_warning_message (char const *format, ...)
{
    fprintf (stderr, "WARNING: ");

    va_list fmt_args;
    va_start (fmt_args, format);
    int flen = idio_error_vfprintf (format, fmt_args);
    va_end (fmt_args);

    switch (format[flen-1]) {
    case '\n':
	break;
    default:
	fprintf (stderr, "\n");
    }
}

/*
 * Code coverage:
 *
 * These two, essentially calling vasprintf() are useful for
 * converting fprintf()-style C arguments into an Idio string.
 *
 * The only external call to idio_error_string() is an "impossible"
 * clause in read.c.
 */
IDIO idio_error_string (char const *format, va_list argp)
{
    char *s;
    if (-1 == idio_vasprintf (&s, format, argp)) {
	idio_error_alloc ("asprintf");
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (s, sh);

    idio_free (s);

    return idio_get_output_string (sh);
}

/*
 * Code coverage:
 *
 * idio_error_printf() is called a lot but I'd like to think those
 * calls could be migrated to an more Idio-centric mode.
 *
 * Possibly by calling idio_snprintf() then some regular Idio error
 * code with the resultant string.
 */
void idio_error_printf (IDIO loc, char const *format, ...)
{
    IDIO_ASSERT (loc);
    IDIO_C_ASSERT (format);
    IDIO_TYPE_ASSERT (string, loc);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg = idio_error_string (format, fmt_args);
    va_end (fmt_args);

    IDIO c = idio_struct_instance (idio_condition_idio_error_type,
				   IDIO_LIST3 (msg,
					       loc,
					       idio_S_nil));
    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

void idio_error_alloc (char const *m)
{
    IDIO_C_ASSERT (m);

    /*
     * This wants to be a lean'n'mean error "handler" as we've
     * (probably) run out of memory.  The chances are {m} is a static
     * string and has been pushed onto the stack so no allocation
     * there.
     *
     * perror(3) ought to be able to work in this situation and in the
     * end we abort(3).
     */
    perror (m);
    abort ();
}

void idio_error_param_type (char const *etype, IDIO who, IDIO c_location)
{
    IDIO_C_ASSERT (etype);
    IDIO_ASSERT (who);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("bad parameter type: '", msh);
    idio_display (who, msh);
    idio_display_C ("' a ", msh);
    idio_display_C ((char *) idio_type2string (who), msh);
    idio_display_C (" is not a ", msh);
    idio_display_C (etype, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

/*
 * A variation on idio_error_param_type() where the message is
 * supplied -- notably so we don't print the value
 */
void idio_error_param_type_msg (char const *msg, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

void idio_error_param_type_msg_args (char const *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    if (idio_S_nil != args) {
	idio_display_C (" ", msh);
	idio_display (args, msh);
    }

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

/*
 * Used by IDIO_TYPE_ASSERT and IDIO_USER_TYPE_ASSERT
 */
void idio_error_param_type_C (char const *etype, IDIO who, char const *file, char const *func, int line)
{
    IDIO_C_ASSERT (etype);
    IDIO_ASSERT (who);

    char c_location[BUFSIZ];
    idio_snprintf (c_location, BUFSIZ, "%s:%s:%d", func, file, line);

    idio_error_param_type (etype, who, idio_string_C (c_location));
}

void idio_error_const_param (char const *type_name, IDIO who, IDIO c_location)
{
    IDIO_C_ASSERT (type_name);
    IDIO_ASSERT (who);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("bad parameter: ", sh);
    idio_display_C (type_name, sh);
    idio_display_C (" (", sh);
    idio_write (who, sh);
    idio_display_C (") is constant", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_const_parameter_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       location,
					       c_location));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

/*
 * Used by IDIO_CONST_ASSERT
 */
void idio_error_const_param_C (char const *type_name, IDIO who, char const *file, char const *func, int line)
{
    IDIO_C_ASSERT (type_name);
    IDIO_ASSERT (who);

    char c_location[BUFSIZ];
    idio_snprintf (c_location, BUFSIZ, "%s:%s:%d", func, file, line);

    idio_error_const_param (type_name, who, idio_string_C (c_location));
}

/*
 * Use idio_error_param_value_exp() for when val should have an
 * expected type
 */
void idio_error_param_value_exp (char const *func, char const *param, IDIO val, char const *exp, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (param);
    IDIO_ASSERT (val);
    IDIO_C_ASSERT (exp);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (func, msh);
    idio_display_C (" ", msh);
    idio_display_C (param, msh);
    idio_display_C ("='", msh);
    idio_display (val, msh);
    idio_display_C ("' a ", msh);
    idio_display_C ((char *) idio_type2string (val), msh);
    idio_display_C (" is not a ", msh);
    idio_display_C (exp, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_value_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

/*
 * Use idio_error_param_value_msg() for when val should have a range
 * of possible values, say
 */
void idio_error_param_value_msg (char const *func, char const *param, IDIO val, char const *msg, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (param);
    IDIO_ASSERT (val);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (func, msh);
    idio_display_C (" ", msh);
    idio_display_C (param, msh);
    idio_display_C ("='", msh);
    idio_display (val, msh);
    idio_display_C ("': ", msh);
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_value_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

/*
 * Use idio_error_param_value_exp() for when val isn't a printable
 * value
 */
void idio_error_param_value_msg_only (char const *func, char const *param, char const *msg, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (param);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (func, msh);
    idio_display_C (" ", msh);
    idio_display_C (param, msh);
    idio_display_C (": ", msh);
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_value_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

void idio_error_param_undefined (IDIO name, IDIO c_location)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display (name, msh);
    idio_display_C (" is undefined", msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_value_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

/*
 * Code coverage:
 *
 * idio_error_param_nil() is slightly anomalous as most user-facing
 * code will check for the correct type being passed in.
 *
 * One place where you can pass an unchecked #n is as the key to a
 * hash table lookup.  Any type is valid as the key to a hash table
 * except #n.
 */
void idio_error_param_nil (char const *func, char const *name, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_param_value_msg_only (func, name, "is nil", c_location);

    /* notreached */
}

/*
 * Should this be idio_error_idio_error() ??
 */
void idio_error (IDIO who, IDIO msg, IDIO args, IDIO c_location)
{
    IDIO_ASSERT (who);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    if (! (idio_isa_string (c_location) ||
	   idio_isa_symbol (c_location))) {
	idio_error_param_type ("string|symbol", c_location, IDIO_C_FUNC_LOCATION ());
    }

    IDIO location = idio_vm_source_location ();

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (msg, sh);
    if (idio_S_nil != args) {
	idio_display_C (" ", sh);
	idio_display (args, sh);
    }
    /*
     * Quick hack for when called by {error} primitive
     */
    if (idio_isa_symbol (c_location)) {
	idio_display_C (" at ", sh);
	idio_display (c_location, sh);
    }

#ifdef IDIO_DEBUG
    if (idio_isa_string (c_location)) {
	idio_display_C (" at ", sh);
	idio_display (c_location, sh);
    }
#endif

    IDIO c = idio_struct_instance (idio_condition_idio_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       location,
					       who));

    idio_raise_condition (idio_S_false, c);
    /* notreached */
}

/*
 * Coding as in the C code doesn't cover the case, usually the
 * default: clause
 */
void idio_coding_error_C (char const *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    idio_error (idio_S_coding, idio_string_C (msg), args, c_location);
    /* notreached */
}

void idio_error_C (char const *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    idio_error (idio_S_internal, idio_string_C (msg), args, c_location);
    /* notreached */
}

IDIO_DEFINE_PRIMITIVE2V_DS ("error", error, (IDIO loc, IDIO msg, IDIO args), "loc msg [detail]", "\
raise an ^idio-error				\n\
						\n\
:param loc: function name			\n\
:type loc: symbol				\n\
:param msg: error message			\n\
:type loc: string				\n\
:param detail: detailed arguments, defaults to ``#n``	\n\
:type detail: list, optional			\n\
						\n\
This does not return!				\n\
")
{
    IDIO_ASSERT (loc);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (symbol, loc);
    IDIO_USER_TYPE_ASSERT (string, msg);

    idio_error (loc, msg, args, loc);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE3V_DS ("error/type", error_type, (IDIO ct, IDIO loc, IDIO msg, IDIO args), "ct loc msg [detail]", "\
raise a `ct` condition				\n\
						\n\
:param ct: condition type			\n\
:type ct: condition type			\n\
:param loc: function name			\n\
:type loc: symbol				\n\
:param msg: error message			\n\
:type loc: string				\n\
:param detail: detailed arguments, defaults to ``#n``	\n\
:type detail: list, optional			\n\
						\n\
This does not return!				\n\
")
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (loc);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (condition_type, ct);
    IDIO_USER_TYPE_ASSERT (symbol, loc);
    IDIO_USER_TYPE_ASSERT (string, msg);

    /*
     * Use a standard format (ie. an existing C function) if we can
     * for consistency.
     *
     * Some of the required conversions are inefficient.
     */
    if (idio_eqp (ct, idio_condition_rt_parameter_type_error_type)) {
	if (idio_isa_pair (args)) {
	    args = IDIO_PAIR_H (args);
	}

	IDIO loc_str = idio_string_C_len (IDIO_SYMBOL_S (loc), IDIO_SYMBOL_BLEN (loc));

	size_t size = 0;
	idio_error_param_type (idio_display_string (msg, &size), args, loc_str);

	return idio_S_notreached;
    }

    IDIO location = idio_vm_source_location ();

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (msg, sh);
    if (idio_S_nil != args) {
	idio_display_C (" ", sh);
	idio_display (args, sh);
    }

    if (idio_isa_symbol (loc)) {
	idio_display_C (" at ", sh);
	idio_display (loc, sh);
    }

    /*
     * XXX
     *
     * This struct_instance creation should be altering the number of
     * arguments based ont he type of condition.
     */
    IDIO c = idio_struct_instance (ct,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       location,
					       args));

    idio_raise_condition (idio_S_false, c);

    return idio_S_notreached;
}

void idio_error_system (char const *func, char const *msg, IDIO args, int err, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    if (NULL != msg) {
	idio_display_C (msg, msh);
	idio_display_C (": ", msh);
    }
    idio_display_C (strerror (err), msh);

    IDIO location = idio_vm_source_location ();

    IDIO dsh = idio_open_output_string_handle_C ();
    if (idio_S_nil != args) {
	idio_display (args, dsh);
#ifdef IDIO_DEBUG
    idio_display_C (": ", dsh);
#endif
    }
#ifdef IDIO_DEBUG
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_system_error_type,
				   IDIO_LIST5 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh),
					       idio_C_int (err),
					       idio_string_C (func)));

    idio_raise_condition (idio_S_true, c);
    /* notreached */
}

void idio_error_system_errno_msg (char const *func, char const *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_system (func, msg, args, errno, c_location);
}

void idio_error_system_errno (char const *func, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_system_errno_msg (func, NULL, args, c_location);
}

void idio_error_divide_by_zero (char const *msg, IDIO nums, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (nums);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_divide_by_zero_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       nums));

    idio_raise_condition (idio_S_true, c);
    /* notreached */
}

void idio_error_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (error);
    IDIO_ADD_PRIMITIVE (error_type);
}

void idio_init_error ()
{
    idio_module_table_register (idio_error_add_primitives, NULL, NULL);

    idio_S_coding = IDIO_SYMBOLS_C_INTERN ("coding");
    idio_gc_protect_auto (idio_S_coding);
    idio_S_internal = IDIO_SYMBOLS_C_INTERN ("internal");
    idio_gc_protect_auto (idio_S_internal);
    idio_S_user = IDIO_SYMBOLS_C_INTERN ("user");
    idio_gc_protect_auto (idio_S_user);
    idio_S_user_code = idio_string_C_len (IDIO_STATIC_STR_LEN ("user code"));
    idio_gc_protect_auto (idio_S_user_code);
}

