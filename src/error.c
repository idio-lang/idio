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

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "c-type.h"
#include "closure.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "handle.h"
#include "idio-string.h"
#include "malloc.h"
#include "pair.h"
#include "primitive.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"

static IDIO idio_S_coding;
static IDIO idio_S_internal;
static IDIO idio_S_user;

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

    idio_error_raise_noncont (idio_condition_idio_error_type,
				   IDIO_LIST3 (msg,
					       loc,
					       idio_S_nil));

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

void idio_error_func_name (IDIO lsh, char *prefix, char *suffix)
{
    IDIO_ASSERT (lsh);

    IDIO_TYPE_ASSERT (handle, lsh);

    IDIO thr = idio_thread_current_thread ();
    IDIO func = IDIO_THREAD_FUNC (thr);
    if (idio_isa_primitive (func)) {
	if (NULL != prefix) {
	    idio_display_C (prefix, lsh);
	}

	idio_display_C (IDIO_PRIMITIVE_NAME (func), lsh);

	if (NULL != suffix) {
	    idio_display_C (suffix, lsh);
	}
    } else if (idio_isa_closure (func)) {
	IDIO name = idio_vm_closure_name (func);

	if (idio_S_nil != name) {
	    if (NULL != prefix) {
		idio_display_C (prefix, lsh);
	    }

	    idio_display (name, lsh);

	    if (NULL != suffix) {
		idio_display_C (suffix, lsh);
	    }
	}
    }
}

/*
 * idio_*_error functions are quite boilerplate in structure
 */
void idio_error_init (IDIO *mp, IDIO *lp, IDIO *dp, IDIO c_location)
{
    if (NULL != mp) {
	*mp = idio_open_output_string_handle_C ();
    }
    if (NULL != lp) {
	*lp = idio_open_output_string_handle_C ();
	idio_display (idio_vm_source_location (), *lp);
	idio_error_func_name (*lp, ":", NULL);
    }
    if (NULL != dp) {
	*dp = idio_open_output_string_handle_C ();
#ifdef IDIO_DEBUG
	idio_display (c_location, *dp);
	idio_display_C (":", *dp);
#endif
    }
}

void idio_error_raise_cont (IDIO ct, IDIO args)
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (struct_type, ct);
    IDIO_TYPE_ASSERT (list, args);

#ifdef IDIO_DEBUG_X
    fprintf (stderr, "\nIDIO_DEBUG: NOTICE: deliberate assert(0) raise-cont\n");
    idio_debug ("%s\n", idio_struct_instance (ct, args));
    assert (0);
#endif
    idio_raise_condition (idio_S_true,
			  idio_struct_instance (ct,
						args));
}

void idio_error_raise_noncont (IDIO ct, IDIO args)
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (struct_type, ct);
    IDIO_TYPE_ASSERT (list, args);

#ifdef IDIO_DEBUG_X
    fprintf (stderr, "\nIDIO_DEBUG: NOTICE: deliberate assert(0) raise-noncont\n");
    idio_debug ("%s\n", idio_struct_instance (ct, args));
    assert (0);
#endif
    idio_raise_condition (idio_S_false,
			  idio_struct_instance (ct,
						args));
}

