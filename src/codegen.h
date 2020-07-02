/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 */
#define IDIO_I_CODE_SHALLOW_ARGUMENT_REF		10
#define IDIO_I_CODE_DEEP_ARGUMENT_REF			11

#define IDIO_I_CODE_SHALLOW_ARGUMENT_SET		12
#define IDIO_I_CODE_DEEP_ARGUMENT_SET			13

#define IDIO_I_CODE_GLOBAL_SYM_REF			20
#define IDIO_I_CODE_CHECKED_GLOBAL_SYM_REF		21
#define IDIO_I_CODE_GLOBAL_FUNCTION_SYM_REF		22
#define IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_SYM_REF	23
#define IDIO_I_CODE_CONSTANT_SYM_REF			24
#define IDIO_I_CODE_COMPUTED_SYM_REF			25

#define IDIO_I_CODE_GLOBAL_SYM_DEF			30
#define IDIO_I_CODE_GLOBAL_SYM_SET			31
#define IDIO_I_CODE_COMPUTED_SYM_SET			32
#define IDIO_I_CODE_COMPUTED_SYM_DEFINE			33

#define IDIO_I_CODE_GLOBAL_VAL_REF			40
#define IDIO_I_CODE_CHECKED_GLOBAL_VAL_REF		41
#define IDIO_I_CODE_GLOBAL_FUNCTION_VAL_REF		42
#define IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_VAL_REF	43
#define IDIO_I_CODE_CONSTANT_VAL_REF			44
#define IDIO_I_CODE_COMPUTED_VAL_REF			45

#define IDIO_I_CODE_GLOBAL_VAL_DEF			50
#define IDIO_I_CODE_GLOBAL_VAL_SET			51
#define IDIO_I_CODE_COMPUTED_VAL_SET			52
#define IDIO_I_CODE_COMPUTED_VAL_DEFINE			53

#define IDIO_I_CODE_PREDEFINED				60
#define IDIO_I_CODE_ALTERNATIVE				61
#define IDIO_I_CODE_SEQUENCE				62
#define IDIO_I_CODE_TR_FIX_LET				63
#define IDIO_I_CODE_FIX_LET				64

#define IDIO_I_CODE_PRIMCALL0				70
#define IDIO_I_CODE_PRIMCALL1				71
#define IDIO_I_CODE_PRIMCALL2				72
#define IDIO_I_CODE_PRIMCALL3				73
#define IDIO_I_CODE_TR_REGULAR_CALL			74
#define IDIO_I_CODE_REGULAR_CALL			75

#define IDIO_I_CODE_FIX_CLOSURE				80
#define IDIO_I_CODE_NARY_CLOSURE			81

#define IDIO_I_CODE_STORE_ARGUMENT			90
#define IDIO_I_CODE_CONS_ARGUMENT			91
#define IDIO_I_CODE_ALLOCATE_FRAME			92
#define IDIO_I_CODE_ALLOCATE_DOTTED_FRAME		93

#define IDIO_I_CODE_PUSH_DYNAMIC			101
#define IDIO_I_CODE_POP_DYNAMIC				102
#define IDIO_I_CODE_DYNAMIC_SYM_REF			103
#define IDIO_I_CODE_DYNAMIC_FUNCTION_SYM_REF		104
#define IDIO_I_CODE_DYNAMIC_VAL_REF			105
#define IDIO_I_CODE_DYNAMIC_FUNCTION_VAL_REF		106

#define IDIO_I_CODE_PUSH_ENVIRON			110
#define IDIO_I_CODE_POP_ENVIRON				111
#define IDIO_I_CODE_ENVIRON_SYM_REF			112
#define IDIO_I_CODE_ENVIRON_VAL_REF			113

#define IDIO_I_CODE_PUSH_TRAP				120
#define IDIO_I_CODE_POP_TRAP				121

#define IDIO_I_CODE_AND					130
#define IDIO_I_CODE_OR					131
#define IDIO_I_CODE_BEGIN				132

#define IDIO_I_CODE_EXPANDER				140
#define IDIO_I_CODE_INFIX_OPERATOR			141
#define IDIO_I_CODE_POSTFIX_OPERATOR			142

#define IDIO_I_CODE_FINISH				998
#define IDIO_I_CODE_NOP					999

/*
 * Idio Intermediate code: idio_I_*
 *
 * These are the coarse instructions for the VM.  The actual assmebly
 * code will specialize these.
 */
#define idio_I_SHALLOW_ARGUMENT_REF		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_SHALLOW_ARGUMENT_REF))
#define idio_I_DEEP_ARGUMENT_REF		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_DEEP_ARGUMENT_REF))

#define idio_I_SHALLOW_ARGUMENT_SET		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_SHALLOW_ARGUMENT_SET))
#define idio_I_DEEP_ARGUMENT_SET		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_DEEP_ARGUMENT_SET))

