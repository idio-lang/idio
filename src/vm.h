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
extern IDIO idio_vm_st;
extern IDIO idio_vm_cs;
extern IDIO idio_vm_ch;
extern IDIO idio_vm_ses;
extern IDIO idio_vm_sps;
extern IDIO idio_vm_krun;
extern int idio_vm_reports;
extern int idio_vm_reporting;
extern int idio_vm_tables;
extern IDIO_IA_T idio_all_code;
extern idio_pc_t idio_vm_FINISH_pc;
extern idio_pc_t idio_vm_NCE_pc;
extern idio_pc_t idio_vm_CHR_pc;
extern idio_pc_t idio_vm_IHR_pc;
extern idio_pc_t idio_vm_AR_pc;
extern idio_pc_t idio_vm_RETURN_pc;
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
 * xenv[n] is that collection loaded in (by evaluation or read in from
 * the pre-compiled X.idio files) and will reflect much of the
 * idio_evaluate_eenv_type structure.
 *
 * The fields are:
 *
 * * index - the xenv's index into idio_xenvs[]
 *
 * * eenv - a pointer back to any eenv used in construction
 *
 * * desc - a descriptive string, usually involving a file name, as an
 *      aide-mémoire
 *
 * * st - symbol table
 *
 *      a map from a SYM-IREF index to the constants index
 *
 *	symbols represents each thing that requires an associated
 *	value.  The evaluator chose a SYM-IREF (rather than a
 *	VAL-IREF) because we haven't seen the symbol *defined* in this
 *	module.  So it is either a forward lookup or a reference to a
 *	library function.  Probably.
 *
 *      A symbol can appear more than once (but less than thrice).
 *      This is a peculiarity of mixing predefs and toplevels (C-land
 *      primitives and Idio-land closures).  The calling convention
 *      for a C-function is much lighter-weight than that of an Idio
 *      closure and quite different.  Once a function has referred to
 *      some {si} with the expectation of it being a predef then we
 *      must add a new {si} for the same {name} to replace it with a
 *      toplevel closure in order that we get different behaviour.
 *      Future lookups of {name} will get the new {si}/toplevel (and
 *      closure calling convention) and only the original code will
 *      still refer to and get the predef behaviour.
 *
 * * cs - constants table
 *
 *      a table of all things (symbols, strings, bignums etc.) that we
 *	can't quietly encode in the byte code.  We can, however,
 *	encode the index into the constants table into the byte code.
 *
 * * ch - constants hash
 *
 *      the inverse of constants and used for "fast" lookups
 *
 * * ses - src exprs
 *
 *      table of source code expressions as seen by the evaluator
 *
 *      These are not saved out as any subsequent reading back in
 *      creates innumerable new instances of blocks of code which
 *      were, originally, all internally self-referential.  The
 *      reference to (* 3 4) in (+ 1 2 (* 3 4)) cannot be re-created
 *      from supplying both lists separately -- they will simply be
 *      two lists.  This becomes exponentially expensive --
 *      O(n^depth-of-expression)??
 *
 * * sps - src properties
 *
 *      table of lexical information (file, line number)
 *
 *	Note that file is really file-index into the constants table
 *	to get the (string) file name.
 *
 * * vt - values table
 *
 *      a table of resolved global value indexes
 *
 *	values holds the (eventually resolved) gvi for each entry in
 *	symbols.  The initial values are all 0 (zero) meaning
 *	unresolved.
 *
 *	values, notably closures, can seemingly appear more than once.
 *	Here you'll see the (lambda-lifted) CREATE-FUNCTION closely
 *	followed by the CREATE-CLOSURE that references it.  The (very
 *	subtle) difference being that the parent *frame* of the
 *	CREATE-FUNCTION is #n (commonly 0x2 when printed) whereas the
 *	parent frame of the (usable) closure is some non-#n value.
 *
 *	Of course, if you re-define something, it'll appear more than
 *	once anyway.  This is redundant for regular values but quite
 *	useful for closures which may improve/embellish previous
 *	versions calling the original code under the hood.
 *
 * Suppose, when we compile a file, {printf} is the third symbol seen,
 * ie. si==2 which happens to be the fifth entry in the constants
 * table, ie. ci==4.
 *
 * The byte code will see "SYM-IREF 2".
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
 * (scope si ci gvi module helpful-string).  If it failed we can add a
 * new symbol information tuple which maps the symbol to itself,
 * cf. calling external programs, eg. ls(1).
 *
 * We can now set values[si] to gvi (from the symbol information).
 *
 * Obviously, "VAL-IREF n" can just blast away and use values[n].
 * Experience suggests that we've successfully arranged to avoid
 * seeing a "VAL-IREF n" before we are certain to have set values[n].
 */
typedef struct idio_xenv_s {
    idio_xi_t index;
    IDIO eenv;
    IDIO desc;
    IDIO st;			/* symbol table */
    IDIO cs;			/* constants */
    IDIO ch;			/* constants hash */
    IDIO vt;			/* value table */
    IDIO ses;			/* src exprs */
    IDIO sps;			/* src properties */
    IDIO_IA_T byte_code;
} idio_xenv_t;

#define IDIO_XENV_INDEX(X)     ((X)->index)
#define IDIO_XENV_EENV(X)      ((X)->eenv)
#define IDIO_XENV_DESC(X)      ((X)->desc)
#define IDIO_XENV_ST(X)        ((X)->st)
#define IDIO_XENV_CS(X)        ((X)->cs)
#define IDIO_XENV_CH(X)        ((X)->ch)
#define IDIO_XENV_VT(X)        ((X)->vt)
#define IDIO_XENV_SES(X)       ((X)->ses)
#define IDIO_XENV_SPS(X)       ((X)->sps)
#define IDIO_XENV_BYTE_CODE(X) ((X)->byte_code)

