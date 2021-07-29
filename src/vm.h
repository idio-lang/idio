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
#define IDIO_A_SHALLOW_ARGUMENT_REF0		1
#define IDIO_A_SHALLOW_ARGUMENT_REF1		2
#define IDIO_A_SHALLOW_ARGUMENT_REF2		3
#define IDIO_A_SHALLOW_ARGUMENT_REF3		4
#define IDIO_A_SHALLOW_ARGUMENT_REF		5
#define IDIO_A_DEEP_ARGUMENT_REF		6

#define IDIO_A_SHALLOW_ARGUMENT_SET0		10
#define IDIO_A_SHALLOW_ARGUMENT_SET1		11
#define IDIO_A_SHALLOW_ARGUMENT_SET2		12
#define IDIO_A_SHALLOW_ARGUMENT_SET3		13
#define IDIO_A_SHALLOW_ARGUMENT_SET		14
#define IDIO_A_DEEP_ARGUMENT_SET		15

#define IDIO_A_GLOBAL_SYM_REF			20
#define IDIO_A_CHECKED_GLOBAL_SYM_REF		21
#define IDIO_A_GLOBAL_FUNCTION_SYM_REF		22
#define IDIO_A_CHECKED_GLOBAL_FUNCTION_SYM_REF	23
#define IDIO_A_CONSTANT_SYM_REF			24
#define IDIO_A_COMPUTED_SYM_REF			25

#define IDIO_A_GLOBAL_SYM_DEF			30
#define IDIO_A_GLOBAL_SYM_SET			31
#define IDIO_A_COMPUTED_SYM_SET			32
#define IDIO_A_COMPUTED_SYM_DEF			33

#define IDIO_A_GLOBAL_VAL_REF			40
#define IDIO_A_CHECKED_GLOBAL_VAL_REF		41
#define IDIO_A_GLOBAL_FUNCTION_VAL_REF		42
#define IDIO_A_CHECKED_GLOBAL_FUNCTION_VAL_REF	43
#define IDIO_A_CONSTANT_VAL_REF			44
#define IDIO_A_COMPUTED_VAL_REF			45

#define IDIO_A_GLOBAL_VAL_DEF			50
#define IDIO_A_GLOBAL_VAL_SET			51
#define IDIO_A_COMPUTED_VAL_SET			52
#define IDIO_A_COMPUTED_VAL_DEF			53

#define IDIO_A_PREDEFINED0			60
#define IDIO_A_PREDEFINED1			61
#define IDIO_A_PREDEFINED2			62
#define IDIO_A_PREDEFINED3			63 /* not implemented */
#define IDIO_A_PREDEFINED4			64 /* not implemented */
#define IDIO_A_PREDEFINED5			65 /* not implemented */
#define IDIO_A_PREDEFINED6			66 /* not implemented */
#define IDIO_A_PREDEFINED7			67 /* not implemented */
#define IDIO_A_PREDEFINED8			68 /* not implemented */
#define IDIO_A_PREDEFINED			69

#define IDIO_A_LONG_GOTO			70
#define IDIO_A_LONG_JUMP_FALSE			71
#define IDIO_A_LONG_JUMP_TRUE			72
#define IDIO_A_SHORT_GOTO			73
#define IDIO_A_SHORT_JUMP_FALSE			74
#define IDIO_A_SHORT_JUMP_TRUE			75

#define IDIO_A_PUSH_VALUE			80
#define IDIO_A_POP_VALUE			81
#define IDIO_A_POP_REG1				82
#define IDIO_A_POP_REG2				83
#define IDIO_A_SRC_EXPR				84
#define IDIO_A_POP_FUNCTION			85
#define IDIO_A_PRESERVE_STATE			86
#define IDIO_A_RESTORE_STATE			87
#define IDIO_A_RESTORE_ALL_STATE		88

#define IDIO_A_CREATE_CLOSURE			90
#define IDIO_A_FUNCTION_INVOKE			91
#define IDIO_A_FUNCTION_GOTO			92
#define IDIO_A_RETURN				93
#define IDIO_A_FINISH				94
#define IDIO_A_ABORT				95

