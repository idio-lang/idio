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
 */
#define IDIO_A_SHALLOW_ARGUMENT_REF0       1
#define IDIO_A_SHALLOW_ARGUMENT_REF1       2
#define IDIO_A_SHALLOW_ARGUMENT_REF2       3
#define IDIO_A_SHALLOW_ARGUMENT_REF3       4
#define IDIO_A_SHALLOW_ARGUMENT_REF        5
#define IDIO_A_DEEP_ARGUMENT_REF           6
#define IDIO_A_GLOBAL_REF                  7
#define IDIO_A_CHECKED_GLOBAL_REF          8
#define IDIO_A_CHECKED_GLOBAL_FUNCTION_REF 9
#define IDIO_A_CONSTANT_REF                10 

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

#define IDIO_A_FINISH                      30

#define IDIO_A_SHALLOW_ARGUMENT_SET0       31
#define IDIO_A_SHALLOW_ARGUMENT_SET1       32
#define IDIO_A_SHALLOW_ARGUMENT_SET2       33
#define IDIO_A_SHALLOW_ARGUMENT_SET3       34
#define IDIO_A_SHALLOW_ARGUMENT_SET        35
#define IDIO_A_DEEP_ARGUMENT_SET           36
#define IDIO_A_GLOBAL_SET                  37

#define IDIO_A_LONG_GOTO                   40
#define IDIO_A_LONG_JUMP_FALSE             41
#define IDIO_A_SHORT_GOTO                  42
#define IDIO_A_SHORT_JUMP_FALSE            43

#define IDIO_A_EXTEND_ENV                  50
#define IDIO_A_UNLINK_ENV                  51
#define IDIO_A_PRESERVE_ENV                52
#define IDIO_A_RESTORE_ENV                 53
#define IDIO_A_PUSH_VALUE                  54
#define IDIO_A_POP_REG1                    55
#define IDIO_A_POP_REG2                    56
#define IDIO_A_POP_FUNCTION                57
#define IDIO_A_CREATE_CLOSURE              58
#define IDIO_A_RETURN                      59
#define IDIO_A_PACK_FRAME                  60
#define IDIO_A_FUNCTION_INVOKE             61
#define IDIO_A_FUNCTION_GOTO               62
#define IDIO_A_POP_CONS_FRAME              63

#define IDIO_A_ALLOCATE_FRAME1             70
#define IDIO_A_ALLOCATE_FRAME2             71
#define IDIO_A_ALLOCATE_FRAME3             72
#define IDIO_A_ALLOCATE_FRAME4             73
#define IDIO_A_ALLOCATE_FRAME5             74
#define IDIO_A_ALLOCATE_FRAME              75
#define IDIO_A_ALLOCATE_DOTTED_FRAME       76

#define IDIO_A_POP_ENV0                    80
#define IDIO_A_POP_ENV1                    81
#define IDIO_A_POP_ENV2                    82
#define IDIO_A_POP_ENV3                    83
#define IDIO_A_POP_ENV                     84

#define IDIO_A_ARITYP1                     90
#define IDIO_A_ARITYP2                     91
#define IDIO_A_ARITYP3                     92
#define IDIO_A_ARITYP4                     93
#define IDIO_A_ARITYEQP                    94
#define IDIO_A_ARITYGEP                    95

#define IDIO_A_SHORT_NUMBER                100
#define IDIO_A_SHORT_NEG_NUMBER            101
#define IDIO_A_CONSTANT_0                  102
#define IDIO_A_CONSTANT_1                  103
#define IDIO_A_CONSTANT_2                  104
#define IDIO_A_CONSTANT_3                  105
#define IDIO_A_CONSTANT_4                  106

#define IDIO_A_PRIMCALL0_NEWLINE           110
#define IDIO_A_PRIMCALL0_READ              111
#define IDIO_A_PRIMCALL1_CAR               112
#define IDIO_A_PRIMCALL1_CDR               113
#define IDIO_A_PRIMCALL1_PAIRP             114
#define IDIO_A_PRIMCALL1_SYMBOLP           115
#define IDIO_A_PRIMCALL1_DISPLAY           116
#define IDIO_A_PRIMCALL1_PRIMITIVEP        117
#define IDIO_A_PRIMCALL1_NULLP             118
#define IDIO_A_PRIMCALL1_CONTINUATIONP     119
#define IDIO_A_PRIMCALL1_EOFP              120
#define IDIO_A_PRIMCALL2_CONS              121
#define IDIO_A_PRIMCALL2_EQP               122
#define IDIO_A_PRIMCALL2_SET_CAR           123
#define IDIO_A_PRIMCALL2_SET_CDR           124
#define IDIO_A_PRIMCALL2_ADD               125
#define IDIO_A_PRIMCALL2_SUBTRACT          126
#define IDIO_A_PRIMCALL2_EQ                127
#define IDIO_A_PRIMCALL2_LT                128
#define IDIO_A_PRIMCALL2_GT                129
#define IDIO_A_PRIMCALL2_MULTIPLY          130
#define IDIO_A_PRIMCALL2_LE                131
#define IDIO_A_PRIMCALL2_GE                132
#define IDIO_A_PRIMCALL2_REMAINDER         133

