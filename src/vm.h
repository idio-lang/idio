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
 * vm.h
 *
 */

#ifndef VM_H
#define VM_H

#include <signal.h>

/*
 * How many signals are there?
 *
 * Linux, OpenSolaris and Mac OS X all seem to define NSIG as the
 * highest signal number.  On FreeBSD, NSIG is the "number of old
 * signals".  SIGRT* are in a range of their own.
 */

#define IDIO_LIBC_FSIG 1

#if defined (BSD)
#if defined (SIGRTMAX)
#define IDIO_LIBC_NSIG (SIGRTMAX + 1)
#else
#define IDIO_LIBC_NSIG NSIG
#endif
#else
#define IDIO_LIBC_NSIG NSIG
#endif

extern IDIO idio_vm_module;
extern IDIO idio_vm_constants;
extern IDIO idio_vm_constants_hash;
extern IDIO idio_vm_src_exprs;
extern IDIO idio_vm_krun;
extern int idio_vm_reports;
extern int idio_vm_reporting;
extern IDIO_IA_T idio_all_code;
extern idio_pc_t idio_vm_FINISH_pc;
extern idio_pc_t idio_vm_NCE_pc;
extern idio_pc_t idio_vm_CHR_pc;
extern idio_pc_t idio_vm_IHR_pc;
extern idio_pc_t idio_vm_AR_pc;
extern idio_pc_t idio_prologue_len;
extern int idio_vm_exit;
extern int idio_vm_virtualisation_WSL;

/*
 * An execution environment, xenv, is a collection of the various VM
 * tables.
 *
 * It allows us to associate collections of VM tables with the byte
 * code that refers to them.  A sort of Global Offset Table (GOT) as
 * used by x86 processors.
 *
 * xenv[0] is the standard VM collection using the static C variables.
 * We need this because the various C idio_init_X() functions want to
 * stuff the VM full of symbols, constants and values, long before we
 * get round to running any byte code.
 *
 * xenv[n] is that collection read in from the pre-compiled X.idio
 * files and will reflect much of the idio_evaluate_eenv_type
 * structure.
 *
 * The fields are:
 *
 * * index - the xenv's index into idio_xenvs[]
 *
 * * desc - a descriptive string, usually a file name
 *
 * * symbols - a map from a SYM_IREF index to the constants index
 *
 *	symbols represents each thing that requires an associated
 *	value.  The evaluator chose a SYM_IREF (rather than a
 *	VAL_IREF) because we haven't seen the symbol *defined* in this
 *	module.  So it is either a forward lookup or a reference to a
 *	library function.  Probably.
 *
 * * constants - a table of all things (symbols, strings, bignums
 *	etc.) that we can't quietly encode in the byte code.  We can
 *	then encode the index into the constants table into the byte
 *	code.
 *
 * * constants_hash - the inverse of constants
 *
 * * src_exprs - table of source code expressions
 *
 * * src_props - table of lexical information (file, line number)
 *
 *	Note that file is really file-index into the constants table
 *	to get the (string) file name.
 *
 * * values - a table of resolved global value indexes
 *
 *	values holds the (eventually resolved) gvi for each entry in
 *	symbols.  The initial values are all 0 (zero) meaning
 *	unresolved.
 *
 * Suppose, when we compile a file, {printf} is the third symbol seen,
 * ie. si==2 which happens to be the fifth entry in the constants
 * table, ie. ci==4.
 *
 * The byte code will see "SYM_IREF 2".
 *
 * Short-circuit: if values[si] is non-zero we've already looked this
 * symbol up and can use values[si].
 *
 * Without a short-circuit, we can dereference the symbols array to
 * get ci==4.  We can dereference the constants table to get
 * sym==printf.
 *
 * Now we need to lookup {printf} in our top level or the (exported)
 * names of our imports.  That will return us some symbol information,
 * (scope mci gvi module helpful-string).  If it failed we can add a
 * new symbol information tuple which maps the symbol to itself,
 * cf. calling external programs, eg. ls(1).
 *
 * We can now set values[si] to gvi (from the symbol information).
 *
 * Obviously, "VAL_IREF n" can just blast away and use values[n].
 * Experience suggests that we've successfully arranged to avoid
 * seeing a "VAL_IREF n" before we are certain to have set values[n].
 */
typedef struct idio_xenv_s {
    size_t index;
    IDIO desc;
    IDIO symbols;
    IDIO constants;
    IDIO constants_hash;
    IDIO src_exprs;
    IDIO src_props;
    IDIO values;
    IDIO_IA_T byte_code;
} idio_xenv_t;

#define IDIO_XENV_INDEX(X)          ((X)->index)
#define IDIO_XENV_DESC(X)           ((X)->desc)
#define IDIO_XENV_SYMBOLS(X)        ((X)->symbols)
#define IDIO_XENV_CONSTANTS(X)      ((X)->constants)
#define IDIO_XENV_CONSTANTS_HASH(X) ((X)->constants_hash)
#define IDIO_XENV_SRC_EXPRS(X)      ((X)->src_exprs)
#define IDIO_XENV_SRC_PROPS(X)      ((X)->src_props)
#define IDIO_XENV_VALUES(X)         ((X)->values)
#define IDIO_XENV_BYTE_CODE(X)      ((X)->byte_code)

extern size_t idio_xenvs_size;
extern idio_xenv_t **idio_xenvs;