#define IDIO_A_ALLOCATE_FRAME1			100
#define IDIO_A_ALLOCATE_FRAME2			101
#define IDIO_A_ALLOCATE_FRAME3			102
#define IDIO_A_ALLOCATE_FRAME4			103
#define IDIO_A_ALLOCATE_FRAME5			104
#define IDIO_A_ALLOCATE_FRAME			105
#define IDIO_A_ALLOCATE_DOTTED_FRAME		106
#define IDIO_A_REUSE_FRAME			107

#define IDIO_A_POP_FRAME0			110
#define IDIO_A_POP_FRAME1			111
#define IDIO_A_POP_FRAME2			112
#define IDIO_A_POP_FRAME3			113
#define IDIO_A_POP_FRAME			114

#define IDIO_A_LINK_FRAME			120
#define IDIO_A_UNLINK_FRAME			121
#define IDIO_A_PACK_FRAME			122
#define IDIO_A_POP_LIST_FRAME			123
#define IDIO_A_EXTEND_FRAME			124

/* NB. No ARITY0P as there is always an implied varargs */
#define IDIO_A_ARITY1P				130
#define IDIO_A_ARITY2P				131
#define IDIO_A_ARITY3P				132
#define IDIO_A_ARITY4P				133
#define IDIO_A_ARITYEQP				134
#define IDIO_A_ARITYGEP				135

#define IDIO_A_SHORT_NUMBER			140 /* not implemented */
#define IDIO_A_SHORT_NEG_NUMBER			141 /* not implemented */
#define IDIO_A_CONSTANT_0			142
#define IDIO_A_CONSTANT_1			143
#define IDIO_A_CONSTANT_2			144
#define IDIO_A_CONSTANT_3			145
#define IDIO_A_CONSTANT_4			146
#define IDIO_A_FIXNUM				147
#define IDIO_A_NEG_FIXNUM			148
#define IDIO_A_CHARACTER			149
#define IDIO_A_NEG_CHARACTER			150
#define IDIO_A_CONSTANT				151
#define IDIO_A_NEG_CONSTANT			152
#define IDIO_A_UNICODE				153

#define IDIO_A_NOP				160
#define IDIO_A_PRIMCALL0			161

#define IDIO_A_PRIMCALL1			165

#define IDIO_A_PRIMCALL2			180

#define IDIO_A_PRIMCALL3			195 /* not implemented */

#define IDIO_A_PRIMCALL				200 /* not implemented */

#define IDIO_A_EXPANDER				210
#define IDIO_A_INFIX_OPERATOR			211
#define IDIO_A_POSTFIX_OPERATOR			212

#define IDIO_A_PUSH_DYNAMIC			220
#define IDIO_A_POP_DYNAMIC			221
#define IDIO_A_DYNAMIC_SYM_REF			222
#define IDIO_A_DYNAMIC_FUNCTION_SYM_REF		223
#define IDIO_A_DYNAMIC_VAL_REF			224
#define IDIO_A_DYNAMIC_FUNCTION_VAL_REF		225

#define IDIO_A_PUSH_ENVIRON			230
#define IDIO_A_POP_ENVIRON			231
#define IDIO_A_ENVIRON_SYM_REF			232
#define IDIO_A_ENVIRON_VAL_REF			233

#define IDIO_A_NON_CONT_ERR			240
#define IDIO_A_PUSH_TRAP			241
#define IDIO_A_POP_TRAP				242
#define IDIO_A_RESTORE_TRAP			243

#define IDIO_A_PUSH_ESCAPER			250
#define IDIO_A_POP_ESCAPER			251
#define IDIO_A_ESCAPER_LABEL_REF		252

extern IDIO idio_vm_module;
extern IDIO idio_vm_constants;
extern IDIO idio_vm_constants_hash;
extern IDIO idio_vm_krun;
extern int idio_vm_reports;
extern int idio_vm_reporting;
extern IDIO_IA_T idio_all_code;
extern idio_ai_t idio_vm_FINISH_pc;
extern idio_ai_t idio_vm_NCE_pc;
extern idio_ai_t idio_vm_CHR_pc;
extern idio_ai_t idio_vm_IHR_pc;
extern idio_ai_t idio_vm_AR_pc;
extern size_t idio_prologue_len;
extern int idio_vm_exit;
extern int idio_vm_virtualisation_WSL;

