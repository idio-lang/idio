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
 * error.h
 *
 */

#ifndef ERROR_H
#define ERROR_H

#include <stdarg.h>

extern IDIO idio_io_read_exception;

void idio_raise_exception (IDIO e);
IDIO idio_make_exception (IDIO e);

IDIO idio_error_string (char const *format, va_list argp);
void idio_error_printf (IDIO loc, char const *format, ...);
void idio_error_error_message (char const *format, ...);
void idio_error_warning_message (char const *format, ...);
void idio_error_alloc (char const *m);
void idio_error_func_name (IDIO msh, char *prefix, char *suffix);
void idio_error_init (IDIO *mp, IDIO *lp, IDIO *dp, IDIO c_location);
void idio_error_raise_cont (IDIO ct, IDIO args);
void idio_error_raise_noncont (IDIO ct, IDIO args);
void idio_error_param_type (char const *etype, IDIO who, IDIO c_location);
void idio_error_param_type_msg (char const *msg, IDIO c_location);
void idio_error_param_type_msg_args (char const *msg, IDIO args, IDIO c_location);
void idio_error_param_type_C (char const *etype, IDIO who, char const *file, char const *func, int line);
void idio_error_const_param (char const *type_name, IDIO who, IDIO c_location);
void idio_error_const_param_C (char const *type_name, IDIO who, char const *file, char const *func, int line);
void idio_error_param_value_exp (char const *func, char const *param, IDIO val, char const *exp, IDIO c_location);
void idio_error_param_value_msg (char const *func, char const *param, IDIO val, char const *msg, IDIO c_location);
void idio_error_param_value_msg_only (char const *func, char const *param, char const *msg, IDIO c_location);
void idio_error_param_undefined (IDIO name, IDIO c_location);
void idio_error_param_nil (char const *func, char const *name, IDIO c_location);
void idio_error (IDIO who, IDIO msg, IDIO args, IDIO c_location);
void idio_coding_error_C (char const *msg, IDIO args, IDIO c_location);
void idio_error_C (char const *msg, IDIO args, IDIO c_location);
void idio_error_system (char const *func, char const *msg, IDIO args, int err, IDIO c_location);
void idio_error_system_errno_msg (char const *func, char const *msg, IDIO args, IDIO c_location);
void idio_error_system_errno (char const *func, IDIO args, IDIO c_location);
void idio_error_divide_by_zero (char const *msg, IDIO nums, IDIO c_location);

void idio_init_error ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
