/*
 * Copyright (c) 2015, 2017, 2018, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

/*
 * Idio VM Assembly code: IDIO_A_*
 *
 * These are broadly following the intermediate instructions but
 * specialize for the common cases.  For loose values of common.
 *
 * As a byte-compiler there should be less than 256 of these!
 */
#define IDIO_A_SHALLOW_ARGUMENT_REF0       1
#define IDIO_A_SHALLOW_ARGUMENT_REF1       2
#define IDIO_A_SHALLOW_ARGUMENT_REF2       3
#define IDIO_A_SHALLOW_ARGUMENT_REF3       4
#define IDIO_A_SHALLOW_ARGUMENT_REF        5
#define IDIO_A_DEEP_ARGUMENT_REF           6
#define IDIO_A_GLOBAL_REF                  7
#define IDIO_A_CHECKED_GLOBAL_REF          8
#define IDIO_A_GLOBAL_FUNCTION_REF	   9
#define IDIO_A_CHECKED_GLOBAL_FUNCTION_REF 10
#define IDIO_A_CONSTANT_REF                11
#define IDIO_A_COMPUTED_REF                12

#define IDIO_A_PREDEFINED0                 20
#define IDIO_A_PREDEFINED1                 21
#define IDIO_A_PREDEFINED2                 22
#define IDIO_A_PREDEFINED3                 23
#define IDIO_A_PREDEFINED4                 24
#define IDIO_A_PREDEFINED5                 25
#define IDIO_A_PREDEFINED6                 26
#define IDIO_A_PREDEFINED7                 27
#define IDIO_A_PREDEFINED8                 28
#define IDIO_A_PREDEFINED                  29

#define IDIO_A_SHALLOW_ARGUMENT_SET0       60
#define IDIO_A_SHALLOW_ARGUMENT_SET1       61
#define IDIO_A_SHALLOW_ARGUMENT_SET2       62
#define IDIO_A_SHALLOW_ARGUMENT_SET3       63
#define IDIO_A_SHALLOW_ARGUMENT_SET        64
#define IDIO_A_DEEP_ARGUMENT_SET           65
#define IDIO_A_GLOBAL_DEF                  66
#define IDIO_A_GLOBAL_SET                  67
#define IDIO_A_COMPUTED_SET                68
#define IDIO_A_COMPUTED_DEFINE             69

#define IDIO_A_LONG_GOTO                   80
#define IDIO_A_LONG_JUMP_FALSE             81
#define IDIO_A_SHORT_GOTO                  82
#define IDIO_A_SHORT_JUMP_FALSE            83

#define IDIO_A_PUSH_VALUE                  90
#define IDIO_A_POP_VALUE                   91
#define IDIO_A_POP_REG1                    92
#define IDIO_A_POP_REG2                    93
#define IDIO_A_POP_EXPR                    94
#define IDIO_A_POP_FUNCTION                95
#define IDIO_A_PRESERVE_STATE              96
#define IDIO_A_RESTORE_STATE               97
#define IDIO_A_RESTORE_ALL_STATE           98

#define IDIO_A_CREATE_CLOSURE              100
#define IDIO_A_FUNCTION_INVOKE             101
#define IDIO_A_FUNCTION_GOTO               102
#define IDIO_A_RETURN                      103
#define IDIO_A_FINISH                      104

#define IDIO_A_ALLOCATE_FRAME1             110
#define IDIO_A_ALLOCATE_FRAME2             111
#define IDIO_A_ALLOCATE_FRAME3             112
#define IDIO_A_ALLOCATE_FRAME4             113
#define IDIO_A_ALLOCATE_FRAME5             114
#define IDIO_A_ALLOCATE_FRAME              115
#define IDIO_A_ALLOCATE_DOTTED_FRAME       116

#define IDIO_A_POP_FRAME0                  120
#define IDIO_A_POP_FRAME1                  121
#define IDIO_A_POP_FRAME2                  122
#define IDIO_A_POP_FRAME3                  123
#define IDIO_A_POP_FRAME                   124

#define IDIO_A_EXTEND_FRAME                130
#define IDIO_A_UNLINK_FRAME                131
#define IDIO_A_PACK_FRAME                  132
#define IDIO_A_POP_CONS_FRAME              133

/* NB. No ARITY0P as there is always an implied varargs */
#define IDIO_A_ARITY1P                     140
#define IDIO_A_ARITY2P                     141
#define IDIO_A_ARITY3P                     142
#define IDIO_A_ARITY4P                     143
#define IDIO_A_ARITYEQP                    144
#define IDIO_A_ARITYGEP                    145

#define IDIO_A_SHORT_NUMBER                150
#define IDIO_A_SHORT_NEG_NUMBER            151
#define IDIO_A_CONSTANT_0                  152
#define IDIO_A_CONSTANT_1                  153
#define IDIO_A_CONSTANT_2                  154
#define IDIO_A_CONSTANT_3                  155
#define IDIO_A_CONSTANT_4                  156

