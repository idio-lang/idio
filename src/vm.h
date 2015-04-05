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
#define IDIO_A_SHALLOW_ARGUMENT_REF0   1
#define IDIO_A_SHALLOW_ARGUMENT_REF1   2
#define IDIO_A_SHALLOW_ARGUMENT_REF2   3
#define IDIO_A_SHALLOW_ARGUMENT_REF3   4
#define IDIO_A_SHALLOW_ARGUMENT_REF    5
#define IDIO_A_DEEP_ARGUMENT_REF       6
#define IDIO_A_GLOBAL_REF              7
#define IDIO_A_CHECKED_GLOBAL_REF      8
#define IDIO_A_CONSTANT                9

#define IDIO_A_PREDEFINED0             10
#define IDIO_A_PREDEFINED1             11
#define IDIO_A_PREDEFINED2             12
#define IDIO_A_PREDEFINED3             13
#define IDIO_A_PREDEFINED4             14
#define IDIO_A_PREDEFINED5             15
#define IDIO_A_PREDEFINED6             16
#define IDIO_A_PREDEFINED7             17
#define IDIO_A_PREDEFINED8             18

#define IDIO_A_PREDEFINED              19

#define IDIO_A_FINISH                  20

#define IDIO_A_SHALLOW_ARGUMENT_SET0   21
#define IDIO_A_SHALLOW_ARGUMENT_SET1   22
#define IDIO_A_SHALLOW_ARGUMENT_SET2   23
#define IDIO_A_SHALLOW_ARGUMENT_SET3   24
#define IDIO_A_SHALLOW_ARGUMENT_SET    25
#define IDIO_A_DEEP_ARGUMENT_SET       26
#define IDIO_A_GLOBAL_SET              27

#define IDIO_A_LONG_GOTO               28
#define IDIO_A_LONG_JUMP_FALSE         29
#define IDIO_A_SHORT_GOTO              30
#define IDIO_A_SHORT_JUMP_FALSE        31

#define IDIO_A_EXTEND_ENV              32
#define IDIO_A_UNLINK_ENV              33
#define IDIO_A_PRESERVE_ENV            34
#define IDIO_A_RESTORE_ENV             35
#define IDIO_A_PUSH_VALUE              36
#define IDIO_A_POP_REG1                37
#define IDIO_A_POP_REG2                38
#define IDIO_A_POP_FUNCTION            39
#define IDIO_A_CREATE_CLOSURE          40
#define IDIO_A_RETURN                  43
#define IDIO_A_PACK_FRAME              44
#define IDIO_A_FUNCTION_INVOKE         45
#define IDIO_A_FUNCTION_GOTO           46
#define IDIO_A_POP_CONS_FRAME          47

#define IDIO_A_ALLOCATE_FRAME1         50
#define IDIO_A_ALLOCATE_FRAME2         51
#define IDIO_A_ALLOCATE_FRAME3         52
#define IDIO_A_ALLOCATE_FRAME4         53
#define IDIO_A_ALLOCATE_FRAME5         54
#define IDIO_A_ALLOCATE_FRAME          55
#define IDIO_A_ALLOCATE_DOTTED_FRAME   56

#define IDIO_A_POP_FRAME0              60
#define IDIO_A_POP_FRAME1              61
#define IDIO_A_POP_FRAME2              62
#define IDIO_A_POP_FRAME3              63
#define IDIO_A_POP_FRAME               64

#define IDIO_A_ARITYP1                 71
#define IDIO_A_ARITYP2                 72
#define IDIO_A_ARITYP3                 73
#define IDIO_A_ARITYP4                 74
#define IDIO_A_ARITYEQP                75
#define IDIO_A_ARITYGEP                78

#define IDIO_A_SHORT_NUMBER            79

#define IDIO_A_CONSTANT_M1             80
#define IDIO_A_CONSTANT_0              81
#define IDIO_A_CONSTANT_1              82
#define IDIO_A_CONSTANT_2              83
#define IDIO_A_CONSTANT_3              84
#define IDIO_A_CONSTANT_4              85

