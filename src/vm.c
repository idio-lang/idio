/*
 * Copyright (c) 2015, 2017, 2018, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "c-type.h"
#include "closure.h"
#include "codegen.h"
#include "command.h"
#include "condition.h"
#include "continuation.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "job-control.h"
#include "keyword.h"
#include "libc-wrap.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "primitive.h"
#include "read.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"

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
 * Note that as you can start it deep inside some nested call
 * structure and have it continue to run until well outside that nest
 * then the indentation will be wrong.
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
static int idio_vm_tracing = 0;
static char *idio_vm_tracing_in = ">>>>>>>>>>>>>>>>>>>>>>>>>";
static char *idio_vm_tracing_out = "<<<<<<<<<<<<<<<<<<<<<<<<<";
#ifdef IDIO_VM_DIS
static int idio_vm_dis = 0;
#endif
FILE *idio_dasm_FILE;
int idio_vm_reports = 0;
int idio_vm_reporting = 0;

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
idio_ai_t idio_vm_FINISH_pc;
idio_ai_t idio_vm_NCE_pc;
idio_ai_t idio_vm_CHR_pc;
idio_ai_t idio_vm_IHR_pc;
idio_ai_t idio_vm_AR_pc;
size_t idio_prologue_len;

int idio_vm_exit = 0;
int idio_vm_virtualisation_WSL = 0;

/**
 * DOC: Some VM tables:
 *
 * constants - all known constants
 *
 *   we can't have numbers, strings, quoted values etc. embedded in
 *   compiled code -- as they are an unknown size which is harder work
 *   for a byte compiler -- but we can have a (known size) index into
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
 * values - all known toplevel values
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
 *   Note: MODULES
 *
 *   Modules blur the easy view of the world as described above for
 *   two reasons.  Firstly, if two modules both define a variable
 *   using the same *symbol* then we must have two distinct *values*
 *   in the VM.  However, we don't know which one of the values we are
 *   meant to use until the time we try to reference it because we
 *   don't know which modules have been set as imports.  So the
 *   variable referencing instructions use a symbol index which,
 *   together with the current environment and imports, we can use to
 *   figure out which of the values we are refering to.
 *
 *   At least, having got a value index, we can memo-ize it for future
 *   use!
 *
 *   Secondly, modules suggest library code, ie. something we can
 *   write out and load in again later.  This causes a similar but
 *   different problem.  If I compile a module it will use the next
 *   available (global) symbol index of the ucrrently running Idio
 *   instance and encode that in the compiled output.  If I compile
 *   another module in a different Idio instance then, because it will
 *   use the same next available (global) symbol index, those symbol
 *   indexes are virtually certain to be in conflict.  Symbol index ci
 *   was used to mean one symbol in one module and a different symbol
 *   in the other.  When a VM instruction uses symbol index ci which
 *   *symbol* should we be using for lookup in any imported modules?
 *
 *   So we need a plan for those two circumstances:
 *
 *     constants/symbols
 *
 *     When a module is compiled it will (can only!) use the next
 *     available (global) constant/symbol index of the currently
 *     running Idio instance and encode that into the compiled output.
 *     This is not an issue until the module is saved.  A subsequent
 *     module, compiled in another Idio instance might also use the
 *     same next available (global) constant/symbol index.  That will
 *     then conflict with the first module when it is loaded in.
 *
 *     Therefore, all module constant/symbol indexes are regarded as
 *     module-specific and there exists module-specific tables
 *     (IDIO_MODULE_VCI()) to contain the module-specific index ->
 *     global index mappings.
 *
 *       A popular alternative is to rewrite the IDIO_A_* codes so
 *       that on the first pass the original index is read, the
 *       correct index is calculated then the VM instructions are
 *       rewritten to use the correct index directly.
 *
 *       Self-modifying code firstly requires that we ensure there is
 *       enough room in the space used by the original instructions to
 *       fit the new instructions (and any difference is handled) but
 *       also prevents us performing any kind of code-corruption (or
 *       malign influence) detection.
 *
 *     These two mappings are created when a module is loaded and the
 *     module-specific constants/symbols are added to the VM:
 *     idio_vm_add_module_constants().
 *
 *       As a side-note: all Idio bootstrap code (and any interactivly
 *       input code) will not have been through
 *       idio_vm_add_module_constants() and therefore the mapping
 *       table will be empty.  As a consequence, for a given
 *       module-specific ci, if the mapping table is empty them we
 *       must assume that the ci is a global ci.
 *
 *     values
 *
 *     Values are a bit more tricky because the referencing
 *     instructions indicate which *symbol* we are trying to
 *     reference.  Once we've mapped the module-specific symbol index
 *     to a global one we then need to discover the instance of the
 *     *symbol* within our environment.  Modules are the problem here
 *     as we don't know whether a symbol has been defined in the
 *     current environment or a module it imports (or the Idio module)
 *     until the time we attempt the reference.
 *
 *     Only then can we make a permanent link between the module's
 *     *symbol* reference and the VM's global *value* index.
 *
 *     Subsequent references get the quicker idio_module_get_vvi()
 *     result.
 *
 *       Again, a popular alternative is to rewrite the IDIO_A_*
 *       codes.  This would result in an even-quicker lookup -- a
 *       putative IDIO_A_GLOBAL_SYM_REF_DIRECT (global-value-index)
 *       which could be a simple array lookup -- much faster than a
 *       hash table lookup (as idio_module_get_vvi() is).
 *
 *     Naming Scheme
 *
 *     To try to maintain the sense of things name variables
 *     appropriately: mci/mvi and gci/gvi for the module-specific and
 *     global versions of constant/value indexes.
 *
 *     There'll be f... variants: fgci == idio_fixnum (gci).
 *
 * closure_name -
 *
 *   if we see GLOBAL-SYM-SET {name} {CLOS} we can maintain a map from
 *   {CLOS} back to the name the closure was once defined as.  This is
 *   handy during a trace when a {name} is redefined -- which happens
 *   a lot.  The trace always prints the closure's ID (memory
 *   location) so you can see if a recursive call is actually
 *   recursive or calling a previous definition (which may be
 *   recursive).
 *
 *   When a reflective evaluator is implemented this table should go
 *   (as the details will be indexes to constants and embedded in the
 *   code).
 *
 * continuations -
 *
 *   each call to idio_vm_run() adds a new {krun} to this stack which
 *   gives the restart/reset condition handlers something to go back
 *   to.
 */
IDIO idio_vm_constants;
IDIO idio_vm_constants_hash;
static IDIO idio_vm_values;
IDIO idio_vm_krun;

static IDIO idio_vm_signal_handler_name;

static IDIO idio_vm_prompt_tag_type;

static time_t idio_vm_t0;

static IDIO idio_vm_get_or_create_vvi_string = idio_S_nil;
static IDIO idio_vm_GLOBAL_SYM_DEF_string = idio_S_nil;
static IDIO idio_vm_GLOBAL_SYM_DEF_gvi0_string = idio_S_nil;
static IDIO idio_vm_GLOBAL_SYM_SET_string = idio_S_nil;
static IDIO idio_vm_COMPUTED_SYM_DEF_string = idio_S_nil;
static IDIO idio_vm_EXPANDER_string = idio_S_nil;
static IDIO idio_vm_INFIX_OPERATOR_string = idio_S_nil;
static IDIO idio_vm_POSTFIX_OPERATOR_string = idio_S_nil;

static idio_ai_t idio_vm_get_or_create_vvi (idio_ai_t mci);

#ifdef IDIO_VM_PROF
static uint64_t idio_vm_ins_counters[IDIO_I_MAX];
static struct timespec idio_vm_ins_call_time[IDIO_I_MAX];
#endif

#define IDIO_THREAD_STACK_PUSH(v)	(idio_array_push (IDIO_THREAD_STACK(thr), v))
#define IDIO_THREAD_STACK_POP()		(idio_array_pop (IDIO_THREAD_STACK(thr)))

#define IDIO_VM_INVOKE_REGULAR_CALL	0
#define IDIO_VM_INVOKE_TAIL_CALL	1

static char *idio_vm_panicking = NULL;

void idio_final_vm ();

/*
 * Code coverage:
 *
 * We're not expecting to panic, right?
 */
void idio_vm_panic (IDIO thr, char const *m)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * Not reached!
     *
     *
     * Ha!  Yeah, I wish! ... :(
     */

    fprintf (stderr, "\n\nPANIC: %s\n\n", m);
    if (idio_vm_panicking) {
	fprintf (stderr, "VM already panicking for %s\n", idio_vm_panicking);
	exit (-2);
    } else {
	idio_vm_panicking = (char *) m;
	idio_vm_thread_state (thr);
	idio_final_vm();
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

    if (idio_vm_tracing) {
	idio_vm_function_trace (ins, thr);
    }

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
	    name = idio_string_C_len (IDIO_STATIC_STR_LEN ("-anon-"));
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
	name = idio_string_C_len (IDIO_STATIC_STR_LEN ("-anon-"));
    }

    IDIO sigstr = idio_ref_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

    IDIO val = IDIO_THREAD_VAL (thr);

    idio_display_C ("(", dsh);
    idio_display (name, dsh);
    idio_display_C (" ", dsh);
    idio_display (sigstr, dsh);
    idio_display_C (") was called as (", dsh);
    idio_display (name, dsh);
    IDIO args = idio_frame_params_as_list (val);
    while (idio_S_nil != args) {
	idio_display_C (" ", dsh);
	idio_display (IDIO_PAIR_H (args), dsh);
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

    if (idio_vm_tracing) {
	idio_vm_function_trace (ins, thr);
    }

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
	    name = idio_string_C_len (IDIO_STATIC_STR_LEN ("-anon-"));
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
	name = idio_string_C_len (IDIO_STATIC_STR_LEN ("-anon-"));
    }

    IDIO sigstr = idio_ref_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

    IDIO val = IDIO_THREAD_VAL (thr);

    idio_display_C ("(", dsh);
    idio_display (name, dsh);
    idio_display_C (" ", dsh);
    idio_display (sigstr, dsh);
    idio_display_C (") was called as (", dsh);
    idio_display (name, dsh);
    IDIO args = idio_frame_params_as_list (val);
    while (idio_S_nil != args) {
	idio_display_C (" ", dsh);
	idio_display (IDIO_PAIR_H (args), dsh);
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
static void idio_error_runtime_unbound (IDIO fmci, IDIO fgci, IDIO sym, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("no such binding", msh);

    idio_display_C ("mci ", dsh);
    idio_display (fmci, dsh);
    idio_display_C (" -> gci ", dsh);
    idio_display (fgci, dsh);

    idio_error_raise_cont (idio_condition_rt_variable_unbound_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

static void idio_error_dynamic_unbound (idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("no such dynamic binding", msh);

    idio_display_C ("mci ", dsh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, dsh);
    idio_display_C (" -> gci ", dsh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

    idio_error_raise_cont (idio_condition_rt_dynamic_variable_unbound_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

static void idio_error_environ_unbound (idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("no such environ binding", msh);

    idio_display_C ("mci ", dsh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, dsh);
    idio_display_C (" -> gci ", dsh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

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
static void idio_vm_error_computed (char const *msg, idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_display_C ("mci ", dsh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, dsh);
    idio_display_C (" -> gci ", dsh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

    idio_error_raise_cont (idio_condition_rt_computed_variable_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       sym));

    /* notreached */
}

static void idio_vm_error_computed_no_accessor (char const *msg, idio_ai_t mci, idio_ai_t gvi, IDIO c_location)
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

    idio_display_C ("mci ", dsh);
    IDIO fmci = idio_fixnum (mci);
    idio_display (fmci, dsh);
    idio_display_C (" -> gci ", dsh);
    IDIO fgci = idio_module_get_vci (idio_thread_current_env (), fmci);
    idio_display (fgci, dsh);
    idio_display_C (" -> gvi ", dsh);
    idio_display (idio_fixnum (gvi), dsh);

    IDIO sym = idio_S_unspec;
    if (idio_S_unspec != fgci) {
	sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
    }

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

    fprintf (stderr, "vm-debug: %s THR %10p\n", prefix, thr);
    idio_debug ("    src=%s\n", idio_vm_source_location ());
    fprintf (stderr, "     pc=%6td\n", IDIO_THREAD_PC (thr));
    idio_debug ("    val=%s\n", IDIO_THREAD_VAL (thr));
    idio_debug ("   reg1=%s\n", IDIO_THREAD_REG1 (thr));
    idio_debug ("   reg2=%s\n", IDIO_THREAD_REG2 (thr));

    IDIO fmci = IDIO_THREAD_EXPR (thr);
    if (idio_isa_fixnum (fmci)) {
	IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	IDIO src = idio_vm_constants_ref (gci);
	idio_debug ("   expr=%s", src);
	idio_debug ("   %s\n", idio_vm_source_location ());
    } else {
	idio_debug ("   expr=%s\n", fmci);
    }
    idio_debug ("   func=%s\n", IDIO_THREAD_FUNC (thr));
    idio_debug ("    env=%s\n", IDIO_THREAD_ENV (thr));
    idio_debug ("  frame=%s\n", IDIO_THREAD_FRAME (thr));

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_debug ("   t/sp=% 3s\n", IDIO_THREAD_TRAP_SP (thr));
    idio_debug ("   d/sp=% 3s\n", IDIO_THREAD_DYNAMIC_SP (thr));
    idio_debug ("   e/sp=% 3s\n", IDIO_THREAD_ENVIRON_SP (thr));
#endif

    idio_debug ("     in=%s\n", IDIO_THREAD_INPUT_HANDLE (thr));
    idio_debug ("    out=%s\n", IDIO_THREAD_OUTPUT_HANDLE (thr));
    idio_debug ("    err=%s\n", IDIO_THREAD_ERROR_HANDLE (thr));
    idio_debug ("    mod=%s\n", IDIO_THREAD_MODULE (thr));
    idio_debug ("  holes=%s\n", IDIO_THREAD_HOLES (thr));
    fprintf (stderr, "jmp_buf=%p\n", IDIO_THREAD_JMP_BUF (thr));
    fprintf (stderr, "\n");

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t stack_size = idio_array_size (stack);

    if (stack_start < 0) {
	stack_start += stack_size;
    }

    IDIO_C_ASSERT (stack_start < stack_size);

    /*
    idio_ai_t i;
    fprintf (stderr, "%s STK %zd:%zd\n", prefix, stack_start, stack_size - 1);
    if (stack_start) {
	fprintf (stderr, "  %3zd  ...\n", stack_start - 1);
    }
    for (i = stack_size - 1; i >= 0; i--) {
	fprintf (stderr, "  %3zd ", i);
	idio_debug ("%.100s\n", idio_array_ref_index (stack, i));
    }
    */

    idio_vm_decode_thread (thr);
}

static void idio_vm_invoke (IDIO thr, IDIO func, int tailp);

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
#define IDIO_IA_GET_NEXT(bc,pcp)	IDIO_IA_AE (bc, *(pcp)); (*pcp)++;
#define IDIO_THREAD_FETCH_NEXT(bc)	IDIO_IA_GET_NEXT (bc, &(IDIO_THREAD_PC(thr)))

static uint64_t idio_vm_read_fixuint (IDIO_IA_T bc, int const n, size_t const offset)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    /* fprintf (stderr, "ivrf: %d %zd\n", n, offset); */

    int i;
    uint64_t r = 0;
    for (i = 0; i < n; i++) {
	r <<= 8;
	r |= IDIO_IA_AE (bc, offset + i);
    }

    return r;
}

static uint64_t idio_vm_get_varuint (IDIO_IA_T bc, idio_ai_t *pcp)
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

static uint64_t idio_vm_get_fixuint (IDIO_IA_T bc, int n, idio_ai_t *pcp)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    uint64_t r = idio_vm_read_fixuint (bc, n, *pcp);
    *pcp += n;

    return r;
}

static uint64_t idio_vm_get_8uint (IDIO_IA_T bc, idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (bc, 1, pcp);
}

static uint64_t idio_vm_get_16uint (IDIO_IA_T bc, idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (bc, 2, pcp);
}

static uint64_t idio_vm_get_32uint (IDIO_IA_T bc, idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (bc, 4, pcp);
}

static uint64_t idio_vm_get_64uint (IDIO_IA_T bc, idio_ai_t *pcp)
{
    return idio_vm_get_fixuint (bc, 8, pcp);
}

static uint64_t idio_vm_fetch_varuint (IDIO thr)
{
    return idio_vm_get_varuint (idio_all_code, &(IDIO_THREAD_PC (thr)));
}

static uint64_t idio_vm_fetch_fixuint (IDIO_IA_T bc, int n, IDIO thr)
{
    return idio_vm_get_fixuint (bc, n, &(IDIO_THREAD_PC (thr)));
}

static uint64_t idio_vm_fetch_8uint (IDIO thr, IDIO_IA_T bc)
{
    return idio_vm_fetch_fixuint (bc, 1, thr);
}

static uint64_t idio_vm_fetch_16uint (IDIO thr, IDIO_IA_T bc)
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
 * Check this aligns with IDIO_IA_PUSH_REF in codegen.c
 */
#define IDIO_VM_FETCH_REF(bc,t)	(idio_vm_fetch_16uint (bc,t))
#define IDIO_VM_GET_REF(bc,pcp)	(idio_vm_get_16uint (bc,pcp))

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

#ifdef IDIO_VM_DYNAMIC_REGISTERS
		       6,
		       IDIO_THREAD_ENVIRON_SP (thr),
		       IDIO_THREAD_DYNAMIC_SP (thr),
		       IDIO_THREAD_TRAP_SP (thr),
#else
		       3,
#endif

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

    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

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

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_THREAD_TRAP_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));

    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));
    if (tsp < 3) {
	/*
	 * As we've just ascertained we don't have a condition handler
	 * this will end in even more tears...
	 */
	idio_coding_error_C ("bad TRAP SP: < 3", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (tsp >= ss) {
	idio_coding_error_C ("bad TRAP SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    ss--;

    IDIO_THREAD_DYNAMIC_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_DYNAMIC_SP (thr));
    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
    if (dsp >= ss) {
	idio_coding_error_C ("bad DYNAMIC SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    ss--;

    IDIO_THREAD_ENVIRON_SP (thr) = IDIO_THREAD_STACK_POP ();

    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_ENVIRON_SP (thr));
    if (esp >= ss) {
	idio_coding_error_C ("bad ENVIRON SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
#endif
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
	       idio_isa_continuation (IDIO_THREAD_FUNC (thr)))) {
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

	    /* idio_debug ("iv-ras: func is not invokable: %s\n", IDIO_THREAD_FUNC (thr)); */
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
    if (IDIO_CLOSURE_CALL_TIME (idio_vm_clos).tv_nsec > IDIO_VM_NS) {
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
    if (IDIO_CLOSURE_RU_UTIME (idio_vm_clos).tv_usec > IDIO_VM_US) {
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
    if (IDIO_CLOSURE_RU_STIME (idio_vm_clos).tv_usec > IDIO_VM_US) {
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
	    if (IDIO_PRIMITIVE_CALL_TIME (func).tv_nsec > IDIO_VM_NS) {
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
	    if (IDIO_PRIMITIVE_RU_UTIME (func).tv_usec > IDIO_VM_US) {
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
	    if (IDIO_PRIMITIVE_RU_STIME (func).tv_usec > IDIO_VM_US) {
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

static void idio_vm_primitive_call_trace (char const *name, IDIO thr, int nargs);
static void idio_vm_primitive_result_trace (IDIO thr);

static void idio_vm_invoke (IDIO thr, IDIO func, int tailp)
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
		IDIO_THREAD_STACK_PUSH (idio_SM_return);
	    }

	    IDIO_THREAD_FRAME (thr) = IDIO_CLOSURE_FRAME (func);
	    IDIO_THREAD_ENV (thr) = IDIO_CLOSURE_ENV (func);
	    IDIO_THREAD_PC (thr) = IDIO_CLOSURE_CODE_PC (func);

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
	     * PC shenanigans for primitives.
	     *
	     * If we are not in tail position then we should push the
	     * current PC onto the stack so that when the invoked code
	     * calls RETURN it will return to whomever called us -- as
	     * the CLOSURE code does above.
	     *
	     * By and large, though, primitives do not change the PC
	     * as they are entirely within the realm of C.  So we
	     * don't really care if they were called in tail position
	     * or not they just do whatever and set VAL.
	     *
	     * However, (apply proc & args) will prepare some
	     * function which may well be a closure which *will*
	     * alter the PC.  (As an aside, apply always invokes proc
	     * in tail position -- as proc *is* in tail position from
	     * apply's point of view).  The closure will, of course,
	     * RETURN to whatever is on top of the stack.
	     *
	     * But we haven't put anything there because this is a
	     * primitive and primitives don't change the PC...
	     *
	     * So, if, after invoking the primitive, the PC has
	     * changed (ie. apply prepared a closure which has set the
	     * PC ready to run the closure when we return from here)
	     * *and* we are not in tail position then we push the
	     * saved pc0 onto the stack.
	     *
	     * NB. If you push PC before calling the primitive (with a
	     * view to popping it off if the PC didn't change) and the
	     * primitive calls idio_raise_condition then there is an
	     * extraneous PC on the stack.
	     */
	    size_t pc0 = IDIO_THREAD_PC (thr);
	    IDIO val = IDIO_THREAD_VAL (thr);

	    IDIO last = IDIO_FRAME_ARGS (val, IDIO_FRAME_NPARAMS (val));
	    /* IDIO_FRAME_NPARAMS (val) -= 1; */

	    /* fprintf (stderr, "iv-invoke %20s %2td\n", IDIO_PRIMITIVE_NAME (func), IDIO_FRAME_NPARAMS (val)); */
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
	    size_t pc = IDIO_THREAD_PC (thr);

	    if (0 == tailp &&
		pc != pc0) {
		IDIO_THREAD_STACK_PUSH (idio_fixnum (pc0));
		IDIO_THREAD_STACK_PUSH (idio_SM_return);
	    }

	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }

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
    default:
	{
	    /*
	     * Test Case: vm-errors/idio_vm_invoke-bad-type.idio
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
 * Or we can stash the current PC on the stack and preserve
 * *everything*.
 *
 * The only problem here is inconvenient conditions.
 */
IDIO idio_vm_invoke_C (IDIO thr, IDIO command)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (command);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
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
	    idio_ai_t fai;
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
		idio_vm_run_C (thr, IDIO_THREAD_PC (thr));
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
	    idio_vm_run_C (thr, IDIO_THREAD_PC (thr));
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
	idio_debug ("iv-invoke-C: marker: expected idio_SM_return not %s\n", marker);
	IDIO_THREAD_STACK_PUSH (marker);
	idio_vm_panic (thr, "iv-invoke-C: unexpected stack marker");
    }
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());

    return r;
}

static idio_ai_t idio_vm_find_stack_marker (IDIO stack, IDIO mark, idio_ai_t from, idio_ai_t max)
{
    IDIO_ASSERT (stack);
    IDIO_ASSERT (mark);

    IDIO_TYPE_ASSERT (array, stack);

    idio_ai_t sp = idio_array_size (stack) - 1;
    if (sp < 0) {
	return sp;
    }

    if (from) {
	if (from < 0 ||
	    from > sp) {
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "find-stack-marker: from %td out of range: 0 - %td", from, sp);

	    idio_coding_error_C (em, mark, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	}
	sp = from;
    }

    if (max) {
	max = 0;
	idio_ai_t max_next = 0;
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

IDIO idio_vm_add_dynamic (IDIO m, IDIO ci, IDIO vi, IDIO note)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (ci);
    IDIO_ASSERT (vi);
    IDIO_ASSERT (note);

    IDIO_TYPE_ASSERT (module, m);
    IDIO_TYPE_ASSERT (fixnum, ci);
    IDIO_TYPE_ASSERT (fixnum, vi);
    IDIO_TYPE_ASSERT (string, note);

    idio_module_set_vci (m, ci, ci);
    idio_module_set_vvi (m, ci, vi);
    return IDIO_LIST5 (idio_S_dynamic, ci, vi, m, note);
}

static void idio_vm_push_dynamic (IDIO thr, idio_ai_t gvi, IDIO val)
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
     * n-3 next-dyn-sp of mark
     */

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_array_push (stack, IDIO_THREAD_DYNAMIC_SP (thr));
    IDIO_THREAD_DYNAMIC_SP (thr) = idio_fixnum (idio_array_size (stack) + 2);
#else
    idio_ai_t dsp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);
    if (dsp >= 3) {
	idio_array_push (stack, idio_array_ref_index (stack, dsp - 3));
    } else {
	idio_array_push (stack, idio_fixnum (-1));
    }
#endif

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

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_THREAD_DYNAMIC_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_DYNAMIC_SP (thr));
#else
    IDIO_THREAD_STACK_POP ();
#endif
}

IDIO idio_vm_dynamic_ref (IDIO thr, idio_ai_t mci, idio_ai_t gvi, IDIO args)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (list, args);

    IDIO stack = IDIO_THREAD_STACK (thr);

#ifdef IDIO_VM_DYNAMIC_REF
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
#else
    idio_ai_t sp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);
#endif

    IDIO v = idio_S_undef;

    for (;;) {
	if (sp >= 3) {
	    IDIO sv = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		v = idio_array_ref_index (stack, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    v = idio_vm_values_ref (gvi);
	    break;
	}
    }

    if (idio_S_undef == v) {
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
	    idio_error_dynamic_unbound (mci, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return IDIO_PAIR_H (args);
	}
    }

    return v;
}

void idio_vm_dynamic_set (IDIO thr, idio_ai_t mci, idio_ai_t gvi, IDIO v)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
#else
    idio_ai_t sp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);
#endif

    for (;;) {
	if (sp >= 3) {
	    IDIO sv = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		idio_array_insert_index (stack, v, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    idio_array_insert_index (idio_vm_values, v, gvi);
	    break;
	}
    }
}

IDIO idio_vm_add_environ (IDIO m, IDIO ci, IDIO vi, IDIO note)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (ci);
    IDIO_ASSERT (vi);
    IDIO_ASSERT (note);

    IDIO_TYPE_ASSERT (module, m);
    IDIO_TYPE_ASSERT (fixnum, ci);
    IDIO_TYPE_ASSERT (fixnum, vi);
    IDIO_TYPE_ASSERT (string, note);

    idio_module_set_vci (m, ci, ci);
    idio_module_set_vvi (m, ci, vi);
    return IDIO_LIST5 (idio_S_environ, ci, vi, m, note);
}

static void idio_vm_push_environ (IDIO thr, idio_ai_t mci, idio_ai_t gvi, IDIO val)
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
     * n-3 next-env-sp of mark
     */

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_array_push (stack, IDIO_THREAD_ENVIRON_SP (thr));
    IDIO_THREAD_ENVIRON_SP (thr) = idio_fixnum (idio_array_size (stack) + 2);
#else
    idio_ai_t esp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);
    if (esp >= 3) {
	idio_array_push (stack, idio_array_ref_index (stack, esp - 3));
    } else {
	idio_array_push (stack, idio_fixnum (-1));
    }
#endif

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

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_THREAD_ENVIRON_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_ENVIRON_SP (thr));
#else
    IDIO_THREAD_STACK_POP ();
#endif
}

IDIO idio_vm_environ_ref (IDIO thr, idio_ai_t mci, idio_ai_t gvi, IDIO args)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (list, args);

    IDIO stack = IDIO_THREAD_STACK (thr);

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
#else
    idio_ai_t sp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);
#endif

    IDIO v = idio_S_undef;

    for (;;) {
	if (sp >= 3) {
	    IDIO sv = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		v = idio_array_ref_index (stack, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    v = idio_vm_values_ref (gvi);
	    break;
	}
    }

    if (idio_S_undef == v) {
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
	    idio_error_environ_unbound (mci, gvi, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    return IDIO_PAIR_H (args);
	}
    }

    return v;
}

void idio_vm_environ_set (IDIO thr, idio_ai_t mci, idio_ai_t gvi, IDIO v)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
#else
    idio_ai_t sp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);
#endif

    for (;;) {
	if (sp >= 3) {
	    IDIO sv = idio_array_ref_index (stack, sp - 1);
	    IDIO_TYPE_ASSERT (fixnum, sv);

	    if (IDIO_FIXNUM_VAL (sv) == gvi) {
		idio_array_insert_index (stack, v, sp - 2);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, sp - 3));
	    }
	} else {
	    idio_array_insert_index (idio_vm_values, v, gvi);
	    break;
	}
    }
}

