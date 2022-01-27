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
 * condition.h
 *
 */

#ifndef CONDITION_H
#define CONDITION_H

#define IDIO_CONDITION_CONDITION_TYPE_NAME	"^condition"
#define IDIO_CONDITION_RT_SIGNAL_TYPE_NAME	"^rt-signal"
#define IDIO_CONDITION_RT_SIGCHLD_TYPE_NAME	"^rt-signal-SIGCHLD"
#define IDIO_CONDITION_RCSE_TYPE_NAME		"^rt-command-status-error"
#define IDIO_CONDITION_RACSE_TYPE_NAME		"^rt-async-command-status-error"

/*
 * Indexes into structures for direct references -- not all are
 * created here, only those used in C
 */
/* ^idio-error */
#define IDIO_SI_IDIO_ERROR_TYPE_MESSAGE		0
#define IDIO_SI_IDIO_ERROR_TYPE_LOCATION	1
#define IDIO_SI_IDIO_ERROR_TYPE_DETAIL		2

/* ^read-error = ^idio_error plus */
#define IDIO_SI_READ_ERROR_TYPE_LINE		3
#define IDIO_SI_READ_ERROR_TYPE_POSITION	4

/* ^evaluation-error = ^idio_error plus */
#define IDIO_SI_EVALUATION_ERROR_TYPE_EXPR	3

/* ^system-error = ^idio_error plus */
#define IDIO_SI_SYSTEM_ERROR_TYPE_ERRNO		3
#define IDIO_SI_SYSTEM_ERROR_TYPE_FUNCTION	4

/* ^rt-variable-error = ^idio_error plus */
#define IDIO_SI_RT_VARIABLE_ERROR_TYPE_NAME	3

/* ^rt-signal */
#define IDIO_SI_RT_SIGNAL_TYPE_SIGNUM		0

extern IDIO idio_condition_define_condition0_string;
extern IDIO idio_condition_define_condition1_string;
extern IDIO idio_condition_define_condition2_string;
extern IDIO idio_condition_define_condition3_string;

extern IDIO idio_condition_define_condition0_dynamic_string;

#define IDIO_DEFINE_CONDITION0(v,n,p) {					\
	IDIO sym = idio_symbols_C_intern (n, sizeof (n) - 1);		\
	v = idio_struct_type (sym, p, idio_S_nil);			\
	idio_gc_protect_auto (v);					\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);	\
	idio_ai_t gvi = idio_vm_extend_values ();			\
	idio_module_set_symbol (sym, IDIO_LIST5 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi), idio_Idio_module, idio_condition_define_condition0_string), idio_Idio_module); \
	idio_module_set_symbol_value (sym, v, idio_Idio_module);	\
    }

#define IDIO_DEFINE_CONDITION0_DYNAMIC(v,n,p) {				\
	IDIO sym = idio_symbols_C_intern (n, sizeof (n) - 1);		\
	v = idio_struct_type (sym, p, idio_S_nil);			\
	idio_gc_protect_auto (v);					\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);	\
	idio_ai_t gvi = idio_vm_extend_values ();			\
	idio_module_set_symbol (sym, IDIO_LIST5 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi), idio_Idio_module, idio_condition_define_condition0_dynamic_string), idio_Idio_module); \
	idio_module_set_symbol_value (sym, v, idio_Idio_module);	\
    }

#define IDIO_DEFINE_CONDITION1(v,n,p,f1) {				\
	IDIO sym = idio_symbols_C_intern (n, sizeof (n) - 1);		\
	v = idio_struct_type (sym, p, IDIO_LIST1 (idio_symbols_C_intern (f1, sizeof (f1) - 1))); \
	idio_gc_protect_auto (v);					\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);	\
	idio_ai_t gvi = idio_vm_extend_values ();			\
	idio_module_set_symbol (sym, IDIO_LIST5 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi), idio_Idio_module, idio_condition_define_condition1_string), idio_Idio_module); \
	idio_module_set_symbol_value (sym, v, idio_Idio_module);	\
    }

#define IDIO_DEFINE_CONDITION2(v,n,p,f1,f2) {				\
	IDIO sym = idio_symbols_C_intern (n, sizeof (n) - 1);				\
	v = idio_struct_type (sym, p, IDIO_LIST2 (idio_symbols_C_intern (f1, sizeof (f1) - 1), idio_symbols_C_intern (f2, sizeof (f2) - 1))); \
	idio_gc_protect_auto (v);					\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);	\
	idio_ai_t gvi = idio_vm_extend_values ();			\
	idio_module_set_symbol (sym, IDIO_LIST5 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi), idio_Idio_module, idio_condition_define_condition2_string), idio_Idio_module); \
	idio_module_set_symbol_value (sym, v, idio_Idio_module);	\
    }

#define IDIO_DEFINE_CONDITION3(v,n,p,f1,f2,f3) {			\
	IDIO sym = idio_symbols_C_intern (n, sizeof (n) - 1);				\
	v = idio_struct_type (sym, p, IDIO_LIST3 (idio_symbols_C_intern (f1, sizeof (f1) - 1), idio_symbols_C_intern (f2, sizeof (f2) - 1), idio_symbols_C_intern (f3, sizeof (f3) - 1))); \
	idio_gc_protect_auto (v);					\
	idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);	\
	idio_ai_t gvi = idio_vm_extend_values ();			\
	idio_module_set_symbol (sym, IDIO_LIST5 (idio_S_toplevel, idio_fixnum (gci), idio_fixnum (gvi), idio_Idio_module, idio_condition_define_condition3_string), idio_Idio_module); \
	idio_module_set_symbol_value (sym, v, idio_Idio_module);	\
    }