#define IDIO_A_PRIMCALL0_NEWLINE       88
#define IDIO_A_PRIMCALL0_READ          89
#define IDIO_A_PRIMCALL1_CAR           90
#define IDIO_A_PRIMCALL1_CDR           91
#define IDIO_A_PRIMCALL1_PAIRP         92
#define IDIO_A_PRIMCALL1_SYMBOLP       93
#define IDIO_A_PRIMCALL1_DISPLAY       94
#define IDIO_A_PRIMCALL1_PRIMITIVEP    95
#define IDIO_A_PRIMCALL1_NULLP         96
#define IDIO_A_PRIMCALL1_CONTINUATIONP 97
#define IDIO_A_PRIMCALL1_EOFP          98
#define IDIO_A_PRIMCALL2_CONS          100
#define IDIO_A_PRIMCALL2_EQP           101
#define IDIO_A_PRIMCALL2_SET_CAR       102
#define IDIO_A_PRIMCALL2_SET_CDR       103
#define IDIO_A_PRIMCALL2_ADD           104
#define IDIO_A_PRIMCALL2_SUBTRACT      105
#define IDIO_A_PRIMCALL2_EQ            106
#define IDIO_A_PRIMCALL2_LT            107
#define IDIO_A_PRIMCALL2_GT            108
#define IDIO_A_PRIMCALL2_MULTIPLY      109
#define IDIO_A_PRIMCALL2_LE            110
#define IDIO_A_PRIMCALL2_GE            111
#define IDIO_A_PRIMCALL2_REMAINDER     112

#define IDIO_A_NOP                     128
#define IDIO_A_PRIMCALL0               129
#define IDIO_A_PRIMCALL1               130
#define IDIO_A_PRIMCALL2               131
#define IDIO_A_PRIMCALL3               132
#define IDIO_A_PRIMCALL                133

#define IDIO_A_DYNAMIC_REF             240
#define IDIO_A_POP_DYNAMIC             241
#define IDIO_A_PUSH_DYNAMIC            242

#define IDIO_A_NON_CONT_ERR            245
#define IDIO_A_PUSH_HANDLER            246
#define IDIO_A_POP_HANDLER             247
#define IDIO_A_POP_ESCAPER             250
#define IDIO_A_PUSH_ESCAPER            251

/*
 * Some unique constants for the VM
 */
#define IDIO_VM_CODE_BASE		   2000
#define IDIO_VM_CODE_SHALLOW_ARGUMENT_REF  (IDIO_VM_CODE_BASE+0)
#define IDIO_VM_CODE_PREDEFINED            (IDIO_VM_CODE_BASE+1)
#define IDIO_VM_CODE_DEEP_ARGUMENT_REF     (IDIO_VM_CODE_BASE+2)
#define IDIO_VM_CODE_SHALLOW_ARGUMENT_SET  (IDIO_VM_CODE_BASE+3)
#define IDIO_VM_CODE_DEEP_ARGUMENT_SET     (IDIO_VM_CODE_BASE+4)
#define IDIO_VM_CODE_GLOBAL_REF            (IDIO_VM_CODE_BASE+5)
#define IDIO_VM_CODE_CHECKED_GLOBAL_REF    (IDIO_VM_CODE_BASE+6)
#define IDIO_VM_CODE_GLOBAL_SET            (IDIO_VM_CODE_BASE+7)
#define IDIO_VM_CODE_CONSTANT              (IDIO_VM_CODE_BASE+8)
#define IDIO_VM_CODE_ALTERNATIVE           (IDIO_VM_CODE_BASE+9)
#define IDIO_VM_CODE_SEQUENCE              (IDIO_VM_CODE_BASE+10)
#define IDIO_VM_CODE_TR_FIX_LET            (IDIO_VM_CODE_BASE+11)
#define IDIO_VM_CODE_FIX_LET               (IDIO_VM_CODE_BASE+12)
#define IDIO_VM_CODE_PRIMCALL0             (IDIO_VM_CODE_BASE+13)
#define IDIO_VM_CODE_PRIMCALL1             (IDIO_VM_CODE_BASE+14)
#define IDIO_VM_CODE_PRIMCALL2             (IDIO_VM_CODE_BASE+15)
#define IDIO_VM_CODE_PRIMCALL3             (IDIO_VM_CODE_BASE+16)
#define IDIO_VM_CODE_FIX_CLOSURE           (IDIO_VM_CODE_BASE+17)
#define IDIO_VM_CODE_NARY_CLOSURE          (IDIO_VM_CODE_BASE+18)
#define IDIO_VM_CODE_TR_REGULAR_CALL       (IDIO_VM_CODE_BASE+19)
#define IDIO_VM_CODE_REGULAR_CALL          (IDIO_VM_CODE_BASE+20)
#define IDIO_VM_CODE_STORE_ARGUMENT        (IDIO_VM_CODE_BASE+21)
#define IDIO_VM_CODE_CONS_ARGUMENT         (IDIO_VM_CODE_BASE+22)
#define IDIO_VM_CODE_ALLOCATE_FRAME        (IDIO_VM_CODE_BASE+23)
#define IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME (IDIO_VM_CODE_BASE+24)
#define IDIO_VM_CODE_FINISH                (IDIO_VM_CODE_BASE+25)
#define IDIO_VM_CODE_PUSH_DYNAMIC	   (IDIO_VM_CODE_BASE+26)
#define IDIO_VM_CODE_POP_DYNAMIC	   (IDIO_VM_CODE_BASE+27)
#define IDIO_VM_CODE_DYNAMIC_REF	   (IDIO_VM_CODE_BASE+28)
#define IDIO_VM_CODE_PUSH_HANDLER	   (IDIO_VM_CODE_BASE+29)
#define IDIO_VM_CODE_POP_HANDLER	   (IDIO_VM_CODE_BASE+30)