#define IDIO_A_PRIMCALL0_NEWLINE           160
#define IDIO_A_PRIMCALL0_READ              161
#define IDIO_A_PRIMCALL1_HEAD              162
#define IDIO_A_PRIMCALL1_TAIL              163
#define IDIO_A_PRIMCALL1_PAIRP             164
#define IDIO_A_PRIMCALL1_SYMBOLP           165
#define IDIO_A_PRIMCALL1_DISPLAY           166
#define IDIO_A_PRIMCALL1_PRIMITIVEP        167
#define IDIO_A_PRIMCALL1_NULLP             168
#define IDIO_A_PRIMCALL1_CONTINUATIONP     169
#define IDIO_A_PRIMCALL1_EOFP              170
#define IDIO_A_PRIMCALL1_SET_CUR_MOD	   171
#define IDIO_A_PRIMCALL2_PAIR              172
#define IDIO_A_PRIMCALL2_EQP               173
#define IDIO_A_PRIMCALL2_SET_HEAD          174
#define IDIO_A_PRIMCALL2_SET_TAIL          175
#define IDIO_A_PRIMCALL2_ADD               176
#define IDIO_A_PRIMCALL2_SUBTRACT          177
#define IDIO_A_PRIMCALL2_EQ                178
#define IDIO_A_PRIMCALL2_LT                179
#define IDIO_A_PRIMCALL2_GT                180
#define IDIO_A_PRIMCALL2_MULTIPLY          181
#define IDIO_A_PRIMCALL2_LE                182
#define IDIO_A_PRIMCALL2_GE                183
#define IDIO_A_PRIMCALL2_REMAINDER         184

#define IDIO_A_NOP                         190
#define IDIO_A_PRIMCALL0                   191
#define IDIO_A_PRIMCALL1                   192
#define IDIO_A_PRIMCALL2                   193
#define IDIO_A_PRIMCALL3                   194
#define IDIO_A_PRIMCALL                    195

#define IDIO_A_LONG_JUMP_TRUE              200
#define IDIO_A_SHORT_JUMP_TRUE             201
#define IDIO_A_FIXNUM                      202
#define IDIO_A_NEG_FIXNUM                  203
#define IDIO_A_CHARACTER                   204
#define IDIO_A_NEG_CHARACTER               205
#define IDIO_A_CONSTANT                    206
#define IDIO_A_NEG_CONSTANT                207

#define IDIO_A_EXPANDER                    210
#define IDIO_A_INFIX_OPERATOR              211
#define IDIO_A_POSTFIX_OPERATOR            212

#define IDIO_A_DYNAMIC_REF                 230
#define IDIO_A_DYNAMIC_FUNCTION_REF	   231
#define IDIO_A_POP_DYNAMIC                 232
#define IDIO_A_PUSH_DYNAMIC                233

#define IDIO_A_ENVIRON_REF                 235
#define IDIO_A_POP_ENVIRON                 236
#define IDIO_A_PUSH_ENVIRON                237

#define IDIO_A_NON_CONT_ERR                240
#define IDIO_A_PUSH_TRAP                   241
#define IDIO_A_POP_TRAP                    242
#define IDIO_A_RESTORE_TRAP                243

#define IDIO_A_POP_ESCAPER                 244
#define IDIO_A_PUSH_ESCAPER                245

extern IDIO idio_vm_constants;
extern IDIO_IA_T idio_all_code;
extern idio_ai_t idio_vm_FINISH_pc;
extern idio_ai_t idio_vm_NCE_pc;
extern idio_ai_t idio_vm_CHR_pc;
extern idio_ai_t idio_vm_IHR_pc;
extern idio_ai_t idio_vm_AR_pc;
extern size_t idio_prologue_len;
extern int idio_vm_exit;

void idio_vm_panic (IDIO thr, char *m);
IDIO idio_vm_closure_name (IDIO c);

IDIO idio_vm_run (IDIO thr);

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
IDIO idio_vm_dynamic_ref (idio_ai_t msi, idio_ai_t gvi, IDIO thr, IDIO args);
void idio_vm_dynamic_set (idio_ai_t msi, idio_ai_t gvi, IDIO v, IDIO thr);
IDIO idio_vm_environ_ref (idio_ai_t msi, idio_ai_t gvi, IDIO thr, IDIO args);
void idio_vm_environ_set (idio_ai_t msi, idio_ai_t gvi, IDIO v, IDIO thr);
IDIO idio_vm_computed_ref (idio_ai_t msi, idio_ai_t gvi, IDIO thr);
IDIO idio_vm_computed_set (idio_ai_t msi, idio_ai_t gvi, IDIO v, IDIO thr);
void idio_vm_computed_define (idio_ai_t msi, idio_ai_t gvi, IDIO v, IDIO thr);
void idio_vm_add_module_constants (IDIO module, IDIO constants);

void idio_raise_condition (IDIO continuablep, IDIO e);
IDIO idio_apply (IDIO fn, IDIO args);
void idio_vm_debug (IDIO thr, char *prefix, idio_ai_t stack_start);
#ifdef IDIO_VM_PERF
void idio_vm_func_start (IDIO clos, struct timespec *tsp);
void idio_vm_func_stop (IDIO clos, struct timespec *tsp);
void idio_vm_prim_time (IDIO clos, struct timespec *ts0p, struct timespec *tsep);
#endif
IDIO idio_vm_invoke_C (IDIO thr, IDIO command);
IDIO idio_vm_source_location ();

void idio_vm_thread_init (IDIO thr);
void idio_vm_default_pc (IDIO thr);
void idio_vm_thread_state ();

time_t idio_vm_elapsed (void);

void idio_init_vm_values ();
void idio_init_vm ();
void idio_vm_add_primitives ();
void idio_final_vm ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