IDIO idio_vm_computed_ref (IDIO thr, idio_ai_t mci, idio_ai_t gvi)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO gns = idio_array_ref_index (idio_vm_values, gvi);

    if (idio_isa_pair (gns)) {
	IDIO get = IDIO_PAIR_H (gns);
	if (idio_isa_primitive (get) ||
	    idio_isa_closure (get)) {
	    return idio_vm_invoke_C (thr, IDIO_LIST1 (get));
	} else {
	    /*
	     * Test Case: XXX computed-errors/idio_vm_computed_ref-no-get-accessor.idio
	     *
	     * cvar-so :$ #n (function (v) v)
	     * cvar-so
	     */
	    idio_vm_error_computed_no_accessor ("get", mci, gvi, IDIO_C_FUNC_LOCATION ());

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
	idio_vm_error_computed ("no get/set accessors", mci, gvi, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

IDIO idio_vm_computed_set (IDIO thr, idio_ai_t mci, idio_ai_t gvi, IDIO v)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO gns = idio_array_ref_index (idio_vm_values, gvi);

    if (idio_isa_pair (gns)) {
	IDIO set = IDIO_PAIR_T (gns);
	if (idio_isa_primitive (set) ||
	    idio_isa_closure (set)) {
	    return idio_vm_invoke_C (thr, IDIO_LIST2 (set, v));
	} else {
	    /*
	     * Test Case: XXX computed-errors/idio_vm_computed_set-no-set-accessor.idio
	     *
	     * cvar-go :$ (function #n "got me") #n
	     * cvar-go = 3
	     */
	    idio_vm_error_computed_no_accessor ("set", mci, gvi, IDIO_C_FUNC_LOCATION ());

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
	idio_vm_error_computed ("no get/set accessors", mci, gvi, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_notreached;
}

void idio_vm_computed_define (IDIO thr, idio_ai_t mci, idio_ai_t gvi, IDIO v)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);

    IDIO_TYPE_ASSERT (pair, v);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_array_insert_index (idio_vm_values, v, gvi);
}

void idio_vm_push_trap (IDIO thr, IDIO handler, IDIO fmci, idio_ai_t next)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (handler);
    IDIO_ASSERT (fmci);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (fixnum, fmci);

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
     * n-3 next-trap-sp of mark
     */

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_array_push (stack, IDIO_THREAD_TRAP_SP (thr));
    IDIO_THREAD_TRAP_SP (thr) = idio_fixnum (idio_array_size (stack) + 2);
#else
    idio_ai_t tsp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0);
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
#endif

    idio_array_push (stack, fmci);
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
    IDIO_THREAD_STACK_POP ();	/* fmci */

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_THREAD_TRAP_SP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
#else
    IDIO_THREAD_STACK_POP ();
#endif
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

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_THREAD_TRAP_SP (thr) = trap_sp;
#endif
}

void idio_vm_push_escaper (IDIO thr, IDIO fmci, idio_ai_t offset)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (fmci);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (fixnum, fmci);

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
    idio_array_push (stack, fmci);
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
    IDIO_THREAD_STACK_POP ();	/* fmci */
    IDIO_THREAD_STACK_POP ();	/* frame */
    IDIO_THREAD_STACK_POP ();	/* offset */
}

void idio_vm_escaper_label_ref (IDIO thr, IDIO fmci)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (fmci);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (fixnum, fmci);

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
    idio_ai_t escaper_sp = idio_array_size (stack);
    while (!done &&
	   escaper_sp >= 0) {
	escaper_sp--;
	escaper_sp = idio_vm_find_stack_marker (stack, idio_SM_escaper, escaper_sp, 0);
	if (escaper_sp >= 0) {
	    if (idio_array_ref_index (stack, escaper_sp - 1) == fmci) {
		done = 1;
	    }
	}
    }

    if (! done) {
	IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	IDIO_TYPE_ASSERT (symbol, sym);

	idio_error_runtime_unbound (fmci, fgci, sym, IDIO_C_FUNC_LOCATION ())	;

	/* notreached */
	return;
    }

    IDIO_THREAD_FRAME (thr) = idio_array_ref_index (stack, escaper_sp - 2);
    IDIO offset = idio_array_ref_index (stack, escaper_sp - 3);
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (offset);

    /*
     * Remove references above us for good house-keeping
     */
    idio_ai_t ai = idio_array_size (stack);
    for (ai--; ai >= escaper_sp - 3; ai--) {
	/* idio_array_insert_index (stack, idio_S_nil, ai); */
    }
    IDIO_ARRAY_USIZE (stack) = escaper_sp - 3;
}

void idio_vm_raise_condition (IDIO continuablep, IDIO condition, int IHR, int reraise)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (condition);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    /* idio_debug ("raise-condition: %s\n", condition); */

    IDIO thr = idio_thread_current_thread ();

    IDIO stack = IDIO_THREAD_STACK (thr);

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO otrap_sp = IDIO_THREAD_TRAP_SP (thr);
#else
    IDIO otrap_sp = idio_fixnum (idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0));
#endif
    idio_ai_t trap_sp = IDIO_FIXNUM_VAL (otrap_sp);

    if (reraise) {
#ifdef IDIO_VM_DYNAMIC_REGISTERS
	trap_sp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0);
#else
	trap_sp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 1);
#endif
    }

    if (trap_sp >= idio_array_size (stack)) {
	idio_vm_thread_state (thr);
	idio_vm_panic (thr, "trap SP >= sizeof (stack)");
    }
    if (trap_sp < 3) {
	fprintf (stderr, "trap_sp = %td\n", trap_sp);
	idio_vm_panic (thr, "trap SP < 3");
    }

    /*
     * This feels mildy expensive: The trap call will say:
     *
     * trap COND-TYPE-NAME handler body
     *
     * So what we have in our hands is an index, mci, into the
     * constants table which we can lookup, idio_vm_constants_ref, to
     * get a symbol, COND-TYPE-NAME.  We now need to look that up,
     * idio_module_symbol_value_recurse, to get a value, trap_ct.
     *
     * We now need to determine if the actual condition isa trap_ct.
     */
    IDIO trap_ct_mci;
    IDIO handler;
    while (1) {
	handler = idio_array_ref_index (stack, trap_sp - 1);
	trap_ct_mci = idio_array_ref_index (stack, trap_sp - 2);
	IDIO ftrap_sp_next = idio_array_ref_index (stack, trap_sp - 3);

	IDIO trap_ct_sym = idio_vm_constants_ref ((idio_ai_t) IDIO_FIXNUM_VAL (trap_ct_mci));
	IDIO trap_ct = idio_module_symbol_value_recurse (trap_ct_sym, IDIO_THREAD_ENV (thr), idio_S_nil);

	if (idio_S_unspec == trap_ct) {
	    idio_vm_panic (thr, "trap condition type is unspec??");
	}

	idio_ai_t trap_sp_next = IDIO_FIXNUM_VAL (ftrap_sp_next);
	IDIO_C_ASSERT (trap_sp_next < idio_array_size (stack));

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
     * prologue we set the PC for the RETURNee.
     */
    int tailp = IDIO_VM_INVOKE_TAIL_CALL;
    if (1 || isa_closure) {
	idio_array_push (stack, idio_fixnum (IDIO_THREAD_PC (thr)));
	idio_array_push (stack, idio_SM_return); /* for the RETURNs */
	if (IHR) {
	    /* tailp = IDIO_VM_INVOKE_REGULAR_CALL; */
	    idio_vm_preserve_all_state (thr); /* for RESTORE-ALL-STATE */

#ifndef IDIO_VM_DYNAMIC_REGISTERS
	    idio_ai_t next_tsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, trap_sp - 3));
	    idio_vm_push_trap (thr,
			       idio_array_ref_index (stack, next_tsp - 1),
			       idio_array_ref_index (stack, next_tsp - 2),
			       IDIO_FIXNUM_VAL (idio_array_ref_index (stack, next_tsp - 3)));
#endif

	    if (isa_closure) {
		idio_array_push (stack, idio_fixnum (idio_vm_IHR_pc)); /* => (POP-TRAP) RESTORE-ALL-STATE, RETURN */
		idio_array_push (stack, idio_SM_return);
	    } else {
		IDIO_THREAD_PC (thr) = idio_vm_IHR_pc; /* => (POP-TRAP) RESTORE-ALL-STATE, RETURN */
	    }
	} else {
	    idio_vm_preserve_state (thr);      /* for RESTORE-STATE */

#ifdef IDIO_VM_DYNAMIC_REGISTERS
	    idio_array_push (stack, otrap_sp); /* for RESTORE-TRAP */
#else
	    idio_ai_t next_tsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, trap_sp - 3));
	    idio_vm_push_trap (thr,
			       idio_array_ref_index (stack, next_tsp - 1),
			       idio_array_ref_index (stack, next_tsp - 2),
			       IDIO_FIXNUM_VAL (idio_array_ref_index (stack, next_tsp - 3)));
