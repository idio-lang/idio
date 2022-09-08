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
 * vm.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "c-type.h"
#include "closure.h"
#include "codegen.h"
#include "command.h"
#include "compile.h"
#include "condition.h"
#include "continuation.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "job-control.h"
#include "keyword.h"
#include "libc-wrap.h"
#include "module.h"
#include "object.h"
#include "pair.h"
#include "path.h"
#include "primitive.h"
#include "read.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm-asm.h"
#include "vm-dasm.h"
#include "vm.h"
#include "vtable.h"

/*
 * Don't overplay our hand in a signal handler.  What's the barest
 * minimum?  We can set (technically, not even read) a sig_atomic_t.
 *
 * https://www.securecoding.cert.org/confluence/display/c/SIG31-C.+Do+not+access+shared+objects+in+signal+handlers
 *
 * What this document doesn't say is if we can set an index in an
 * array of sig_atomic_t.
 *
 * NB Make the array IDIO_LIBC_NSIG + 1 as idio_vm_run1() will be
 * trying to access [IDIO_LIBC_NSIG] itself, not up to IDIO_LIBC_NSIG.
 */
volatile sig_atomic_t idio_vm_signal_record[IDIO_LIBC_NSIG+1];

IDIO idio_vm_module = idio_S_nil;

/**
 * DOC: Some debugging aids.
 *
 * idio_vm_tracing reports the nominal function call and arguments and
 * return value.  You can enable/disable it in code with
 *
 * %%vm-trace {val}.
 *
 * where {val} is the depth of tracing you are interested in.  10 will
 * trace 10 levels of calls deep.
 *
 *
 * idio_vm_dis reports the byte-instruction by byte-instruction flow.
 * You can enable/disable it in code with
 *
 * %%vm-dis {val}
 *
 * It is very^W verbose.
 *
 * You need the compile flag -DIDIO_VM_DIS to use it.
 */
static int idio_vm_tracing_user = 0;
static int idio_vm_tracing_all = 0;
static int idio_vm_tracing = 0;
static char *idio_vm_tracing_in = ">>>>>>>>>>>>>>>>>>>>>>>>>";
static char *idio_vm_tracing_out = "<<<<<<<<<<<<<<<<<<<<<<<<<";
#ifdef IDIO_VM_DIS
static int idio_vm_dis = 0;
#endif
FILE *idio_tracing_FILE;
#ifdef IDIO_VM_DIS
FILE *idio_dasm_FILE;
#endif
int idio_vm_reports = 0;
int idio_vm_reporting = 0;
int idio_vm_tables = 0;

/**
 * DOC:
 *
 * We don't know if some arbitrary code is going to set a global value
 * to be a closure.  If it does, we need to retain the code for the
 * closure.  Hence a global list of all known code.
 *
 * Prologue
 *
 * There is a prologue which defines some universal get-out behaviour
 * (from Queinnec).  idio_vm_FINISH_pc is the PC for the FINISH
 * instruction and idio_prologue_len how big the prologue is.
 *
 * In addition:
 *
 *   idio_vm_NCE_pc	NON-CONT-ERR
 *   idio_vm_CHR_pc	condition handler return
 *   idio_vm_IHR_pc	interrupt handler return
 *   idio_vm_AR_pc	apply return
 */
IDIO_IA_T idio_all_code;
idio_pc_t idio_vm_FINISH_pc;
idio_pc_t idio_vm_NCE_pc;
idio_pc_t idio_vm_CHR_pc;
idio_pc_t idio_vm_IHR_pc;
idio_pc_t idio_vm_AR_pc;
idio_pc_t idio_vm_RETURN_pc;
idio_pc_t idio_prologue_len;

int idio_vm_exit = 0;
int idio_vm_virtualisation_WSL = 0;

/**
 * DOC: Some VM tables:
 *
 * idio_vm_st - symbol table
 *
 *   Most of the time the VM should be happily modifying values --
 *   seems reasonable.  However, from time to time, elements of the C
 *   code base will want to access a value by name (consider the
 *   expander code wanting to get the current value of the
 *   *expander-list*).  Here we want a standard xenv-style symbol
 *   index matching the corresponding value (index).
 *
 *   Usually this table should only have entries for the generic C
 *   code base but might be extended (with a large number of empty
 *   entries) when a shared library module is loaded.
 *
 * idio_vm_cs - constants - all known constants
 *
 *   we can't have numbers, strings, quoted values etc. embedded in
 *   compiled code -- as they are an unknown size which is harder work
 *   for a byte compiler -- but we can embed an integer index into
 *   this table
 *
 *   symbols are also kept in this table.  symbols are a fixed size (a
 *   pointer) but they will have different values from compile to
 *   compile (and potentially from run to run).  So the symbol itself
 *   isn't an idempotent entity to be encoded for the VM and we must
 *   use a fixed index.
 *
 *   A symbol index in the following discussion is an index into the
 *   constants table.
 *
 * idio_vm_ch - constants hash
 *
 *   this is used as a fast lookup into idio_vm_cs
 *
 * idio_vm_vt - values table - all known toplevel values
 *
 *   values is what the *VM* cares about and is the table of all
 *   toplevel values (lexical, dynamic, etc.) and the various
 *   -SET/-REF instructions indirect into this table by an index
 *   (usually a symbol index -- which requires further module-specific
 *   mapping to a value index).
 *
 *   The *evaluator* cares about symbols.  By and large it doesn't
 *   care about values although for primitives and templates it must
 *   set values as soon as it sees them because the templates will be
 *   run during evaluation and need primitives (and other templates)
 *   to be available.
 *
 * continuations -
 *
 *   each call to idio_vm_run() adds a new {krun} to this stack which
 *   gives the restart/reset condition handlers something to go back
 *   to.
 */
IDIO idio_vm_st;
IDIO idio_vm_cs;
IDIO idio_vm_ch;
static IDIO idio_vm_vt;
IDIO idio_vm_ses;
IDIO idio_vm_sps;

idio_xi_t idio_xenvs_size;
idio_xenv_t **idio_xenvs;

static IDIO idio_S_cfw;		/* compile-file-writer */

IDIO idio_vm_krun;

static IDIO idio_vm_signal_handler_name;

static IDIO idio_vm_prompt_tag_type;

static time_t idio_vm_t0;

static IDIO idio_vm_SYM_DEF_string = idio_S_nil;
static IDIO idio_vm_SYM_DEF_gvi0_string = idio_S_nil;
static IDIO idio_vm_SYM_SET_string = idio_S_nil;
static IDIO idio_vm_SYM_SET_gvi0_string = idio_S_nil;
static IDIO idio_vm_SYM_SET_predef_string = idio_S_nil;
static IDIO idio_vm_COMPUTED_SYM_DEF_string = idio_S_nil;
static IDIO idio_vm_COMPUTED_SYM_DEF_gvi0_string = idio_S_nil;
static IDIO idio_vm_EXPANDER_string = idio_S_nil;
static IDIO idio_vm_INFIX_OPERATOR_string = idio_S_nil;
static IDIO idio_vm_POSTFIX_OPERATOR_string = idio_S_nil;
static IDIO idio_vm_PUSH_DYNAMIC_string = idio_S_nil;
static IDIO idio_vm_DYNAMIC_SYM_REF_string = idio_S_nil;
static IDIO idio_vm_DYNAMIC_FUNCTION_SYM_REF_string = idio_S_nil;
static IDIO idio_vm_PUSH_ENVIRON_string = idio_S_nil;
static IDIO idio_vm_ENVIRON_SYM_REF_string = idio_S_nil;
static IDIO idio_vm_anon_string = idio_S_nil;

static struct timespec idio_vm_ts0;
#ifdef IDIO_VM_PROF
static uint64_t idio_vm_ins_counters[IDIO_I_MAX];
static struct timespec idio_vm_ins_call_time[IDIO_I_MAX];
#endif

#define IDIO_THREAD_STACK_PUSH(v)	(idio_array_push (IDIO_THREAD_STACK(thr), v))
#define IDIO_THREAD_STACK_POP()		(idio_array_pop (IDIO_THREAD_STACK(thr)))

typedef enum {
    IDIO_VM_INVOKE_REGULAR_CALL,
    IDIO_VM_INVOKE_TAIL_CALL
} idio_vm_invoke_enum;

static char *idio_vm_panicking = NULL;

void idio_vm_dump_symbols ();
void idio_vm_dump_operators ();
void idio_vm_dump_constants ();
void idio_vm_dump_src_exprs ();
void idio_vm_dump_src_props ();
void idio_vm_dump_values ();
void idio_final_vm ();

void idio_vm_dump_all ()
{
    idio_vm_dump_constants ();
    idio_vm_dump_symbols ();
    idio_vm_dump_operators ();
    idio_vm_dump_src_props ();
    idio_vm_dump_dasm ();

    /*
     * XXX idio_vm_dump_values() will potentially call (*a lot* of)
     * Idio code to convert values to strings for printing.  In one
     * debugging instance it *doubled* the size of the traced code
     * output.
     */
    idio_vm_dump_values ();
}

/*
 * Code coverage:
 *
 * We're not expecting to panic, right?
 */
void idio_vm_panic (IDIO thr, char const *m)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

#ifdef IDIO_GDB_DEBUG
    fprintf (stderr, "\n\nIDIO_GDB_DEBUG: NOTICE: deliberate SIGINT in vm-panic ****\n\n");
    fprintf (stderr, "PANIC: %s\n", m);
    kill (getpid (), SIGINT);
#endif

    /*
     * Not reached!
     *
     *
     * Ha!  Yeah, I wish! ... :(
     */

    fprintf (stderr, "\n\nPANIC: %s\n\n", m);
#ifdef IDIO_VM_DIS
    if (idio_vm_dis) { fprintf (idio_dasm_FILE, "\n\nPANIC: %s\n\n", m); }
#endif

    if (idio_vm_panicking) {
	fprintf (stderr, "VM already panicking for %s\n", idio_vm_panicking);
	exit (-2);
    } else {
	idio_vm_panicking = (char *) m;
	idio_vm_thread_state (thr);

#ifdef IDIO_DEBUG
	idio_vm_reporting = 1;
	idio_vm_dump_all ();
#endif

	idio_final_vm ();
	idio_exit_status = -1;
	idio_vm_restore_exit (idio_k_exit, idio_S_unspec);
	IDIO_C_ASSERT (0);
    }
}

/*
 * Code coverage:
 *
 * No-one expects the Spanish Inquisition.
 */
