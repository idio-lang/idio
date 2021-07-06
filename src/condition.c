/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 */

/**
 * DOC: Idio conditions
 *
 * A thin shim around structs presenting an interpretation of Scheme's
 * SRFI 35/36
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ffi.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "c-type.h"
#include "closure.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "job-control.h"
#include "module.h"
#include "pair.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"

/*
 * We use these *_condition_type_mci in idio_vm_init_thread() to
 * bootstrap the base trap handlers
 */
IDIO idio_condition_condition_type_mci;

/* SRFI-36 */
IDIO idio_condition_condition_type;
IDIO idio_condition_error_type;
IDIO idio_condition_io_error_type;
IDIO idio_condition_io_handle_error_type;
IDIO idio_condition_io_read_error_type;
IDIO idio_condition_io_write_error_type;
IDIO idio_condition_io_closed_error_type;
IDIO idio_condition_io_filename_error_type;
IDIO idio_condition_io_mode_error_type;
IDIO idio_condition_io_malformed_filename_error_type;
IDIO idio_condition_io_file_protection_error_type;
IDIO idio_condition_io_file_is_read_only_error_type;
IDIO idio_condition_io_file_already_exists_error_type;
IDIO idio_condition_io_no_such_file_error_type;
IDIO idio_condition_read_error_type;
IDIO idio_condition_evaluation_error_type;
IDIO idio_condition_string_error_type;

/* Idio */
IDIO idio_condition_idio_error_type;
IDIO idio_condition_system_error_type;

IDIO idio_condition_static_error_type;
IDIO idio_condition_st_variable_error_type;
IDIO idio_condition_st_variable_type_error_type;
IDIO idio_condition_st_function_error_type;
IDIO idio_condition_st_function_arity_error_type;

IDIO idio_condition_runtime_error_type;
IDIO idio_condition_rt_parameter_error_type;
IDIO idio_condition_rt_parameter_type_error_type;
IDIO idio_condition_rt_const_parameter_error_type;
IDIO idio_condition_rt_parameter_value_error_type;
IDIO idio_condition_rt_parameter_nil_error_type;
IDIO idio_condition_rt_variable_error_type;
IDIO idio_condition_rt_variable_unbound_error_type;
IDIO idio_condition_rt_dynamic_variable_error_type;
IDIO idio_condition_rt_dynamic_variable_unbound_error_type;
IDIO idio_condition_rt_environ_variable_error_type;
IDIO idio_condition_rt_environ_variable_unbound_error_type;
IDIO idio_condition_rt_computed_variable_error_type;
IDIO idio_condition_rt_computed_variable_no_accessor_error_type;
IDIO idio_condition_rt_function_error_type;
IDIO idio_condition_rt_function_arity_error_type;
IDIO idio_condition_rt_module_error_type;
IDIO idio_condition_rt_module_unbound_error_type;
IDIO idio_condition_rt_module_symbol_unbound_error_type;
IDIO idio_condition_rt_path_error_type;
IDIO idio_condition_rt_glob_error_type;
IDIO idio_condition_rt_array_error_type;
IDIO idio_condition_rt_hash_error_type;
IDIO idio_condition_rt_hash_key_not_found_error_type;
IDIO idio_condition_rt_number_error_type;
IDIO idio_condition_rt_bignum_error_type;
IDIO idio_condition_rt_bignum_conversion_error_type;
IDIO idio_condition_rt_C_conversion_error_type;
IDIO idio_condition_rt_fixnum_error_type;
IDIO idio_condition_rt_fixnum_conversion_error_type;
IDIO idio_condition_rt_divide_by_zero_error_type;
IDIO idio_condition_rt_bitset_error_type;
IDIO idio_condition_rt_bitset_bounds_error_type;
IDIO idio_condition_rt_bitset_size_mismatch_error_type;
IDIO idio_condition_rt_keyword_error_type;
IDIO idio_condition_rt_libc_error_type;
IDIO idio_condition_rt_libc_format_error_type;
IDIO idio_condition_rt_regex_error_type;
IDIO idio_condition_rt_struct_error_type;
IDIO idio_condition_rt_symbol_error_type;

IDIO idio_condition_rt_command_error_type;
IDIO idio_condition_rt_command_argv_type_error_type;
IDIO idio_condition_rt_command_format_error_type;
IDIO idio_condition_rt_command_env_type_error_type;
IDIO idio_condition_rt_command_exec_error_type;
IDIO idio_condition_rt_command_status_error_type;
IDIO idio_condition_rt_async_command_status_error_type;

IDIO idio_condition_rt_signal_type;

IDIO idio_condition_reset_condition_handler;
IDIO idio_condition_restart_condition_handler;
IDIO idio_condition_default_condition_handler;
IDIO idio_condition_default_rcse_handler;
IDIO idio_condition_default_racse_handler;
IDIO idio_condition_default_SIGCHLD_handler;

IDIO idio_condition_default_handler;

static IDIO idio_condition_define_condition0_string = idio_S_nil;
IDIO idio_condition_define_condition0_dynamic_string = idio_S_nil; /* needed in lib-wrap.c */
static IDIO idio_condition_define_condition1_string = idio_S_nil;
static IDIO idio_condition_define_condition2_string = idio_S_nil;
static IDIO idio_condition_define_condition3_string = idio_S_nil;