#define IDIO_A_NOP                         140
#define IDIO_A_PRIMCALL0                   141
#define IDIO_A_PRIMCALL1                   142
#define IDIO_A_PRIMCALL2                   143
#define IDIO_A_PRIMCALL3                   144
#define IDIO_A_PRIMCALL                    145

#define IDIO_A_LONG_JUMP_TRUE              150
#define IDIO_A_SHORT_JUMP_TRUE             151
#define IDIO_A_FIXNUM                      152
#define IDIO_A_NEG_FIXNUM                  153
#define IDIO_A_CHARACTER                   154
#define IDIO_A_NEG_CHARACTER               155
#define IDIO_A_CONSTANT                    156
#define IDIO_A_NEG_CONSTANT                157

#define IDIO_A_EXPANDER                    160

#define IDIO_A_DYNAMIC_REF                 240
#define IDIO_A_POP_DYNAMIC                 241
#define IDIO_A_PUSH_DYNAMIC                242

#define IDIO_A_NON_CONT_ERR                245
#define IDIO_A_PUSH_HANDLER                246
#define IDIO_A_POP_HANDLER                 247
#define IDIO_A_POP_ESCAPER                 250
#define IDIO_A_PUSH_ESCAPER                251

/*
 * Some unique constants for the VM
 *
 * They don't have to be different to any other constants but if they
 * are then we can help with debugging.  (There's plenty of room in
 * the constants table.)
 *
 * Arbitrary gaps in the numbering to ease inserting another
 */
#define IDIO_VM_CODE_BASE			 2000
#define IDIO_VM_CODE_SHALLOW_ARGUMENT_REF        (IDIO_VM_CODE_BASE+0)
#define IDIO_VM_CODE_PREDEFINED                  (IDIO_VM_CODE_BASE+1)
#define IDIO_VM_CODE_DEEP_ARGUMENT_REF           (IDIO_VM_CODE_BASE+2)
#define IDIO_VM_CODE_SHALLOW_ARGUMENT_SET        (IDIO_VM_CODE_BASE+3)
#define IDIO_VM_CODE_DEEP_ARGUMENT_SET           (IDIO_VM_CODE_BASE+4)

#define IDIO_VM_CODE_GLOBAL_REF                  (IDIO_VM_CODE_BASE+10)
#define IDIO_VM_CODE_CHECKED_GLOBAL_REF          (IDIO_VM_CODE_BASE+11)
#define IDIO_VM_CODE_CHECKED_GLOBAL_FUNCTION_REF (IDIO_VM_CODE_BASE+12)
#define IDIO_VM_CODE_GLOBAL_SET                  (IDIO_VM_CODE_BASE+13)
#define IDIO_VM_CODE_CONSTANT                    (IDIO_VM_CODE_BASE+14)
    
#define IDIO_VM_CODE_ALTERNATIVE                 (IDIO_VM_CODE_BASE+20)
#define IDIO_VM_CODE_SEQUENCE                    (IDIO_VM_CODE_BASE+21)
#define IDIO_VM_CODE_TR_FIX_LET                  (IDIO_VM_CODE_BASE+22)
#define IDIO_VM_CODE_FIX_LET                     (IDIO_VM_CODE_BASE+23)
#define IDIO_VM_CODE_PRIMCALL0                   (IDIO_VM_CODE_BASE+24)

#define IDIO_VM_CODE_PRIMCALL1                   (IDIO_VM_CODE_BASE+30)
#define IDIO_VM_CODE_PRIMCALL2                   (IDIO_VM_CODE_BASE+31)
#define IDIO_VM_CODE_PRIMCALL3                   (IDIO_VM_CODE_BASE+32)
#define IDIO_VM_CODE_FIX_CLOSURE                 (IDIO_VM_CODE_BASE+33)
#define IDIO_VM_CODE_NARY_CLOSURE                (IDIO_VM_CODE_BASE+34)
    
#define IDIO_VM_CODE_TR_REGULAR_CALL             (IDIO_VM_CODE_BASE+40)
#define IDIO_VM_CODE_REGULAR_CALL                (IDIO_VM_CODE_BASE+41)
#define IDIO_VM_CODE_STORE_ARGUMENT              (IDIO_VM_CODE_BASE+42)
#define IDIO_VM_CODE_CONS_ARGUMENT               (IDIO_VM_CODE_BASE+43)
#define IDIO_VM_CODE_ALLOCATE_FRAME              (IDIO_VM_CODE_BASE+44)

#define IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME       (IDIO_VM_CODE_BASE+50)
#define IDIO_VM_CODE_FINISH                      (IDIO_VM_CODE_BASE+51)
#define IDIO_VM_CODE_PUSH_DYNAMIC                (IDIO_VM_CODE_BASE+52)
#define IDIO_VM_CODE_POP_DYNAMIC                 (IDIO_VM_CODE_BASE+53)
#define IDIO_VM_CODE_DYNAMIC_REF                 (IDIO_VM_CODE_BASE+54)
    