#define IDIO_THREAD_SYMBOLS(T)        IDIO_XENV_SYMBOLS (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_CONSTANTS(T)      IDIO_XENV_CONSTANTS (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_CONSTANTS_HASH(T) IDIO_XENV_CONSTANTS_HASH (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_SRC_EXPRS(T)      IDIO_XENV_SRC_EXPRS (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_SRC_PROPS(T)      IDIO_XENV_SRC_PROPS (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_VALUES(T)         IDIO_XENV_VALUES (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_BYTE_CODE(T)      IDIO_XENV_BYTE_CODE (idio_xenvs[IDIO_THREAD_XI(T)])

#define IDIO_VM_NS	1000000000L
#define IDIO_VM_US	1000000L

void idio_vm_panic (IDIO thr, char const *m);
void idio_vm_error (char const *msg, IDIO args, IDIO c_location);
IDIO idio_vm_closure_name (IDIO c);

#define IDIO_VM_RUN_C		0
#define IDIO_VM_RUN_IDIO	1

extern volatile sig_atomic_t idio_vm_signal_record[IDIO_LIBC_NSIG+1];
void idio_vm_sa_signal (int signum);
void idio_vm_signal_report ();

IDIO idio_vm_run (IDIO thr, idio_pc_t pc, int caller);
IDIO idio_vm_run_C (IDIO thr, idio_pc_t pc);

void idio_vm_restore_continuation (IDIO k, IDIO val);
void idio_vm_restore_exit (IDIO k, IDIO val);
idio_as_t idio_vm_extend_constants_direct (IDIO cs, IDIO ch, IDIO v);
idio_as_t idio_vm_extend_default_constants (IDIO v);
idio_as_t idio_vm_extend_constants (IDIO thr, IDIO v);
IDIO idio_vm_constants_ref (IDIO thr, idio_as_t gci);
idio_ai_t idio_vm_constants_lookup (IDIO thr, IDIO v);
idio_as_t idio_vm_constants_lookup_or_extend (IDIO thr, IDIO v);
IDIO idio_vm_src_expr_ref (size_t xi, idio_as_t gci);
IDIO idio_vm_src_props_ref (size_t xi, idio_as_t gci);
idio_as_t idio_vm_extend_values (IDIO thr);
idio_as_t idio_vm_extend_default_values ();
IDIO idio_vm_values_ref (size_t xi, idio_as_t gvi);
IDIO idio_vm_default_values_ref (idio_as_t gvi);
void idio_vm_values_set (size_t xi, idio_as_t gvi, IDIO v);
void idio_vm_default_values_set (idio_as_t gvi, IDIO v);
void idio_vm_decode_thread (IDIO thr);
void idio_vm_decode_stack (IDIO thr, IDIO stack);
void idio_vm_reset_thread (IDIO thr, int verbose);
IDIO idio_vm_add_dynamic (IDIO m, IDIO ci, IDIO vi, IDIO note);
IDIO idio_vm_dynamic_ref (IDIO thr, idio_as_t msi, idio_as_t gvi, IDIO args);
void idio_vm_dynamic_set (IDIO thr, idio_as_t msi, idio_as_t gvi, IDIO v);
IDIO idio_vm_add_environ (IDIO m, IDIO ci, IDIO vi, IDIO note);
IDIO idio_vm_environ_ref (IDIO thr, idio_as_t msi, idio_as_t gvi, IDIO args);
void idio_vm_environ_set (IDIO thr, idio_as_t msi, idio_as_t gvi, IDIO v);
IDIO idio_vm_computed_ref (IDIO thr, idio_as_t msi, idio_as_t gvi);
IDIO idio_vm_computed_set (IDIO thr, idio_as_t msi, idio_as_t gvi, IDIO v);
void idio_vm_computed_define (IDIO thr, idio_as_t msi, idio_as_t gvi, IDIO v);
void idio_vm_push_abort (IDIO thr, IDIO krun);
void idio_vm_pop_abort (IDIO thr);
idio_sp_t idio_vm_find_abort_1 (IDIO thr);
idio_sp_t idio_vm_find_abort_2 (IDIO thr);

void idio_raise_condition (IDIO continuablep, IDIO e);
void idio_reraise_condition (IDIO continuablep, IDIO condition);
IDIO idio_apply (IDIO fn, IDIO args);

uint64_t idio_vm_get_varuint (IDIO_IA_T bc, idio_pc_t *pcp);
uint64_t idio_vm_get_16uint (IDIO_IA_T bc, idio_pc_t *pcp);
uint64_t idio_vm_fetch_16uint (IDIO thr, IDIO_IA_T bc);

void idio_vm_debug (IDIO thr, char const *prefix, idio_ai_t stack_start);
#ifdef IDIO_VM_PROF
void idio_vm_func_start (IDIO clos, struct timespec *tsp, struct rusage *rup);
void idio_vm_func_stop (IDIO clos, struct timespec *tsp, struct rusage *rup);
void idio_vm_prim_time (IDIO clos, struct timespec *ts0p, struct timespec *tsep, struct rusage *ru0p, struct rusage *ruep);
#endif
IDIO idio_vm_invoke_C_thread (IDIO thr, IDIO command);
IDIO idio_vm_invoke_C (IDIO command);
IDIO idio_vm_source_location ();
IDIO idio_vm_frame_tree (IDIO args);
void idio_vm_trap_state (IDIO thr);

void idio_vm_thread_init (IDIO thr);
void idio_vm_default_pc (IDIO thr);
void idio_vm_thread_state (IDIO thr);

time_t idio_vm_elapsed (void);

void idio_vm_stop_tracing ();

void idio_init_vm_values ();
void idio_final_xenv ();
void idio_init_vm ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
