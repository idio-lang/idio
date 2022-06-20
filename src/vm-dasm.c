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
 * vm-dasm.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "c-type.h"
#include "closure.h"
#include "codegen.h"
#include "command.h"
#include "condition.h"
#include "continuation.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "job-control.h"
#include "keyword.h"
#include "libc-wrap.h"
#include "module.h"
#include "object.h"
#include "pair.h"
#include "path.h"
#include "primitive.h"
#include "read.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm-asm.h"
#include "vm-dasm.h"
#include "vm.h"
#include "vtable.h"

/*
 * Code coverage:
 *
 * Used by the disassembler.
 *
 * Compile a debug build then run with --vm-reports
 */
char const *idio_vm_bytecode2string (int code)
{
    char *r;

    switch (code) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:    r = "A-SHALLOW-ARGUMENT-REF0";    break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:    r = "A-SHALLOW-ARGUMENT-REF1";    break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:    r = "A-SHALLOW-ARGUMENT-REF2";    break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:    r = "A-SHALLOW-ARGUMENT-REF3";    break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:     r = "A-SHALLOW-ARGUMENT-REF";     break;
    case IDIO_A_DEEP_ARGUMENT_REF:        r = "A-DEEP-ARGUMENT-REF";        break;

    case IDIO_A_SHALLOW_ARGUMENT_SET0:    r = "A-SHALLOW-ARGUMENT-SET0";    break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:    r = "A-SHALLOW-ARGUMENT-SET1";    break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:    r = "A-SHALLOW-ARGUMENT-SET2";    break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:    r = "A-SHALLOW-ARGUMENT-SET3";    break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:     r = "A-SHALLOW-ARGUMENT-SET";     break;
    case IDIO_A_DEEP_ARGUMENT_SET:        r = "A-DEEP-ARGUMENT-SET";        break;

    case IDIO_A_SYM_REF:                  r = "A-SYM-REF";                  break;
    case IDIO_A_FUNCTION_SYM_REF:         r = "A-FUNCTION-SYM-REF";         break;
    case IDIO_A_CONSTANT_REF:             r = "A-CONSTANT-REF";             break;
    case IDIO_A_COMPUTED_SYM_REF:         r = "A-COMPUTED-SYM-REF";         break;

    case IDIO_A_SYM_DEF:                  r = "A-SYM-DEF";                  break;
    case IDIO_A_SYM_SET:                  r = "A-SYM-SET";                  break;
    case IDIO_A_COMPUTED_SYM_SET:         r = "A-COMPUTED-SYM-SET";         break;
    case IDIO_A_COMPUTED_SYM_DEF:         r = "A-COMPUTED-SYM-DEF";         break;

    case IDIO_A_VAL_REF:                  r = "A-VAL-REF";                  break;
    case IDIO_A_FUNCTION_VAL_REF:         r = "A-FUNCTION-VAL-REF";         break;

    case IDIO_A_VAL_SET:                  r = "A-VAL-SET";                  break;

    case IDIO_A_PREDEFINED0:              r = "A-PREDEFINED0";              break;
    case IDIO_A_PREDEFINED1:              r = "A-PREDEFINED1";              break;
    case IDIO_A_PREDEFINED2:              r = "A-PREDEFINED2";              break;
    case IDIO_A_PREDEFINED3:              r = "A-PREDEFINED3";              break;
    case IDIO_A_PREDEFINED4:              r = "A-PREDEFINED4";              break;
    case IDIO_A_PREDEFINED5:              r = "A-PREDEFINED5";              break;
    case IDIO_A_PREDEFINED6:              r = "A-PREDEFINED6";              break;
    case IDIO_A_PREDEFINED7:              r = "A-PREDEFINED7";              break;
    case IDIO_A_PREDEFINED8:              r = "A-PREDEFINED8";              break;
    case IDIO_A_PREDEFINED:               r = "A-PREDEFINED";               break;

    case IDIO_A_LONG_GOTO:                r = "A-LONG-GOTO";                break;
    case IDIO_A_LONG_JUMP_FALSE:          r = "A-LONG-JUMP-FALSE";          break;
    case IDIO_A_LONG_JUMP_TRUE:           r = "A-LONG-JUMP-TRUE";           break;
    case IDIO_A_SHORT_GOTO:               r = "A-SHORT-GOTO";               break;
    case IDIO_A_SHORT_JUMP_FALSE:         r = "A-SHORT-JUMP-FALSE";         break;
    case IDIO_A_SHORT_JUMP_TRUE:          r = "A-SHORT-JUMP-TRUE";          break;

    case IDIO_A_PUSH_VALUE:               r = "A-PUSH-VALUE";               break;
    case IDIO_A_POP_VALUE:                r = "A-POP-VALUE";                break;
    case IDIO_A_POP_REG1:                 r = "A-POP-REG1";                 break;
    case IDIO_A_POP_REG2:                 r = "A-POP-REG2";                 break;
    case IDIO_A_SRC_EXPR:                 r = "A-SRC-EXPR";                 break;
    case IDIO_A_POP_FUNCTION:             r = "A-POP-FUNCTION";             break;
    case IDIO_A_PRESERVE_STATE:           r = "A-PRESERVE-STATE";           break;
    case IDIO_A_RESTORE_STATE:            r = "A-RESTORE-STATE";            break;
    case IDIO_A_RESTORE_ALL_STATE:        r = "A-RESTORE-ALL-STATE";        break;

    case IDIO_A_CREATE_FUNCTION:          r = "A-CREATE-FUNCTION";          break;
    case IDIO_A_CREATE_CLOSURE:           r = "A-CREATE-CLOSURE";           break;
    case IDIO_A_FUNCTION_INVOKE:          r = "A-FUNCTION-INVOKE";          break;
    case IDIO_A_FUNCTION_GOTO:            r = "A-FUNCTION-GOTO";            break;
    case IDIO_A_RETURN:                   r = "A-RETURN";                   break;
    case IDIO_A_FINISH:                   r = "A-FINISH";                   break;
    case IDIO_A_PUSH_ABORT:               r = "A-PUSH-ABORT";               break;
    case IDIO_A_POP_ABORT:                r = "A-POP-ABORT";                break;

    case IDIO_A_ALLOCATE_FRAME1:          r = "A-ALLOCATE-FRAME1";          break;
    case IDIO_A_ALLOCATE_FRAME2:          r = "A-ALLOCATE-FRAME2";          break;
    case IDIO_A_ALLOCATE_FRAME3:          r = "A-ALLOCATE-FRAME3";          break;
    case IDIO_A_ALLOCATE_FRAME4:          r = "A-ALLOCATE-FRAME4";          break;
    case IDIO_A_ALLOCATE_FRAME5:          r = "A-ALLOCATE-FRAME5";          break;
    case IDIO_A_ALLOCATE_FRAME:           r = "A-ALLOCATE-FRAME";           break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:    r = "A-ALLOCATE-DOTTED-FRAME";    break;
    case IDIO_A_REUSE_FRAME:              r = "A-REUSE-FRAME";              break;

    case IDIO_A_POP_FRAME0:               r = "A-POP-FRAME0";               break;
    case IDIO_A_POP_FRAME1:               r = "A-POP-FRAME1";               break;
    case IDIO_A_POP_FRAME2:               r = "A-POP-FRAME2";               break;
    case IDIO_A_POP_FRAME3:               r = "A-POP-FRAME3";               break;
    case IDIO_A_POP_FRAME:                r = "A-POP-FRAME";                break;

    case IDIO_A_LINK_FRAME:               r = "A-LINK-FRAME";               break;
    case IDIO_A_UNLINK_FRAME:             r = "A-UNLINK-FRAME";             break;
    case IDIO_A_PACK_FRAME:               r = "A-PACK-FRAME";               break;
    case IDIO_A_POP_LIST_FRAME:           r = "A-POP-LIST-FRAME";           break;
    case IDIO_A_EXTEND_FRAME:             r = "A-EXTEND-FRAME";             break;

    case IDIO_A_ARITY1P:                  r = "A-ARITY1P";                  break;
    case IDIO_A_ARITY2P:                  r = "A-ARITY2P";                  break;
    case IDIO_A_ARITY3P:                  r = "A-ARITY3P";                  break;
    case IDIO_A_ARITY4P:                  r = "A-ARITY4P";                  break;
    case IDIO_A_ARITYEQP:                 r = "A-ARITYEQP";                 break;
    case IDIO_A_ARITYGEP:                 r = "A-ARITYGEP";                 break;

    case IDIO_A_SHORT_NUMBER:             r = "A-SHORT-NUMBER";             break;
    case IDIO_A_SHORT_NEG_NUMBER:         r = "A-SHORT-NEG-NUMBER";         break;
    case IDIO_A_CONSTANT_0:               r = "A-CONSTANT-0";               break;
    case IDIO_A_CONSTANT_1:               r = "A-CONSTANT-1";               break;
    case IDIO_A_CONSTANT_2:               r = "A-CONSTANT-2";               break;
    case IDIO_A_CONSTANT_3:               r = "A-CONSTANT-3";               break;
    case IDIO_A_CONSTANT_4:               r = "A-CONSTANT-4";               break;
    case IDIO_A_FIXNUM:                   r = "A-FIXNUM";                   break;
    case IDIO_A_NEG_FIXNUM:               r = "A-NEG-FIXNUM";               break;
    case IDIO_A_CONSTANT:                 r = "A-CONSTANT";                 break;
    case IDIO_A_NEG_CONSTANT:             r = "A-NEG-CONSTANT";             break;
    case IDIO_A_UNICODE:                  r = "A-UNICODE";                  break;

    case IDIO_A_NOP:                      r = "A-NOP";                      break;
    case IDIO_A_PRIMCALL0:                r = "A-PRIMCALL0";                break;
    case IDIO_A_PRIMCALL1:                r = "A-PRIMCALL1";                break;
    case IDIO_A_PRIMCALL2:                r = "A-PRIMCALL2";                break;

    case IDIO_A_PRIMCALL3:                r = "A-PRIMCALL3";                break;
    case IDIO_A_PRIMCALL:                 r = "A-PRIMCALL";                 break;

    case IDIO_A_SUPPRESS_RCSE:            r = "A-SUPPRESS-RCSE";            break;
    case IDIO_A_POP_RCSE:                 r = "A-POP-RCSE";                 break;

    case IDIO_A_NOT:                      r = "A-NOT";                      break;

    case IDIO_A_EXPANDER:                 r = "A-EXPANDER";                 break;
    case IDIO_A_INFIX_OPERATOR:           r = "A-INFIX-OPERATOR";           break;
    case IDIO_A_POSTFIX_OPERATOR:         r = "A-POSTFIX-OPERATOR";         break;

    case IDIO_A_PUSH_DYNAMIC:             r = "A-PUSH-DYNAMIC";             break;
    case IDIO_A_POP_DYNAMIC:              r = "A-POP-DYNAMIC";              break;
    case IDIO_A_DYNAMIC_SYM_REF:          r = "A-DYNAMIC-SYM-REF";          break;
    case IDIO_A_DYNAMIC_FUNCTION_SYM_REF: r = "A-DYNAMIC-FUNCTION-SYM-REF"; break;

    case IDIO_A_PUSH_ENVIRON:             r = "A-PUSH-ENVIRON";             break;
    case IDIO_A_POP_ENVIRON:              r = "A-POP-ENVIRON";              break;
    case IDIO_A_ENVIRON_SYM_REF:          r = "A-ENVIRON-SYM-REF";          break;

    case IDIO_A_NON_CONT_ERR:             r = "A-NON-CONT-ERR";             break;
    case IDIO_A_PUSH_TRAP:                r = "A-PUSH-TRAP";                break;
    case IDIO_A_POP_TRAP:                 r = "A-POP-TRAP";                 break;

    case IDIO_A_PUSH_ESCAPER:             r = "A-PUSH-ESCAPER";             break;
    case IDIO_A_POP_ESCAPER:              r = "A-POP-ESCAPER";              break;

    default:
	/* fprintf (stderr, "idio_vm_bytecode2string: unexpected bytecode %d\n", code); */
	r = "Unknown bytecode";
	break;
    }

    return r;
}

