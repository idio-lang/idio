/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * vm-asm.h
 *
 */

#ifndef VM_ASM_H
#define VM_ASM_H

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

    IDIO_A_SYM_REF,
    IDIO_A_SYM_IREF,
    IDIO_A_FUNCTION_SYM_REF,
    IDIO_A_FUNCTION_SYM_IREF,
    IDIO_A_CONSTANT_REF,
    IDIO_A_CONSTANT_IREF,
    IDIO_A_COMPUTED_SYM_REF,
    IDIO_A_COMPUTED_SYM_IREF,

    IDIO_A_SYM_DEF,
    IDIO_A_SYM_IDEF,
    IDIO_A_SYM_SET,
    IDIO_A_SYM_ISET,
    IDIO_A_COMPUTED_SYM_SET,
    IDIO_A_COMPUTED_SYM_ISET,
    IDIO_A_COMPUTED_SYM_DEF,
    IDIO_A_COMPUTED_SYM_IDEF,

    IDIO_A_VAL_REF,
    IDIO_A_VAL_IREF,
    IDIO_A_FUNCTION_VAL_REF,
    IDIO_A_FUNCTION_VAL_IREF,

    IDIO_A_VAL_SET,
    IDIO_A_VAL_ISET,

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
    IDIO_A_CREATE_IFUNCTION,	/* top level closure */
    IDIO_A_CREATE_CLOSURE,
    IDIO_A_CREATE_ICLOSURE,
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
    IDIO_A_IEXPANDER,
    IDIO_A_INFIX_OPERATOR,
    IDIO_A_INFIX_IOPERATOR,
    IDIO_A_POSTFIX_OPERATOR,
    IDIO_A_POSTFIX_IOPERATOR,

    IDIO_A_PUSH_DYNAMIC,
    IDIO_A_PUSH_IDYNAMIC,
    IDIO_A_POP_DYNAMIC,
    IDIO_A_DYNAMIC_SYM_REF,
    IDIO_A_DYNAMIC_SYM_IREF,
    IDIO_A_DYNAMIC_FUNCTION_SYM_REF,
    IDIO_A_DYNAMIC_FUNCTION_SYM_IREF,

    IDIO_A_PUSH_ENVIRON,
    IDIO_A_PUSH_IENVIRON,
    IDIO_A_POP_ENVIRON,
    IDIO_A_ENVIRON_SYM_REF,
    IDIO_A_ENVIRON_SYM_IREF,

    IDIO_A_NON_CONT_ERR,
    IDIO_A_PUSH_TRAP,
    IDIO_A_PUSH_ITRAP,
    IDIO_A_POP_TRAP,

    IDIO_A_PUSH_ESCAPER,
    IDIO_A_PUSH_IESCAPER,
    IDIO_A_POP_ESCAPER,
    IDIO_A_ESCAPER_LABEL_REF,
    IDIO_A_ESCAPER_LABEL_IREF,
} idio_vm_a_enum;

/*
 * At the time of r0.1 the tests used around 19000 symbols, so
 * uint16_t.
 *
 * uint32_t adds a second or two to the test duration.  Maybe.
 *
 * The question comes up because adding more SRC_EXPR byte code
 * statements was producing 55k additional constants which bumped us
 * over the uint16_t limit.  SRC_EXPR constants can live in another
 * table.
 */
#define IDIO_IA_PUSH_REF(n)	IDIO_IA_PUSH_16UINT (n)
#define IDIO_VM_FETCH_REF(bc,t)	(idio_vm_fetch_16uint (bc,t))
#define IDIO_VM_GET_REF(bc,pcp)	(idio_vm_get_16uint (bc,pcp))

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