IDIO_DEFINE_PRIMITIVE2V_DS ("make-condition-type", make_condition_type, (IDIO name, IDIO parent, IDIO fields), "name parent fields", "\
make a new condition type			\n\
						\n\
:param name: condition type name		\n\
:param parent: parent condition type		\n\
:param fields: condition type fields		\n\
						\n\
:return: new condition type			\n\
						\n\
make a new condition type based on existing condition `parent` with fields `fields`\n\
")
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (parent);
    IDIO_ASSERT (fields);

    /*
     * Test Case: condition-errors/make-condition-type-bad-name-type.idio
     *
     * make-condition-type #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, name);

    if (idio_S_nil != parent) {
	/*
	 * Test Case: condition-errors/make-condition-type-bad-parent-type.idio
	 *
	 * make-condition-type 'foo #t
	 */
	IDIO_USER_TYPE_ASSERT (condition_type, parent);
    }
    /*
     * Test Case: n/a
     *
     * fields is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, fields);

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

IDIO_DEFINE_PRIMITIVE1_DS ("condition-type?", condition_typep, (IDIO o), "o", "\
test if `o` is a condition type			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a condition type #f otherwise\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_condition_type (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("allocate-condition", allocate_condition, (IDIO ct), "ct", "\
allocate a condition of condition type `ct`	\n\
						\n\
:param ct: condition type to allocate		\n\
						\n\
:return: allocated condition			\n\
						\n\
The allocated condition will have fields set to #n\n\
")
{
    IDIO_ASSERT (ct);

    /*
     * Test Case: condition-errors/allocate-condition-bad-type.idio
     *
     * allocate-condition #t
     */
    IDIO_USER_TYPE_ASSERT (condition_type, ct);

    return idio_allocate_struct_instance (ct, 1);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("make-condition", make_condition, (IDIO ct, IDIO values), "ct values", "\
initialize a condition of condition type `ct` with values `values`\n\
						\n\
:param ct: condition type to allocate		\n\
:param values: initial values for condition fields\n\
						\n\
:return: allocated condition			\n\
")
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (values);

    /*
     * Test Case: condition-errors/make-condition-bad-type.idio
     *
     * make-condition #t
     */
    IDIO_USER_TYPE_ASSERT (condition_type, ct);
    /*
     * Test Case: n/a
     *
     * values is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, values);

    return idio_struct_instance (ct, values);
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

IDIO_DEFINE_PRIMITIVE1_DS ("condition?", conditionp, (IDIO o), "o", "\
test if `o` is a condition			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a condition #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_condition (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_condition_isap (IDIO c, IDIO ct)
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (ct);

    IDIO_TYPE_ASSERT (condition, c);
    IDIO_TYPE_ASSERT (condition_type, ct);

    if (idio_struct_instance_isa (c, ct)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE2_DS ("condition-isa?", condition_isap, (IDIO c, IDIO ct), "c ct", "\
test if condition `c` is a condition type `ct`	\n\
						\n\
:param c: condition to test			\n\
:type c: condition				\n\
:param ct: condition type to assert		\n\
:type ct: condition type			\n\
						\n\
:return: #t if `c` is a condition type `ct`, #f otherwise\n\
")
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (ct);

    /*
     * Test Case: condition-errors/condition-isa-bad-condition-type.idio
     *
     * condition-isa? #t #t
     */
    IDIO_USER_TYPE_ASSERT (condition, c);
    /*
     * Test Case: condition-errors/condition-isa-bad-condition-type-type.idio
     *
     * condition-isa? (make-condition ^error) #t
     */
    IDIO_USER_TYPE_ASSERT (condition_type, ct);

    IDIO r = idio_S_false;

    if (idio_condition_isap (c, ct)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("condition-ref", condition_ref, (IDIO c, IDIO field), "c field", "\
return field `field` of condition `c`		\n\
						\n\
:param c: condition				\n\
:param field: field to return			\n\
						\n\
:return: field `field` of `c`			\n\
")
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (field);

    /*
     * Test Case: condition-errors/condition-ref-bad-condition-type.idio
     *
     * condition-ref #t #t
     */
    IDIO_USER_TYPE_ASSERT (condition, c);
    /*
     * Test Case: condition-errors/condition-ref-bad-field-type.idio
     *
     * condition-ref (make-condition ^error) #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, field);

    return idio_struct_instance_ref (c, field);
}

IDIO_DEFINE_PRIMITIVE3_DS ("condition-set!", condition_set, (IDIO c, IDIO field, IDIO value), "c field value", "\
set field `field` of condition `c` to value `value`\n\
						\n\
:param c: condition				\n\
:param field: field to set			\n\
:param value: value to set			\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (field);
    IDIO_ASSERT (value);

    /*
     * Test Case: condition-errors/condition-set-bad-condition-type.idio
     *
     * condition-set! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (condition, c);
    /*
     * Test Case: condition-errors/condition-set-bad-field-type.idio
     *
     * condition-set! (make-condition ^error) #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, field);

    return idio_struct_instance_set (c, field, value);
}

void idio_condition_set_default_handler (IDIO ct, IDIO handler)
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (handler);

    IDIO_TYPE_ASSERT (condition_type, ct);
    IDIO_TYPE_ASSERT (function, handler);

    idio_hash_put (idio_condition_default_handler, ct, handler);
}

IDIO_DEFINE_PRIMITIVE2_DS ("set-default-handler!", set_default_handler, (IDIO ct, IDIO handler), "ct handler", "\
set the default handler for condition type ``ct`` to	\n\
``handler``						\n\
							\n\
If a condition of type ``ct`` is not otherwise handled	\n\
then ``handler`` will be invoked with the continuation.	\n\
							\n\
:param ct: condition type				\n\
:type ct: condition type				\n\
:param handler: handler for the condition type		\n\
:type handler: function				\n\
							\n\
:return: #<unspec>					\n\
")
{
    IDIO_ASSERT (ct);
    IDIO_ASSERT (handler);

    /*
     * Test Case: condition-errors/set-condition-handler-bad-condition-type-type.idio
     *
     * set-condition-handler! #t #t
     */
    IDIO_USER_TYPE_ASSERT (condition_type, ct);
    /*
     * Test Case: condition-errors/set-condition-handler-bad-handler-type.idio
     *
     * set-condition-handler! ^error #t
     */
    IDIO_USER_TYPE_ASSERT (function, handler);

    idio_condition_set_default_handler (ct, handler);

    return idio_S_unspec;
}

