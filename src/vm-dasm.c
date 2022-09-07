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

/*
 * IDIO_VM_DASM_OP() is largely used for those opcodes where we're
 * using a relative address into a symbol table (or other).  We want
 * to normalize the whitespace so that the, say, .37 is easier to scan
 * (being a single column rather than being lost in text) but that the
 * whitespace isn't so large that the eyeball begins to wander off
 * line.
 *
 * Technically, it should be %-24s to be large enough for
 * DYNAMIC-FUNCTION-SYM-REF but we can live with that being an
 * exception and eyeball wandering not being the norm.
 *
 * %-20s happens to align with SHALLOW-ARGUMENT-REF and others.
 */
#define IDIO_VM_DASM_OP(s)	{ fprintf (fp, "%-20s ", s); }
#define IDIO_VM_DASM(...)	{ fprintf (fp, __VA_ARGS__); }

IDIO idio_vm_dasm_symbols_ref (idio_xi_t xi, idio_as_t si)
{
    IDIO st = IDIO_XENV_ST (idio_xenvs[xi]);
    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);

    if (xi) {
	if (si >= idio_array_size (st)) {
	    fprintf (stderr, "dasm-sym-ref [%zu].%zd > %zu\n", xi, si, idio_array_size (st));
	    idio_debug ("st %s\n", st);
	    return idio_S_undef;
	    assert (0);
	}
	IDIO fci = idio_array_ref_index (st, si);

	if (idio_isa_fixnum (fci)) {
	    return idio_array_ref_index (cs, IDIO_FIXNUM_VAL (fci));
	} else {
	    return idio_S_unspec;
	}
    } else {
	return idio_array_ref_index (cs, si);
    }
}

IDIO idio_vm_dasm_constants_ref (idio_xi_t xi, idio_as_t ci)
{
    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);

    return idio_array_ref_index (cs, ci);
}

