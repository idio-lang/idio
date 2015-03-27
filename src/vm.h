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
#define IDIO_VM_CODE_CALL0                 (IDIO_VM_CODE_BASE+13)
#define IDIO_VM_CODE_CALL1                 (IDIO_VM_CODE_BASE+14)
#define IDIO_VM_CODE_CALL2                 (IDIO_VM_CODE_BASE+15)
#define IDIO_VM_CODE_CALL3                 (IDIO_VM_CODE_BASE+16)
#define IDIO_VM_CODE_FIX_CLOSURE           (IDIO_VM_CODE_BASE+17)
#define IDIO_VM_CODE_NARY_CLOSURE          (IDIO_VM_CODE_BASE+18)
#define IDIO_VM_CODE_TR_REGULAR_CALL       (IDIO_VM_CODE_BASE+19)
#define IDIO_VM_CODE_REGULAR_CALL          (IDIO_VM_CODE_BASE+20)
#define IDIO_VM_CODE_STORE_ARGUMENT        (IDIO_VM_CODE_BASE+21)
#define IDIO_VM_CODE_CONS_ARGUMENT         (IDIO_VM_CODE_BASE+22)
#define IDIO_VM_CODE_ALLOCATE_FRAME        (IDIO_VM_CODE_BASE+23)
#define IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME (IDIO_VM_CODE_BASE+24)

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
#define idio_I_CALL0                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CALL0))
#define idio_I_CALL1                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CALL1))
#define idio_I_CALL2                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CALL2))
#define idio_I_CALL3                 ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CALL3))
#define idio_I_FIX_CLOSURE           ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_FIX_CLOSURE))
#define idio_I_NARY_CLOSURE          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_NARY_CLOSURE))
#define idio_I_TR_REGULAR_CALL       ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_TR_REGULAR_CALL))
#define idio_I_REGULAR_CALL          ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_REGULAR_CALL))
#define idio_I_STORE_ARGUMENT        ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_STORE_ARGUMENT))
#define idio_I_CONS_ARGUMENT         ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_CONS_ARGUMENT))
#define idio_I_ALLOCATE_FRAME        ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALLOCATE_FRAME))
#define idio_I_ALLOCATE_DOTTED_FRAME ((const IDIO) IDIO_CONSTANT (IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME))

void idio_vm_codegen (IDIO code);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
