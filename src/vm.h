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

/*
 * Idio VM Assembly code: IDIO_A_*
 *
 * These are broadly following the intermediate instructions but
 * specialize for the common cases.  For loose values of common.
 *
 * As a byte-compiler there should be less than 256 of these!
 */
typedef enum {
    IDIO_A_SHALLOW_ARGUMENT_REF0,
    IDIO_A_SHALLOW_ARGUMENT_REF1,
    IDIO_A_SHALLOW_ARGUMENT_REF2,
    IDIO_A_SHALLOW_ARGUMENT_REF3,
    IDIO_A_SHALLOW_ARGUMENT_REF,
    IDIO_A_DEEP_ARGUMENT_REF,

    IDIO_A_SHALLOW_ARGUMENT_SET0,
    IDIO_A_SHALLOW_ARGUMENT_SET1,
    IDIO_A_SHALLOW_ARGUMENT_SET2,
    IDIO_A_SHALLOW_ARGUMENT_SET3,
    IDIO_A_SHALLOW_ARGUMENT_SET,
    IDIO_A_DEEP_ARGUMENT_SET,

    IDIO_A_GLOBAL_SYM_REF,
    IDIO_A_GLOBAL_SYM_IREF,
    IDIO_A_CHECKED_GLOBAL_SYM_REF,
    IDIO_A_GLOBAL_FUNCTION_SYM_REF,
    IDIO_A_GLOBAL_FUNCTION_SYM_IREF,
    IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF,
    IDIO_A_CONSTANT_SYM_REF,
    IDIO_A_CONSTANT_SYM_IREF,
    IDIO_A_COMPUTED_SYM_REF,
    IDIO_A_COMPUTED_SYM_IREF,

    IDIO_A_GLOBAL_SYM_DEF,
    IDIO_A_GLOBAL_SYM_IDEF,
    IDIO_A_GLOBAL_SYM_SET,
    IDIO_A_GLOBAL_SYM_ISET,
    IDIO_A_COMPUTED_SYM_SET,
    IDIO_A_COMPUTED_SYM_ISET,
    IDIO_A_COMPUTED_SYM_DEF,
    IDIO_A_COMPUTED_SYM_IDEF,

    IDIO_A_GLOBAL_VAL_REF,
    IDIO_A_GLOBAL_VAL_IREF,
    IDIO_A_CHECKED_GLOBAL_VAL_REF,
    IDIO_A_GLOBAL_FUNCTION_VAL_REF,
    IDIO_A_GLOBAL_FUNCTION_VAL_IREF,
    IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF,
    IDIO_A_CONSTANT_VAL_REF,
    IDIO_A_CONSTANT_VAL_IREF,
    IDIO_A_COMPUTED_VAL_REF,
    IDIO_A_COMPUTED_VAL_IREF,

    IDIO_A_GLOBAL_VAL_DEF,
    IDIO_A_GLOBAL_VAL_IDEF,
    IDIO_A_GLOBAL_VAL_SET,
    IDIO_A_GLOBAL_VAL_ISET,
    IDIO_A_COMPUTED_VAL_SET,
    IDIO_A_COMPUTED_VAL_ISET,
    IDIO_A_COMPUTED_VAL_DEF,
    IDIO_A_COMPUTED_VAL_IDEF,

    IDIO_A_PREDEFINED0,
    IDIO_A_PREDEFINED1,
    IDIO_A_PREDEFINED2,
    IDIO_A_PREDEFINED3,		/* not implemented */
    IDIO_A_PREDEFINED4,		/* not implemented */
    IDIO_A_PREDEFINED5,		/* not implemented */
    IDIO_A_PREDEFINED6,		/* not implemented */
    IDIO_A_PREDEFINED7,		/* not implemented */
    IDIO_A_PREDEFINED8,		/* not implemented */
    IDIO_A_PREDEFINED,

    IDIO_A_LONG_GOTO,
    IDIO_A_LONG_JUMP_FALSE,
    IDIO_A_LONG_JUMP_TRUE,
    IDIO_A_SHORT_GOTO,
    IDIO_A_SHORT_JUMP_FALSE,
    IDIO_A_SHORT_JUMP_TRUE,

    IDIO_A_PUSH_VALUE,
    IDIO_A_POP_VALUE,
    IDIO_A_POP_REG1,
    IDIO_A_POP_REG2,
    IDIO_A_SRC_EXPR,
    IDIO_A_POP_FUNCTION,
    IDIO_A_PRESERVE_STATE,
    IDIO_A_RESTORE_STATE,
    IDIO_A_RESTORE_ALL_STATE,

    IDIO_A_CREATE_FUNCTION,	/* top level closure */
    IDIO_A_CREATE_CLOSURE,
    IDIO_A_FUNCTION_INVOKE,
    IDIO_A_FUNCTION_GOTO,
    IDIO_A_RETURN,
    IDIO_A_FINISH,
    IDIO_A_PUSH_ABORT,
    IDIO_A_POP_ABORT,

    IDIO_A_ALLOCATE_FRAME1,
    IDIO_A_ALLOCATE_FRAME2,
    IDIO_A_ALLOCATE_FRAME3,
    IDIO_A_ALLOCATE_FRAME4,
    IDIO_A_ALLOCATE_FRAME5,
    IDIO_A_ALLOCATE_FRAME,
    IDIO_A_ALLOCATE_DOTTED_FRAME,
    IDIO_A_REUSE_FRAME,

    IDIO_A_POP_FRAME0,
    IDIO_A_POP_FRAME1,
    IDIO_A_POP_FRAME2,
    IDIO_A_POP_FRAME3,
    IDIO_A_POP_FRAME,

    IDIO_A_LINK_FRAME,
    IDIO_A_UNLINK_FRAME,
    IDIO_A_PACK_FRAME,
    IDIO_A_POP_LIST_FRAME,
    IDIO_A_EXTEND_FRAME,

    /* NB. No ARITY0P as there is always an implied varargs */
    IDIO_A_ARITY1P,
    IDIO_A_ARITY2P,
    IDIO_A_ARITY3P,
    IDIO_A_ARITY4P,
    IDIO_A_ARITYEQP,
    IDIO_A_ARITYGEP,

    IDIO_A_SHORT_NUMBER,	/* not implemented */
    IDIO_A_SHORT_NEG_NUMBER,	/* not implemented */
    IDIO_A_CONSTANT_0,
    IDIO_A_CONSTANT_1,
    IDIO_A_CONSTANT_2,
    IDIO_A_CONSTANT_3,
    IDIO_A_CONSTANT_4,
    IDIO_A_FIXNUM,
    IDIO_A_NEG_FIXNUM,
    IDIO_A_CONSTANT,
    IDIO_A_NEG_CONSTANT,
    IDIO_A_UNICODE,

    IDIO_A_NOP,
    IDIO_A_PRIMCALL0,
    IDIO_A_PRIMCALL1,
    IDIO_A_PRIMCALL2,
    IDIO_A_PRIMCALL3,		/* not implemented */
    IDIO_A_PRIMCALL,		/* not implemented */

    IDIO_A_SUPPRESS_RCSE,
    IDIO_A_POP_RCSE,

    IDIO_A_NOT,

    IDIO_A_EXPANDER,
    IDIO_A_INFIX_OPERATOR,
    IDIO_A_POSTFIX_OPERATOR,

    IDIO_A_PUSH_DYNAMIC,
    IDIO_A_POP_DYNAMIC,
    IDIO_A_DYNAMIC_SYM_REF,
    IDIO_A_DYNAMIC_SYM_IREF,
    IDIO_A_DYNAMIC_FUNCTION_SYM_REF,
    IDIO_A_DYNAMIC_FUNCTION_SYM_IREF,
    IDIO_A_DYNAMIC_VAL_REF,
    IDIO_A_DYNAMIC_VAL_IREF,
    IDIO_A_DYNAMIC_FUNCTION_VAL_REF,
    IDIO_A_DYNAMIC_FUNCTION_VAL_IREF,

    IDIO_A_PUSH_ENVIRON,
    IDIO_A_POP_ENVIRON,
    IDIO_A_ENVIRON_SYM_REF,
    IDIO_A_ENVIRON_SYM_IREF,
    IDIO_A_ENVIRON_VAL_REF,
    IDIO_A_ENVIRON_VAL_IREF,

    IDIO_A_NON_CONT_ERR,
    IDIO_A_PUSH_TRAP,
    IDIO_A_POP_TRAP,
    IDIO_A_RESTORE_TRAP,

    IDIO_A_PUSH_ESCAPER,
    IDIO_A_POP_ESCAPER,
    IDIO_A_ESCAPER_LABEL_REF,
    IDIO_A_ESCAPER_LABEL_IREF,
} idio_vm_a_enum;