void idio_vm_dasm (FILE *fp, idio_xi_t xi, idio_pc_t pc0, idio_pc_t pce)
{
    IDIO_C_ASSERT (fp);

    IDIO_IA_T bc = IDIO_XENV_BYTE_CODE (idio_xenvs[xi]);

    fprintf (fp, "byte code for xenv[%zu]: %zd instruction bytes\n", xi, IDIO_IA_USIZE (bc));

    IDIO thr = idio_thread_current_thread ();

    if (0 == pce) {
	pce = IDIO_IA_USIZE (bc);
    }

    if (pc0 > pce) {
	fprintf (stderr, "\n\nPC %zd > max code PC %zd\n", pc0, pce);
	idio_vm_panic (thr, "vm-dasm: bad PC!");
    }

    IDIO cs = IDIO_XENV_CS (idio_xenvs[xi]);
    IDIO fnh = IDIO_HASH_EQP (8);
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

		IDIO_VM_DASM ("SHALLOW-ARGUMENT-REF %" PRIu64 " ", j);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_REF:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		uint64_t j = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("DEEP-ARGUMENT-REF    %" PRIu64 " %" PRIu64 " ", i, j);
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

		IDIO_VM_DASM ("SHALLOW-ARGUMENT-SET %" PRIu64 " ", i);
	    }
	    break;
	case IDIO_A_DEEP_ARGUMENT_SET:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);
		uint64_t j = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("DEEP-ARGUMENT-SET    %" PRIu64 " %" PRIu64 "", i, j);
	    }
	    break;
	case IDIO_A_SYM_REF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM_OP ("SYM-REF");
		IDIO_VM_DASM (".%-4" PRIu64 " ", si);

		if (idio_isa_symbol (sym)) {
		    idio_debug_FILE (fp, " %s", sym);
		} else {
		    IDIO_VM_DASM (" !! %s", idio_type2string (sym));
		    idio_debug_FILE (fp, " %s !!", sym);
		}
	    }
	    break;
	case IDIO_A_FUNCTION_SYM_REF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM_OP ("FUNCTION-SYM-REF");
		IDIO_VM_DASM (".%-4" PRIu64 "", si);
		idio_debug_FILE (fp, " %s", sym);
	    }
	    break;
	case IDIO_A_CONSTANT_REF:
	    {
		uint64_t ci = idio_vm_get_varuint (bc, pcp);

		IDIO c = idio_vm_dasm_constants_ref (xi, ci);

		IDIO_VM_DASM_OP ("CONSTANT-REF");
		IDIO_VM_DASM (".%-4" PRIu64 " ", ci);
		idio_debug_FILE (fp, " %s", c);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_REF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("COMPUTED-SYM-REF");
		IDIO_VM_DASM (".%-4" PRIu64 "", si);
	    }
	    break;
	case IDIO_A_SYM_DEF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);
		uint64_t kci = idio_vm_get_varuint (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);
		IDIO kind = idio_vm_dasm_constants_ref (xi, kci);

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM_OP ("SYM-DEF");
		    IDIO_VM_DASM (".%-4" PRIu64 " ", si);
		    idio_debug_FILE (fp, "%s as ", sym);
		    idio_debug_FILE (fp, "%s", kind);
		} else {
		    IDIO_VM_DASM_OP ("SYM-DEF");
		    IDIO_VM_DASM (".%-4" PRIu64 " !! isa %s ", si, idio_type2string (sym));
		    idio_debug_FILE (fp, "== %s !! ", sym);
		}
	    }
	    break;
	case IDIO_A_SYM_SET:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM_OP ("SYM-SET");
		    IDIO_VM_DASM (".%-4" PRIu64 " ", si);
		    idio_debug_FILE (fp, "%s", sym);
		} else {
		    IDIO_VM_DASM_OP ("SYM-SET");
		    IDIO_VM_DASM (".%-4" PRIu64 " !! %s", si, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s !! ", sym);
		}
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_SET:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("COMPUTED-SYM-SET");
		IDIO_VM_DASM (".%-4" PRIu64 " ", si);
	    }
	    break;
	case IDIO_A_COMPUTED_SYM_DEF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("COMPUTED-SYM-DEF");
		IDIO_VM_DASM (".%-4" PRIu64 "", si);
	    }
	    break;
	case IDIO_A_VAL_SET:
	    {
		uint64_t vi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("VAL-SET");
		IDIO_VM_DASM (".%-4" PRIu64, vi);
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
		uint64_t si = idio_vm_get_varuint (bc, pcp);

		IDIO pd = idio_vm_iref_val (thr, xi, si, "PREDEFINED", IDIO_VM_IREF_VAL_UNDEF_FATAL);
		if (idio_isa_primitive (pd)) {
		    IDIO_VM_DASM_OP ("PREDEFINED");
		    IDIO_VM_DASM (".%-4" PRIu64 " PRIM %-20s", si, IDIO_PRIMITIVE_NAME (pd));
		} else {
		    IDIO_VM_DASM_OP ("PREDEFINED");
		    IDIO_VM_DASM (".%-4" PRIu64 " !! isa %s ", si, idio_type2string (pd));
		    idio_debug_FILE (fp, "== %s !!", pd);
		}
	    }
	    break;
	case IDIO_A_LONG_GOTO:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LG@%" PRIu64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM_OP ("LONG-GOTO");
		IDIO_VM_DASM ("+%" PRIu64 " %s", i, h);
	    }
	    break;
	case IDIO_A_LONG_JUMP_FALSE:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LJF@%" PRIu64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM_OP ("LONG-JUMP-FALSE");
		IDIO_VM_DASM ("+%" PRIu64 " %s", i, h);
	    }
	    break;
	case IDIO_A_LONG_JUMP_TRUE:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "LJT@%" PRIu64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM_OP ("LONG-JUMP-TRUE");
		IDIO_VM_DASM ("+%" PRIu64 " %s", i, h);
	    }
	    break;
	case IDIO_A_SHORT_GOTO:
	    {
		IDIO_I i = IDIO_IA_GET_NEXT (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SG@%zd", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM_OP ("SHORT-GOTO");
		IDIO_VM_DASM ("+%hhu %s", i, h);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_FALSE:
	    {
		IDIO_I i = IDIO_IA_GET_NEXT (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SJF@%zd", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM_OP ("SHORT-JUMP-FALSE");
		IDIO_VM_DASM ("+%hhu %s", i, h);
	    }
	    break;
	case IDIO_A_SHORT_JUMP_TRUE:
	    {
		IDIO_I i = IDIO_IA_GET_NEXT (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "SJT@%zd", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM_OP ("SHORT-JUMP-TRUE");
		IDIO_VM_DASM ("+%hhu %s", i, h);
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
		uint64_t sei = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM_OP ("SRC-EXPR");
		IDIO_VM_DASM (".%-4" PRIu64 " ", sei);

		IDIO e = idio_vm_src_expr_ref (xi, sei);
		IDIO lo = idio_vm_src_props_ref (xi, sei);

		if (idio_isa_pair (lo)) {
		    IDIO fi = IDIO_PAIR_H (lo);
		    IDIO fn = idio_hash_reference (fnh, fi, IDIO_LIST1 (idio_S_false));
		    if (idio_S_false == fn) {
			fn = idio_array_ref_index (cs, IDIO_FIXNUM_VAL (fi));
			idio_hash_set (fnh, fi, fn);
		    }
		    idio_debug_FILE (fp, " %s", fn);
		    idio_debug_FILE (fp, ":line %s", IDIO_PAIR_HT (lo));
		} else {
		    IDIO_VM_DASM (" %-25s", "<no lex tuple>");
		}
		if (idio_S_false != e) {
		    idio_debug_FILE (fp, "\n  %.80s", e);
		}
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
		uint64_t code_len = idio_vm_get_varuint (bc, pcp);
		uint64_t nci  = idio_vm_get_varuint (bc, pcp);
		uint64_t ssci = idio_vm_get_varuint (bc, pcp);
		uint64_t dsci = idio_vm_get_varuint (bc, pcp);
		uint64_t sei = idio_vm_get_varuint (bc, pcp);

		char h[BUFSIZ];
		size_t hlen = idio_snprintf (h, BUFSIZ, "CF@%" PRIu64 "", pc + i);
		idio_hash_put (hints, idio_fixnum (pc + i), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM_OP ("CREATE-FUNCTION");
		IDIO_VM_DASM ("@ +%" PRIu64 " %s RETURN @%" PRIu64 "", i, h, pc + i + code_len - 1);

		/* name lookup */
		IDIO name = idio_vm_dasm_constants_ref (xi, nci);
		size_t size = 0;
		char *ids = idio_display_string (name, &size);
		IDIO_VM_DASM ("\n  name   %s", ids);
		IDIO_GC_FREE (ids, size);

		/* sigstr lookup */
		IDIO ss = idio_vm_dasm_constants_ref (xi, ssci);
		size = 0;
		ids = idio_display_string (ss, &size);
		IDIO_VM_DASM ("\n  sigstr %s", ids);
		IDIO_GC_FREE (ids, size);

		/* docstr lookup */
		IDIO ds = idio_vm_dasm_constants_ref (xi, dsci);
		if (idio_S_nil != ds) {
		    size = 0;
		    ids = idio_as_string_safe (ds, &size, 1, 1);
		    IDIO_VM_DASM ("\n  docstr %s", ids);
		    IDIO_GC_FREE (ids, size);
		}

		/* srcloc lookup */
		IDIO sp = idio_vm_src_props_ref (xi, sei);
		if (idio_isa_pair (sp)) {
		    IDIO fn = idio_vm_dasm_constants_ref (xi, IDIO_FIXNUM_VAL (IDIO_PAIR_H (sp)));
		    idio_debug_FILE (fp, "\n  srcloc %s", fn);
		    idio_debug_FILE (fp, ":line %s", IDIO_PAIR_HT (sp));
		}
	    }
	    break;
	case IDIO_A_CREATE_CLOSURE:
	    {
		uint64_t vi = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("CREATE-CLOSURE");
		IDIO_VM_DASM (".%-4" PRIu64, vi);
	    }
	    break;
	case IDIO_A_FUNCTION_INVOKE:
	    {
		IDIO_VM_DASM ("FUNCTION-INVOKE ... ");
	    }
	    break;
	case IDIO_A_FUNCTION_GOTO:
	    {
		IDIO_VM_DASM ("FUNCTION-GOTO ... ");
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
		size_t hlen = idio_snprintf (h, BUFSIZ, "A@%" PRIu64 "", pc + o);
		idio_hash_put (hints, idio_fixnum (pc + o), idio_symbols_C_intern (h, hlen));
		IDIO_VM_DASM ("PUSH-ABORT");
		IDIO_VM_DASM ("to PC +%" PRIu64 " %s", o + 1, h);
	    }
	    break;
	case IDIO_A_POP_ABORT:
	    {
		IDIO_VM_DASM ("POP-ABORT");
	    }
	    break;
	case IDIO_A_FINISH:
	    {
		IDIO_VM_DASM ("FINISH\n");
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

		IDIO_VM_DASM ("ALLOCATE-FRAME %" PRIu64, i);
	    }
	    break;
	case IDIO_A_ALLOCATE_DOTTED_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("ALLOCATE-DOTTED-FRAME %" PRIu64, arity);
	    }
	    break;
	case IDIO_A_REUSE_FRAME:
	    {
		uint64_t i = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("REUSE-FRAME %" PRIu64, i);
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

		IDIO_VM_DASM ("POP-FRAME %" PRIu64 "", rank);
	    }
	    break;
	case IDIO_A_LINK_FRAME:
	    {
		uint64_t si = idio_vm_get_varuint (bc, pcp);

		IDIO names = idio_vm_dasm_constants_ref (xi, si);
		IDIO_VM_DASM_OP ("LINK-FRAME");
		IDIO_VM_DASM (".%-4" PRIu64, si);
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

		IDIO_VM_DASM ("PACK-FRAME %" PRIu64 "", arity);
	    }
	    break;
	case IDIO_A_POP_LIST_FRAME:
	    {
		uint64_t arity = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("POP-LIST-FRAME %" PRIu64 "", arity);
	    }
	    break;
	case IDIO_A_EXTEND_FRAME:
	    {
		uint64_t alloc = idio_vm_get_varuint (bc, pcp);
		uint64_t si = idio_vm_get_varuint (bc, pcp);

		IDIO names = idio_vm_dasm_constants_ref (xi, si);

		IDIO_VM_DASM ("EXTEND-FRAME %2" PRIu64 " .%-4" PRIu64, alloc, si);
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

		IDIO_VM_DASM ("ARITY=? %" PRIu64 "", arityp1);
	    }
	    break;
	case IDIO_A_ARITYGEP:
	    {
		uint64_t arityp1 = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("ARITY>=? %" PRIu64 "", arityp1);
	    }
	    break;
	case IDIO_A_CONSTANT_0:
	    {
		IDIO_VM_DASM ("CONSTANT 0");
	    }
	    break;
	case IDIO_A_CONSTANT_1:
	    {
		IDIO_VM_DASM ("CONSTANT 1");
	    }
	    break;
	case IDIO_A_CONSTANT_2:
	    {
		IDIO_VM_DASM ("CONSTANT 2");
	    }
	    break;
	case IDIO_A_CONSTANT_3:
	    {
		IDIO_VM_DASM ("CONSTANT 3");
	    }
	    break;
	case IDIO_A_CONSTANT_4:
	    {
		IDIO_VM_DASM ("CONSTANT 4");
	    }
	    break;
	case IDIO_A_FIXNUM:
	    {
		uint64_t v = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM ("FIXNUM %" PRIu64 "", v);
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

		IDIO_VM_DASM ("CONSTANT %5" PRIu64 "", v);
		size_t size = 0;
		char *ids = idio_display_string (IDIO_CONSTANT_IDIO ((intptr_t) v), &size);
		IDIO_VM_DASM (" %s", ids);
		IDIO_GC_FREE (ids, size);
	    }
	    break;
	case IDIO_A_NEG_CONSTANT:
	    {
		int64_t v = idio_vm_get_varuint (bc, pcp);

		v = -v;
		IDIO_VM_DASM ("NEG-CONSTANT %6" PRId64 "", v);
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
		uint64_t si = idio_vm_get_varuint (bc, pcp);

		IDIO pd = idio_vm_iref_val (thr, xi, si, "PRIMITIVE/0", IDIO_VM_IREF_VAL_UNDEF_FATAL);
		if (idio_isa_primitive (pd)) {
		    IDIO_VM_DASM_OP ("PRIMITIVE/0");
		    IDIO_VM_DASM (".%-4" PRIu64 " %s", si, IDIO_PRIMITIVE_NAME (pd));
		} else {
		    IDIO_VM_DASM_OP ("PRIMITIVE/0");
		    IDIO_VM_DASM (".%-4" PRIu64 " !! isa %s ", si, idio_type2string (pd));
		    idio_debug_FILE (fp, "== %s !!", pd);
		}
	    }
	    break;
	case IDIO_A_PRIMCALL1:
	    {
		uint64_t si = idio_vm_get_varuint (bc, pcp);

		IDIO pd = idio_vm_iref_val (thr, xi, si, "PRIMITIVE/1", IDIO_VM_IREF_VAL_UNDEF_FATAL);
		if (idio_isa_primitive (pd)) {
		    IDIO_VM_DASM_OP ("PRIMITIVE/1");
		    IDIO_VM_DASM (".%-4" PRIu64 " %s", si, IDIO_PRIMITIVE_NAME (pd));
		} else {
		    IDIO_VM_DASM_OP ("PRIMITIVE/1");
		    IDIO_VM_DASM (".%-4" PRIu64 " !! isa %s ", si, idio_type2string (pd));
		    idio_debug_FILE (fp, "== %s !!", pd);
		}
	    }
	    break;
	case IDIO_A_PRIMCALL2:
	    {
		uint64_t si = idio_vm_get_varuint (bc, pcp);

		IDIO pd = idio_vm_iref_val (thr, xi, si, "PRIMITIVE/2", IDIO_VM_IREF_VAL_UNDEF_FATAL);
		if (idio_isa_primitive (pd)) {
		    IDIO_VM_DASM_OP ("PRIMITIVE/2");
		    IDIO_VM_DASM (".%-4" PRIu64 " %s", si, IDIO_PRIMITIVE_NAME (pd));
		} else {
		    IDIO_VM_DASM_OP ("PRIMITIVE/2");
		    IDIO_VM_DASM (".%-4" PRIu64 " !! isa %s ", si, idio_type2string (pd));
		    idio_debug_FILE (fp, "== %s !!", pd);
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
		uint64_t ci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("EXPANDER");
		IDIO_VM_DASM (".%-4" PRIu64 "", ci);
	    }
	    break;
	case IDIO_A_INFIX_OPERATOR:
	    {
		uint64_t oi = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM_OP ("INFIX-OPERATOR");
		IDIO_VM_DASM (".%-4" PRIu64 " pri %4" PRIu64 " ", oi, pri);
		IDIO sym = idio_vm_symbols_ref (xi, oi);
		IDIO_TYPE_ASSERT (symbol, sym);
		idio_debug_FILE (fp, "%s", sym);
	    }
	    break;
	case IDIO_A_POSTFIX_OPERATOR:
	    {
		uint64_t oi = IDIO_VM_GET_REF (bc, pcp);
		uint64_t pri = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM_OP ("POSTFIX-OPERATOR");
		IDIO_VM_DASM (".%-4" PRIu64 " pri %4" PRIu64 " ", oi, pri);
		IDIO sym = idio_vm_symbols_ref (xi, oi);
		IDIO_TYPE_ASSERT (symbol, sym);
		idio_debug_FILE (fp, "%s", sym);
	    }
	    break;
	case IDIO_A_PUSH_DYNAMIC:
	    {
		uint64_t ci = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, ci);

		IDIO_VM_DASM_OP ("PUSH-DYNAMIC");
		IDIO_VM_DASM (".%-4" PRIu64 " ", ci);
		idio_debug_FILE (fp, "%s", sym);
	    }
	    break;
	case IDIO_A_POP_DYNAMIC:
	    {
		IDIO_VM_DASM_OP ("POP-DYNAMIC");
	    }
	    break;
	case IDIO_A_DYNAMIC_SYM_REF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM_OP ("DYNAMIC-SYM-REF");
		IDIO_VM_DASM (".%-4" PRIu64 " ", si);
		idio_debug_FILE (fp, "%s", sym);
	    }
	    break;
	case IDIO_A_DYNAMIC_FUNCTION_SYM_REF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		IDIO_VM_DASM_OP ("DYNAMIC-FUNCTION-SYM-REF");
		IDIO_VM_DASM (".%-4" PRIu64 " ", si);
		idio_debug_FILE (fp, "%s", sym);
	    }
	    break;
	case IDIO_A_PUSH_ENVIRON:
	    {
		uint64_t ci = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, ci);

		IDIO_VM_DASM_OP ("PUSH-ENVIRON");
		IDIO_VM_DASM (".%-4" PRIu64 " ", ci);
		idio_debug_FILE (fp, "%s", sym);
	    }
	    break;
	case IDIO_A_POP_ENVIRON:
	    {
		IDIO_VM_DASM_OP ("POP-ENVIRON");
	    }
	    break;
	case IDIO_A_ENVIRON_SYM_REF:
	    {
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO sym = idio_vm_dasm_symbols_ref (xi, si);

		if (idio_isa_symbol (sym)) {
		    IDIO_VM_DASM_OP ("ENVIRON-SYM-REF");
		    IDIO_VM_DASM (".%-4" PRIu64 " ", si);
		    idio_debug_FILE (fp, "%s", sym);
		} else {
		    IDIO_VM_DASM_OP ("ENVIRON-SYM-REF");
		    IDIO_VM_DASM (".%-4" PRIu64 " !! %s", si, idio_type2string (sym));
		    idio_debug_FILE (fp, " %s !! ", sym);
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
		uint64_t si = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("PUSH-TRAP");
		IDIO_VM_DASM (".%-4" PRIu64, si);
	    }
	    break;
	case IDIO_A_POP_TRAP:
	    {
		IDIO_VM_DASM ("POP-TRAP");
	    }
	    break;
	case IDIO_A_PUSH_ESCAPER:
	    {
		uint64_t ci = IDIO_VM_GET_REF (bc, pcp);
		uint64_t offset = idio_vm_get_varuint (bc, pcp);

		IDIO_VM_DASM_OP ("PUSH-ESCAPER");
		IDIO_VM_DASM (".%-4" PRIu64 " -> %" PRIu64, ci, pc + offset + 1);
	    }
	    break;
	case IDIO_A_POP_ESCAPER:
	    {
		IDIO_VM_DASM ("POP-ESCAPER");
	    }
	    break;
	case IDIO_A_ESCAPER_LABEL_REF:
	    {
		uint64_t gci = IDIO_VM_GET_REF (bc, pcp);

		IDIO_VM_DASM_OP ("ESCAPER-LABEL-REF");
		IDIO_VM_DASM (".%-4" PRIu64, gci);
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
		fprintf (stderr, "idio-vm-dasm: unexpected ins %3d [%zu]@%zd\n", ins, xi, pci);
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
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected instruction: %3d [%zu]@%" PRId64 "\n", ins, xi, pci);

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

    if (idio_isa_pair (args)) {
	IDIO c = IDIO_PAIR_H (args);
	if (idio_isa_closure (c)) {
	    idio_pc_t pc0 = IDIO_CLOSURE_CODE_PC (c);
	    idio_pc_t pce = pc0 + IDIO_CLOSURE_CODE_LEN (c);
	    idio_xi_t xi = IDIO_CLOSURE_XI (c);
	    idio_vm_dasm (stderr, xi, pc0, pce);
	    return idio_S_unspec;
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

void idio_vm_dump_xenv_dasm (idio_xi_t xi)
{
    char fn[40];
    snprintf (fn, 40, "idio-vm-dasm.%zu", xi);

    FILE *fp = fopen (fn, "w");
    if (NULL == fp) {
	perror ("fopen (idio-vm-dasm, w)");
	return;
    }
    /*
     * Debugging by dropping out the dasm so far can be risky so
     * linebuffer the output for max info.
     */
    setlinebuf (fp);

    idio_debug_FILE (fp, "%s\n", IDIO_XENV_DESC (idio_xenvs[xi]));

    idio_vm_dasm (fp, xi, 0, 0);

    fclose (fp);
}

void idio_vm_dump_dasm ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "vm-dasm[%zu] ", idio_xenvs_size);
#endif

    for (idio_xi_t xi = 0; xi < idio_xenvs_size; xi++) {
	idio_vm_dump_xenv_dasm (xi);
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
    idio_module_table_register (idio_vm_dasm_add_primitives, NULL, NULL);
}