extern IDIO idio_condition_condition_type_mci;

extern IDIO idio_condition_condition_type;
extern IDIO idio_condition_error_type;
extern IDIO idio_condition_io_error_type;
extern IDIO idio_condition_io_handle_error_type;
extern IDIO idio_condition_io_read_error_type;
extern IDIO idio_condition_io_write_error_type;
extern IDIO idio_condition_io_closed_error_type;
extern IDIO idio_condition_io_filename_error_type;
extern IDIO idio_condition_io_mode_error_type;
extern IDIO idio_condition_io_malformed_filename_error_type;
extern IDIO idio_condition_io_file_protection_error_type;
extern IDIO idio_condition_io_file_is_read_only_error_type;
extern IDIO idio_condition_io_file_already_exists_error_type;
extern IDIO idio_condition_io_no_such_file_error_type;
extern IDIO idio_condition_read_error_type;
extern IDIO idio_condition_evaluation_error_type;
extern IDIO idio_condition_string_error_type;

extern IDIO idio_condition_idio_error_type;
extern IDIO idio_condition_system_error_type;

extern IDIO idio_condition_static_error_type;
extern IDIO idio_condition_st_variable_error_type;
extern IDIO idio_condition_st_variable_type_error_type;
extern IDIO idio_condition_st_function_error_type;
extern IDIO idio_condition_st_function_arity_error_type;

extern IDIO idio_condition_runtime_error_type;
extern IDIO idio_condition_rt_syntax_error_type;
extern IDIO idio_condition_rt_parameter_error_type;
extern IDIO idio_condition_rt_parameter_type_error_type;
extern IDIO idio_condition_rt_const_parameter_error_type;
extern IDIO idio_condition_rt_parameter_value_error_type;
extern IDIO idio_condition_rt_parameter_nil_error_type;
extern IDIO idio_condition_rt_variable_error_type;
extern IDIO idio_condition_rt_variable_unbound_error_type;
extern IDIO idio_condition_rt_dynamic_variable_error_type;
extern IDIO idio_condition_rt_dynamic_variable_unbound_error_type;
extern IDIO idio_condition_rt_environ_variable_error_type;
extern IDIO idio_condition_rt_environ_variable_unbound_error_type;
extern IDIO idio_condition_rt_computed_variable_error_type;
extern IDIO idio_condition_rt_computed_variable_no_accessor_error_type;
extern IDIO idio_condition_rt_function_error_type;
extern IDIO idio_condition_rt_function_arity_error_type;
extern IDIO idio_condition_rt_module_error_type;
extern IDIO idio_condition_rt_module_unbound_error_type;
extern IDIO idio_condition_rt_module_symbol_unbound_error_type;
extern IDIO idio_condition_rt_path_error_type;
extern IDIO idio_condition_rt_glob_error_type;
extern IDIO idio_condition_rt_command_error_type;
extern IDIO idio_condition_rt_command_argv_type_error_type;
extern IDIO idio_condition_rt_command_format_error_type;
extern IDIO idio_condition_rt_command_env_type_error_type;
extern IDIO idio_condition_rt_command_exec_error_type;
extern IDIO idio_condition_rt_command_status_error_type;
extern IDIO idio_condition_rt_async_command_status_error_type;
extern IDIO idio_condition_rt_array_error_type;
extern IDIO idio_condition_rt_hash_error_type;
extern IDIO idio_condition_rt_hash_key_not_found_error_type;
extern IDIO idio_condition_rt_number_error_type;
extern IDIO idio_condition_rt_bignum_error_type;
extern IDIO idio_condition_rt_bignum_conversion_error_type;
extern IDIO idio_condition_rt_C_conversion_error_type;
extern IDIO idio_condition_rt_fixnum_error_type;
extern IDIO idio_condition_rt_fixnum_conversion_error_type;
extern IDIO idio_condition_rt_divide_by_zero_error_type;
extern IDIO idio_condition_rt_bitset_error_type;
extern IDIO idio_condition_rt_bitset_bounds_error_type;
extern IDIO idio_condition_rt_bitset_size_mismatch_error_type;
extern IDIO idio_condition_rt_keyword_error_type;
extern IDIO idio_condition_rt_libc_error_type;
extern IDIO idio_condition_rt_libc_format_error_type;
extern IDIO idio_condition_rt_regex_error_type;
extern IDIO idio_condition_rt_struct_error_type;
extern IDIO idio_condition_rt_symbol_error_type;
extern IDIO idio_condition_rt_load_error_type;
extern IDIO idio_condition_rt_vtable_unbound_error_type;
extern IDIO idio_condition_rt_vtable_method_unbound_error_type;

extern IDIO idio_condition_rt_signal_type;

extern IDIO idio_condition_reset_condition_handler;
extern IDIO idio_condition_restart_condition_handler;
extern IDIO idio_condition_default_condition_handler;
extern IDIO idio_condition_default_rcse_handler;
extern IDIO idio_condition_default_racse_handler;
extern IDIO idio_condition_default_SIGCHLD_handler;

extern IDIO idio_condition_default_handler;

int idio_isa_condition_type (IDIO o);
int idio_isa_condition (IDIO o);

int idio_condition_isap (IDIO c, IDIO ct);
void idio_condition_set_default_handler (IDIO ct, IDIO handler);

void idio_init_condition ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