#endif

	    if (idio_S_true == continuablep) {
		if (isa_closure) {
		    idio_array_push (stack, idio_fixnum (idio_vm_CHR_pc)); /* => POP/RESTORE-TRAP, RESTORE-STATE, RETURN */
		    idio_array_push (stack, idio_SM_return);
		} else {
		    IDIO_THREAD_PC (thr) = idio_vm_CHR_pc;
		}
	    } else {
		if (isa_closure) {
		    idio_array_push (stack, idio_fixnum (idio_vm_NCE_pc)); /* => NON-CONT-ERR */
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

    /*
     * We need to run this code in the care of the next handler on the
     * stack (not the current handler).  Unless the next handler is
     * the base handler in which case it gets reused (ad infinitum).
     *
     * We can do that easily by just changing IDIO_THREAD_TRAP_SP
     * but if that handler RETURNs then we must restore the current
     * handler.
     */
#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_THREAD_TRAP_SP (thr) = idio_array_ref_index (stack, trap_sp - 3);
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
#endif

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

    size_t nargs = idio_list_length (args);
    size_t size = nargs;

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
    IDIO k_stack = IDIO_CONTINUATION_STACK (k);
    if (IDIO_CONTINUATION_FLAGS (k) & IDIO_CONTINUATION_FLAG_DELIMITED) {
	fprintf (stderr, "KD ss->%" PRIdPTR "\n", IDIO_FIXNUM_VAL (k_stack));
	if (IDIO_ARRAY_USIZE (IDIO_THREAD_STACK (thr)) < IDIO_FIXNUM_VAL (k_stack)) {
	    fprintf (stderr, "KD >%td\n", IDIO_ARRAY_USIZE (IDIO_THREAD_STACK (thr)));
	    idio_vm_thread_state (thr);
	    IDIO_C_ASSERT (0);
	}
	IDIO_ARRAY_USIZE (IDIO_THREAD_STACK (thr)) = IDIO_FIXNUM_VAL (k_stack);
    } else {
	idio_ai_t al = idio_array_size (k_stack);

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

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_THREAD_ENVIRON_SP (thr)	= IDIO_CONTINUATION_ENVIRON_SP (k);
    IDIO_THREAD_DYNAMIC_SP (thr)	= IDIO_CONTINUATION_DYNAMIC_SP (k);
    IDIO_THREAD_TRAP_SP (thr)		= IDIO_CONTINUATION_TRAP_SP (k);
#endif

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
	fprintf (stderr, "%%vm-apply-continuation: restoring krun #%td: ", krun_p);
	idio_debug ("%s\n", IDIO_PAIR_HT (krun));
	idio_vm_restore_continuation (IDIO_PAIR_H (krun), val);

	return idio_S_notreached;
    }

    idio_coding_error_C ("failed to invoke contunation", IDIO_LIST2 (n, val), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%%vm-trace", vm_trace, (IDIO level), "level", "\
set VM tracing to `level`				\n\
							\n\
:param level: new VM tracing level			\n\
:type level: fixnum					\n\
							\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (level);

    /*
     * Test Case: vm-errors/vm-trace-bad-type.idio
     *
     * %%vm-trace #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, level);

    idio_vm_tracing = IDIO_FIXNUM_VAL (level);

    return idio_S_unspec;
}

#ifdef IDIO_VM_DIS
IDIO_DEFINE_PRIMITIVE1_DS ("%%vm-dis", vm_dis, (IDIO dis), "dis", "\
set VM live disassembly to to `dis`			\n\
							\n\
:param dis: new VM live disassembly setting		\n\
:type dis: fixnum					\n\
							\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (dis);

    /*
     * Test Case: ??
     *
     * VM_DIS is not a feature!
     */
    IDIO_USER_TYPE_ASSERT (fixnum, dis);

    idio_vm_dis = IDIO_FIXNUM_VAL (dis);

    return idio_S_unspec;
}

#define IDIO_VM_RUN_DIS(...)	if (idio_vm_dis) { fprintf (stderr, __VA_ARGS__); }
#else
#define IDIO_VM_RUN_DIS(...)
#endif

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
IDIO idio_vm_closure_name (IDIO c)
{
    IDIO_ASSERT (c);
    IDIO_TYPE_ASSERT (closure, c);

    return idio_ref_property (c, idio_KW_name, IDIO_LIST1 (idio_S_nil));
}

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
static void idio_vm_function_trace (IDIO_I ins, IDIO thr)
{
    IDIO func = IDIO_THREAD_FUNC (thr);
    IDIO val = IDIO_THREAD_VAL (thr);
    IDIO args = idio_frame_params_as_list (val);
    IDIO expr = idio_list_append2 (IDIO_LIST1 (func), args);

#ifdef IDIO_VM_PROF
    struct timespec ts;
    if (clock_gettime (CLOCK_MONOTONIC, &ts) < 0) {
	perror ("clock_gettime (CLOCK_MONOTONIC, ts)");
    }
#endif

    /*
     * %9d	- clock ns
     * SPACE
     * %7zd	- PC of ins
     * SPACE
     * %40s	- lexical information
     * %*s	- trace-depth indent
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %s	- expression
     */

#ifdef IDIO_VM_PROF
    fprintf (stderr, "%09ld ", ts.tv_nsec);
#endif
    fprintf (stderr, "%7td ", IDIO_THREAD_PC (thr) - 1);

    IDIO lo_sh = idio_open_output_string_handle_C ();
    IDIO fmci = IDIO_THREAD_EXPR (thr);
    if (idio_isa_fixnum (fmci)) {
	IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	IDIO src = idio_vm_constants_ref (gci);

	if (idio_isa_pair (src)) {
	    IDIO lo = idio_hash_ref (idio_src_properties, src);
	    if (idio_S_unspec == lo){
		idio_display (lo, lo_sh);
	    } else {
		idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME), lo_sh);
		idio_display_C (":line ", lo_sh);
		idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE), lo_sh);
	    }
	} else {
	    idio_display (src, lo_sh);
	    idio_display_C (" !pair", lo_sh);
	}
    } else {
	idio_display (fmci, lo_sh);
    }
    idio_debug ("%-40s", idio_get_output_string (lo_sh));

    fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_in);

    int isa_closure = idio_isa_closure (func);

    if (isa_closure) {
	IDIO name = idio_ref_property (func, idio_KW_name, IDIO_LIST1 (idio_S_nil));
	IDIO sigstr = idio_ref_property (func, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));

	if (idio_S_nil != name) {
	    size_t size = 0;
	    char *s = idio_display_string (name, &size);
	    fprintf (stderr, "(%s", s);
	    IDIO_GC_FREE (s, size);
	} else {
	    fprintf (stderr, "(-anon-");
	}
	if (idio_S_nil != sigstr) {
	    size_t size = 0;
	    char *s = idio_display_string (sigstr, &size);
	    fprintf (stderr, " %s", s);
	    IDIO_GC_FREE (s, size);
	}
	fprintf (stderr, ") was ");
    } else if (idio_isa_primitive (func)) {
    }

    if (isa_closure) {
	switch (ins) {
	case IDIO_A_FUNCTION_GOTO:
	    fprintf (stderr, "tail-called as\n");
	    break;
	case IDIO_A_FUNCTION_INVOKE:
	    fprintf (stderr, "called as\n");
	    break;
	}

	fprintf (stderr, "%9s ", "");
	fprintf (stderr, "%7s ", "");
	fprintf (stderr, "%40s", "");

	fprintf (stderr, "%*s  ", idio_vm_tracing, "");
    }

    idio_debug ("%s", expr);
    fprintf (stderr, "\n");
}

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
static void idio_vm_primitive_call_trace (char const *name, IDIO thr, int nargs)
{
    /*
     * %7zd	- PC of ins
     * SPACE
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent (>= 1)
     * %s	- expression
     */
    fprintf (stderr, "%9s ", "");
    fprintf (stderr, "%7td ", IDIO_THREAD_PC (thr) - 1);
    fprintf (stderr, "%40s", "");

    /* fprintf (stderr, "        __primcall__    "); */

    fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_in);
    fprintf (stderr, "(%s", name);
    if (nargs > 1) {
	idio_debug (" %s", IDIO_THREAD_REG1 (thr));
    }
    if (nargs > 0) {
	idio_debug (" %s", IDIO_THREAD_VAL (thr));
    }
    fprintf (stderr, ")\n");
}

/*
 * Code coverage:
 *
 * Used by the tracer.
 */
static void idio_vm_primitive_result_trace (IDIO thr)
{
    IDIO val = IDIO_THREAD_VAL (thr);

    /*
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent (>= 1)
     * %s	- expression
     */
    /* fprintf (stderr, "                                "); */

    fprintf (stderr, "%9s ", "");
    fprintf (stderr, "%7td ", IDIO_THREAD_PC (thr));
    fprintf (stderr, "%40s", "");
    fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_out);
    idio_debug ("%s\n", val);
}

/*
 * Code coverage:
 *
 * Used by the loadable-objects code.
 */
/*
 * When a module is written out it's constants will have arbitrary
 * module-specific indexes and so the representation must be
 * key:value, ie. a hash
 */
void idio_vm_add_module_constants (IDIO module, IDIO constants)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (constants);
    IDIO_TYPE_ASSERT (module, module);

    if (idio_S_nil == constants) {
	return;
    }

    IDIO_TYPE_ASSERT (array, constants);

    idio_ai_t i;
    idio_ai_t al = idio_array_size (constants);

    for (i = 0; i < al; i++) {
	IDIO c = idio_array_ref_index (constants, i);
	if (idio_S_nil != c) {
	    idio_ai_t gci = idio_vm_constants_lookup_or_extend (c);
	    idio_module_set_vci (module, idio_fixnum (i), idio_fixnum (gci));
	}
    }
}

static idio_ai_t idio_vm_get_or_create_vvi (idio_ai_t mci)
{
    IDIO fmci = idio_fixnum (mci);

    IDIO ce = idio_thread_current_env ();

    IDIO fgvi = idio_module_get_vvi (ce, fmci);
    /*
     * NB 0 is the placeholder value index (see idio_init_vm_values())
     */
    idio_ai_t gvi = 0;

    if (idio_S_unspec == fgvi ||
	0 == IDIO_FIXNUM_VAL (fgvi)) {
	/*
	 * This is the first time we have looked for mci in this module
	 * and we have failed to find a vvi, ie. fast-lookup, mapping
	 * to a value index.  So we need to find the symbol in the
	 * current environment (or its imports) and set the mapping up
	 * for future calls.
	 *
	 * Remember this could be a ref of something that has never
	 * been set.  In a pure programming language that would be an
	 * error.  However, in Idio that could be the name of a
	 * program we want to execute: "ls -l" should have both "ls"
	 * and "-l" as un-bound symbols for which we need to return a
	 * gvi of 0 so that IDIO_A[_CHECKED]_GLOBAL_FUNCTION_SYM_REF
	 * (and IDIO_A[_CHECKED]_GLOBAL_SYM_REF) can return the
	 * symbols for idio_vm_invoke() to exec.
	 */

	/*
	 * 1. map mci to a global constant index
	 */
	IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);
	IDIO sym = idio_vm_constants_ref (gci);
	IDIO_TYPE_ASSERT (symbol, sym);

	/*
	 * 2. Look in the current environment
	 */
	IDIO si_ce = idio_module_find_symbol (sym, ce);

	if (idio_S_false == si_ce) {
	    /*
	     * 2. Look in the imports of the current environment.
	     */
	    IDIO si_im = idio_module_find_symbol_recurse (sym, ce, 2);

	    if (idio_S_false == si_im) {
		IDIO dr = idio_module_direct_reference (sym);
		if (idio_S_false != dr) {
		    IDIO si_dr = IDIO_PAIR_HTT (dr);
		    idio_module_set_symbol (sym, si_dr, ce);
		    fgvi = IDIO_PAIR_HTT (si_dr);
		    idio_module_set_vvi (ce, fmci, fgvi);
		    return IDIO_FIXNUM_VAL (fgvi);
		} else {
#ifdef IDIO_DEBUG_X
		    fprintf (stderr, "[%d] iv-goc-vvi:", getpid ());
		    idio_debug (" %-20s return 0\n", sym);
		    idio_debug ("  ce %s\n", ce);
		    idio_debug ("  sl %s\n", idio_vm_source_location ());
#endif
		    return 0;
		}

		/*
		 * Not found so forge this mci to have a value of
		 * itself, the symbol sym.
		 */
		gvi = idio_vm_extend_values ();
		fgvi = idio_fixnum (gvi);
		si_im = IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, ce, idio_vm_get_or_create_vvi_string);
		idio_module_set_symbol (sym, si_im, ce);
		idio_module_set_symbol_value (sym, sym, ce);

		return gvi;
	    }

	    fgvi = IDIO_PAIR_HTT (si_im);
	    gvi = IDIO_FIXNUM_VAL (fgvi);
	    IDIO_C_ASSERT (gvi >= 0);

	    /*
	     * There was no existing entry for:
	     *
	     *   a. this mci in the idio_module_vvi table -- so set it
	     *
	     *   b. this sym in the current environment -- so copy the
	     *   imported si
	     */
	    idio_module_set_symbol (sym, si_im, ce);
	    idio_module_set_vvi (ce, fmci, fgvi);
	} else {
	    fgvi = IDIO_PAIR_HTT (si_ce);
	    gvi = IDIO_FIXNUM_VAL (fgvi);
	    IDIO_C_ASSERT (gvi >= 0);

	    if (0 == gvi &&
		idio_eqp (ce, idio_Idio_module_instance ())) {
		IDIO si_im = idio_module_find_symbol_recurse (sym, ce, 2);

		if (idio_S_false != si_im) {
		    fgvi = IDIO_PAIR_HTT (si_im);
		    gvi = IDIO_FIXNUM_VAL (fgvi);
		    IDIO_C_ASSERT (gvi >= 0);

		}

		IDIO_PAIR_HTT (si_ce) = fgvi;
	    }

	    /*
	     * There was no existing entry for:
	     *
	     *   a. this mci in the idio_module_vvi table -- so set it
	     */
	    idio_module_set_vvi (ce, fmci, fgvi);
	}
    } else {
	gvi = IDIO_FIXNUM_VAL (fgvi);
	IDIO_C_ASSERT (gvi >= 0);
    }

    return gvi;
}

