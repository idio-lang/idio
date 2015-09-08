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
 * error.h
 * 
 */

#ifndef ERROR_H
#define ERROR_H

extern IDIO idio_io_read_exception;

void idio_raise_exception (IDIO e);
IDIO idio_make_exception (IDIO e);

IDIO idio_error_string (char *format, va_list argp);
void idio_error_printf (IDIO loc, char *format, ...);
void idio_error_error_message (char *format, ...);
void idio_error_warning_message (char *format, ...);
void idio_error_strerror (char *msg, IDIO loc);
void idio_error_alloc (char *m);
void idio_error_param_nil (char *name, IDIO loc);
void idio_error_param_type (char *etype, IDIO who, IDIO loc);
void idio_error_param_type_C (char *etype, IDIO who, char *file, const char *func, int line);
void idio_error (IDIO who, IDIO msg, IDIO args, IDIO loc);
void idio_error_C (char *msg, IDIO args, IDIO loc);
void idio_error_system (char *msg, IDIO args, int err, IDIO loc);
void idio_error_system_errno (char *msg, IDIO args, IDIO loc);

void idio_init_error ();
void idio_error_add_primitives ();
void idio_final_error ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