void idio_condition_clear_default_handler (IDIO ct)
{
    IDIO_ASSERT (ct);

    IDIO_TYPE_ASSERT (condition_type, ct);

    idio_hash_delete (idio_condition_default_handler, ct);
}

IDIO_DEFINE_PRIMITIVE1_DS ("clear-default-handler!", clear_default_handler, (IDIO ct), "ct", "\
unset the default handler for condition type ``ct``	\n\
							\n\
The default behaviour for conditions of type ``ct`` will\n\
resume.							\n\
							\n\
:param ct: condition type				\n\
:type ct: condition type				\n\
							\n\
:return: #<unspec>					\n\
")
{
    IDIO_ASSERT (ct);

    /*
     * Test Case: condition-errors/clear-condition-handler-bad-condition-type-type.idio
     *
     * clear-condition-handler! #t
     */
    IDIO_USER_TYPE_ASSERT (condition_type, ct);

    idio_condition_clear_default_handler (ct);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("default-SIGCHLD-handler", default_SIGCHLD_handler, (IDIO c), "c", "\
The default handler for an ^rt-signal-SIGCHLD condition		\n\
								\n\
This invokes do-job-notification				\n\
								\n\
:param c: the condition						\n\
:type c: condition instance					\n\
")
{
    IDIO_ASSERT (c);

    /*
     * XXX IDIO_USER_TYPE_ASSERT() will raise a condition if it fails!
     */
    IDIO_USER_TYPE_ASSERT (condition, c);

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (c);

    if (idio_struct_type_isa (sit, idio_condition_rt_signal_type)) {
	IDIO signum_I = IDIO_STRUCT_INSTANCE_FIELDS (c, IDIO_SI_RT_SIGNAL_TYPE_SIGNUM);
	int signum_C = IDIO_C_TYPE_int (signum_I);

	switch (signum_C) {
	case SIGCHLD:
	    return idio_job_control_SIGCHLD_signal_handler ();
	default:
	    /*
	     * Code coverage:
	     *
	     * We need to handle signals properly.  That means being
	     * able to replace and restore signal handlers via the
	     * stack.
	     *
	     * After that we can abuse default-SIGCHLD-handler as the
	     * handler for some other signal.
	     */
	    fprintf (stderr, "default-SIGCHLD-handler: condition signum was %d\n", signum_C);
	    break;
	}
    }

    /*
     * Code coverage:
     *
     * trap ^rt-number-error default-SIGCHLD-handler {
     *   1 / 0
     * }
     */
    idio_raise_condition (idio_S_true, c);

    /*
     * For a continuable continuation, if it gets here, we'll
     * return void because...
     */
    return idio_S_void;
}

/*
 * Some common code.
 *
 * The basic premise is to fall on our sword in the same way the child
 * did thus propgating the exit status up the process tree.
 */
IDIO idio_condition_exit_on_error (IDIO c)
{
    IDIO_ASSERT (c);

    /*
     * XXX IDIO_USER_TYPE_ASSERT() will raise a condition if it fails!
     */
    IDIO_USER_TYPE_ASSERT (condition, c);

    /*
     * Erm, we just happen to know that ^rt-command-status-error is
     * derived from ^idio-error and therefore status is the fourth
     * element after location, message and detail.
     *
     * status is the list (exit x) or (killed y)
     *
     * Should it be the C/pointer?
     */
    IDIO sl = idio_struct_instance_ref_direct (c, 3);

    if (idio_isa_pair (sl)) {
	if (idio_S_exit == IDIO_PAIR_H (sl)) {
	    IDIO st = IDIO_PAIR_HT (sl);
	    if (idio_isa_C_int (st)) {
		int st_C = IDIO_C_TYPE_int (st);

		if (st_C) {
		    exit (st_C);
		}
	    } else {
		idio_debug ("default rcse: status = %s (exit not a C/int)\n", sl);
		fprintf (stderr, "isa %s\n", idio_type2string (st));
		exit (1);
	    }
	} else if (idio_S_killed == IDIO_PAIR_H (sl)) {
	    IDIO sig = IDIO_PAIR_HT (sl);
	    if (idio_isa_C_int (sig)) {
		int sig_C = IDIO_C_TYPE_int (sig);

		kill (getpid (), sig_C);
	    } else {
		idio_debug ("default rcse: status = %s (killed not a C/int)\n", sl);
		exit (1);
	    }
	} else {
	    idio_debug ("default rcse: status = %s\n", sl);
	    exit (1);
	}
    }

    /*
     * This is the default value for (exit 0) -- other conditions will
     * have called exit (x) or kill -y $self
     */
    return idio_S_unspec;
}

/*
 * Let's try to be consistent with condition-report by, uh, calling
 * condition-report
 */
void idio_condition_report (char *prefix, IDIO c)
{
    IDIO_C_ASSERT (prefix);
    IDIO_ASSERT (c);

    IDIO_TYPE_ASSERT (condition, c);

    IDIO cr_cmd = IDIO_LIST4 (idio_module_symbol_value (idio_symbols_C_intern ("condition-report"),
							idio_Idio_module,
							idio_S_nil),
			      idio_string_C (prefix),
			      c,
			      idio_thread_current_error_handle ());

    idio_vm_invoke_C (idio_thread_current_thread (), cr_cmd);
}

IDIO_DEFINE_PRIMITIVE1_DS ("default-rcse-handler", default_rcse_handler, (IDIO c), "c", "\
The default handler for an ^rt-command-status-error condition	\n\
								\n\
This effects an exit-on-error					\n\
								\n\
:param c: the condition						\n\
:type c: condition instance					\n\
:return: as below						\n\
								\n\
If the command exits with a non-zero status (from exit(3) or	\n\
by signal) then we exit the same way.				\n\
								\n\
Otherwise #unspec						\n\
")
{
    IDIO_ASSERT (c);

    /*
     * XXX IDIO_USER_TYPE_ASSERT() will raise a condition if it fails!
     */
    IDIO_USER_TYPE_ASSERT (condition, c);

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (c);

    if (idio_struct_type_isa (sit, idio_condition_rt_command_status_error_type)) {
	return idio_condition_exit_on_error (c);
    }

    /*
     * Code coverage:
     *
     * trap ^rt-number-error default-rcse-handler {
     *   1 / 0
     * }
     */
    idio_raise_condition (idio_S_true, c);

    /*
     * For a continuable continuation, if it gets here, we'll
     * return void because...
     */
    return idio_S_void;
}

IDIO_DEFINE_PRIMITIVE1_DS ("default-racse-handler", default_racse_handler, (IDIO c), "c", "\
The default handler for an ^rt-async-command-status-error condition	\n\
								\n\
This returns #unspec						\n\
								\n\
:param c: the condition						\n\
:type c: condition instance					\n\
:return: #unspec						\n\
								\n\
The default behaviour is to ignore failed asynchronous processes\n\
")
{
    IDIO_ASSERT (c);

    /*
     * XXX IDIO_USER_TYPE_ASSERT() will raise a condition if it fails!
     */
    IDIO_USER_TYPE_ASSERT (condition, c);

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (c);

    if (idio_struct_type_isa (sit, idio_condition_rt_async_command_status_error_type)) {
	idio_condition_report ("default-racse-handler: this async job result has been ignored", c);
	return idio_S_unspec;
    }

    /*
     * Code coverage:
     *
     * trap ^rt-number-error default-racse-handler {
     *   1 / 0
     * }
     */
    idio_raise_condition (idio_S_true, c);

    /*
     * For a continuable continuation, if it gets here, we'll
     * return void because...
     */
    return idio_S_void;
}

IDIO_DEFINE_PRIMITIVE1_DS ("default-condition-handler", default_condition_handler, (IDIO c), "c", "\
Invoke the default handler for condition `c`			\n\
								\n\
If there is no default handler:					\n\
- if the session is interactive then the debugger is invoked	\n\
- otherwise the condition is re-raised				\n\
								\n\
:param c: the condition						\n\
:type c: condition instance					\n\
								\n\
does not return per se						\n\
")
{
    IDIO_ASSERT (c);

    /*
     * XXX IDIO_USER_TYPE_ASSERT() will raise a condition if it fails!
     */
    IDIO_USER_TYPE_ASSERT (condition, c);

    IDIO thr = idio_thread_current_thread ();

    /*
     * Technically we can allow the user to override the SIGCHLD and
     * SIGHUP handlers -- though we'd advise against it.
     */
    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (c);
    while (idio_S_nil != sit) {
	IDIO handler = idio_hash_ref (idio_condition_default_handler, sit);

	if (idio_S_unspec != handler) {
	    return idio_vm_invoke_C (thr, IDIO_LIST2 (handler, c));
	}

	sit = IDIO_STRUCT_TYPE_PARENT (sit);
    }

    /* reset sit in case we changed it above */
    sit = IDIO_STRUCT_INSTANCE_TYPE (c);

    if (idio_struct_type_isa (sit, idio_condition_rt_signal_type)) {
	IDIO signum_I = IDIO_STRUCT_INSTANCE_FIELDS (c, IDIO_SI_RT_SIGNAL_TYPE_SIGNUM);
	int signum_C = IDIO_C_TYPE_int (signum_I);

	switch (signum_C) {
	case SIGCHLD:
	    /* fprintf (stderr, "default-c-h: SIGCHLD -> idio_command_SIGCHLD_signal_handler\n"); */
	    return idio_job_control_SIGCHLD_signal_handler ();
	case SIGHUP:
	    /*
	     * Code coverage:
	     *
	     * Testing this requires proper signal handling.
	     * Otherwise we can quite happily send ourselves a SIGHUP:
	     *
	     * import libc
	     * kill (getpid) SIGHUP
	     *
	     * except the default disposition is to terminate.  Which
	     * ends the test.
	     */
	    return idio_job_control_SIGHUP_signal_handler ();
	default:
	    /*
	     * Code coverage:
	     *
	     * Ditto.  (See SIGHUP above.)
	     */
	    break;
	}
    } else if (idio_struct_type_isa (sit, idio_condition_rt_async_command_status_error_type)) {
	/*
	 * Code coverage:
	 *
	 * There's a separate default-racse-handler, above, which
	 * should capture this condition under normal circumstances.
	 * That makes it hard to get here.
	 *
	 * However, we *are* here in case something has gone wrong
	 * higher up.
	 *
	 * It's not easy to provoke this as something like:
	 *
	 * trap ^rt-async-command-status-error default-condition-handler {
	 *   auto-exit -e 1
	 * }
	 *
	 * has racse fired from inside the context of the SIGCHLD
	 * handler which is below us on the stack.  Although it might
	 * not do anything as its update-status call is neutered by
	 * foreground-job blocking.
	 */
	idio_debug ("default-c-h: ignoring %s\n", c);
	return idio_S_unspec;
    } else if (idio_struct_type_isa (sit, idio_condition_rt_command_status_error_type)) {
	/*
	 * Code coverage:
	 *
	 * There's a separate default-rcse-handler, above, which
	 * should capture this condition under normal circumstances.
	 * That makes it hard to get here.
	 *
	 * However, we *are* here in case something has gone wrong
	 * higher up.
	 *
	 * It's not easy to provoke this as something like:
	 *
	 * trap ^rt-command-status-error default-condition-handler {
	 *   auto-exit -e 1
	 * }
	 *
	 * has rcse fired from inside the context of the SIGCHLD
	 * handler which is below us on the stack.  Although it might
	 * not do anything as its update-status call is neutered by
	 * foreground-job blocking.
	 */
	return idio_condition_exit_on_error (c);
    }

    if (idio_job_control_interactive) {
	/*
	 * Code coverage:
	 *
	 * I suppose we need a way of forging the interactive state,
	 * like Bash's set -i.
	 *
	 * Until then we'll not get code coverage here.
	 */
	idio_condition_report ("default-condition-handler", c);

	if (IDIO_STATE_RUNNING == idio_state) {
	    IDIO cmd = IDIO_LIST1 (idio_module_symbol_value (idio_symbols_C_intern ("debug"),
							     idio_Idio_module,
							     idio_S_nil));

	    IDIO r = idio_vm_invoke_C (thr, cmd);

	    /*
	     * PC for RETURN
	     *
	     * If we were invoked as a condition handler then the stack is
	     * prepared with a return to idio_vm_CHR_pc (which will
	     * POP-TRAP, RESTORE-STATE and RETURN).
	     *
	     *
	     */
	    /* IDIO_THREAD_PC (thr) = idio_vm_CHR_pc + 2; */

	    return r;
	} else {
	    fprintf (stderr, "\ndefault-condition-handler: bootstrap incomplete\n");
	}
    }

    /*
     * Code coverage:
     *
     * We can get here with something like:
     *
     * 1 / 0
     *
     * except it spews "things are going badly" messages on the screen
     * as the restart handler ABORTs the current expression which
     * don't make for good looking tests.
     *
     * "No, honestly, that's what we are expecting to see..."
     */
#ifdef IDIO_DEBUG
    idio_debug ("\ndefault-condition-handler: no handler re-raising %s\n", c);
#endif
    idio_raise_condition (idio_S_true, c);

    idio_debug ("default-condition-handler: returning %s\n", idio_S_void);

    /*
     * For a continuable continuation, if it gets here, we'll
     * return void because...
     */
    return idio_S_void;
}

IDIO_DEFINE_PRIMITIVE1_DS ("restart-condition-handler", restart_condition_handler, (IDIO c), "c", "\
Invoke a VMM restart handler for `c`				\n\
								\n\
:param c: the condition						\n\
:type c: condition instance					\n\
								\n\
does not return per se						\n\
")
{
    IDIO_ASSERT (c);

    /*
     * Code coverage:
     *
     * Things have to be going badly wrong to get here.
     *
     * If this code succeeds then we will be ABORTing the current
     * expression with appropriate verbose remarks to stderr.
     *
     * Not something we expect to see in the tests.
     */

    if (idio_isa_condition (c)) {
	IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (c);

	idio_condition_report ("restart-condition-handler", c);

	/*
	 * Hmm, a timing issue with SIGCHLD?  Should have been caught
	 * in default-condition-handler.
	 */
	if (idio_struct_type_isa (sit, idio_condition_rt_signal_type)) {
	    IDIO signum_I = IDIO_STRUCT_INSTANCE_FIELDS(c, IDIO_SI_RT_SIGNAL_TYPE_SIGNUM);
	    int signum_C = IDIO_C_TYPE_int (signum_I);

	    switch (signum_C) {
	    case SIGCHLD:
		fprintf (stderr, "restart-c-h: SIGCHLD -> idio_command_SIGCHLD_signal_handler\n");
		idio_job_control_SIGCHLD_signal_handler ();
		return idio_S_unspec;
	    case SIGHUP:
		fprintf (stderr, "restart-c-h: SIGHUP -> idio_command_SIGHUP_signal_handler\n");
		idio_job_control_SIGHUP_signal_handler ();
		return idio_S_unspec;
	    default:
		break;
	    }
	} else if (idio_struct_type_isa (sit, idio_condition_rt_async_command_status_error_type)) {
	    /* return idio_command_rcse_handler (c); */
	    idio_debug ("restart-c-h: racse = %s\n", c);
	    fprintf (stderr, "restart-c-h: racse?? =>> #unspec\n");
	    return idio_S_unspec;
	} else if (idio_struct_type_isa (sit, idio_condition_rt_command_status_error_type)) {
	    /* return idio_command_rcse_handler (c); */
	    idio_debug ("restart-c-h: rcse = %s\n", c);
	    fprintf (stderr, "restart-c-h: rcse?? =>> exit-on-error\n");
	    return idio_condition_exit_on_error (c);
	} else if (idio_struct_type_isa (sit, idio_condition_system_error_type)) {
	    return idio_S_unspec;
	}
    }

    /*
     * As the restart-condition-handler we'll go back to #1, the most
     * recent ABORT.
     */
    idio_ai_t krun_p = idio_array_size (idio_vm_krun);
    IDIO krun = idio_S_nil;
    while (krun_p > 1) {
	krun = idio_array_pop (idio_vm_krun);
	idio_debug ("restart-condition-handler: krun: popping %s\n", IDIO_PAIR_HT (krun));
	krun_p--;
    }

    if (idio_isa_pair (krun)) {
	fprintf (stderr, "restart-condition-handler: restoring krun #%td: ", krun_p);
	idio_debug ("%s\n", IDIO_PAIR_HT (krun));
#ifdef IDIO_DEBUG
	idio_vm_thread_state (idio_thread_current_thread ());
#endif
	idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);
	return idio_S_notreached;
    }

    fprintf (stderr, "restart-condition-handler: nothing to restore\n");
#ifdef IDIO_DEBUG
    idio_debug ("\nrestart-condition-handler: re-raising %s\n", c);
    idio_vm_trap_state (idio_thread_current_thread ());
    idio_vm_frame_tree (idio_S_nil);
#endif
    idio_raise_condition (idio_S_true, c);

    /* notreached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("reset-condition-handler", reset_condition_handler, (IDIO c), "c", "\
Reset the VM!							\n\
								\n\
:param c: the condition						\n\
:type c: condition instance					\n\
								\n\
Does not return.						\n\
")
{
    IDIO_ASSERT (c);

    /*
     * Code coverage:
     *
     * Things have to be going very badly wrong to get here and
     * experience suggests they're about to get worse as this code
     * doesn't do a great job in failing successfully.
     *
     * If we do get here we're on the way out anyway.
     */

    IDIO eh = idio_thread_current_error_handle ();
    idio_display_C ("\nreset-condition-handler: ", eh);

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (c);
    idio_display (IDIO_STRUCT_TYPE_NAME (sit), eh);
    idio_display_C (": ", eh);
    idio_display (c, eh);
    idio_display_C ("\n", eh);

    /*
     * As the reset-condition-handler we'll go back to the first krun
     * on the VM's stack which should be ABORT to main.
     */
    idio_ai_t krun_p = idio_array_size (idio_vm_krun);
    IDIO krun = idio_S_nil;
    while (krun_p > 0) {
	krun = idio_array_pop (idio_vm_krun);
	krun_p--;
    }

    idio_exit_status = 1;
    if (idio_isa_pair (krun)) {
	fprintf (stderr, "reset-condition-handler: restoring krun #%td: ", krun_p);
	idio_debug ("%s\n", IDIO_PAIR_HT (krun));
	idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

	return idio_S_notreached;
    }

    fprintf (stderr, "reset-condition-handler: nothing to restore\n");

    fprintf (stderr, "reset-condition-handler/exit (%d)\n", idio_exit_status);
    idio_final ();
    exit (idio_exit_status);

    /*
     * Hmm, I think the sigjmpbuf can get mis-arranged so exit
     * directly as above...
     */
    idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

    return idio_S_notreached;
}