/*
 * Idio Intermediate code: idio_I_*
 *
 * These are the coarse instructions for the VM.  The actual assmebly
 * code will specialize these.
 */
#define idio_I_SHALLOW_ARGUMENT_REF  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_SHALLOW_ARGUMENT_REF))
#define idio_I_PREDEFINED            ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PREDEFINED))
#define idio_I_DEEP_ARGUMENT_REF     ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_DEEP_ARGUMENT_REF))
#define idio_I_SHALLOW_ARGUMENT_SET  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_SHALLOW_ARGUMENT_SET))
#define idio_I_DEEP_ARGUMENT_SET     ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_DEEP_ARGUMENT_SET))
#define idio_I_GLOBAL_REF            ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_GLOBAL_REF))
#define idio_I_CHECKED_GLOBAL_REF    ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CHECKED_GLOBAL_REF))
#define idio_I_GLOBAL_SET            ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_GLOBAL_SET))
#define idio_I_CONSTANT              ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CONSTANT))
#define idio_I_ALTERNATIVE           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALTERNATIVE))
#define idio_I_SEQUENCE              ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_SEQUENCE))
#define idio_I_TR_FIX_LET            ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_TR_FIX_LET))
#define idio_I_FIX_LET               ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_FIX_LET))
#define idio_I_PRIMCALL0             ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL0))
#define idio_I_PRIMCALL1             ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL1))
#define idio_I_PRIMCALL2             ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL2))
#define idio_I_PRIMCALL3             ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PRIMCALL3))
#define idio_I_FIX_CLOSURE           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_FIX_CLOSURE))
#define idio_I_NARY_CLOSURE          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_NARY_CLOSURE))
#define idio_I_TR_REGULAR_CALL       ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_TR_REGULAR_CALL))
#define idio_I_REGULAR_CALL          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_REGULAR_CALL))
#define idio_I_STORE_ARGUMENT        ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_STORE_ARGUMENT))
#define idio_I_CONS_ARGUMENT         ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CONS_ARGUMENT))
#define idio_I_ALLOCATE_FRAME        ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALLOCATE_FRAME))
#define idio_I_ALLOCATE_DOTTED_FRAME ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME))
#define idio_I_FINISH                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_FINISH))
#define idio_I_PUSH_DYNAMIC          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PUSH_DYNAMIC))
#define idio_I_POP_DYNAMIC           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POP_DYNAMIC))
#define idio_I_DYNAMIC_REF           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_DYNAMIC_REF))
#define idio_I_PUSH_HANDLER          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PUSH_HANDLER))
#define idio_I_POP_HANDLER           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POP_HANDLER))

void idio_vm_codegen (IDIO thr, IDIO m);
void idio_vm_run (IDIO thr);

idio_ai_t idio_vm_extend_constants (IDIO v);
IDIO idio_vm_constants_ref (idio_ai_t i);
idio_ai_t idio_vm_extend_symbols (IDIO v);
IDIO idio_vm_symbols_ref (idio_ai_t i);
idio_ai_t idio_vm_extend_primitives (IDIO v);
IDIO idio_vm_primitives_ref (idio_ai_t i);
idio_ai_t idio_vm_extend_dynamics (IDIO v);
IDIO idio_vm_dynamics_ref (idio_ai_t i);
void idio_vm_abort_thread (IDIO thr);

void idio_signal_exception (int continuablep, IDIO e);

void idio_init_vm ();
void idio_vm_add_primitives ();
void idio_final_vm ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