void idio_vm_error (char const *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_display (args, dsh);

    idio_error_raise_cont (idio_condition_runtime_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_vm_error_function_invoke (char const *msg, IDIO args, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_display (args, dsh);

    idio_error_raise_cont (idio_condition_rt_function_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_vm_function_trace (IDIO_I ins, IDIO thr);
static void idio_vm_error_arity (IDIO_I ins, IDIO thr, size_t const given, size_t const arity, IDIO c_location)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_vm_function_trace (ins, thr);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "incorrect arity: %zd args for an arity-%zd function", given, arity);
    idio_display_C_len (em, eml, msh);

    IDIO func = IDIO_THREAD_FUNC (thr);
    IDIO name;
    if (idio_isa_closure (func)) {
	name = idio_ref_property (func, idio_KW_name, IDIO_LIST1 (idio_S_nil));
	if (idio_S_nil == name) {
	    name = idio_vm_anon_string;
	}
    } else if (idio_isa_primitive (func)) {
	name = idio_string_C_len (IDIO_PRIMITIVE_NAME (func), IDIO_PRIMITIVE_NAME_LEN (func));
    } else {
	/*
	 * Code coverage: strictly it is not possible to get here
	 * because we already checked for closures and primitives.
	 * Coding error?
	 *
	 * Also we can squelch an uninitialised variable error.
	 */
	name = IDIO_STRING ("-?func?-");
    }

    IDIO sigstr = idio_ref_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

    IDIO val = IDIO_THREAD_VAL (thr);

    idio_display_C ("(", dsh);
    idio_display (name, dsh);
    if (idio_S_nil != sigstr) {
	idio_display_C (" ", dsh);
	idio_display (sigstr, dsh);
    }
    idio_display_C (") was called as (", dsh);
    idio_display (name, dsh);
    IDIO args = idio_frame_params_as_list (val);
    while (idio_S_nil != args) {
	idio_display_C (" ", dsh);
	IDIO e = IDIO_PAIR_H (args);

	size_t size = 0;
	char *s = idio_report_string (e, &size, 4, idio_S_nil, 1);
	idio_display_C (s, dsh);
	IDIO_GC_FREE (s, size);

	args = IDIO_PAIR_T (args);
    }
    idio_display_C (")", dsh);

    idio_error_raise_cont (idio_condition_rt_function_arity_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_vm_error_arity_varargs (IDIO_I ins, IDIO thr, size_t const given, size_t const arity, IDIO c_location)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_vm_function_trace (ins, thr);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "incorrect arity: %zd args for an arity-%zd+ function", given, arity);
    idio_display_C_len (em, eml, msh);

    IDIO func = IDIO_THREAD_FUNC (thr);
    IDIO name;
    if (idio_isa_closure (func)) {
	name = idio_ref_property (func, idio_KW_name, IDIO_LIST1 (idio_S_nil));
	if (idio_S_nil == name) {
	    name = idio_vm_anon_string;
	}
    } else if (idio_isa_primitive (func)) {
	name = idio_string_C_len (IDIO_PRIMITIVE_NAME (func), IDIO_PRIMITIVE_NAME_LEN (func));
    } else {
	/*
	 * Code coverage: strictly it is not possible to get here
	 * because we already checked for closures and primitives.
	 * Coding error?
	 *
	 * Also we can squelch an uninitialised variable error.
	 */
	name = IDIO_STRING ("-?func?-");
    }

    IDIO sigstr = idio_ref_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

    IDIO val = IDIO_THREAD_VAL (thr);

    idio_display_C ("(", dsh);
    idio_display (name, dsh);
    if (idio_S_nil != sigstr) {
	idio_display_C (" ", dsh);
	idio_display (sigstr, dsh);
    }
    idio_display_C (") was called as (", dsh);
    idio_display (name, dsh);
    IDIO args = idio_frame_params_as_list (val);
    while (idio_S_nil != args) {
	idio_display_C (" ", dsh);
	IDIO e = IDIO_PAIR_H (args);

	size_t size = 0;
	char *s = idio_report_string (e, &size, 4, idio_S_nil, 1);
	idio_display_C (s, dsh);
	IDIO_GC_FREE (s, size);

	args = IDIO_PAIR_T (args);
    }
    idio_display_C (")", dsh);

    idio_error_raise_cont (idio_condition_rt_function_arity_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

/*
 * Code coverage:
 *
 * I need to remember how to provoke these...
 */
static void idio_error_runtime_unbound (IDIO fsi, IDIO fgvi, IDIO sym, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("no such binding", msh);

    idio_display_C ("si ", dsh);
    idio_display (fsi, dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (fgvi, dsh);

    idio_error_raise_cont (idio_condition_rt_variable_unbound_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

static void idio_error_dynamic_unbound (idio_as_t si, idio_as_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("no such dynamic binding", msh);

    idio_display_C ("si ", dsh);
    IDIO fsi = idio_fixnum (si);
    idio_display (fsi, dsh);
    idio_display_C (" -> gsi ?? ", dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_vm_constants_ref (IDIO_THREAD_XI (idio_thread_current_thread ()), IDIO_FIXNUM_VAL (fsi));

    idio_error_raise_cont (idio_condition_rt_dynamic_variable_unbound_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

static void idio_error_environ_unbound (idio_as_t si, idio_as_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("no such environ binding", msh);

    idio_display_C ("si ", dsh);
    IDIO fsi = idio_fixnum (si);
    idio_display (fsi, dsh);
    idio_display_C (" -> gsi ?? ", dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_vm_constants_ref (IDIO_THREAD_XI (idio_thread_current_thread ()), IDIO_FIXNUM_VAL (fsi));

    idio_error_raise_cont (idio_condition_rt_environ_variable_unbound_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

/*
 * Code coverage:
 *
 * We shouldn't have been able to create a computed variable without
 * accessors.
 *
 * Coding error.
 */
static void idio_vm_error_computed (char const *msg, idio_as_t ci, idio_as_t gvi, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_display_C ("ci ", dsh);
    IDIO fci = idio_fixnum (ci);
    idio_display (fci, dsh);
    idio_display_C (" -> gci ?? ", dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_vm_constants_ref (IDIO_THREAD_XI (idio_thread_current_thread ()), IDIO_FIXNUM_VAL (fci));

    idio_error_raise_cont (idio_condition_rt_computed_variable_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

static void idio_vm_error_computed_no_accessor (char const *msg, idio_as_t ci, idio_as_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("no computed ", msh);
    idio_display_C (msg, msh);
    idio_display_C (" accessor", msh);

    idio_display_C ("ci ", dsh);
    IDIO fci = idio_fixnum (ci);
    idio_display (fci, dsh);
    idio_display_C (" -> gci ?? ", dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_vm_constants_ref (IDIO_THREAD_XI (idio_thread_current_thread ()), IDIO_FIXNUM_VAL (fci));

    idio_error_raise_cont (idio_condition_rt_computed_variable_no_accessor_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

/*
 * Code coverage:
 *
 * Rest easy!  Gets called a lot when things go wrong.
 */
void idio_vm_debug (IDIO thr, char const *prefix, idio_ai_t stack_start)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (prefix);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_debug ("%s\n\n", thr);
    idio_debug ("     src=%s", idio_vm_source_location ());
    fprintf (stderr, " ([%zu].%zd)\n\n", IDIO_THREAD_XI (thr), IDIO_FIXNUM_VAL (IDIO_THREAD_EXPR (thr)));

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_as_t stack_size = idio_array_size (stack);

    if (stack_start < 0) {
	stack_start += stack_size;
    }

    IDIO_C_ASSERT ((idio_as_t) stack_start < stack_size);

    idio_vm_decode_thread (thr);
}

idio_xenv_t *idio_xenv ()
{
    idio_xenv_t *xenv;

    IDIO_GC_ALLOC (xenv, sizeof (idio_xenv_t));

    IDIO_XENV_INDEX (xenv) = idio_xenvs_size;
    IDIO_XENV_EENV (xenv) = idio_S_nil;

    /*
     * special case index 0, the standard VM tables
     */
    if (0 == idio_xenvs_size) {
	IDIO_XENV_DESC (xenv)      = IDIO_STRING ("default execution environment");
	IDIO_XENV_ST (xenv)        = idio_vm_st;
	IDIO_XENV_CS (xenv)        = idio_vm_cs;
	IDIO_XENV_CH (xenv)        = idio_vm_ch;
	IDIO_XENV_VT (xenv)        = idio_vm_vt;
	IDIO_XENV_SES (xenv)       = idio_vm_ses;
	IDIO_XENV_SPS (xenv)       = idio_vm_sps;
	IDIO_XENV_BYTE_CODE (xenv) = idio_all_code;

	idio_gc_protect_auto (IDIO_XENV_DESC (xenv));
    }

    idio_xenvs = idio_realloc (idio_xenvs, (idio_xenvs_size + 1) * sizeof (idio_xenv_t *));
    idio_xenvs[idio_xenvs_size++] = xenv;

    return xenv;
}

/*
 * XXX idio_free_xenv is called after the GC has freed everything
 * including the protected elements of these structures
 */
void idio_free_xenv (idio_xenv_t *xenv)
{
    /*
     * index 0 is the main VM tables which are freed separately
     */
    if (NULL == xenv) {
	return;
    }

    if (0 == IDIO_XENV_INDEX (xenv)) {
	return;
    }

    idio_ia_free (IDIO_XENV_BYTE_CODE (xenv));

    IDIO_GC_FREE (xenv, sizeof (idio_xenv_t));
}

idio_xi_t idio_new_xenv (IDIO desc)
{
    IDIO_ASSERT (desc);

    IDIO_TYPE_ASSERT (string, desc);

    idio_xenv_t *xenv = idio_xenv ();

    IDIO_XENV_DESC (xenv) = desc;
    IDIO_XENV_ST (xenv)   = idio_array (0);
    IDIO_XENV_CS (xenv)   = idio_array (0);
    IDIO_XENV_CH (xenv)   = IDIO_HASH_EQP (8);
    IDIO_XENV_VT (xenv)   = idio_array (0);
    IDIO_XENV_SES (xenv)  = idio_array (0);
    IDIO_XENV_SPS (xenv)  = idio_array (0);

    idio_gc_protect_auto (IDIO_XENV_DESC (xenv));
    idio_gc_protect_auto (IDIO_XENV_ST (xenv));
    idio_gc_protect_auto (IDIO_XENV_CS (xenv));
    idio_gc_protect_auto (IDIO_XENV_CH (xenv));
    idio_gc_protect_auto (IDIO_XENV_VT (xenv));
    idio_gc_protect_auto (IDIO_XENV_SES (xenv));
    idio_gc_protect_auto (IDIO_XENV_SPS (xenv));

    IDIO_XENV_BYTE_CODE (xenv)      = idio_ia (100);
    idio_codegen_code_prologue (IDIO_XENV_BYTE_CODE (xenv));

    return IDIO_XENV_INDEX (xenv);
}

static void idio_vm_invoke (IDIO thr, IDIO func, idio_vm_invoke_enum tailp);

/*
 * Reading numbers from the byte code.  Broadly numbers of a fixed
 * width (1 through 8 bytes) and numbers of a variable width (1
 * through 9 bytes).
 *
 * What complicates the issue is that we have two requestors, the VM
 * and the disassembler.  They both maintain a program counter, PC,
 * and we use &PC in these functions.
 *
 * The disassembler can call the *get* functions directly, passing its
 * &PC and then there's a series of VM convenience functions, *fetch*,
 * which inject the thread's PC.
 *
 * We should probably ditch the convenience functions as I regularly
 * forget which is which.
 */

static uint64_t idio_vm_read_fixuint (IDIO_IA_T bc, int const n, size_t const offset)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    int i;
    uint64_t r = 0;
    for (i = 0; i < n; i++) {
	r <<= 8;
	r |= IDIO_IA_AE (bc, offset + i);
    }

    return r;
}

uint64_t idio_vm_get_varuint (IDIO_IA_T bc, idio_pc_t *pcp)
{
    int i = IDIO_IA_GET_NEXT (bc, pcp);
    if (i <= 240) {
	return i;
    } else if (i <= 248) {
	int j = IDIO_IA_GET_NEXT (bc, pcp);

	return (240 + 256 * (i - 241) + j);
    } else if (249 == i) {
	int j = IDIO_IA_GET_NEXT (bc, pcp);
	int k = IDIO_IA_GET_NEXT (bc, pcp);

	return (2288 + 256 * j + k);
    } else {
	int n = (i - 250) + 3;

	uint64_t r = 0;
	for (i = 0; i < n; i++) {
	    r <<= 8;
	    r |= IDIO_IA_GET_NEXT (bc, pcp);
	}

	return r;
    }
}

static uint64_t idio_vm_get_fixuint (IDIO_IA_T bc, int n, idio_pc_t *pcp)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    uint64_t r = idio_vm_read_fixuint (bc, n, *pcp);
    *pcp += n;

    return r;
}

static uint64_t idio_vm_get_8uint (IDIO_IA_T bc, idio_pc_t *pcp)
{
    return idio_vm_get_fixuint (bc, 1, pcp);
}

uint64_t idio_vm_get_16uint (IDIO_IA_T bc, idio_pc_t *pcp)
{
    return idio_vm_get_fixuint (bc, 2, pcp);
}

static uint64_t idio_vm_get_32uint (IDIO_IA_T bc, idio_pc_t *pcp)
{
    return idio_vm_get_fixuint (bc, 4, pcp);
}

static uint64_t idio_vm_get_64uint (IDIO_IA_T bc, idio_pc_t *pcp)
{
    return idio_vm_get_fixuint (bc, 8, pcp);
}

static uint64_t idio_vm_fetch_varuint (IDIO_IA_T bc, IDIO thr)
{
    return idio_vm_get_varuint (bc, &(IDIO_THREAD_PC (thr)));
}

static uint64_t idio_vm_fetch_fixuint (IDIO_IA_T bc, int n, IDIO thr)
{
    return idio_vm_get_fixuint (bc, n, &(IDIO_THREAD_PC (thr)));
}

static uint64_t idio_vm_fetch_8uint (IDIO thr, IDIO_IA_T bc)
{
    return idio_vm_fetch_fixuint (bc, 1, thr);
}

uint64_t idio_vm_fetch_16uint (IDIO thr, IDIO_IA_T bc)
{
    return idio_vm_fetch_fixuint (bc, 2, thr);
}

static uint64_t idio_vm_fetch_32uint (IDIO thr, IDIO_IA_T bc)
{
    return idio_vm_fetch_fixuint (bc, 4, thr);
}

static uint64_t idio_vm_fetch_64uint (IDIO thr, IDIO_IA_T bc)
{
    return idio_vm_fetch_fixuint (bc, 8, thr);
}

/*
 * For a function with varargs, ie.
 *
 * (define (func x & rest) ...)
 *
 * we need to rewrite the call such that the non-mandatory args are
 * bundled up as a list:
 *
 * (func a b c d) => (func a (b c d))
 */
static void idio_vm_listify (IDIO frame, size_t const arity)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);

    size_t index = IDIO_FRAME_NPARAMS (frame);
    IDIO result = idio_S_nil;

    for (;;) {
	if (arity == index) {
	    IDIO_FRAME_ARGS (frame, arity) = result;
	    return;
	} else {
	    result = idio_pair (IDIO_FRAME_ARGS (frame, index - 1),
				result);
	    index--;
	}
    }
}

static void idio_vm_preserve_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_array_push_n (IDIO_THREAD_STACK (thr),
		       3,
		       IDIO_THREAD_FRAME (thr),
		       IDIO_THREAD_ENV (thr),
		       idio_SM_preserve_state);
}

static void idio_vm_preserve_all_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_vm_preserve_state (thr);
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_REG1 (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_REG2 (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_EXPR (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_FUNC (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
    IDIO_THREAD_STACK_PUSH (idio_SM_preserve_all_state);
}

static void idio_vm_restore_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_vm_debug (thr, "ivrs", -5); */

    idio_sp_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_preserve_state != marker) {
	idio_debug ("iv_restore_state: marker: expected idio_SM_preserve_state not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv_restore_state: unexpected stack marker");
    }
    ss--;

    IDIO_THREAD_ENV (thr) = IDIO_THREAD_STACK_POP ();
    if (idio_S_nil != IDIO_THREAD_ENV (thr)) {
	if (! idio_isa_module (IDIO_THREAD_ENV (thr))) {
	    idio_debug ("\n\n****\nvm-restore-state: env = %s ?? -- not a module\n", IDIO_THREAD_ENV (thr));
	    idio_vm_decode_thread (thr);
	    idio_vm_debug (thr, "vm-restore-state", 0);
	    idio_vm_reset_thread (thr, 1);
	    return;
	}
	IDIO_TYPE_ASSERT (module, IDIO_THREAD_ENV (thr));
    }
    ss--;

    IDIO_THREAD_FRAME (thr) = IDIO_THREAD_STACK_POP ();
    if (idio_S_nil != IDIO_THREAD_FRAME (thr)) {
	IDIO_TYPE_ASSERT (frame, IDIO_THREAD_FRAME (thr));
    }
    ss--;
}

static void idio_vm_restore_all_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_debug ("iv-restore-all-state: THR %s\n", thr); */
    /* idio_debug ("iv-restore-all-state: STK %s\n", IDIO_THREAD_STACK (thr)); */
    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_preserve_all_state != marker) {
	idio_debug ("iv-restore-all-state: marker: expected idio_SM_preserve_all_state not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv-restore-all-state: unexpected stack marker");
    }
    IDIO_THREAD_VAL (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_FUNC (thr) = IDIO_THREAD_STACK_POP ();

    if (0 == idio_job_control_interactive) {
	/*
	 * This verification of _FUNC() needs to be in sync with what
	 * idio_vm_invoke() allows
	 */
	if (! (idio_isa_function (IDIO_THREAD_FUNC (thr)) ||
	       idio_isa_string (IDIO_THREAD_FUNC (thr)) ||
	       idio_isa_symbol (IDIO_THREAD_FUNC (thr)) ||
	       idio_isa_continuation (IDIO_THREAD_FUNC (thr)) ||
	       idio_isa_generic (IDIO_THREAD_FUNC (thr)))) {
	    /*
	     * XXX what should we do here?
	     *
	     * This can be triggered by ``#f 10`` and, if we are
	     * interactive, should just be a condition-report followed
	     * by a restore to the top-level.
	     *
	     * The underlying problem is that this continuation is
	     * being restored from within the default/restore/reset
	     * handler and calling idio_error_param_*() will
	     * immediately call the outer handler.
	     *
	     * The interactive user will already have had a
	     * condition-report, they don't need any more.
	     */

	    idio_debug ("iv-ras: func is not invokable: %s\n", IDIO_THREAD_FUNC (thr));
	    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_FUNC (thr));
	    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
	    IDIO_THREAD_STACK_PUSH (marker);
#ifdef IDIO_DEBUG
	    idio_vm_thread_state (thr);
#endif

	    idio_error_param_value_msg ("VM/RESTORE", "func", IDIO_THREAD_FUNC (thr), "not an invokable value", IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    IDIO_THREAD_EXPR (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_EXPR (thr));
    IDIO_THREAD_REG2 (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_REG1 (thr) = IDIO_THREAD_STACK_POP ();
    idio_vm_restore_state (thr);
}

#ifdef IDIO_VM_PROF
static struct timespec idio_vm_clos_t0;
static struct rusage idio_vm_clos_ru0;
static IDIO idio_vm_clos = NULL;

/*
 * Code coverage:
 *
 * Clearly nothing is going to happen unless IDIO_VM_PROF is enabled!
 */
void idio_vm_func_start (IDIO func, struct timespec *tsp, struct rusage *rup)
{
    IDIO_ASSERT (func);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    /*
	     * Test Case: ??
	     *
	     * idio_vm_invoke() should have beaten us to this.
	     */
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	    idio_vm_clos = func;
	    IDIO_CLOSURE_CALLED (idio_vm_clos)++;
	    if (clock_gettime (CLOCK_MONOTONIC, &idio_vm_clos_t0) < 0) {
		perror ("vm-func-start: clock_gettime (CLOCK_MONOTONIC, idio_vm_clos_t0)");
	    }
	    if (getrusage (RUSAGE_SELF, &idio_vm_clos_ru0) < 0) {
		perror ("vm-func-start: getrusage (RUSAGE_SELF, idio_vm_clos_ru0)");
	    }
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    IDIO_PRIMITIVE_CALLED (func)++;
	    IDIO_C_ASSERT (tsp);
	    if (clock_gettime (CLOCK_MONOTONIC, tsp) < 0) {
		perror ("clock_gettime (CLOCK_MONOTONIC, tsp)");
	    }
	    IDIO_C_ASSERT (rup);
	    if (getrusage (RUSAGE_SELF, rup)) {
		perror ("getrusage (RUSAGE_SELF, rup)");
	    }
	}
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * idio_vm_invoke() should have beaten us to this.
	     */
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST1 (func),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

void idio_vm_func_stop (IDIO func, struct timespec *tsp, struct rusage *rup)
{
    IDIO_ASSERT (func);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    /*
	     * Test Case: ??
	     *
	     * idio_vm_invoke() should have beaten us to this.
	     */
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    IDIO_C_ASSERT (tsp);
	    if (clock_gettime (CLOCK_MONOTONIC, tsp) < 0) {
		perror ("clock_gettime (CLOCK_MONOTONIC, tsp)");
	    }
	    IDIO_C_ASSERT (rup);
	    if (getrusage (RUSAGE_SELF, rup)) {
		perror ("getrusage (RUSAGE_SELF, rup)");
	    }
	}
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * idio_vm_invoke() should have beaten us to this.
	     */
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST1 (func),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

static void idio_vm_clos_time (IDIO thr, char const *context)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (NULL == idio_vm_clos) {
	return;
    }

    if (0 == idio_vm_clos->type) {
	/*
	 * closure stashed in idio_vm_clos has been recycled before we
	 * got round to updating its timings
	 */
	return;
    }

    if (! idio_isa_closure (idio_vm_clos)) {
	/*
	 * closure stashed in idio_vm_clos has been recycled before we
	 * got round to updating its timings
	 */
	return;
    }

    struct timespec clos_te;
    if (clock_gettime (CLOCK_MONOTONIC, &clos_te) < 0) {
	perror ("vm-clos-time: clock_gettime (CLOCK_MONOTONIC, clos_te)");
    }

    struct rusage clos_rue;
    if (getrusage (RUSAGE_SELF, &clos_rue) < 0) {
	perror ("vm-clos-time: getrusage (RUSAGE_SELF, clos_rue)");
    }

    struct timespec ts_d;
    /* elapsed */
    ts_d.tv_sec = clos_te.tv_sec - idio_vm_clos_t0.tv_sec;
    ts_d.tv_nsec = clos_te.tv_nsec - idio_vm_clos_t0.tv_nsec;
    if (ts_d.tv_nsec < 0) {
	ts_d.tv_nsec += IDIO_VM_NS;
	ts_d.tv_sec -= 1;
    }

    IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_sec += ts_d.tv_sec;
    IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_nsec += ts_d.tv_nsec;
    if (IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_nsec >= IDIO_VM_NS) {
	IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_nsec -= IDIO_VM_NS;
	IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_sec += 1;
    }

    struct timeval tv_d;
    /* User */
    tv_d.tv_sec = clos_rue.ru_utime.tv_sec - idio_vm_clos_ru0.ru_utime.tv_sec;
    tv_d.tv_usec = clos_rue.ru_utime.tv_usec - idio_vm_clos_ru0.ru_utime.tv_usec;
    if (tv_d.tv_usec < 0) {
	tv_d.tv_usec += IDIO_VM_US;
	tv_d.tv_sec -= 1;
    }

    IDIO_CLOSURE_RU_UTIME (idio_vm_clos).tv_sec += tv_d.tv_sec;
    IDIO_CLOSURE_RU_UTIME (idio_vm_clos).tv_usec += tv_d.tv_usec;
    if (IDIO_CLOSURE_RU_UTIME (idio_vm_clos).tv_usec >= IDIO_VM_US) {
	IDIO_CLOSURE_RU_UTIME (idio_vm_clos).tv_usec -= IDIO_VM_US;
	IDIO_CLOSURE_RU_UTIME (idio_vm_clos).tv_sec += 1;
    }

    /* Sys */
    tv_d.tv_sec = clos_rue.ru_stime.tv_sec - idio_vm_clos_ru0.ru_stime.tv_sec;
    tv_d.tv_usec = clos_rue.ru_stime.tv_usec - idio_vm_clos_ru0.ru_stime.tv_usec;
    if (tv_d.tv_usec < 0) {
	tv_d.tv_usec += IDIO_VM_US;
	tv_d.tv_sec -= 1;
    }

    IDIO_CLOSURE_RU_STIME (idio_vm_clos).tv_sec += tv_d.tv_sec;
    IDIO_CLOSURE_RU_STIME (idio_vm_clos).tv_usec += tv_d.tv_usec;
    if (IDIO_CLOSURE_RU_STIME (idio_vm_clos).tv_usec >= IDIO_VM_US) {
	IDIO_CLOSURE_RU_STIME (idio_vm_clos).tv_usec -= IDIO_VM_US;
	IDIO_CLOSURE_RU_STIME (idio_vm_clos).tv_sec += 1;
    }

    idio_vm_clos = NULL;
}

void idio_vm_prim_time (IDIO func, struct timespec *ts0p, struct timespec *tsep, struct rusage *ru0p, struct rusage *ruep)
{
    IDIO_ASSERT (func);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    /*
	     * Test Case: ??
	     *
	     * idio_vm_invoke() should have beaten us to this.
	     */
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    IDIO_C_ASSERT (ts0p);
	    IDIO_C_ASSERT (tsep);

	    struct timespec ts_d;
	    /* Elapsed */
	    ts_d.tv_sec = tsep->tv_sec - ts0p->tv_sec;
	    ts_d.tv_nsec = tsep->tv_nsec - ts0p->tv_nsec;
	    if (ts_d.tv_nsec < 0) {
		ts_d.tv_nsec += IDIO_VM_NS;
		ts_d.tv_sec -= 1;
	    }

	    IDIO_PRIMITIVE_CALL_TIME (func).tv_sec += ts_d.tv_sec;
	    IDIO_PRIMITIVE_CALL_TIME (func).tv_nsec += ts_d.tv_nsec;
	    if (IDIO_PRIMITIVE_CALL_TIME (func).tv_nsec >= IDIO_VM_NS) {
		IDIO_PRIMITIVE_CALL_TIME (func).tv_nsec -= IDIO_VM_NS;
		IDIO_PRIMITIVE_CALL_TIME (func).tv_sec += 1;
	    }

	    IDIO_C_ASSERT (ru0p);
	    IDIO_C_ASSERT (ruep);

	    struct timeval tv_d;
	    /* User */
	    tv_d.tv_sec = ruep->ru_utime.tv_sec - ru0p->ru_utime.tv_sec;
	    tv_d.tv_usec = ruep->ru_utime.tv_usec - ru0p->ru_utime.tv_usec;
	    if (tv_d.tv_usec < 0) {
		tv_d.tv_usec += IDIO_VM_US;
		tv_d.tv_sec -= 1;
	    }

	    IDIO_PRIMITIVE_RU_UTIME (func).tv_sec += tv_d.tv_sec;
	    IDIO_PRIMITIVE_RU_UTIME (func).tv_usec += tv_d.tv_usec;
	    if (IDIO_PRIMITIVE_RU_UTIME (func).tv_usec >= IDIO_VM_US) {
		IDIO_PRIMITIVE_RU_UTIME (func).tv_usec -= IDIO_VM_US;
		IDIO_PRIMITIVE_RU_UTIME (func).tv_sec += 1;
	    }

	    /* Sys */
	    tv_d.tv_sec = ruep->ru_stime.tv_sec - ru0p->ru_stime.tv_sec;
	    tv_d.tv_usec = ruep->ru_stime.tv_usec - ru0p->ru_stime.tv_usec;
	    if (tv_d.tv_usec < 0) {
		tv_d.tv_usec += IDIO_VM_US;
		tv_d.tv_sec -= 1;
	    }

	    IDIO_PRIMITIVE_RU_STIME (func).tv_sec += tv_d.tv_sec;
	    IDIO_PRIMITIVE_RU_STIME (func).tv_usec += tv_d.tv_usec;
	    if (IDIO_PRIMITIVE_RU_STIME (func).tv_usec >= IDIO_VM_US) {
		IDIO_PRIMITIVE_RU_STIME (func).tv_usec -= IDIO_VM_US;
		IDIO_PRIMITIVE_RU_STIME (func).tv_sec += 1;
	    }
	}
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * idio_vm_invoke() should have beaten us to this.
	     */
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST1 (func),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

#endif

static void idio_vm_primitive_call_trace (IDIO primdata, IDIO thr, int nargs);
static void idio_vm_primitive_result_trace (IDIO thr);

static void idio_vm_invoke (IDIO thr, IDIO func, idio_vm_invoke_enum tailp)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (func);
    IDIO_TYPE_ASSERT (thread, thr);

    switch ((intptr_t) func & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    /*
	     * Test Case: vm-errors/idio_vm_invoke-constant.idio
	     *
	     * 1 2 3
	     */
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	    if (0 == tailp) {
		IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
		IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_XI (thr)));
		IDIO_THREAD_STACK_PUSH (idio_SM_return);
	    }

	    IDIO_THREAD_FRAME (thr) = IDIO_CLOSURE_FRAME (func);
	    IDIO_THREAD_ENV (thr)   = IDIO_CLOSURE_ENV (func);
	    IDIO_THREAD_XI (thr)    = IDIO_CLOSURE_XI (func);
	    IDIO_THREAD_PC (thr)    = IDIO_CLOSURE_CODE_PC (func);

	    if (idio_vm_tracing &&
		0 == tailp) {
		idio_vm_tracing++;
	    }
#ifdef IDIO_VM_PROF
	    idio_vm_func_start (func, NULL, NULL);
#endif
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    /*
	     * XI/PC shenanigans for primitives.
	     *
	     * If we are not in tail position then we should push the
	     * current XI/PC onto the stack so that when the invoked
	     * code calls RETURN it will return to whomever called us
	     * -- as the CLOSURE code does above.
	     *
	     * By and large, though, primitives do not change the
	     * XI/PC as they are entirely within the realm of C.  So
	     * we don't really care if they were called in tail
	     * position or not they just do whatever and set VAL.
	     *
	     * However, (apply proc & args) will prepare some function
	     * which may well be a closure which *will* alter the
	     * XI/PC.  (As an aside, apply always invokes proc in tail
	     * position -- as proc *is* in tail position from apply's
	     * point of view).  The closure will, of course, RETURN to
	     * whatever is on top of the stack.
	     *
	     * But we haven't put anything there because this is a
	     * primitive and primitives don't change the XI/PC...
	     *
	     * So, if, after invoking the primitive, the XI/PC has
	     * changed (ie. apply prepared a closure which has set the
	     * XI/PC ready to run the closure when we return from
	     * here) *and* we are not in tail position then we push
	     * the saved xi0/pc0 onto the stack.
	     *
	     * NB. If you push XI/PC before calling the primitive
	     * (with a view to popping it off if the XI/PC didn't
	     * change) and the primitive calls idio_raise_condition
	     * then there is an extraneous XI/PC/marker on the stack.
	     */
	    idio_xi_t xi0 = IDIO_THREAD_XI (thr);
	    idio_pc_t pc0 = IDIO_THREAD_PC (thr);
	    IDIO val = IDIO_THREAD_VAL (thr);
	    assert (idio_isa_frame (val));
	    IDIO_TYPE_ASSERT (frame, val);

	    IDIO last = IDIO_FRAME_ARGS (val, IDIO_FRAME_NPARAMS (val));
	    /* IDIO_FRAME_NPARAMS (val) -= 1; */

	    if (idio_S_nil != last) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		fprintf (stderr, "func args (%d): %s ", IDIO_FRAME_NPARAMS (val) + 1, IDIO_PRIMITIVE_NAME (func));
		idio_debug ("*val* %s; ", val);
		idio_debug ("last %s\n", last);
		idio_vm_thread_state (thr);
		idio_coding_error_C ("primitive: using varargs?", last, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }

	    /*
	     * Unlike the other invocations of a primitive (see
	     * PRIMCALL*, below) we haven't preset _VAL, _REG1 with
	     * our arguments so idio_vm_primitive_call_trace() can't
	     * do the right thing.
	     *
	     * idio_vm_start_func() bumbles through well enough.
	     */
#ifdef IDIO_VM_PROF
	    struct timespec prim_t0;
	    struct rusage prim_ru0;
	    idio_vm_func_start (func, &prim_t0, &prim_ru0);
#endif

	    switch (IDIO_PRIMITIVE_ARITY (func)) {
	    case 0:
		{
		    IDIO args = idio_frame_args_as_list_from (val, 0);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (args);
		}
		break;
	    case 1:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO args = idio_frame_args_as_list_from (val, 1);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, args);
		}
		break;
	    case 2:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO args = idio_frame_args_as_list_from (val, 2);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, args);
		}
		break;
	    case 3:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO arg3 = IDIO_FRAME_ARGS (val, 2);
		    IDIO args = idio_frame_args_as_list_from (val, 3);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, args);
		}
		break;
	    case 4:
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO arg3 = IDIO_FRAME_ARGS (val, 2);
		    IDIO arg4 = IDIO_FRAME_ARGS (val, 3);
		    IDIO args = idio_frame_args_as_list_from (val, 4);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, arg4, args);
		}
		break;
	    case 5:
		/*
		 * Code coverage:
		 *
		 * No 5-argument primitives.
		 */
		{
		    IDIO arg1 = IDIO_FRAME_ARGS (val, 0);
		    IDIO arg2 = IDIO_FRAME_ARGS (val, 1);
		    IDIO arg3 = IDIO_FRAME_ARGS (val, 2);
		    IDIO arg4 = IDIO_FRAME_ARGS (val, 3);
		    IDIO arg5 = IDIO_FRAME_ARGS (val, 4);
		    IDIO args = idio_frame_args_as_list_from (val, 5);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, arg4, arg5, args);
		}
		break;
	    default:
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_vm_error_function_invoke ("arity unexpected", IDIO_LIST2 (func, val), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
		break;
	    }

#ifdef IDIO_VM_PROF
	    struct timespec prim_te;
	    struct rusage prim_rue;
	    idio_vm_func_stop (func, &prim_te, &prim_rue);
	    idio_vm_prim_time (func, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif
	    idio_xi_t xi = IDIO_THREAD_XI (thr);
	    idio_pc_t pc = IDIO_THREAD_PC (thr);

	    if (0 == tailp &&
		(xi != xi0 ||
		 pc != pc0)) {
		IDIO_THREAD_STACK_PUSH (idio_fixnum (pc0));
		IDIO_THREAD_STACK_PUSH (idio_fixnum (xi0));
		IDIO_THREAD_STACK_PUSH (idio_SM_return);
	    }

	    idio_vm_primitive_result_trace (thr);

	    return;
	}
	break;
    case IDIO_TYPE_CONTINUATION:
	{
	    IDIO val = IDIO_THREAD_VAL (thr);

	    IDIO last = IDIO_FRAME_ARGS (val, IDIO_FRAME_NPARAMS (val));
	    /* IDIO_FRAME_NPARAMS (val) -= 1; */

	    if (idio_S_nil != last) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_coding_error_C ("continuation: varargs?", last, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }

	    /*
	     * I mis-read/didn't read
	     * https://www.scheme.com/tspl3/control.html#./control:s53
	     * carefully enough:
	     *
	     *   In the context of multiple values, a continuation may
	     *   actually accept zero or more than one argument
	     *
	     * That *should* only affect where a closure is being used
	     * as a continuation such that this unary test is still
	     * valid for a continuation object.
	     */
	    if (IDIO_FRAME_NPARAMS (val) != 1) {
		/*
		 * Test Case: vm-errors/idio_vm_invoke-continuation-num-args.idio
		 *
		 * %%call/uc (function (k) {
		 *              k 1 2
		 * })
		 *
		 * NB We need to call the %%call/uc primitive as
		 * call/cc is wrappered and handles multiple
		 * arguments!  Oh, the irony!
		 */
		idio_vm_error_function_invoke ("unary continuation", IDIO_LIST2 (func, val), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }

	    idio_vm_restore_continuation (func, IDIO_FRAME_ARGS (val, 0));
	}
	break;
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SYMBOL:
	{
	    size_t pathname_len = 0;
	    char *pathname = idio_command_find_exe (func, &pathname_len);
	    if (NULL != pathname) {
		IDIO_THREAD_VAL (thr) = idio_command_invoke (func, thr, pathname);
		IDIO_GC_FREE (pathname, pathname_len);
	    } else {
		/*
		 * Test Case: vm-errors/idio_vm_invoke-command-not-found.idio
		 *
		 * tmpdir := (make-tmp-dir)
		 * rmdir tmpdir
		 * PATH = tmpdir
		 * 'foo 1 2
		 */
		IDIO val = IDIO_THREAD_VAL (thr);
		/*
		 * IDIO_FRAME_FA() includes a varargs element so
		 * should always be one or more
		 */
		IDIO args = idio_S_nil;
		if (IDIO_FRAME_NPARAMS (val) > 0) {
		    args = idio_frame_params_as_list (val);
		} else {
		    /*
		     * A single varargs element but if it is #n then
		     * nothing
		     */
		    if (idio_S_nil != IDIO_FRAME_ARGS (val, 0)) {
			args = IDIO_FRAME_ARGS (val, 0);
		    }
		}

		IDIO invocation = IDIO_LIST1 (func);
		if (idio_S_nil != args) {
		    invocation = idio_list_append2 (invocation, args);
		}

		idio_command_not_found_error ("external command not found",
					      invocation,
					      IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	if (idio_isa_generic (func)) {
	    /*
	     * Here, we've already been primed with our args in *VAL*.
	     * All we need to do is re-run this function with the
	     * correct func which, in the case of a generic function,
	     * is its instance-proc.
	     */
	    IDIO proc = idio_struct_instance_ref_direct (func, IDIO_CLASS_ST_PROC);

	    idio_vm_invoke (thr, proc, tailp);
	} else {
	    /*
	     * Test Case: vm-errors/idio_vm_invoke-bad-type-2.idio
	     *
	     * <class> 1 2
	     */
	    idio_vm_error_function_invoke ("cannot invoke struct-instance",
					   idio_S_nil,
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    default:
	{
	    /*
	     * Test Case: vm-errors/idio_vm_invoke-bad-type-1.idio
	     *
	     * ^error 1 2
	     */
	    idio_vm_error_function_invoke ("cannot invoke",
					   idio_list_append2 (IDIO_LIST1 (func),
							      idio_frame_params_as_list (IDIO_THREAD_VAL (thr))),
					   IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
	break;
    }
}

/*
 * Given a command as a list, (foo bar baz), run the code
 *
 * WARNING: in the calling environment idio_gc_protect() any IDIO
 * objects you want to use after calling this function (as it may call
 * idio_gc_collect())
 *
 * This is a troublesome function.  We are trying to make C call an
 * Idio function which, straightforward enough, does require we follow
 * the full Idio calling conventions.  Notably, stack preparation,
 * frame handling, state preservation etc..
 *
 * Or we can stash the current XI/PC on the stack and preserve
 * *everything*.
 *
 * The only problem here is inconvenient conditions.
 */
IDIO idio_vm_invoke_C_thread (IDIO thr, IDIO command)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (command);

    IDIO_TYPE_ASSERT (thread, thr);

    switch ((intptr_t) command & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	{
	    /*
	     * Test Case: ??
	     *
	     * coding error
	     */
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (command), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    default:
	break;
    }

    idio_xi_t xi0 = IDIO_THREAD_XI (thr);
    idio_pc_t pc0 = IDIO_THREAD_PC (thr);
    IDIO_THREAD_STACK_PUSH (idio_fixnum (pc0));
    IDIO_THREAD_STACK_PUSH (idio_fixnum (xi0));
    IDIO_THREAD_STACK_PUSH (idio_SM_return);
    idio_vm_preserve_all_state (thr);

    switch (command->type) {
    case IDIO_TYPE_PAIR:
	{
	    /*
	     * (length command) will give us the +1 frame allocation we need
	     * because it will allocate a slot for the command name even
	     * though it won't go there.
	     */
	    IDIO vs = idio_frame_allocate (idio_list_length (command));
	    idio_fi_t fai;
	    IDIO args = IDIO_PAIR_T (command);
	    for (fai = 0; idio_S_nil != args; fai++) {
		idio_frame_update (vs, 0, fai, IDIO_PAIR_H (args));
		args = IDIO_PAIR_T (args);
	    }
	    IDIO_THREAD_VAL (thr) = vs;

	    idio_vm_invoke (thr, IDIO_PAIR_H (command), IDIO_VM_INVOKE_TAIL_CALL);

	    /*
	     * XXX
	     *
	     * If the command was a primitive then we called
	     * idio_vm_run() we'd be continuing our parent's loop.
	     *
	     * Need to figure out the whole invoke-from-C thing
	     * properly (or at least consistently).
	     */
	    if (! idio_isa_primitive (IDIO_PAIR_H (command))) {
		idio_vm_run_C (thr, IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr));
	    }
	}
	break;
    case IDIO_TYPE_CLOSURE:
	{
	    /*
	     * Must be a thunk
	     */
	    IDIO vs = idio_frame_allocate (1);
	    IDIO_THREAD_VAL (thr) = vs;

	    idio_vm_invoke (thr, command, IDIO_VM_INVOKE_TAIL_CALL);
	    idio_vm_run_C (thr, IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr));
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    IDIO vs = idio_frame_allocate (1);
	    IDIO_THREAD_VAL (thr) = vs;
	    idio_vm_invoke (thr, command, IDIO_VM_INVOKE_TAIL_CALL);
	}
	break;
    default:
	{
	    fprintf (stderr, "iv-invoke-C: I can't do that, Dave!\n");
	    idio_debug ("command %s\n", command);
	}
	break;
    }

    IDIO r = IDIO_THREAD_VAL (thr);

    idio_vm_restore_all_state (thr);
    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_return != marker) {
	idio_debug ("iviCt: marker: expected idio_SM_return not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iviCt: unexpected stack marker");
    }
    IDIO_THREAD_XI (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());

    return r;
}

IDIO idio_vm_invoke_C (IDIO command)
{
    IDIO_ASSERT (command);

    return idio_vm_invoke_C_thread (idio_thread_current_thread (), command);
}

static idio_sp_t idio_vm_find_stack_marker (IDIO stack, IDIO mark, idio_sp_t from, idio_sp_t max)
{
    IDIO_ASSERT (stack);
    IDIO_ASSERT (mark);

    IDIO_TYPE_ASSERT (array, stack);

    idio_sp_t sp = idio_array_size (stack) - 1;
    if (sp < 0) {
	return sp;
    }

    if (from) {
	if (from < 0 ||
	    from > sp) {
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "find-stack-marker: from %zd out of range: 0 - %zd", from, sp);

	    idio_coding_error_C (em, mark, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	}
	sp = from;
    }

    if (max) {
	max = 0;
	idio_sp_t max_next = 0;
	for (; sp > 0; sp--) {
	    IDIO se = idio_array_ref_index (stack, sp);
	    if (mark == se) {
		IDIO val;
		if (idio_SM_trap == mark) {
		    val = idio_array_ref_index (stack, sp - 3);
		    if (IDIO_FIXNUM_VAL (val) > max_next) {
			max = sp;
			max_next = IDIO_FIXNUM_VAL (val);
		    }
		} else {
		    /*
		     * Test Case: ??
		     *
		     * Coding error.
		     */
		    idio_debug ("iv-find-stack-marker: max %s unexpected\n", mark);
		    idio_coding_error_C ("unexpected max mark", mark, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return -1;
		}
	    }
	}

	return max;
    } else {
	for (; sp >= 0; sp--) {
	    IDIO se = idio_array_ref_index (stack, sp);
	    if (mark == se) {
		return sp;
	    }
	}

	/* should be -1 */
	return sp;
    }
}

IDIO idio_vm_add_dynamic (IDIO si, IDIO ci, IDIO vi, IDIO m, IDIO note)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (ci);
    IDIO_ASSERT (vi);
    IDIO_ASSERT (m);
    IDIO_ASSERT (note);

    IDIO_TYPE_ASSERT (fixnum, si);
    IDIO_TYPE_ASSERT (fixnum, ci);
    IDIO_TYPE_ASSERT (fixnum, vi);
    IDIO_TYPE_ASSERT (module, m);
    IDIO_TYPE_ASSERT (string, note);

    return IDIO_LIST6 (idio_S_dynamic, si, ci, vi, m, note);
}

static void idio_vm_push_dynamic (IDIO thr, idio_as_t gvi, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * stack order:
     *
     * n   idio_SM_dynamic
     * n-1 vi
     * n-2 val
     * n-3 sp of next idio_SM_dynamic
     */

    idio_sp_t dsp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);
    if (dsp >= 3) {
	idio_array_push (stack, idio_fixnum (dsp));
    } else {
	idio_array_push (stack, idio_fixnum (-1));
    }

    idio_array_push (stack, val);
    idio_array_push (stack, idio_fixnum (gvi));
    idio_array_push (stack, idio_SM_dynamic);
}

static void idio_vm_pop_dynamic (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_dynamic != marker) {
	idio_debug ("iv-pop-dynamic: marker: expected idio_SM_dynamic not %s\n", marker);
	idio_vm_panic (thr, "iv-pop-dynamic: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();	/* vi */
    IDIO_THREAD_STACK_POP ();	/* val */
    IDIO_THREAD_STACK_POP ();	/* sp */
}

IDIO idio_vm_dynamic_ref (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO args)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (list, args);

    IDIO stack = IDIO_THREAD_STACK (thr);

#ifdef IDIO_VM_DYNAMIC_REF
    idio_sp_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
#else
    idio_sp_t sp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);
#endif

    IDIO val = idio_S_undef;

    for (;;) {
	if (sp >= 3) {
	    IDIO dvi = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, dvi);

	    if (IDIO_FIXNUM_VAL (dvi) == (idio_ai_t) gvi) {
		val = idio_array_ref_index (stack, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    /*
	     * dynamic values, as they appear on the stack, can only
	     * be in xi==0
	     */
	    val = idio_vm_values_ref (0, gvi);
	    break;
	}
    }

    if (idio_S_undef == val) {
	if (idio_S_nil == args) {
	    /*
	     * Test Case: vm-errors/idio_vm_dynamic_ref-unbound.idio
	     *
	     * dynamic-let (dyn-var 5) (dyn-var + 5)
	     * define (use-dyn-var v) {
	     *   (dynamic dyn-var) + v
	     * }
	     * use-dyn-var 10
	     */
	    idio_error_dynamic_unbound (si, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return IDIO_PAIR_H (args);
	}
    }

    return val;
}

void idio_vm_dynamic_set (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO v)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_sp_t sp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);

    for (;;) {
	if (sp >= 3) {
	    IDIO sv = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == (idio_ai_t) gvi) {
		idio_array_insert_index (stack, v, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    /*
	     * dynamic values, as they appear on the stack, can only
	     * be in xi==0
	     */
	    IDIO vs0 = IDIO_XENV_VT (idio_xenvs[0]);
	    idio_array_insert_index (vs0, v, gvi);
	    break;
	}
    }
}

IDIO idio_vm_add_environ (IDIO si, IDIO ci, IDIO vi, IDIO m, IDIO note)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (ci);
    IDIO_ASSERT (vi);
    IDIO_ASSERT (m);
    IDIO_ASSERT (note);

    IDIO_TYPE_ASSERT (fixnum, si);
    IDIO_TYPE_ASSERT (fixnum, ci);
    IDIO_TYPE_ASSERT (fixnum, vi);
    IDIO_TYPE_ASSERT (module, m);
    IDIO_TYPE_ASSERT (string, note);

    return IDIO_LIST6 (idio_S_environ, si, ci, vi, m, note);
}

static void idio_vm_push_environ (IDIO thr, idio_as_t gvi, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);

    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * stack order:
     *
     * n   idio_SM_environ
     * n-1 vi
     * n-2 val
     * n-3 sp of next idio_SM_environ
     */

    idio_sp_t esp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);
    if (esp >= 3) {
	idio_array_push (stack, idio_fixnum (esp));
    } else {
	idio_array_push (stack, idio_fixnum (-1));
    }

    idio_array_push (stack, val);
    idio_array_push (stack, idio_fixnum (gvi));
    idio_array_push (stack, idio_SM_environ);
}

static void idio_vm_pop_environ (IDIO thr)
{
    IDIO_ASSERT (thr);

    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_environ != marker) {
	idio_debug ("iv-pop-environ: marker: expected idio_SM_environ not %s\n", marker);
	idio_vm_panic (thr, "iv-pop-environ: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_STACK_POP ();
}

IDIO idio_vm_environ_ref (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO args)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (list, args);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_sp_t sp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);

    IDIO val = idio_S_undef;

    for (;;) {
	if (sp >= 3) {
	    IDIO evi = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, evi);

	    if (IDIO_FIXNUM_VAL (evi) == (idio_ai_t) gvi) {
		val = idio_array_ref_index (stack, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    /*
	     * environ values, as they appear on the stack, can only
	     * be in xi==0
	     */
	    val = idio_vm_values_ref (0, gvi);
	    break;
	}
    }

    if (idio_S_undef == val) {
	if (idio_S_nil == args) {
	    /*
	     * Test Case: vm-errors/idio_vm_environ_ref-unbound.idio
	     *
	     * {
	     *   env-var :* "foo"
	     *   string-length env-var		; 3
	     * }
	     * string-length env-var
	     */
	    idio_error_environ_unbound (si, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return IDIO_PAIR_H (args);
	}
    }

    return val;
}

void idio_vm_environ_set (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO v)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_sp_t sp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);

    for (;;) {
	if (sp >= 3) {
	    IDIO sv = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == (idio_ai_t) gvi) {
		idio_array_insert_index (stack, v, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    /*
	     * environ values, as they appear on the stack, can only
	     * be in xi==0
	     */
	    IDIO vs0 = IDIO_XENV_VT (idio_xenvs[0]);
	    idio_array_insert_index (vs0, v, gvi);
	    break;
	}
    }
}

IDIO idio_vm_computed_ref (idio_xi_t xi, idio_as_t si, idio_as_t vi)
{
    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
    IDIO gns = idio_array_ref_index (vs, vi);

    if (idio_isa_pair (gns)) {
	IDIO get = IDIO_PAIR_H (gns);
	if (idio_isa_primitive (get) ||
	    idio_isa_closure (get)) {
	    return idio_vm_invoke_C (IDIO_LIST1 (get));
	} else {
	    /*
	     * Test Case: XXX computed-errors/idio_vm_computed_ref-no-get-accessor.idio
	     *
	     * cvar-so :$ #n (function (v) v)
	     * cvar-so
	     */
	    idio_vm_error_computed_no_accessor ("get", si, vi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * We shouldn't have been able to create a computed variable without
	 * accessors.
	 *
	 * Coding error.
	 */
	idio_vm_error_computed ("no get/set accessors", si, vi, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

IDIO idio_vm_computed_iref (IDIO gns, idio_as_t si)
{
    IDIO_ASSERT (gns);

    if (idio_isa_pair (gns)) {
	IDIO get = IDIO_PAIR_H (gns);
	if (idio_isa_primitive (get) ||
	    idio_isa_closure (get)) {
	    return idio_vm_invoke_C (IDIO_LIST1 (get));
	} else {
	    /*
	     * Test Case: XXX computed-errors/idio_vm_computed_ref-no-get-accessor.idio
	     *
	     * cvar-so :$ #n (function (v) v)
	     * cvar-so
	     */
	    idio_vm_error_computed_no_accessor ("get", si, 0, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * We shouldn't have been able to create a computed variable without
	 * accessors.
	 *
	 * Coding error.
	 */
	idio_vm_error_computed ("no get/set accessors", si, 0, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

IDIO idio_vm_computed_set (idio_xi_t xi, idio_as_t si, idio_as_t vi, IDIO v)
{
    IDIO_ASSERT (v);

    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
    IDIO gns = idio_array_ref_index (vs, vi);

    if (idio_isa_pair (gns)) {
	IDIO set = IDIO_PAIR_T (gns);
	if (idio_isa_primitive (set) ||
	    idio_isa_closure (set)) {
	    return idio_vm_invoke_C (IDIO_LIST2 (set, v));
	} else {
	    /*
	     * Test Case: XXX computed-errors/idio_vm_computed_set-no-set-accessor.idio
	     *
	     * cvar-go :$ (function #n "got me") #n
	     * cvar-go = 3
	     */
	    idio_vm_error_computed_no_accessor ("set", si, vi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * We shouldn't have been able to create a computed variable without
	 * accessors.
	 *
	 * Coding error.
	 */
	idio_vm_error_computed ("no get/set accessors", si, vi, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

IDIO idio_vm_computed_iset (IDIO gns, idio_as_t si, IDIO v)
{
    IDIO_ASSERT (gns);
    IDIO_ASSERT (v);

    if (idio_isa_pair (gns)) {
	IDIO set = IDIO_PAIR_T (gns);
	if (idio_isa_primitive (set) ||
	    idio_isa_closure (set)) {
	    return idio_vm_invoke_C (IDIO_LIST2 (set, v));
	} else {
	    /*
	     * Test Case: XXX computed-errors/idio_vm_computed_set-no-set-accessor.idio
	     *
	     * cvar-go :$ (function #n "got me") #n
	     * cvar-go = 3
	     */
	    idio_vm_error_computed_no_accessor ("set", si, 0, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * We shouldn't have been able to create a computed variable without
	 * accessors.
	 *
	 * Coding error.
	 */
	idio_vm_error_computed ("no get/set accessors", si, 0, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

void idio_vm_computed_define (idio_xi_t xi, idio_as_t si, idio_as_t gvi, IDIO v)
{
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (pair, v);

    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);

    idio_array_insert_index (vs, v, gvi);
}

void idio_vm_push_trap (IDIO thr, IDIO handler, IDIO fgci, idio_sp_t next)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (handler);
    IDIO_ASSERT (fgci);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (fixnum, fgci);

    if (! idio_isa_function (handler)) {
	/*
	 * Test Case: vm-errors/idio_vm_push_trap-bad-handler-type.idio
	 *
	 * trap ^error #t #t
	 */
	idio_error_param_type ("function", handler, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * stack order:
     *
     * n   idio_SM_trap
     * n-1 handler
     * n-2 condition-type
     * n-3 sp of next idio_SM_trap
     */

    idio_sp_t tsp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0);
    if (next) {
	tsp = next;
    }
    if (tsp >= 1) {
	idio_array_push (stack, idio_fixnum (tsp));
    } else {
	/*
	 * We shouldn't get here because we forced several handlers on
	 * at the bottom of each stack.
	 */
	idio_array_push (stack, idio_fixnum (-1));
    }

    idio_array_push (stack, fgci);
    idio_array_push (stack, handler);
    idio_array_push (stack, idio_SM_trap);
}

static void idio_vm_pop_trap (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_trap != marker) {
	idio_debug ("iv-pop-trap: marker: expected idio_SM_trap not %s\n", marker);
	idio_vm_panic (thr, "iv-pop-trap: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();	/* handler */
    IDIO_THREAD_STACK_POP ();	/* fgci */
    IDIO_THREAD_STACK_POP ();	/* sp */
}

static void idio_vm_restore_trap (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO trap_sp = IDIO_THREAD_STACK_POP ();
    if (idio_isa_fixnum (trap_sp) == 0) {
	IDIO_THREAD_STACK_PUSH (trap_sp);
	idio_vm_panic (thr, "restore-trap: not a fixnum");
    }
    IDIO_TYPE_ASSERT (fixnum, trap_sp);
}

void idio_vm_push_escaper (IDIO thr, IDIO fgci, idio_sp_t offset)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (fgci);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (fixnum, fgci);

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * stack order:
     *
     * n   idio_SM_escaper
     * n-1 label
     * n-2 frame
     * n-3 (absolute) PC to resume(*)
     *
     * (*) PC after the POP-ESCAPER
     */

    idio_array_push (stack, idio_fixnum (IDIO_THREAD_PC (thr) + offset + 1));
    idio_array_push (stack, IDIO_THREAD_FRAME (thr));
    idio_array_push (stack, fgci);
    idio_array_push (stack, idio_SM_escaper);
}

static void idio_vm_pop_escaper (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_escaper != marker) {
	idio_debug ("iv-pop-escaper: marker: expected idio_SM_escaper not %s\n", marker);
	idio_vm_panic (thr, "iv-pop-escaper: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();	/* fgci */
    IDIO_THREAD_STACK_POP ();	/* frame */
    IDIO_THREAD_STACK_POP ();	/* offset */
}

void idio_vm_escaper_label_ref (IDIO thr, IDIO fci)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (fci);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (fixnum, fci);

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * stack order:
     *
     * n   idio_SM_escaper
     * n-1 label
     * n-2 frame to resume
     * n-3 (absolute) PC to resume(*)
     *
     * (*) PC after the POP-ESCAPER
     */

    int done = 0;
    idio_sp_t escaper_sp = idio_array_size (stack);
    while (!done &&
	   escaper_sp >= 0) {
	escaper_sp--;
	escaper_sp = idio_vm_find_stack_marker (stack, idio_SM_escaper, escaper_sp, 0);
	if (escaper_sp >= 0) {
	    if (idio_array_ref_index (stack, escaper_sp - 1) == fci) {
		done = 1;
	    }
	}
    }

    if (! done) {
	idio_error_runtime_unbound (fci, idio_S_nil, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO_THREAD_FRAME (thr) = idio_array_ref_index (stack, escaper_sp - 2);
    IDIO offset = idio_array_ref_index (stack, escaper_sp - 3);
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (offset);

    /*
     * Remove references above us for good house-keeping
     */
    /*
    idio_sp_t ai = idio_array_size (stack);
    for (ai--; ai >= escaper_sp - 3; ai--) {
	idio_array_insert_index (stack, idio_S_nil, ai);
    }
    */
    IDIO_ARRAY_USIZE (stack) = escaper_sp - 3;
}

void idio_vm_push_abort (IDIO thr, IDIO krun)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (krun);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (pair, krun);
    IDIO_TYPE_ASSERT (continuation, IDIO_PAIR_H (krun));

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * stack order:
     *
     * n   idio_SM_abort
     * n-1 (k, desc)
     * n-2 sp of next idio_SM_abort
     */

    /* push n-2 */
    idio_sp_t asp = idio_vm_find_stack_marker (stack, idio_SM_abort, 0, 0);
    if (asp >= 2) {
	idio_array_push (stack, idio_fixnum (asp));
    } else {
	idio_array_push (stack, idio_fixnum (-1));
    }

    idio_array_push (stack, krun);
    idio_array_push (stack, idio_SM_abort);
}

static void idio_vm_push_offset_abort (IDIO thr, uint64_t o)
{
    IDIO_ASSERT (thr);

    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * stack order:
     *
     * n   idio_SM_abort
     * n-1 (k, desc)
     * n-2 sp of next idio_SM_abort
     */

    /*
     * A vanilla continuation right now would just lead us back into
     * the errant code that we are putting this ABORT wrapper around.
     * Therefore we want to massage the continuation's PC to be:
     *
     * * offset by {o} which would take us to the POP-ABORT which
     *   would be expecting the restored stack to have the
     *   idio_SM_abort marker etc.
     *
     * * offset by {o}+1 which would take us to the instruction after
     *   POP-ABORT which would not be expecting the restored stack to
     *   have the idio_SM_abort marker etc.
     *
     * The latter seems more convenient (and see comments on
     * implementation below).
     *
     * For the construction of a continuation, the XI shouldn't change
     * -- we can't construct a call/cc across an execution environment
     * boundary.  However, continuations can be called from another
     * execution environment so we need to remember the current XI in
     * the continuation to be able to resume execution sensibly.
     */

    IDIO k = idio_continuation (thr, IDIO_CONTINUATION_CALL_CC);

    IDIO_CONTINUATION_PC (k) += o + 1;

    IDIO kosh = idio_open_output_string_handle_C ();
    idio_display_C ("ABORT to toplevel (PC [", kosh);
    idio_display (idio_fixnum (IDIO_CONTINUATION_XI (k)), kosh);
    idio_display_C ("]@", kosh);
    idio_display (idio_fixnum (IDIO_CONTINUATION_PC (k)), kosh);
    idio_display_C (")", kosh);

    /* push n-2 */
    idio_sp_t asp = idio_vm_find_stack_marker (stack, idio_SM_abort, 0, 0);
    if (asp >= 2) {
	idio_array_push (stack, idio_fixnum (asp));
    } else {
	idio_array_push (stack, idio_fixnum (-1));
    }

    /*
     * If we took the former approach, that the continuation contains
     * the idio_SM_abort marker, etc., then we need to push a krun
     * value on the stack (before creating the continuation) which
     * should contain the continuation...
     *
     * So we would need to push a dummy krun, (#n #n), and set its ph
     * and pht after we've created the continuation stack.
     */
    IDIO krun = IDIO_LIST2 (k, idio_get_output_string (kosh));

    /* push n-1 */
    idio_array_push (stack, krun);

    /* push n */
    idio_array_push (stack, idio_SM_abort);

    /*
     * We would create the continuation, k, now, so as to include the
     * idio_SM_abort etc. and change k's PC to +{o} and create kosh.
     *
     * We can now override krun's values:
     *
     * IDIO_PAIR_H (krun) = k;
     * IDIO_PAIR_HT (krun) = idio_get_output_string (kosh);
     */
}

void idio_vm_pop_abort (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO marker = IDIO_THREAD_STACK_POP ();
    if (idio_SM_abort != marker) {
	idio_debug ("iv-pop-abort: marker: expected idio_SM_abort not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv-pop-abort: unexpected stack marker");
    }
    IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_STACK_POP ();
}

idio_sp_t idio_vm_find_abort_1 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_sp_t asp = idio_vm_find_stack_marker (stack, idio_SM_abort, 0, 0);

    if (-1 == asp ||
	asp < 2) {
	fprintf (stderr, "find-abort-1: no ABORTs? asp == %" PRIdPTR "\n", asp);
#ifdef IDIO_DEBUG
	idio_vm_thread_state (thr);
#endif
	return 0;
	assert (0);
    }

    IDIO I_next = idio_array_ref_index (stack, asp - 2);
    idio_sp_t next = IDIO_FIXNUM_VAL (I_next);

    int done = 0;
    while (! done) {
	if (-1 == next) {
	    return asp;
	}

	asp = next;

	I_next = idio_array_ref_index (stack, asp - 2);
	next = IDIO_FIXNUM_VAL (I_next);
    }

    return 0;
}

idio_sp_t idio_vm_find_abort_2 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_sp_t asp = idio_vm_find_stack_marker (stack, idio_SM_abort, 0, 0);

    if (-1 == asp ||
	asp < 2) {
	fprintf (stderr, "find-abort-2: no ABORTs? asp == %" PRIdPTR "\n", asp);
#ifdef IDIO_DEBUG
	idio_vm_thread_state (thr);
#endif
	assert (0);
    }

    IDIO I_next = idio_array_ref_index (stack, asp - 2);
    idio_sp_t next = IDIO_FIXNUM_VAL (I_next);

    if (-1 == next) {
	fprintf (stderr, "find-abort-2: only 1 ABORT\n");
#ifdef IDIO_DEBUG
	idio_vm_thread_state (thr);
#endif
	return 0;
    }

    int done = 0;
    while (! done) {
	IDIO I_next_1 = idio_array_ref_index (stack, next - 2);
	idio_sp_t next_1 = IDIO_FIXNUM_VAL (I_next_1);

	if (-1 == next_1) {
	    return asp;
	}

	asp = next;
	next = next_1;
    }

    return 0;
}

void idio_vm_raise_condition (IDIO continuablep, IDIO condition, int IHR, int reraise)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (condition);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    /* idio_debug ("raise-condition: %s\n", condition); */

    IDIO thr = idio_thread_current_thread ();

    IDIO stack = IDIO_THREAD_STACK (thr);

    IDIO otrap_sp = idio_fixnum (idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0));
    idio_sp_t trap_sp = IDIO_FIXNUM_VAL (otrap_sp);

    if (reraise) {
	trap_sp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 1);
    }

    if (trap_sp >= (idio_sp_t) idio_array_size (stack)) {
	idio_vm_thread_state (thr);
	idio_vm_panic (thr, "trap SP >= sizeof (stack)");
    }
    if (trap_sp < 3) {
	fprintf (stderr, "trap_sp = %zd\n", trap_sp);
	idio_vm_panic (thr, "trap SP < 3");
    }

    /*
     * This feels mildy expensive: The trap call will say:
     *
     * trap COND-TYPE-NAME handler body
     *
     * So what we have in our hands is an index, gci, into the
     * constants table which we can lookup, idio_vm_constants_ref, to
     * get a symbol, COND-TYPE-NAME.  We now need to look that up,
     * idio_module_symbol_value_recurse, to get a value, trap_ct.
     *
     * We now need to determine if the actual condition isa trap_ct.
     */
    IDIO trap_ct_gci;
    IDIO handler;
    while (1) {
	handler = idio_array_ref_index (stack, trap_sp - 1);
	trap_ct_gci = idio_array_ref_index (stack, trap_sp - 2);
	IDIO ftrap_sp_next = idio_array_ref_index (stack, trap_sp - 3);

	IDIO trap_ct_sym = idio_vm_constants_ref (0, (idio_ai_t) IDIO_FIXNUM_VAL (trap_ct_gci));

	IDIO trap_ct = idio_module_symbol_value_recurse (trap_ct_sym, IDIO_THREAD_ENV (thr), idio_S_nil);

	if (! idio_isa_struct_type (trap_ct)) {
	    idio_debug ("trap_ct %s is invalid\n", trap_ct);
	    assert (0);
	}

	if (idio_S_unspec == trap_ct) {
	    idio_vm_panic (thr, "trap condition type is unspec??");
	}

	idio_sp_t trap_sp_next = IDIO_FIXNUM_VAL (ftrap_sp_next);
	IDIO_C_ASSERT ((size_t) trap_sp_next < idio_array_size (stack));

	if (idio_struct_instance_isa (condition, trap_ct)) {
	    break;
	}

	if (trap_sp == trap_sp_next) {
	    idio_debug ("ivrc: Yikes!  Failed to match TRAP on %s\n", condition);
	    idio_vm_panic (thr, "ivrc: no more TRAP handlers\n");
	}
	trap_sp = trap_sp_next;
    }

    int isa_closure = idio_isa_closure (handler);

    /*
     * Whether we are continuable or not determines where in the
     * prologue we set the XI/PC for the RETURNee.
     */
    int tailp = IDIO_VM_INVOKE_TAIL_CALL;
    if (1 || isa_closure) {
	idio_array_push (stack, idio_fixnum (IDIO_THREAD_PC (thr)));
	idio_array_push (stack, idio_fixnum (IDIO_THREAD_XI (thr)));
	idio_array_push (stack, idio_SM_return); /* for the RETURNs */
	if (IHR) {
	    /* tailp = IDIO_VM_INVOKE_REGULAR_CALL; */
	    idio_vm_preserve_all_state (thr); /* for RESTORE-ALL-STATE */

	    /*
	     * We need to run this code in the care of the next
	     * handler on the stack (not the current handler).  Unless
	     * the next handler is the base handler in which case it
	     * gets reused (ad infinitum).
	     *
	     * We can do that by pushing the next handler onto the top
	     * of the stack -- therefore it becomes the first.  We
	     * don't change the associated values so the the next
	     * handler's next will be pushed on to the stack in turn.
	     *
	     * This results in a strange chain with "older" handlers
	     * seemingly preceeding newer ones:
	     *
	     *   handler-3
	     *   handler-4
	     *   handler-5
	     *   handler-4
	     *   handler-3
	     *   handler-2
	     *   handler-1
	     *
	     * It comes out in the wash, honest!
	     */
	    idio_sp_t next_tsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, trap_sp - 3));
	    idio_vm_push_trap (thr,
			       idio_array_ref_index (stack, next_tsp - 1),
			       idio_array_ref_index (stack, next_tsp - 2),
			       IDIO_FIXNUM_VAL (idio_array_ref_index (stack, next_tsp - 3)));

	    if (isa_closure) {
		idio_array_push (stack, idio_fixnum (idio_vm_IHR_pc)); /* => (POP-TRAP) RESTORE-ALL-STATE, RETURN */
		idio_array_push (stack, idio_fixnum (IDIO_THREAD_XI (thr)));
		idio_array_push (stack, idio_SM_return);
	    } else {
		IDIO_THREAD_PC (thr) = idio_vm_IHR_pc; /* => (POP-TRAP) RESTORE-ALL-STATE, RETURN */
	    }
	} else {
	    idio_vm_preserve_state (thr);      /* for RESTORE-STATE */

	    idio_sp_t next_tsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, trap_sp - 3));
	    idio_vm_push_trap (thr,
			       idio_array_ref_index (stack, next_tsp - 1),
			       idio_array_ref_index (stack, next_tsp - 2),
			       IDIO_FIXNUM_VAL (idio_array_ref_index (stack, next_tsp - 3)));

	    if (idio_S_true == continuablep) {
		if (isa_closure) {
		    idio_array_push (stack, idio_fixnum (idio_vm_CHR_pc)); /* => POP/RESTORE-TRAP, RESTORE-STATE, RETURN */
		    idio_array_push (stack, idio_fixnum (IDIO_THREAD_XI (thr)));
		    idio_array_push (stack, idio_SM_return);
		} else {
		    IDIO_THREAD_PC (thr) = idio_vm_CHR_pc;
		}
	    } else {
		if (isa_closure) {
		    idio_array_push (stack, idio_fixnum (idio_vm_NCE_pc)); /* => NON-CONT-ERR */
		    idio_array_push (stack, idio_fixnum (IDIO_THREAD_XI (thr)));
		    idio_array_push (stack, idio_SM_return);
		} else {
		    IDIO_THREAD_PC (thr) = idio_vm_NCE_pc;
		}
	    }
	}
    }

    /*
     * We should, normally, make some distinction between a call to a
     * primitive and a call to a closure as the primitive doesn't need
     * some of the wrapping around it.  In fact a call to a primitive
     * will unhelpfully trip over the wrapping because it didn't
     * RETURN into the unwrapping code (CHR - condition handler
     * return).
     *
     * On top of which, if this was a regular condition, called in
     * tail position then it effectively replaces the extant function
     * call at the time the condition was raised (by a C function,
     * probably).
     *
     * That's not so clever for an interrupt handler where we
     * effectively want to stop the clock, run something else and then
     * puts the clock back where it was.  Here we *don't* want to
     * either run in tail position (although that might matter less)
     * or replace the extant function call.
     *
     * This is particularly obvious for SIGCHLD where we are blocked
     * in waitpid, probably, and the SIGCHLD arrives, and we replace
     * the call to waitpid with result of do-job-notification
     * (eventually, via some C functions) which returns a value that
     * no-one is expecting.
     */

    IDIO vs = idio_frame (idio_S_nil, IDIO_LIST1 (condition));
    IDIO_THREAD_VAL (thr) = vs;

    /* God speed! */
    idio_vm_invoke (thr, handler, tailp);

    /*
     * Actually, for a user-defined error handler, which will be a
     * closure, idio_vm_invoke did nothing much, the error
     * handling closure will only be run when we continue looping
     * around idio_vm_run1.
     *
     * Wait, though, consider how we've got to here.  Some
     * (user-level) code called a C primitive which stumbled over an
     * erroneous condition.  That C code called an idio_*error*
     * routine which eventually called us, idio_vm_raise_condition.
     *
     * Whilst we are ready to process the OOB exception handler in
     * Idio-land, the C language part of us still thinks we've in a
     * regular function call tree from the point of origin of the
     * error, ie. we still have a trail of C language frames back
     * through the caller (of idio_vm_raise_condition) -- and, in
     * turn, back up through to idio_vm_run1 where the C primitive was
     * first called from.  We need to make that trail disappear
     * otherwise either we'll accidentally call C's 'return' back down
     * that C stack (erroneously) or, after enough
     * idio_vm_raise_condition()s, we'll blow up the C stack.
     *
     * That means siglongjmp(3).
     *
     * XXX siglongjmp means that we won't be free()ing any memory
     * allocated during the life of that C stack.  Unless we think of
     * something clever...well?...er, still waiting...
     */

    siglongjmp (IDIO_THREAD_JMP_BUF (thr), IDIO_VM_SIGLONGJMP_CONDITION);

    /* not reached */
    IDIO_C_ASSERT (0);
}

void idio_raise_condition (IDIO continuablep, IDIO condition)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (condition);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    idio_vm_raise_condition (continuablep, condition, 0, 0);
}

void idio_reraise_condition (IDIO continuablep, IDIO condition)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (condition);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    idio_vm_raise_condition (continuablep, condition, 0, 1);
}

IDIO_DEFINE_PRIMITIVE1_DS ("raise", raise, (IDIO c), "c", "\
raise the condition `c`					\n\
							\n\
!! MAY RETURN !!					\n\
							\n\
:param c: condition to raise				\n\
:type c: condition					\n\
							\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (c);

    /*
     * Test Case: vm-errors/raise-bad-type.idio
     *
     * raise #t
     */
    IDIO_USER_TYPE_ASSERT (condition, c);

    idio_raise_condition (idio_S_true, c);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("reraise", reraise, (IDIO c), "c", "\
reraise the condition `c`				\n\
							\n\
In particular this rediscovers the top-most trap	\n\
handler.						\n\
							\n\
:param c: condition to raise				\n\
:type c: condition					\n\
")
{
    IDIO_ASSERT (c);

    /*
     * Test Case: vm-errors/reraise-bad-type.idio
     *
     * reraise #t
     */
    IDIO_USER_TYPE_ASSERT (condition, c);

    idio_reraise_condition (idio_S_true, c);

    return idio_S_notreached;
}

IDIO idio_apply (IDIO fn, IDIO args)
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (args);

    /* idio_debug ("apply: %s", fn); */
    /* idio_debug (" %s\n", args); */

    idio_fi_t nargs = idio_list_length (args);
    idio_fi_t size = nargs;

    /*
     * (apply + 1 2 '(3 4 5))
     *
     * fn == +
     * args == (1 2 (3 4 5))
     *
     * nargs == 3
     *
     * size => (nargs - 1) + len (args[nargs-1])
     */

    IDIO larg = args;
    while (idio_S_nil != larg &&
	   idio_S_nil != IDIO_PAIR_T (larg)) {
	larg = IDIO_PAIR_T (larg);
    }
    if (idio_S_nil != larg) {
	larg = IDIO_PAIR_H (larg);
	if (idio_S_nil == larg ||
	    idio_isa_pair (larg)) {
	    size = (nargs - 1) + idio_list_length (larg);
	} else {
	    nargs += 1;
	    larg = idio_S_nil;
	}
    }

    IDIO vs = idio_frame_allocate (size + 1);

    if (nargs) {
	idio_ai_t vsi;
	for (vsi = 0; vsi < nargs - 1; vsi++) {
	    IDIO_FRAME_ARGS (vs, vsi) = IDIO_PAIR_H (args);
	    args = IDIO_PAIR_T (args);
	}
	args = larg;
	for (; idio_S_nil != args; vsi++) {
	    IDIO_FRAME_ARGS (vs, vsi) = IDIO_PAIR_H (args);
	    args = IDIO_PAIR_T (args);
	}
    }

    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_VAL (thr) = vs;

    idio_vm_invoke (thr, fn, IDIO_VM_INVOKE_TAIL_CALL);

    return IDIO_THREAD_VAL (thr);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("apply", apply, (IDIO fn, IDIO args), "fn [args]", "\
call `fn` with `args`			\n\
					\n\
:param fn: function to call		\n\
:type fn: function			\n\
:param args: arguments to `fn`		\n\
:type args: parameters plus list	\n\
					\n\
The last element of `args` is special.	\n\
If it is a list then the elements of that list	\n\
are appended to the arguments to `fn`	\n\
					\n\
.. code-block:: idio			\n\
					\n\
   apply \\+ 1 2 3		; 6	\n\
   apply \\+ 1 2 3 #n		; 6	\n\
   apply \\+ 1 2 3 '(4 5)	; 15	\n\
")
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (args);

    return idio_apply (fn, args);
}

IDIO_DEFINE_PRIMITIVE1_DS ("make-prompt-tag", make_prompt_tag, (IDIO name), "name", "\
create a prompt tag from `name`		\n\
					\n\
:param name: prompt tag name		\n\
:type name: symbol			\n\
					\n\
:return: prompt tag			\n\
:rtype: struct instance			\n\
")
{
    IDIO_ASSERT (name);

    /*
     * Test Case: vm-errors/make-prompt-tag-bad-type.idio
     *
     * make-prompt-tag #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, name);

    return idio_struct_instance (idio_vm_prompt_tag_type, IDIO_LIST1 (name));
}

IDIO_DEFINE_PRIMITIVE0_DS ("holes", vm_dc_holes, (void), "", "\
return the current list of holes	\n\
					\n\
:return: list				\n\
					\n\
see make-hole				\n\
")
{
    return IDIO_THREAD_HOLES (idio_thread_current_thread ());
}

void idio_vm_dc_hole_push (IDIO hole)
{
    IDIO_ASSERT (hole);

    IDIO_TYPE_ASSERT (pair, hole);

    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_HOLES (thr) = idio_pair (hole, IDIO_THREAD_HOLES (thr));
}

IDIO_DEFINE_PRIMITIVE1_DS ("hole-push!", vm_dc_hole_push, (IDIO hole), "hole", "\
push `hole` onto the VM-wide list of holes			\n\
								\n\
:param hole:							\n\
:type hole: a hole						\n\
:return: unspec							\n\
								\n\
see make-hole							\n\
")
{
    IDIO_ASSERT (hole);

    /*
     * Test Case: vm-errors/hole-push-bad-type.idio
     *
     * hole-push! #t
     */
    IDIO_USER_TYPE_ASSERT (pair, hole);

    idio_vm_dc_hole_push (hole);

    return idio_S_unspec;
}

IDIO idio_vm_dc_hole_pop (void)
{
    IDIO thr = idio_thread_current_thread ();
    IDIO holes = IDIO_THREAD_HOLES (thr);
    IDIO r = IDIO_PAIR_H (holes);

    IDIO_THREAD_HOLES (thr) = IDIO_PAIR_T (holes);

    return r;
}

IDIO_DEFINE_PRIMITIVE0_DS ("hole-pop!", vm_dc_hole_pop, (void), "", "\
pop a `hole` from the VM-wide list of holes			\n\
								\n\
:return: a cell							\n\
")
{
    return idio_vm_dc_hole_pop ();
}

IDIO idio_vm_dc_make_hole (IDIO tag, IDIO mark, IDIO k)
{
    IDIO_ASSERT (tag);
    IDIO_ASSERT (mark);
    IDIO_ASSERT (k);

    return idio_pair (idio_pair (tag, mark), k);
}

IDIO_DEFINE_PRIMITIVE3_DS ("make-hole", vm_dc_make_hole, (IDIO tag, IDIO mark, IDIO k), "tag mark k", "\
create a hole				\n\
					\n\
:param tag: prompt-tag to unwind to	\n\
:type tag: any testable by eq?		\n\
:param mark: shift or prompt		\n\
:type mark: boolean			\n\
:param k: continuation			\n\
:type k: continuation/function		\n\
:return: hole				\n\
")
{
    IDIO_ASSERT (tag);
    IDIO_ASSERT (mark);
    IDIO_ASSERT (k);

    return idio_vm_dc_make_hole (tag, mark, k);
}

IDIO idio_vm_restore_continuation_data (IDIO k, IDIO val)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    IDIO thr = IDIO_CONTINUATION_THR (k);
    if (! idio_isa_thread (thr)) {
	idio_debug ("restore-continuation: not a thread: %s\n", thr);
	exit (1);
    }

    IDIO_THREAD_PC (thr)		= IDIO_CONTINUATION_PC (k);
    IDIO_THREAD_XI (thr)		= IDIO_CONTINUATION_XI (k);
    IDIO k_stack = IDIO_CONTINUATION_STACK (k);
    if (IDIO_CONTINUATION_FLAGS (k) & IDIO_CONTINUATION_FLAG_DELIMITED) {
	intptr_t C_k_stack = IDIO_FIXNUM_VAL (k_stack);
	fprintf (stderr, "KD ss->%" PRIdPTR "\n", C_k_stack);
	if (C_k_stack < 0) {
	    fprintf (stderr, "KD < 0\n");
	    idio_vm_thread_state (thr);
	    IDIO_C_ASSERT (0);
	} else if (IDIO_ARRAY_USIZE (IDIO_THREAD_STACK (thr)) < (uintptr_t) C_k_stack) {
	    fprintf (stderr, "KD >%zd\n", IDIO_ARRAY_USIZE (IDIO_THREAD_STACK (thr)));
	    idio_vm_thread_state (thr);
	    IDIO_C_ASSERT (0);
	}
	IDIO_ARRAY_USIZE (IDIO_THREAD_STACK (thr)) = IDIO_FIXNUM_VAL (k_stack);
    } else {
	idio_sp_t al = idio_array_size (k_stack);

	/*
	 * WARNING:
	 *
	 * Make sure you *copy* the continuation's stack -- in case this
	 * continuation is used again.
	 */

	idio_duplicate_array (IDIO_THREAD_STACK (thr), k_stack, al, IDIO_COPY_SHALLOW);
    }
    IDIO_THREAD_FRAME (thr)		= IDIO_CONTINUATION_FRAME (k);
    IDIO_THREAD_ENV (thr)		= IDIO_CONTINUATION_ENV (k);
    memcpy (IDIO_THREAD_JMP_BUF (thr), IDIO_CONTINUATION_JMP_BUF (k), sizeof (sigjmp_buf));

#ifdef IDIO_CONTINUATION_HANDLES
    /*
     * Hmm, slight complication.  Auto-restoring file descriptors
     * means that any work done in with-handle-redir in
     * job-control.idio is immediately undone and as the thing that
     * the continuation restores is something that has just been
     * closed by with-handle-redir then "hilarity" ensues.
     *
     * By hilarity I mean a core dump as the code does manage to
     * identify an error but can't write to a closed file descriptor
     * (stderr).
     *
     * TBD
     */
    IDIO_THREAD_INPUT_HANDLE (thr)	= IDIO_CONTINUATION_INPUT_HANDLE (k);
    IDIO_THREAD_OUTPUT_HANDLE (thr)	= IDIO_CONTINUATION_OUTPUT_HANDLE (k);
    IDIO_THREAD_ERROR_HANDLE (thr)	= IDIO_CONTINUATION_ERROR_HANDLE (k);
#endif

    IDIO_THREAD_MODULE (thr)		= IDIO_CONTINUATION_MODULE (k);
    IDIO_THREAD_HOLES (thr)		= idio_copy_pair (IDIO_CONTINUATION_HOLES (k), IDIO_COPY_DEEP);

    IDIO_THREAD_VAL (thr)		= val;

    idio_thread_set_current_thread (thr);

    return thr;
}

void idio_vm_restore_continuation (IDIO k, IDIO val)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    IDIO thr = idio_vm_restore_continuation_data (k, val);

    siglongjmp (IDIO_THREAD_JMP_BUF (thr), IDIO_VM_SIGLONGJMP_CONTINUATION);

    /* notreached */
    IDIO_C_ASSERT (0);
}

void idio_vm_restore_exit (IDIO k, IDIO val)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    idio_vm_restore_continuation_data (k, val);

    IDIO thr = idio_thread_current_thread ();

    siglongjmp (IDIO_THREAD_JMP_BUF (thr), IDIO_VM_SIGLONGJMP_EXIT);

    /* notreached */
    IDIO_C_ASSERT (0);
}

IDIO idio_vm_call_cc (IDIO proc, int kind)
{
    IDIO_ASSERT (proc);

    IDIO_TYPE_ASSERT (closure, proc);

    IDIO thr = idio_thread_current_thread ();

    IDIO k = idio_continuation (thr, kind);

    IDIO_THREAD_VAL (thr) = idio_frame (IDIO_THREAD_FRAME (thr), IDIO_LIST1 (k));

    idio_vm_invoke (thr, proc, IDIO_VM_INVOKE_REGULAR_CALL);

    siglongjmp (IDIO_THREAD_JMP_BUF (thr), IDIO_VM_SIGLONGJMP_CALLCC);

    /* not reached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%%call/uc", call_uc, (IDIO proc), "proc", "\
call `proc` with the current (undelimited) continuation		\n\
								\n\
:param proc:							\n\
:type proc: a closure of 1 argument				\n\
								\n\
This is the ``%%call/uc`` primitive.				\n\
")
{
    IDIO_ASSERT (proc);

    /*
     * Test Case: vm-errors/call-uc-bad-type.idio
     *
     * %%call/uc #t
     */
    IDIO_USER_TYPE_ASSERT (closure, proc);

    return idio_vm_call_cc (proc, IDIO_CONTINUATION_CALL_CC);
}

IDIO_DEFINE_PRIMITIVE1_DS ("%%call/dc", call_dc, (IDIO proc), "proc", "\
call `proc` with the current (delimited) continuation		\n\
								\n\
:param proc:							\n\
:type proc: a closure of 1 argument				\n\
								\n\
This is the ``%%call/dc`` primitive.				\n\
")
{
    IDIO_ASSERT (proc);

    /*
     * Test Case: vm-errors/call-dc-bad-type.idio
     *
     * %%call/dc #t
     */
    IDIO_USER_TYPE_ASSERT (closure, proc);

    return idio_vm_call_cc (proc, IDIO_CONTINUATION_CALL_DC);
}

/*
 * Code coverage:
 *
 * Used by the debugger.
 */
IDIO_DEFINE_PRIMITIVE0_DS ("%%vm-continuations", vm_continuations, (), "", "\
return the current VM continuations			\n\
							\n\
the format is undefined and subject to arbitrary change	\n\
")
{
    return idio_vm_krun;
}

/*
 * Code coverage:
 *
 * Used by the debugger.
 */
IDIO_DEFINE_PRIMITIVE2_DS ("%%vm-apply-continuation", vm_apply_continuation, (IDIO n, IDIO val), "n v", "\
invoke the `n`\\ :sup:`th` VM continuation with value `v`	\n\
								\n\
:param n: the continuation to invoke				\n\
:type n: (non-negative) integer					\n\
:param v: the value to pass to the continuation			\n\
								\n\
`n` is subject to a range check on the array of stored		\n\
continuations in the VM.					\n\
								\n\
The function does not return.					\n\
")
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (val);

    idio_ai_t n_C = 0;

    if (idio_isa_fixnum (n)) {
	n_C = IDIO_FIXNUM_VAL (n);
    } else if (idio_isa_bignum (n)) {
	if (IDIO_BIGNUM_INTEGER_P (n)) {
	    n_C = idio_bignum_ptrdiff_t_value (n);
	} else {
	    IDIO n_i = idio_bignum_real_to_integer (n);
	    if (idio_S_nil == n_i) {
		idio_error_param_type ("integer", n, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		n_C = idio_bignum_ptrdiff_t_value (n_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (n_C < 0) {
	idio_error_param_type ("positive integer", n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_ai_t krun_p = idio_array_size (idio_vm_krun);

    if (n_C >= krun_p) {
	idio_error_param_type ("out of range", n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO krun = idio_S_nil;

    while (krun_p > n_C) {
	krun = idio_array_pop (idio_vm_krun);
	krun_p--;
    }

    if (idio_isa_pair (krun)) {
	fprintf (stderr, "%%vm-apply-continuation: restoring krun #%zd: ", krun_p);
	idio_debug ("%s\n", IDIO_PAIR_HT (krun));
	idio_vm_restore_continuation (IDIO_PAIR_H (krun), val);

	return idio_S_notreached;
    }

    idio_coding_error_C ("failed to invoke contunation", IDIO_LIST2 (n, val), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

void idio_vm_start_tracing (int level)
{
    idio_vm_tracing_user = level;
    /* idio_vm_tracing_all = 0; */
    /* idio_vm_tracing = 0; */

    if (idio_tracing_FILE != stderr) {
	fclose (idio_tracing_FILE);
    }

    idio_tracing_FILE = stderr;
}

void idio_vm_stop_tracing ()
{
    idio_vm_tracing_user = 0;
    idio_vm_tracing_all = 0;
    idio_vm_tracing = 0;

    if (idio_tracing_FILE != stderr) {
	fclose (idio_tracing_FILE);
    }

    idio_tracing_FILE = stderr;
}

void idio_vm_set_tracing_file (IDIO args)
{
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (pair, args);

    IDIO file = IDIO_PAIR_H (args);
    char *mode_C = IDIO_MODE_W;
    int free_mode_C = 0;
    size_t mode_size = 0;

    if (idio_S_nil != IDIO_PAIR_T (args)) {
	IDIO mode = IDIO_PAIR_HT (args);
	mode_C = idio_string_as_C (mode, &mode_size);
	free_mode_C = 1;

	/*
	 * Use mode_size + 1 to avoid a truncation warning -- we're
	 * just seeing if mode_C includes a NUL
	 */
	size_t C_size = idio_strnlen (mode_C, mode_size + 1);
	if (C_size != mode_size) {
	    IDIO_GC_FREE (mode_C, mode_size);

	    idio_file_handle_format_error ("%%vm-trace", "mode", "contains an ASCII NUL", mode, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    if (idio_tracing_FILE != stderr) {
	fclose (idio_tracing_FILE);
    }

    if (idio_S_nil == file) {
	idio_tracing_FILE = stderr;
    } else if (idio_isa_string (file)) {
	size_t file_size = 0;
	char *file_C = idio_string_as_C (file, &file_size);

	/*
	 * Use file_size + 1 to avoid a truncation warning -- we're
	 * just seeing if file_C includes a NUL
	 */
	size_t C_size = idio_strnlen (file_C, file_size + 1);
	if (C_size != file_size) {
	    IDIO_GC_FREE (file_C, file_size);
	    if (free_mode_C) {
		IDIO_GC_FREE (mode_C, mode_size);
	    }

	    idio_file_handle_format_error ("%%vm-trace", "filename", "contains an ASCII NUL", file, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	idio_tracing_FILE = fopen (file_C, mode_C);
	IDIO_GC_FREE (file_C, file_size);

	if (NULL == idio_tracing_FILE) {
	    perror ("fdopen");
	    idio_tracing_FILE = stderr;
	} else {
	    /*
	     * As it's a new file, make it line buffered as the
	     * chances are we're only using %%vm-trace when things are
	     * going wrong...
	     */
	    setlinebuf (idio_tracing_FILE);
	}
    } else if (idio_isa_fd_handle (file)) {
	idio_tracing_FILE = fdopen (IDIO_FILE_HANDLE_FD (file), mode_C);

	if (NULL == idio_tracing_FILE) {
	    perror ("fdopen");
	    idio_tracing_FILE = stderr;
	}
    }

    if (free_mode_C) {
	IDIO_GC_FREE (mode_C, mode_size);
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%%vm-trace", vm_trace, (IDIO level, IDIO args), "level [file [mode]]", "\
set VM tracing to `level` for user code			\n\
							\n\
:param level: new VM tracing level			\n\
:type level: fixnum					\n\
:param file: new VM tracing file, defaults to ``#n``	\n\
:type file: string, FD handle or ``#n``, optional	\n\
:param mode: file mode, defaults to ``\"w\"``		\n\
:type mode: string, optional				\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (level);
    IDIO_ASSERT (args);

    /*
     * Test Case: vm-errors/vm-trace-bad-type.idio
     *
     * %%vm-trace #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, level);

    idio_vm_tracing_user = IDIO_FIXNUM_VAL (level);

    if (idio_isa_pair (args)) {
	idio_vm_set_tracing_file (args);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%%vm-trace-all", vm_trace_all, (IDIO level, IDIO args), "level [file [mode]]", "\
set VM tracing to `level` for all code			\n\
							\n\
:param level: new VM tracing level			\n\
:type level: fixnum					\n\
:param file: new VM tracing file, defaults to ``#n``	\n\
:type file: string, FD handle or ``#n``, optional	\n\
:param mode: file mode, defaults to ``\"w\"``		\n\
:type mode: string, optional				\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (level);
    IDIO_ASSERT (args);

    /*
     * Test Case: vm-errors/vm-trace-bad-type.idio
     *
     * %%vm-trace #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, level);

    idio_vm_tracing_user = IDIO_FIXNUM_VAL (level);
    if (idio_vm_tracing_user) {
	idio_vm_tracing_all = 1;
	idio_vm_tracing = 1;
    } else {
	idio_vm_tracing_all = 0;
	idio_vm_tracing = 0;
    }

    if (idio_isa_pair (args)) {
	idio_vm_set_tracing_file (args);
    }

    return idio_S_unspec;
}

#ifdef IDIO_VM_DIS
void idio_vm_set_dasm_file (IDIO args)
{
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (pair, args);

    IDIO file = IDIO_PAIR_H (args);
    char *mode_C = IDIO_MODE_W;
    int free_mode_C = 0;
    size_t mode_size = 0;

    if (idio_S_nil != IDIO_PAIR_T (args)) {
	IDIO mode = IDIO_PAIR_HT (args);
	mode_C = idio_string_as_C (mode, &mode_size);
	free_mode_C = 1;

	/*
	 * Use mode_size + 1 to avoid a truncation warning -- we're
	 * just seeing if mode_C includes a NUL
	 */
	size_t C_size = idio_strnlen (mode_C, mode_size + 1);
	if (C_size != mode_size) {
	    IDIO_GC_FREE (mode_C, mode_size);

	    idio_file_handle_format_error ("%%vm-trace", "mode", "contains an ASCII NUL", mode, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    if (idio_dasm_FILE != stderr) {
	fclose (idio_dasm_FILE);
    }

    if (idio_S_nil == file) {
	idio_dasm_FILE = stderr;
    } else if (idio_isa_string (file)) {
	size_t file_size = 0;
	char *file_C = idio_string_as_C (file, &file_size);

	/*
	 * Use file_size + 1 to avoid a truncation warning -- we're
	 * just seeing if file_C includes a NUL
	 */
	size_t C_size = idio_strnlen (file_C, file_size + 1);
	if (C_size != file_size) {
	    IDIO_GC_FREE (file_C, file_size);
	    if (free_mode_C) {
		IDIO_GC_FREE (mode_C, mode_size);
	    }

	    idio_file_handle_format_error ("%%vm-trace", "filename", "contains an ASCII NUL", file, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	idio_dasm_FILE = fopen (file_C, mode_C);
	IDIO_GC_FREE (file_C, file_size);

	if (NULL == idio_dasm_FILE) {
	    perror ("fdopen");
	    idio_dasm_FILE = stderr;
	} else {
	    /*
	     * As it's a new file, make it line buffered as the
	     * chances are we're only using %%vm-dis when things are
	     * going wrong...
	     */
	    setlinebuf (idio_dasm_FILE);
	}
    } else if (idio_isa_fd_handle (file)) {
	idio_dasm_FILE = fdopen (IDIO_FILE_HANDLE_FD (file), mode_C);

	if (NULL == idio_dasm_FILE) {
	    perror ("fdopen");
	    idio_dasm_FILE = stderr;
	}
    }

    if (free_mode_C) {
	IDIO_GC_FREE (mode_C, mode_size);
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%%vm-dis", vm_dis, (IDIO dis, IDIO args), "dis [file [mode]]", "\
set VM live disassembly to to `dis`			\n\
							\n\
:param dis: new VM live disassembly setting		\n\
:type dis: fixnum					\n\
:param file: new VM running DASM file, defaults to ``#n``	\n\
:type file: string, FD handle or ``#n``, optional	\n\
:param mode: file mode, defaults to ``\"w\"``		\n\
:type mode: string, optional				\n\
							\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (dis);
    IDIO_ASSERT (args);

    /*
     * Test Case: ??
     *
     * VM_DIS is not a feature!
     */
    IDIO_USER_TYPE_ASSERT (fixnum, dis);

    idio_vm_dis = IDIO_FIXNUM_VAL (dis);

    if (idio_isa_pair (args)) {
	idio_vm_set_dasm_file (args);
    }

    return idio_S_unspec;
}

#define IDIO_VM_RUN_DIS(...)	if (idio_vm_dis) { fprintf (idio_dasm_FILE, __VA_ARGS__); }
#else
#define IDIO_VM_RUN_DIS(...)
#endif

/*
 * Code coverage:
 *
 * Used by reporting tools.
 */
IDIO idio_vm_closure_name (IDIO c)
{
    IDIO_ASSERT (c);
    IDIO_TYPE_ASSERT (closure, c);

    return IDIO_CLOSURE_NAME (c);
}

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
static struct timespec idio_vm_ts_cur = {0, 0};
static struct timespec idio_vm_ts_delta = {0, 0};
void idio_vm_time_delta ()
{
    struct timespec ts;
    if (clock_gettime (CLOCK_MONOTONIC, &ts) < 0) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ts)");
    }

    if (idio_vm_ts_cur.tv_sec) {
	idio_vm_ts_delta.tv_sec = ts.tv_sec - idio_vm_ts_cur.tv_sec;
	idio_vm_ts_delta.tv_nsec = ts.tv_nsec - idio_vm_ts_cur.tv_nsec;

	if (idio_vm_ts_delta.tv_nsec < 0) {
	    idio_vm_ts_delta.tv_nsec += IDIO_VM_NS;
	    idio_vm_ts_delta.tv_sec -= 1;
	}
    }

    idio_vm_ts_cur.tv_sec = ts.tv_sec;
    idio_vm_ts_cur.tv_nsec = ts.tv_nsec;
}

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
static void idio_vm_function_trace (IDIO_I ins, IDIO thr)
{
    if (idio_vm_tracing < 1 ||
	idio_vm_tracing > idio_vm_tracing_user) {
	return;
    }

    IDIO func = IDIO_THREAD_FUNC (thr);
    IDIO val = IDIO_THREAD_VAL (thr);
    IDIO args = idio_frame_params_as_list (val);
    IDIO expr = idio_list_append2 (IDIO_LIST1 (func), args);

    /*
     * %9d	- clock ns
     * SPACE
     * %6d	- PID
     * SPACE
     * %-11s	- [xi]@PC of ins
     * SPACE
     * %40s	- lexical information
     * %.*s	- trace-depth indent
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %s	- expression types
     */

    idio_vm_time_delta ();
    fprintf (idio_tracing_FILE, "%09ld ", idio_vm_ts_delta.tv_nsec);
    fprintf (idio_tracing_FILE, "%6d ", getpid ());

    /*
     * Technically, intptr_t (idio_xi_t, idio_pc_t) can be 20 chars
     * plus "[]@" and a newline means a good 50 byte buffer is
     * required
     */
    char buf[51];
    snprintf (buf, 50, "[%zu]@%zd", IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr) - 1);
    buf[50] = '\0';
    fprintf (idio_tracing_FILE, "%-11s ", buf);

    IDIO lo_sh = idio_open_output_string_handle_C ();
    IDIO fsei = IDIO_THREAD_EXPR (thr);
    idio_display (fsei, lo_sh);
    idio_debug_FILE (idio_tracing_FILE, "%-40s", idio_get_output_string (lo_sh));

    fprintf (idio_tracing_FILE, "%.*s  ", idio_vm_tracing, idio_vm_tracing_in);

    IDIO name = idio_ref_property (func, idio_KW_name, IDIO_LIST1 (idio_S_nil));
    if (idio_S_nil != name) {
	size_t size = 0;
	char *s = idio_display_string (name, &size);
	fprintf (idio_tracing_FILE, "(%s", s);
	IDIO_GC_FREE (s, size);
    } else {
	fprintf (idio_tracing_FILE, "(-anon-");
    }

    IDIO sigstr = idio_ref_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));
    if (idio_S_nil != sigstr) {
	size_t size = 0;
	char *s = idio_display_string (sigstr, &size);
	if (size) {
	    fprintf (idio_tracing_FILE, " %s", s);
	}
	IDIO_GC_FREE (s, size);
    }
    fprintf (idio_tracing_FILE, ")");

    fprintf (idio_tracing_FILE, " was ");
    switch (ins) {
    case IDIO_A_FUNCTION_GOTO:
	fprintf (idio_tracing_FILE, "tail-called as\n");
	break;
    case IDIO_A_FUNCTION_INVOKE:
	fprintf (idio_tracing_FILE, "called as\n");
	break;
    }

    /*
     * indent back to same level...
     */
    fprintf (idio_tracing_FILE, "%9s ", "");
    fprintf (idio_tracing_FILE, "%6s ", "");
    fprintf (idio_tracing_FILE, "%7s ", "");
    fprintf (idio_tracing_FILE, "%40s", "");

    fprintf (idio_tracing_FILE, "%*s  ", idio_vm_tracing, "");

    fprintf (idio_tracing_FILE, "(");
    int first = 1;
    while (idio_S_nil != expr) {
	IDIO e = IDIO_PAIR_H (expr);

	if (first) {
	    first = 0;
	} else {
	    fprintf (idio_tracing_FILE, " ");
	}
	size_t size = 0;
	char *s = idio_report_string (e, &size, 4, idio_S_nil, 1);
	fprintf (idio_tracing_FILE, "%s", s);
	IDIO_GC_FREE (s, size);

	expr = IDIO_PAIR_T (expr);
    }
    fprintf (idio_tracing_FILE, ")");

    fprintf (idio_tracing_FILE, "\n");
}

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
static void idio_vm_primitive_call_trace (IDIO primdata, IDIO thr, int nargs)
{
    if (idio_vm_tracing < 1 ||
	idio_vm_tracing > idio_vm_tracing_user) {
	return;
    }

    /*
     * %9d	- clock ns
     * SPACE
     * %6d	- PID
     * SPACE
     * %-11s	- [xi]@PC of ins
     * SPACE
     * %40s	- lexical information
     * %.*s	- trace-depth indent (>= 1)
     * %s	- expression
     */
    idio_vm_time_delta ();
    fprintf (idio_tracing_FILE, "%09ld ", idio_vm_ts_delta.tv_nsec);
    fprintf (idio_tracing_FILE, "%6d ", getpid ());

    char buf[51];
    snprintf (buf, 50, "[%zu]@%zd", IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr) - 1);
    buf[50] = '\0';
    fprintf (idio_tracing_FILE, "%-11s ", buf);

    IDIO lo_sh = idio_open_output_string_handle_C ();
    IDIO fsei = IDIO_THREAD_EXPR (thr);
    idio_display (fsei, lo_sh);
    idio_debug_FILE (idio_tracing_FILE, "%-40s", idio_get_output_string (lo_sh));

    fprintf (idio_tracing_FILE, "%.*s  ", idio_vm_tracing, idio_vm_tracing_in);
    fprintf (idio_tracing_FILE, "(%s", IDIO_PRIMITIVE_NAME (primdata));

    IDIO sigstr = idio_ref_property (primdata, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));
    if (idio_S_nil != sigstr) {
	size_t size = 0;
	char *s = idio_display_string (sigstr, &size);
	fprintf (idio_tracing_FILE, " %s", s);
	IDIO_GC_FREE (s, size);
    }
    fprintf (idio_tracing_FILE, ") primitive call as\n");

    /*
     * indent back to same level...
     */
    fprintf (idio_tracing_FILE, "%9s ", "");
    fprintf (idio_tracing_FILE, "%6s ", "");
    fprintf (idio_tracing_FILE, "%7s ", "");
    fprintf (idio_tracing_FILE, "%40s", "");

    fprintf (idio_tracing_FILE, "%*s  ", idio_vm_tracing, "");

    fprintf (idio_tracing_FILE, "(%s", IDIO_PRIMITIVE_NAME (primdata));
    if (nargs > 1) {
	size_t size = 0;
	char *s = idio_report_string (IDIO_THREAD_REG1 (thr), &size, 4, idio_S_nil, 1);
	fprintf (idio_tracing_FILE, " %s", s);
	IDIO_GC_FREE (s, size);
    }
    if (nargs > 0) {
	size_t size = 0;
	char *s = idio_report_string (IDIO_THREAD_VAL (thr), &size, 4, idio_S_nil, 1);
	fprintf (idio_tracing_FILE, " %s", s);
	IDIO_GC_FREE (s, size);
    }
    fprintf (idio_tracing_FILE, ")\n");
}

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
static void idio_vm_primitive_result_trace (IDIO thr)
{
    if (idio_vm_tracing < 1 ||
	idio_vm_tracing > idio_vm_tracing_user) {
	return;
    }

    /*
     * %9d	- clock ns
     * SPACE
     * %6d	- PID
     * SPACE
     * %-11s	- [xi]@PC of ins
     * SPACE
     * %40s	- (lexical information == "")
     * %.*s	- trace-depth indent
     * %s	- value
     */

    idio_vm_time_delta ();
    fprintf (idio_tracing_FILE, "%09ld ", idio_vm_ts_delta.tv_nsec);
    fprintf (idio_tracing_FILE, "%6d ", getpid ());

    char buf[51];
    snprintf (buf, 50, "[%zu]@%zd", IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr));
    buf[50] = '\0';
    fprintf (idio_tracing_FILE, "%-11s ", buf);

    fprintf (idio_tracing_FILE, "%40s", "");
    fprintf (idio_tracing_FILE, "%.*s  ", idio_vm_tracing, idio_vm_tracing_out);
    size_t size = 0;
    char *s = idio_report_string (IDIO_THREAD_VAL (thr), &size, 4, idio_S_nil, 1);
    fprintf (idio_tracing_FILE, "%s\n", s);
    IDIO_GC_FREE (s, size);
}

#ifdef IDIO_VM_PROF
void idio_vm_update_ins_time (IDIO_I ins, struct timespec ins_t0)
{
    struct timespec ins_te;
    if (clock_gettime (CLOCK_MONOTONIC, &ins_te) < 0) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ins_te)");
    }

    struct timespec ins_td;
    ins_td.tv_sec = ins_te.tv_sec - ins_t0.tv_sec;
    ins_td.tv_nsec = ins_te.tv_nsec - ins_t0.tv_nsec;
    if (ins_td.tv_nsec < 0) {
	ins_td.tv_nsec += IDIO_VM_NS;
	ins_td.tv_sec -= 1;
    }

    idio_vm_ins_call_time[ins].tv_sec += ins_td.tv_sec;
    idio_vm_ins_call_time[ins].tv_nsec += ins_td.tv_nsec;
    if (idio_vm_ins_call_time[ins].tv_nsec >= IDIO_VM_NS) {
	idio_vm_ins_call_time[ins].tv_nsec -= IDIO_VM_NS;
	idio_vm_ins_call_time[ins].tv_sec += 1;
    }
}
#endif

/*
 * Ensure we have a gvi
 */
typedef enum {
    IDIO_VM_IREF_MDR_UNDEF_FATAL,
    IDIO_VM_IREF_MDR_UNDEF_NEW,
} idio_vm_iref_enum;

idio_as_t idio_vm_iref (IDIO thr, idio_xi_t xi, idio_as_t si, char *const op, IDIO def, idio_vm_iref_enum mode)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (op);
    IDIO_ASSERT (def);

    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", op, si);

    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
    IDIO fgvi = idio_array_ref_index (vs, si);
    idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

    if (0 == gvi) {
	IDIO sym = idio_vm_symbols_ref (xi, si);
	IDIO_TYPE_ASSERT (symbol, sym);

	IDIO_VM_RUN_DIS ("%-20s ", IDIO_SYMBOL_S (sym));

	IDIO fsi = idio_fixnum (si);
	IDIO ce = idio_thread_current_env ();
	IDIO si_ce = idio_module_find_symbol_recurse (sym, ce, 1);

	if (idio_S_false == si_ce) {
	    /*
	     * This happens for:
	     *
	     * * first use of a name:
	     *
	     *   - require path-functions
	     *
	     *   - (function? struct-utsname-set!)
	     *
	     *     as part of a test to see if the setter has
	     *     been defined (ans: no)
	     *
	     *   - (false)
	     *
	     *     external command name (and arguments)
	     *
	     * Leaving no easy way to identify a genuine failure.
	     * Which is annoying.
	     *
	     * If we failed to look it up (by hook or by crook) we'll
	     * now fall through to the missing clause
	     */
	} else {
	    fgvi = IDIO_SI_VI (si_ce);
	    gvi = IDIO_FIXNUM_VAL (fgvi);

	    idio_vm_values_set (xi, si, fgvi);
	}

	if (0 == gvi) {
	    /* missing => symbol of itself */

	    gvi = idio_vm_extend_values (0);
	    fgvi = idio_fixnum (gvi);
	    idio_vm_values_set (xi, si, fgvi);

	    idio_vm_values_set (0, gvi, sym);

	    /*
	    fprintf (stderr, "%-17s iref     [%zu].%-4zd [0].%-4zd ", op, xi, si, gvi);
	    idio_debug ("%-10s ", IDIO_MODULE_NAME (ce));
	    idio_debug ("%-20s\n", sym);
	    */

	    IDIO_VM_RUN_DIS ("=> [0].%-4" PRIu64 " ", gvi);

	    idio_as_t ci = idio_vm_constants_lookup_or_extend (xi, sym);
	    IDIO fci = idio_fixnum (ci);

	    si_ce = IDIO_LIST6 (idio_S_toplevel, fsi, fci, fgvi, ce, def);
	    idio_module_set_symbol (sym, si_ce, ce);
	}
    }

    return gvi;
}

IDIO idio_vm_iref_val (IDIO thr, idio_xi_t xi, idio_as_t si, char *const op, idio_vm_iref_val_enum mode)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (op);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
    idio_ai_t gvi = si;

    if (xi) {
	IDIO fgvi = idio_array_ref_index (vs, si);
	IDIO_TYPE_ASSERT (fixnum, fgvi);
	gvi = IDIO_FIXNUM_VAL (fgvi);

	if (0 == gvi) {
	    idio_vm_iref_enum flag = IDIO_VM_IREF_MDR_UNDEF_FATAL;
	    if (IDIO_VM_IREF_VAL_UNDEF_SYM == mode) {
		flag = IDIO_VM_IREF_MDR_UNDEF_NEW;
	    }
	    gvi = idio_vm_iref (thr, xi, si, op, idio_S_false, flag);
	    fgvi = idio_fixnum (gvi);
	}
    }

    IDIO val = idio_vm_values_ref (0, gvi);

    if (idio_S_undef == val) {
	IDIO sym = idio_vm_symbols_ref (xi, si);
	IDIO_TYPE_ASSERT (symbol, sym);

	if (IDIO_VM_IREF_VAL_UNDEF_SYM == mode) {
	    idio_vm_values_set (0, gvi, sym);

	    /*
	    fprintf (stderr, "%-17s iref_val undef->sym [%zu].%-4zd [0].%-4zd ", op, xi, si, gvi);
	    idio_debug ("%-20s ", sym);
	    idio_debug ("%s\n", idio_module_find_symbol_recurse (sym, IDIO_THREAD_ENV (thr), 1));
	    */

	    val = sym;
	} else {
	    IDIO fsi = idio_fixnum (si);
	    /*
	     * Test Case: ??
	     *
	     * It means that the VM's global table of values has
	     * undefined values in it.  Which can't be a good thing.
	     */
	    idio_error_runtime_unbound (fsi, idio_fixnum (gvi), sym, IDIO_C_FUNC_LOCATION_S (op));

	    return idio_S_notreached;
	}
    }

    return val;
}

void idio_vm_iset_val (IDIO thr, idio_xi_t xi, idio_as_t si, char *const op, IDIO def, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (op);
    IDIO_ASSERT (def);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, def);

    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
    IDIO fgvi = idio_array_ref_index (vs, si);
    idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

    if (0 == gvi) {
	gvi = idio_vm_iref (thr, xi, si, op, def, IDIO_VM_IREF_MDR_UNDEF_NEW);
    }

    /*
     * Overwriting predefs should be banned -- except that it's pretty
     * useful to improve/embellish the basic C implementation.
     *
     * If we do overwrite a predef we must update the symbol's info in
     * order that future aspirants do not treat this closure as a
     * primitive (and things go subtly wrong in the VM).
     */
    IDIO sym = idio_vm_symbols_ref (xi, si);
    IDIO_TYPE_ASSERT (symbol, sym);

    IDIO ce = idio_thread_current_env ();
    IDIO si_ce = idio_module_find_symbol (sym, ce);

    if (idio_isa_pair (si_ce) &&
	idio_S_predef == IDIO_SI_SCOPE (si_ce) &&
	! idio_isa_primitive (val)) {
	/*
	 * Overwrite bits of the symbol info including a new vi
	 */
	IDIO_SI_SCOPE (si_ce) = idio_S_toplevel;
	IDIO_SI_SI (si_ce) = idio_fixnum (si);

	gvi = idio_vm_extend_values (0);
	fgvi = idio_fixnum (gvi);

	IDIO_SI_VI (si_ce) = fgvi;

	IDIO_SI_DESCRIPTION (si_ce) = idio_vm_SYM_SET_predef_string;

	idio_vm_values_set (xi, si, fgvi);

	idio_module_set_symbol (sym, si_ce, ce);
    }

    idio_vm_values_set (0, gvi, val);
}

int idio_vm_run1 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_IA_T bc = IDIO_THREAD_BYTE_CODE (thr);

    idio_xi_t xi = IDIO_THREAD_XI (thr);
    idio_pc_t pc = IDIO_THREAD_PC (thr);
    if (pc < 0) {
	fprintf (stderr, "\n\nidio_vm_run1: #%d [%zu]@%zd has PC < 0\n", IDIO_THREAD_FLAGS (thr), xi, pc);
	idio_vm_panic (thr, "idio_vm_run1: bad PC!");
    } else if (pc >= (ssize_t) IDIO_IA_USIZE (bc)) {
	fprintf (stderr, "\n\nidio_vm_run1: #%d [%zu]@%zd >= max code PC %" PRIdPTR "\n", IDIO_THREAD_FLAGS (thr), xi, pc, IDIO_IA_USIZE (bc));
	idio_vm_panic (thr, "idio_vm_run1: bad PC!");
    }

    IDIO_I ins = IDIO_THREAD_FETCH_NEXT (bc);

    switch (ins) {
    case IDIO_A_PUSH_ABORT:
	IDIO_VM_RUN_DIS ("\n");
	break;
    }

#ifdef IDIO_VM_DIS
    char xs[30];		/* %zu is ~20 characters */
    snprintf (xs, 30, "[%zu]", xi);
    char sss[30];
    snprintf (sss, 30, "{%zu}", idio_array_size (IDIO_THREAD_STACK (thr)));
    IDIO_VM_RUN_DIS ("              #%-2d%4s@%-6zd%6s ",  IDIO_THREAD_FLAGS (thr), xs, pc, sss);
#endif

#ifdef IDIO_VM_PROF
    idio_vm_ins_counters[ins]++;
    struct timespec ins_t0;
    if (clock_gettime (CLOCK_MONOTONIC, &ins_t0) < 0) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ins_t0)");
    }
#endif

    IDIO_VM_RUN_DIS ("%3d: ", ins);

    switch (ins) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 0");

	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 0);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 1");

	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 1);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 2");

	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 2);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 3");

	IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 3);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:
	{
	    uint64_t j = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF %" PRIu64, j);

	    IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), j);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (bc, thr);
	    uint64_t j = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-REF %" PRIu64 " %" PRIu64, i, j);

	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), i, j);
	}
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET0:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 0");

	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 0) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 1");

	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 1) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 2");

	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 2) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 3");

	IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), 3) = IDIO_THREAD_VAL (thr);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET %" PRIu64, i);

	    IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), i) = IDIO_THREAD_VAL (thr);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (bc, thr);
	    uint64_t j = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-SET %" PRIu64 " %" PRIu64, i, j);

	    idio_frame_update (IDIO_THREAD_FRAME (thr), i, j, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_SYM_REF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", "SYM-REF", si);

	    IDIO_THREAD_VAL (thr) = idio_vm_iref_val (thr, xi, si, "SYM-REF", IDIO_VM_IREF_VAL_UNDEF_SYM);
	}
	break;
    case IDIO_A_FUNCTION_SYM_REF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", "FUNCTION-SYM-REF", si);

	    IDIO_THREAD_VAL (thr) = idio_vm_iref_val (thr, xi, si, "FUNCTION-SYM-REF", IDIO_VM_IREF_VAL_UNDEF_SYM);
	}
	break;
    case IDIO_A_CONSTANT_REF:
	{
	    uint64_t ci = idio_vm_fetch_varuint (bc, thr);

	    IDIO c = idio_vm_constants_ref (xi, ci);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", "CONSTANT-REF", ci);
#ifdef IDIO_VM_DIS
	    if (idio_vm_dis) {
		idio_debug_FILE (idio_dasm_FILE, "%.80s", c);
	    }
#endif

	    switch ((intptr_t) c & IDIO_TYPE_MASK) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
		IDIO_THREAD_VAL (thr) = c;
		break;
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_coding_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-REF"));

		/* notreached */
		return 0;
		break;
	    case IDIO_TYPE_POINTER_MARK:
		{
		    switch (c->type) {
		    case IDIO_TYPE_STRING:
			if (IDIO_FLAGS (c) & IDIO_FLAG_CONST) {
			    IDIO_THREAD_VAL (thr) = c;
			} else {
			    IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			}
			break;
		    case IDIO_TYPE_SYMBOL:
		    case IDIO_TYPE_KEYWORD:
			IDIO_THREAD_VAL (thr) = c;
			break;
		    case IDIO_TYPE_PAIR:
		    case IDIO_TYPE_ARRAY:
		    case IDIO_TYPE_HASH:
		    case IDIO_TYPE_BIGNUM:
		    case IDIO_TYPE_BITSET:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_STRUCT_INSTANCE:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_PRIMITIVE:
		    case IDIO_TYPE_CLOSURE:
			idio_debug ("idio_vm_run1/CONSTANT-REF: you should NOT be reifying %s", c);
			IDIO name = idio_ref_property (c, idio_KW_name, idio_S_unspec);
			if (idio_S_unspec != name) {
			    idio_debug (" %s", name);
			}
			fprintf (stderr, "\n");
			IDIO_THREAD_VAL (thr) = c;
			break;
		    default:
			/*
			 * Test Case: ??
			 *
			 * Coding error.
			 */
			idio_coding_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-REF"));

			/* notreached */
			return 0;
			break;
		    }
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT-REF"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", c, (intptr_t) c & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

		/* notreached */
		return 0;
		break;
	    }
    	}
    	break;
    case IDIO_A_COMPUTED_SYM_REF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

#ifdef IDIO_DEBUG
	    IDIO sym = idio_vm_symbols_ref (xi, si);
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " %-20s ", "COMPUTED-SYM-REF", si, IDIO_SYMBOL_S (sym));
#else
	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", "COMPUTED-SYM-REF", si);