#define IDIO_VM_DASM(...)	{ fprintf (fp, __VA_ARGS__); }

IDIO idio_vm_dasm_symbols_ref (size_t xi, idio_as_t si)
{
    IDIO st = IDIO_XENV_SYMBOLS (idio_xenvs[xi]);
    IDIO cs = IDIO_XENV_CONSTANTS (idio_xenvs[xi]);

    if (xi) {
	if (si >= idio_array_size (st)) {
	    fprintf (stderr, "si %zu > %zu\n", si, idio_array_size (st));
	    idio_debug ("st %s\n", st);
	    return idio_S_undef;
	    assert (0);
	}
	IDIO fci = idio_array_ref_index (st, si);

	return idio_array_ref_index (cs, IDIO_FIXNUM_VAL (fci));
    } else {
	return idio_array_ref_index (cs, si);
    }
}

IDIO idio_vm_dasm_constants_ref (size_t xi, idio_as_t ci)
{
    IDIO cs = IDIO_XENV_CONSTANTS (idio_xenvs[xi]);

    return idio_array_ref_index (cs, ci);
}

void idio_vm_dasm (FILE *fp, size_t xi, idio_pc_t pc0, idio_pc_t pce)
{
    IDIO_C_ASSERT (fp);

    IDIO_IA_T bc = IDIO_XENV_BYTE_CODE (idio_xenvs[xi]);

    fprintf (fp, "byte code for xenv[%zu]: %zd instruction bytes\n", xi, IDIO_IA_USIZE (bc));

    IDIO thr = idio_thread_current_thread ();
    IDIO ce = idio_thread_current_env ();

    if (0 == pce) {
	pce = IDIO_IA_USIZE (bc);
    }

    if (pc0 > pce) {
	fprintf (stderr, "\n\nPC %zd > max code PC %zd\n", pc0, pce);
	idio_vm_panic (thr, "vm-dasm: bad PC!");
    }

    IDIO hints = IDIO_HASH_EQP (256);

    idio_pc_t pc = pc0;
    idio_pc_t *pcp = &pc;

    for (; pc < pce;) {
	IDIO hint = idio_hash_ref (hints, idio_fixnum (pc));
	if (idio_S_unspec != hint) {
	    size_t size = 0;
	    char *hint_C = idio_as_string_safe (hint, &size, 40, 1);
	    IDIO_VM_DASM ("%-20s ", hint_C);
	    IDIO_GC_FREE (hint_C, size);
	} else {
	    IDIO_VM_DASM ("%20s ", "");
	}

	IDIO_VM_DASM ("%6zd ", pc);

	IDIO_I ins = IDIO_IA_GET_NEXT (bc, pcp);

	IDIO_VM_DASM ("%3d: ", ins);

	switch (ins) {
	case IDIO_A_SHALLOW_ARGUMENT_REF0:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 0");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF1:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 1");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF2:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 2");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF3:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF 3");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_REF:
	    {
		uint64_t j = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF %" PRId64 "", j);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_REF:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		uint64_t j = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("DEEP-ARGUMENT-REF %" PRId64 " %" PRId64 "", i, j);
	    }
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET0:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 0");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET1:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 1");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET2:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 2");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET3:
	    IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET 3");
	    break;
	case IDIO_A_SHALLOW_ARGUMENT_SET:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET %" PRId64 "", i);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_SET:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		uint64_t j = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("DEEP-ARGUMENT-SET %" PRId64 " %" PRId64 "", i, j);
	    }
	    break;
	case IDIO_A_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_SYM_IREF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM ("SYM-IREF %" PRId64 "", si);
		idio_debug_FILE (fp, " %s", sym);
	    }
	    break;
	case IDIO_A_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("FUNCTION-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_FUNCTION_SYM_IREF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM ("FUNCTION-SYM-IREF %" PRId64 "", si);
		idio_debug_FILE (fp, " %s", sym);
	    }
	    break;
	case IDIO_A_CONSTANT_REF:
	    {
		idio_ai_t mci = idio_vm_get_varuint (bc, pcp);

		IDIO fmci = idio_fixnum (mci);
		IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO c = idio_vm_dasm_constants_ref (xi, gci);

		IDIO_VM_DASM ("CONSTANT-REF %5zd", mci);
		idio_debug_FILE (fp, " %s", c);
	    }
	    break;
	case IDIO_A_CONSTANT_IREF:
	    {
		idio_ai_t mci = idio_vm_get_varuint (bc, pcp);

		IDIO fmci = idio_fixnum (mci);
		IDIO fgci = idio_module_get_or_set_vci (ce, fmci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO c = idio_vm_dasm_constants_ref (xi, gci);

		IDIO_VM_DASM ("CONSTANT-IREF %5zd", mci);
		idio_debug_FILE (fp, " %s", c);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-REF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_IREF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-IREF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_SYM_DEF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t mkci = idio_vm_get_varuint (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO fgkci = idio_module_get_or_set_vci (ce, idio_fixnum (mkci));

		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));
		IDIO kind = idio_vm_dasm_constants_ref (xi, IDIO_FIXNUM_VAL (fgkci));

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("SYM-DEF %" PRId64 " %s as %s", mci, IDIO_SYMBOL_S (sym), IDIO_SYMBOL_S (kind));
		} else {
		    IDIO_VM_DASM ("SYM-DEF %" PRId64 " ?? %s", mci, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_SYM_IDEF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);
		uint64_t kci = idio_vm_get_varuint (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);
		IDIO kind = idio_vm_dasm_constants_ref (xi, kci);

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("SYM-IDEF %" PRId64 " %s as %s", si, IDIO_SYMBOL_S (sym), IDIO_SYMBOL_S (kind));
		} else {
		    IDIO_VM_DASM ("SYM-IDEF %" PRId64 " ?? %s", si, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_SYM_SET:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("SYM-SET %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
		} else {
		    IDIO_VM_DASM ("SYM-SET %" PRId64 " ?? %s", mci, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_SYM_ISET:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("SYM-ISET %" PRId64 " %s", si, IDIO_SYMBOL_S (sym));
		} else {
		    IDIO_VM_DASM ("SYM-ISET %" PRId64 " ?? %s", si, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_SET:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-SET %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_ISET:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-ISET %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_DEF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-DEF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_IDEF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("COMPUTED-SYM-IDEF %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_VAL_IREF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("VAL-IREF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_FUNCTION_VAL_REF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("FUNCTION-VAL-REF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_FUNCTION_VAL_IREF:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("FUNCTION-VAL-IREF %" PRId64 "", gvi);
	    }
	    break;
	case IDIO_A_VAL_SET:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("VAL-SET %" PRId64, gvi);
	    }
	    break;
	case IDIO_A_VAL_ISET:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("VAL-ISET %" PRId64, gvi);
	    }
	    break;
	case IDIO_A_PREDEFINED0:
	    {
		IDIO_VM_DASM ("PREDEFINED 0 #t");
	    }
	    break;
	case IDIO_A_PREDEFINED1:
	    {
		IDIO_VM_DASM ("PREDEFINED 1 #f");
	    }
	    break;
	case IDIO_A_PREDEFINED2:
	    {
		IDIO_VM_DASM ("PREDEFINED 2 #nil");
	    }
	    break;
	case IDIO_A_PREDEFINED:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		if (xi) {
		    IDIO_VM_DASM ("PREDEFINED %" PRId64, vi);
		} else {
		    IDIO pd = idio_vm_values_ref (xi, vi);
		    if (idio_isa_primitive (pd)) {
			IDIO_VM_DASM ("PREDEFINED %" PRId64 " PRIM %s", vi, IDIO_PRIMITIVE_NAME (pd));
		    } else {
			IDIO_VM_DASM ("PREDEFINED %" PRId64 " %s", vi, idio_type2string (pd));
		    }
		}
	    }
	    break;
	case IDIO_A_LONG_GOTO:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LG@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("LONG-GOTO +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_LONG_JUMP_FALSE:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LJF@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("LONG-JUMP-FALSE +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_LONG_JUMP_TRUE:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LJT@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("LONG-JUMP-TRUE +%" PRId64 " %" PRId64 "", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_GOTO:
	    {
		int i = IDIO_IA_GET_NEXT (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SG@%zd", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("SHORT-GOTO +%d %zd", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_FALSE:
	    {
		int i = IDIO_IA_GET_NEXT (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SJF@%zd", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("SHORT-JUMP-FALSE +%d %zd", i, pc + i);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_TRUE:
	    {
		int i = IDIO_IA_GET_NEXT (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SJT@%zd", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("SHORT-JUMP-TRUE %d %zd", i, pc + i);
	    }
	    break;
	case IDIO_A_PUSH_VALUE:
	    {
		IDIO_VM_DASM ("PUSH-VALUE");
	    }
	    break;
	case IDIO_A_POP_VALUE:
	    {
		IDIO_VM_DASM ("POP-VALUE");
	    }
	    break;
	case IDIO_A_POP_REG1:
	    {
		IDIO_VM_DASM ("POP-REG1");
	    }
	    break;
	case IDIO_A_POP_REG2:
	    {
		IDIO_VM_DASM ("POP-REG2");
	    }
	    break;
	case IDIO_A_SRC_EXPR:
	    {
		idio_ai_t sci = idio_vm_get_varuint (bc, pcp);

		IDIO fsci = idio_fixnum (sci);
		IDIO fgci = idio_module_get_or_set_vci (ce, fsci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO_VM_DASM ("SRC-EXPR %zd", sci);
		IDIO e = idio_vm_src_expr_ref (xi, gci);
		IDIO lo = idio_vm_src_props_ref (xi, gci);

		if (xi) {
		    if (idio_isa_pair (lo)) {
			idio_debug_FILE (fp, " %s", IDIO_PAIR_H (lo));
			idio_debug_FILE (fp, ":line %s", IDIO_PAIR_HT (lo));
		    } else {
			IDIO_VM_DASM (" %-25s", "<no lex tuple>");
		    }
		} else {
		    if (idio_S_unspec == lo) {
			IDIO_VM_DASM (" %-25s", "<no lexobj>");
		    } else {
			idio_debug_FILE (fp, " %s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_NAME));
			idio_debug_FILE (fp, ":line %s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_LINE));
		    }
		}
		idio_debug_FILE (fp, "\n  %s", e);
	    }
	    break;
	case IDIO_A_POP_FUNCTION:
	    {
		IDIO_VM_DASM ("POP-FUNCTION");
	    }
	    break;
	case IDIO_A_PRESERVE_STATE:
	    {
		IDIO_VM_DASM ("PRESERVE-STATE");
	    }
	    break;
	case IDIO_A_RESTORE_STATE:
	    {
		IDIO_VM_DASM ("RESTORE-STATE");
	    }
	    break;
	case IDIO_A_RESTORE_ALL_STATE:
	    {
		IDIO_VM_DASM ("RESTORE-ALL-STATE");
	    }
	    break;
	case IDIO_A_CREATE_FUNCTION:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		/* uint64_t code_len = */ idio_vm_get_varuint (bc, pcp);
		uint64_t nci  = idio_vm_get_varuint (bc, pcp);
		uint64_t ssci = idio_vm_get_varuint (bc, pcp);
		uint64_t dsci = idio_vm_get_varuint (bc, pcp);
		uint64_t slci = idio_vm_get_varuint (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "CF@%" PRId64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("CREATE-FUNCTION @ +%" PRId64 " %" PRId64 "", i, pc + i);

		/* name lookup */
		IDIO fnci = idio_fixnum (nci);
		IDIO fgnci = idio_module_get_or_set_vci (ce, fnci);
		IDIO name = idio_S_nil;
		if (idio_S_unspec != fgnci) {
		    name = idio_vm_dasm_constants_ref (xi, IDIO_FIXNUM_VAL (fgnci));
		} else {
		    fprintf (stderr, "vm cc name: failed to find %" PRIu64 "\n", nci);
		}
		size_t size = 0;
		char *ids = idio_display_string (name, &size);
		IDIO_VM_DASM ("\n  name %s", ids);
		IDIO_GC_FREE (ids, size);

		/* sigstr lookup */
		IDIO fssci = idio_fixnum (ssci);
		IDIO fgssci = idio_module_get_or_set_vci (ce, fssci);
		IDIO ss = idio_S_nil;
		if (idio_S_unspec != fgssci) {
		    ss = idio_vm_dasm_constants_ref (xi, IDIO_FIXNUM_VAL (fgssci));
		} else {
		    fprintf (stderr, "vm cc sig: failed to find %" PRIu64 "\n", ssci);
		}
		size = 0;
		ids = idio_display_string (ss, &size);
		IDIO_VM_DASM ("\n  sigstr %s", ids);
		IDIO_GC_FREE (ids, size);

		/* docstr lookup */
		IDIO fdsci = idio_fixnum (dsci);
		IDIO fgdsci = idio_module_get_or_set_vci (ce, fdsci);
		IDIO ds = idio_S_nil;
		if (idio_S_unspec != fgdsci) {
		    ds = idio_vm_dasm_constants_ref (xi, IDIO_FIXNUM_VAL (fgdsci));
		} else {
		    fprintf (stderr, "vm cc doc: failed to find %" PRIu64 "\n", dsci);
		}
		if (idio_S_nil != ds) {
		    size = 0;
		    ids = idio_as_string_safe (ds, &size, 1, 1);
		    IDIO_VM_DASM ("\n  docstr %s", ids);
		    IDIO_GC_FREE (ids, size);
		}

		/* srcloc lookup */
		IDIO fslci = idio_fixnum (slci);
		IDIO fgslci = idio_module_get_or_set_vci (ce, fslci);
		IDIO sl = idio_S_nil;
		if (idio_S_unspec != fgslci) {
		    sl = idio_vm_dasm_constants_ref (xi, IDIO_FIXNUM_VAL (fgslci));
		} else {
		    fprintf (stderr, "vm cc doc: failed to find %" PRIu64 "\n", slci);
		}
		if (idio_S_nil != sl) {
		    size = 0;
		    ids = idio_as_string_safe (sl, &size, 1, 1);
		    IDIO_VM_DASM ("\n  srcloc %s", ids);
		    IDIO_GC_FREE (ids, size);
		}
	    }
	    break;
	case IDIO_A_CREATE_CLOSURE:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("CREATE-CLOSURE %" PRId64, gvi);
	    }
	    break;
	case IDIO_A_CREATE_ICLOSURE:
	    {
		uint64_t gvi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("CREATE-ICLOSURE %" PRId64, gvi);
	    }
	    break;
	case IDIO_A_FUNCTION_INVOKE:
	    {
		IDIO_VM_DASM ("FUNCTION-INVOKE ... ");
	    }
	    break;
	case IDIO_A_FUNCTION_GOTO:
	    {
		IDIO_VM_DASM ("FUNCTION-GOTO ...");
	    }
	    break;
	case IDIO_A_RETURN:
	    {
		IDIO_VM_DASM ("RETURN\n");
	    }
	    break;
	case IDIO_A_PUSH_ABORT:
	    {
		uint64_t o = idio_vm_get_varuint (bc, pcp);
		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "A@%" PRId64 "", pc + o);
		idio_hash_put (hints, idio_fixnum (pc + o), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("PUSH-ABORT to PC +%" PRIu64 " A@%" PRId64, o + 1, pc + o);
	    }
	    break;
	case IDIO_A_POP_ABORT:
	    {
		IDIO_VM_DASM ("POP-ABORT");
	    }
	    break;
	case IDIO_A_FINISH:
	    {
		IDIO_VM_DASM ("FINISH");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME1:
	    {
		/* no args, no need to pull an empty list ref */
		IDIO_VM_DASM ("ALLOCATE-FRAME 1");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME2:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 2");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME3:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 3");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME4:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 4");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME5:
	    {
		IDIO_VM_DASM ("ALLOCATE-FRAME 5");
	    }
	    break;
	case IDIO_A_ALLOCATE_FRAME:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ALLOCATE-FRAME %" PRId64, i);
	    }
	    break;
	case IDIO_A_ALLOCATE_DOTTED_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ALLOCATE-DOTTED-FRAME %" PRId64, arity);
	    }
	    break;
	case IDIO_A_REUSE_FRAME:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("REUSE-FRAME %" PRId64, i);
	    }
	    break;
	case IDIO_A_POP_FRAME0:
	    {
		IDIO_VM_DASM ("POP-FRAME 0");
	    }
	    break;
	case IDIO_A_POP_FRAME1:
	    {
		IDIO_VM_DASM ("POP-FRAME 1");
	    }
	    break;
	case IDIO_A_POP_FRAME2:
	    {
		IDIO_VM_DASM ("POP-FRAME 2");
	    }
	    break;
	case IDIO_A_POP_FRAME3:
	    {
		IDIO_VM_DASM ("POP-FRAME 3");
	    }
	    break;
	case IDIO_A_POP_FRAME:
	    {
		uint64_t rank = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("POP-FRAME %" PRId64 "", rank);
	    }
	    break;
	case IDIO_A_LINK_FRAME:
	    {
		uint64_t ssci = idio_vm_get_varuint (bc, pcp);
		IDIO names = idio_vm_dasm_constants_ref (xi, ssci);
		IDIO_VM_DASM ("LINK-FRAME sci=%" PRId64, ssci);
		idio_debug_FILE (fp, " %s", names);
	    }
	    break;
	case IDIO_A_UNLINK_FRAME:
	    {
		IDIO_VM_DASM ("UNLINK-FRAME");
	    }
	    break;
	case IDIO_A_PACK_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("PACK-FRAME %" PRId64 "", arity);
	    }
	    break;
	case IDIO_A_POP_LIST_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("POP-LIST-FRAME %" PRId64 "", arity);
	    }
	    break;
	case IDIO_A_EXTEND_FRAME:
	    {
		uint64_t alloc = idio_vm_get_varuint (bc, pcp);
		uint64_t ssci = idio_vm_get_varuint (bc, pcp);
		IDIO names = idio_vm_dasm_constants_ref (xi, ssci);

		IDIO_VM_DASM ("EXTEND-FRAME %" PRId64 " sci=%" PRId64, alloc, ssci);
		idio_debug_FILE (fp, " %s", names);
	    }
	    break;
	case IDIO_A_ARITY1P:
	    {
		IDIO_VM_DASM ("ARITY=1?");
	    }
	    break;
	case IDIO_A_ARITY2P:
	    {
		IDIO_VM_DASM ("ARITY=2?");
	    }
	    break;
	case IDIO_A_ARITY3P:
	    {
		IDIO_VM_DASM ("ARITY=3?");
	    }
	    break;
	case IDIO_A_ARITY4P:
	    {
		IDIO_VM_DASM ("ARITY=4?");
	    }
	    break;
	case IDIO_A_ARITYEQP:
	    {
		uint64_t arityp1 = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ARITY=? %" PRId64 "", arityp1);
	    }
	    break;
	case IDIO_A_ARITYGEP:
	    {
		uint64_t arityp1 = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("ARITY>=? %" PRId64 "", arityp1);
	    }
	    break;
	case IDIO_A_CONSTANT_0:
	    {
		IDIO_VM_DASM ("CONSTANT       0");
	    }
	    break;
	case IDIO_A_CONSTANT_1:
	    {
		IDIO_VM_DASM ("CONSTANT       1");
	    }
	    break;
	case IDIO_A_CONSTANT_2:
	    {
		IDIO_VM_DASM ("CONSTANT       2");
	    }
	    break;
	case IDIO_A_CONSTANT_3:
	    {
		IDIO_VM_DASM ("CONSTANT       3");
	    }
	    break;
	case IDIO_A_CONSTANT_4:
	    {
		IDIO_VM_DASM ("CONSTANT       4");
	    }
	    break;
	case IDIO_A_FIXNUM:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("FIXNUM %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_NEG_FIXNUM:
	    {
		int64_t v = idio_vm_get_varuint (bc, pcp);
		v = -v;
		IDIO_VM_DASM ("NEG-FIXNUM %" PRId64 "", v);
	    }
	    break;
	case IDIO_A_CONSTANT:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("CONSTANT     %5" PRIu64 "", v);
		size_t size = 0;
		char *ids = idio_display_string (IDIO_CONSTANT_IDIO ((intptr_t) v), &size);
		IDIO_VM_DASM (" %s", ids);
		IDIO_GC_FREE (ids, size);
	    }
	    break;
	case IDIO_A_NEG_CONSTANT:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		v = -v;
		IDIO_VM_DASM ("NEG-CONSTANT   %6" PRId64 "", v);
		size_t size = 0;
		char *ids = idio_display_string (IDIO_CONSTANT_IDIO ((intptr_t) v), &size);
		IDIO_VM_DASM (" %s", ids);
		IDIO_GC_FREE (ids, size);
	    }
	    break;
	case IDIO_A_UNICODE:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);
		IDIO_VM_DASM ("UNICODE #U+%04" PRIX64 "", v);
	    }
	    break;
	case IDIO_A_NOP:
	    {
		IDIO_VM_DASM ("NOP");
	    }
	    break;
	case IDIO_A_PRIMCALL0:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		if (xi) {
		    IDIO_VM_DASM ("PRIMITIVE/0 %" PRId64, vi);
		} else {
		    IDIO primdata = idio_vm_values_ref (xi, vi);
		    IDIO_VM_DASM ("PRIMITIVE/0 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
		}
	    }
	    break;
	case IDIO_A_PRIMCALL1:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		if (xi) {
		    IDIO_VM_DASM ("PRIMITIVE/1 %" PRId64, vi);
		} else {
		    IDIO primdata = idio_vm_values_ref (xi, vi);
		    IDIO_VM_DASM ("PRIMITIVE/1 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
		}
	    }
	    break;
	case IDIO_A_PRIMCALL2:
	    {
		uint64_t vi = idio_vm_get_varuint (bc, pcp);
		if (xi) {
		    IDIO_VM_DASM ("PRIMITIVE/2 %" PRId64, vi);
		} else {
		    IDIO primdata = idio_vm_values_ref (xi, vi);
		    IDIO_VM_DASM ("PRIMITIVE/2 %" PRId64 " %s", vi, IDIO_PRIMITIVE_NAME (primdata));
		}
	    }
	    break;
	case IDIO_A_SUPPRESS_RCSE:
	    IDIO_VM_DASM ("SUPPRESS-RCSE");
	    break;
	case IDIO_A_POP_RCSE:
	    IDIO_VM_DASM ("POP-RCSE");
	    break;
	case IDIO_A_NOT:
	    IDIO_VM_DASM ("NOT");
	    break;
	case IDIO_A_EXPANDER:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("EXPANDER %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_IEXPANDER:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("IEXPANDER %" PRId64 "", mci);
	    }
	    break;
	case IDIO_A_INFIX_OPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("INFIX-OPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_INFIX_IOPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("INFIX-IOPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_POSTFIX_OPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("POSTFIX-OPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_POSTFIX_IOPERATOR:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("POSTFIX-IOPERATOR %" PRId64 " %" PRId64 "", mci, pri);
	    }
	    break;
	case IDIO_A_PUSH_DYNAMIC:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("PUSH-DYNAMIC %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_PUSH_IDYNAMIC:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("PUSH-IDYNAMIC %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_POP_DYNAMIC:
	    {
		IDIO_VM_DASM ("POP-DYNAMIC");
	    }
	    break;
	case IDIO_A_DYNAMIC_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("DYNAMIC-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
		} else {
		    IDIO_VM_DASM ("DYNAMIC-SYM-REF %" PRId64 " ?? %s", mci, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_DYNAMIC_SYM_IREF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM ("DYNAMIC-SYM-IREF %" PRId64 " %s", si, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("DYNAMIC-FUNCTION-SYM-REF %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_DYNAMIC_FUNCTION_SYM_IREF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM ("DYNAMIC-FUNCTION-SYM-IREF %" PRId64 " %s", si, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_PUSH_ENVIRON:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("PUSH-ENVIRON %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_PUSH_IENVIRON:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("PUSH-IENVIRON %" PRId64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_POP_ENVIRON:
	    {
		IDIO_VM_DASM ("POP-ENVIRON");
	    }
	    break;
	case IDIO_A_ENVIRON_SYM_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO fgci = idio_module_get_or_set_vci (ce, idio_fixnum (mci));
		IDIO sym = idio_vm_dasm_symbols_ref (xi, IDIO_FIXNUM_VAL (fgci));

		IDIO_VM_DASM ("ENVIRON-SYM-REF %" PRIu64 " %s", mci, IDIO_SYMBOL_S (sym));
	    }
	    break;
	case IDIO_A_ENVIRON_SYM_IREF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM ("ENVIRON-SYM-IREF %" PRIu64 " %s", si, IDIO_SYMBOL_S (sym));
		} else {
		    IDIO_VM_DASM ("ENVIRON-SYM-IREF %" PRIu64 " ?? %s", si, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s", sym);
		}
	    }
	    break;
	case IDIO_A_NON_CONT_ERR:
	    {
		IDIO_VM_DASM ("NON-CONT-ERROR");
	    }
	    break;
	case IDIO_A_PUSH_TRAP:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("PUSH-TRAP %" PRIu64, mci);
	    }
	    break;
	case IDIO_A_PUSH_ITRAP:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("PUSH-ITRAP %" PRIu64, mci);
	    }
	    break;
	case IDIO_A_POP_TRAP:
	    {
		IDIO_VM_DASM ("POP-TRAP");
	    }
	    break;
	case IDIO_A_PUSH_ESCAPER:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t offset = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("PUSH-ESCAPER %" PRIu64 " -> %" PRIu64, mci, pc + offset + 1);
	    }
	    break;
	case IDIO_A_PUSH_IESCAPER:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t offset = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("PUSH-IESCAPER %" PRIu64 " -> %" PRIu64, mci, pc + offset + 1);
	    }
	    break;
	case IDIO_A_POP_ESCAPER:
	    {
		IDIO_VM_DASM ("POP-ESCAPER");
	    }
	    break;
	case IDIO_A_ESCAPER_LABEL_REF:
	    {
		uint64_t mci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM ("ESCAPER-LABEL-REF %" PRIu64, mci);
	    }
	    break;
	default:
	    {
		/*
		 * Test Case: ??
		 *
		 * Coding error.  Also not in sync with idio_vm_run1()!
		 */
		idio_pc_t pci = pc;
		if (pci) {
		    pci--;
		}
		idio_pc_t pcm = pc + 10;
		if (pc >= 10) {
		    pc = pc - 10;
		} else {
		    pc = 0;
		}
		fprintf (stderr, "idio-vm-dasm: unexpected ins %3d @%zd\n", ins, pci);
		fprintf (stderr, "dumping from %zd to %zd\n", pc, pcm - 1);
		if (pc % 10) {
		    idio_pc_t pc1 = pc - (pc % 10);
		    fprintf (stderr, "\n  %5zd ", pc1);
		    for (; pc1 < pc; pc1++) {
			fprintf (stderr, "    ");
		    }
		}
		for (; pc < pcm; pc++) {
		    if (0 == (pc % 10)) {
			fprintf (stderr, "\n  %5zd ", pc);
		    }
		    fprintf (stderr, "%3d ", IDIO_IA_AE (bc, pc));
		}
		fprintf (stderr, "\n");
		IDIO_VM_DASM ("-- ?? --\n");
		continue;
		return;
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected instruction: %3d @%" PRId64 "\n", ins, pci);

		/* notreached */
		return;
	    }
	    break;
	}

	IDIO_VM_DASM ("\n");
    }
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%%idio-dasm", dasm, (IDIO args), "[c]", "\
generate the disassembler code for closure `c` or everything	\n\
								\n\
:param c: (optional) the closure to disassemble			\n\
:type c: closure						\n\
								\n\
The output goes to the file(s) :file:`idio-vm-dasm.{n}` in the	\n\
current directory.  These may get overwritten when Idio stops.	\n\
")
{
    IDIO_ASSERT (args);

    idio_pc_t pc0 = 0;
    idio_pc_t pce = 0;

    if (idio_isa_pair (args)) {
	IDIO c = IDIO_PAIR_H (args);
	if (idio_isa_closure (c)) {
	    pc0 = IDIO_CLOSURE_CODE_PC (c);
	    pce = pc0 + IDIO_CLOSURE_CODE_LEN (c);
	    fprintf (stderr, "NOTICE: unable to dump a specific closure: %zu, %zu\n", pc0, pce);
	} else {
	    /*
	     * Test Case: vm-errors/idio-dasm-bad-type.idio
	     *
	     * %%idio-dasm #t
	     */
	    idio_error_param_type ("closure", c, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    idio_vm_dump_dasm ();

    return idio_S_unspec;
}

void idio_vm_dump_dasm ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-dasm ");
#endif

    for (size_t xi = 0; xi < idio_xenvs_size; xi++) {
	char fn[40];
	snprintf (fn, 40, "idio-vm-dasm.%zu", xi);

	FILE *fp = fopen (fn, "w");
	if (NULL == fp) {
	    perror ("fopen (idio-vm-dasm, w)");
	    return;
	}

	idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

	idio_vm_dasm (fp, xi, 0, 0);

	fclose (fp);
    }
}

void idio_vm_dasm_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (dasm);
}

void idio_final_vm_dasm ()
{
    if (getpid () == idio_pid) {
	if (idio_vm_reports) {
	    idio_vm_dump_dasm ();
	}
    }
}

void idio_init_vm_dasm ()
{
    idio_module_table_register (idio_vm_dasm_add_primitives, idio_final_vm_dasm, NULL);
}