#define idio_I_GLOBAL_SYM_REF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_SYM_REF))
#define idio_I_CHECKED_GLOBAL_SYM_REF		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_CHECKED_GLOBAL_SYM_REF))
#define idio_I_GLOBAL_FUNCTION_SYM_REF		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_FUNCTION_SYM_REF))
#define idio_I_CHECKED_GLOBAL_FUNCTION_SYM_REF	((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_SYM_REF))
#define idio_I_CONSTANT_SYM_REF                 ((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_CONSTANT_SYM_REF))
#define idio_I_COMPUTED_SYM_REF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_COMPUTED_SYM_REF))

#define idio_I_GLOBAL_SYM_DEF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_SYM_DEF))
#define idio_I_GLOBAL_SYM_SET			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_SYM_SET))
#define idio_I_COMPUTED_SYM_SET			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_COMPUTED_SYM_SET))
#define idio_I_COMPUTED_SYM_DEFINE		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_COMPUTED_SYM_DEFINE))

#define idio_I_GLOBAL_VAL_REF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_VAL_REF))
#define idio_I_CHECKED_GLOBAL_VAL_REF		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_CHECKED_GLOBAL_VAL_REF))
#define idio_I_GLOBAL_FUNCTION_VAL_REF		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_FUNCTION_VAL_REF))
#define idio_I_CHECKED_GLOBAL_FUNCTION_VAL_REF	((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_VAL_REF))
#define idio_I_CONSTANT_VAL_REF                 ((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_CONSTANT_VAL_REF))
#define idio_I_COMPUTED_VAL_REF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_COMPUTED_VAL_REF))

#define idio_I_GLOBAL_VAL_DEF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_VAL_DEF))
#define idio_I_GLOBAL_VAL_SET			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_GLOBAL_VAL_SET))
#define idio_I_COMPUTED_VAL_SET			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_COMPUTED_VAL_SET))
#define idio_I_COMPUTED_VAL_DEFINE		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_COMPUTED_VAL_DEFINE))

#define idio_I_PREDEFINED			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PREDEFINED))
#define idio_I_ALTERNATIVE			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_ALTERNATIVE))
#define idio_I_SEQUENCE				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_SEQUENCE))
#define idio_I_TR_FIX_LET			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_TR_FIX_LET))
#define idio_I_FIX_LET				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_FIX_LET))

#define idio_I_PRIMCALL0			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PRIMCALL0))
#define idio_I_PRIMCALL1			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PRIMCALL1))
#define idio_I_PRIMCALL2			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PRIMCALL2))
#define idio_I_PRIMCALL3			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PRIMCALL3))
#define idio_I_TR_REGULAR_CALL			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_TR_REGULAR_CALL))
#define idio_I_REGULAR_CALL			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_REGULAR_CALL))

#define idio_I_FIX_CLOSURE			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_FIX_CLOSURE))
#define idio_I_NARY_CLOSURE			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_NARY_CLOSURE))

#define idio_I_STORE_ARGUMENT			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_STORE_ARGUMENT))
#define idio_I_CONS_ARGUMENT			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_CONS_ARGUMENT))
#define idio_I_ALLOCATE_FRAME			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_ALLOCATE_FRAME))
#define idio_I_ALLOCATE_DOTTED_FRAME		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_ALLOCATE_DOTTED_FRAME))

#define idio_I_PUSH_DYNAMIC			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PUSH_DYNAMIC))
#define idio_I_POP_DYNAMIC			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_POP_DYNAMIC))
#define idio_I_DYNAMIC_SYM_REF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_DYNAMIC_SYM_REF))
#define idio_I_DYNAMIC_FUNCTION_SYM_REF		((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_DYNAMIC_FUNCTION_SYM_REF))

#define idio_I_PUSH_ENVIRON			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PUSH_ENVIRON))
#define idio_I_POP_ENVIRON			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_POP_ENVIRON))
#define idio_I_ENVIRON_SYM_REF			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_ENVIRON_SYM_REF))

#define idio_I_PUSH_TRAP			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_PUSH_TRAP))
#define idio_I_POP_TRAP				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_POP_TRAP))

#define idio_I_AND				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_AND))
#define idio_I_OR				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_OR))
#define idio_I_BEGIN				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_BEGIN))

#define idio_I_EXPANDER				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_EXPANDER))
#define idio_I_INFIX_OPERATOR			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_INFIX_OPERATOR))
#define idio_I_POSTFIX_OPERATOR			((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_POSTFIX_OPERATOR))

#define idio_I_FINISH				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_FINISH))
#define idio_I_NOP				((const IDIO) IDIO_CONSTANT_I_CODE (IDIO_I_CODE_NOP))

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
/* mode: C */
/* coding: utf-8-unix */
/* End: */