#endif

	    IDIO gns = idio_vm_iref_val (thr, xi, si, "COMPUTED-SYM-REF", IDIO_VM_IREF_VAL_UNDEF_FATAL);
	    IDIO_THREAD_VAL (thr) = idio_vm_computed_iref (gns, si);
	}
	break;
    case IDIO_A_SYM_DEF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t kci = idio_vm_fetch_varuint (bc, thr);

	    IDIO fsi = idio_fixnum (si);

	    /*
	     * We could call idio_vm_symbols_ref() but we might need
	     * the intermediate values in a minute
	     */
	    IDIO st = IDIO_XENV_ST (idio_xenvs[xi]);
	    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);

	    IDIO fci = idio_array_ref_index (st, si);

	    IDIO sym = idio_array_ref_index (cs, IDIO_FIXNUM_VAL (fci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO kind = idio_vm_constants_ref (xi, kci);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " %-20s ", "SYM-DEF", si, IDIO_SYMBOL_S (sym));

	    IDIO ce = idio_thread_current_env ();
	    IDIO si_ce;
	    if (idio_S_environ == kind ||
		idio_S_dynamic == kind) {
		si_ce = idio_module_find_symbol_recurse (sym, ce, 1);
	    } else {
		si_ce = idio_module_find_symbol (sym, ce);
	    }

	    idio_as_t gvi = 0;
	    IDIO fgvi = idio_S_false;

	    if (idio_S_false == si_ce) {
		idio_as_t gci = idio_vm_constants_lookup_or_extend (0, sym);
		IDIO fgci = idio_fixnum (gci);

		gvi = idio_vm_extend_values (0);
		fgvi = idio_fixnum (gvi);

		si_ce = IDIO_LIST6 (kind, fsi, fgci, fgvi, ce, idio_vm_SYM_DEF_string);
		idio_module_set_symbol (sym, si_ce, ce);
	    } else {
		fgvi = IDIO_SI_VI (si_ce);
		gvi = IDIO_FIXNUM_VAL (fgvi);

		if (0 == gvi) {
		    gvi = idio_vm_extend_values (0);
		    fgvi = idio_fixnum (gvi);

		    IDIO_SI_VI (si_ce) = fgvi;
		    IDIO_SI_DESCRIPTION (si_ce) = idio_vm_SYM_DEF_gvi0_string;
		}
	    }

	    IDIO_VM_RUN_DIS ("[0].%" PRIu64 " ", gvi);
	}
	break;
    case IDIO_A_SYM_SET:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    idio_vm_iset_val (thr, xi, si, "SYM-SET", idio_vm_SYM_SET_gvi0_string, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_COMPUTED_SYM_SET:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

#ifdef IDIO_DEBUG
	    IDIO sym = idio_vm_symbols_ref (xi, si);
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " %-20s ", "COMPUTED-SYM-SET", si, IDIO_SYMBOL_S (sym));
#else
	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", "COMPUTED-SYM-SET", si);