int idio_vm_run1 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_IA_T bc = idio_all_code;

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc < 0) {
	fprintf (stderr, "\n\nidio_vm_run1: PC %td < 0\n", pc);
	idio_vm_panic (thr, "idio_vm_run1: bad PC!");
    } else if (pc > IDIO_IA_USIZE (bc)) {
	fprintf (stderr, "\n\nidio_vm_run1: PC %td > max code PC %" PRIdPTR "\n", pc, IDIO_IA_USIZE (bc));
	idio_vm_panic (thr, "idio_vm_run1: bad PC!");
    }

    IDIO_VM_RUN_DIS ("              %6zd [%3zd] ", pc, idio_array_size (IDIO_THREAD_STACK (thr)));

    IDIO_I ins = IDIO_THREAD_FETCH_NEXT (bc);

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
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF %" PRId64 "", j);
	    IDIO_THREAD_VAL (thr) = IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), j);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-REF %" PRId64 " %" PRId64 "", i, j);
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
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET %" PRId64 "", i);
	    IDIO_FRAME_ARGS (IDIO_THREAD_FRAME (thr), i) = IDIO_THREAD_VAL (thr);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-SET %" PRId64 " %" PRId64 "", i, j);
	    idio_frame_update (IDIO_THREAD_FRAME (thr), i, j, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_GLOBAL_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("GLOBAL-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    /*
		     * Test Case: ??
		     *
		     * Coding error.
		     */
		    idio_debug ("\nGLOBAL-SYM-REF: %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		    IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		IDIO ce = idio_thread_current_env ();
		IDIO si_ce = idio_module_find_symbol_recurse (sym, ce, 1);

		if (idio_S_false == si_ce) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else {
		    IDIO si_fgvi = IDIO_PAIR_HTT (si_ce);
		    idio_ai_t si_gvi = IDIO_FIXNUM_VAL (si_fgvi);
		    if (0 == si_gvi) {
			/*
			 * Test Case: ??
			 *
			 * Coding error.
			 */
			idio_debug ("G-R ce=%s\n", ce);
			idio_debug ("G-R si_ce=%s\n", si_ce);
			idio_error_runtime_unbound (fmci, fgci, sym, IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"));

			/* notreached */
			return 0;
		    } else {
			/*
			 * Setup the fast-lookup -- NB we don't update
			 * the si_ce value with si_fgvi -- should we?
			 */
			idio_module_set_vvi (ce, fmci, si_fgvi);
			IDIO val = idio_vm_values_ref (si_gvi);

			if (idio_S_undef == val) {
			    IDIO_THREAD_VAL (thr) = sym;
			} else if (idio_S_unspec == val) {
			    /*
			     * Test Case: ??
			     *
			     * Coding error.
			     */
			    idio_debug ("\nGLOBAL-SYM-REF: %s", sym);
			    fprintf (stderr, " #%" PRId64, mci);
			    idio_dump (thr, 2);
			    idio_debug ("c-m: %s\n", idio_thread_current_module ());
			    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

			    /* notreached */
			    return 0;
			} else {
			    IDIO_THREAD_VAL (thr) = val;
			}
		    }
		}
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

    	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    /*
		     * Test Case: ??
		     *
		     * Coding error.
		     */
		    idio_debug ("\nCHECKED-GLOBAL-SYM-REF: %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    IDIO_C_ASSERT (0);
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CHECKED-GLOBAL-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		    IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		idio_debug ("CHECKED-GLOBAL-SYM-REF: %s gvi==0 => sym\n", sym);
		idio_debug (" ce=%s\n", idio_thread_current_env ());
		fprintf (stderr, " mci #%" PRId64 "\n", mci);
		idio_vm_panic (thr, "CHECKED-GLOBAL-SYM-REF: gvi==0");
		IDIO_THREAD_VAL (thr) = sym;
	    }
	}
	break;
    case IDIO_A_GLOBAL_FUNCTION_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("GLOBAL-FUNCTION-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    /*
		     * Test Case: ??
		     *
		     * Coding error.
		     */
		    idio_debug ("\nGLOBAL-FUNCTION-SYM-REF:) %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-FUNCTION-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		  IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		IDIO ce = idio_thread_current_env ();
		IDIO si_ce = idio_module_find_symbol_recurse (sym, ce, 1);

		if (idio_S_false == si_ce) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else {
		    IDIO si_fgvi = IDIO_PAIR_HTT (si_ce);
		    idio_ai_t si_gvi = IDIO_FIXNUM_VAL (si_fgvi);
		    if (0 == si_gvi) {
			/*
			 * Test Case: ??
			 *
			 * Coding error.
			 */
			idio_error_runtime_unbound (fmci, fgci, sym, IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-REF"));

			/* notreached */
			return 0;
		    } else {
			/*
			 * Setup the fast-lookup -- NB we don't update
			 * the si_ce value with si_fgvi -- should we?
			 */
			idio_module_set_vvi (ce, fmci, si_fgvi);
			IDIO val = idio_vm_values_ref (si_gvi);

			if (idio_S_undef == val) {
			    IDIO_THREAD_VAL (thr) = sym;
			} else if (idio_S_unspec == val) {
			    /*
			     * Test Case: ??
			     *
			     * Coding error.
			     */
			    idio_debug ("\nGLOBAL-FUNCTION-SYM-REF: %s", sym);
			    fprintf (stderr, " #%" PRId64, mci);
			    idio_dump (thr, 2);
			    idio_debug ("c-m: %s\n", idio_thread_current_module ());
			    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("GLOBAL-FUNCTION-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

			    /* notreached */
			    return 0;
			} else {
			    IDIO_THREAD_VAL (thr) = val;
			}
		    }
		}
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-FUNCTION-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = idio_vm_values_ref (gvi);

		if (idio_S_undef == val) {
		    IDIO_THREAD_VAL (thr) = sym;
		} else if (idio_S_unspec == val) {
		    /*
		     * Test Case: ??
		     *
		     * Coding error.
		     */
		    idio_debug ("\nCHECKED-GLOBAL-FUNCTION-SYM-REF:) %s", sym);
		    fprintf (stderr, " #%" PRId64, mci);
		    idio_dump (thr, 2);
		    idio_debug ("c-m: %s\n", idio_thread_current_module ());
		    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CHECKED-GLOBAL-FUNCTION-SYM-REF"), "unspecified toplevel: %" PRId64 "", mci);

		    /* notreached */
		    return 0;
		} else {
		    IDIO_THREAD_VAL (thr) = val;
		}
	    } else {
		idio_debug ("CHECKED-GLOBAL-FUNCTION-SYM-REF: %s gvi==0 => sym\n", sym);
		idio_vm_panic (thr, "CHECKED-GLOBAL-FUNCTION-SYM-REF: gvi==0");
		IDIO_THREAD_VAL (thr) = sym;
	    }
	}
	break;
    case IDIO_A_CONSTANT_SYM_REF:
    	{
    	    idio_ai_t mci = idio_vm_fetch_varuint (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	    IDIO c = idio_vm_constants_ref (gci);

    	    IDIO_VM_RUN_DIS ("CONSTANT %td", gci);
#ifdef IDIO_VM_DIS
	    if (idio_vm_dis) {
		idio_debug (" %s", c);
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
		idio_coding_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-SYM-REF"));

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
			idio_debug ("idio_vm_run1/CONSTANT-SYM-REF: you should NOT be reifying %s", c);
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
			idio_coding_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-SYM-REF"));

			/* notreached */
			return 0;
			break;
		    }
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT-SYM-REF"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", c, (intptr_t) c & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

		/* notreached */
		return 0;
		break;
	    }
    	}
    	break;
    case IDIO_A_COMPUTED_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);

#ifdef IDIO_DEBUG
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("COMPUTED-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
#endif

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_computed_ref (thr, mci, gvi);
	    } else {
		idio_vm_panic (thr, "COMPUTED-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_GLOBAL_SYM_DEF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t mkci = idio_vm_fetch_varuint (thr);
	    IDIO fmci = idio_fixnum (mci);

	    IDIO ce = idio_thread_current_env ();
	    IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
	    IDIO fgkci = idio_module_get_or_set_vci (ce, idio_fixnum (mkci));
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO kind = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgkci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("GLOBAL-SYM-DEF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    IDIO si_ce;
	    if (idio_S_environ == kind ||
		idio_S_dynamic == kind) {
		si_ce = idio_module_find_symbol_recurse (sym, ce, 1);
	    } else {
		si_ce = idio_module_find_symbol (sym, ce);
	    }
	    if (idio_S_false == si_ce) {
		idio_ai_t gvi = idio_vm_extend_values ();
		IDIO fgvi = idio_fixnum (gvi);
		idio_module_set_vvi (ce, idio_fixnum (mci), fgvi);
		si_ce = IDIO_LIST5 (kind, fmci, fgvi, ce, idio_vm_GLOBAL_SYM_DEF_string);
		idio_module_set_symbol (sym, si_ce, ce);
	    } else {
		IDIO fgvi = IDIO_PAIR_HTT (si_ce);
		if (0 == IDIO_FIXNUM_VAL (fgvi)) {
		    idio_ai_t gvi = idio_vm_extend_values ();
		    fgvi = idio_fixnum (gvi);
		    idio_module_set_vvi (ce, idio_fixnum (mci), fgvi);
		    si_ce = IDIO_LIST5 (IDIO_PAIR_H (si_ce), fmci, fgvi, ce, idio_vm_GLOBAL_SYM_DEF_gvi0_string);
		    idio_module_set_symbol (sym, si_ce, ce);
		}
	    }
	}
	break;
    case IDIO_A_GLOBAL_SYM_SET:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    IDIO_VM_RUN_DIS ("GLOBAL-SYM-SET %" PRId64 "/%" PRId64 " %s", mci, gvi, IDIO_SYMBOL_S (sym));

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		idio_vm_values_set (gvi, val);

		if (idio_isa_closure (val)) {
		    IDIO name = idio_ref_property (val, idio_KW_name, IDIO_LIST1 (idio_S_false));
		    if (idio_S_false == name) {
			idio_set_property (val, idio_KW_name, sym);
			IDIO str = idio_ref_property (val, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));
			if (idio_S_nil != str) {
			    idio_set_property (val, idio_KW_sigstr, str);
			}
			str = idio_ref_property (val, idio_KW_docstr, IDIO_LIST1 (idio_S_nil));
			if (idio_S_nil != str) {
			    idio_set_property (val, idio_KW_docstr, str);
			}
		    }
		}
	    } else {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		IDIO ce = idio_thread_current_env ();
		IDIO si_ce = idio_module_find_symbol (sym, ce);
		idio_debug ("GLOBAL-SYM-SET: UNBOUND sym=%s", sym);
		idio_debug (" fmci=%s", fmci);
		idio_debug (" fgci=%s\n", fgci);
		idio_debug (" ce=%s\n", IDIO_MODULE_NAME (ce));
		idio_debug (" si_ce=%s\n", si_ce);
		idio_debug (" MI=%s\n", IDIO_MODULE_IMPORTS (ce));

		IDIO si = idio_module_find_symbol_recurse (sym, ce, 1);
		idio_debug (" si=%s\n", si);

		idio_error_runtime_unbound (fmci, fgci, sym, IDIO_C_FUNC_LOCATION_S ("GLOBAL-SYM-SET"));
		idio_vm_panic (thr, "GLOBAL-SYM-SET: no gvi!");

		/* notreached */
		return 0;
	    }
	}
	break;
    case IDIO_A_COMPUTED_SYM_SET:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);

#ifdef IDIO_DEBUG
	    IDIO fmci = idio_fixnum (mci);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("COMPUTED-SYM-SET %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
#endif

	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		/*
		 * For other values, *val* remains the value "set".  For a
		 * computed value, setting it runs an arbitrary piece of
		 * code which returns a value.
		 */
		IDIO_THREAD_VAL (thr) = idio_vm_computed_set (thr, mci, gvi, val);
	    } else {
		idio_vm_panic (thr, "COMPUTED-SYM-SET: no gvi!");
	    }
	}
	break;
    case IDIO_A_COMPUTED_SYM_DEF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO ce = idio_thread_current_env ();
	    IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
	    IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    IDIO_TYPE_ASSERT (symbol, sym);

	    IDIO_VM_RUN_DIS ("COMPUTED-SYM-DEF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (ce, idio_fixnum (mci), fgvi);

	    IDIO si_ce = idio_module_find_symbol (sym, ce);

	    if (idio_S_false == si_ce) {
		si_ce = IDIO_LIST5 (idio_S_toplevel, idio_fixnum (mci), fgvi, ce, idio_vm_COMPUTED_SYM_DEF_string);
		idio_module_set_symbol (sym, si_ce, ce);
	    } else {
		IDIO_PAIR_HTT (si_ce) = fgvi;
	    }

	    IDIO val = IDIO_THREAD_VAL (thr);

	    idio_vm_computed_define (thr, mci, gvi, val);
	}
	break;
    case IDIO_A_GLOBAL_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("GLOBAL-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "GLOBAL-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "CHECKED-GLOBAL-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_GLOBAL_FUNCTION_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("GLOBAL-FUNCTION-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "GLOBAL-FUNCTION-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-FUNCTION-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_values_ref (gvi);
	    } else {
		fprintf (stderr, "CHECKED-GLOBAL-FUNCTION-VAL-REF: gvi == 0\n");
		IDIO_C_ASSERT (0);
	    }
	}
	break;
    case IDIO_A_CONSTANT_VAL_REF:
	{
	    idio_ai_t gvi = idio_vm_fetch_varuint (thr);
	    IDIO fgvi = idio_fixnum (gvi);
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fgvi);
	    idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	    IDIO c = idio_vm_constants_ref (gci);

	    IDIO_VM_RUN_DIS ("CONSTANT %td", gci);
#ifdef IDIO_VM_DIS
	    if (idio_vm_dis) {
		idio_debug (" %s", c);
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
		idio_coding_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-VAL-REF"));

		/* notreached */
		return 0;
		break;
	    case IDIO_TYPE_POINTER_MARK:
		{
		    switch (c->type) {
		    case IDIO_TYPE_STRING:
		    case IDIO_TYPE_SYMBOL:
		    case IDIO_TYPE_KEYWORD:
		    case IDIO_TYPE_PAIR:
		    case IDIO_TYPE_ARRAY:
		    case IDIO_TYPE_HASH:
		    case IDIO_TYPE_BIGNUM:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_STRUCT_INSTANCE:
			IDIO_THREAD_VAL (thr) = idio_copy (c, IDIO_COPY_DEEP);
			break;
		    case IDIO_TYPE_PRIMITIVE:
		    case IDIO_TYPE_CLOSURE:
			idio_debug ("idio_vm_run1/CONSTANT-VAL-REF: you should NOT be reifying %s", c);
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
			idio_coding_error_C ("invalid constant type", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT-VAL-REF"));

			/* notreached */
			return 0;
			break;
		    }
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT-VAL-REF"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", c, (intptr_t) c & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

		/* notreached */
		return 0;
		break;
	    }
	}
	break;
    case IDIO_A_COMPUTED_VAL_REF:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("COMPUTED-VAL-REF %" PRId64, gvi);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_computed_ref (thr, gvi, gvi);
	    } else {
		idio_vm_panic (thr, "COMPUTED-VAL-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_GLOBAL_VAL_DEF:
	{
#ifdef IDIO_DEBUG
	    uint64_t gvi =
#endif
		IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("GLOBAL-VAL-DEF %" PRId64, gvi);

	    /* no symbol to define! */
	    IDIO_C_ASSERT (0);
	}
	break;
    case IDIO_A_GLOBAL_VAL_SET:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("GLOBAL-VAL-SET %" PRId64, gvi);

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		idio_vm_values_set (gvi, val);
	    } else {
		idio_vm_panic (thr, "GLOBAL-VAL-SET: no gvi!");
	    }
	}
	break;
    case IDIO_A_COMPUTED_VAL_SET:
	{
	    uint64_t gvi = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("COMPUTED-VAL-SET %" PRId64, gvi);

	    if (gvi) {
		IDIO val = IDIO_THREAD_VAL (thr);

		/*
		 * For other values, *val* remains the value "set".  For a
		 * computed value, setting it runs an arbitrary piece of
		 * code which returns a value.
		 */
		IDIO_THREAD_VAL (thr) = idio_vm_computed_set (thr, gvi, gvi, val);
	    } else {
		idio_vm_panic (thr, "COMPUTED-VAL-SET: no gvi!");
	    }
	}
	break;
    case IDIO_A_COMPUTED_VAL_DEF:
	{
#ifdef IDIO_DEBUG
	    uint64_t gvi =
#endif
		IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("COMPUTED-VAL-DEF %" PRId64, gvi);

	    /* no symbol to define! */
	    IDIO_C_ASSERT (0);
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
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO pd = idio_vm_values_ref (vi);
	    IDIO_VM_RUN_DIS ("PREDEFINED %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (pd));
	    IDIO_THREAD_VAL (thr) = pd;
	}
	break;
    case IDIO_A_LONG_GOTO:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-GOTO +%" PRId64 "", i);
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_LONG_JUMP_FALSE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-JUMP-FALSE +%" PRId64 "", i);
	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_LONG_JUMP_TRUE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-JUMP-TRUE +%" PRId64 "", i);
	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_GOTO:
	{
	    int i = IDIO_THREAD_FETCH_NEXT (bc);
	    IDIO_VM_RUN_DIS ("SHORT-GOTO +%d", i);
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_SHORT_JUMP_FALSE:
	{
	    int i = IDIO_THREAD_FETCH_NEXT (bc);
	    IDIO_VM_RUN_DIS ("SHORT-JUMP-FALSE +%d", i);
	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_JUMP_TRUE:
	{
	    int i = IDIO_THREAD_FETCH_NEXT (bc);
	    IDIO_VM_RUN_DIS ("SHORT-JUMP-TRUE +%d", i);
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
    	    idio_ai_t mci = idio_vm_fetch_varuint (thr);
	    IDIO fmci = idio_fixnum (mci);
	    IDIO_VM_RUN_DIS ("SRC-EXPR %td", mci);
	    IDIO_THREAD_EXPR (thr) = fmci;
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
	    idio_vm_preserve_state (thr);
	    IDIO_VM_RUN_DIS ("PRESERVE-STATE");
	}
	break;
    case IDIO_A_RESTORE_STATE:
	{
	    idio_vm_restore_state (thr);
	    IDIO_VM_RUN_DIS ("RESTORE-STATE");
	}
	break;
    case IDIO_A_RESTORE_ALL_STATE:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-ALL-STATE");
	    idio_vm_restore_all_state (thr);
	}
	break;
    case IDIO_A_CREATE_CLOSURE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CREATE-CLOSURE @ +%" PRId64 "", i);
	    uint64_t code_len = idio_vm_fetch_varuint (thr);
	    uint64_t ssci = idio_vm_fetch_varuint (thr);
	    uint64_t dsci = idio_vm_fetch_varuint (thr);
	    uint64_t slci = idio_vm_fetch_varuint (thr);

	    IDIO ce = idio_thread_current_env ();

	    /* sigstr lookup */
	    IDIO fci = idio_fixnum (ssci);
	    IDIO fgci = idio_module_get_or_set_vci (ce, fci);
	    IDIO sigstr = idio_S_nil;
	    if (idio_S_unspec != fgci) {
		sigstr = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    } else {
		fprintf (stderr, "vm cc sig: failed to find %" PRId64 " (%" PRId64 ")\n", (int64_t) IDIO_FIXNUM_VAL (fci), ssci);
	    }

	    /* docstr lookup */
	    fci = idio_fixnum (dsci);
	    fgci = idio_module_get_or_set_vci (ce, fci);
	    IDIO docstr = idio_S_nil;
	    if (idio_S_unspec != fgci) {
		docstr = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    } else {
		fprintf (stderr, "vm cc doc: failed to find %" PRId64 " (%" PRId64 ")\n", (int64_t) IDIO_FIXNUM_VAL (fci), dsci);
	    }

	    /* srcloc lookup */
	    fci = idio_fixnum (slci);
	    fgci = idio_module_get_or_set_vci (ce, fci);
	    IDIO srcloc = idio_S_nil;
	    if (idio_S_unspec != fgci) {
		srcloc = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
	    } else {
		fprintf (stderr, "vm cc doc: failed to find %" PRId64 " (%" PRId64 ")\n", (int64_t) IDIO_FIXNUM_VAL (fci), dsci);
	    }

	    IDIO_THREAD_VAL (thr) = idio_closure (IDIO_THREAD_PC (thr) + i, code_len, IDIO_THREAD_FRAME (thr), IDIO_THREAD_ENV (thr), sigstr, docstr, srcloc);
	}
	break;
    case IDIO_A_FUNCTION_INVOKE:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-INVOKE ...");
	    if (! idio_isa_primitive (IDIO_THREAD_FUNC (thr))) {
		IDIO_VM_RUN_DIS ("\n");
	    }
	    if (idio_vm_tracing) {
		idio_vm_function_trace (ins, thr);
	    }
#ifdef IDIO_VM_PROF
	    idio_vm_clos_time (thr, "FUNCTION-INVOKE");
#endif

	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), IDIO_VM_INVOKE_REGULAR_CALL);
	}
	break;
    case IDIO_A_FUNCTION_GOTO:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-GOTO ...");
	    if (! idio_isa_primitive (IDIO_THREAD_FUNC (thr))) {
		IDIO_VM_RUN_DIS ("\n");
	    }
	    if (idio_vm_tracing) {
		idio_vm_function_trace (ins, thr);
	    }
#ifdef IDIO_VM_PROF
	    idio_vm_clos_time (thr, "FUNCTION-GOTO");
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
	    IDIO ipc = IDIO_THREAD_STACK_POP ();
	    if (! IDIO_TYPE_FIXNUMP (ipc)) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_debug ("\n\nRETURN {fixnum}: not %s\n", ipc);
		idio_vm_debug (thr, "IDIO_A_RETURN", 0);
		IDIO_THREAD_STACK_PUSH (ipc);
		idio_vm_decode_thread (thr);
		idio_coding_error_C ("RETURN: not a number", ipc, IDIO_C_FUNC_LOCATION_S ("RETURN"));

		/* notreached */
		return 0;
	    }
	    idio_ai_t pc = IDIO_FIXNUM_VAL (ipc);
	    if (pc > IDIO_IA_USIZE (bc) ||
		pc < 0) {
		fprintf (stderr, "\n\nPC= %td?\n", pc);
		idio_dump (thr, 1);
		idio_dump (IDIO_THREAD_STACK (thr), 1);
		idio_vm_decode_thread (thr);
		idio_vm_panic (thr, "RETURN: impossible PC on stack top");
	    }
	    IDIO_VM_RUN_DIS ("RETURN to %" PRIdPTR, pc);
	    IDIO_THREAD_PC (thr) = pc;
	    if (idio_vm_tracing) {
		fprintf (stderr, "%9s ", "");
		fprintf (stderr, "%7td ", IDIO_THREAD_PC (thr));
		fprintf (stderr, "%40s", "");
		fprintf (stderr, "%.*s  ", idio_vm_tracing, idio_vm_tracing_out);
		idio_debug ("%s\n", IDIO_THREAD_VAL (thr));
		if (idio_vm_tracing <= 1) {
		    /* fprintf (stderr, "XXX RETURN to %td: tracing depth <= 1!\n", pc); */
		} else {
		    idio_vm_tracing--;
		}
	    }
#ifdef IDIO_VM_PROF
	    idio_vm_clos_time (thr, "RETURN");
