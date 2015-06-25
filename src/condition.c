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
 * condition.c
 *
 * A thin shim around structs presenting an interpretation of Scheme's
 * SRFI 35/36
 */

#include "idio.h"

/* SRFI-36 */
IDIO idio_condition_condition_type;
IDIO idio_condition_message_type;
IDIO idio_condition_error_type;
IDIO idio_condition_io_error_type;
IDIO idio_condition_io_handle_error_type;
IDIO idio_condition_io_read_error_type;
IDIO idio_condition_io_write_error_type;
IDIO idio_condition_io_closed_error_type;
IDIO idio_condition_io_filename_error_type;
IDIO idio_condition_io_malformed_filename_error_type;
IDIO idio_condition_io_file_protection_error_type;
IDIO idio_condition_io_file_is_read_only_error_type;
IDIO idio_condition_io_file_already_exists_error_type;
IDIO idio_condition_io_no_such_file_error_type;
IDIO idio_condition_read_error_type;

/* Idio */
IDIO idio_condition_idio_error_type;
IDIO idio_condition_system_error_type;

IDIO idio_condition_static_error_type;
IDIO idio_condition_st_variable_error_type;
IDIO idio_condition_st_variable_type_error_type;
IDIO idio_condition_st_function_error_type;
IDIO idio_condition_st_function_arity_error_type;

IDIO idio_condition_runtime_error_type;
IDIO idio_condition_rt_variable_error_type;
IDIO idio_condition_rt_variable_unbound_error_type;
IDIO idio_condition_rt_dynamic_variable_error_type;
IDIO idio_condition_rt_dynamic_variable_unbound_error_type;
IDIO idio_condition_rt_environ_variable_error_type;
IDIO idio_condition_rt_environ_variable_unbound_error_type;
IDIO idio_condition_rt_function_error_type;
IDIO idio_condition_rt_function_arity_error_type;
IDIO idio_condition_rt_module_error_type;
IDIO idio_condition_rt_module_unbound_error_type;
IDIO idio_condition_rt_module_symbol_unbound_error_type;
IDIO idio_condition_rt_glob_error_type;
IDIO idio_condition_rt_array_bounds_error_type;

IDIO idio_condition_rt_command_exec_error_type;
IDIO idio_condition_rt_command_status_error_type;

IDIO idio_condition_rt_signal_type;

IDIO_DEFINE_PRIMITIVE2V ("make-condition-type", make_condition_type, (IDIO name, IDIO parent, IDIO fields))
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (parent);
    IDIO_ASSERT (fields);

    IDIO_VERIFY_PARAM_TYPE (symbol, name);
    if (idio_S_nil != parent) {
	IDIO_VERIFY_PARAM_TYPE (condition_type, parent);
    }
    IDIO_VERIFY_PARAM_TYPE (list, fields);

    return idio_struct_type (name, parent, fields);
}

int idio_isa_condition_type (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_struct_type (o) &&
	idio_struct_type_isa (o, idio_condition_condition_type)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1 ("condition-type?", condition_typep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_condition_type (o)) {
	r = idio_S_true;
    }

    return r;
}

/* message-condition-type? */
IDIO_DEFINE_PRIMITIVE1 ("message-condition?", message_conditionp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_struct_instance (o) &&
	idio_struct_instance_isa (o, idio_condition_message_type)) {
	r = idio_S_true;
    }

    return r;
}

/* error-condition-type? */
IDIO_DEFINE_PRIMITIVE1 ("error?", errorp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_struct_instance (o) &&
	idio_struct_instance_isa (o, idio_condition_error_type)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("allocate-condition", allocate_condition, (IDIO ct))
{
    IDIO_ASSERT (ct);

    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);

    return idio_allocate_struct_instance (ct, 1);
}

IDIO_DEFINE_PRIMITIVE1V ("make-condition", make_condition, (IDIO ct, IDIO values))
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (values);

    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);
    IDIO_VERIFY_PARAM_TYPE (list, values);

    return idio_struct_instance (ct, values);
}

IDIO idio_condition_idio_error (IDIO message, IDIO location, IDIO detail)
{
    IDIO_ASSERT (message);
    IDIO_ASSERT (location);
    IDIO_ASSERT (detail);

    return idio_struct_instance (idio_condition_idio_error_type, IDIO_LIST3 (message, location, detail));
}

IDIO_DEFINE_PRIMITIVE1V ("%idio-error-condition", idio_error_condition, (IDIO message, IDIO args))
{
    IDIO_ASSERT (message);
    IDIO_ASSERT (args);

    IDIO_VERIFY_PARAM_TYPE (string, message);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_struct_instance (idio_condition_idio_error_type, idio_list_append2 (IDIO_LIST1 (message), args));
}