#define IDIO_VM_NS	1000000000L
#define IDIO_VM_US	1000000L

void idio_vm_panic (IDIO thr, char *m);
void idio_vm_error (char *msg, IDIO args, IDIO c_location);
IDIO idio_vm_closure_name (IDIO c);

#define IDIO_VM_RUN_C		0
#define IDIO_VM_RUN_IDIO	1

extern volatile sig_atomic_t idio_vm_signal_record[IDIO_LIBC_NSIG+1];
void idio_vm_sa_signal (int signum);
void idio_vm_signal_report ();

IDIO idio_vm_run (IDIO thr, idio_ai_t pc, int caller);
IDIO idio_vm_run_C (IDIO thr, idio_ai_t pc);
void idio_vm_dasm (IDIO thr, IDIO_IA_T bc, idio_ai_t pc0, idio_ai_t pce);

void idio_vm_restore_continuation (IDIO k, IDIO val);
void idio_vm_restore_exit (IDIO k, IDIO val);
idio_ai_t idio_vm_extend_constants (IDIO v);
IDIO idio_vm_constants_ref (idio_ai_t gci);
idio_ai_t idio_vm_constants_lookup (IDIO v);
idio_ai_t idio_vm_constants_lookup_or_extend (IDIO v);
idio_ai_t idio_vm_extend_values ();
IDIO idio_vm_values_ref (idio_ai_t gvi);
void idio_vm_values_set (idio_ai_t gvi, IDIO v);
idio_ai_t idio_vm_extend_primitives (IDIO v);
void idio_vm_decode_thread (IDIO thr);
void idio_vm_decode_stack (IDIO stack);
void idio_vm_reset_thread (IDIO thr, int verbose);
IDIO idio_vm_add_dynamic (IDIO m, IDIO ci, IDIO vi, IDIO note);
IDIO idio_vm_dynamic_ref (IDIO thr, idio_ai_t msi, idio_ai_t gvi, IDIO args);
void idio_vm_dynamic_set (IDIO thr, idio_ai_t msi, idio_ai_t gvi, IDIO v);
IDIO idio_vm_add_environ (IDIO m, IDIO ci, IDIO vi, IDIO note);
IDIO idio_vm_environ_ref (IDIO thr, idio_ai_t msi, idio_ai_t gvi, IDIO args);
void idio_vm_environ_set (IDIO thr, idio_ai_t msi, idio_ai_t gvi, IDIO v);
IDIO idio_vm_computed_ref (IDIO thr, idio_ai_t msi, idio_ai_t gvi);
IDIO idio_vm_computed_set (IDIO thr, idio_ai_t msi, idio_ai_t gvi, IDIO v);
void idio_vm_computed_define (IDIO thr, idio_ai_t msi, idio_ai_t gvi, IDIO v);
void idio_vm_add_module_constants (IDIO module, IDIO constants);

void idio_raise_condition (IDIO continuablep, IDIO e);
void idio_reraise_condition (IDIO continuablep, IDIO condition);
IDIO idio_apply (IDIO fn, IDIO args);
void idio_vm_debug (IDIO thr, char *prefix, idio_ai_t stack_start);
#ifdef IDIO_VM_PROF
void idio_vm_func_start (IDIO clos, struct timespec *tsp, struct rusage *rup);
void idio_vm_func_stop (IDIO clos, struct timespec *tsp, struct rusage *rup);
void idio_vm_prim_time (IDIO clos, struct timespec *ts0p, struct timespec *tsep, struct rusage *ru0p, struct rusage *ruep);
#endif
IDIO idio_vm_invoke_C (IDIO thr, IDIO command);
IDIO idio_vm_source_location ();
IDIO idio_vm_source_expr ();
IDIO idio_vm_frame_tree (IDIO args);
void idio_vm_trap_state (IDIO thr);

void idio_vm_thread_init (IDIO thr);
void idio_vm_default_pc (IDIO thr);
void idio_vm_thread_state (IDIO thr);

time_t idio_vm_elapsed (void);

void idio_init_vm_values ();
void idio_init_vm ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