#endif
	}
	break;
    case IDIO_A_FINISH:
	{
	    /* invoke exit handler... */
	    IDIO_VM_RUN_DIS ("FINISH\n");
	    return 0;
	}
	break;
    case IDIO_A_PUSH_ABORT:
	{
	    uint64_t o = idio_vm_fetch_varuint (thr);

	    IDIO_VM_RUN_DIS ("PUSH-ABORT to PC +%" PRIu64 "\n", o);

	    /*
	     * A continuation right now would just lead us back into
	     * this errant code.  We want to massage the
	     * continuation's PC to be offset by {o}.
	     */
	    IDIO k = idio_continuation (thr, IDIO_CONTINUATION_CALL_CC);

	    IDIO_CONTINUATION_PC (k) += o;

	    IDIO dosh = idio_open_output_string_handle_C ();
	    idio_display_C ("ABORT to toplevel (PC ", dosh);
	    idio_display (idio_fixnum (IDIO_CONTINUATION_PC (k)), dosh);
	    idio_display_C (")", dosh);

	    idio_array_push (idio_vm_krun, IDIO_LIST2 (k, idio_get_output_string (dosh)));
	    idio_command_suppress_rcse = idio_S_false;
	}
	break;
    case IDIO_A_POP_ABORT:
	{
	    IDIO_VM_RUN_DIS ("POP-ABORT\n");

	    idio_array_pop (idio_vm_krun);
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
	    IDIO frame = idio_frame_allocate (2);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 2");
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_FRAME3:
	{
	    IDIO frame = idio_frame_allocate (3);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 3");
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
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME %" PRId64, i);
	    IDIO frame = idio_frame_allocate (i);
	    IDIO_THREAD_VAL (thr) = frame;
	}
	break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-DOTTED-FRAME %" PRId64, arity);
	    IDIO vs = idio_frame_allocate (arity);
	    idio_frame_update (vs, 0, arity - 1, idio_S_nil);
	    IDIO_THREAD_VAL (thr) = vs;
	}
	break;
    case IDIO_A_REUSE_FRAME:
	{
	    uint64_t size = idio_vm_fetch_varuint (thr);
	    IDIO frame = IDIO_THREAD_FRAME (thr);
	    /* fprintf (stderr, "REUSE %2d for %2" PRIu64 "\n", IDIO_FRAME_NPARAMS (frame), size); */
	    IDIO_VM_RUN_DIS ("REUSE-FRAME %" PRId64 "", size);
	    if (size > IDIO_FRAME_NALLOC (frame)) {
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
	    uint64_t rank = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("POP-FRAME %" PRId64 "", rank);
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, rank, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_LINK_FRAME:
	{
	    uint64_t ssci = idio_vm_fetch_varuint (thr);
	    if (ssci) {
		IDIO fci = idio_fixnum (ssci);
		IDIO ce = idio_thread_current_env ();
		IDIO fgci = idio_module_get_or_set_vci (ce, fci);
		IDIO_TYPE_ASSERT (frame, IDIO_THREAD_VAL (thr));
		IDIO_FRAME_NAMES (IDIO_THREAD_VAL (thr)) = fgci;
	    }

	    IDIO_VM_RUN_DIS ("LINK-FRAME sci=%" PRId64, ssci);
	    IDIO_THREAD_FRAME (thr) = idio_link_frame (IDIO_THREAD_FRAME (thr), IDIO_THREAD_VAL (thr));
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
	    uint64_t arity = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("PACK-FRAME %" PRId64 "", arity);
	    idio_vm_listify (IDIO_THREAD_VAL (thr), arity);
	}
	break;
    case IDIO_A_POP_LIST_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);

	    IDIO_VM_RUN_DIS ("POP-LIST-FRAME %" PRId64 "", arity);
	    idio_frame_update (IDIO_THREAD_VAL (thr),
			       0,
			       arity,
			       idio_pair (IDIO_THREAD_STACK_POP (),
					  idio_frame_fetch (IDIO_THREAD_VAL (thr), 0, arity)));
	}
	break;
    case IDIO_A_EXTEND_FRAME:
	{
	    uint64_t alloc = idio_vm_fetch_varuint (thr);
	    uint64_t ssci = idio_vm_fetch_varuint (thr);
	    if (ssci) {
		IDIO fci = idio_fixnum (ssci);
		IDIO ce = idio_thread_current_env ();
		IDIO fgci = idio_module_get_or_set_vci (ce, fci);
		IDIO_FRAME_NAMES (IDIO_THREAD_FRAME (thr)) = fgci;
	    }
	    IDIO_VM_RUN_DIS ("EXTEND-FRAME %" PRId64 " sci=%" PRId64, alloc, ssci);
	    idio_extend_frame (IDIO_THREAD_FRAME (thr), alloc);
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
	    uint64_t arityp1 = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ARITY=? %" PRId64 "", arityp1);
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if (arityp1 != nargs) {
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
	    uint64_t arityp1 = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ARITY>=? %" PRId64 "", arityp1);
	    idio_ai_t nargs = -1;
	    IDIO val = IDIO_THREAD_VAL (thr);
	    if (idio_S_nil != val) {
		IDIO_TYPE_ASSERT (frame, val);
		nargs = IDIO_FRAME_NPARAMS (val) + 1;
	    }
	    if (nargs < arityp1) {
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
	    IDIO_THREAD_VAL (thr) = idio_fixnum (0);
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
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("FIXNUM %" PRId64 "", v);
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
	    int64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-FIXNUM %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("NEG-FIXNUM"), "FIXNUM OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = idio_fixnum ((intptr_t) v);
	}
	break;
    case IDIO_A_CONSTANT:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CONSTANT %" PRId64 "", v);
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
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-CONSTANT %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("NEG-CONSTANT"), "CONSTANT OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);

		/* notreached */
		return 0;
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT_IDIO ((intptr_t) v);
	}
	break;
    case IDIO_A_UNICODE:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("UNICODE %" PRId64 "", v);
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
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_values_ref (vi);
	    /*
	     * XXX see comment in idio_meaning_primitive_application()
	     * for setting *func*
	     */
	    IDIO_THREAD_FUNC (thr) = primdata;
	    IDIO_VM_RUN_DIS ("PRIMITIVE0 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 0);
	    }
#ifdef IDIO_VM_PROF
	    struct timespec prim_t0;
	    struct rusage prim_ru0;
	    idio_vm_func_start (primdata, &prim_t0, &prim_ru0);
#endif
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) ();
#ifdef IDIO_VM_PROF
	    struct timespec prim_te;
	    struct rusage prim_rue;
	    idio_vm_func_stop (primdata, &prim_te, &prim_rue);
	    idio_vm_prim_time (primdata, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1:
	{
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_values_ref (vi);
	    /*
	     * XXX see comment in idio_meaning_primitive_application()
	     * for setting *func*
	     */
	    IDIO_THREAD_FUNC (thr) = primdata;
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 1);
	    }
#ifdef IDIO_VM_PROF
	    struct timespec prim_t0;
	    struct rusage prim_ru0;
	    idio_vm_func_start (primdata, &prim_t0, &prim_ru0);
#endif
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) (IDIO_THREAD_VAL (thr));
#ifdef IDIO_VM_PROF
	    struct timespec prim_te;
	    struct rusage prim_rue;
	    idio_vm_func_stop (primdata, &prim_te, &prim_rue);
	    idio_vm_prim_time (primdata, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2:
	{
	    uint64_t vi = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_values_ref (vi);
	    /*
	     * XXX see comment in idio_meaning_primitive_application()
	     * for setting *func*
	     */
	    IDIO_THREAD_FUNC (thr) = primdata;
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 2);
	    }
#ifdef IDIO_VM_PROF
	    struct timespec prim_t0;
	    struct rusage prim_ru0;
	    idio_vm_func_start (primdata, &prim_t0, &prim_ru0);
#endif
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
#ifdef IDIO_VM_PROF
	    struct timespec prim_te;
	    struct rusage prim_rue;
	    idio_vm_func_stop (primdata, &prim_te, &prim_rue);
	    idio_vm_prim_time (primdata, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
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
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO_VM_RUN_DIS ("EXPANDER %" PRId64 "", mci);

	    IDIO fmci = idio_fixnum (mci);
	    IDIO ce = idio_thread_current_env ();
	    idio_module_set_vci (ce, fmci, fmci);
	    IDIO sym = idio_vm_constants_ref (mci);

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (ce, fmci, fgvi);

	    IDIO si_ce = idio_module_find_symbol (sym, ce);

	    if (idio_S_false == si_ce) {
		idio_debug ("EXPANDER: %s ", sym);
		fprintf (stderr, "%" PRIu64 " undefined?  setting...\n", mci);
		si_ce = IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, ce, idio_vm_EXPANDER_string);
		idio_module_set_symbol (sym, si_ce, ce);
	    } else {
		IDIO_PAIR_HTT (si_ce) = fgvi;
	    }

	    IDIO val = IDIO_THREAD_VAL (thr);
	    IDIO cname = idio_ref_property (val, idio_KW_name, IDIO_LIST1 (idio_S_false));
	    if (idio_S_false == cname) {
		idio_set_property (val, idio_KW_name, sym);
	    }
	    idio_install_expander (sym, val);
	    idio_module_set_symbol_value (sym, val, ce);
	}
	break;
    case IDIO_A_INFIX_OPERATOR:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t pri = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("INFIX-OPERATOR %" PRId64 "", mci);

	    IDIO fmci = idio_fixnum (mci);
	    idio_module_set_vci (idio_operator_module, fmci, fmci);
	    IDIO sym = idio_vm_constants_ref (mci);

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (idio_operator_module, fmci, fgvi);

	    IDIO si_ce = idio_module_find_symbol (sym, idio_operator_module);

	    if (idio_S_false == si_ce) {
		idio_debug ("INFIX-OPERATOR: %s ", sym);
		fprintf (stderr, "%" PRIu64 " undefined?  setting...\n", mci);
		si_ce = IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, idio_operator_module, idio_vm_INFIX_OPERATOR_string);
		idio_module_set_symbol (sym, si_ce, idio_operator_module);
	    } else {
		IDIO_PAIR_HTT (si_ce) = fgvi;
	    }

	    IDIO val = IDIO_THREAD_VAL (thr);
	    IDIO cname = idio_ref_property (val, idio_KW_name, IDIO_LIST1 (idio_S_false));
	    if (idio_S_false == cname) {
		idio_set_property (val, idio_KW_name, sym);
	    }
	    idio_install_infix_operator (sym, val, pri);
	    idio_module_set_symbol_value (sym, val, idio_operator_module);
	}
	break;
    case IDIO_A_POSTFIX_OPERATOR:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t pri = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("POSTFIX-OPERATOR %" PRId64 "", mci);

	    IDIO fmci = idio_fixnum (mci);
	    idio_module_set_vci (idio_operator_module, fmci, fmci);
	    IDIO sym = idio_vm_constants_ref (mci);

	    idio_ai_t gvi = idio_vm_extend_values ();
	    IDIO fgvi = idio_fixnum (gvi);
	    idio_module_set_vvi (idio_operator_module, fmci, fgvi);

	    IDIO si_ce = idio_module_find_symbol (sym, idio_operator_module);

	    if (idio_S_false == si_ce) {
		idio_debug ("POSTFIX-OPERATOR: %s ", sym);
		fprintf (stderr, "%" PRIu64 " undefined?  setting...\n", mci);
		si_ce = IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, idio_operator_module, idio_vm_POSTFIX_OPERATOR_string);
		idio_module_set_symbol (sym, si_ce, idio_operator_module);
	    } else {
		IDIO_PAIR_HTT (si_ce) = fgvi;
	    }

	    IDIO val = IDIO_THREAD_VAL (thr);
	    IDIO cname = idio_ref_property (val, idio_KW_name, IDIO_LIST1 (idio_S_false));
	    if (idio_S_false == cname) {
		idio_set_property (val, idio_KW_name, sym);
	    }
	    idio_install_postfix_operator (sym, val, pri);
	    idio_module_set_symbol_value (sym, val, idio_operator_module);
	}
	break;
    case IDIO_A_PUSH_DYNAMIC:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO_VM_RUN_DIS ("PUSH-DYNAMIC %" PRId64, mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		idio_vm_push_dynamic (thr, gvi, IDIO_THREAD_VAL (thr));
	    } else {
		idio_vm_panic (thr, "PUSH-DYNAMIC: no gvi!");
	    }
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
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO_VM_RUN_DIS ("DYNAMIC-SYM-REF %" PRId64 "", mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (thr, mci, gvi, idio_S_nil);
	    } else {
		idio_vm_panic (thr, "DYNAMIC-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO_VM_RUN_DIS ("DYNAMIC-FUNCTION-SYM-REF %" PRId64 "", mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (thr, mci, gvi, idio_S_nil);
	    } else {
		idio_vm_panic (thr, "DYNAMIC-FUNCTION-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_PUSH_ENVIRON:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO_VM_RUN_DIS ("PUSH-ENVIRON %" PRId64, mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		idio_vm_push_environ (thr, mci, gvi, IDIO_THREAD_VAL (thr));
	    } else {
		idio_vm_panic (thr, "PUSH-ENVIRON: no gvi!");
	    }
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
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    IDIO_VM_RUN_DIS ("ENVIRON-SYM-REF %" PRId64 "", mci);
	    idio_ai_t gvi = idio_vm_get_or_create_vvi (mci);

	    if (gvi) {
		IDIO_THREAD_VAL (thr) = idio_vm_environ_ref (thr, mci, gvi, idio_S_nil);
	    } else {
		idio_vm_panic (thr, "ENVIRON-SYM-REF: no gvi!");
	    }
	}
	break;
    case IDIO_A_NON_CONT_ERR:
	{
	    IDIO_VM_RUN_DIS ("NON-CONT-ERROR\n");

	    /*
	     * We'll go back to krun #1, the most recent ABORT.
	     */
	    idio_ai_t krun_p = idio_array_size (idio_vm_krun);
	    IDIO krun = idio_S_nil;
	    while (krun_p > 1) {
		krun = idio_array_pop (idio_vm_krun);
		idio_debug ("NON-CONT-ERROR: krun: popping %s\n", IDIO_PAIR_HT (krun));
		krun_p--;
	    }

	    if (idio_isa_pair (krun)) {
		fprintf (stderr, "NON-CONT-ERROR: restoring krun #%td: ", krun_p);
		idio_debug ("%s\n", IDIO_PAIR_HT (krun));
		idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

		/* notreached */
		return 0;
	    }

	    fprintf (stderr, "NON-CONT-ERROR: nothing to restore\n");
	    idio_vm_panic (thr, "NON-CONT-ERROR");

	    /* notreached */
	    return 0;
	}
	break;
    case IDIO_A_PUSH_TRAP:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("PUSH-TRAP %" PRId64 "", mci);
	    idio_vm_push_trap (thr, IDIO_THREAD_VAL (thr), idio_fixnum (mci), 0);
	}
	break;
    case IDIO_A_POP_TRAP:
	{
	    IDIO_VM_RUN_DIS ("POP-TRAP");
	    idio_vm_pop_trap (thr);
	}
	break;
    case IDIO_A_RESTORE_TRAP:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-TRAP");
	    idio_vm_restore_trap (thr);
	}
	break;
    case IDIO_A_PUSH_ESCAPER:
	{
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);
	    uint64_t offset = idio_vm_fetch_varuint (thr);

	    IDIO_VM_RUN_DIS ("PUSH-ESCAPER %" PRId64 "", mci);
	    idio_vm_push_escaper (thr, idio_fixnum (mci), offset);
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
	    uint64_t mci = IDIO_VM_FETCH_REF (thr, bc);

	    IDIO_VM_RUN_DIS ("ESCAPER-LABEL_REF %" PRId64 "", mci);
	    idio_vm_escaper_label_ref (thr, idio_fixnum (mci));
	}
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    idio_ai_t pc = IDIO_THREAD_PC (thr);
	    idio_ai_t pcm = pc + 10;
	    pc = pc - 10;
	    if (pc < 0) {
		pc = 0;
	    }
	    if (pc % 10) {
		idio_ai_t pc1 = pc - (pc % 10);
		fprintf (stderr, "\n  %5td ", pc1);
		for (; pc1 < pc; pc1++) {
		    fprintf (stderr, "    ");
		}
	    }
	    for (; pc < pcm; pc++) {
		if (0 == (pc % 10)) {
		    fprintf (stderr, "\n  %5td ", pc);
		}
		fprintf (stderr, "%3d ", IDIO_IA_AE (bc, pc));
	    }
	    fprintf (stderr, "\n");
	    fprintf (stderr, "unexpected instruction: %3d @%td\n", ins, IDIO_THREAD_PC (thr) - 1);
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected instruction: %3d @%" PRId64 "\n", ins, IDIO_THREAD_PC (thr) - 1);

	    /* notreached */
	    return 0;
	}
	break;
    }

#ifdef IDIO_VM_PROF
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
    if (idio_vm_ins_call_time[ins].tv_nsec > IDIO_VM_NS) {
	idio_vm_ins_call_time[ins].tv_nsec -= IDIO_VM_NS;
	idio_vm_ins_call_time[ins].tv_sec += 1;
    }
#endif

    IDIO_VM_RUN_DIS ("\n");
    return 1;
}

#define IDIO_VM_DASM(...)	{ fprintf (idio_dasm_FILE, __VA_ARGS__); }