int idio_isa_condition (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_struct_instance (o) &&
	idio_struct_instance_isa (o, idio_condition_condition_type)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1 ("condition?", conditionp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_condition (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("condition-isa?", condition_isap, (IDIO c, IDIO ct))
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (ct);

    IDIO_VERIFY_PARAM_TYPE (condition, c);
    IDIO_VERIFY_PARAM_TYPE (condition_type, ct);
    
    IDIO r = idio_S_false;

    if (idio_struct_instance_isa (c, ct)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("condition-ref", condition_ref, (IDIO c, IDIO field))
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (field);
    IDIO_VERIFY_PARAM_TYPE (condition, c);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    return idio_struct_instance_ref (c, field);
}

/* condition-ref <condition-message> message */
IDIO_DEFINE_PRIMITIVE1 ("condition-message", condition_message, (IDIO c))
{
    IDIO_ASSERT (c);
    IDIO_VERIFY_PARAM_TYPE (condition, c);

    if (! idio_struct_instance_isa (c, idio_condition_message_type)) {
	idio_error_printf ("not a message condition", c);
	return idio_S_unspec;
    }

    return idio_struct_instance_ref_direct (c, 0);
}

IDIO_DEFINE_PRIMITIVE3 ("condition-set!", condition_set, (IDIO c, IDIO field, IDIO value))
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (field);
    IDIO_ASSERT (value);

    IDIO_VERIFY_PARAM_TYPE (condition, c);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    return idio_struct_instance_set (c, field, value);
}

#define IDIO_DEFINE_CONDITION0(v,n,p) {		\
    IDIO sym = idio_symbols_C_intern (n);	\
    v = idio_struct_type (sym, p, idio_S_nil);	\
    idio_gc_protect (v);			\
    idio_module_toplevel_set_symbol_value (sym, v);	\
    }

#define IDIO_DEFINE_CONDITION1(v,n,p,f1) {	\
    IDIO sym = idio_symbols_C_intern (n);	\
    v = idio_struct_type (sym, p, IDIO_LIST1 (idio_symbols_C_intern (f1)));	\
    idio_gc_protect (v);			\
    idio_module_toplevel_set_symbol_value (sym, v);	\
    }

#define IDIO_DEFINE_CONDITION2(v,n,p,f1,f2) {	\
    IDIO sym = idio_symbols_C_intern (n);	\
    v = idio_struct_type (sym, p, IDIO_LIST2 (idio_symbols_C_intern (f1), idio_symbols_C_intern (f2))); \
    idio_gc_protect (v);			\
    idio_module_toplevel_set_symbol_value (sym, v);	\
    }

#define IDIO_DEFINE_CONDITION3(v,n,p,f1,f2,f3) {	\
    IDIO sym = idio_symbols_C_intern (n);	\
    v = idio_struct_type (sym, p, IDIO_LIST3 (idio_symbols_C_intern (f1), idio_symbols_C_intern (f2), idio_symbols_C_intern (f3))); \
    idio_gc_protect (v);			\
    idio_module_toplevel_set_symbol_value (sym, v);	\
    }

void idio_init_condition ()
{
    /* SRFI-35-ish */
    IDIO_DEFINE_CONDITION0 (idio_condition_condition_type, "^condition", idio_S_nil);
    IDIO_DEFINE_CONDITION1 (idio_condition_message_type, "^message", idio_condition_condition_type, "message");
    IDIO_DEFINE_CONDITION0 (idio_condition_error_type, "^error", idio_condition_condition_type);

    /* Idio */
    IDIO_DEFINE_CONDITION3 (idio_condition_idio_error_type, "^idio-error", idio_condition_error_type, "message", "location", "detail");

    /* SRFI-36-ish */
    IDIO_DEFINE_CONDITION0 (idio_condition_io_error_type, "^i/o-error", idio_condition_idio_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_io_handle_error_type, "^i/o-handle-error", idio_condition_io_error_type, "handle");
    IDIO_DEFINE_CONDITION0 (idio_condition_io_read_error_type, "^i/o-read-error", idio_condition_io_handle_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_write_error_type, "^i/o-write-error", idio_condition_io_handle_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_closed_error_type, "^i/o-closed-error", idio_condition_io_handle_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_io_filename_error_type, "^i/o-filename-error", idio_condition_io_error_type, "filename");
    IDIO_DEFINE_CONDITION0 (idio_condition_io_malformed_filename_error_type, "^i/o-malformed-filename-error", idio_condition_io_filename_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_protection_error_type, "^i/o-file-protection-error", idio_condition_io_filename_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_is_read_only_error_type, "^i/o-file-is-read-only-error", idio_condition_io_file_protection_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_already_exists_error_type, "^i/o-file-already-exists-error", idio_condition_io_filename_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_no_such_file_error_type, "^i/o-no-such-file-error", idio_condition_io_filename_error_type);

    /* NB. no column or span! */
    IDIO_DEFINE_CONDITION2 (idio_condition_read_error_type, "^read-error", idio_condition_idio_error_type, "line", "position");

    /* Idio */
    IDIO_DEFINE_CONDITION1 (idio_condition_system_error_type, "^system-error", idio_condition_idio_error_type, "errno");

    IDIO_DEFINE_CONDITION0 (idio_condition_static_error_type, "^static-error", idio_condition_idio_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_st_variable_error_type, "^st-variable-error", idio_condition_static_error_type, "name");
    IDIO_DEFINE_CONDITION0 (idio_condition_st_variable_type_error_type, "^st-variable-type-error", idio_condition_st_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_st_function_error_type, "^st-function-error", idio_condition_static_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_st_function_arity_error_type, "^st-function-arity-error", idio_condition_st_function_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_runtime_error_type, "^runtime-error", idio_condition_idio_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_variable_error_type, "^rt-variable-error", idio_condition_runtime_error_type, "name");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_variable_unbound_error_type, "^rt-variable-unbound-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_dynamic_variable_error_type, "^rt-dynamic-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_dynamic_variable_unbound_error_type, "^rt-dynamic-variable-unbound-error", idio_condition_rt_dynamic_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_environ_variable_error_type, "^rt-environ-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_environ_variable_unbound_error_type, "^rt-environ-variable-unbound-error", idio_condition_rt_environ_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_function_error_type, "^rt-function-error", idio_condition_static_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_function_arity_error_type, "^rt-function-arity-error", idio_condition_rt_function_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_module_error_type, "^rt-module-error", idio_condition_runtime_error_type, "module");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_module_unbound_error_type, "^rt-module-unbound-error", idio_condition_rt_module_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_module_symbol_unbound_error_type, "^rt-module-symbol-unbound-error", idio_condition_rt_module_error_type, "symbol");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_glob_error_type, "^rt-glob-error", idio_condition_runtime_error_type, "pattern");

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_command_exec_error_type, "^rt-command-exec-error", idio_condition_system_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_status_error_type, "^rt-command-status-error", idio_condition_runtime_error_type, "status");

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_array_bounds_error_type, "^rt-array-bounds-error", idio_condition_runtime_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_signal_type, "^rt-signal", idio_condition_error_type, "signal");
}

