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
 * condition.h
 * 
 */

#ifndef CONDITION_H
#define CONDITION_H

extern IDIO idio_condition_condition_type;
extern IDIO idio_condition_message_type;
extern IDIO idio_condition_error_type;
extern IDIO idio_condition_io_error_type;
extern IDIO idio_condition_io_handle_error_type;
extern IDIO idio_condition_io_read_error_type;
extern IDIO idio_condition_io_write_error_type;
extern IDIO idio_condition_io_closed_error_type;
extern IDIO idio_condition_io_filename_error_type;
extern IDIO idio_condition_io_malformed_filename_error_type;
extern IDIO idio_condition_io_file_protection_error_type;
extern IDIO idio_condition_io_file_is_read_only_error_type;
extern IDIO idio_condition_io_file_already_exists_error_type;
extern IDIO idio_condition_io_no_such_file_error_type;
extern IDIO idio_condition_read_error_type;

extern IDIO idio_condition_idio_error_type;
extern IDIO idio_condition_system_error_type;

extern IDIO idio_condition_static_error_type;
extern IDIO idio_condition_st_variable_error_type;
extern IDIO idio_condition_st_variable_type_error_type;
extern IDIO idio_condition_st_function_error_type;
extern IDIO idio_condition_st_function_arity_error_type;

extern IDIO idio_condition_runtime_error_type;
extern IDIO idio_condition_rt_variable_error_type;
extern IDIO idio_condition_rt_variable_unbound_error_type;
extern IDIO idio_condition_rt_dynamic_variable_error_type;
extern IDIO idio_condition_rt_dynamic_variable_unbound_error_type;
extern IDIO idio_condition_rt_environ_variable_error_type;
extern IDIO idio_condition_rt_environ_variable_unbound_error_type;
extern IDIO idio_condition_rt_function_error_type;
extern IDIO idio_condition_rt_function_arity_error_type;
extern IDIO idio_condition_rt_module_error_type;
extern IDIO idio_condition_rt_module_unbound_error_type;
extern IDIO idio_condition_rt_module_symbol_unbound_error_type;
extern IDIO idio_condition_rt_glob_error_type;
extern IDIO idio_condition_rt_command_exec_error_type;
extern IDIO idio_condition_rt_command_status_error_type;
extern IDIO idio_condition_rt_array_bounds_error_type;
extern IDIO idio_condition_rt_bignum_conversion_error_type;
extern IDIO idio_condition_rt_fixnum_conversion_error_type;

extern IDIO idio_condition_rt_signal_type;

int idio_isa_condition_type (IDIO o);
int idio_isa_condition (IDIO o);

IDIO idio_condition_idio_error (IDIO message, IDIO location, IDIO detail);

void idio_init_condition ();
void idio_condition_add_primitives ();
void idio_final_condition ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