void idio_vm_dasm (IDIO thr, IDIO_IA_T bc, idio_ai_t pc0, idio_ai_t pce)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    fprintf (stderr, "dasm: ia=%10p pc0=%td pce=%td\n", bc, pc0, pce);

    if (pc0 < 0) {
	pc0 = IDIO_THREAD_PC (thr);
    }

    if (0 == pce) {
	pce = IDIO_IA_USIZE (bc);
    }

    if (pc0 > pce) {
	fprintf (stderr, "\n\nPC %td > max code PC %td\n", pc0, pce);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %.1000s\n", IDIO_THREAD_STACK (thr));
	idio_vm_panic (thr, "vm-dasm: bad PC!");
    }

    IDIO_VM_DASM ("idio_vm_dasm: thr %p pc0 %6td pce %6td\n", thr, pc0, pce);

    IDIO hints = IDIO_HASH_EQP (256);

    idio_ai_t pc = pc0;
    idio_ai_t *pcp = &pc;

    for (; pc < pce;) {
	IDIO hint = idio_hash_ref (hints, idio_fixnum (pc));
	if (idio_S_unspec != hint) {
	    size_t size = 0;
	    char *hint_C = idio_as_string_safe (hint, &size, 40, 1);
	    IDIO_VM_DASM ("%-20s ", hint_C);
	    IDIO_GC_FREE (hint_C, size);
	} else {
	    IDIO_VM_DASM ("%20s ", "");
	}

	IDIO_VM_DASM ("%6td ", pc);

	IDIO_I ins = IDIO_IA_GET_NEXT (bc, pcp);

	IDIO_VM_DASM ("%3d: ", ins);

	switch (ins) {
	case IDIO_A_SHALLOW_ARGUMENT_REF0:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 0");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF1:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 1");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF2:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 2");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF3:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 3");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF:
	    {
		uint64_t j = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF %" PRId64 "", j);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_REF:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		uint64_t j = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("DEEP-ARGUMENT-REF %" PRId64 " %" PRId64 "", i, j);
	    }
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET0:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 0");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET1:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 1");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET2:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 2");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET3:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 3");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET %" PRId64 "", i);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_SET:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		uint64_t j = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("DEEP-ARGUMENT-SET %" PRId64 " %" PRId64 "", i, j);
	    }
	    break;
	case IDIO_A_GLOBAL_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("GLOBAL-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_GLOBAL_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("GLOBAL-FUNCTION-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-FUNCTION-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_CONSTANT_SYM_REF:
	    {
		idio_ai_t mci = idio_vm_get_varuint (bc, pcp);

		IDIO fmci = idio_fixnum (mci);
		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO c = idio_vm_constants_ref (gci);

		IDIO_VM_DASM ("CONSTANT SYM %5td", mci);
		idio_debug_FILE (idio_dasm_FILE, " %s", c);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_GLOBAL_SYM_DEF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t mkci = idio_vm_get_varuint (bc, pcp);

		IDIO ce = idio_thread_current_env ();
		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO fgkci = idio_module_get_or_set_vci (ce, idio_fixnum (mkci));

		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
		IDIO kind = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgkci));

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("GLOBAL-SYM-DEF %" PRId64 " %s as %s", mci, IDIO_SYMBOL_S (sym), IDIO_SYMBOL_S (kind));
		} else {
		    IDIO_VM_DASM ("GLOBAL-SYM-DEF %" PRId64 " %s", mci, idio_type2string (sym));
		    idio_debug_FILE (idio_dasm_FILE, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_GLOBAL_SYM_SET:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), idio_fixnum (mci));
		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("GLOBAL-SYM-SET %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
		} else {
		    IDIO_VM_DASM ("GLOBAL-SYM-SET %" PRId64 " %s", mci, idio_type2string (sym));
		    idio_debug_FILE (idio_dasm_FILE, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_SET:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-SET %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_DEF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-DEF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_GLOBAL_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("GLOBAL-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_GLOBAL_FUNCTION_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("GLOBAL-FUNCTION-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("CHECKED-GLOBAL-FUNCTION-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_CONSTANT_VAL_REF:
	    {
		idio_ai_t gvi = idio_vm_get_varuint (bc, pcp);

		IDIO fgvi = idio_fixnum (gvi);
		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fgvi);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO c = idio_vm_constants_ref (gci);

		IDIO_VM_DASM ("CONSTANT VAL %5td", gvi);
		idio_debug_FILE (idio_dasm_FILE, " %s", c);
	    }
	    break;
	case IDIO_A_COMPUTED_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_GLOBAL_VAL_DEF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);
		/* uint64_t mkci = idio_vm_get_varuint (bc, pcp); */

		IDIO ce = idio_thread_current_env ();
		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (gvi));
		/* IDIO fgkci = idio_module_get_or_set_vci (ce, idio_fixnum (mkci)); */

		IDIO val = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));
		/* IDIO kind = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgkci)); */

		if (idio_isa_symbol (val)) {
		    IDIO_VM_DASM ("GLOBAL-VAL-DEF %" PRId64 " %s", gvi, IDIO_SYMBOL_S (val));
		} else {
		    IDIO_VM_DASM ("GLOBAL-VAL-DEF %" PRId64 " %s", gvi, idio_type2string (val));
		    idio_debug_FILE (idio_dasm_FILE, " %s", val);
		}
	    }
	    break;
	case IDIO_A_GLOBAL_VAL_SET:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("GLOBAL-VAL-SET %" PRId64, gvi);
	    }
	    break;
	case IDIO_A_COMPUTED_VAL_SET:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-VAL-SET %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_COMPUTED_VAL_DEF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-VAL-DEF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_PREDEFINED0:
	    {
		IDIO_VM_DASM ("PREDEFINED 0 #t");
	    }
	    break;
	case IDIO_A_PREDEFINED1:
	    {
		IDIO_VM_DASM ("PREDEFINED 1 #f");
	    }
	    break;
	case IDIO_A_PREDEFINED2:
	    {
		IDIO_VM_DASM ("PREDEFINED 2 #nil");
	    }
	    break;
	case IDIO_A_PREDEFINED:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		IDIO pd = idio_vm_values_ref (vi);
		if (idio_isa_primitive (pd)) {
		    IDIO_VM_DASM ("PREDEFINED %" PRId64 " PRIM %s", vi, IDIO_PRIMITIVE_NAME (pd));
		} else {
		    IDIO_VM_DASM ("PREDEFINED %" PRId64 " %s", vi, idio_type2string (pd));
		}
	    }
	    break;
	case IDIO_A_LONG_GOTO:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LG@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("LONG-GOTO +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_LONG_JUMP_FALSE:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LJF@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("LONG-JUMP-FALSE +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_LONG_JUMP_TRUE:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LJT@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("LONG-JUMP-TRUE +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_GOTO:
	    {
		int i = IDIO_IA_GET_NEXT (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SG@%td", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("SHORT-GOTO +%d %td", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_FALSE:
	    {
		int i = IDIO_IA_GET_NEXT (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SJF@%td", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("SHORT-JUMP-FALSE +%d %td", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_TRUE:
	    {
		int i = IDIO_IA_GET_NEXT (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SJT@%td", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("SHORT-JUMP-TRUE %d %td", i, pc + i);
	    }
	    break;
	case IDIO_A_PUSH_VALUE:
	    {
		IDIO_VM_DASM ("PUSH-VALUE");
	    }
	    break;
	case IDIO_A_POP_VALUE:
	    {
		IDIO_VM_DASM ("POP-VALUE");
	    }
	    break;
	case IDIO_A_POP_REG1:
	    {
		IDIO_VM_DASM ("POP-REG1");
	    }
	    break;
	case IDIO_A_POP_REG2:
	    {
		IDIO_VM_DASM ("POP-REG2");
	    }
	    break;
	case IDIO_A_SRC_EXPR:
	    {
		idio_ai_t mci = idio_vm_get_varuint (bc, pcp);

		IDIO fmci = idio_fixnum (mci);
		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO e = idio_vm_constants_ref (gci);
		IDIO lo = idio_hash_ref (idio_src_properties, e);

		IDIO_VM_DASM ("SRC-EXPR %td", mci);
		if (idio_S_unspec == lo) {
		    IDIO_VM_DASM (" %-25s", "<no lexobj>");
		} else {
		    idio_debug_FILE (idio_dasm_FILE, " %s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME));
		    idio_debug_FILE (idio_dasm_FILE, ":line %s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE));
		}
		idio_debug_FILE (idio_dasm_FILE, " %s", e);
	    }
	    break;
	case IDIO_A_POP_FUNCTION:
	    {
		IDIO_VM_DASM ("POP-FUNCTION");
	    }
	    break;
	case IDIO_A_PRESERVE_STATE:
	    {
		IDIO_VM_DASM ("PRESERVE-STATE");
	    }
	    break;
	case IDIO_A_RESTORE_STATE:
	    {
		IDIO_VM_DASM ("RESTORE-STATE");
	    }
	    break;
	case IDIO_A_RESTORE_ALL_STATE:
	    {
		IDIO_VM_DASM ("RESTORE-ALL-STATE");
	    }
	    break;
	case IDIO_A_CREATE_CLOSURE:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		/* uint64_t code_len = */ idio_vm_get_varuint (bc, pcp);
		uint64_t ssci = idio_vm_get_varuint (bc, pcp);
		uint64_t dsci = idio_vm_get_varuint (bc, pcp);
		uint64_t slci = idio_vm_get_varuint (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "C@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("CREATE-CLOSURE @ +%" PRId64 " %" PRId64 "", i, pc + i);

		IDIO ce = idio_thread_current_env ();

		/* sigstr lookup */
		IDIO fssci = idio_fixnum (ssci);
		IDIO fgssci = idio_module_get_or_set_vci (ce, fssci);
		IDIO ss = idio_S_nil;
		if (idio_S_unspec != fgssci) {
		    ss = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgssci));
		} else {
		    fprintf (stderr, "vm cc sig: failed to find %" PRIu64 "\n", ssci);
		}
		size_t size = 0;
		char *ids = idio_display_string (ss, &size);
		IDIO_VM_DASM (" args=%s", ids);
		IDIO_GC_FREE (ids, size);

		/* docstr lookup */
		IDIO fdsci = idio_fixnum (dsci);
		IDIO fgdsci = idio_module_get_or_set_vci (ce, fdsci);
		IDIO ds = idio_S_nil;
		if (idio_S_unspec != fgdsci) {
		    ds = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgdsci));
		} else {
		    fprintf (stderr, "vm cc doc: failed to find %" PRIu64 "\n", dsci);
		}
		if (idio_S_nil != ds) {
		    size = 0;
		    ids = idio_as_string_safe (ds, &size, 1, 1);
		    IDIO_VM_DASM ("\n%s", ids);
		    IDIO_GC_FREE (ids, size);
		}

		/* srcloc lookup */
		IDIO fslci = idio_fixnum (slci);
		IDIO fgslci = idio_module_get_or_set_vci (ce, fslci);
		IDIO sl = idio_S_nil;
		if (idio_S_unspec != fgslci) {
		    sl = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgslci));
		} else {
		    fprintf (stderr, "vm cc doc: failed to find %" PRIu64 "\n", slci);
		}
		if (idio_S_nil != sl) {
		    size = 0;
		    ids = idio_as_string_safe (sl, &size, 1, 1);
		    IDIO_VM_DASM ("\n%s", ids);
		    IDIO_GC_FREE (ids, size);
		}
	    }
	    break;
	case IDIO_A_FUNCTION_INVOKE:
	    {
		IDIO_VM_DASM ("FUNCTION-INVOKE ... ");
	    }
	    break;
	case IDIO_A_FUNCTION_GOTO:
	    {
		IDIO_VM_DASM ("FUNCTION-GOTO ...");
	    }
	    break;
	case IDIO_A_RETURN:
	    {
		IDIO_VM_DASM ("RETURN\n");
	    }
	    break;
	case IDIO_A_PUSH_ABORT:
	    {
		uint64_t o = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "A@%" PRId64 "", pc + o);
		idio_hash_put (hints, idio_fixnum (pc + o), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("PUSH-ABORT to PC +%" PRIu64 " %" PRId64, o, pc + o);
	    }
	    break;
	case IDIO_A_POP_ABORT:
	    {
		IDIO_VM_DASM ("POP-ABORT");
	    }
	    break;
	case IDIO_A_FINISH:
	    {
		IDIO_VM_DASM ("FINISH");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME1:
	    {
		/* no args, no need to pull an empty list ref */
		IDIO_VM_DASM ("ALLOCATE-FRAME 1");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME2:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 2");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME3:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 3");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME4:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 4");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME5:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 5");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ALLOCATE-FRAME %" PRId64, i);
	    }
	    break;
	case IDIO_A_ALLOCATE_DOTTED_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ALLOCATE-DOTTED-FRAME %" PRId64, arity);
	    }
	    break;
	case IDIO_A_REUSE_FRAME:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("REUSE-FRAME %" PRId64, i);
	    }
	    break;
	case IDIO_A_POP_FRAME0:
	    {
		IDIO_VM_DASM ("POP-FRAME 0");
	    }
	    break;
	case IDIO_A_POP_FRAME1:
	    {
		IDIO_VM_DASM ("POP-FRAME 1");
	    }
	    break;
	case IDIO_A_POP_FRAME2:
	    {
		IDIO_VM_DASM ("POP-FRAME 2");
	    }
	    break;
	case IDIO_A_POP_FRAME3:
	    {
		IDIO_VM_DASM ("POP-FRAME 3");
	    }
	    break;
	case IDIO_A_POP_FRAME:
	    {
		uint64_t rank = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("POP-FRAME %" PRId64 "", rank);
	    }
	    break;
	case IDIO_A_LINK_FRAME:
	    {
		uint64_t ssci = idio_vm_get_varuint (bc, pcp);
		IDIO names = idio_vm_constants_ref (ssci);
		IDIO_VM_DASM ("LINK-FRAME sci=%" PRId64, ssci);
		idio_debug_FILE (idio_dasm_FILE, " %s", names);
	    }
	    break;
	case IDIO_A_UNLINK_FRAME:
	    {
		IDIO_VM_DASM ("UNLINK-FRAME");
	    }
	    break;
	case IDIO_A_PACK_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("PACK-FRAME %" PRId64 "", arity);
	    }
	    break;
	case IDIO_A_POP_LIST_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("POP-LIST-FRAME %" PRId64 "", arity);
	    }
	    break;
	case IDIO_A_EXTEND_FRAME:
	    {
		uint64_t alloc = idio_vm_get_varuint (bc, pcp);
		uint64_t ssci = idio_vm_get_varuint (bc, pcp);
		IDIO names = idio_vm_constants_ref (ssci);

		IDIO_VM_DASM ("EXTEND-FRAME %" PRId64 " sci=%" PRId64, alloc, ssci);
		idio_debug_FILE (idio_dasm_FILE, " %s", names);
	    }
	    break;
	case IDIO_A_ARITY1P:
	    {
		IDIO_VM_DASM ("ARITY=1?");
	    }
	    break;
	case IDIO_A_ARITY2P:
	    {
		IDIO_VM_DASM ("ARITY=2?");
	    }
	    break;
	case IDIO_A_ARITY3P:
	    {
		IDIO_VM_DASM ("ARITY=3?");
	    }
	    break;
	case IDIO_A_ARITY4P:
	    {
		IDIO_VM_DASM ("ARITY=4?");
	    }
	    break;
	case IDIO_A_ARITYEQP:
	    {
		uint64_t arityp1 = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ARITY=? %" PRId64 "", arityp1);
	    }
	    break;
	case IDIO_A_ARITYGEP:
	    {
		uint64_t arityp1 = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ARITY>=? %" PRId64 "", arityp1);
	    }
	    break;
	case IDIO_A_CONSTANT_0:
	    {
		IDIO_VM_DASM ("CONSTANT       0");
	    }
	    break;
	case IDIO_A_CONSTANT_1:
	    {
		IDIO_VM_DASM ("CONSTANT       1");
	    }
	    break;
	case IDIO_A_CONSTANT_2:
	    {
		IDIO_VM_DASM ("CONSTANT       2");
	    }
	    break;
	case IDIO_A_CONSTANT_3:
	    {
		IDIO_VM_DASM ("CONSTANT       3");
	    }
	    break;
	case IDIO_A_CONSTANT_4:
	    {
		IDIO_VM_DASM ("CONSTANT       4");
	    }
	    break;
	case IDIO_A_FIXNUM:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("FIXNUM %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_NEG_FIXNUM:
	    {
		int64_t v = idio_vm_get_varuint (bc, pcp);
		v = -v;
		IDIO_VM_DASM ("NEG-FIXNUM %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_CONSTANT:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("CONSTANT     %5" PRIu64 "", v);
		size_t size = 0;
		char *ids = idio_display_string (IDIO_CONSTANT_IDIO ((intptr_t) v), &size);
		IDIO_VM_DASM (" %s", ids);
		IDIO_GC_FREE (ids, size);
	    }
	    break;
	case IDIO_A_NEG_CONSTANT:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		v = -v;
		IDIO_VM_DASM ("NEG-CONSTANT   %6" PRId64 "", v);
		size_t size = 0;
		char *ids = idio_display_string (IDIO_CONSTANT_IDIO ((intptr_t) v), &size);
		IDIO_VM_DASM (" %s", ids);
		IDIO_GC_FREE (ids, size);
	    }
	    break;
	case IDIO_A_UNICODE:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("UNICODE #U+%04" PRIX64 "", v);
	    }
	    break;
	case IDIO_A_NOP:
	    {
		IDIO_VM_DASM ("NOP");
	    }
	    break;
	case IDIO_A_PRIMCALL0:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		IDIO primdata = idio_vm_values_ref (vi);
		IDIO_VM_DASM ("PRIMITIVE0 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    }
	    break;
	case IDIO_A_PRIMCALL1:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		IDIO primdata = idio_vm_values_ref (vi);
		IDIO_VM_DASM ("PRIMITIVE1 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    }
	    break;
	case IDIO_A_PRIMCALL2:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		IDIO primdata = idio_vm_values_ref (vi);
		IDIO_VM_DASM ("PRIMITIVE2 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
	    }
	    break;
	case IDIO_A_SUPPRESS_RCSE:
	    IDIO_VM_DASM ("SUPPRESS-RCSE");
	    break;
	case IDIO_A_POP_RCSE:
	    IDIO_VM_DASM ("POP-RCSE");
	    break;
	case IDIO_A_NOT:
	    IDIO_VM_DASM ("NOT");
	    break;
	case IDIO_A_EXPANDER:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("EXPANDER %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_INFIX_OPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("INFIX-OPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_POSTFIX_OPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("POSTFIX-OPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_PUSH_DYNAMIC:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), idio_fixnum (mci));
		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("PUSH-DYNAMIC %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_POP_DYNAMIC:
	    {
		IDIO_VM_DASM ("POP-DYNAMIC");
	    }
	    break;
	case IDIO_A_DYNAMIC_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), idio_fixnum (mci));
		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("DYNAMIC-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), idio_fixnum (mci));
		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("DYNAMIC-FUNCTION-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_PUSH_ENVIRON:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), idio_fixnum (mci));
		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("PUSH-ENVIRON %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_POP_ENVIRON:
	    {
		IDIO_VM_DASM ("POP-ENVIRON");
	    }
	    break;
	case IDIO_A_ENVIRON_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), idio_fixnum (mci));
		IDIO sym = idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("ENVIRON-SYM-REF %" PRIu64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_NON_CONT_ERR:
	    {
		IDIO_VM_DASM ("NON-CONT-ERROR");
	    }
	    break;
	case IDIO_A_PUSH_TRAP:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("PUSH-TRAP %" PRIu64, mci);
	    }
	    break;
	case IDIO_A_POP_TRAP:
	    {
		IDIO_VM_DASM ("POP-TRAP");
	    }
	    break;
	case IDIO_A_RESTORE_TRAP:
	    {
		IDIO_VM_DASM ("RESTORE-TRAP");
	    }
	    break;
	case IDIO_A_PUSH_ESCAPER:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t offset = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("PUSH-ESCAPER %" PRIu64 " -> %" PRIu64, mci, pc + offset + 1);
	    }
	    break;
	case IDIO_A_POP_ESCAPER:
	    {
		IDIO_VM_DASM ("POP-ESCAPER");
	    }
	    break;
	case IDIO_A_ESCAPER_LABEL_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("ESCAPER-LABEL-REF %" PRIu64, mci);
	    }
	    break;
	default:
	    {
		/*
		 * Test Case: ??
		 *
		 * Coding error.  Also not in sync with idio_vm_run1()!
		 */
		idio_ai_t pci = pc - 1;
		idio_ai_t pcm = pc + 10;
		pc = pc - 10;
		if (pc < 0) {
		    pc = 0;
		}
		fprintf (stderr, "idio-vm-dasm: unexpected ins %3d @%td\n", ins, pci);
		fprintf (stderr, "dumping from %td to %td\n", pc, pcm - 1);
		if (pc % 10) {
		    idio_ai_t pc1 = pc - (pc % 10);
		    fprintf (stderr, "\n  %5td ", pc1);
		    for (; pc1 < pc; pc1++) {
			fprintf (stderr, "    ");
		    }
		}
		for (; pc < pcm; pc++) {
		    if (0 == (pc % 10)) {
			fprintf (stderr, "\n  %5td ", pc);
		    }
		    fprintf (stderr, "%3d ", IDIO_IA_AE (bc, pc));
		}
		fprintf (stderr, "\n");
		IDIO_VM_DASM ("-- ?? --\n");
		continue;
		return;
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected instruction: %3d @%" PRId64 "\n", ins, pci);

		/* notreached */
		return;
	    }
	    break;
	}

	IDIO_VM_DASM ("\n");
    }
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%%idio-dasm", dasm, (IDIO args), "[c]", "\
generate the disassembler code for closure `c` or everything	\n\
								\n\