void idio_condition_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (make_condition_type);
    IDIO_ADD_PRIMITIVE (condition_typep);
    IDIO_ADD_PRIMITIVE (message_conditionp);
    IDIO_ADD_PRIMITIVE (errorp);

    IDIO_ADD_PRIMITIVE (allocate_condition);
    IDIO_ADD_PRIMITIVE (make_condition);
    IDIO_ADD_PRIMITIVE (idio_error_condition);
    IDIO_ADD_PRIMITIVE (conditionp);
    IDIO_ADD_PRIMITIVE (condition_isap);
    IDIO_ADD_PRIMITIVE (condition_ref);
    IDIO_ADD_PRIMITIVE (condition_message);
    IDIO_ADD_PRIMITIVE (condition_set);
}

void idio_final_condition ()
{
    idio_gc_expose (idio_condition_condition_type);
    idio_gc_expose (idio_condition_message_type);
    idio_gc_expose (idio_condition_error_type);
    idio_gc_expose (idio_condition_idio_error_type);
    idio_gc_expose (idio_condition_io_error_type);
    idio_gc_expose (idio_condition_io_handle_error_type);
    idio_gc_expose (idio_condition_io_read_error_type);
    idio_gc_expose (idio_condition_io_write_error_type);
    idio_gc_expose (idio_condition_io_closed_error_type);
    idio_gc_expose (idio_condition_io_filename_error_type);
    idio_gc_expose (idio_condition_io_malformed_filename_error_type);
    idio_gc_expose (idio_condition_io_file_protection_error_type);
    idio_gc_expose (idio_condition_io_file_is_read_only_error_type);
    idio_gc_expose (idio_condition_io_file_already_exists_error_type);
    idio_gc_expose (idio_condition_io_no_such_file_error_type);
    idio_gc_expose (idio_condition_read_error_type);

    idio_gc_expose (idio_condition_system_error_type);

    idio_gc_expose (idio_condition_static_error_type);
    idio_gc_expose (idio_condition_st_variable_error_type);
    idio_gc_expose (idio_condition_st_variable_type_error_type);
    idio_gc_expose (idio_condition_st_function_error_type);
    idio_gc_expose (idio_condition_st_function_arity_error_type);

    idio_gc_expose (idio_condition_runtime_error_type);
    idio_gc_expose (idio_condition_rt_variable_error_type);
    idio_gc_expose (idio_condition_rt_variable_unbound_error_type);
    idio_gc_expose (idio_condition_rt_dynamic_variable_error_type);
    idio_gc_expose (idio_condition_rt_dynamic_variable_unbound_error_type);
    idio_gc_expose (idio_condition_rt_environ_variable_error_type);
    idio_gc_expose (idio_condition_rt_environ_variable_unbound_error_type);
    idio_gc_expose (idio_condition_rt_function_error_type);
    idio_gc_expose (idio_condition_rt_function_arity_error_type);
    idio_gc_expose (idio_condition_rt_module_error_type);
    idio_gc_expose (idio_condition_rt_module_unbound_error_type);
    idio_gc_expose (idio_condition_rt_module_symbol_unbound_error_type);
    idio_gc_expose (idio_condition_rt_glob_error_type);
    idio_gc_expose (idio_condition_rt_command_exec_error_type);
    idio_gc_expose (idio_condition_rt_command_status_error_type);
    idio_gc_expose (idio_condition_rt_array_bounds_error_type);
    idio_gc_expose (idio_condition_rt_signal_type);
}