extern idio_xi_t idio_xenvs_size;
extern idio_xenv_t **idio_xenvs;

#define IDIO_THREAD_ST(T)        IDIO_XENV_ST (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_CS(T)        IDIO_XENV_CS (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_CH(T)        IDIO_XENV_CH (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_VT(T)        IDIO_XENV_VT (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_SES(T)       IDIO_XENV_SES (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_SPS(T)       IDIO_XENV_SPS (idio_xenvs[IDIO_THREAD_XI(T)])
#define IDIO_THREAD_BYTE_CODE(T) IDIO_XENV_BYTE_CODE (idio_xenvs[IDIO_THREAD_XI(T)])

#define IDIO_VM_NS	1000000000L
#define IDIO_VM_US	1000000L

void idio_vm_panic (IDIO thr, char const *m);
void idio_vm_error (char const *msg, IDIO args, IDIO c_location);
IDIO idio_vm_closure_name (IDIO c);

typedef enum {
    IDIO_VM_RUN_C,
    IDIO_VM_RUN_IDIO
} idio_vm_run_enum;

extern volatile sig_atomic_t idio_vm_signal_record[IDIO_LIBC_NSIG+1];
void idio_vm_sa_signal (int signum);
void idio_vm_signal_report ();

IDIO idio_vm_run (IDIO thr, idio_xi_t xi, idio_pc_t pc, idio_vm_run_enum caller);
IDIO idio_vm_run_C (IDIO thr, idio_xi_t xi, idio_pc_t pc);

void idio_vm_restore_continuation (IDIO k, IDIO val);
void idio_vm_restore_exit (IDIO k, IDIO val);

void idio_vm_start_tracing ();
void idio_vm_stop_tracing ();

IDIO idio_vm_symbols_ref (idio_xi_t xi, idio_as_t si);
idio_as_t idio_vm_extend_constants_direct (IDIO cs, IDIO ch, IDIO v);
idio_as_t idio_vm_extend_default_constants (IDIO v);
idio_as_t idio_vm_extend_constants (idio_xi_t xi, IDIO v);
IDIO idio_vm_constants_ref (idio_xi_t xi, idio_as_t ci);
idio_ai_t idio_vm_constants_lookup (idio_xi_t xi, IDIO v);
idio_as_t idio_vm_constants_lookup_or_extend (idio_xi_t xi, IDIO v);
IDIO idio_vm_src_expr_ref (idio_xi_t xi, idio_as_t ci);
IDIO idio_vm_src_props_ref (idio_xi_t xi, idio_as_t ci);
idio_as_t idio_vm_extend_values (idio_xi_t xi);
idio_as_t idio_vm_extend_default_values ();
IDIO idio_vm_values_ref (idio_xi_t xi, idio_as_t vi);
IDIO idio_vm_values_gref (idio_xi_t xi, idio_as_t vi, char *const op);
IDIO idio_vm_default_values_ref (idio_as_t gvi);
void idio_vm_values_set (idio_xi_t xi, idio_as_t vi, IDIO v);
void idio_vm_default_values_set (idio_as_t gvi, IDIO v);
void idio_vm_decode_thread (IDIO thr);
void idio_vm_decode_stack (IDIO thr, IDIO stack);
void idio_vm_reset_thread (IDIO thr, int verbose);
IDIO idio_vm_add_dynamic (IDIO xi, IDIO si, IDIO ci, IDIO vi, IDIO m, IDIO note);
IDIO idio_vm_dynamic_ref (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO args);
void idio_vm_dynamic_set (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO v);
IDIO idio_vm_add_environ (IDIO xi, IDIO si, IDIO ci, IDIO vi, IDIO m, IDIO note);
IDIO idio_vm_environ_ref (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO args);
void idio_vm_environ_set (IDIO thr, idio_as_t si, idio_as_t gvi, IDIO v);
IDIO idio_vm_computed_ref (idio_xi_t xi, idio_as_t si, idio_as_t gvi);
IDIO idio_vm_computed_set (idio_xi_t xi, idio_as_t si, idio_as_t gvi, IDIO v);
void idio_vm_computed_define (idio_xi_t xi, idio_as_t si, idio_as_t gvi, IDIO v);
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
IDIO idio_vm_run_xenv (idio_xi_t xi, IDIO pc);
idio_xi_t idio_vm_add_xenv (IDIO desc, IDIO st, IDIO cs, IDIO ch, IDIO vt, IDIO ses, IDIO sps, IDIO bs);
idio_xi_t idio_vm_add_xenv_from_eenv (IDIO thr, IDIO eenv);
void idio_vm_save_xenvs (idio_xi_t from);
IDIO idio_vm_source_location ();
IDIO idio_vm_frame_tree (IDIO args);
void idio_vm_trap_state (IDIO thr);

typedef enum {
    IDIO_VM_IREF_VAL_UNDEF_FATAL,
    IDIO_VM_IREF_VAL_UNDEF_SYM,
} idio_vm_iref_val_enum;

IDIO idio_vm_iref_val (IDIO thr, idio_xi_t xi, idio_as_t si, char *const op, idio_vm_iref_val_enum mode);
void idio_vm_thread_init (IDIO thr);
void idio_vm_default_pc (IDIO thr);
IDIO idio_vm_extend_tables (idio_xi_t xi, IDIO name, IDIO scope, IDIO module, IDIO desc);
void idio_vm_thread_state (IDIO thr);

time_t idio_vm_elapsed (void);

void idio_init_vm_values ();
void idio_final_xenv ();
void idio_init_vm ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