:param c: (optional) the closure to disassemble			\n\
:type c: closure						\n\
								\n\
The output goes to the file \"vm-dasm\" in the current directory.	\n\
It will get overwritten when Idio stops.			\n\
")
{
    IDIO_ASSERT (args);

    idio_ai_t pc0 = 0;
    idio_ai_t pce = 0;

    if (idio_isa_pair (args)) {
	IDIO c = IDIO_PAIR_H (args);
	if (idio_isa_closure (c)) {
	    pc0 = IDIO_CLOSURE_CODE_PC (c);
	    pce = pc0 + IDIO_CLOSURE_CODE_LEN (c);
	} else {
	    /*
	     * Test Case: vm-errors/idio-dasm-bad-type.idio
	     *
	     * %%idio-dasm #t
	     */
	    idio_error_param_type ("closure", c, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    idio_vm_dasm (idio_thread_current_thread (), idio_all_code, pc0, pce);

    return idio_S_unspec;
}

void idio_vm_thread_init (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (thr));

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));
#else
    idio_ai_t tsp = idio_vm_find_stack_marker (IDIO_THREAD_STACK (thr), idio_SM_trap, 0, 0);
#endif
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
	IDIO_THREAD_STACK_PUSH (idio_condition_condition_type_mci);
	IDIO_THREAD_STACK_PUSH (idio_condition_reset_condition_handler);
	IDIO_THREAD_STACK_PUSH (idio_SM_trap);
#ifdef IDIO_VM_DYNAMIC_REGISTERS
	IDIO_THREAD_TRAP_SP (thr) = idio_fixnum (sp + 3);
	IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
#endif
    }

    idio_vm_push_trap (thr, idio_condition_restart_condition_handler, idio_condition_condition_type_mci, 0);
    idio_vm_push_trap (thr, idio_condition_default_condition_handler, idio_condition_condition_type_mci, 0);
    IDIO fvci = idio_fixnum (idio_vm_constants_lookup (IDIO_SYMBOLS_C_INTERN (IDIO_CONDITION_RCSE_TYPE_NAME)));
    idio_vm_push_trap (thr, idio_condition_default_rcse_handler, fvci, 0);
    fvci = idio_fixnum (idio_vm_constants_lookup (IDIO_SYMBOLS_C_INTERN (IDIO_CONDITION_RACSE_TYPE_NAME)));
    idio_vm_push_trap (thr, idio_condition_default_racse_handler, fvci, 0);
    fvci = idio_fixnum (idio_vm_constants_lookup (IDIO_SYMBOLS_C_INTERN (IDIO_CONDITION_RT_SIGCHLD_TYPE_NAME)));
    idio_vm_push_trap (thr, idio_condition_default_SIGCHLD_handler, fvci, 0);
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
    IDIO_THREAD_PC (thr) = IDIO_IA_USIZE (idio_all_code);
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

IDIO idio_vm_run (IDIO thr, idio_ai_t pc, int caller)
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

    /*
     * Save a continuation in case things get ropey and we have to
     * bail out.
     */
    volatile idio_ai_t krun_p0 = idio_array_size (idio_vm_krun);
    if (0 == krun_p0 &&
	thr != idio_expander_thread) {
	fprintf (stderr, "How is krun 0?\n");
	/*
	 * experience suggests that things are bad, now
	 */
	IDIO_C_ASSERT (0);
    }

    IDIO_THREAD_PC (thr) = pc;
    volatile idio_ai_t v_PC0 = IDIO_THREAD_PC (thr);
    volatile idio_ai_t v_ss0 = idio_array_size (IDIO_THREAD_STACK (thr));

    if (IDIO_VM_RUN_C == caller) {
	/*
	 * make sure this segment returns to idio_vm_FINISH_pc
	 */
	IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
	IDIO_THREAD_STACK_PUSH (idio_SM_return);
    }

    /*
     * We get called from places where code has been generated but no
     * RETURN is appended.
     */
    idio_ia_push (idio_all_code, IDIO_A_RETURN);

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
	fprintf (stderr, "thr      = %s\n", idio_type2string (thr));
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
     * (0x0) to large (0xffffffff).  s9-test.idio has enough happening
     * to show the effect of using a large value -- virtual memory
     * usage should reach 1+GB.
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
			IDIO_THREAD_STACK_PUSH (idio_SM_return);

			idio_vm_preserve_all_state (thr);
#ifndef IDIO_VM_DYNAMIC_REGISTERS
			/*
			 * Duplicate the existing top-most trap to
			 * have something to pop off
			 */
			IDIO stack = IDIO_THREAD_STACK (thr);
			idio_ai_t next_tsp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0);
			idio_vm_push_trap (thr,
					   idio_array_ref_index (stack, next_tsp - 1),
					   idio_array_ref_index (stack, next_tsp - 2),
					   IDIO_FIXNUM_VAL (idio_array_ref_index (stack, next_tsp - 3)));
#endif
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
	fprintf (fh, "vm_run: %10" PRIdPTR " ins in time %3ld.%03ld => %6" PRIdPTR " i/ms\n", loops, td.tv_sec, (long) td.tv_usec / 1000, ipms);
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
    int bail = 0;
    IDIO krun = idio_S_nil;
    idio_ai_t krun_p = 0;
    if (IDIO_VM_RUN_C == caller) {
	if (IDIO_THREAD_PC (thr) != (idio_vm_FINISH_pc + 1)) {
	    fprintf (stderr, "vm-run: THREAD %td failed to run to FINISH: PC %td != %td\n", v_PC0, IDIO_THREAD_PC (thr), (idio_vm_FINISH_pc + 1));
	    idio_vm_dasm (thr, idio_all_code, v_PC0, IDIO_THREAD_PC (thr));
	    bail = 1;
	}

	idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

	if (ss != v_ss0) {
	    fprintf (stderr, "vm-run: THREAD failed to consume stack: SP0 %td -> %td\n", v_ss0 - 1, ss - 1);
	    idio_vm_decode_thread (thr);
	    if (ss < v_ss0) {
		fprintf (stderr, "\n\nNOTICE: current stack smaller than when we started\n");
	    }
	    bail = 1;
	}

	/*
	 * ABORT and others will have added to idio_vm_krun with some
	 * abandon but are in no position to repair the krun stack
	 */
	krun_p = idio_array_size (idio_vm_krun);
	idio_ai_t krun_pd = krun_p - krun_p0;
	if (krun_pd > 1) {
	    fprintf (stderr, "vm-run: krun: popping %td to #%td\n", krun_pd, krun_p0);
	}
	while (krun_p > krun_p0) {
	    krun = idio_array_pop (idio_vm_krun);
	    krun_p--;
	}
	if (krun_pd > 1) {
	    idio_gc_collect_gen ("vm-run: pop krun");
	}

	if (bail) {
	    if (idio_isa_pair (krun)) {
		fprintf (stderr, "vm-run/bail: restoring krun #%td: ", krun_p - 1);
		idio_debug ("%s\n", IDIO_PAIR_HT (krun));
		idio_vm_restore_continuation (IDIO_PAIR_H (krun), idio_S_unspec);

		return idio_S_notreached;
	    } else {
		fprintf (stderr, "vm-run/bail: nothing to restore => exit (1)\n");
		idio_exit_status = 1;
		idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

		return idio_S_notreached;
	    }
	}
    }

    return r;
}

IDIO idio_vm_run_C (IDIO thr, idio_ai_t pc)
{
    IDIO_ASSERT (thr);
    IDIO_C_ASSERT (pc);

    IDIO_TYPE_ASSERT (thread, thr);

    return idio_vm_run (thr, pc, IDIO_VM_RUN_C);
}

IDIO_DEFINE_PRIMITIVE2_DS ("vm-run", vm_run, (IDIO thr, IDIO PC), "thr PC", "\
run code at `PC` in thread `thr`		\n\
						\n\
:param thr: thread to run			\n\
:type thr: thread				\n\
:param PC: PC to use				\n\
:type PC: fixnum				\n\
:return: *val* register				\n\
")
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (PC);

    /*
     * Test Case: vm-errors/vm-run-bad-thread-type.idio
     *
     * vm-run #t #t
     */
    IDIO_USER_TYPE_ASSERT (thread, thr);
    /*
     * Test Case: vm-errors/vm-run-bad-PC-type.idio
     *
     * vm-run (threading/current-thread) #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, PC);

    /*
     * We've been called from Idio-land to start running some code --
     * usually that which has just been generated by the {codegen}
     * primitive.
     *
     * This is ostensibly a function call so push the current PC on
     * the stack, right?
     *
     * Not quite, this call to a primitive was already inside
     * idio_vm_run() so if we set, say, the current PC on the stack to
     * be returned to and call idio_vm_run() then it will
     *
     * 1. run this new bit of code which will
     *
     * 2. RETURN to the PC on the stack (the caller of this primitive)
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
     * However, before we return to our caller we must set the PC back
     * to the original PC we saw when we were called in order that our
     * caller can continue.
     *
     * NB.  If you don't do that final part you may get an obscure
     * "PANIC: restore-trap" failure.  That's because the PC *after*
     * idio_vm_PC is the instruction in the prologue for, er,
     * restoring a trap.  It's not meant to be called next!
     */

    idio_ai_t PC0 = IDIO_THREAD_PC (thr);

    IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
    IDIO_THREAD_STACK_PUSH (idio_SM_return);

    IDIO r = idio_vm_run (thr, IDIO_FIXNUM_VAL (PC), IDIO_VM_RUN_IDIO);

    IDIO_THREAD_PC (thr) = PC0;

    return r;
}

idio_ai_t idio_vm_extend_constants (IDIO v)
{
    IDIO_ASSERT (v);

    idio_ai_t gci = idio_array_size (idio_vm_constants);
    idio_array_push (idio_vm_constants, v);
    if (idio_S_nil != v) {
	idio_hash_put (idio_vm_constants_hash, v, idio_fixnum (gci));
    }
    return gci;
}

IDIO idio_vm_constants_ref (idio_ai_t gci)
{
    if (gci > idio_array_size (idio_vm_constants)) {
	IDIO_C_ASSERT (0);
    }
    return idio_array_ref_index (idio_vm_constants, gci);
}

idio_ai_t idio_vm_constants_lookup (IDIO name)
{
    IDIO_ASSERT (name);

    if (idio_S_nil != name) {
	IDIO fgci = idio_hash_ref (idio_vm_constants_hash, name);
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
    return idio_array_find_eqp (idio_vm_constants, name, 0);
}

idio_ai_t idio_vm_constants_lookup_or_extend (IDIO name)
{
    IDIO_ASSERT (name);

    idio_ai_t gci = idio_vm_constants_lookup (name);
    if (-1 == gci) {
	gci = idio_vm_extend_constants (name);
    }

    return gci;
}

void idio_vm_dump_constants ()
{
#ifdef IDIO_DEBUG
	fprintf (stderr, "vm-constants ");
#endif
    FILE *fp = fopen ("vm-constants", "w");
    if (NULL == fp) {
	perror ("fopen (vm-constants, w)");
	return;
    }

    idio_ai_t al = idio_array_size (idio_vm_constants);
    fprintf (fp, "idio_vm_constants: %td\n", al);
    idio_ai_t i;
    for (i = 0 ; i < al; i++) {
	IDIO c = idio_array_ref_index (idio_vm_constants, i);
	fprintf (fp, "%6td: ", i);
	size_t size = 0;
	char *cs = idio_as_string_safe (c, &size, 40, 1);
	fprintf (fp, "%-20s %s\n", idio_type2string (c), cs);
	IDIO_GC_FREE (cs, size);
    }

    fclose (fp);
}

IDIO_DEFINE_PRIMITIVE0_DS ("vm-constants", vm_constants, (void), "", "\
Return the current vm constants array.		\n\
						\n\
:return: vm constants				\n\
:type args: array				\n\
")
{
    return idio_vm_constants;
}

idio_ai_t idio_vm_extend_values ()
{
    idio_ai_t i = idio_array_size (idio_vm_values);
    idio_array_push (idio_vm_values, idio_S_undef);
    return i;
}

IDIO_DEFINE_PRIMITIVE0_DS ("vm-extend-values", vm_extend_values, (void), "", "\
Extend the VM's values array			\n\
						\n\
:return: index					\n\
:rtype: integer					\n\
")
{
    idio_ai_t gvi = idio_vm_extend_values ();

    return idio_integer (gvi);
}

IDIO idio_vm_values_ref (idio_ai_t gvi)
{
    if (gvi) {
	IDIO v = idio_array_ref_index (idio_vm_values, gvi);

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

IDIO_DEFINE_PRIMITIVE1_DS ("vm-values-ref", vm_values_ref, (IDIO index), "index", "\
Return the VM's values array entry at `index`	\n\
						\n\
:param index: index				\n\
:type index: integer				\n\
:return: value					\n\
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

    return idio_vm_values_ref (gvi);
}

void idio_vm_values_set (idio_ai_t gvi, IDIO v)
{
    idio_array_insert_index (idio_vm_values, v, gvi);
}

void idio_vm_dump_values ()
{
#ifdef IDIO_DEBUG
	fprintf (stderr, "vm-values ");
#endif
    FILE *fp = fopen ("vm-values", "w");
    if (NULL == fp) {
	perror ("fopen (vm-values, w)");
	return;
    }

    /*
     * The printer will update {seen} and potentially call some
     * Idio-code for structures.  That means we're at risk of garbage
     * collection.
     */
    idio_gc_pause ("vm-dump-values");

    IDIO Rx = IDIO_SYMBOLS_C_INTERN ("Rx");

    idio_ai_t al = idio_array_size (idio_vm_values);
    fprintf (fp, "idio_vm_values: %td\n", al);
    idio_ai_t i;
    for (i = 0 ; i < al; i++) {
	IDIO v = idio_array_ref_index (idio_vm_values, i);
	fprintf (fp, "%6td: ", i);
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
	fprintf (fp, "%-20s %s\n", idio_type2string (v), vs);
	IDIO_GC_FREE (vs, size);
    }

    idio_gc_resume ("vm-dump-values");

    fclose (fp);
}

void idio_vm_thread_state (IDIO thr)
{
    IDIO_ASSERT (thr);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_vm_debug (thr, "vm-thread-state", 0);
    fprintf (stderr, "\n");

    IDIO frame = IDIO_THREAD_FRAME (thr);
    while (idio_S_nil != frame) {
	IDIO faci = IDIO_FRAME_NAMES (frame);
	IDIO names = idio_S_nil;
	names = idio_vm_constants_ref (IDIO_FIXNUM_VAL (faci));

	fprintf (stderr, "vm-thread-state: frame: %10p (%10p) %2u/%2u %5" PRIdPTR, frame, IDIO_FRAME_NEXT (frame), IDIO_FRAME_NPARAMS (frame), IDIO_FRAME_NALLOC (frame), IDIO_FIXNUM_VAL (faci));
	idio_debug (" - %-20s - ", names);
	idio_debug ("%s\n", idio_frame_args_as_list (frame));
	frame = IDIO_FRAME_NEXT (frame);
    }

    fprintf (stderr, "\n");

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
#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
#else
    idio_ai_t dsp = idio_vm_find_stack_marker (stack, idio_SM_dynamic, 0, 0);
#endif
    while (dsp != -1) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "vm-thread-state: dynamic: SP %3td ", dsp);
	idio_debug (" next %s", idio_array_ref_index (stack, dsp - 3));
	idio_debug (" vi %s", idio_array_ref_index (stack, dsp - 1));
	idio_debug (" val %s\n", idio_array_ref_index (stack, dsp - 2));
	dsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, dsp - 3));
    }

    header = 1;
#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
#else
    idio_ai_t esp = idio_vm_find_stack_marker (stack, idio_SM_environ, 0, 0);
#endif
    while (esp != -1) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	fprintf (stderr, "vm-thread-state: environ: SP %3td ", esp);
	idio_debug ("= %s\n", idio_array_ref_index (stack, esp - 1));
	idio_debug (" next %s", idio_array_ref_index (stack, dsp - 3));
	idio_debug (" vi %s", idio_array_ref_index (stack, dsp - 1));
	idio_debug (" val %s\n", idio_array_ref_index (stack, dsp - 2));
	esp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, esp - 3));
    }

    header = 1;
    idio_ai_t krun_p = idio_array_size (idio_vm_krun) - 1;
    while (krun_p >= 0) {
	if (header) {
	    header = 0;
	    fprintf (stderr, "\n");
	}
	IDIO krun = idio_array_ref_index (idio_vm_krun, krun_p);
	fprintf (stderr, "vm-thread-state: krun: % 3td", krun_p + 1);
	idio_debug (" %s\n", IDIO_PAIR_HT (krun));
	krun_p--;
    }

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

time_t idio_vm_elapsed (void)
{
    return (time ((time_t *) NULL) - idio_vm_t0);
}

