/*
 * Copyright (c) 2015, 2017 Ian Fitchet <idf(at)idio-lang.org>
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
 * codegen.h
 * 
 */

#ifndef CODEGEN_H
#define CODEGEN_H

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
#define IDIO_VM_CODE_GLOBAL_DEF                  (IDIO_VM_CODE_BASE+13)
#define IDIO_VM_CODE_GLOBAL_SET                  (IDIO_VM_CODE_BASE+14)
#define IDIO_VM_CODE_CONSTANT                    (IDIO_VM_CODE_BASE+15)
#define IDIO_VM_CODE_COMPUTED_REF                (IDIO_VM_CODE_BASE+16)
#define IDIO_VM_CODE_COMPUTED_SET                (IDIO_VM_CODE_BASE+17)
#define IDIO_VM_CODE_COMPUTED_DEFINE             (IDIO_VM_CODE_BASE+18)
    
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
#define IDIO_VM_CODE_DYNAMIC_FUNCTION_REF	 (IDIO_VM_CODE_BASE+55)
    
#define IDIO_VM_CODE_PUSH_ENVIRON                (IDIO_VM_CODE_BASE+60)
#define IDIO_VM_CODE_POP_ENVIRON                 (IDIO_VM_CODE_BASE+61)
#define IDIO_VM_CODE_ENVIRON_REF                 (IDIO_VM_CODE_BASE+62)
    
#define IDIO_VM_CODE_PUSH_HANDLER                (IDIO_VM_CODE_BASE+70)
#define IDIO_VM_CODE_POP_HANDLER                 (IDIO_VM_CODE_BASE+71)
#define IDIO_VM_CODE_PUSH_TRAP                   (IDIO_VM_CODE_BASE+72)
#define IDIO_VM_CODE_POP_TRAP                    (IDIO_VM_CODE_BASE+73)

#define IDIO_VM_CODE_AND                         (IDIO_VM_CODE_BASE+75)
#define IDIO_VM_CODE_OR                          (IDIO_VM_CODE_BASE+76)
#define IDIO_VM_CODE_BEGIN                       (IDIO_VM_CODE_BASE+77)

#define IDIO_VM_CODE_EXPANDER                    (IDIO_VM_CODE_BASE+80)
#define IDIO_VM_CODE_INFIX_OPERATOR              (IDIO_VM_CODE_BASE+81)
#define IDIO_VM_CODE_POSTFIX_OPERATOR            (IDIO_VM_CODE_BASE+82)

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
#define idio_I_GLOBAL_DEF                  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_GLOBAL_DEF))
#define idio_I_GLOBAL_SET                  ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_GLOBAL_SET))
#define idio_I_COMPUTED_REF                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_COMPUTED_REF))
#define idio_I_COMPUTED_SET                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_COMPUTED_SET))
#define idio_I_COMPUTED_DEFINE             ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_COMPUTED_DEFINE))
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
#define idio_I_DYNAMIC_FUNCTION_REF	   ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_DYNAMIC_FUNCTION_REF))
#define idio_I_PUSH_ENVIRON                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PUSH_ENVIRON))
#define idio_I_POP_ENVIRON                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POP_ENVIRON))
#define idio_I_ENVIRON_REF                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ENVIRON_REF))
#define idio_I_PUSH_HANDLER                ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PUSH_HANDLER))
#define idio_I_POP_HANDLER                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POP_HANDLER))
#define idio_I_PUSH_TRAP                   ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_PUSH_TRAP))
#define idio_I_POP_TRAP                    ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POP_TRAP))
#define idio_I_AND                         ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_AND))
#define idio_I_OR                          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_OR))
#define idio_I_BEGIN                       ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_BEGIN))
#define idio_I_EXPANDER                    ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_EXPANDER))
#define idio_I_INFIX_OPERATOR              ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_INFIX_OPERATOR))
#define idio_I_POSTFIX_OPERATOR            ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_POSTFIX_OPERATOR))

#define idio_I_NOP                         ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_NOP))

IDIO_IA_T idio_ia (size_t n);
void idio_ia_free (IDIO_IA_T ia);
void idio_ia_push (IDIO_IA_T ia, IDIO_I ins);
idio_ai_t idio_codegen_extend_constants (IDIO cs, IDIO v);
idio_ai_t idio_codegen_constants_lookup (IDIO cs, IDIO v);
idio_ai_t idio_codegen_constants_lookup_or_extend (IDIO cs, IDIO v);
void idio_codegen_code_prologue (IDIO_IA_T ia);
void idio_codegen (IDIO thr, IDIO m, IDIO cs);

void idio_init_codegen ();
void idio_codegen_add_primitives ();
void idio_final_codegen ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