#endif

	    IDIO gns = idio_vm_iref_val (thr, xi, si, "COMPUTED-SYM-SET", IDIO_VM_IREF_VAL_UNDEF_FATAL);
	    IDIO val = IDIO_THREAD_VAL (thr);
	    IDIO_THREAD_VAL (thr) = idio_vm_computed_iset (gns, si, val);
	}
	break;
    case IDIO_A_COMPUTED_SYM_DEF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO fsi = idio_fixnum (si);

	    IDIO sym = idio_vm_symbols_ref (xi, si);
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " %-20s", "COMPUTED-SYM-DEF", si, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = 0;
	    IDIO fgvi = idio_S_false;

	    IDIO ce = idio_thread_current_env ();
	    IDIO si_ce = idio_module_find_symbol (sym, ce);

	    if (idio_S_false == si_ce) {
		idio_as_t gci = idio_vm_constants_lookup_or_extend (0, sym);
		IDIO fgci = idio_fixnum (gci);

		gvi = idio_vm_extend_values (0);
		fgvi = idio_fixnum (gvi);
		idio_vm_values_set (xi, si, fgvi);

		si_ce = IDIO_LIST6 (idio_S_toplevel, fsi, fgci, fgvi, ce, idio_vm_COMPUTED_SYM_DEF_string);
		idio_module_set_symbol (sym, si_ce, ce);
	    } else {
		fgvi = IDIO_SI_VI (si_ce);
		gvi = IDIO_FIXNUM_VAL (fgvi);

		if (0 == gvi) {
		    gvi = idio_vm_extend_values (0);
		    fgvi = idio_fixnum (gvi);
		    idio_vm_values_set (xi, si, fgvi);

		    si_ce = IDIO_LIST6 (idio_S_toplevel,
					fsi,
					IDIO_SI_CI (si_ce),
					fgvi,
					ce,
					idio_vm_COMPUTED_SYM_DEF_gvi0_string);
		    idio_module_set_symbol (sym, si_ce, ce);
		}
	    }

	    IDIO_VM_RUN_DIS ("[0].%" PRIu64 " ", gvi);

	    if (xi) {
		IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
		idio_array_set (vs, fsi, fgvi);
	    }

	    IDIO val = IDIO_THREAD_VAL (thr);

	    idio_vm_computed_define (0, si, gvi, val);
	}
	break;
    case IDIO_A_VAL_SET:
	{
	    uint64_t vi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", "VAL-SET", vi);

	    if (vi) {
		IDIO fgvi = idio_vm_values_ref (xi, vi);
		idio_as_t gvi = IDIO_FIXNUM_VAL (fgvi);

		if (0 == gvi) {
		    gvi = idio_vm_extend_values (0);
		    fgvi = idio_fixnum (gvi);
		    idio_vm_values_set (xi, vi, fgvi);
		}

		IDIO_VM_RUN_DIS ("[0].%" PRIu64 " ", gvi);

		idio_vm_values_set (0, gvi, IDIO_THREAD_VAL (thr));
	    } else {
		idio_vm_panic (thr, "VAL-SET: no vi!");
	    }
	}
	break;
    case IDIO_A_PREDEFINED0:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 0 #t");

	    IDIO_THREAD_VAL (thr) = idio_S_true;
	}
	break;
    case IDIO_A_PREDEFINED1:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 1 #f");

	    IDIO_THREAD_VAL (thr) = idio_S_false;
	}
	break;
    case IDIO_A_PREDEFINED2:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 2 #nil");

	    IDIO_THREAD_VAL (thr) = idio_S_nil;
	}
	break;
    case IDIO_A_PREDEFINED:
	{
	    uint64_t si = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("%-17s   .%-4" PRIu64 " ", "PREDEFINED", si);

	    IDIO pd = idio_vm_iref_val (thr, xi, si, "PREDEFINED", IDIO_VM_IREF_VAL_UNDEF_FATAL);
	    if (idio_isa_primitive (pd)) {
		IDIO_VM_RUN_DIS ("%-20s", IDIO_PRIMITIVE_NAME (pd));
	    } else {
		IDIO_VM_RUN_DIS ("!! isa %-20s !!", idio_type2string (pd));
		fprintf (stderr, "%-17s   .%-4" PRIu64 " ", "PREDEFINED", si);
		IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
		IDIO fgvi = idio_array_ref_index (vs, si);
		IDIO_TYPE_ASSERT (fixnum, fgvi);
		idio_as_t gvi = IDIO_FIXNUM_VAL (fgvi);
		fprintf (stderr, "[0].%-4zd ", gvi);
		fprintf (stderr, "[%zu]@%zd\n", xi, pc);
		IDIO_TYPE_ASSERT (primitive, pd);
	    }
	    IDIO_THREAD_VAL (thr) = pd;
	}
	break;
    case IDIO_A_LONG_GOTO:
	{
	    uint64_t i = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("LONG-GOTO +%" PRIu64, i);

	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_LONG_JUMP_FALSE:
	{
	    uint64_t i = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("LONG-JUMP-FALSE +%" PRIu64, i);

	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_LONG_JUMP_TRUE:
	{
	    uint64_t i = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("LONG-JUMP-TRUE +%" PRIu64, i);

	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_GOTO:
	{
	    IDIO_I i = IDIO_THREAD_FETCH_NEXT (bc);

	    IDIO_VM_RUN_DIS ("SHORT-GOTO +%hhu", i);

	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_SHORT_JUMP_FALSE:
	{
	    IDIO_I i = IDIO_THREAD_FETCH_NEXT (bc);

	    IDIO_VM_RUN_DIS ("SHORT-JUMP-FALSE +%hhu", i);

	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_JUMP_TRUE:
	{
	    IDIO_I i = IDIO_THREAD_FETCH_NEXT (bc);

	    IDIO_VM_RUN_DIS ("SHORT-JUMP-TRUE +%hhu", i);

	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_PUSH_VALUE:
	{
	    IDIO_VM_RUN_DIS ("PUSH-VALUE");

	    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_VALUE:
	{
	    IDIO_VM_RUN_DIS ("POP-VALUE");

	    IDIO_THREAD_VAL (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_POP_REG1:
	{
	    IDIO_VM_RUN_DIS ("POP-REG1");

	    IDIO_THREAD_REG1 (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_POP_REG2:
	{
	    IDIO_VM_RUN_DIS ("POP-REG2");

	    IDIO_THREAD_REG2 (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_SRC_EXPR:
	{
	    idio_ai_t sei = idio_vm_fetch_varuint (bc, thr);

	    IDIO fsei = idio_fixnum (sei);

	    IDIO_VM_RUN_DIS ("SRC-EXPR %zd", sei);

	    IDIO_THREAD_EXPR (thr) = fsei;
	    break;
	}
	break;
    case IDIO_A_POP_FUNCTION:
	{
	    IDIO_VM_RUN_DIS ("POP-FUNCTION");

	    IDIO_THREAD_FUNC (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_PRESERVE_STATE:
	{
	    IDIO_VM_RUN_DIS ("PRESERVE-STATE");

	    idio_vm_preserve_state (thr);
	}
	break;
    case IDIO_A_RESTORE_STATE:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-STATE");

	    idio_vm_restore_state (thr);
	}
	break;
    case IDIO_A_RESTORE_ALL_STATE:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-ALL-STATE");

	    idio_vm_restore_all_state (thr);
	}
	break;
    case IDIO_A_CREATE_FUNCTION:
	{
	    uint64_t i        = idio_vm_fetch_varuint (bc, thr);
	    uint64_t code_len = idio_vm_fetch_varuint (bc, thr);
	    uint64_t nci      = idio_vm_fetch_varuint (bc, thr);
	    uint64_t ssci     = idio_vm_fetch_varuint (bc, thr);
	    uint64_t dsci     = idio_vm_fetch_varuint (bc, thr);
	    uint64_t sei      = idio_vm_fetch_varuint (bc, thr);

	    /* name lookup */
	    IDIO name = idio_vm_constants_ref (xi, nci);

	    /* sigstr lookup */
	    IDIO sigstr = idio_vm_constants_ref (xi, ssci);

	    /* docstr lookup */
	    IDIO docstr = idio_vm_constants_ref (xi, dsci);

	    IDIO_VM_RUN_DIS ("CREATE-FUNCTION @ +%" PRIu64 " %-20s ", i, IDIO_SYMBOL_S (name));

	    IDIO_THREAD_VAL (thr) = idio_toplevel_closure (xi,
							   IDIO_THREAD_PC (thr) + i,
							   code_len,
							   IDIO_THREAD_FRAME (thr),
							   IDIO_THREAD_ENV (thr),
							   name,
							   sigstr,
							   docstr,
							   sei);
	}
	break;
    case IDIO_A_CREATE_CLOSURE:
	{
	    uint64_t vi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("CREATE-CLOSURE .%-4" PRIu64 " ", vi);

	    IDIO cl = idio_vm_values_gref (xi, vi, "CREATE-CLOSURE");

	    IDIO_THREAD_VAL (thr) = idio_closure (cl, IDIO_THREAD_FRAME (thr));
	}
	break;
    case IDIO_A_FUNCTION_INVOKE:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-INVOKE ...\n");
	    if (! idio_isa_primitive (IDIO_THREAD_FUNC (thr))) {
		IDIO_VM_RUN_DIS ("\n");
	    }

	    idio_vm_function_trace (ins, thr);
#ifdef IDIO_VM_PROF
	    idio_vm_clos_time (thr, "FUNCTION-INVOKE");
	    idio_vm_update_ins_time (ins, ins_t0);
#endif

	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), IDIO_VM_INVOKE_REGULAR_CALL);
	}
	break;
    case IDIO_A_FUNCTION_GOTO:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-GOTO ...\n");
	    if (! idio_isa_primitive (IDIO_THREAD_FUNC (thr))) {
		IDIO_VM_RUN_DIS ("\n");
	    }

	    idio_vm_function_trace (ins, thr);
#ifdef IDIO_VM_PROF
	    idio_vm_clos_time (thr, "FUNCTION-GOTO");
	    idio_vm_update_ins_time (ins, ins_t0);
#endif

	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), IDIO_VM_INVOKE_TAIL_CALL);
	}
	break;
    case IDIO_A_RETURN:
	{
	    IDIO marker = IDIO_THREAD_STACK_POP ();
	    if (idio_SM_return != marker) {
		idio_debug ("\n\nERROR: RETURN: marker: expected idio_SM_return not %s\n", marker);
		IDIO_THREAD_STACK_PUSH (marker);
		idio_vm_decode_thread (thr);
		idio_vm_panic (thr, "RETURN: unexpected stack marker");
	    }

	    /*
	     * Recover and test the stack values for XI and PC
	     */
	    IDIO fs_xi = IDIO_THREAD_STACK_POP ();
	    if (! IDIO_TYPE_FIXNUMP (fs_xi)) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_debug ("\n\nRETURN {fixnum}: not %s\n", fs_xi);
		idio_vm_debug (thr, "IDIO_A_RETURN", 0);
		IDIO_THREAD_STACK_PUSH (fs_xi);
		IDIO_THREAD_STACK_PUSH (marker);
		idio_vm_decode_thread (thr);
		idio_coding_error_C ("RETURN: xi not a number", fs_xi, IDIO_C_FUNC_LOCATION_S ("RETURN"));

		/* notreached */
		return 0;
	    }
	    idio_xi_t s_xi = IDIO_FIXNUM_VAL (fs_xi);
	    if (s_xi >= idio_xenvs_size) {
		fprintf (stderr, "\n\nXI= %zu of %zu?\n", s_xi, idio_xenvs_size);
		idio_dump (thr, 1);
		idio_dump (IDIO_THREAD_STACK (thr), 1);
		idio_vm_decode_thread (thr);
		idio_vm_panic (thr, "RETURN: impossible XI on stack top");
	    }

	    IDIO fs_pc = IDIO_THREAD_STACK_POP ();
	    if (! IDIO_TYPE_FIXNUMP (fs_pc)) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_debug ("\n\nRETURN {fixnum} {fixnum}: not %s\n", fs_pc);
		idio_vm_debug (thr, "IDIO_A_RETURN", 0);
		IDIO_THREAD_STACK_PUSH (fs_pc);
		IDIO_THREAD_STACK_PUSH (fs_xi);
		IDIO_THREAD_STACK_PUSH (marker);
		idio_vm_decode_thread (thr);
		idio_coding_error_C ("RETURN: pc not a number", fs_pc, IDIO_C_FUNC_LOCATION_S ("RETURN"));

		/* notreached */
		return 0;
	    }
	    idio_pc_t s_pc = IDIO_FIXNUM_VAL (fs_pc);

	    IDIO_IA_T s_bc = bc;
	    if (s_xi != xi) {
		s_bc = IDIO_XENV_BYTE_CODE (idio_xenvs[s_xi]);
	    }

	    if (s_pc > (ssize_t) IDIO_IA_USIZE (s_bc) ||
		s_pc < 0) {
		fprintf (stderr, "\n\nRETURN: to [%zu]@%zd?\n\n", s_xi, s_pc);
		idio_dump (thr, 1);
		idio_dump (IDIO_THREAD_STACK (thr), 1);
		idio_vm_decode_thread (thr);
		idio_vm_panic (thr, "RETURN: impossible PC on stack top");
	    }

	    IDIO_VM_RUN_DIS ("RETURN to [%zu]@%zu\n", s_xi, s_pc);
	    IDIO_THREAD_XI (thr) = s_xi;
	    IDIO_THREAD_PC (thr) = s_pc;

	    if (idio_vm_tracing_user &&
		idio_vm_tracing <= 1) {
		/* fprintf (stderr, "XXX RETURN to %zd: tracing depth <= 1!\n", s_pc); */
	    } else {
		idio_vm_tracing--;
	    }
	    if (idio_vm_tracing > 0 &&
		idio_vm_tracing < idio_vm_tracing_user) {
		/*
		 * %9d	- clock ns
		 * SPACE
		 * %6d	- PID
		 * SPACE
		 * %-11s	- [xi]@PC of ins
		 * SPACE
		 * %40s	- (lexical information == "")
		 * %.*s	- trace-depth indent
		 * %s	- value
		 */
		idio_vm_time_delta ();
		fprintf (idio_tracing_FILE, "%09ld ", idio_vm_ts_delta.tv_nsec);
		fprintf (idio_tracing_FILE, "%6d ", getpid ());

		char buf[51];
		snprintf (buf, 50, "[%zu]@%zd", IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr));
		buf[50] = '\0';
		fprintf (idio_tracing_FILE, "%-11s ", buf);

		fprintf (idio_tracing_FILE, "%40s", "");
		fprintf (idio_tracing_FILE, "%.*s  ", idio_vm_tracing, idio_vm_tracing_out);
		size_t size = 0;
		char *s = idio_report_string (IDIO_THREAD_VAL (thr), &size, 4, idio_S_nil, 1);
		fprintf (idio_tracing_FILE, "%s\n", s);
		IDIO_GC_FREE (s, size);
	    }
#ifdef IDIO_VM_PROF
	    idio_vm_clos_time (thr, "RETURN");
#endif
	}
	break;
    case IDIO_A_FINISH:
	{
	    /* invoke exit handler... */
	    IDIO_VM_RUN_DIS ("FINISH\n\n");

	    return 0;
	}
	break;
    case IDIO_A_PUSH_ABORT:
	{
	    uint64_t o = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("PUSH-ABORT to PC +%" PRIu64 "", o);

	    idio_vm_push_offset_abort (thr, o);
	    idio_command_suppress_rcse = idio_S_false;

	    if (idio_vm_tracing_user) {
		idio_vm_tracing = 1;
	    }
	}
	break;
    case IDIO_A_POP_ABORT:
	{
	    IDIO_VM_RUN_DIS ("POP-ABORT\n");

	    idio_vm_pop_abort (thr);

	    if (0 == idio_vm_tracing_all) {
		idio_vm_tracing = 0;
	    }
	}
	break;
    case IDIO_A_ALLOCATE_FRAME1:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 1");

	    /* no args, no need to pull an empty list ref */
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (1);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME2:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 2");

	    IDIO frame = idio_frame_allocate (2);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME3:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 3");

	    IDIO frame = idio_frame_allocate (3);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME4:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 4");

	    IDIO frame = idio_frame_allocate (4);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME5:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 5");

	    IDIO frame = idio_frame_allocate (5);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME:
	{
	    uint64_t i = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME %" PRIu64, i);

	    IDIO frame = idio_frame_allocate (i);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("ALLOCATE-DOTTED-FRAME %" PRIu64, arity);

	    IDIO vs = idio_frame_allocate (arity);
	    idio_frame_update (vs, 0, arity - 1, idio_S_nil);
	    IDIO_THREAD_VAL (thr) = vs;
	}
	break;
    case IDIO_A_REUSE_FRAME:
	{
	    uint64_t size = idio_vm_fetch_varuint (bc, thr);

	    IDIO frame = IDIO_THREAD_FRAME (thr);
	    /* fprintf (stderr, "REUSE %2d for %2" PRIu64 "\n", IDIO_FRAME_NPARAMS (frame), size); */
	    IDIO_VM_RUN_DIS ("REUSE-FRAME %" PRIu64, size);
	    if (size > (uint64_t) IDIO_FRAME_NALLOC (frame)) {
		IDIO_THREAD_VAL (thr) = idio_frame_allocate (size);
	    } else {
		/*
		 * XXX needs some thought -- there's interaction with
		 * UNLINK-FRAME which doesn't know we REUSED
		 */
		IDIO_THREAD_VAL (thr) = idio_frame_allocate (size);
		break;
	    }
	}
	break;
    case IDIO_A_POP_FRAME0:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 0");

	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 0, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME1:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 1");

	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 1, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME2:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 2");

	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 2, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME3:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 3");

	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 3, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME:
	{
	    uint64_t rank = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("POP-FRAME %" PRIu64, rank);

	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, rank, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_LINK_FRAME:
	{
	    uint64_t si = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("LINK-FRAME si=%" PRIu64, si);

	    IDIO frame = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != frame) {
		IDIO_FRAME_XI (frame) = xi;
		IDIO_FRAME_NAMES (frame) = idio_fixnum (si);
	    }

	    IDIO_THREAD_FRAME (thr) = idio_link_frame (IDIO_THREAD_FRAME (thr), frame);
	}
	break;
    case IDIO_A_UNLINK_FRAME:
	{
	    IDIO_VM_RUN_DIS ("UNLINK-FRAME");

	    IDIO_THREAD_FRAME (thr) = IDIO_FRAME_NEXT (IDIO_THREAD_FRAME (thr));
	}
	break;
    case IDIO_A_PACK_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("PACK-FRAME %" PRIu64, arity);

	    idio_vm_listify (IDIO_THREAD_VAL (thr), arity);
	}
	break;
    case IDIO_A_POP_LIST_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("POP-LIST-FRAME %" PRIu64, arity);

	    idio_frame_update (IDIO_THREAD_VAL (thr),
			       0,
			       arity,
			       idio_pair (IDIO_THREAD_STACK_POP (),
					  idio_frame_fetch (IDIO_THREAD_VAL (thr), 0, arity)));
	}
	break;
    case IDIO_A_EXTEND_FRAME:
	{
	    uint64_t alloc = idio_vm_fetch_varuint (bc, thr);
	    uint64_t si = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("EXTEND-FRAME %" PRIu64 " sci=%" PRIu64, alloc, si);

	    IDIO frame = IDIO_THREAD_FRAME (thr);
	    if (idio_S_nil != frame) {
		IDIO_FRAME_XI (frame) = xi;
		IDIO_FRAME_NAMES (frame) = idio_fixnum (si);
	    }

	    idio_extend_frame (frame, alloc);
	}
	break;
    case IDIO_A_ARITY1P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=1?");

	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if (1 != nargs) {
		/*
		 * Test Case: vm-errors/IDIO-A-ARITY1P-too-many.idio
		 *
		 * define (fn) #t
		 * fn 1
		 */
		idio_vm_error_arity (ins, thr, nargs - 1, 0, IDIO_C_FUNC_LOCATION_S ("ARITY1P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITY2P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=2?");

	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if (2 != nargs) {
		/*
		 * Test Cases:
		 *
		 *   vm-errors/IDIO-A-ARITY2P-too-few.idio
		 *   vm-errors/IDIO-A-ARITY2P-too-many.idio
		 *
		 * define (fn a) #t
		 * (fn)
		 * fn 1 2
		 */
		idio_vm_error_arity (ins, thr, nargs - 1, 1, IDIO_C_FUNC_LOCATION_S ("ARITY2P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITY3P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=3?");

	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if (3 != nargs) {
		/*
		 * Test Cases:
		 *
		 *   vm-errors/IDIO-A-ARITY3P-too-few.idio
		 *   vm-errors/IDIO-A-ARITY3P-too-many.idio
		 *
		 * define (fn a b) #t
		 * fn 1
		 * fn 1 2 3
		 */
		idio_vm_error_arity (ins, thr, nargs - 1, 2, IDIO_C_FUNC_LOCATION_S ("ARITY3P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITY4P:
	{
	    IDIO_VM_RUN_DIS ("ARITY=4?");

	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if (4 != nargs) {
		/*
		 * Test Cases:
		 *
		 *   vm-errors/IDIO-A-ARITY2P-too-few.idio
		 *   vm-errors/IDIO-A-ARITY2P-too-many.idio
		 *
		 * define (fn a b c) #t
		 * fn 1 2
		 * fn 1 2 3 4
		 */
		idio_vm_error_arity (ins, thr, nargs - 1, 3, IDIO_C_FUNC_LOCATION_S ("ARITY4P"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYEQP:
	{
	    uint64_t arityp1 = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("ARITY=? %" PRIu64, arityp1);

	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if ((idio_ai_t) arityp1 != nargs) {
		/*
		 * Test Cases:
		 *
		 *   vm-errors/IDIO-A-ARITYEQP-too-few.idio
		 *   vm-errors/IDIO-A-ARITYEQP-too-many.idio
		 *
		 * define (fn a b c d) #t
		 * fn 1 2 3
		 * fn 1 2 3 4 5
		 */
		idio_vm_error_arity (ins, thr, nargs - 1, arityp1 - 1, IDIO_C_FUNC_LOCATION_S ("ARITYEQP"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYGEP:
	{
	    uint64_t arityp1 = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("ARITY>=? %" PRIu64, arityp1);

	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if (nargs < (idio_ai_t) arityp1) {
		/*
		 * Test Cases:
		 *
		 *   vm-errors/IDIO-A-ARITY2P-too-few.idio
		 *
		 * define (fn a & b) #t
		 * (fn)
		 */
		idio_vm_error_arity_varargs (ins, thr, nargs - 1, arityp1 - 1, IDIO_C_FUNC_LOCATION_S ("ARITYGEP"));

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_CONSTANT_0:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 0");

	    IDIO_THREAD_VAL (thr) = idio_fixnum0;
	}
	break;
    case IDIO_A_CONSTANT_1:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 1");

	    IDIO_THREAD_VAL (thr) = idio_fixnum (1);
	}
	break;
    case IDIO_A_CONSTANT_2:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 2");

	    IDIO_THREAD_VAL (thr) = idio_fixnum (2);
	}
	break;
    case IDIO_A_CONSTANT_3:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 3");

	    IDIO_THREAD_VAL (thr) = idio_fixnum (3);
	}
	break;
    case IDIO_A_CONSTANT_4:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 4");

	    IDIO_THREAD_VAL (thr) = idio_fixnum (4);
	}
	break;
    case IDIO_A_FIXNUM:
	{
	    uint64_t v = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("FIXNUM %" PRIu64, v);

	    if (IDIO_FIXNUM_MAX < v) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("FIXNUM"), "FIXNUM OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = idio_fixnum ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_FIXNUM:
	{
	    int64_t v = idio_vm_fetch_varuint (bc, thr);

	    v = -v;

	    IDIO_VM_RUN_DIS ("NEG-FIXNUM %6" PRId64, v);

	    if (IDIO_FIXNUM_MIN > v) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("NEG-FIXNUM"), "FIXNUM OOB: %" PRId64 " < %" PRId64, v, IDIO_FIXNUM_MIN);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = idio_fixnum ((intptr_t) v);
	}
	break;
    case IDIO_A_CONSTANT:
	{
	    uint64_t v = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("CONSTANT %" PRIu64, v);

	    if (IDIO_FIXNUM_MAX < v) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT"), "CONSTANT OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT_IDIO ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_CONSTANT:
	{
	    int64_t v = idio_vm_fetch_varuint (bc, thr);

	    v = -v;

	    IDIO_VM_RUN_DIS ("NEG-CONSTANT %6" PRId64, v);

	    if (IDIO_FIXNUM_MIN > v) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("NEG-CONSTANT"), "CONSTANT OOB: %" PRId64 " < %" PRId64, v, IDIO_FIXNUM_MIN);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT_IDIO ((intptr_t) v);
	}
	break;
    case IDIO_A_UNICODE:
	{
	    uint64_t v = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("UNICODE %" PRIu64, v);

	    if (IDIO_FIXNUM_MAX < v) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("UNICODE"), "UNICODE OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_UNICODE ((intptr_t) v);
	}
	break;
    case IDIO_A_NOP:
	{
	    IDIO_VM_RUN_DIS ("NOP");
	}
	break;
    case IDIO_A_PRIMCALL0:
	{
	    uint64_t si = idio_vm_fetch_varuint (bc, thr);

	    IDIO pd = idio_vm_iref_val (thr, xi, si, "PRIMITIVE/0", IDIO_VM_IREF_VAL_UNDEF_FATAL);
	    IDIO_TYPE_ASSERT (primitive, pd);
	    /*
	     * XXX see comment in idio_meaning_primitive_application()
	     * for setting *func*
	     */
	    IDIO_THREAD_FUNC (thr) = pd;

	    IDIO_VM_RUN_DIS ("PRIMITIVE/0 .%-4" PRIu64 " %s", si, IDIO_PRIMITIVE_NAME (pd));

	    idio_vm_primitive_call_trace (pd, thr, 0);
#ifdef IDIO_VM_PROF
	    struct timespec prim_t0;
	    struct rusage prim_ru0;
	    idio_vm_func_start (pd, &prim_t0, &prim_ru0);
#endif
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (pd) ();
#ifdef IDIO_VM_PROF
	    struct timespec prim_te;
	    struct rusage prim_rue;
	    idio_vm_func_stop (pd, &prim_te, &prim_rue);
	    idio_vm_prim_time (pd, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif
	    idio_vm_primitive_result_trace (thr);
	}
	break;
    case IDIO_A_PRIMCALL1:
	{
	    uint64_t si = idio_vm_fetch_varuint (bc, thr);

	    IDIO pd = idio_vm_iref_val (thr, xi, si, "PRIMITIVE/1", IDIO_VM_IREF_VAL_UNDEF_FATAL);
	    IDIO_TYPE_ASSERT (primitive, pd);
	    /*
	     * XXX see comment in idio_meaning_primitive_application()
	     * for setting *func*
	     */
	    IDIO_THREAD_FUNC (thr) = pd;

	    IDIO_VM_RUN_DIS ("PRIMITIVE/1 .%-4" PRIu64 " %s", si, IDIO_PRIMITIVE_NAME (pd));

	    idio_vm_primitive_call_trace (pd, thr, 1);
#ifdef IDIO_VM_PROF
	    struct timespec prim_t0;
	    struct rusage prim_ru0;
	    idio_vm_func_start (pd, &prim_t0, &prim_ru0);
#endif
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (pd) (IDIO_THREAD_VAL (thr));
#ifdef IDIO_VM_PROF
	    struct timespec prim_te;
	    struct rusage prim_rue;
	    idio_vm_func_stop (pd, &prim_te, &prim_rue);
	    idio_vm_prim_time (pd, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif
	    idio_vm_primitive_result_trace (thr);
	}
	break;
    case IDIO_A_PRIMCALL2:
	{
	    uint64_t si = idio_vm_fetch_varuint (bc, thr);

	    IDIO pd = idio_vm_iref_val (thr, xi, si, "PRIMITIVE/2", IDIO_VM_IREF_VAL_UNDEF_FATAL);
	    IDIO_TYPE_ASSERT (primitive, pd);
	    /*
	     * XXX see comment in idio_meaning_primitive_application()
	     * for setting *func*
	     */
	    IDIO_THREAD_FUNC (thr) = pd;

	    IDIO_VM_RUN_DIS ("PRIMITIVE/2 .%-4" PRIu64 " %s", si, IDIO_PRIMITIVE_NAME (pd));

	    idio_vm_primitive_call_trace (pd, thr, 2);
#ifdef IDIO_VM_PROF
	    struct timespec prim_t0;
	    struct rusage prim_ru0;
	    idio_vm_func_start (pd, &prim_t0, &prim_ru0);
#endif
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (pd) (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
#ifdef IDIO_VM_PROF
	    struct timespec prim_te;
	    struct rusage prim_rue;
	    idio_vm_func_stop (pd, &prim_te, &prim_rue);
	    idio_vm_prim_time (pd, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif
	    idio_vm_primitive_result_trace (thr);
	}
	break;
    case IDIO_A_POP_RCSE:
	IDIO_VM_RUN_DIS ("POP-RCSE");

	idio_command_suppress_rcse = IDIO_THREAD_STACK_POP ();
	break;
    case IDIO_A_SUPPRESS_RCSE:
	IDIO_VM_RUN_DIS ("SUPPRESS-RCSE");

	IDIO_THREAD_STACK_PUSH (idio_command_suppress_rcse);
	idio_command_suppress_rcse = idio_S_true;
	break;
    case IDIO_A_NOT:
	IDIO_VM_RUN_DIS ("NOT");

	IDIO_THREAD_VAL (thr) = IDIO_THREAD_VAL (thr) == idio_S_false ? idio_S_true : idio_S_false;
	break;
    case IDIO_A_EXPANDER:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("EXPANDER .%-4" PRIu64 " ", si);

	    IDIO sym = idio_vm_symbols_ref (xi, si);
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
	    IDIO fgvi = idio_array_ref_index (vs, si);
	    idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

	    IDIO ce = idio_thread_current_env ();

	    if (0 == gvi) {
		IDIO si_ce = idio_module_find_symbol (sym, ce);

		if (idio_S_false == si_ce) {
		    /*
		    idio_debug ("EXPANDER: %-20s ", sym);
		    fprintf (stderr, ".%-4" PRIu64 " undefined?  setting...\n", si);
		    */

		    idio_as_t ci = idio_vm_constants_lookup_or_extend (xi, sym);
		    IDIO fci = idio_fixnum (ci);

		    IDIO fsi = idio_fixnum (si);
		    gvi = idio_vm_extend_values (0);
		    fgvi = idio_fixnum (gvi);
		    idio_vm_values_set (xi, si, fgvi);

		    si_ce = IDIO_LIST6 (idio_S_toplevel, fsi, fci, fgvi, ce, idio_vm_EXPANDER_string);
		    idio_module_set_symbol (sym, si_ce, ce);
		} else {
		    fgvi = IDIO_SI_VI (si_ce);
		    gvi = IDIO_FIXNUM_VAL (fgvi);
		}
	    }

	    IDIO_VM_RUN_DIS ("[0].%" PRIu64 " ", gvi);

	    IDIO val = IDIO_THREAD_VAL (thr);
	    idio_install_expander (xi, sym, val);
	    idio_module_set_symbol_value_xi (xi, sym, val, ce);
	}
	break;
    case IDIO_A_INFIX_OPERATOR:
	{
	    uint64_t oi = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t pri = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("INFIX-OPERATOR .%-4" PRIu64 " pri %4" PRIu64 " ", oi, pri);

	    IDIO sym = idio_vm_symbols_ref (xi, oi);

	    IDIO vt = IDIO_XENV_VT (idio_xenvs[xi]);
	    IDIO fgvi = idio_array_ref_index (vt, oi);
	    idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

	    if (0 == gvi) {
		IDIO op_si = idio_module_find_symbol (sym, idio_operator_module);

		if (idio_S_false == op_si) {
		    /*
		    idio_debug ("INFIX-OPERATOR: %-20s ", sym);
		    fprintf (stderr, ".%-4" PRIu64 " undefined?  setting...\n", oi);
		    */

		    idio_as_t ci = idio_vm_constants_lookup_or_extend (xi, sym);
		    IDIO fci = idio_fixnum (ci);

		    IDIO foi = idio_fixnum (oi);
		    gvi = idio_vm_extend_values (0);
		    fgvi = idio_fixnum (gvi);
		    idio_vm_values_set (xi, oi, fgvi);

		    op_si = IDIO_LIST6 (idio_S_toplevel, foi, fci, fgvi, idio_operator_module, idio_vm_INFIX_OPERATOR_string);
		    idio_module_set_symbol (sym, op_si, idio_operator_module);
		}

		fgvi = IDIO_SI_VI (op_si);
		gvi = IDIO_FIXNUM_VAL (fgvi);
	    }

	    IDIO_VM_RUN_DIS ("[0].%" PRIu64 " ", gvi);

	    IDIO val = IDIO_THREAD_VAL (thr);
	    idio_install_infix_operator (xi, sym, val, pri);
	}
	break;
    case IDIO_A_POSTFIX_OPERATOR:
	{
	    uint64_t oi = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t pri = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("POSTFIX-OPERATOR .%-4" PRIu64 " pri %4" PRIu64 " ", oi, pri);

	    IDIO sym = idio_vm_symbols_ref (xi, oi);

	    IDIO vt = IDIO_XENV_VT (idio_xenvs[xi]);
	    IDIO fgvi = idio_array_ref_index (vt, oi);
	    idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

	    if (0 == gvi) {
		IDIO op_si = idio_module_find_symbol (sym, idio_operator_module);

		if (idio_S_false == op_si) {
		    /*
		    idio_debug ("POSTFIX-OPERATOR: %-20s ", sym);
		    fprintf (stderr, ".%-4" PRIu64 " undefined?  setting...\n", oi);
		    */

		    idio_as_t ci = idio_vm_constants_lookup_or_extend (xi, sym);
		    IDIO fci = idio_fixnum (ci);

		    IDIO foi = idio_fixnum (oi);
		    gvi = idio_vm_extend_values (0);
		    fgvi = idio_fixnum (gvi);
		    idio_vm_values_set (xi, oi, fgvi);

		    op_si = IDIO_LIST6 (idio_S_toplevel, foi, fci, fgvi, idio_operator_module, idio_vm_POSTFIX_OPERATOR_string);
		    idio_module_set_symbol (sym, op_si, idio_operator_module);
		}

		fgvi = IDIO_SI_VI (op_si);
		gvi = IDIO_FIXNUM_VAL (fgvi);
	    }

	    IDIO_VM_RUN_DIS ("[0].%" PRIu64 " ", gvi);

	    IDIO val = IDIO_THREAD_VAL (thr);
	    idio_install_postfix_operator (xi, sym, val, pri);
	}
	break;
    case IDIO_A_PUSH_DYNAMIC:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("PUSH-DYNAMIC %" PRIu64 " ", si);

	    idio_ai_t gvi = idio_vm_iref (thr,
					  xi,
					  si,
					  "PUSH-DYNAMIC",
					  idio_vm_PUSH_DYNAMIC_string,
					  IDIO_VM_IREF_MDR_UNDEF_FATAL);

	    idio_vm_push_dynamic (thr, gvi, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_DYNAMIC:
	{
	    IDIO_VM_RUN_DIS ("POP-DYNAMIC");

	    idio_vm_pop_dynamic (thr);
	}
	break;
    case IDIO_A_DYNAMIC_SYM_REF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    idio_ai_t gvi = idio_vm_iref (thr,
					  xi,
					  si,
					  "DYNAMIC-SYM-REF",
					  idio_vm_DYNAMIC_SYM_REF_string,
					  IDIO_VM_IREF_MDR_UNDEF_FATAL);

	    IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (thr, si, gvi, idio_S_nil);
	}
	break;
    case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    idio_ai_t gvi = idio_vm_iref (thr,
					  xi,
					  si,
					  "DYNAMIC-FUNCTION-SYM-REF",
					  idio_vm_DYNAMIC_FUNCTION_SYM_REF_string,
					  IDIO_VM_IREF_MDR_UNDEF_FATAL);

	    IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (thr, si, gvi, idio_S_nil);
	}
	break;
    case IDIO_A_PUSH_ENVIRON:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("PUSH-ENVIRON %" PRIu64 " ", si);

	    idio_ai_t gvi = idio_vm_iref (thr,
					  xi,
					  si,
					  "PUSH-ENVIRON",
					  idio_vm_PUSH_ENVIRON_string,
					  IDIO_VM_IREF_MDR_UNDEF_FATAL);

	    idio_vm_push_environ (thr, gvi, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_ENVIRON:
	{
	    IDIO_VM_RUN_DIS ("POP-ENVIRON");

	    idio_vm_pop_environ (thr);
	}
	break;
    case IDIO_A_ENVIRON_SYM_REF:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    idio_ai_t gvi = idio_vm_iref (thr,
					  xi,
					  si,
					  "ENVIRON-SYM-REF",
					  idio_vm_ENVIRON_SYM_REF_string,
					  IDIO_VM_IREF_MDR_UNDEF_FATAL);

	    IDIO_THREAD_VAL (thr) = idio_vm_environ_ref (thr, si, gvi, idio_S_nil);
	}
	break;
    case IDIO_A_NON_CONT_ERR:
	{
	    IDIO_VM_RUN_DIS ("NON-CONT-ERROR\n");

	    /*
	     * As the NON-CONT-ERROR handler we'll go back to the
	     * first ABORT, which should be ABORT to main
	     */
	    idio_sp_t asp = idio_vm_find_abort_1 (thr);

	    if (asp) {
		IDIO stack = IDIO_THREAD_STACK (thr);
#ifdef IDIO_DEBUG
		fprintf (stderr, "NON-CONT-ERR: ABORT stack from %zd to %zd\n", idio_array_size (stack), asp + 1);
#endif
		IDIO krun = idio_array_ref_index (stack, asp - 1);
		IDIO_ARRAY_USIZE (stack) = asp + 1;
		idio_vm_thread_state (thr);

		idio_exit_status = 1;
		if (idio_isa_pair (krun)) {
		    fprintf (stderr, "NON-CONT-ERR: restoring ABORT continuation #1: ");
		    idio_debug ("%s\n", IDIO_PAIR_HT (krun));
		    idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

		    /* notreached */
		    return 0;
		}
	    }

	    fprintf (stderr, "NON-CONT-ERROR: nothing to restore\n");
	    idio_vm_panic (thr, "NON-CONT-ERROR");

	    /* notreached */
	    return 0;
	}
	break;
    case IDIO_A_PUSH_TRAP:
	{
	    uint64_t si = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("PUSH-TRAP %" PRIu64 " ", si);

	    /*
	     * Annoyingly, if anything in the codebase tries to access
	     * {si} in this execution environment (eg. it tries to
	     * raise this condition in a test) they will (probably)
	     * set its value to be something.  Which is of no use to
	     * us as we want the gci for this symbol, ie. the global
	     * constant index.
	     *
	     * We'll have to look it up each time.
	     */
	    IDIO st = IDIO_XENV_ST (idio_xenvs[xi]);
	    IDIO fci = idio_array_ref_index (st, si);
	    idio_as_t ci = IDIO_FIXNUM_VAL (fci);

	    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);
	    IDIO sym = idio_array_ref_index (cs, ci);

	    idio_ai_t gci = idio_vm_constants_lookup_or_extend (0, sym);
	    IDIO fgci = idio_fixnum (gci);

	    IDIO_VM_RUN_DIS (" %" PRIu64 " ", gci);

	    idio_vm_push_trap (thr, IDIO_THREAD_VAL (thr), fgci, 0);
	}
	break;
    case IDIO_A_POP_TRAP:
	{
	    IDIO_VM_RUN_DIS ("POP-TRAP");

	    idio_vm_pop_trap (thr);
	}
	break;
    case IDIO_A_PUSH_ESCAPER:
	{
	    uint64_t gci = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t offset = idio_vm_fetch_varuint (bc, thr);

	    IDIO_VM_RUN_DIS ("PUSH-ESCAPER %" PRIu64, gci);

	    idio_vm_push_escaper (thr, idio_fixnum (gci), offset);
	}
	break;
    case IDIO_A_POP_ESCAPER:
	{
	    IDIO_VM_RUN_DIS ("POP-ESCAPER");

	    idio_vm_pop_escaper (thr);
	}
	break;
    case IDIO_A_ESCAPER_LABEL_REF:
	{
	    uint64_t ci = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("ESCAPER-LABEL_REF %" PRIu64, ci);

	    idio_vm_escaper_label_ref (thr, idio_fixnum (ci));
	}
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    idio_pc_t pc = IDIO_THREAD_PC (thr);
	    idio_pc_t pcm = pc + 10;
	    pc = pc - 10;
	    if (pc < 0) {
		pc = 0;
	    }
	    if (pc % 10) {
		idio_pc_t pc1 = pc - (pc % 10);
		fprintf (stderr, "\n  %5zd ", pc1);
		for (; pc1 < pc; pc1++) {
		    fprintf (stderr, "    ");
		}
	    }
	    for (; pc < pcm; pc++) {
		if (0 == (pc % 10)) {
		    fprintf (stderr, "\n  %5zd ", pc);
		}
		fprintf (stderr, "%3d ", IDIO_IA_AE (bc, pc));
	    }
	    fprintf (stderr, "\n");
	    fprintf (stderr, "unexpected instruction: %3d [%zu]@%zd\n", ins, IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr) - 1);
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected instruction: %3d [%zu]@%" PRIu64 "\n", ins, IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr) - 1);

	    /* notreached */
	    return 0;
	}
	break;
    }

#ifdef IDIO_VM_PROF
    /*
     * We updated the ins timers for FUNCTION-* because they call
     * idio_vm_invoke() which "may take some time".
     */
    switch (ins) {
    case IDIO_A_FUNCTION_INVOKE:
    case IDIO_A_FUNCTION_GOTO:
	break;
    default:
	idio_vm_update_ins_time (ins, ins_t0);
	break;
    }
#endif

    IDIO_VM_RUN_DIS ("\n");
    return 1;
}

void idio_vm_thread_init (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_sp_t sp = idio_array_size (IDIO_THREAD_STACK (thr));

    idio_sp_t tsp = idio_vm_find_stack_marker (IDIO_THREAD_STACK (thr), idio_SM_trap, 0, 0);
    IDIO_C_ASSERT (tsp <= sp);

    if (tsp < 1) {
	/*
	 * Special case.  We can't call the generic idio_vm_push_trap
	 * as that assumes a sensible TRAP_SP to be pushed on the
	 * stack first.  We don't have that yet.
	 *
	 * In the meanwhile, the manual result of the stack will be
	 *
	 * #[ ... (sp)NEXT-TRAP-SP CONDITION-TYPE HANDLER MARK-push-trap ]
	 *
	 * where, as this is the fallback handler, NEXT-TRAP-SP points
	 * at MARK-push-trap, ie sp+3.
	 *
	 * The CONDITION-TYPE for the fallback handler is ^condition
	 * (the base type for all other conditions).
	 *
	 * Not forgetting to set the actual TRAP_SP to sp+3 as well!
	 */
	IDIO_THREAD_STACK_PUSH (idio_fixnum (sp + 3));
	IDIO_THREAD_STACK_PUSH (idio_condition_condition_type_gci);
	IDIO_THREAD_STACK_PUSH (idio_condition_reset_condition_handler);
	IDIO_THREAD_STACK_PUSH (idio_SM_trap);
    }

    idio_vm_push_trap (thr, idio_condition_restart_condition_handler, idio_condition_condition_type_gci, 0);
    idio_vm_push_trap (thr, idio_condition_default_condition_handler, idio_condition_condition_type_gci, 0);
    IDIO fgci = idio_fixnum (idio_vm_constants_lookup (0, IDIO_SYMBOL (IDIO_CONDITION_RCSE_TYPE_NAME)));
    idio_vm_push_trap (thr, idio_condition_default_rcse_handler, fgci, 0);
    fgci = idio_fixnum (idio_vm_constants_lookup (0, IDIO_SYMBOL (IDIO_CONDITION_RACSE_TYPE_NAME)));
    idio_vm_push_trap (thr, idio_condition_default_racse_handler, fgci, 0);
    fgci = idio_fixnum (idio_vm_constants_lookup (0, IDIO_SYMBOL (IDIO_CONDITION_RT_SIGCHLD_TYPE_NAME)));
    idio_vm_push_trap (thr, idio_condition_default_SIGCHLD_handler, fgci, 0);
}

void idio_vm_default_pc (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * The problem for an external user, eg. idio_meaning_expander, is
     * that if the expander is a primitive then idio_vm_run() pushes
     * idio_vm_FINISH_pc on the stack expecting the code to run through
     * to the NOP/RETURN it added on the end.  However, for a
     * primitive idio_vm_invoke() will simply do it's thing without
     * changing the PC.  Which will not be about to walk into
     * NOP/RETURN.
     *
     * So we need to preset the PC to be ready to walk into
     * NOP/RETURN.
     *
     * If we put on real code the idio_vm_invoke will set PC after
     * this.
     */
    IDIO_THREAD_PC (thr) = idio_vm_RETURN_pc;
}

void idio_vm_sa_signal (int signum)
{
    idio_vm_signal_record[signum] = 1;
}

void idio_vm_signal_report ()
{
    int printed = 0;
    int signum;
    for (signum = IDIO_LIBC_FSIG; signum <= IDIO_LIBC_NSIG; signum++) {
	if (idio_vm_signal_record[signum]) {
	    if (printed) {
		fprintf (stderr, " ");
	    } else {
		fprintf (stderr, "Pending signals: ");
	    }
	    fprintf (stderr, "%s", idio_libc_signal_name (signum));
	    printed = 1;
	}
    }
    if (printed) {
	fprintf (stderr, "\n");
    }
}

static uintptr_t idio_vm_run_loops = 0;

IDIO idio_vm_run (IDIO thr, idio_xi_t xi, idio_pc_t pc, idio_vm_run_enum caller)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (pc);

    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * https://stackoverflow.com/questions/7996825/why-volatile-works-for-setjmp-longjmp
     *
     * siglongjmp() clobbers registers so "save" or flag important
     * automatic variables in *this* function in/as a volatile before
     * the sigsetjmp(), to be restored afterwards
     *
     * Obviously this only affects variables that can be stored in
     * registers (numbers, pointers) although there's no way of
     * knowing which have been stored in a register.
     *
     * Most of these variables are (effectively) single use archives
     * so simply mark the variable as volatile and no need to restore
     * to an efficient automatic/register variable.
     */
    IDIO_v v_thr = thr;

    char xs[30];		/* %zu is ~20 characters */
    snprintf (xs, 30, "[%zu]", xi);
    char sss[30];
    snprintf (sss, 30, "{%zu}", idio_array_size (IDIO_THREAD_STACK (thr)));
    IDIO_VM_RUN_DIS ("              #%-2d%4s@%-6zd%6s ",  IDIO_THREAD_FLAGS (thr), xs, pc, sss);

    IDIO_VM_RUN_DIS (" --- CALLED from %s\n", caller ? "Idio" : "C");

    IDIO_THREAD_XI (thr) = xi;
    IDIO_THREAD_PC (thr) = pc;
    volatile idio_xi_t v_XI0 = IDIO_THREAD_XI (thr);
    volatile idio_pc_t v_PC0 = IDIO_THREAD_PC (thr);
    volatile idio_sp_t v_ss0 = idio_array_size (IDIO_THREAD_STACK (thr));

    if (IDIO_VM_RUN_C == caller) {
	/*
	 * make sure this segment returns to idio_vm_FINISH_pc
	 */
	IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
	IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_XI (thr)));
	IDIO_THREAD_STACK_PUSH (idio_SM_return);
    }

#ifdef IDIO_DEBUG
    struct timeval t0;
    if (gettimeofday (&t0, NULL) == -1) {
	perror ("gettimeofday");
    }
    volatile uintptr_t v_loops0 = idio_vm_run_loops;
#endif

    volatile int v_gc_pause = idio_gc_get_pause ("idio_vm_run");

    /*
     * As sigjmp_buf is storing the registers I'm guessing it is too
     * big to be stored in a register itself.
     *
     * Don't quote me on that, though.
     */
    sigjmp_buf osjb;
    memcpy (osjb, IDIO_THREAD_JMP_BUF (thr), sizeof (sigjmp_buf));

    /*
     * Ready ourselves for idio_raise_condition/continuations to
     * clear the decks.
     */

    /*
     * Hmm, we really should consider caring whether we got here from
     * a siglongjmp...shouldn't we?
     *
     * I'm not sure we do care.
     */
    switch (sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1)) {
    case 0:
	break;
    case IDIO_VM_SIGLONGJMP_CONDITION:
	idio_gc_reset ("idio_vm_run/condition", v_gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_CONTINUATION:
	idio_gc_reset ("idio_vm_run/continuation", v_gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_CALLCC:
	idio_gc_reset ("idio_vm_run/callcc", v_gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_EVENT:
	idio_gc_reset ("idio_vm_run/event", v_gc_pause);
	break;
    case IDIO_VM_SIGLONGJMP_EXIT:
	fprintf (stderr, "NOTICE: idio_vm_run/exit (%d) for PID %d\n", idio_exit_status, getpid ());
	idio_gc_reset ("idio_vm_run/exit", v_gc_pause);
	idio_final ();
	exit (idio_exit_status);
	break;
    default:
	fprintf (stderr, "sigsetjmp: unexpected value\n");
	IDIO_C_ASSERT (0);
	break;
    }

    /*
     * This is where the problems arise post-siglongjmp().
     */
    if (!idio_isa_thread (v_thr)) {
	fprintf (stderr, "\n\n\nrun: v_thr corrupt:\n");
	fprintf (stderr, "thr      = %s\n", idio_type2string (v_thr));
	fprintf (stderr, "v_thr    = %s\n", idio_type2string (v_thr));
	idio_debug ("curr thr = %s\n", idio_thread_current_thread ());
	abort ();

	/*
	 * (current-thread) won't be correct but it'll allow us to
	 * exit "cleanly" -- rather than a core dump
	 */
	thr = idio_thread_current_thread ();
	idio_final ();
	exit (3);
    }
    thr = v_thr;

    /*
     * Finally, run the VM code with idio_vm_run1(), one instruction
     * at a time.
     *
     * Every so often we'll poke the GC to tidy up.  It has its own
     * view on whether it is time and/or safe to garbage collect but
     * we need to poke it to find out.
     *
     * It's also a good time to react to any asynchronous events,
     * signals. etc..
     *
     *
     * NB. Perhaps counter-inuitively, not running the GC slows things
     * down dramatically as the Unix process struggles to jump around
     * in masses of virtual memory.  What you really want to be doing
     * is constantly trimming the deadwood -- but only when you've
     * done enough work to generate some deadwood.
     *
     * That said, at the opposite extreme, calling the GC every time
     * round the loop is equally slow as you waste CPU cycles running
     * over the (same) allocated memory to little effect.
     *
     * Often enough, then, but not too often.
     *
     * You can experiment by changing the mask, Oxff, from smallest
     * (0x0) to large (0xffffffff).  The test suite has enough
     * happening to show the effect of using a large value -- virtual
     * memory usage should reach 1+GB.
     *
     * Setting it very small is a good way to find out if you have
     * successfully protected all your values in C-land.  Random SEGVs
     * occur if you haven't.
     */
    for (;;) {
	if (idio_vm_run1 (thr)) {

	    /*
	     * Has anything interesting happened of late while we were
	     * busy doing other things?
	     */
	    int signum;
	    for (signum = IDIO_LIBC_FSIG; signum <= IDIO_LIBC_NSIG; signum++) {
		if (idio_vm_signal_record[signum]) {
		    idio_vm_signal_record[signum] = 0;

		    IDIO signal_condition = idio_array_ref_index (idio_vm_signal_handler_conditions, (idio_ai_t) signum);
		    if (idio_S_nil != signal_condition) {
			if (idio_vm_tracing_user) {
			    struct timespec ts;
			    if (clock_gettime (CLOCK_MONOTONIC, &ts) < 0) {
				perror ("clock_gettime (CLOCK_MONOTONIC, ts)");
			    }

			    struct timespec td;
			    td.tv_sec = ts.tv_sec - idio_vm_ts0.tv_sec;
			    td.tv_nsec = ts.tv_nsec - idio_vm_ts0.tv_nsec;
			    if (td.tv_nsec < 0) {
				td.tv_nsec += IDIO_VM_NS;
				ts.tv_sec -= 1;
			    }
			    /*
			     * Printing time_t portably?  Technically,
			     * time_t is an arithmetic type which
			     * could be signed, unsigned or a floating
			     * point type.
			     */
			    fprintf (idio_tracing_FILE, "SIGNAL +%" PRIdMAX ".%09ld %s/%d -> condition handler\n", (intmax_t) td.tv_sec, td.tv_nsec, idio_libc_signal_name (signum), signum);
			}
			idio_vm_raise_condition (idio_S_true, signal_condition, 1, 1);

			return idio_S_notreached;
		    } else {
			/*
			 * Test Case: ??
			 *
			 * XXX needs revisiting
			 */
			fprintf (stderr, "iv-run: signal %d has no condition?\n", signum);
			idio_coding_error_C ("signal without a condition to raise", idio_fixnum (signum), IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }

		    IDIO signal_handler_name = idio_array_ref (idio_vm_signal_handler_name, idio_fixnum (signum));
		    if (idio_S_nil == signal_handler_name) {
			fprintf (stderr, "iv-run: raising signal %d: no handler name\n", signum);
			idio_debug ("iv-run: sig-handler-name %s\n", idio_vm_signal_handler_name);
			IDIO_C_ASSERT (0);
		    }
		    IDIO signal_handler_exists = idio_module_find_symbol_recurse (signal_handler_name, idio_Idio_module, 1);
		    IDIO idio_vm_signal_handler = idio_S_nil;
		    if (idio_S_false != signal_handler_exists) {
			idio_vm_signal_handler = idio_module_symbol_value_recurse (signal_handler_name, idio_Idio_module, idio_S_nil);
		    }

		    if (idio_S_nil != idio_vm_signal_handler) {
			/*
			 * We're about to call an event handler which
			 * could be either a primitive or a closure.
			 *
			 * Either way, we are in the middle of some
			 * sequence of instructions -- we are *not* at a
			 * safe point, eg. in between two lines of source
			 * code (assuming such a point would itself be
			 * "safe").  So we need to preserve all state on
			 * the stack.
			 *
			 * Including the current PC.  We'll replace it
			 * with idio_vm_IHR_pc which knows how to unpick
			 * what we've just pushed onto the stack and
			 * RETURN to the current PC.
			 *
			 * In case the handler is a closure, we need to
			 * create an empty argument frame in
			 * IDIO_THREAD_VAL(thr).
			 *
			 * If the handler is a primitive then it'll run
			 * through to completion and we'll immediately
			 * start running idio_vm_IHR_pc to get us back to
			 * where we interrupted.  All good.
			 *
			 * If it is a closure then we need to invoke it
			 * such that it will return back to the current
			 * PC, idio_vm_IHR_pc.  It we must invoke it as a
			 * regular call, ie. not in tail position.
			 */

#ifdef IDIO_DEBUG
			fprintf (stderr, "iv-run: handling signum %d\n", signum);
#endif
			IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
			IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_XI (thr)));
			IDIO_THREAD_STACK_PUSH (idio_SM_return);

			idio_vm_preserve_all_state (thr);

			/*
			 * Duplicate the existing top-most trap to
			 * have something to pop off
			 */
			IDIO stack = IDIO_THREAD_STACK (thr);
			idio_sp_t next_tsp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0);
			idio_vm_push_trap (thr,
					   idio_array_ref_index (stack, next_tsp - 1),
					   idio_array_ref_index (stack, next_tsp - 2),
					   IDIO_FIXNUM_VAL (idio_array_ref_index (stack, next_tsp - 3)));

			IDIO_THREAD_PC (thr) = idio_vm_IHR_pc; /* => (POP-TRAP) RESTORE-ALL-STATE, RETURN */

			/* one arg, signum */
			IDIO vs = idio_frame_allocate (2);
			idio_frame_update (vs, 0, 0, idio_fixnum (signum));

			IDIO_THREAD_VAL (thr) = vs;
			idio_vm_invoke (thr, idio_vm_signal_handler, IDIO_VM_INVOKE_REGULAR_CALL);

			siglongjmp (IDIO_THREAD_JMP_BUF (thr), IDIO_VM_SIGLONGJMP_EVENT);

			/* notreached */
			IDIO_C_ASSERT (0);
		    } else {
			idio_debug ("iv-run: signal_handler_name=%s\n", signal_handler_name);
			idio_debug ("iv-run: idio_vm_signal_handler_name=%s\n", idio_vm_signal_handler_name);
			idio_debug ("iv-run: idio_vm_signal_handler_name[17]=%s\n", idio_array_ref (idio_vm_signal_handler_name, idio_fixnum (SIGCHLD)));
			fprintf (stderr, "iv-run: no sighandler for signal #%d\n", signum);
		    }
		}
	    }

	    if ((++idio_vm_run_loops & 0xff) == 0) {
		idio_gc_possibly_collect ();
	    }
	} else {
	    break;
	}
    }

    /*
     * XXX I just don't like this -- but it works.
     */
    memcpy (IDIO_THREAD_JMP_BUF (thr), osjb, sizeof (sigjmp_buf));

#ifdef IDIO_DEBUG
    struct timeval tr;
    if (gettimeofday (&tr, NULL) == -1) {
	perror ("gettimeofday");
    }

    struct timeval td;
    td.tv_sec = tr.tv_sec - t0.tv_sec;
    td.tv_usec = tr.tv_usec - t0.tv_usec;

    if (td.tv_usec < 0) {
	td.tv_usec += 1000000;
	td.tv_sec -= 1;
    }

    /*
     * If we've taken long enough and done enough then record OPs/ms
     *
     * test.idio	=> ~1750/ms (~40/ms under valgrind)
     * counter.idio	=> ~6000/ms (~10-15k/ms in a lean build)
     */
    uintptr_t loops = (idio_vm_run_loops - v_loops0);
    if (loops > 500000 &&
	(td.tv_sec ||
	 td.tv_usec > 500000)) {
	uintptr_t ipms = loops / (td.tv_sec * 1000 + td.tv_usec / 1000);
	FILE *fh = stderr;
#ifdef IDIO_VM_PROF
	fh = idio_vm_perf_FILE;
#endif
	fprintf (fh, "[%d]vm_run: %10" PRIdPTR " ins in time %4jd.%03ld => %6" PRIdPTR " i/ms\n", getpid(), loops, (intmax_t) td.tv_sec, (long) td.tv_usec / 1000, ipms);
	if (td.tv_sec > 10) {
	    fprintf (fh, "[%d>%d] %jds: slow call to [%zu}@%zd\n", getppid (), getpid (), (intmax_t) td.tv_sec, v_XI0, v_PC0);
	}
    }
#endif

    IDIO r = IDIO_THREAD_VAL (thr);

    if (idio_vm_exit) {
	fprintf (stderr, "vm-run/exit (%d)\n", idio_exit_status);
	idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

	return idio_S_notreached;
    }

    /*
     * Check we are where we think we should be...wherever that is!
     *
     * There's an element of having fallen down the rabbit hole here
     * so we do what we can.  We shouldn't be anywhere other than one
     * beyond idio_vm_FINISH_pc having successfully run through the code
     * we were passed and we shouldn't have left the stack in a mess.
     *
     * XXX except if a handler went off from a signal handler...
     */
    if (IDIO_VM_RUN_C == caller) {
	int bail = 0;

	if (IDIO_THREAD_XI (thr) != v_XI0 ||
	    IDIO_THREAD_PC (thr) != (idio_vm_FINISH_pc + 1)) {
	    fprintf (stderr, "vm-run: THREAD #%d [%zu]@%zd failed to run to FINISH [%zu]@%zd\n",
		     IDIO_THREAD_FLAGS (thr),
		     IDIO_THREAD_XI (thr),
		     IDIO_THREAD_PC (thr),
		     v_XI0,
		     (idio_vm_FINISH_pc + 1));
	    bail = 1;
	}

	idio_sp_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

	if (ss != v_ss0) {
	    fprintf (stderr, "vm-run: THREAD #%d [%zu]@%zd failed to consume stack: SP0 %zd -> %zd\n",
		     IDIO_THREAD_FLAGS (thr),
		     v_XI0,
		     v_PC0,
		     v_ss0 - 1,
		     ss - 1);
	    idio_vm_decode_thread (thr);
	    if (ss < v_ss0) {
		fprintf (stderr, "\n\nNOTICE: current stack smaller than when we started\n");
	    }
	    bail = 1;
	}

	if (bail) {
	    /*
	     * As a mitigation, if interactive we'll go back to ABORT
	     * #2, the most recent code-set top-level ABORT.
	     * Otherwise #1.
	     */
	    int abort_index = 0;
	    IDIO thr = idio_thread_current_thread ();
	    idio_sp_t asp = -1;
	    if (idio_job_control_interactive) {
		asp = idio_vm_find_abort_2 (thr);
		abort_index = 2;
	    } else {
		asp = idio_vm_find_abort_1 (thr);
		abort_index = 1;
	    }

	    if (asp) {
		IDIO stack = IDIO_THREAD_STACK (thr);
#ifdef IDIO_DEBUG
		fprintf (stderr, "vm-run: bail: ABORT stack from %zd to %zd\n", idio_array_size (stack), asp + 1);
#endif
		IDIO krun = idio_array_ref_index (stack, asp - 1);
		IDIO_ARRAY_USIZE (stack) = asp + 1;
		idio_vm_thread_state (thr);

		idio_exit_status = 1;
		if (idio_isa_pair (krun)) {
		    fprintf (stderr, "vm-run: bail: restoring ABORT #%d: ", abort_index);
		    idio_debug ("%s\n", IDIO_PAIR_HT (krun));
		    idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

		    return idio_S_notreached;
		}
	    }

	    fprintf (stderr, "vm-run/bail: nothing to restore => exit (1)\n");
	    idio_exit_status = 1;
	    idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

	    return idio_S_notreached;
	}
    }

    return r;
}

IDIO idio_vm_run_C (IDIO thr, idio_xi_t xi, idio_pc_t pc)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (pc);

    IDIO_TYPE_ASSERT (thread, thr);

    return idio_vm_run (thr, xi, pc, IDIO_VM_RUN_C);
}

IDIO_DEFINE_PRIMITIVE0V_DS ("vm-run", vm_run, (IDIO args), "[xi [PC]]", "\
run code at `PC` in xenv `xi`			\n\
						\n\
:param xi: execution environment to use, defaults to current xi	\n\
:type xi: fixnum or thread, optional		\n\
:param PC: PC to use, defaults to current PC	\n\
:type PC: fixnum, optional			\n\
:return: *val* register				\n\
")
{
    IDIO_ASSERT (args);

    IDIO thr = idio_thread_current_thread ();
    idio_xi_t xi0 = IDIO_THREAD_XI (thr);
    idio_pc_t PC0 = IDIO_THREAD_PC (thr);

    /*
     * Test Case(s):
     *
     *   vm-errors/vm-run-bad-xi-value-{1,2}.idio
     *   vm-errors/vm-run-bad-xi-type.idio
     *
     * vm-run -1 #t
     * vm-run 98765 #t
     * vm-run #t #t
     */

    idio_xi_t C_xi = 0;
    if (idio_isa_pair (args)) {
	IDIO xi = IDIO_PAIR_H (args);
	args = IDIO_PAIR_T (args);

	if (idio_isa_fixnum (xi)) {
	    intptr_t v = IDIO_FIXNUM_VAL (xi);

	    if (v < 0) {
		idio_error_param_value_msg ("vm-run", "xi", xi, "should be non-negative", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else if (v >= (intptr_t) idio_xenvs_size) {
		idio_error_param_value_msg ("vm-run", "xi", xi, "is too large", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    C_xi = v;
	} else if (idio_isa_thread (xi)) {
	    C_xi = IDIO_THREAD_XI (xi);
	} else {
	    idio_error_param_type ("fixnum|thread", xi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    /*
     * Test Case(s):
     *
     *   vm-errors/vm-run-bad-PC-value-{1,2}.idio
     *   vm-errors/vm-run-bad-PC-type.idio
     *
     * vm-run (threading/current-thread) -1
     * vm-run (threading/current-thread) 536870911 ; FIXNUM-MAX on 32-bit
     * vm-run (threading/current-thread) #t
     */
    idio_pc_t pc = PC0;
    if (idio_isa_pair (args)) {
	IDIO PC = IDIO_PAIR_H (args);
	IDIO_USER_TYPE_ASSERT (fixnum, PC);

	pc = IDIO_FIXNUM_VAL (PC);

	if (pc < 0) {
	    idio_error_param_value_msg ("vm-run", "PC", PC, "should be non-negative", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else if (pc >= IDIO_IA_USIZE (IDIO_XENV_BYTE_CODE (idio_xenvs[C_xi]))) {
	    idio_error_param_value_msg ("vm-run", "PC", PC, "is too large", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    /*
     * We've been called from Idio-land to start running some code --
     * usually that which has just been generated by the {codegen}
     * primitive.
     *
     * This is ostensibly a function call so push the current XI/PC on
     * the stack, right?
     *
     * Not quite, this call to a primitive was already inside
     * idio_vm_run() so if we set, say, the current XI/PC on the stack
     * to be returned to and call idio_vm_run() then it will
     *
     * 1. run this new bit of code which will
     *
     * 2. RETURN to the XI/PC on the stack (the caller of this
     *    primitive)
     *
     * 3. continue from there
     *
     * Wait!  We never came back *here* to return the value to the
     * caller of this primitive.
     *
     * As it happens, our caller *will* have received the correct
     * value as part of the above loop but careful analysis (read:
     * printf()) shows that we *enter* this primitive any number of
     * times but only ever *leave* it when X (for some X) happens.
     *
     * At which point things don't line up any more.
     *
     * Instead, set the RETURN jump to idio_vm_FINISH_pc to cause
     * whatever we intend to have run actually stop the idio_vm_run()
     * loop and return a value to us.
     *
     * However, before we return to our caller we must set the XI/PC
     * back to the original XI/PC we saw when we were called in order
     * that our caller can continue.
     *
     * NB.  If you don't do that final part you may get an obscure
     * "PANIC: restore-trap" failure.  That's because the PC *after*
     * idio_vm_FINISH_pc is the instruction in the prologue for, er,
     * restoring a trap.  It's not meant to be called next!
     */

    IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
    IDIO_THREAD_STACK_PUSH (idio_fixnum (xi0));
    IDIO_THREAD_STACK_PUSH (idio_SM_return);

    IDIO r = idio_vm_run (thr, C_xi, pc, IDIO_VM_RUN_IDIO);

    IDIO_THREAD_XI (thr) = xi0;
    IDIO_THREAD_PC (thr) = PC0;

    return r;
}

IDIO idio_vm_symbols_ref (idio_xi_t xi, idio_as_t si)
{
    IDIO st = IDIO_XENV_ST (idio_xenvs[xi]);

    IDIO fci = idio_array_ref_index (st, si);
    IDIO_TYPE_ASSERT (fixnum, fci);

    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);

    return idio_array_ref_index (cs, IDIO_FIXNUM_VAL (fci));
}

void idio_vm_symbols_set (idio_xi_t xi, idio_as_t si, IDIO ci)
{
    IDIO_ASSERT (ci);

    IDIO_TYPE_ASSERT (fixnum, ci);

    IDIO st = IDIO_XENV_ST (idio_xenvs[xi]);
    idio_array_insert_index (st, ci, si);
}

void idio_vm_dump_xenv_symbols (idio_xi_t xi)
{
    char fn[40];
    snprintf (fn, 40, "idio-vm-st.%zu", xi);
    FILE *fp = fopen (fn, "w");
    if (NULL == fp) {
	perror ("fopen (idio-vm-st, w)");
	return;
    }

    idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

    IDIO st = IDIO_XENV_ST (idio_xenvs[xi]);
    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);

    idio_as_t al = idio_array_size (st);
    fprintf (fp, "VM symbols for xenv[%zu]: %zd references into the constants table\n", xi, al);
    fprintf (fp, " %-6.6s %-5.5s %s\n", "si", "ci", "constant");
    fprintf (fp, " %6.6s %5.5s %s\n", "------", "-----", "--------");

    idio_as_t i;
    for (i = 0 ; i < al; i++) {
	IDIO ci = idio_array_ref_index (st, i);
	fprintf (fp, " %-6zu ", i);
	if (idio_S_false != ci) {
	    idio_debug_FILE (fp, "%-5s ", ci);
	} else {
	    fprintf (fp, "%-5s ", "-");
	}
	if (idio_isa_integer (ci)) {
	    idio_debug_FILE (fp, "%-30s", idio_array_ref_index (cs, IDIO_FIXNUM_VAL (ci)));
	} else {
	    fprintf (fp, "%s ", "-");
	}
	fprintf (fp, "\n");
    }
    fprintf (fp, "\n");

    fclose (fp);
}

void idio_vm_dump_symbols ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-symbols ");
#endif

    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_vm_dump_xenv_symbols (xi);
    }
}

void idio_vm_dump_xenv_operators (idio_xi_t xi)
{
    IDIO eenv = IDIO_XENV_EENV (idio_xenvs[xi]);

    if (! idio_isa_struct_instance (eenv)) {
	return;
    }

    char fn[40];
    snprintf (fn, 40, "idio-vm-ot.%zu", xi);
    FILE *fp = fopen (fn, "w");
    if (NULL == fp) {
	perror ("fopen (idio-vm-ot, w)");
	return;
    }

    idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);
    IDIO ot = idio_list_reverse (IDIO_MEANING_EENV_OPERATORS (eenv));

    idio_as_t al = idio_list_length (ot);
    fprintf (fp, "VM operators for xenv[%zu]: %zd references into the constants table\n", xi, al);
    fprintf (fp, " %-6.6s %-5.5s %s\n", "oi", "ci", "constant");
    fprintf (fp, " %6.6s %5.5s %s\n", "------", "-----", "--------");

    while (idio_S_nil != ot) {
	IDIO op_si = IDIO_PAIR_H (ot);
	IDIO si = IDIO_PAIR_T (op_si);

	IDIO foi = IDIO_SI_SI (si);
	fprintf (fp, " %-6zu ", IDIO_FIXNUM_VAL (foi));

	IDIO fci = IDIO_SI_CI (si);
	fprintf (fp, "%-5zu ", IDIO_FIXNUM_VAL (fci));
	idio_debug_FILE (fp, "%-30s", idio_array_ref_index (cs, IDIO_FIXNUM_VAL (fci)));
	fprintf (fp, "\n");

	ot = IDIO_PAIR_T (ot);
    }
    fprintf (fp, "\n");

    fclose (fp);
}

void idio_vm_dump_operators ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-operators ");
#endif

    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_vm_dump_xenv_operators (xi);
    }
}

/*
 * idio_init_module() wants to add some constants for future modules
 * to use before we get round to initialising xenvs
 */
idio_as_t idio_vm_extend_constants_direct (IDIO cs, IDIO ch, IDIO v)
{
    IDIO_ASSERT (cs);
    IDIO_ASSERT (ch);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (hash, ch);

    idio_as_t ci = idio_array_size (cs);
    idio_array_push (cs, v);
    if (idio_S_nil != v) {
	idio_hash_put (ch, v, idio_fixnum (ci));
    }

    return ci;
}

/*
 * before we get round to initialising xenvs:
 *
 * * idio_init_module() wants to add some constants for future modules
 *   to use
 *
 * * IDIO_DEFINE_CONDITIONn() wants to add some constants
 */
idio_as_t idio_vm_extend_default_constants (IDIO v)
{
    IDIO_ASSERT (v);

    return idio_vm_extend_constants_direct (idio_vm_cs, idio_vm_ch, v);
}

idio_as_t idio_vm_extend_constants (idio_xi_t xi, IDIO v)
{
    IDIO_ASSERT (v);

    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);
    IDIO ch = IDIO_XENV_CH (idio_xenvs[xi]);

    return idio_vm_extend_constants_direct (cs, ch, v);
}

IDIO idio_vm_constants_ref (idio_xi_t xi, idio_as_t ci)
{
    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);

    return idio_array_ref_index (cs, ci);
}

idio_ai_t idio_vm_constants_lookup (idio_xi_t xi, IDIO name)
{
    IDIO_ASSERT (name);

    IDIO ch = IDIO_XENV_CH (idio_xenvs[xi]);

    if (idio_S_nil != name) {
	IDIO fgci = idio_hash_ref (ch, name);
	if (idio_S_unspec == fgci) {
	    return -1;
	} else {
	    return IDIO_FIXNUM_VAL (fgci);
	}
    }

    /*
     * This should only be for #n and we should have put that in slot
     * 0...
     */
    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);
    return idio_array_find_eqp (cs, name, 0);
}

idio_as_t idio_vm_constants_lookup_or_extend (idio_xi_t xi, IDIO name)
{
    IDIO_ASSERT (name);

    idio_ai_t gci = idio_vm_constants_lookup (xi, name);
    if (-1 == gci) {
	gci = idio_vm_extend_constants (xi, name);
    }

    return gci;
}

void idio_vm_dump_xenv_constants (idio_xi_t xi)
{
    char fn[40];
    snprintf (fn, 40, "idio-vm-cs.%zu", xi);

    FILE *fp = fopen (fn, "w");
    if (NULL == fp) {
	perror ("fopen (idio-vm-cs, w)");
	return;
    }

    idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);

    idio_as_t al = idio_array_size (cs);

    fprintf (fp, "VM constants for xenv[%zu]: %zu constants\n", xi, al);
    fprintf (fp, "%6.6s  %-20.20s %s\n", "ci", "type", "constant");
    fprintf (fp, "%6.6s  %-20.20s %s\n", "--", "----", "--------");

    idio_as_t i;
    for (i = 0 ; i < al; i++) {
	IDIO c = idio_array_ref_index (cs, i);
	fprintf (fp, "%6zd: ", i);
	size_t size = 0;
	char *cs = idio_as_string_safe (c, &size, 40, 1);
	fprintf (fp, "%-20s %s\n", idio_type2string (c), cs);
	IDIO_GC_FREE (cs, size);
    }
    fprintf (fp, "\n");

    fclose (fp);
}

void idio_vm_dump_constants ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-constants ");
#endif

    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_vm_dump_xenv_constants (xi);
    }
}

IDIO_DEFINE_PRIMITIVE0_DS ("vm-constants", vm_constants, (void), "", "\
Return the current vm constants array.		\n\
						\n\
:return: vm constants				\n\
:type args: array				\n\
")
{
    return idio_vm_cs;
}

IDIO idio_vm_src_expr_ref (idio_xi_t xi, idio_as_t sei)
{
    IDIO ses = IDIO_XENV_SES (idio_xenvs[xi]);

    if (sei >= idio_array_size (ses)) {
	return idio_S_false;
    }

    return idio_array_ref_index (ses, sei);
}

IDIO idio_vm_src_props_ref (idio_xi_t xi, idio_as_t spi)
{
    IDIO sps = IDIO_XENV_SPS (idio_xenvs[xi]);

    if (spi >= idio_array_size (sps)) {
	return idio_S_false;
    }

    return idio_array_ref_index (sps, spi);
}

void idio_vm_dump_xenv_src_exprs (idio_xi_t xi)
{
    char fn[40];
    snprintf (fn, 40, "idio-ses.%zu", xi);
    FILE *fp = fopen (fn, "w");
    if (NULL == fp) {
	perror ("fopen (idio-ses, w)");
	return;
    }

    idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

    IDIO ses = IDIO_XENV_SES (idio_xenvs[xi]);

    idio_as_t al = idio_array_size (ses);
    fprintf (fp, "VM source expressions for xenv[%zu]: %zd source expressions\n", xi, al);

    idio_as_t i;
    for (i = 0 ; i < al; i++) {
	IDIO src = idio_array_ref_index (ses, i);
	fprintf (fp, "%6zd: ", i);
	idio_debug_FILE (fp, "%s\n", src);
    }
    fprintf (fp, "\n");

    fclose (fp);
}

void idio_vm_dump_src_exprs ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-src-exprs ");
#endif

    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_vm_dump_xenv_src_exprs (xi);
    }
}

void idio_vm_dump_xenv_src_props (idio_xi_t xi)
{
    char fn[40];
    snprintf (fn, 40, "idio-sps.%zu", xi);
    FILE *fp = fopen (fn, "w");
    if (NULL == fp) {
	perror ("fopen (idio-sps, w)");
	return;
    }

    idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);
    IDIO sps = IDIO_XENV_SPS (idio_xenvs[xi]);

    IDIO fnh = IDIO_HASH_EQP (8);

    idio_as_t al = idio_array_size (sps);
    fprintf (fp, "VM source properties for xenv[%zu]: %zd source properties\n", xi, al);

    idio_as_t i;
    for (i = 0 ; i < al; i++) {
	IDIO p = idio_array_ref_index (sps, i);
	fprintf (fp, "%6zd: ", i);

	if (idio_isa_pair (p)) {
	    IDIO fi = IDIO_PAIR_H (p);
	    IDIO fn = idio_hash_reference (fnh, fi, IDIO_LIST1 (idio_S_false));
	    if (idio_S_false == fn) {
		fn = idio_array_ref_index (cs, IDIO_FIXNUM_VAL (fi));
		idio_hash_set (fnh, fi, fn);
	    }
	    idio_debug_FILE (fp, "%s", fn);
	    idio_debug_FILE (fp, ":line %4s", IDIO_PAIR_HT (p));
	} else {
	    fprintf (fp, " %-25s", "<no lex tuple>");
	}
	fprintf (fp, "\n");
    }

    fclose (fp);
}

void idio_vm_dump_src_props ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-src-props ");
#endif

    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_vm_dump_xenv_src_props (xi);
    }
}

IDIO_DEFINE_PRIMITIVE0_DS ("vm-src-exprs", vm_src_exprs, (void), "", "\
Return the current vm source constants array.	\n\
						\n\
:return: vm source constants			\n\
:type args: array				\n\
")
{
    return idio_vm_ses;
}

idio_as_t idio_vm_extend_values (idio_xi_t xi)
{
    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);

    idio_as_t i = idio_array_size (vs);
    idio_array_push (vs, idio_S_undef);

    if (0 == xi) {
	idio_array_push (idio_vm_st, idio_S_false);
    }

    return i;
}