IDIO_DEFINE_PRIMITIVE2_DS ("run-in-thread", run_in_thread, (IDIO thr, IDIO func, IDIO args), "thr func [args]", "\
Run `func [args]` in thread `thr`.				\n\
								\n\
:param thr: the thread						\n\
:type thr: thread						\n\
:param func: a function	or ``#n``				\n\
:type func: function						\n\
:param args: (optional) arguments to `func`			\n\
:type args: list						\n\
")
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (func);
    IDIO_ASSERT (args);

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
    IDIO_USER_TYPE_ASSERT (function, func);

    IDIO cthr = idio_thread_current_thread ();

    idio_thread_set_current_thread (thr);

    idio_ai_t pc0 = IDIO_THREAD_PC (thr);
    idio_vm_default_pc (thr);

    IDIO r = idio_apply (func, args);

    if (IDIO_THREAD_PC (thr) != pc0) {
	IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
	IDIO_THREAD_STACK_PUSH (idio_SM_return);

	r = idio_vm_run (thr, IDIO_THREAD_PC (thr), IDIO_VM_RUN_IDIO);

	idio_ai_t pc = IDIO_THREAD_PC (thr);
	if (pc == (idio_vm_FINISH_pc + 1)) {
	    IDIO_THREAD_PC (thr) = pc0;
	}
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

    while (idio_S_nil != frame) {
	IDIO faci = IDIO_FRAME_NAMES (frame);
	idio_ai_t aci = IDIO_FIXNUM_VAL (faci);
	IDIO names = idio_S_nil;
	names = idio_vm_constants_ref (aci);
	if (0 == aci) {
	    fprintf (stderr, "  ?? aci=%td ", aci);
	    idio_debug ("%s\n", names);
	}

	/*
	 * formal parameters -- marked with *
	 */
	idio_ai_t al = IDIO_FRAME_NPARAMS (frame);
	idio_ai_t i;
	for (i = 0; i < al; i++) {
	    fprintf (stderr, "  %2d %2td* ", depth, i);
	    if (idio_S_nil != names) {
		idio_debug ("%15s = ", IDIO_PAIR_H (names));
		names = IDIO_PAIR_T (names);
	    } else {
		fprintf (stderr, "%15s = ", "?");
	    }
	    idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));
	}

	/*
	 * varargs element -- probably named #f
	 */
	fprintf (stderr, "  %2d %2td  ", depth, i);
	if (idio_S_nil != names) {
	    if (idio_S_false == IDIO_PAIR_H (names)) {
		fprintf (stderr, "%15s = ", "*");
	    } else {
		idio_debug ("%15s = ", IDIO_PAIR_H (names));
	    }
	    names = IDIO_PAIR_T (names);
	} else {
	    fprintf (stderr, "%15s = ", "?");
	}
	idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));

	/*
	 * "locals"
	 */
	al = IDIO_FRAME_NALLOC (frame);
	for (i++; i < al; i++) {
	    fprintf (stderr, "  %2d %2td  ", depth, i);
	    if (idio_S_nil != names) {
		idio_debug ("%15s = ", IDIO_PAIR_H (names));
		names = IDIO_PAIR_T (names);
	    } else {
		fprintf (stderr, "%15s = ", "?");
	    }
	    idio_debug ("%s\n", IDIO_FRAME_ARGS (frame, i));
	}
	fprintf (stderr, "\n");

	depth++;
	frame = IDIO_FRAME_NEXT (frame);
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
    idio_ai_t ss = idio_array_size (stack);

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_TYPE_ASSERT (fixnum, IDIO_THREAD_TRAP_SP (thr));
    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));
#else
    idio_ai_t tsp = idio_vm_find_stack_marker (stack, idio_SM_trap, 0, 0);
#endif

    if (tsp > ss) {
	fprintf (stderr, "TRAP SP %td > size (stack) %td\n", tsp, ss);
    } else {
	while (1) {
	    fprintf (stderr, "vm-thread-state: trap: SP %3td: ", tsp);
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

	    IDIO ct_mci = idio_array_ref_index (stack, tsp - 2);

	    IDIO ct_sym = idio_vm_constants_ref ((idio_ai_t) IDIO_FIXNUM_VAL (ct_mci));
	    IDIO ct = idio_module_symbol_value_recurse (ct_sym, IDIO_THREAD_ENV (thr), idio_S_nil);

	    if (idio_isa_struct_type (ct)) {
		idio_debug (" %s\n", IDIO_STRUCT_TYPE_NAME (ct));
	    } else {
		idio_debug (" %s\n", ct);
	    }

	    idio_ai_t ntsp = IDIO_FIXNUM_VAL (idio_array_ref_index (stack, tsp - 3));
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
    IDIO fmci = IDIO_THREAD_EXPR (cthr);
    if (idio_isa_fixnum (fmci)) {
	IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	IDIO expr = idio_vm_constants_ref (gci);

	IDIO lo = idio_S_nil;
	if (idio_S_nil != expr) {
	    lo = idio_hash_ref (idio_src_properties, expr);
	    if (idio_S_unspec == lo) {
		lo = idio_S_nil;
	    }
	}

	if (idio_S_nil == lo) {
	    idio_display_C ("<no lexobj for ", lsh);
	    idio_display (expr, lsh);
	    idio_display_C (">", lsh);
	} else {
	    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME), lsh);
	    idio_display_C (":line ", lsh);
	    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE), lsh);
	}
    } else {
	idio_display (fmci, lsh);
    }

    return idio_get_output_string (lsh);
}

IDIO idio_vm_source_expr ()
{
    IDIO cthr = idio_thread_current_thread ();
    IDIO fmci = IDIO_THREAD_EXPR (cthr);
    if (idio_isa_fixnum (fmci)) {
	IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
	idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

	return idio_vm_constants_ref (gci);
    }

    return idio_S_nil;
}

void idio_vm_decode_thread (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp0 = idio_array_size (stack) - 1;
    idio_ai_t sp = sp0;
#ifdef IDIO_VM_DYNAMIC_REGISTERS
    idio_ai_t tsp = IDIO_FIXNUM_VAL (IDIO_THREAD_TRAP_SP (thr));
    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));

    fprintf (stderr, "vm-decode-thread: thr=%8p esp=%4td dsp=%4td tsp=%4td sp=%4td pc=%6td\n", thr, esp, dsp, tsp, sp, IDIO_THREAD_PC (thr));
#else
    fprintf (stderr, "vm-decode-thread: thr=%8p sp=%4td pc=%6td\n", thr, sp, IDIO_THREAD_PC (thr));
#endif

    idio_vm_decode_stack (stack);
}

void idio_vm_decode_stack (IDIO stack)
{
    IDIO_ASSERT (stack);
    IDIO_TYPE_ASSERT (array, stack);

    idio_ai_t sp0 = idio_array_size (stack) - 1;
    idio_ai_t sp = sp0;

    fprintf (stderr, "vm-decode-stack: stk=%p sp=%4td\n", stack, sp);

    for (;sp >= 0; ) {

	fprintf (stderr, "%4td\t", sp);

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
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), sv2);
	    idio_debug ("%-20s ", idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci)));
	    idio_ai_t tsp = IDIO_FIXNUM_VAL (sv3);
	    fprintf (stderr, "next t/h @%td", tsp);
	    sp -= 4;
	} else if (idio_SM_escaper == sv0 &&
		   sp >= 3 &&
		   idio_isa_fixnum (sv1) &&
		   idio_isa_frame (sv2) &&
		   idio_isa_fixnum (sv3)) {
	    fprintf (stderr, "%-20s ", "ESCAPER");
	    IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), sv1);
	    idio_debug ("%-20s ", idio_vm_constants_ref (IDIO_FIXNUM_VAL (fgci)));
	    fprintf (stderr, "PC -> %" PRIdPTR, IDIO_FIXNUM_VAL (sv3));
	    sp -= 4;
	} else if (idio_SM_dynamic == sv0 &&
		   sp >= 3) {
	    fprintf (stderr, "%-20s vi=%5" PRIdPTR " ", "DYNAMIC", IDIO_FIXNUM_VAL (sv1));
	    idio_debug ("%-35s ", sv2);
	    idio_ai_t dsp = IDIO_FIXNUM_VAL (sv3);
	    fprintf (stderr, "next dyn @%td", dsp);
	    sp -= 4;
	} else if (idio_SM_environ == sv0 &&
		   sp >= 3) {
	    fprintf (stderr, "%-20s vi=%5" PRIdPTR, "ENVIRON", IDIO_FIXNUM_VAL (sv1));
	    idio_debug ("%-35s ", sv2);
	    idio_ai_t esp = IDIO_FIXNUM_VAL (sv3);
	    fprintf (stderr, "next env @%td", esp);
	    sp -= 4;
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
#ifdef IDIO_VM_DYNAMIC_REGISTERS
		   sp >= 5 &&
		   idio_isa_module (sv1) &&
		   (idio_S_nil == sv2 ||
		    idio_isa_frame (sv2)) &&
		   idio_isa_fixnum (sv3) &&
		   idio_isa_fixnum (sv4) &&
		   idio_isa_fixnum (sv5)) {
	    fprintf (stderr, "%-20s ", "STATE");
	    idio_debug ("esp %s ", sv5);
	    idio_debug ("dsp %s ", sv4);
	    idio_debug ("tsp %s ", sv3);
	    idio_debug ("%s ", sv2);
	    idio_debug ("%s ", sv1);
	    sp -= 6;
#else
	    sp >= 2 &&
		idio_isa_module (sv1) &&
		(idio_S_nil == sv2 ||
		 idio_isa_frame (sv2))) {
	    fprintf (stderr, "%-20s ", "STATE");
	    idio_debug ("%s ", sv2);
	    idio_debug ("%s ", sv1);
	    sp -= 3;
#endif
	} else if (idio_SM_return == sv0 &&
		   sp >= 1 &&
		   idio_isa_fixnum (sv1)) {
	    fprintf (stderr, "%-20s ", "RETURN");
	    idio_debug ("%s ", sv1);
	    int pc = IDIO_FIXNUM_VAL (sv1);
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
	    sp -= 2;
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
     /*
      * Start up and shutdown generates some 7600 constants (probably
      * 2700 actual constants) and running the test suite generates
      * 23000 constants (probably 5000 actual constants).  Most of
      * these are src code properties -- it is difficult to
      * distinguish a genuine PAIR as a constant from a PAIR used as a
      * source code property.
      */
    idio_vm_constants = idio_array (24000);
    idio_gc_protect_auto (idio_vm_constants);
    /*
     * The only "constant" we can't put in our idio_vm_constants_hash
     * is #n (#n can't be a key in a hash) so plonk it in slot 0 so it
     * is a quick lookup when we fail to find the key in the hash.
     */
    idio_array_push (idio_vm_constants, idio_S_nil);

    idio_vm_constants_hash = IDIO_HASH_EQUALP (2048);
    idio_gc_protect_auto (idio_vm_constants_hash);

    /*
     * Start up and shutdown generates some 1761 values and running
     * the test suite generates 2034 values
     */
    idio_vm_values = idio_array (3000);
    idio_gc_protect_auto (idio_vm_values);

    idio_vm_krun = idio_array (4);
    idio_gc_protect_auto (idio_vm_krun);

    /*
     * Push a dummy value onto idio_vm_values so that slot 0 is
     * unavailable.  We can then use 0 as a marker to say the value
     * needs to be dynamically referenced and the the 0 backfilled
     * with the true value.
     *
     * idio_S_undef is the value whereon the *_REF VM instructions do
     * a double-take
     */
    idio_array_push (idio_vm_values, idio_S_undef);

#define IDIO_VM_STRING(c,s) idio_vm_ ## c ## _string = idio_string_C (s); idio_gc_protect_auto (idio_vm_ ## c ## _string);

    IDIO_VM_STRING (get_or_create_vvi, "get-or-create-vvi");
    IDIO_VM_STRING (GLOBAL_SYM_DEF, "GLOBAL-SYM-DEF");
    IDIO_VM_STRING (GLOBAL_SYM_DEF_gvi0, "GLOBAL-SYM-DEF/gvi=0");
    IDIO_VM_STRING (GLOBAL_SYM_SET, "GLOBAL-SYM-SET");
    IDIO_VM_STRING (COMPUTED_SYM_DEF, "COMPUTED-SYM-DEF");
    IDIO_VM_STRING (EXPANDER, "EXPANDER");
    IDIO_VM_STRING (INFIX_OPERATOR, "INFIX-OPERATOR");
    IDIO_VM_STRING (POSTFIX_OPERATOR, "POSTFIX-OPERATOR");
}

typedef struct idio_vm_symbol_s {
    char *name;
    uint8_t value;
} idio_vm_symbol_t;

static idio_vm_symbol_t idio_vm_symbols[] = {
    { "A-PRIMCALL0",				IDIO_A_PRIMCALL0 },
    { "A-PRIMCALL1",				IDIO_A_PRIMCALL1 },
    { "A-PRIMCALL2",				IDIO_A_PRIMCALL2 },
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
#ifdef IDIO_VM_DIS
    IDIO_ADD_PRIMITIVE (vm_dis);
#endif
    IDIO_ADD_PRIMITIVE (dasm);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_run);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_constants);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_extend_values);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_vm_module, vm_values_ref);

    IDIO_ADD_PRIMITIVE (idio_thread_state);
    IDIO_ADD_PRIMITIVE (exit);
    IDIO_ADD_PRIMITIVE (run_in_thread);
    IDIO_ADD_PRIMITIVE (vm_frame_tree);
    IDIO_ADD_PRIMITIVE (vm_trap_state);
}

void idio_final_vm ()
{
    /*
     * Run a GC in case someone is hogging all the file descriptors,
     * say, as we want to use one, at least.
     */
    idio_gc_collect_all ("final-vm");
    IDIO thr = idio_thread_current_thread ();

    if (getpid () == idio_pid) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "final-vm: ");

	IDIO stack = IDIO_THREAD_STACK (thr);
	idio_ai_t ss = idio_array_size (stack);
	if (ss > 24) {
	    fprintf (stderr, "VM didn't finish cleanly with %td > 24 entries on the stack\n", ss);
	    idio_vm_thread_state (thr);
	}
#endif

	if (idio_vm_reports) {
#ifdef IDIO_DEBUG
	    fprintf (stderr, "vm-dasm ");
#endif
	    idio_dasm_FILE = fopen ("vm-dasm", "w");
	    if (idio_dasm_FILE) {
		idio_vm_dasm (thr, idio_all_code, 0, 0);
		fclose (idio_dasm_FILE);
	    }

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
	     * The flag, idio_vm_reports is, clearly, toggled on the
	     * presence of the --vm-reports argument but we don't want
	     * this alternate behaviour to prevent
	     * ^rt-parameter-value-error being raised during run-time.
	     *
	     * So we need yet another flag for during VM reporting.
	     */
	    idio_vm_reporting = 1;
	    idio_vm_dump_constants ();
	    idio_vm_dump_values ();

#ifdef IDIO_VM_PROF
#ifdef IDIO_DEBUG
	    fprintf (stderr, "vm-perf ");
#endif
	    fprintf (idio_vm_perf_FILE, "final-vm: created %zu instruction bytes\n", IDIO_IA_USIZE (idio_all_code));
	    fprintf (idio_vm_perf_FILE, "final-vm: created %td constants\n", idio_array_size (idio_vm_constants));
	    fprintf (idio_vm_perf_FILE, "final-vm: created %td values\n", idio_array_size (idio_vm_values));
#endif

#ifdef IDIO_VM_PROF
	    uint64_t c = 0;
	    struct timespec t;
	    t.tv_sec = 0;
	    t.tv_nsec = 0;

	    for (IDIO_I i = 1; i < IDIO_I_MAX; i++) {
		c += idio_vm_ins_counters[i];
		t.tv_sec += idio_vm_ins_call_time[i].tv_sec;
		t.tv_nsec += idio_vm_ins_call_time[i].tv_nsec;
		if (t.tv_nsec > IDIO_VM_NS) {
		    t.tv_nsec -= IDIO_VM_NS;
		    t.tv_sec += 1;
		}
	    }

	    float c_pct = 0;
	    float t_pct = 0;

	    fprintf (idio_vm_perf_FILE, "vm-ins:  %4.4s %-40.40s %8.8s %5.5s %15.15s %5.5s %6.6s\n", "code", "instruction", "count", "cnt%", "time (sec.nsec)", "time%", "ns/call");
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

			fprintf (idio_vm_perf_FILE, "vm-ins:  %4" PRIu8 " %-40s %8" PRIu64 " %5.1f %5ld.%09ld %5.1f",
				 i,
				 bc_name,
				 idio_vm_ins_counters[i],
				 count_pct,
				 idio_vm_ins_call_time[i].tv_sec,
				 idio_vm_ins_call_time[i].tv_nsec,
				 time_pct);
			double call_time = 0;
			if (idio_vm_ins_counters[i]) {
			    call_time = (idio_vm_ins_call_time[i].tv_sec * IDIO_VM_NS + idio_vm_ins_call_time[i].tv_nsec) / idio_vm_ins_counters[i];
			}
			fprintf (idio_vm_perf_FILE, " %6.f", call_time);
			fprintf (idio_vm_perf_FILE, "\n");
		    }
		}
	    }
	    fprintf (idio_vm_perf_FILE, "vm-ins:  %4s %-38s %10" PRIu64 " %5.1f %5ld.%09ld %5.1f\n", "", "total", c, c_pct, t.tv_sec, t.tv_nsec, t_pct);
#endif
	}

#ifdef IDIO_DEBUG
	fprintf (stderr, "\n");
#endif
    }

    idio_ia_free (idio_all_code);
    idio_all_code = NULL;
}

void idio_init_vm ()
{
    idio_module_table_register (idio_vm_add_primitives, idio_final_vm, NULL);

    /*
     * Careful analysis:
     *
     * grep CONSTANT vm-dasm | sed 's/.*[0-9]: //' | sort | uniq -c |
     * sort -n
     *
     * suggests that, for common terms, to keep the varuints small
     * (<240 -- see idio_vm_fetch_varuint()) we should pre-fill the
     * constants array with things we know are going to get used.
     *
     * NB The idio_S_X values are initialised after idio_vm_values()
     * is run, above, so we might as well add them all down here.  It
     * means they get shoved out to ~80th in the constants list (as
     * other modules have initialised before us) -- but that's well
     * within the 240 allowed for in the 1 byte varuint limit.
     */

    /* used in bootstrap */
    idio_vm_extend_constants (idio_S_block);
    idio_vm_extend_constants (idio_S_colon_eq);
    idio_vm_extend_constants (idio_S_cond);
    idio_vm_extend_constants (idio_S_define);
    idio_vm_extend_constants (idio_S_else);
    idio_vm_extend_constants (idio_S_eq);
    idio_vm_extend_constants (idio_S_error);
    idio_vm_extend_constants (idio_S_function);
    idio_vm_extend_constants (idio_S_if);
    idio_vm_extend_constants (idio_S_ph);
    idio_vm_extend_constants (idio_S_quote);
    idio_vm_extend_constants (idio_bignum_real_C ("0.0", 3));
    idio_vm_extend_constants (idio_bignum_real_C ("1.0", 3));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("\n")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("closed application: (e)")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("closed application: (end)")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("closed application: (loop)")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("closed application: (r)")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("closed application: (start)")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("closed application: (v)")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("closed application: (x)")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("invalid syntax")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("not a char-set")));
    idio_vm_extend_constants (idio_string_C_len (IDIO_STATIC_STR_LEN ("not a condition:")));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("&args"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN (":"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("close"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("define-syntax"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("display"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("display*"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("ih"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("operator"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("pair?"));
    idio_vm_extend_constants (IDIO_SYMBOLS_C_INTERN ("seq"));

    idio_vm_module = idio_module (IDIO_SYMBOLS_C_INTERN ("vm"));

    idio_vm_t0 = time ((time_t *) NULL);

    idio_all_code = idio_ia (500000);

    idio_codegen_code_prologue (idio_all_code);
    idio_prologue_len = IDIO_IA_USIZE (idio_all_code);

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
    idio_dasm_FILE = stderr;

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

    idio_vm_prompt_tag_type = idio_struct_type (IDIO_SYMBOLS_C_INTERN ("prompt-tag"), idio_S_nil, IDIO_LIST1 (IDIO_SYMBOLS_C_INTERN ("name")));
}

