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

void idio_error_message (char *format, ...)
{
    fprintf (stderr, "ERROR: ");

    va_list fmt_args;
    va_start (fmt_args, format);
    vfprintf (stderr, format, fmt_args);
    va_end (fmt_args);

    switch (format[strlen(format)-1]) {
    case '\n':
	break;
    default:
	fprintf (stderr, "\n");
    }
    
    IDIO_C_ASSERT (0);
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

    idio_error_message ("not a %s: %s", etype, idio_display_string (who));
}

void idio_error_add_C (const char *s)
{
}