IDIO_DEFINE_PRIMITIVE0_DS ("vm-extend-values", vm_extend_values, (void), "", "\
Extend the VM's values table			\n\
						\n\
:return: index					\n\
:rtype: integer					\n\
")
{
    idio_as_t gvi = idio_vm_extend_values (0);

    return idio_integer (gvi);
}

idio_as_t idio_vm_extend_default_values ()
{
    /*
     * We need this because we're creating values (in the C bootstrap)
     * before xenvs appear on the scene
     */
    idio_as_t i = idio_array_size (idio_vm_vt);
    idio_array_push (idio_vm_vt, idio_S_undef);

    idio_array_push (idio_vm_st, idio_S_false);

    return i;
}

IDIO idio_vm_values_ref (idio_xi_t xi, idio_as_t vi)
{
    if (vi) {
	IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
	IDIO v = idio_array_ref_index (vs, vi);

	if (idio_isa_struct_instance (v)) {
	    if (idio_struct_type_isa (IDIO_STRUCT_INSTANCE_TYPE (v), idio_path_type)) {
		v = idio_path_expand (v);
	    }
	}

	return v;
    } else {
	return idio_S_undef;
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("vm-values-ref", vm_values_ref, (IDIO index, IDIO args), "index [xi]", "\
Return the VM's values table entry at `index`	\n\
in execution environment `xi`			\n\
						\n\
:param index: index				\n\
:type index: integer				\n\
:param xi: xi, defaults to the current xi	\n\
:type xi: integer, optional			\n\
:return: value					\n\
						\n\
The choice of `xi` is limited as there is no	\n\
visibility of existing xi usage.  The only	\n\
known value is ``0`` for the global VM tables.	\n\
")
{
    IDIO_ASSERT (index);

    idio_ai_t gvi = -1;

    if (idio_isa_fixnum (index)) {
	gvi = IDIO_FIXNUM_VAL (index);
    } else if (idio_isa_bignum (index)) {
	if (IDIO_BIGNUM_INTEGER_P (index)) {
	    gvi = idio_bignum_ptrdiff_t_value (index);
	} else {
	    IDIO ii = idio_bignum_real_to_integer (index);
	    if (idio_S_nil == ii) {
		/*
		 * Test Case: vm-errors/vm-values-ref-bignum-float.idio
		 *
		 * vm-values-ref 1.1
		 */
		idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		gvi = idio_bignum_ptrdiff_t_value (ii);
	    }
	}
    } else {
	/*
	 * Test Case: vm-errors/vm-values-ref-bad-type.idio
	 *
	 * vm-values-ref #t
	 */
	idio_error_param_type ("integer", index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_xi_t xi = IDIO_THREAD_XI (idio_thread_current_thread ());

    if (idio_isa_pair (args)) {
	IDIO fxi = IDIO_PAIR_H (args);
	/*
	 * Test Case: vm-errors/vm-values-ref-bad-xi-type.idio
	 *
	 * vm-values-ref 0 #t
	 */
	IDIO_USER_TYPE_ASSERT (fixnum, fxi);

	xi = IDIO_FIXNUM_VAL (fxi);
    }

    return idio_vm_values_ref (xi, gvi);
}

IDIO idio_vm_values_gref (idio_xi_t xi, idio_as_t vi, char *const op)
{
    IDIO_C_ASSERT (op);

    if (vi) {
	idio_as_t gvi = vi;

	if (xi) {
	    IDIO fgvi = idio_vm_values_ref (xi, vi);
	    IDIO_TYPE_ASSERT (fixnum, fgvi);
	    gvi = IDIO_FIXNUM_VAL (fgvi);

	    /* for the idio_vm_values_ref() next */
	    xi = 0;
	}

	return idio_vm_values_ref (xi, gvi);
    } else {
	return idio_S_undef;
    }
}

/*
 * There's a lot of this in the C bootstrap
 */
IDIO idio_vm_default_values_ref (idio_as_t gvi)
{
    return idio_vm_values_ref (0, gvi);
}

void idio_vm_values_set (idio_xi_t xi, idio_as_t vi, IDIO v)
{
    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
    idio_array_insert_index (vs, v, vi);
}

void idio_vm_default_values_set (idio_as_t gvi, IDIO v)
{
    return idio_vm_values_set (0, gvi, v);
}

void idio_vm_dump_xenv_values (idio_xi_t xi)
{
    IDIO Rx = IDIO_SYMBOL ("Rx");

    char fn[40];
    snprintf (fn, 40, "idio-vm-vt.%zu", xi);
    FILE *fp = fopen (fn, "w");
    if (NULL == fp) {
	perror ("fopen (idio-vm-vt, w)");
	return;
    }

    idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

    IDIO vs = IDIO_XENV_VT (idio_xenvs[xi]);
    IDIO vs0 = IDIO_XENV_VT (idio_xenvs[0]);

    idio_as_t al = idio_array_size (vs);
    fprintf (fp, "VM values for xenv[%zu]: %zd values\n", xi, al);
    fprintf (fp, "%6.6s  %4.4s %-20.20s %s\n", "vi", "gvi", "type", "value");
    fprintf (fp, "%6.6s  %4.4s %-20.20s %s\n", "--", "---", "----", "-----");

    idio_as_t i;
    for (i = 0 ; i < al; i++) {
	fprintf (fp, "%6zd: ", i);
	IDIO fgvi = idio_array_ref_index (vs, i);

	idio_as_t gvi = 0;
	if (xi) {
	    gvi = IDIO_FIXNUM_VAL (fgvi);
	} else {
	    gvi = i;
	}
	fprintf (fp, "%4zd ", gvi);

	if (gvi) {
	    IDIO v = idio_array_ref_index (vs0, gvi);

	    size_t size = 0;
	    char *vs = NULL;
	    if (idio_src_properties == v) {
		/*
		 * This is tens of thousands of
		 *
		 * e -> struct {file, line, e}
		 *
		 * entries.  It takes millions of calls to implement and
		 * seconds to print!
		 */
		vs = idio_as_string_safe (v, &size, 0, 1);
	    } else if (idio_isa_struct_instance (v) &&
		       (IDIO_STRUCT_TYPE_NAME (IDIO_STRUCT_INSTANCE_TYPE (v)) == Rx)) {
		/*
		 * These objects are a little bit recursive and can easily
		 * become 100+MB when printed (to a depth of 40...)
		 */
		vs = idio_as_string_safe (v, &size, 4, 1);
	    } else {
		vs = idio_as_string_safe (v, &size, 40, 1);
	    }
	    fprintf (fp, "%-20s %s", idio_type2string (v), vs);
	    IDIO_GC_FREE (vs, size);
	} else {
	    fprintf (fp, "%-20s %s", "-", "-");
	}
	fprintf (fp, "\n");
    }
    fprintf (fp, "\n");

    fclose (fp);
}

void idio_vm_dump_values ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-vt ");
#endif

    /*
     * The printer will update {seen} and potentially call some
     * Idio-code for structures.  That means we're at risk of garbage
     * collection.
     */
    idio_gc_pause ("vm-dump-values");

    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_vm_dump_xenv_values (xi);
    }

    idio_gc_resume ("vm-dump-values");
}

IDIO idio_vm_extend_tables (idio_xi_t xi, IDIO name, IDIO scope, IDIO module, IDIO desc)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (scope);
    IDIO_ASSERT (module);
    IDIO_ASSERT (desc);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (symbol, scope);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (string, desc);

    idio_as_t ci = idio_vm_constants_lookup_or_extend (xi, name);
    IDIO fci = idio_fixnum (ci);
    idio_as_t vi = idio_vm_extend_values (xi);
    IDIO fvi = idio_fixnum (vi);

    idio_as_t gvi = vi;
    if (xi) {
	gvi = idio_vm_extend_values (0);
    }
    IDIO fgvi = idio_fixnum (gvi);

    idio_vm_symbols_set (xi, vi, fci);
    idio_vm_values_set (xi, vi, fgvi);

    IDIO si = IDIO_LIST6 (scope, fvi, fci, fgvi, module, desc);

    return si;
}

void idio_vm_thread_state (IDIO thr)
{
    IDIO_ASSERT (thr);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_vm_debug (thr, "vm-thread-state", 0);
    fprintf (stderr, "\n");

    idio_vm_frame_tree (idio_S_nil);
    fprintf (stderr, "\n");

#ifdef IDIO_DEBUG
    IDIO frame = IDIO_THREAD_FRAME (thr);
    while (idio_S_nil != frame) {
	idio_xi_t frame_xi = IDIO_FRAME_XI (frame);
	IDIO cs = IDIO_XENV_CS (idio_xenvs[frame_xi]);
	idio_as_t ncs = idio_array_size (cs);

	IDIO faci = IDIO_FRAME_NAMES (frame);
	idio_ai_t aci = IDIO_FIXNUM_VAL (faci);
	IDIO names = idio_S_nil;
	if (aci < (idio_ai_t) ncs) {
	    names = idio_array_ref_index (cs, aci);
	}

	fprintf (stderr, "vm-thread-state: frame: %10p (%10p) %2u/%2u %5" PRIdPTR, frame, IDIO_FRAME_NEXT (frame), IDIO_FRAME_NPARAMS (frame), IDIO_FRAME_NALLOC (frame), aci);
	idio_debug (" - %-20s - ", names);
	idio_debug ("%s\n", idio_frame_args_as_list (frame));
	frame = IDIO_FRAME_NEXT (frame);
    }
    fprintf (stderr, "\n");
#endif

    idio_vm_trap_state (thr);

    int header = 1;
    IDIO dhs = idio_hash_keys_to_list (idio_condition_default_handler);

    while (idio_S_nil != dhs) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	IDIO ct = IDIO_PAIR_H (dhs);
	idio_debug ("vm-thread-state: dft handlers: %-45s ", idio_hash_ref (idio_condition_default_handler, ct));
	idio_debug (" %s\n", IDIO_STRUCT_TYPE_NAME (ct));

	dhs = IDIO_PAIR_T (dhs);
    }

    header = 1;
    idio_sp_t dsp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);
    while (dsp != -1) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "vm-thread-state: dynamic: SP %3zd ", dsp);
	idio_debug (" next %s", idio_array_ref_index (stack, dsp - 3));
	idio_debug (" vi %s", idio_array_ref_index (stack, dsp - 1));
	idio_debug (" val %s\n", idio_array_ref_index (stack, dsp - 2));
	dsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, dsp - 3));
    }

    header = 1;
    idio_sp_t esp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);
    while (esp != -1) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "vm-thread-state: environ: SP %3zd ", esp);
	idio_debug ("= %s\n", idio_array_ref_index (stack, esp - 1));
	idio_debug (" next %s", idio_array_ref_index (stack, esp - 3));
	idio_debug (" vi %s", idio_array_ref_index (stack, esp - 1));
	idio_debug (" val %s\n", idio_array_ref_index (stack, esp - 2));
	esp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, esp - 3));
    }

    header = 1;
    idio_sp_t asp = idio_vm_find_stack_marker (stack, idio_SM_abort, 0, 0);
    while (asp != -1) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "vm-thread-state: abort: SP %3zd ", asp);
	idio_debug ("= %s\n", idio_array_ref_index (stack, asp - 1));
	asp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, asp - 2));
    }

    fprintf (stderr, "\n");
    if (NULL == idio_k_exit) {
	fprintf (stderr, "vm-thread-state: idio_k_exit NULL\n");
    } else {
	idio_debug ("vm-thread-state: idio_k_exit %s\n", idio_k_exit);
    }
}