void idio_error_param_type (char const *etype, IDIO who, IDIO c_location)
{
    IDIO_C_ASSERT (etype);
    IDIO_ASSERT (who);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("bad parameter type: '", msh);
    idio_display (who, msh);
    idio_display_C ("' a ", msh);
    idio_display_C ((char *) idio_type2string (who), msh);
    idio_display_C (" is not a ", msh);
    idio_display_C (etype, msh);

    idio_error_raise_noncont (idio_condition_rt_parameter_type_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_noncont (idio_condition_rt_parameter_type_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

    /* notreached */
}

void idio_error_param_type_msg_args (char const *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    if (idio_S_nil != args) {
	idio_display_C (" ", msh);
	idio_display (args, msh);
    }

    idio_error_raise_noncont (idio_condition_rt_parameter_type_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("bad parameter: ", msh);
    idio_display_C (type_name, msh);
    idio_display_C (" (", msh);
    idio_write (who, msh);
    idio_display_C (") is constant", msh);

    idio_error_raise_noncont (idio_condition_rt_const_parameter_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       idio_get_output_string (lsh),
					       idio_get_output_string (dsh)));

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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (func, msh);
    idio_display_C (" ", msh);
    idio_display_C (param, msh);
    idio_display_C ("='", msh);
    idio_display (val, msh);
    idio_display_C ("' a ", msh);
    idio_display_C ((char *) idio_type2string (val), msh);
    idio_display_C (" is not a ", msh);
    idio_display_C (exp, msh);

    idio_error_raise_noncont (idio_condition_rt_parameter_value_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (func, msh);
    idio_display_C (" ", msh);
    idio_display_C (param, msh);
    idio_display_C ("='", msh);
    idio_display (val, msh);
    idio_display_C ("': ", msh);
    idio_display_C (msg, msh);

    idio_error_raise_noncont (idio_condition_rt_parameter_value_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (func, msh);
    idio_display_C (" ", msh);
    idio_display_C (param, msh);
    idio_display_C (": ", msh);
    idio_display_C (msg, msh);

    idio_error_raise_noncont (idio_condition_rt_parameter_value_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

    /* notreached */
}

void idio_error_param_undefined (IDIO name, IDIO c_location)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display (name, msh);
    idio_display_C (" is undefined", msh);

    idio_error_raise_noncont (idio_condition_rt_parameter_value_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display (msg, msh);
    if (idio_S_nil != args) {
	idio_display_C (" ", msh);
	idio_display (args, msh);
    }
    /*
     * Quick hack for when called by {error} primitive
     */
    if (idio_isa_symbol (c_location)) {
	idio_display_C (" at ", msh);
	idio_display (c_location, msh);
    }

    idio_error_raise_noncont (idio_condition_idio_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  who));

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
	char *msg_C = idio_display_string (msg, &size);
	char em[BUFSIZ];
	snprintf (em, BUFSIZ, "%s", msg_C);
	IDIO_GC_FREE (msg_C, size);

	idio_error_param_type (em, args, loc_str);

	return idio_S_notreached;
    } else if (idio_eqp (ct, idio_condition_rt_parameter_value_error_type)) {
	IDIO func = loc;
	IDIO param = msg;
	IDIO val = idio_S_nil;
	msg = idio_S_nil;

	size_t nargs = idio_list_length (args);
	if (nargs > 2) {
	    nargs = 2;
	}
	switch (nargs) {
	case 2:
	    msg = IDIO_PAIR_HT (args);
	    /* fall through */
	case 1:
	    val = IDIO_PAIR_H (args);
	}

	IDIO msh;
	IDIO lsh;
	IDIO dsh;
	idio_error_init (&msh, &lsh, &dsh, IDIO_C_FUNC_LOCATION ());

	idio_display (func, msh);
	idio_display_C (" ", msh);
	idio_display (param, msh);
	idio_display_C ("='", msh);
	idio_display (val, msh);
	idio_display_C ("': ", msh);
	idio_display (msg, msh);

	idio_error_raise_noncont (idio_condition_rt_parameter_value_error_type,
				  IDIO_LIST3 (idio_get_output_string (msh),
					      idio_get_output_string (lsh),
					      idio_get_output_string (dsh)));

	return idio_S_notreached;
    } else if (idio_eqp (ct, idio_condition_rt_parameter_error_type)) {
	IDIO func = loc;
	IDIO param = idio_S_nil;

	size_t nargs = idio_list_length (args);
	if (nargs > 1) {
	    nargs = 1;
	}
	switch (nargs) {
	case 1:
	    param = IDIO_PAIR_H (args);
	}

	IDIO msh;
	IDIO lsh;
	IDIO dsh;
	idio_error_init (&msh, &lsh, &dsh, IDIO_C_FUNC_LOCATION ());

	idio_display (func, msh);
	idio_display_C (" ", msh);
	idio_display (param, msh);
	idio_display_C (" ", msh);
	idio_display (msg, msh);

	idio_error_raise_noncont (idio_condition_rt_parameter_value_error_type,
				  IDIO_LIST3 (idio_get_output_string (msh),
					      idio_get_output_string (lsh),
					      idio_get_output_string (dsh)));

	return idio_S_notreached;
    }

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, loc);

    if (idio_isa_symbol (loc)) {
	idio_display (loc, msh);
	idio_display_C (" ", msh);
    }

    idio_display (msg, msh);
    if (idio_S_nil != args) {
	idio_display_C (" ", msh);
	idio_display (args, msh);
    }

    /*
     * XXX
     *
     * This struct_instance creation should be altering the number of
     * arguments based on the type of condition.
     */
    idio_error_raise_noncont (ct,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  args));

    return idio_S_notreached;
}

void idio_error_system (char const *func, char const *msg, IDIO args, int err, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    if (NULL != msg) {
	idio_display_C (msg, msh);
	idio_display_C (": ", msh);
    }
    idio_display_C (strerror (err), msh);

    if (idio_S_nil != args) {
	idio_display (args, dsh);
    }

    idio_error_raise_cont (idio_condition_system_error_type,
			   IDIO_LIST5 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       idio_C_int (err),
				       idio_string_C (func)));

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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_divide_by_zero_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       nums));

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

    idio_S_coding    = IDIO_SYMBOL ("coding");
    idio_S_internal  = IDIO_SYMBOL ("internal");
    idio_S_user      = IDIO_SYMBOL ("user");
}