#define IDIO_VM_CODE_PUSH_HANDLER                (IDIO_VM_CODE_BASE+60)
#define IDIO_VM_CODE_POP_HANDLER                 (IDIO_VM_CODE_BASE+61)
#define IDIO_VM_CODE_AND                         (IDIO_VM_CODE_BASE+62)
#define IDIO_VM_CODE_OR                          (IDIO_VM_CODE_BASE+63)
#define IDIO_VM_CODE_BEGIN                       (IDIO_VM_CODE_BASE+64)

#define IDIO_VM_CODE_EXPANDER                    (IDIO_VM_CODE_BASE+70)

#define IDIO_VM_CODE_NOP                         (IDIO_VM_CODE_BASE+99)

/*
 * Idio Intermediate code: idio_I_*
 *
 * These are the coarse instructions for the VM.  The actual assmebly
 * code will specialize these.
 */
#define idio_I_SHALLOW_ARGUMENT_REF        ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_SHALLOW_ARGUMENT_REF))
#define idio_I_PREDEFINED                  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PREDEFINED))
#define idio_I_DEEP_ARGUMENT_REF           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_DEEP_ARGUMENT_REF))
#define idio_I_SHALLOW_ARGUMENT_SET        ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_SHALLOW_ARGUMENT_SET))
#define idio_I_DEEP_ARGUMENT_SET           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_DEEP_ARGUMENT_SET))
#define idio_I_GLOBAL_REF                  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_GLOBAL_REF))
#define idio_I_CHECKED_GLOBAL_REF          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CHECKED_GLOBAL_REF))
#define idio_I_CHECKED_GLOBAL_FUNCTION_REF ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CHECKED_GLOBAL_FUNCTION_REF))
#define idio_I_GLOBAL_SET                  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_GLOBAL_SET))
#define idio_I_CONSTANT                    ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CONSTANT))
#define idio_I_ALTERNATIVE                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALTERNATIVE))
#define idio_I_SEQUENCE                    ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_SEQUENCE))
#define idio_I_TR_FIX_LET                  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_TR_FIX_LET))
#define idio_I_FIX_LET                     ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_FIX_LET))
#define idio_I_PRIMCALL0                   ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL0))
#define idio_I_PRIMCALL1                   ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL1))
#define idio_I_PRIMCALL2                   ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL2))
#define idio_I_PRIMCALL3                   ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL3))
#define idio_I_FIX_CLOSURE                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_FIX_CLOSURE))
#define idio_I_NARY_CLOSURE                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_NARY_CLOSURE))
#define idio_I_TR_REGULAR_CALL             ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_TR_REGULAR_CALL))
#define idio_I_REGULAR_CALL                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_REGULAR_CALL))
#define idio_I_STORE_ARGUMENT              ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_STORE_ARGUMENT))
#define idio_I_CONS_ARGUMENT               ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CONS_ARGUMENT))
#define idio_I_ALLOCATE_FRAME              ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALLOCATE_FRAME))
#define idio_I_ALLOCATE_DOTTED_FRAME       ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME))
#define idio_I_FINISH                      ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_FINISH))
#define idio_I_PUSH_DYNAMIC                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PUSH_DYNAMIC))
#define idio_I_POP_DYNAMIC                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POP_DYNAMIC))
#define idio_I_DYNAMIC_REF                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_DYNAMIC_REF))
#define idio_I_PUSH_HANDLER                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PUSH_HANDLER))
#define idio_I_POP_HANDLER                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POP_HANDLER))
#define idio_I_AND                         ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_AND))
#define idio_I_OR                          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_OR))
#define idio_I_BEGIN                       ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_BEGIN))
#define idio_I_EXPANDER                    ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_EXPANDER))

#define idio_I_NOP                         ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_NOP))

void idio_vm_codegen (IDIO thr, IDIO m);
IDIO idio_vm_run (IDIO thr, int run_gc);

idio_ai_t idio_vm_extend_constants (IDIO v);
IDIO idio_vm_constants_ref (idio_ai_t i);
idio_ai_t idio_vm_extend_symbols (IDIO v);
IDIO idio_vm_symbols_ref (idio_ai_t i);
idio_ai_t idio_vm_symbols_lookup (IDIO v);
idio_ai_t idio_vm_extend_primitives (IDIO v);
IDIO idio_vm_primitives_ref (idio_ai_t i);
idio_ai_t idio_vm_extend_dynamics (IDIO v);
IDIO idio_vm_dynamics_ref (idio_ai_t i);
void idio_vm_abort_thread (IDIO thr);

IDIO idio_signal_exception (IDIO continuablep, IDIO e);
IDIO idio_apply (IDIO fn, IDIO args);

void idio_vm_thread_init (IDIO thr);
void idio_vm_default_pc (IDIO thr);
void idio_init_vm ();
void idio_vm_add_primitives ();
void idio_final_vm ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