IDIO_DEFINE_PRIMITIVE0_DS ("idio-thread-state", idio_thread_state, (), "", "\
Display a dump of the current thread's state	\n\
						\n\
:return: ``#<unspec>``				\n\
")
{
    idio_vm_thread_state (idio_thread_current_thread ());

    return idio_S_unspec;
}

/*
 * Code coverage:
 *
 * Tricky!
 *
 * This is, in principle, a more ordered Idio-exit, rather than, say,
 * libc/exit.
 *
 * In practice this is more likely to provoke catastrophic error.  Ho
 * hum.
 */
IDIO_DEFINE_PRIMITIVE1_DS ("exit", exit, (IDIO istatus), "status", "\
attempt to exit with status `status`			\n\
							\n\
:param status: exit status				\n\
:type status: fixnum or C/int				\n\
							\n\
Does not return [#]_.					\n\
							\n\
This form will attempt to run through the full system shutdown.	\n\
							\n\
.. seealso:: :ref:`libc/exit <libc/exit>` for a more abrupt exit	\n\
							\n\
.. [#] YMMV						\n\
")
{
    IDIO_ASSERT (istatus);

    int status = -1;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else if (idio_isa_C_int (istatus)) {
	status = IDIO_C_TYPE_int (istatus);
    } else {
	/*
	 * Test Case: vm-errors/exit-bad-type.idio
	 *
	 * Idio/exit #t
	 *
	 * NB without the SPACE before the slash which is
	 * end-of-comment in C.  Otherwise, with vanilla "exit", we
	 * pick up the libc/exit.
	 */
	idio_error_param_type ("fixnum|C/int", istatus, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * We've been asked to exit.  Try to flush the usual buffers.
     */
    IDIO oh = idio_thread_current_output_handle ();
    idio_flush_handle (oh);

    IDIO eh = idio_thread_current_error_handle ();
    idio_flush_handle (eh);

    idio_exit_status = status;

    idio_vm_restore_exit (idio_k_exit, istatus);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%set-exit-status", set_exit_status, (IDIO istatus), "status", "\
update potential :lname:`Idio` exit status with	\n\
`status`					\n\
						\n\
:param status: exit status			\n\
:type status: C/int				\n\
")
{
    IDIO_ASSERT (istatus);

    IDIO_TYPE_ASSERT (C_int, istatus);

    idio_exit_status = IDIO_C_TYPE_int (istatus);

    return idio_S_unspec;
}

time_t idio_vm_elapsed (void)
{
    return (time ((time_t *) NULL) - idio_vm_t0);
}

IDIO_DEFINE_PRIMITIVE2_DS ("run-in-thread", run_in_thread, (IDIO thr, IDIO thunk), "thr thunk", "\
Run `thunk` in thread `thr`.			\n\
						\n\
:param thr: the thread				\n\
:type thr: thread				\n\
:param thunk: a thunk				\n\
:type thunk: function				\n\
")
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (thunk);

    /*
     * Test Case: vm-errors/run-in-thread-bad-thread-type.idio
     *
     * run-in-thread #t #t
     */
    IDIO_USER_TYPE_ASSERT (thread, thr);
    /*
     * Test Case: vm-errors/run-in-thread-bad-func-type.idio
     *
     * run-in-thread (threading/current-thread) #t
     */
    IDIO_USER_TYPE_ASSERT (function, thunk);

    IDIO cthr = idio_thread_current_thread ();

    idio_thread_set_current_thread (thr);

    idio_xi_t xi0 = IDIO_THREAD_XI (thr);
    idio_pc_t pc0 = IDIO_THREAD_PC (thr);
    idio_vm_default_pc (thr);

    IDIO r = idio_vm_invoke_C_thread (thr, thunk);

    if (IDIO_THREAD_PC (thr) != pc0) {
	IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
	IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_XI (thr)));
	IDIO_THREAD_STACK_PUSH (idio_SM_return);

	r = idio_vm_run (thr, IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr), IDIO_VM_RUN_IDIO);

	idio_pc_t pc = IDIO_THREAD_PC (thr);
	if (pc == (idio_vm_FINISH_pc + 1)) {
	    IDIO_THREAD_PC (thr) = pc0;
	}
	IDIO_THREAD_XI (thr) = xi0;
    }

    idio_thread_set_current_thread (cthr);

    return r;
}

