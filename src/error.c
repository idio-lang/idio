/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

#include "idio.h"

static IDIO idio_S_internal;
static IDIO idio_S_user;
static IDIO idio_S_user_code;

void idio_error_vfprintf (char *format, va_list argp)
{
    vfprintf (stderr, format, argp);
}

IDIO idio_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == vasprintf (&s, format, argp)) {
	idio_error_alloc ("asprintf");
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (s, sh);
    free (s);

    return idio_get_output_string (sh);
}

void idio_error_printf (IDIO loc, char *format, ...)
{
    IDIO_ASSERT (loc);
    IDIO_C_ASSERT (format);
    IDIO_TYPE_ASSERT (string, loc);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg = idio_error_string (format, fmt_args);
    va_end (fmt_args);

    IDIO c = idio_condition_idio_error (msg, loc, idio_S_nil);
    idio_raise_condition (idio_S_false, c);
}

void idio_error_error_message (char *format, ...)
{
    fprintf (stderr, "ERROR: ");

    va_list fmt_args;
    va_start (fmt_args, format);
    idio_error_vfprintf (format, fmt_args);
    va_end (fmt_args);

    switch (format[strlen(format)-1]) {
    case '\n':
	break;
    default:
	fprintf (stderr, "\n");
    }
}

void idio_error_warning_message (char *format, ...)
{
    fprintf (stderr, "WARNING: ");

    va_list fmt_args;
    va_start (fmt_args, format);
    idio_error_vfprintf (format, fmt_args);
    va_end (fmt_args);

    switch (format[strlen(format)-1]) {
    case '\n':
	break;
    default:
	fprintf (stderr, "\n");
    }
}

void idio_error_strerror (char *msg, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_printf (c_location, "%s: %s", msg, strerror (errno));
}

void idio_error_alloc (char *m)
{
    IDIO_C_ASSERT (m);

    /*
     * This wants to be a lean'n'mean error "handler" as we've
     * (probably) run out of memory.  The chances are {m} is a static
     * string and has been pushed onto the stack so no allocation
     * there.
     *
     * perror(3) ought to be able to work in this situation and in the
     * end we assert(0) and therefore abort(3).
     */
    perror (m);
    IDIO_C_ASSERT (0);
}

void idio_error_param_nil (char *name, IDIO c_location)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (name, sh);
    idio_display_C (" is nil", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_nil_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       location,
					       c_location));
    idio_raise_condition (idio_S_false, c);
}

void idio_error_param_type (char *etype, IDIO who, IDIO c_location)
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

    IDIO dsh = idio_open_output_string_handle_C ();

#ifdef IDIO_DEBUG
    idio_display_C (": ", dsh);
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh)));
    idio_raise_condition (idio_S_false, c);
}

/*
 * Used by IDIO_TYPE_ASSERT and IDIO_VERIFY_PARAM_TYPE
 */
void idio_error_param_type_C (char *etype, IDIO who, char *file, const char *func, int line)
{
    IDIO_C_ASSERT (etype);
    IDIO_ASSERT (who);

    char c_location[BUFSIZ];
    sprintf (c_location, "%s:%s:%d", func, file, line);

    idio_error_param_type (etype, who, idio_string_C (c_location));
}

void idio_error_const_param (char *type_name, IDIO who, IDIO c_location)
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
}

/*
 * Used by IDIO_CONST_ASSERT
 */
void idio_error_const_param_C (char *type_name, IDIO who, char *file, const char *func, int line)
{
    IDIO_C_ASSERT (type_name);
    IDIO_ASSERT (who);

    char c_location[BUFSIZ];
    sprintf (c_location, "%s:%s:%d", func, file, line);

    idio_debug ("const: %s\n", who);

    idio_error_const_param (type_name, who, idio_string_C (c_location));
}

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

#ifdef IDIO_DEBUG
    idio_display_C (" at ", sh);
    idio_display (c_location, sh);
#endif

    IDIO c = idio_condition_idio_error (idio_get_output_string (sh),
					location,
					who);

    idio_raise_condition (idio_S_false, c);
}

void idio_error_C (char *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error (idio_S_internal, idio_string_C (msg), args, c_location);
}

IDIO_DEFINE_PRIMITIVE2V ("error", error, (IDIO c_location, IDIO msg, IDIO args))
{
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (symbol, c_location);
    IDIO_VERIFY_PARAM_TYPE (string, msg);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    idio_error (idio_S_user_code, msg, args, c_location);

    return idio_S_notreached;
}

void idio_error_system (char *msg, IDIO args, int err, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);
    if (idio_S_nil != args) {
	idio_display_C (": ", msh);
	idio_display (args, msh);
    }

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (err), dsh);

    IDIO c = idio_struct_instance (idio_condition_system_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_get_output_string (dsh),
					       idio_C_int (err)));
    idio_raise_condition (idio_S_true, c);
}

void idio_error_system_errno (char *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_system (msg, args, errno, c_location);
}

void idio_error_divide_by_zero (char *msg, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_divide_by_zero_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       c_location));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

void idio_init_error ()
{
    idio_S_internal = idio_symbols_C_intern ("internal");
    idio_gc_protect (idio_S_internal);
    idio_S_user = idio_symbols_C_intern ("user");
    idio_gc_protect (idio_S_user);
    idio_S_user_code = idio_string_C ("user code");
    idio_gc_protect (idio_S_user_code);
}

void idio_error_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (error);
}

void idio_final_error ()
{
    idio_gc_expose (idio_S_internal);
    idio_gc_expose (idio_S_user);
    idio_gc_expose (idio_S_user_code);
}