extern IDIO idio_vm_module;
extern IDIO idio_vm_constants;
extern IDIO idio_vm_constants_hash;
extern IDIO idio_vm_src_constants;
extern IDIO idio_vm_krun;
extern FILE *idio_dasm_FILE;
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
void idio_vm_dasm (IDIO thr, IDIO_IA_T bc, idio_pc_t pc0, idio_pc_t pce, IDIO eenv);

void idio_vm_restore_continuation (IDIO k, IDIO val);
void idio_vm_restore_exit (IDIO k, IDIO val);
idio_as_t idio_vm_extend_constants (IDIO v);
IDIO idio_vm_constants_ref (idio_as_t gci);
idio_ai_t idio_vm_constants_lookup (IDIO v);
idio_as_t idio_vm_constants_lookup_or_extend (IDIO v);
IDIO idio_vm_src_constants_ref (IDIO eenv, idio_as_t gci);
IDIO idio_vm_src_properties_ref (IDIO eenv, IDIO src);
idio_as_t idio_vm_extend_values ();
IDIO idio_vm_values_ref (idio_as_t gvi);
void idio_vm_values_set (idio_as_t gvi, IDIO v);
void idio_vm_decode_thread (IDIO thr);
void idio_vm_decode_stack (IDIO stack);
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
void idio_vm_add_module_constants (IDIO module, IDIO constants);

void idio_raise_condition (IDIO continuablep, IDIO e);
void idio_reraise_condition (IDIO continuablep, IDIO condition);
IDIO idio_apply (IDIO fn, IDIO args);
void idio_vm_debug (IDIO thr, char const *prefix, idio_ai_t stack_start);
#ifdef IDIO_VM_PROF
void idio_vm_func_start (IDIO clos, struct timespec *tsp, struct rusage *rup);
void idio_vm_func_stop (IDIO clos, struct timespec *tsp, struct rusage *rup);
void idio_vm_prim_time (IDIO clos, struct timespec *ts0p, struct timespec *tsep, struct rusage *ru0p, struct rusage *ruep);
#endif
IDIO idio_vm_invoke_C (IDIO thr, IDIO command);
IDIO idio_vm_source_location ();
IDIO idio_vm_frame_tree (IDIO args);
void idio_vm_trap_state (IDIO thr);

void idio_vm_thread_init (IDIO thr);
void idio_vm_default_pc (IDIO thr);
void idio_vm_thread_state (IDIO thr);

time_t idio_vm_elapsed (void);

void idio_vm_stop_tracing ();

void idio_init_vm_values ();
void idio_init_vm ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