IDIO idio_vm_frame_tree (IDIO args)
{
    IDIO_ASSERT (args);

    IDIO thr = idio_thread_current_thread ();

    IDIO frame = IDIO_THREAD_FRAME (thr);

    int depth = 0;

    int first = 1;
    while (idio_S_nil != frame) {
	if (first) {
	    first = 0;
	    fprintf (stderr, "  %2.2s %2.2s  %20.20s   %s\n", "frame", "#", "var", "val");
	}

	idio_xi_t frame_xi = IDIO_FRAME_XI (frame);
	IDIO cs = IDIO_XENV_CS (idio_xenvs[frame_xi]);
	idio_as_t ncs = idio_array_size (cs);

	IDIO faci = IDIO_FRAME_NAMES (frame);
	idio_ai_t aci = IDIO_FIXNUM_VAL (faci);
	IDIO names = idio_S_nil;
	if (aci < (idio_ai_t) ncs) {
	    names = idio_array_ref_index (cs, aci);
	}
	if (0 == aci) {
	    fprintf (stderr, "  ?? aci=%zd ", aci);
	    idio_debug ("%s\n", names);
	}

	/*
	 * formal parameters -- marked with *
	 */
	idio_ai_t al = IDIO_FRAME_NPARAMS (frame);
	idio_ai_t i;
	for (i = 0; i < al; i++) {
	    fprintf (stderr, "  %2d %2zdp ", depth, i);
	    if (idio_S_nil != names) {
		idio_debug ("%20s = ", IDIO_PAIR_H (names));
		names = IDIO_PAIR_T (names);
	    } else {
		fprintf (stderr, "%20s = ", "?");
	    }
	    idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));
	}

	/*
	 * varargs element -- probably named #f
	 */
	fprintf (stderr, "  %2d %2zd* ", depth, i);
	if (idio_S_nil != names) {
	    if (idio_S_false == IDIO_PAIR_H (names)) {
		fprintf (stderr, "%20s = ", "-");
	    } else {
		idio_debug ("%20s = ", IDIO_PAIR_H (names));
	    }
	    names = IDIO_PAIR_T (names);
	} else {
	    fprintf (stderr, "%20s = ", "?");
	}
	idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));

	/*
	 * "locals"
	 */
	al = IDIO_FRAME_NALLOC (frame);
	for (i++; i < al; i++) {
	    fprintf (stderr, "  %2d %2zdl ", depth, i);
	    if (idio_S_nil != names) {
		idio_debug ("%20s = ", IDIO_PAIR_H (names));
		names = IDIO_PAIR_T (names);
	    } else {
		fprintf (stderr, "%20s = ", "?");
	    }
	    idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));
	}
	fprintf (stderr, "\n");

	depth++;
	frame = IDIO_FRAME_NEXT (frame);
    }
    if (0 == first) {
	fprintf (stderr, "      #p is a parameter\n");
	fprintf (stderr, "      #* is the varargs arg - is the name (if no name given)\n");
	fprintf (stderr, "      #l is a local var\n");
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%vm-frame-tree", vm_frame_tree, (IDIO args), "[args]", "\
Show the current frame tree.					\n\
								\n\
:param args: (optional)						\n\
:type args: list						\n\
")
{
    IDIO_ASSERT (args);

    return idio_vm_frame_tree (args);
}

void idio_vm_trap_state (IDIO thr)
{
    IDIO_ASSERT (thr);

    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_sp_t ss = idio_array_size (stack);

    idio_sp_t tsp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0);

    if (tsp > ss) {
	fprintf (stderr, "TRAP SP %zd > size (stack) %zd\n", tsp, ss);
    } else {
	while (1) {
	    fprintf (stderr, "vm-thread-state: trap: SP %3zd: ", tsp);
	    IDIO handler = idio_array_ref_index (stack, tsp - 1);

	    if (idio_isa_closure (handler)) {
		IDIO name = idio_ref_property (handler, idio_KW_name, IDIO_LIST1 (idio_S_nil));
		if (idio_S_nil != name) {
		    idio_debug (" %-45s", name);
		} else {
		    idio_debug (" %-45s", handler);
		}
	    } else {
		idio_debug (" %-45s", handler);
	    }

	    IDIO ct_gci = idio_array_ref_index (stack, tsp - 2);
	    idio_ai_t gci = IDIO_FIXNUM_VAL (ct_gci);

	    IDIO ct_sym = idio_vm_constants_ref (0, gci);
	    IDIO ct = idio_module_symbol_value_recurse (ct_sym, IDIO_THREAD_ENV (thr), idio_S_nil);

	    if (idio_isa_struct_type (ct)) {
		idio_debug (" %s\n", IDIO_STRUCT_TYPE_NAME (ct));
	    } else {
		idio_debug (" %s\n", ct);
	    }

	    idio_sp_t ntsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, tsp - 3));
	    if (ntsp == tsp) {
		break;
	    }
	    tsp = ntsp;
	}
    }
}

IDIO_DEFINE_PRIMITIVE0_DS ("%vm-trap-state", vm_trap_state, (void), "", "\
Show the current trap tree.					\n\
")
{
    idio_vm_trap_state (idio_thread_current_thread ());

    return idio_S_unspec;
}

IDIO idio_vm_run_xenv (idio_xi_t xi, IDIO pcs)
{
    IDIO_ASSERT (pcs);

    IDIO_TYPE_ASSERT (list, pcs);

    IDIO thr = idio_thread_current_thread ();

    idio_pc_t opc = IDIO_THREAD_PC (thr);
    idio_xi_t oxi = IDIO_THREAD_XI (thr);

    idio_vm_preserve_all_state (thr);

    IDIO r = idio_S_unspec;

    while (idio_S_nil != pcs) {
	idio_pc_t C_pc = IDIO_FIXNUM_VAL (IDIO_PAIR_H (pcs));

	r = idio_vm_run (thr, xi, C_pc, IDIO_VM_RUN_C);

	pcs = IDIO_PAIR_T (pcs);
    }

    idio_vm_restore_all_state (thr);

    IDIO_THREAD_XI (thr) = oxi;
    IDIO_THREAD_PC (thr) = opc;

    return r;
}

