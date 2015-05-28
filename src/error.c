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
 * error.c
 * 
 */

#include "idio.h"

static IDIO idio_S_internal;
static IDIO idio_S_user;

void idio_error_vfprintf (char *format, va_list argp)
{
    vfprintf (stderr, format, argp);
}

IDIO idio_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == vasprintf (&s, format, argp)) {
	idio_signal_exception (idio_S_false, IDIO_LIST1 (idio_string_C ("idio-error-message: vasprintf")));
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (s, sh);
    free (s);

    return idio_get_output_string (sh);
}

void idio_error_message (char *format, ...)
{
    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg = idio_error_string (format, fmt_args);
    va_end (fmt_args);

    IDIO c = idio_condition_idio_error (msg, idio_S_internal, idio_S_nil);
    idio_signal_exception (idio_S_false, c);
    idio_debug ("error-message: condition %s\n", c);
    IDIO_C_ASSERT (0);
}

void idio_warning_message (char *format, ...)
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

void idio_strerror (char *msg)
{
    idio_error_message ("%s: %s", msg, strerror (errno));
}

void idio_error_alloc (IDIO f)
{
    IDIO_ASSERT (f);

    idio_strerror ("general allocation fault");
}

void idio_error_param_nil (char *name)
{
    IDIO_C_ASSERT (name);
    
    idio_error_message ("%s is nil", name);
}

void idio_error_param_type (char *etype, IDIO who)
{
    IDIO_C_ASSERT (etype);
    IDIO_ASSERT (who);

    char *whos = idio_as_string (who, 1);
    idio_error_message ("not a %s: %s", etype, whos);
    free (whos);
}

IDIO idio_error (IDIO who, IDIO msg, IDIO args)
{
    IDIO_ASSERT (who);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args); 

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (msg, sh);

    IDIO_TYPE_ASSERT (list, args); 
    while (idio_S_nil != args) { 
	idio_write_char (IDIO_CHARACTER (' '), sh);
	idio_write (IDIO_PAIR_H (args), sh);
	
    	args = IDIO_PAIR_T (args); 
    	IDIO_TYPE_ASSERT (list, args); 
    } 

    IDIO c = idio_condition_idio_error (idio_get_output_string (sh),
					who,
					idio_S_nil);
    idio_signal_exception (idio_S_false, c);

    fprintf (stderr, "primitive-error: return from signal exception: XXX abort!\n");
    idio_vm_abort_thread (idio_current_thread ());
    return idio_S_unspec;
}

IDIO idio_error_C (char *msg, IDIO args)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args); 

    return idio_error (idio_S_internal, idio_string_C (msg), args);
}

IDIO_DEFINE_PRIMITIVE1V ("error", error, (IDIO msg, IDIO args))
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (args); 
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO who = idio_S_user;
    
    if (idio_isa_symbol (msg)) {
	who = msg;
	msg = idio_list_head (args);
	args = idio_list_tail (args);
    }

    return idio_error (who, msg, args);
}

void idio_init_error ()
{
    idio_S_internal = idio_symbols_C_intern ("internal");
    idio_gc_protect (idio_S_internal);
    idio_S_user = idio_symbols_C_intern ("user");
    idio_gc_protect (idio_S_user);
}

void idio_error_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (error);
}

void idio_final_error ()
{
    idio_gc_expose (idio_S_internal);
    idio_gc_expose (idio_S_user);
}

