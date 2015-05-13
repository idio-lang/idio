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

void idio_error_vfprintf (char *format, va_list argp)
{
    vfprintf (stderr, format, argp);
}

void idio_error_message (char *format, ...)
{
    fprintf (stderr, "\n\nERROR: ");

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
    
    idio_signal_exception (0, IDIO_LIST1 (idio_string_C ("idio-error-message")));
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

void idio_error_alloc (IDIO f)
{
    IDIO_ASSERT (f);

    idio_error_message ("general allocation fault: %s", strerror (errno));
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

void idio_error_add_C (const char *s)
{
    idio_error_message (s);
}

IDIO_DEFINE_PRIMITIVE1V ("error", error, (IDIO m, IDIO args))
{
    IDIO_ASSERT (m);
    /* IDIO_ASSERT (args); */
    
    fprintf (stderr, "primitive-error: ");
    char *ms = idio_as_string (m, 1);
    fprintf (stderr, "%s", ms);
    free (ms);

    /* IDIO_TYPE_ASSERT (list, args); */
    /* while (idio_S_nil != args) { */
    /* 	char *s = idio_as_string (IDIO_PAIR_H (args), 1); */
    /* 	fprintf (stderr, " %s", s); */
    /* 	free (s); */
    /* 	args = IDIO_PAIR_T (args); */
    /* 	IDIO_TYPE_ASSERT (list, args); */
    /* } */
    fprintf (stderr, "\n");

    idio_signal_exception (0, m);

    fprintf (stderr, "primitive-error: return from signal exception: XXX abort!\n");
    idio_vm_abort_thread (idio_current_thread ());
    return idio_S_unspec;
}

void idio_init_error ()
{
}

void idio_error_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (error);
}

void idio_final_error ()
{
}