idio_xi_t idio_vm_add_xenv (IDIO desc, IDIO st, IDIO cs, IDIO ch, IDIO vt, IDIO ses, IDIO sps, IDIO bc)
{
    IDIO_ASSERT (desc);
    IDIO_ASSERT (st);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (ch);
    IDIO_ASSERT (vt);
    IDIO_ASSERT (ses);
    IDIO_ASSERT (sps);
    IDIO_ASSERT (bc);

    IDIO_TYPE_ASSERT (string, desc);
    IDIO_TYPE_ASSERT (array, st);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (hash, ch);
    IDIO_TYPE_ASSERT (array, vt);
    IDIO_TYPE_ASSERT (array, ses);
    IDIO_TYPE_ASSERT (array, sps);

    idio_xenv_t *xe = idio_xenv ();

    IDIO_XENV_EENV (xe)		  = idio_S_nil;

    /*
     * XXX We need to idio_gc_protect_auto() these elements as they
     * are not in a GC-visible structure.
     *
     * Note, though, these tables are freed (see idio_final()) *after*
     * the GC has mechanically free every allocated value including
     * these things we are protecting.
     */
    IDIO_XENV_DESC (xe) = desc;
    idio_gc_protect_auto (IDIO_XENV_DESC (xe));

    IDIO_XENV_ST (xe)   = st;
    idio_gc_protect_auto (IDIO_XENV_ST (xe));

    IDIO_XENV_CS (xe)   = cs;
    idio_gc_protect_auto (IDIO_XENV_CS (xe));

    IDIO_XENV_CH (xe)   = ch;
    idio_gc_protect_auto (IDIO_XENV_CH (xe));

    IDIO_XENV_VT (xe)   = vt;
    idio_gc_protect_auto (IDIO_XENV_VT (xe));

    IDIO_XENV_SES (xe)  = ses;
    idio_gc_protect_auto (IDIO_XENV_SES (xe));

    IDIO_XENV_SPS (xe)  = sps;
    idio_gc_protect_auto (IDIO_XENV_SPS (xe));

    if (idio_isa_octet_string (bc)) {
	IDIO_XENV_BYTE_CODE (xe) = idio_codegen_string2idio_ia (bc);
    } else if (idio_isa_C_pointer (bc)) {
	IDIO_IA_T ia = IDIO_C_TYPE_POINTER_P (bc);

	IDIO_XENV_BYTE_CODE (xe) = ia;
	IDIO_IA_REFCNT (ia)++;
    } else {
	fprintf (stderr, "add-xenv: unexpected byte code format\n");
	assert (0);
    }

    return IDIO_XENV_INDEX (xe);
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%vm-add-xenv", vm_add_xenv, (IDIO args), "desc st cs ch vt ses sps byte-code pc", "\
Add a new xenv derived from the arguments	\n\
						\n\
:param desc: a description			\n\
:type desc: string				\n\
:param st: symbol table				\n\
:type st: array					\n\
:param cs: constants				\n\
:type cs: array					\n\
:param cs: constants hash			\n\
:type cs: hash					\n\
:param vt: value table				\n\
:type vt: array					\n\
:param ses: source expressions			\n\
:type ses: array				\n\
:param sps: source properties			\n\
:type sps: array				\n\
:param byte-code: byte code			\n\
:type byte-code: octet-string			\n\
:param pc: the starting PC			\n\
:type pc: fixnum				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (args);

    IDIO thr = idio_thread_current_thread ();
    size_t n = idio_list_length (args);

    if (n != 8) {
	idio_vm_error_arity (0, thr, n, 8, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int i = 0;
    IDIO desc = idio_list_nth (args, i++, idio_S_nil);
    IDIO st   = idio_list_nth (args, i++, idio_S_nil);
    IDIO cs   = idio_list_nth (args, i++, idio_S_nil);
    IDIO ch   = idio_list_nth (args, i++, idio_S_nil);
    IDIO vt   = idio_list_nth (args, i++, idio_S_nil);
    IDIO ses  = idio_list_nth (args, i++, idio_S_nil);
    IDIO sps  = idio_list_nth (args, i++, idio_S_nil);
    IDIO bs   = idio_list_nth (args, i++, idio_S_nil);
    IDIO pc   = idio_list_nth (args, i++, idio_S_nil);

    IDIO_USER_TYPE_ASSERT (string, desc);
    IDIO_USER_TYPE_ASSERT (array, st);
    IDIO_USER_TYPE_ASSERT (array, cs);
    IDIO_USER_TYPE_ASSERT (hash, ch);
    IDIO_USER_TYPE_ASSERT (array, vt);
    IDIO_USER_TYPE_ASSERT (array, ses);
    IDIO_USER_TYPE_ASSERT (array, sps);
    IDIO_USER_TYPE_ASSERT (octet_string, bs);
    IDIO_USER_TYPE_ASSERT (integer, pc);

    idio_xi_t xi = idio_vm_add_xenv (desc, st, cs, ch, vt, ses, sps, bs);

    return idio_fixnum (xi);
}

idio_xi_t idio_vm_add_xenv_from_eenv (IDIO thr, IDIO eenv)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO desc = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_DESC);
    IDIO st   = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_ST);
    IDIO cs   = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_CS);
    IDIO ch   = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_CH);
    IDIO vt   = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_VT);
    IDIO ses  = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_SES);
    IDIO sps  = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_SPS);

    IDIO CTP_bc    = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_BYTE_CODE);
    if (idio_CSI_idio_ia_s != IDIO_C_TYPE_POINTER_PTYPE (CTP_bc)) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_exp ("%vm-add-xenv-from-eenv", "byte-code", CTP_bc, "struct-idio-ia-s", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    idio_xi_t xi = idio_vm_add_xenv (desc, st, cs, ch, vt, ses, sps, CTP_bc);

    IDIO_XENV_EENV (idio_xenvs[xi]) = eenv;
    idio_gc_protect_auto (eenv);

    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_XI, idio_fixnum (xi));

    return xi;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%vm-add-xenv-from-eenv", vm_add_xenv_from_eenv, (IDIO eenv, IDIO pc), "eenv pc", "\
Add a new xenv derived from `eenv` with `pc`	\n\
being the starting PC				\n\
						\n\
:param eenv: an evaluation environment		\n\
:type eenv: struct-instance			\n\
:param pc: the starting PC			\n\
:type pc: fixnum				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (pc);

    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO_TYPE_ASSERT (fixnum, pc);

    IDIO thr = idio_thread_current_thread ();
    idio_pc_t opc = IDIO_THREAD_PC (thr);
    idio_xi_t oxi = IDIO_THREAD_XI (thr);

    idio_xi_t xi = 0;
    if (idio_default_eenv != eenv) {
	xi = idio_vm_add_xenv_from_eenv (thr, eenv);
    }

    idio_vm_preserve_all_state (thr);

    IDIO r = idio_S_unspec;

    idio_pc_t C_pc = IDIO_FIXNUM_VAL (pc);

    fprintf (stderr, "\n\n%%vaxfe running xi %zu @%zu\n", xi, C_pc);

    r = idio_vm_run (thr, xi, C_pc, IDIO_VM_RUN_C);

    idio_debug ("%vaxfe => %s\n", r);

    idio_vm_restore_all_state (thr);

    IDIO_THREAD_XI (thr) = oxi;
    IDIO_THREAD_PC (thr) = opc;

    return r;
}

void idio_vm_dump_xenv (idio_xi_t xi)
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-xenv [%zu] ", xi);
#endif

    idio_vm_dump_xenv_constants (xi);
    idio_vm_dump_xenv_symbols (xi);
    idio_vm_dump_xenv_operators (xi);
    idio_vm_dump_xenv_src_props (xi);
    idio_vm_dump_xenv_dasm (xi);
    idio_vm_dump_xenv_values (xi);
}

void idio_vm_save_xenvs (idio_xi_t from)
{
    if (from >= idio_xenvs_size) {
	fprintf (stderr, "WARNING: save-xenvs: xi %zd >= max XI %zd\n", from, idio_xenvs_size);
	return;
    }

    IDIO lsh = idio_open_input_string_handle_C (IDIO_STATIC_STR_LEN ("import compile"));
    idio_load_handle_C (lsh, idio_read, idio_evaluate_func, idio_default_eenv);

    IDIO cfw = idio_module_symbol_value (idio_S_cfw,
					 idio_compile_module,
					 idio_S_nil);

    for (idio_xi_t xi = from ; xi < idio_xenvs_size; xi++) {
	IDIO eenv = IDIO_XENV_EENV (idio_xenvs[xi]);

	IDIO efn = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_FILE);

	if (idio_isa_string (efn)) {
	    idio_debug ("saving xenv for %s\n", efn);
	    idio_vm_invoke_C (IDIO_LIST4 (cfw, efn, eenv, idio_fixnum (idio_prologue_len)));
	}
    }
}

/*
 * NB There is no point in exposing idio_vm_source_location() as a
 * primitive as wherever you call it it returns that place -- in other
 * words it is of no use in any kind of handler as it merely tells you
 * you are in the handler.
 */
IDIO idio_vm_source_location ()
{
    IDIO lsh = idio_open_output_string_handle_C ();
    IDIO cthr = idio_thread_current_thread ();
    IDIO fsei = IDIO_THREAD_EXPR (cthr);
    idio_xi_t xi = IDIO_THREAD_XI (cthr);
    if (idio_isa_fixnum (fsei)) {
	IDIO sp = idio_vm_src_props_ref (xi, IDIO_FIXNUM_VAL (fsei));

	if (idio_isa_pair (sp)) {
	    IDIO fn = idio_vm_constants_ref (xi, IDIO_FIXNUM_VAL (IDIO_PAIR_H (sp)));
	    idio_display (fn, lsh);
	    idio_display_C (":line ", lsh);
	    idio_display (IDIO_PAIR_HT (sp), lsh);
	} else {
	    idio_display_C ("<no source properties>", lsh);
	}
    } else {
	idio_display (fsei, lsh);
    }

    return idio_get_output_string (lsh);
}

void idio_vm_decode_thread (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_sp_t sp0 = idio_array_size (stack) - 1;
    idio_sp_t sp = sp0;
    fprintf (stderr, "vm-decode-thread: thr=%8p sp=%4zd pc=[%zu]@%zd\n", thr, sp, IDIO_THREAD_XI (thr), IDIO_THREAD_PC (thr));

    idio_vm_decode_stack (thr, stack);
}

void idio_vm_decode_stack (IDIO thr, IDIO stack)
{
    IDIO_ASSERT (stack);
    IDIO_TYPE_ASSERT (array, stack);

    idio_sp_t sp0 = idio_array_size (stack) - 1;
    idio_sp_t sp = sp0;

    fprintf (stderr, "vm-decode-stack: stk=%p sp=%4zd\n", stack, sp);

    for (;sp >= 0; ) {

	fprintf (stderr, "%4zd\t", sp);

	IDIO sv0 = sp >= 0 ? idio_array_ref_index (stack, sp - 0) : idio_S_nil;
	IDIO sv1 = sp >= 1 ? idio_array_ref_index (stack, sp - 1) : idio_S_nil;
	IDIO sv2 = sp >= 2 ? idio_array_ref_index (stack, sp - 2) : idio_S_nil;
	IDIO sv3 = sp >= 3 ? idio_array_ref_index (stack, sp - 3) : idio_S_nil;
	IDIO sv4 = sp >= 4 ? idio_array_ref_index (stack, sp - 4) : idio_S_nil;
	IDIO sv5 = sp >= 5 ? idio_array_ref_index (stack, sp - 5) : idio_S_nil;

	/*
	 * Make some educated guess about what was pushed onto the
	 * stack
	 */
	if (idio_SM_trap == sv0 &&
	    sp >= 3 &&
	    idio_isa_function (sv1) &&
	    idio_isa_fixnum (sv2) &&
	    idio_isa_fixnum (sv3)) {
	    fprintf (stderr, "%-20s ", "TRAP");
	    idio_debug ("%-35s ", sv1);

	    IDIO fgci = sv2;
	    idio_debug ("%-20s ", idio_vm_constants_ref (0, IDIO_FIXNUM_VAL (fgci)));
	    idio_sp_t tsp = IDIO_FIXNUM_VAL (sv3);
	    fprintf (stderr, "next t/h @%zd", tsp);
	    sp -= 4;
	} else if (idio_SM_escaper == sv0 &&
		   sp >= 3 &&
		   idio_isa_fixnum (sv1) &&
		   idio_isa_frame (sv2) &&
		   idio_isa_fixnum (sv3)) {
	    fprintf (stderr, "%-20s ", "ESCAPER");

	    IDIO fgci = sv1;
	    idio_debug ("%-20s ", idio_vm_constants_ref (0, IDIO_FIXNUM_VAL (fgci)));
	    fprintf (stderr, "PC -> %" PRIdPTR, IDIO_FIXNUM_VAL (sv3));
	    sp -= 4;
	} else if (idio_SM_dynamic == sv0 &&
		   sp >= 3) {
	    fprintf (stderr, "%-20s vi=%5" PRIdPTR " ", "DYNAMIC", IDIO_FIXNUM_VAL (sv1));
	    idio_debug ("%-35s ", sv2);
	    idio_sp_t dsp = IDIO_FIXNUM_VAL (sv3);
	    fprintf (stderr, "next dyn @%zd", dsp);
	    sp -= 4;
	} else if (idio_SM_environ == sv0 &&
		   sp >= 3) {
	    fprintf (stderr, "%-20s vi=%5" PRIdPTR, "ENVIRON", IDIO_FIXNUM_VAL (sv1));
	    idio_debug ("%-35s ", sv2);
	    idio_sp_t esp = IDIO_FIXNUM_VAL (sv3);
	    fprintf (stderr, "next env @%zd", esp);
	    sp -= 4;
	} else if (idio_SM_abort == sv0 &&
		   sp >= 2) {
	    fprintf (stderr, "%-20s ", "ABORT");
	    if (idio_isa_pair (sv1)) {
		idio_debug ("%-35s ", IDIO_PAIR_HT (sv1));
	    } else {
		idio_debug ("?? %-35s ", sv1);
	    }
	    idio_sp_t asp = IDIO_FIXNUM_VAL (sv2);
	    fprintf (stderr, "next abort @%zd", asp);
	    sp -= 3;
	} else if (idio_SM_preserve_all_state == sv0 &&
		   sp >= 5) {
	    fprintf (stderr, "%-20s ", "ALL-STATE");
	    idio_debug ("reg1 %s ", sv5);
	    idio_debug ("reg2 %s ", sv4);
	    idio_debug ("expr %s ", sv3);
	    idio_debug ("func %s ", sv2);
	    idio_debug ("val  %s ", sv1);
	    sp -= 6;
	} else if (idio_SM_preserve_state == sv0 &&
		   sp >= 2 &&
		   idio_isa_module (sv1) &&
		   (idio_S_nil == sv2 ||
		    idio_isa_frame (sv2))) {
	    fprintf (stderr, "%-20s ", "STATE");
	    idio_debug ("mod %s ", sv1);
	    idio_debug ("%s ", sv2);
	    sp -= 3;
	} else if (idio_SM_return == sv0 &&
		   sp >= 2 &&
		   idio_isa_fixnum (sv1) &&
		   idio_isa_fixnum (sv2)) {
	    fprintf (stderr, "%-20s ", "RETURN");
	    idio_debug ("[%s]@", sv1);
	    idio_debug ("%s ", sv2);
	    ssize_t spc = IDIO_FIXNUM_VAL (sv2);
	    if (spc < 0) {
		fprintf (stderr, "sv1==pc %zd < 0", spc);
	    } else {
		idio_pc_t pc = spc;
		if (idio_vm_NCE_pc == pc) {
		    fprintf (stderr, "-- NON-CONT-ERROR");
		} else if  (idio_vm_FINISH_pc == pc) {
		    fprintf (stderr, "-- FINISH");
		} else if (idio_vm_CHR_pc == pc) {
		    fprintf (stderr, "-- condition handler return (TRAP + STATE + RETURN following?)");
		} else if (idio_vm_AR_pc ==  pc) {
		    fprintf (stderr, "-- apply return");
		} else if (idio_vm_IHR_pc == pc) {
		    fprintf (stderr, "-- interrupt handler return (ALL-STATE (+ STATE) + RETURN following?)");
		}
	    }
	    sp -= 3;
	} else if (idio_SM_preserve_continuation == sv0 &&
		   sp >= 1 &&
		   idio_isa_fixnum (sv1)) {
	    fprintf (stderr, "%-20s ", "CONTINUATION PC");
	    idio_debug ("%s ", sv1);
	    sp -= 2;
	} else {
	    fprintf (stderr, "a %-18s ", idio_type2string (sv0));
	    idio_debug ("%.100s", sv0);
	    sp -= 1;
	}

	fprintf (stderr, "\n");
    }
}

void idio_vm_reset_thread (IDIO thr, int verbose)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (0 && verbose) {
	fprintf (stderr, "\nvm-reset-thread\n");

	/* IDIO stack = IDIO_THREAD_STACK (thr); */
	IDIO frame = IDIO_THREAD_FRAME (thr);

	idio_vm_thread_state (thr);

	size_t i = 0;
	while (idio_S_nil != frame) {
	    fprintf (stderr, "call frame %4zd: ", i++);
	    idio_debug ("%s\n", frame);
	    frame = IDIO_FRAME_NEXT (frame);
	}

	idio_debug ("env: %s\n", IDIO_THREAD_ENV (thr));

	idio_debug ("MODULE:\t%s\n", IDIO_MODULE_NAME (IDIO_THREAD_MODULE (thr)));
	idio_debug ("INPUT:\t%s\n", IDIO_THREAD_INPUT_HANDLE (thr));
	idio_debug ("OUTPUT:\t%s\n", IDIO_THREAD_OUTPUT_HANDLE (thr));
    }

    /*
     * There was code to clear the stack here as where better to clear
     * down the stack than in the reset code?  But the question was,
     * clear down to what to ensure the engine kept running?  Whatever
     * value was chosen always seemed to end in tears.
     *
     * However, idio_vm_run() knows the SP for when it was started and
     * given that we're about to tell it to FINISH the current run
     * then it would make sense for it to clear down to the last known
     * good value, the value it had when it started.
     */
    IDIO_THREAD_PC (thr) = idio_vm_FINISH_pc;
}

void idio_init_vm_values ()
{
    idio_vm_st = idio_array (0);
    idio_gc_protect_auto (idio_vm_st);

     /*
      * Start up and shutdown generates some 7600 constants (probably
      * 2700 actual constants) and running the test suite generates
      * 23000 constants (probably 5000 actual constants).  Most of
      * these are src code properties -- it is difficult to
      * distinguish a genuine PAIR as a constant from a PAIR used as a
      * source code property.
      */
    idio_vm_cs = idio_array (24000);
    idio_gc_protect_auto (idio_vm_cs);
    /*
     * The only "constant" we can't put in our idio_vm_ch
     * is #n (#n can't be a key in a hash) so plonk it in slot 0 so it
     * is a quick lookup when we fail to find the key in the hash.
     */
    idio_array_push (idio_vm_cs, idio_S_nil);

    /*
     * IDIO_HASH_COUNT (idio_vm_ch)
     * empty	=> 5k
     * test	=> 23k
     */
    idio_vm_ch = IDIO_HASH_EQUALP (8 * 1024);
    idio_gc_protect_auto (idio_vm_ch);

    /*
     * Start up and shutdown generates some 1761 values and running
     * the test suite generates 2034 values
     */
    idio_vm_vt = idio_array (3000);
    idio_gc_protect_auto (idio_vm_vt);

    /*
     * Start up and shutdown generates some 9815 src exprs/props and
     * running the test suite generates some 51928 src exprs/props
     * (yikes!)
     */
    idio_vm_ses = idio_array (12000);
    idio_gc_protect_auto (idio_vm_ses);

    idio_vm_sps = idio_array (12000);
    idio_gc_protect_auto (idio_vm_sps);

    idio_vm_krun = idio_array (4);
    idio_gc_protect_auto (idio_vm_krun);

    /*
     * Push a dummy value onto idio_vm_vt so that slot 0 is
     * unavailable.  We can then use 0 as a marker to say the value
     * needs to be dynamically referenced and the the 0 backfilled
     * with the true value.
     *
     * idio_S_undef is the value whereon the *_REF VM instructions do
     * a double-take
     */
    idio_array_push (idio_vm_vt, idio_S_undef);

#define IDIO_VM_STRING(c,s) idio_vm_ ## c ## _string = idio_string_C (s); idio_gc_protect_auto (idio_vm_ ## c ## _string);

    IDIO_VM_STRING (SYM_DEF,                  "SYM-DEF");
    IDIO_VM_STRING (SYM_DEF_gvi0,             "SYM-DEF/gvi=0");
    IDIO_VM_STRING (SYM_SET,                  "SYM-SET");
    IDIO_VM_STRING (SYM_SET_gvi0,             "SYM-SET/gvi=0");
    IDIO_VM_STRING (SYM_SET_predef,           "SYM-SET/predef");
    IDIO_VM_STRING (COMPUTED_SYM_DEF,         "COMPUTED-SYM-DEF");
    IDIO_VM_STRING (COMPUTED_SYM_DEF_gvi0,    "COMPUTED-SYM-DEF/gvi=0");
    IDIO_VM_STRING (EXPANDER,                 "EXPANDER");
    IDIO_VM_STRING (INFIX_OPERATOR,           "INFIX-OPERATOR");
    IDIO_VM_STRING (POSTFIX_OPERATOR,         "POSTFIX-OPERATOR");
    IDIO_VM_STRING (PUSH_DYNAMIC,             "PUSH-DYNAMIC");
    IDIO_VM_STRING (DYNAMIC_SYM_REF,          "DYNAMIC-SYM-REF");
    IDIO_VM_STRING (DYNAMIC_FUNCTION_SYM_REF, "DYNAMIC-FUNCTION-SYM-REF");
    IDIO_VM_STRING (PUSH_ENVIRON,             "PUSH-ENVIRON");
    IDIO_VM_STRING (ENVIRON_SYM_REF,          "ENVIRON-SYM-REF");

    IDIO_VM_STRING (anon,                     "-anon-");

    idio_all_code = idio_ia (500000);

    idio_codegen_code_prologue (idio_all_code);
    idio_prologue_len = IDIO_IA_USIZE (idio_all_code);

    /*
     * Having created the main VM tables, above, we can create the
     * first xenv (which implicitly uses them)
     */
    idio_xenv ();
}

typedef struct idio_vm_symbol_s {
    char *name;
    uint8_t value;
} idio_vm_symbol_t;

static idio_vm_symbol_t idio_vm_symbols[] = {
    { "A-PRIMCALL0", IDIO_A_PRIMCALL0 },
    { "A-PRIMCALL1", IDIO_A_PRIMCALL1 },
    { "A-PRIMCALL2", IDIO_A_PRIMCALL2 },
    { NULL, 0 }
};

void idio_vm_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (raise);
    IDIO_ADD_PRIMITIVE (reraise);
    IDIO_ADD_PRIMITIVE (apply);
    IDIO_ADD_PRIMITIVE (make_prompt_tag);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_dc_holes);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_dc_hole_push);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_dc_hole_pop);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_dc_make_hole);
    IDIO_ADD_PRIMITIVE (call_uc);
    IDIO_ADD_PRIMITIVE (call_dc);
    IDIO_ADD_PRIMITIVE (vm_continuations);
    IDIO_ADD_PRIMITIVE (vm_apply_continuation);
    IDIO_ADD_PRIMITIVE (vm_trace);
    IDIO_ADD_PRIMITIVE (vm_trace_all);
#ifdef IDIO_VM_DIS
    IDIO_ADD_PRIMITIVE (vm_dis);
#endif

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_run);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_constants);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_extend_values);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_src_exprs);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_values_ref);

    IDIO_ADD_PRIMITIVE (idio_thread_state);
    IDIO_ADD_PRIMITIVE (exit);
    IDIO_ADD_PRIMITIVE (set_exit_status);
    IDIO_ADD_PRIMITIVE (run_in_thread);
    IDIO_ADD_PRIMITIVE (vm_frame_tree);
    IDIO_ADD_PRIMITIVE (vm_trap_state);
    IDIO_ADD_PRIMITIVE (vm_add_xenv);
    IDIO_ADD_PRIMITIVE (vm_add_xenv_from_eenv);
}

void idio_final_vm ()
{
    /*
     * Run a GC in case someone is hogging all the file descriptors,
     * say, as we want to use one, at least.
     */
    idio_gc_collect_all ("final-vm");

    if (getpid () == idio_pid) {
#ifdef IDIO_DEBUG
	IDIO thr = idio_thread_current_thread ();
	fprintf (stderr, "final-vm: ");

	IDIO stack = IDIO_THREAD_STACK (thr);
	idio_sp_t ss = idio_array_size (stack);
	if (ss > 27) {
	    fprintf (stderr, "VM didn't finish cleanly with %zd > 27 entries on the stack\n", ss);
	    idio_vm_thread_state (thr);
	}
#endif

	if (idio_vm_tables) {
	    /*
	     * We deliberately test that broken struct instance and
	     * C/pointer printers generate ^rt-parameter-value-errors.
	     *
	     * Unfortunately, those values still exist which, as we're
	     * about to try to print them out again, here, is going to
	     * be a slight problem.
	     *
	     * Obviously, the same could be true for regular usage.
	     *
	     * We *could* establish a trap, right now, for
	     * ^rt-parameter-values-error and revel in some #<bad
	     * printer> messages or, alternatively, we could engineer
	     * the code to fall back on the default struct instance
	     * and C/pointer printers.
	     *
	     * The flag, idio_vm_tables is, clearly, toggled on the
	     * presence of the --vm-tables argument but we don't want
	     * this alternate behaviour to prevent
	     * ^rt-parameter-value-error being raised during run-time.
	     *
	     * So we need yet another flag for during VM reporting.
	     */
	    idio_vm_reporting = 1;
	    idio_vm_dump_all ();
	}

	if (idio_vm_reports) {
#ifdef IDIO_VM_PROF
#ifdef IDIO_DEBUG
	    fprintf (stderr, "vm-perf ");
#endif

	    FILE *vm_opcodes = fopen ("idio-vm-opcodes", "w");

	    uint64_t c = 0;
	    struct timespec t;
	    t.tv_sec = 0;
	    t.tv_nsec = 0;

	    for (IDIO_I i = 1; i < IDIO_I_MAX; i++) {
		c += idio_vm_ins_counters[i];
		t.tv_sec += idio_vm_ins_call_time[i].tv_sec;
		t.tv_nsec += idio_vm_ins_call_time[i].tv_nsec;
		if (t.tv_nsec >= IDIO_VM_NS) {
		    t.tv_nsec -= IDIO_VM_NS;
		    t.tv_sec += 1;
		}
	    }

	    float c_pct = 0;
	    float t_pct = 0;

	    fprintf (vm_opcodes, "%4.4s %-40.40s %8.8s %5.5s %15.15s %5.5s %6.6s\n", "code", "instruction", "count", "cnt%", "time (sec.nsec)", "time%", "ns/call");
	    for (IDIO_I i = 1; i < IDIO_I_MAX; i++) {
		if (1 || idio_vm_ins_counters[i]) {
		    char const *bc_name = idio_vm_bytecode2string (i);
		    if (strcmp (bc_name, "Unknown bytecode") ||
			idio_vm_ins_counters[i]) {
			float count_pct = 100.0 * idio_vm_ins_counters[i] / c;
			c_pct += count_pct;

			/*
			 * convert to 100ths of a second
			 */
			float t_time = t.tv_sec * 100 + t.tv_nsec / 10000000;
			float i_time = idio_vm_ins_call_time[i].tv_sec * 100 + idio_vm_ins_call_time[i].tv_nsec / 10000000;
			float time_pct = i_time * 100 / t_time;
			t_pct += time_pct;

			fprintf (vm_opcodes, "%4" PRIu8 " %-40s %8" PRIu64 " %5.1f %5jd.%09ld %5.1f",
				 i,
				 bc_name,
				 idio_vm_ins_counters[i],
				 count_pct,
				 (intmax_t) idio_vm_ins_call_time[i].tv_sec,
				 idio_vm_ins_call_time[i].tv_nsec,
				 time_pct);
			double call_time = 0;
			if (idio_vm_ins_counters[i]) {
			    call_time = (idio_vm_ins_call_time[i].tv_sec * IDIO_VM_NS + idio_vm_ins_call_time[i].tv_nsec) / idio_vm_ins_counters[i];
			}
			fprintf (vm_opcodes, " %6.f", call_time);
			fprintf (vm_opcodes, "\n");
		    }
		}
	    }
	    fprintf (vm_opcodes, "%4s %-38s %10" PRIu64 " %5.1f %5jd.%09ld %5.1f\n", "", "total", c, c_pct, (intmax_t) t.tv_sec, t.tv_nsec, t_pct);

	    fclose (vm_opcodes);
#endif
	}

#ifdef IDIO_DEBUG
	fprintf (stderr, "\n");
#endif
    }

    fclose (idio_tracing_FILE);
    idio_ia_free (idio_all_code);
    idio_all_code = NULL;
}

/*
 * deletion of idio_xenvs is delayed partly because some
 * idio_final_X() (notably module) want to drop out details from the
 * tables.
 */
void idio_final_xenv ()
{
    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_free_xenv (idio_xenvs[xi]);
    }
    if (idio_xenvs_size) {
	IDIO_GC_FREE (idio_xenvs, idio_xenvs_size * sizeof (idio_xenv_t *));
    }
}

void idio_init_vm ()
{
    idio_module_table_register (idio_vm_add_primitives, idio_final_vm, NULL);

    /*
     * Careful analysis:
     *
     * egrep CONSTANT-I?REF idio-vm-dasm* | sed -E 's/.*[0-9]: CONSTANT-I?REF +\.?[0-9]+ //' | sort | uniq -c | sort -n | tail -40
     *
     * suggests that, for common terms, to keep the ci varuints used
     * by these references to one byte (in practice, <240 -- see
     * idio_vm_fetch_varuint()), we should pre-fill the constants
     * array with things we know are going to get used.
     *
     * The vm-dasm should probably be the result of a regular start
     * and stop, say, idio --vm-reports empty, rather than a specific
     * run, say, the test suite.
     *
     * Having updated the list below, you can then look at the
     * subsequent constants references to see the effect.  Note that
     * other C idio_init_X() will have added 130-odd constants before
     * this code gets a look-in.
     *
     * NB The idio_S_X values are initialised after idio_vm_values() is
     * run, above, so we might as well add them all down here.  It
     * means they get shoved out to ~80th in the constants list (as
     * other modules have initialised before us) -- but that's well
     * within the 240 allowed for in the 1 byte varuint limit.
     */

    /* used in bootstrap */
    idio_vm_extend_default_constants (idio_S_block);
    idio_vm_extend_default_constants (idio_S_colon_eq);
    idio_vm_extend_default_constants (idio_S_cond);
    idio_vm_extend_default_constants (idio_S_define);
    idio_vm_extend_default_constants (idio_S_else);
    idio_vm_extend_default_constants (idio_S_eq);
    idio_vm_extend_default_constants (idio_S_error);
    idio_vm_extend_default_constants (idio_S_function);
    idio_vm_extend_default_constants (idio_S_if);
    idio_vm_extend_default_constants (idio_S_ph);
    idio_vm_extend_default_constants (idio_S_quote);
    idio_vm_extend_default_constants (idio_bignum_real_C ("0.0", 3));
    idio_vm_extend_default_constants (idio_bignum_real_C ("1.0", 3));
    idio_vm_extend_default_constants (IDIO_STRING ("\n"));
    idio_vm_extend_default_constants (IDIO_STRING ("closed application: (e)"));
    idio_vm_extend_default_constants (IDIO_STRING ("closed application: (end)"));
    idio_vm_extend_default_constants (IDIO_STRING ("closed application: (loop)"));
    idio_vm_extend_default_constants (IDIO_STRING ("closed application: (r)"));
    idio_vm_extend_default_constants (IDIO_STRING ("closed application: (start)"));
    idio_vm_extend_default_constants (IDIO_STRING ("closed application: (v)"));
    idio_vm_extend_default_constants (IDIO_STRING ("closed application: (x)"));
    idio_vm_extend_default_constants (IDIO_STRING ("invalid syntax"));
    idio_vm_extend_default_constants (IDIO_STRING ("not a char-set"));
    idio_vm_extend_default_constants (IDIO_STRING ("not a condition:"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("&args"));
    idio_vm_extend_default_constants (IDIO_SYMBOL (":"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("close"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("define-syntax"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("display"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("display*"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("ih"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("operator"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("pair?"));
    idio_vm_extend_default_constants (IDIO_SYMBOL ("seq"));

    idio_vm_module = idio_module (IDIO_SYMBOL ("vm"));

    idio_vm_t0 = time ((time_t *) NULL);

    idio_vm_signal_handler_name = idio_array (IDIO_LIBC_NSIG + 1);
    idio_gc_protect_auto (idio_vm_signal_handler_name);
    /*
     * idio_vm_run1() will be indexing anywhere into this array when
     * it gets a signal so make sure that the "used" size is up there
     * by putting a value in index NSIG.
     */
    idio_array_insert_index (idio_vm_signal_handler_name, idio_S_nil, (idio_ai_t) IDIO_LIBC_NSIG);

#ifdef IDIO_VM_PROF
    for (IDIO_I i = 1; i < IDIO_I_MAX; i++) {
	idio_vm_ins_call_time[i].tv_sec = 0;
	idio_vm_ins_call_time[i].tv_nsec = 0;
    }
#endif
    idio_tracing_FILE = stderr;
#ifdef IDIO_VM_DIS
    idio_dasm_FILE = stderr;
#endif

    idio_vm_symbol_t *cs = idio_vm_symbols;
    for (; cs->name != NULL; cs++) {
	/*
	 * The three existing elements of this array are all 11 bytes
	 * long so the following strnlen (..., 20), magic number,
	 * allows for some casual expansion.
	 *
	 * strnlen rather than idio_strnlen during bootstrap.
	 */
	IDIO sym = idio_symbols_C_intern (cs->name, strnlen (cs->name, 20));
	idio_module_export_symbol_value (sym, idio_fixnum (cs->value), idio_vm_module);
    }

    idio_vm_prompt_tag_type = idio_struct_type (IDIO_SYMBOL ("prompt-tag"),
						idio_S_nil,
						IDIO_LIST1 (IDIO_SYMBOL ("name")));
    idio_gc_protect_auto (idio_vm_prompt_tag_type);

    if (clock_gettime (CLOCK_MONOTONIC, &idio_vm_ts0) < 0) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ts)");
    }

    idio_S_cfw = IDIO_SYMBOL ("compile-file-writer");
}