void idio_condition_add_primitives ()
{
    idio_condition_default_handler = IDIO_HASH_EQP (8);
    idio_gc_protect_auto (idio_condition_default_handler);

    IDIO_ADD_PRIMITIVE (make_condition_type);
    IDIO_ADD_PRIMITIVE (condition_typep);

    IDIO_ADD_PRIMITIVE (allocate_condition);
    IDIO_ADD_PRIMITIVE (make_condition);
    IDIO_ADD_PRIMITIVE (conditionp);
    IDIO_ADD_PRIMITIVE (condition_isap);
    IDIO_ADD_PRIMITIVE (condition_ref);
    IDIO_ADD_PRIMITIVE (condition_set);

    IDIO_ADD_PRIMITIVE (set_default_handler);
    IDIO_ADD_PRIMITIVE (clear_default_handler);

    IDIO fvi;
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, reset_condition_handler);
    idio_condition_reset_condition_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, restart_condition_handler);
    idio_condition_restart_condition_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, default_condition_handler);
    idio_condition_default_condition_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, default_rcse_handler);
    idio_condition_default_rcse_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, default_racse_handler);
    idio_condition_default_racse_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, default_SIGCHLD_handler);
    idio_condition_default_SIGCHLD_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
}

void idio_init_condition ()
{
    idio_module_table_register (idio_condition_add_primitives, NULL);

#define IDIO_CONDITION_STRING(c,s) idio_condition_ ## c ## _string = idio_string_C (s); idio_gc_protect_auto (idio_condition_ ## c ## _string);

    IDIO_CONDITION_STRING (define_condition0, "IDIO-DEFINE-CONDITION0");
    IDIO_CONDITION_STRING (define_condition0_dynamic, "IDIO-DEFINE-CONDITION0-DYNAMIC");
    IDIO_CONDITION_STRING (define_condition1, "IDIO-DEFINE-CONDITION1");
    IDIO_CONDITION_STRING (define_condition2, "IDIO-DEFINE-CONDITION2");
    IDIO_CONDITION_STRING (define_condition3, "IDIO-DEFINE-CONDITION3");

    /* SRFI-35-ish */
    IDIO_DEFINE_CONDITION0 (idio_condition_condition_type, IDIO_CONDITION_CONDITION_TYPE_NAME, idio_S_nil);
    IDIO_DEFINE_CONDITION0 (idio_condition_error_type, "^error", idio_condition_condition_type);

    /*
     * We want the fmci of ^condition for the *-condition-handler(s)
     * which means we have to repeat a couple of the actions of the
     * IDIO_DEFINE_CONDITION0 macro.
     */
    IDIO sym = idio_symbols_C_intern (IDIO_CONDITION_CONDITION_TYPE_NAME);
    idio_ai_t gci = idio_vm_constants_lookup_or_extend (sym);
    idio_condition_condition_type_mci = idio_fixnum (gci);

    /* Idio */
    IDIO_DEFINE_CONDITION3 (idio_condition_idio_error_type, "^idio-error", idio_condition_error_type, "message", "location", "detail");

    /* SRFI-36-ish */
    IDIO_DEFINE_CONDITION0 (idio_condition_io_error_type, "^i/o-error", idio_condition_idio_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_io_handle_error_type, "^i/o-handle-error", idio_condition_io_error_type, "handle");
    IDIO_DEFINE_CONDITION0 (idio_condition_io_read_error_type, "^i/o-read-error", idio_condition_io_handle_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_write_error_type, "^i/o-write-error", idio_condition_io_handle_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_closed_error_type, "^i/o-closed-error", idio_condition_io_handle_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_io_filename_error_type, "^i/o-filename-error", idio_condition_io_error_type, "filename");
    IDIO_DEFINE_CONDITION1 (idio_condition_io_mode_error_type, "^i/o-mode-error", idio_condition_io_error_type, "mode");
    IDIO_DEFINE_CONDITION0 (idio_condition_io_malformed_filename_error_type, "^i/o-malformed-filename-error", idio_condition_io_filename_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_protection_error_type, "^i/o-file-protection-error", idio_condition_io_filename_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_is_read_only_error_type, "^i/o-file-is-read-only-error", idio_condition_io_file_protection_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_io_file_already_exists_error_type, "^i/o-file-already-exists-error", idio_condition_io_filename_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_io_no_such_file_error_type, "^i/o-no-such-file-error", idio_condition_io_filename_error_type);

    /* NB. no column or span! */
    IDIO_DEFINE_CONDITION2 (idio_condition_read_error_type, "^read-error", idio_condition_idio_error_type, "line", "position");
    IDIO_DEFINE_CONDITION1 (idio_condition_evaluation_error_type, "^evaluation-error", idio_condition_idio_error_type, "expr");
    IDIO_DEFINE_CONDITION0 (idio_condition_string_error_type, "^string-error", idio_condition_idio_error_type);

    /* Idio */
    IDIO_DEFINE_CONDITION2 (idio_condition_system_error_type, "^system-error", idio_condition_idio_error_type, "errno", "function");

    IDIO_DEFINE_CONDITION0 (idio_condition_static_error_type, "^static-error", idio_condition_idio_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_st_variable_error_type, "^st-variable-error", idio_condition_static_error_type, "name");
    IDIO_DEFINE_CONDITION0 (idio_condition_st_variable_type_error_type, "^st-variable-type-error", idio_condition_st_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_st_function_error_type, "^st-function-error", idio_condition_static_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_st_function_arity_error_type, "^st-function-arity-error", idio_condition_st_function_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_runtime_error_type, "^runtime-error", idio_condition_idio_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_parameter_error_type, "^rt-parameter-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_parameter_type_error_type, "^rt-parameter-type-error", idio_condition_rt_parameter_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_const_parameter_error_type, "^rt-const-parameter-error", idio_condition_rt_parameter_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_parameter_value_error_type, "^rt-parameter-value-error", idio_condition_rt_parameter_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_parameter_nil_error_type, "^rt-parameter-nil-error", idio_condition_rt_parameter_value_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_variable_error_type, "^rt-variable-error", idio_condition_runtime_error_type, "name");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_variable_unbound_error_type, "^rt-variable-unbound-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_dynamic_variable_error_type, "^rt-dynamic-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_dynamic_variable_unbound_error_type, "^rt-dynamic-variable-unbound-error", idio_condition_rt_dynamic_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_environ_variable_error_type, "^rt-environ-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_environ_variable_unbound_error_type, "^rt-environ-variable-unbound-error", idio_condition_rt_environ_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_computed_variable_error_type, "^rt-computed-variable-error", idio_condition_rt_variable_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_computed_variable_no_accessor_error_type, "^rt-computed-variable-no-accessor-error", idio_condition_rt_computed_variable_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_function_error_type, "^rt-function-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_function_arity_error_type, "^rt-function-arity-error", idio_condition_rt_function_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_module_error_type, "^rt-module-error", idio_condition_runtime_error_type, "module");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_module_unbound_error_type, "^rt-module-unbound-error", idio_condition_rt_module_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_module_symbol_unbound_error_type, "^rt-module-symbol-unbound-error", idio_condition_rt_module_error_type, "symbol");

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_path_error_type, "^rt-path-error", idio_condition_runtime_error_type, "pathname");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_glob_error_type, "^rt-glob-error", idio_condition_runtime_error_type, "pattern");

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_command_error_type, "^rt-command-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_argv_type_error_type, "^rt-command-argv-type-error", idio_condition_rt_command_error_type, "arg");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_format_error_type, "^rt-command-format-error", idio_condition_rt_command_error_type, "name");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_env_type_error_type, "^rt-command-env-type-error", idio_condition_rt_command_error_type, "name");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_exec_error_type, "^rt-command-exec-error", idio_condition_rt_command_error_type, "errno");
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_command_status_error_type, IDIO_CONDITION_RCSE_TYPE_NAME, idio_condition_rt_command_error_type, "status");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_async_command_status_error_type, IDIO_CONDITION_RACSE_TYPE_NAME, idio_condition_rt_command_status_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_array_error_type, "^rt-array-error", idio_condition_runtime_error_type, "index");

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_hash_error_type, "^rt-hash-error", idio_condition_runtime_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_hash_key_not_found_error_type, "^rt-hash-key-not-found-error", idio_condition_rt_hash_error_type, "key");

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_number_error_type, "^rt-number-error", idio_condition_runtime_error_type, "number");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_divide_by_zero_error_type, "^rt-divide-by-zero-error", idio_condition_rt_number_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_bignum_error_type, "^rt-bignum-error", idio_condition_rt_number_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_bignum_conversion_error_type, "^rt-bignum-conversion-error", idio_condition_rt_bignum_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_C_conversion_error_type, "^rt-C-conversion-error", idio_condition_rt_number_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_fixnum_error_type, "^rt-fixnum-error", idio_condition_rt_number_error_type);
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_fixnum_conversion_error_type, "^rt-fixnum-conversion-error", idio_condition_rt_fixnum_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_bitset_error_type, "^rt-bitset-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_bitset_bounds_error_type, "^rt-bitset-bounds-error", idio_condition_rt_bitset_error_type, "bit");
    IDIO_DEFINE_CONDITION2 (idio_condition_rt_bitset_size_mismatch_error_type, "^rt-bitset-size-mismatch-error", idio_condition_rt_bitset_error_type, "size1", "size2");

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_keyword_error_type, "^rt-keyword-error", idio_condition_runtime_error_type, "keyword");

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_libc_error_type, "^rt-libc-error", idio_condition_runtime_error_type);
    IDIO_DEFINE_CONDITION1 (idio_condition_rt_libc_format_error_type, "^rt-libc-format-error", idio_condition_rt_libc_error_type, "name");
    IDIO_DEFINE_CONDITION0 (idio_condition_rt_regex_error_type, "^rt-regex-error", idio_condition_rt_libc_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_struct_error_type, "^rt-struct-error", idio_condition_runtime_error_type);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_symbol_error_type, "^rt-symbol-error", idio_condition_runtime_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_signal_type, IDIO_CONDITION_RT_SIGNAL_TYPE_NAME, idio_condition_error_type, "signum");
}

