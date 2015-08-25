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
 * vm.c
 * 
 */

#include "idio.h"

typedef struct idio_i_array_s {
    size_t n;
    size_t i;
    IDIO_I *ae;
} idio_i_array_t;
    
/*
 * Some debugging aids.
 *
 * idio_vm_tracing reports the nominal function call and arguments and
 * return value.  You can enable/disable it in code with
 *
 * %%vm-trace {val}.
 *
 * Note that as you can start it deep inside some nested call
 * structure and have it continue to run until well outside that nest
 * then the indentation will be wrong.
 *
 *
 * idio_vm_dis reports the byte-instruction by byte-instruction flow.
 * You can enable/disable it in code with
 *
 * %%vm-dis {val}
 *
 * It is very^W verbose.  You were warned.  I don't feel bad about
 * your new found pain.
 */
static int idio_vm_tracing = 0;
#ifdef IDIO_DEBUG
static int idio_vm_dis = 0;
#endif
/*
 * We don't know if some arbitrary code is going to set a global value
 * to be a closure.  If it does, we need to retain the code for the
 * closure.  Hence a global list of all known code.
 *
 * Prologue
 *
 * There is a prologue which defines some universal get-out behaviour
 * (from Queinnec).  idio_vm_FINISH_pc is the PC for the FINISH
 * instruction and idio_prologue_len how big the prologue is.
 *
 * In addition:
 *
 *   idio_vm_NCE_pc	NON-CONT-ERR
 *   idio_vm_CR_pc	condition return
 *   idio_vm_IHR_pc	interrupt handler return
 */
static idio_i_array_t *idio_all_code;
static idio_ai_t idio_vm_FINISH_pc;
static idio_ai_t idio_vm_NCE_pc;
static idio_ai_t idio_vm_CR_pc;
static idio_ai_t idio_vm_IHR_pc;
static size_t idio_prologue_len;

/*
 * Some VM tables:
 *
 * constants - all known constants
 *
 *   we can't have numbers and quoted values etc. embedded in compiled
 *   code -- as they are an unknown size which is bad for a byte
 *   compiler -- but we can have an index into this table
 *
 * symbols/values - all known toplevel symbols and values
 * 
 *   these two run in lock-step.  values is what the VM cares about
 *   and is the tables of all toplevel values (lexical and dynamic)
 *   across all modules and GLOBAL-SET/REF etc. indirect into this
 *   table by an index.  The evaluator cares about symbols.  Each time
 *   it extends the tables of symbols the table of values grows by one
 *   too (and is set to #undef to nominally catch undefined values).
 *
 * primitives -
 *
 *   do these need a separate table?  They've currently got one...
 *
 * closure_name -
 *
 *   if we see GLOBAL-SET {name} {CLOS} we can maintain a map from
 *   {CLOS} back to the name the closure was once defined as.  This is
 *   handy during a trace when a {name} is redefined -- which happens
 *   a lot.  The trace always prints the closure's ID (memory
 *   location) so you can see if a recursive call is actually
 *   recursive or calling a previous definition (which may be
 *   recursive).
 *
 *   When a reflective evaluator is implemented this table should go
 *   (as the details will be indexes to constants and embedded in the
 *   code).
 */
static IDIO idio_vm_constants;
static IDIO idio_vm_symbols;
static IDIO idio_vm_values;
static IDIO idio_vm_primitives;
static IDIO idio_vm_closure_name;

static IDIO idio_vm_base_error_handler_primdata;
static IDIO idio_vm_sigchld_handler_name;
static IDIO idio_vm_sigchld_handler;
static IDIO idio_S_sigchld;

#define IDIO_THREAD_FETCH_NEXT()	(idio_all_code->ae[IDIO_THREAD_PC(thr)++])
#define IDIO_THREAD_STACK_PUSH(v)	(idio_array_push (IDIO_THREAD_STACK(thr), v))
#define IDIO_THREAD_STACK_POP()		(idio_array_pop (IDIO_THREAD_STACK(thr)))

#define IDIO_VM_INVOKE_REGULAR	 0
#define IDIO_VM_INVOKE_TAIL_CALL 1

static void idio_vm_panic (IDIO thr, char *m)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * Not reached!
     *
     *
     * Ha!  Yeah, I wish! ... :(
     */
    
    fprintf (stderr, "\n\nPANIC: %s\n\n", m);
    idio_dump (thr, 10);
    
    IDIO_C_ASSERT (0);
}

static void idio_vm_error_compile_param_args (char *m, IDIO mt, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (mt);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);
    
    idio_error_C (m, mt, loc);
}

static void idio_vm_error_compile_param_type (char *m, IDIO t, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (t);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_error_param_type (m, t, loc);
}

static void idio_vm_error_function_invoke (char *msg, IDIO args, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, loc);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);
    idio_display_C (": '", sh);
    idio_display (args, sh);
    idio_display_C ("'", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_function_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil));
    idio_raise_condition (idio_S_true, c);
}

static void idio_vm_function_trace (IDIO_I ins, IDIO thr);
static void idio_vm_error_arity (IDIO_I ins, IDIO thr, size_t given, size_t arity, IDIO loc)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, loc);

    fprintf (stderr, "\n\nARITY != %zd\n", arity);
    idio_vm_function_trace (ins, thr);

    IDIO sh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "incorrect arity: %zd args for an arity-%zd function", given, arity);
    idio_display_C (em, sh);
    IDIO c = idio_struct_instance (idio_condition_rt_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil));
    idio_raise_condition (idio_S_true, c);
}

static void idio_vm_error_arity_varargs (IDIO_I ins, IDIO thr, size_t given, size_t arity, IDIO loc)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (string, loc);

    fprintf (stderr, "\n\nARITY < %zd\n", arity);
    idio_vm_function_trace (ins, thr);

    IDIO sh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "incorrect arity: %zd args for an arity-%zd+ function", given, arity);
    idio_display_C (em, sh);
    IDIO c = idio_struct_instance (idio_condition_rt_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil));
    idio_raise_condition (idio_S_true, c);
}

static void idio_error_dynamic_unbound (idio_ai_t index, IDIO loc)
{
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("no such dynamic binding", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_dynamic_variable_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       idio_vm_symbols_ref (index)));
    idio_raise_condition (idio_S_true, c);
}

static void idio_error_environ_unbound (idio_ai_t index, IDIO loc)
{
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("no such environ binding", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_environ_variable_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       idio_vm_symbols_ref (index)));
    idio_raise_condition (idio_S_true, c);
}

static void idio_vm_error_computed (char *msg, idio_ai_t index, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);
    IDIO c = idio_struct_instance (idio_condition_rt_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       idio_vm_symbols_ref (index)));
    idio_raise_condition (idio_S_true, c);
}

/*
 * Idio instruction arrays.
 *
 * We generate our byte compiled code into these.  There's often
 * several floating around at once as in order to determine how far to
 * jump over some not-immediately-relevant code to move onto the next
 * (source code) statement we need to have already byte compiled the
 * not-immediately-relevant code.  Which we can then copy.
 *
 * XXX These are reference counted for reasons to do with an
 * undiscovered bug^W^Wundocumented feature.
 */
idio_i_array_t *idio_i_array (size_t n)
{
    if (n <= 0) {
	n = 100;
    }

    idio_i_array_t *ia = idio_alloc (sizeof (idio_i_array_t));
    ia->ae = idio_alloc (n * sizeof (IDIO_I));
    ia->n = n;
    ia->i = 0;

    return ia;
}

void idio_i_array_free (idio_i_array_t *ia)
{
    free (ia->ae);
    free (ia);
}

static void idio_i_array_resize_by (idio_i_array_t *ia, size_t n)
{
    ia->n += n;
    ia->ae = idio_realloc (ia->ae, ia->n);
}

void idio_i_array_resize (idio_i_array_t *ia)
{
    idio_i_array_resize_by (ia, ia->n >> 1);
}

void idio_i_array_append (idio_i_array_t *ia1, idio_i_array_t *ia2)
{
    if ((ia1->n - ia1->i) < ia2->i) {
	idio_i_array_resize_by (ia1, ia2->i);
    }
    size_t i;
    for (i = 0; i < ia2->i; i++) {
	ia1->ae[ia1->i++] = ia2->ae[i];
    }
}

void idio_i_array_copy (idio_i_array_t *iad, idio_i_array_t *ias)
{
    if (iad->n < ias->i) {
	idio_i_array_resize_by (iad, ias->i - iad->n);
    }
    size_t i;
    for (i = 0; i < ias->i; i++) {
	iad->ae[i] = ias->ae[i];
    }
    iad->i = ias->i;
}

void idio_i_array_push (idio_i_array_t *ia, IDIO_I ins)
{
    if (ia->i >= ia->n) {
	idio_i_array_resize (ia);
    }
    ia->ae[ia->i++] = ins;
}

#define IDIO_IA_PUSH1(i1)		(idio_i_array_push (ia, i1))
#define IDIO_IA_PUSH2(i1,i2)		IDIO_IA_PUSH1 (i1); IDIO_IA_PUSH1 (i2)
#define IDIO_IA_PUSH3(i1,i2,i3)		IDIO_IA_PUSH1 (i1); IDIO_IA_PUSH1 (i2); IDIO_IA_PUSH1 (i3)
#define IDIO_IA_PUSH4(i1,i2,i3,i4)	IDIO_IA_PUSH1 (i1); IDIO_IA_PUSH1 (i2); IDIO_IA_PUSH1 (i3); IDIO_IA_PUSH1 (i4)
#define IDIO_IA_PUSH5(i1,i2,i3,i4,i5)	IDIO_IA_PUSH1 (i1); IDIO_IA_PUSH1 (i2); IDIO_IA_PUSH1 (i3); IDIO_IA_PUSH1 (i4); IDIO_IA_PUSH1 (i5)

/*
 * Variable length integers
 *
 * We want to be able to encode integers -- usually indexes but also
 * fixnums and characters -- reasonably efficiently (ie. a small
 * number of bytes) but there is the potential for some large values.

 * SQLite4 varuint: https://sqlite.org/src4/doc/trunk/www/varint.wiki
 *
 * This encoding supports positive numbers only hence extra
 * instructions to reference negative values.
 */
idio_i_array_t *idio_i_array_compute_varuint (uint64_t offset)
{
    idio_i_array_t *ia = idio_i_array (10);
    
    if (offset <= 240) {
	IDIO_IA_PUSH1 (offset);
    } else if (offset <= 2287) {
	IDIO_IA_PUSH1 (((offset - 240) / 256) + 241);
	IDIO_IA_PUSH1 ((offset - 240) % 256);
    } else if (offset <= 67823) {
	IDIO_IA_PUSH1 (249);
	IDIO_IA_PUSH1 ((offset - 2288) / 256);
	IDIO_IA_PUSH1 ((offset - 2288) % 256);
    } else {
	int n;
	if (offset <= 16777215) {
	    IDIO_IA_PUSH1 (250);
	    n = 3;
	} else if (offset <= 4294967295LL) {
	    IDIO_IA_PUSH1 (251);
	    n = 4;
	} else if (offset <= 1099511627775LL) {
	    IDIO_IA_PUSH1 (252);
	    n = 5;
	} else if (offset <= 281474976710655LL) {
	    IDIO_IA_PUSH1 (253);
	    n = 6;
	} else if (offset <= 72057594037927935LL) {
	    IDIO_IA_PUSH1 (254);
	    n = 7;
	} else {
	    IDIO_IA_PUSH1 (255);
	    n = 8;
	}

	/*
	 * Do the hard work here so that decoding can simply
	 * bitwise-shift left the current value by 8, read a byte,
	 * bitwise-or it onto the current value.
	 */
	int i0 = ia->i;
	int i;
	for (i = 0; i < n; i++) {
	    ia->ae[i0 + n -1 - i] = (offset & 0xff);
	    offset >>= 8;
	}
	ia->i += n;
    }

    return ia;
}

#define IDIO_IA_PUSH_VARUINT(n)    { idio_i_array_t *ia2 = idio_i_array_compute_varuint (n); idio_i_array_append (ia, ia2); idio_i_array_free (ia2); }

/*
 * Compiling
 *
 * Compiling the intermediate code (idio_I_*) is a reasonably
 * straightforward swap to IDIO_A_*.
 *
 * There's some specialisation for particularly common tuples to
 * reduce the size of the byte code and, hopefully, make the resultant
 * interpretation quicker as there's less decoding of arguments to do.
 *
 * The flip side of that is more code both in compilation and
 * interpretation of the resultant byte code.
 */
void idio_vm_compile (IDIO thr, idio_i_array_t *ia, IDIO m, int depth)
{
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (pair, m);

    /* idio_debug ("compile: %s\n", m);  */
    
    IDIO mh = IDIO_PAIR_H (m);
    IDIO mt = IDIO_PAIR_T (m);

    if (! IDIO_TYPE_CONSTANTP (mh)) {
	/*
	 * A sequence of idio_I_* code segments will appear here as a
	 * list so we simply recurse for each one.
	 */
	if (idio_isa_pair (mh)) {
	    while (idio_S_nil != m) {
		idio_vm_compile (thr, ia, IDIO_PAIR_H (m), depth + 1);
		m = IDIO_PAIR_T (m);
		if (! idio_isa_list (m)) {
		    idio_error_C ("compile: not a sequence", m, IDIO_C_LOCATION ("idio_vm_compile"));
		    return;
		}
	    }
	    return;
	} else {
	    idio_debug ("\nWARNING: not a CONSTANT|pair: unexpected intermediate code: %s\n\n", mh);
	    return;
	}
    }
    
    switch (IDIO_CONSTANT_VAL (mh)) {
    case IDIO_VM_CODE_SHALLOW_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("SHALLOW-ARGUMENT-REF j", mt, IDIO_C_LOCATION ("idio_vm_compile/SHALLOW-ARGUMENT-REF"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/SHALLOW-ARGUMENT-REF"));
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (j)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF0); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF1); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF2); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF3); break;
	    default:
		IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_PREDEFINED:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("PREDEFINED i", mt, IDIO_C_LOCATION ("idio_vm_compile/PREDEFINED"));
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_vm_error_compile_param_type ("fixnum", i, IDIO_C_LOCATION ("idio_vm_compile/PREDEFINED"));
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (i)) {
	    default:
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
		break;
	    }

	    /*
	     * Specialization.  Or will be...
	     */
	    if (idio_S_true == i) {
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED0);
	    } else if (idio_S_false == i) {
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED1);
	    } else if (idio_S_nil == i) {
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED2);
	    } else if (idio_S_nil == i) { /* cons */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED3);
	    } else if (idio_S_nil == i) { /* car */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED4);
	    } else if (idio_S_nil == i) { /* cdr */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED5);
	    } else if (idio_S_nil == i) { /* pair? */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED6);
	    } else if (idio_S_nil == i) { /* symbol? */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED7);
	    } else if (idio_S_nil == i) { /* eq? */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED8);
	    } else {
		/* see above */
		/* IDIO_IA_PUSH1 (IDIO_A_PREDEFINED); */
		/* IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i)); */
	    }
	}
	break;
    case IDIO_VM_CODE_DEEP_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("DEEP-ARGUMENT-REF i j", mt, IDIO_C_LOCATION ("idio_vm_compile/DEEP-ARGUMENT-REF"));
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_vm_error_compile_param_type ("fixnum", i, IDIO_C_LOCATION ("idio_vm_compile/DEEP-ARGUMENT-REF"));
		return;
	    }

	    IDIO j = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/DEEP-ARGUMENT-REF"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_DEEP_ARGUMENT_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_SHALLOW_ARGUMENT_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("SHALLOW-ARGUMENT-SET j m1", mt, IDIO_C_LOCATION ("idio_vm_compile/SHALLOW-ARGUMENT-SET"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/SHALLOW-ARGUMENT-SET"));
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1, depth + 1);

	    switch (IDIO_FIXNUM_VAL (j)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET0); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET1); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET2); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET3); break;
	    default:
		IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_DEEP_ARGUMENT_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("DEEP-ARGUMENT-SET i j m1", mt, IDIO_C_LOCATION ("idio_vm_compile/DEEP-ARGUMENT-SET"));
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_vm_error_compile_param_type ("fixnum", i, IDIO_C_LOCATION ("idio_vm_compile/DEEP-ARGUMENT-SET"));
		return;
	    }

	    IDIO j = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/DEEP-ARGUMENT-SET"));
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    idio_vm_compile (thr, ia, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_DEEP_ARGUMENT_SET);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_GLOBAL_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("GLOBAL-REF j", mt, IDIO_C_LOCATION ("idio_vm_compile/GLOBAL-REF"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/GLOBAL-REF"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_CHECKED_GLOBAL_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("CHECKED-GLOBAL-REF j", mt, IDIO_C_LOCATION ("idio_vm_compile/CHECKED-GLOBAL-REF"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/CHECKED-GLOBAL-REF"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_CHECKED_GLOBAL_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_CHECKED_GLOBAL_FUNCTION_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("CHECKED-GLOBAL-FUNCTION-REF j", mt, IDIO_C_LOCATION ("idio_vm_compile/CHECKED-GLOBAL-FUNCTION-REF"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/CHECKED-GLOBAL-FUNCTION-REF"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_CHECKED_GLOBAL_FUNCTION_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_COMPUTED_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("COMPUTED-REF j", mt, IDIO_C_LOCATION ("idio_vm_compile/COMPUTED-REF"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/COMPUTED-REF"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_GLOBAL_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("GLOBAL-SET j m1", mt, IDIO_C_LOCATION ("idio_vm_compile/GLOBAL-SET"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/GLOBAL-SET"));
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    
	    idio_vm_compile (thr, ia, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_SET);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_COMPUTED_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("COMPUTED-SET j m1", mt, IDIO_C_LOCATION ("idio_vm_compile/COMPUTED-SET"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/COMPUTED-SET"));
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    
	    idio_vm_compile (thr, ia, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_SET);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_CONSTANT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("CONSTANT j", mt, IDIO_C_LOCATION ("idio_vm_compile/CONSTANT"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    switch ((intptr_t) j & 3) {
	    case IDIO_TYPE_FIXNUM_MARK:
		{
		    intptr_t jv = IDIO_FIXNUM_VAL (j);
		    switch (jv) {
		    case 0:
			IDIO_IA_PUSH1 (IDIO_A_CONSTANT_0);
			return;
		    case 1:
			IDIO_IA_PUSH1 (IDIO_A_CONSTANT_1);
			return;
		    case 2:
			IDIO_IA_PUSH1 (IDIO_A_CONSTANT_2);
			return;
		    case 3:
			IDIO_IA_PUSH1 (IDIO_A_CONSTANT_3);
			return;
		    case 4:
			IDIO_IA_PUSH1 (IDIO_A_CONSTANT_4);
			return;
		    default:
			{
			    intptr_t jv = IDIO_FIXNUM_VAL (j);

			    if (jv >= 0) {
				IDIO_IA_PUSH1 (IDIO_A_FIXNUM);
				IDIO_IA_PUSH_VARUINT (jv);
			    } else {
				IDIO_IA_PUSH1 (IDIO_A_NEG_FIXNUM);
				IDIO_IA_PUSH_VARUINT (-jv);
			    }
			    return;
			}
			break;
		    }
		}
		break;	    
	    case IDIO_TYPE_CHARACTER_MARK:
		{
		    intptr_t jv = IDIO_CHARACTER_VAL (j);

		    if (jv >= 0) {
			IDIO_IA_PUSH1 (IDIO_A_CHARACTER);
			IDIO_IA_PUSH_VARUINT (jv);
		    } else {
			IDIO_IA_PUSH1 (IDIO_A_NEG_CHARACTER);
			IDIO_IA_PUSH_VARUINT (-jv);
		    }
		    return;
		}
		break;
	    case IDIO_TYPE_CONSTANT_MARK:
		if (idio_S_true == j) {
		    IDIO_IA_PUSH1 (IDIO_A_PREDEFINED0);
		    return;
		} else if (idio_S_false == j) {
		    IDIO_IA_PUSH1 (IDIO_A_PREDEFINED1);
		    return;
		} else if (idio_S_nil == j) {
		    IDIO_IA_PUSH1 (IDIO_A_PREDEFINED2);
		    return;
		} else {
		    intptr_t jv = IDIO_CONSTANT_VAL (j);

		    if (jv >= 0) {
			IDIO_IA_PUSH1 (IDIO_A_CONSTANT);
			IDIO_IA_PUSH_VARUINT (jv);
		    } else {
			IDIO_IA_PUSH1 (IDIO_A_NEG_CONSTANT);
			IDIO_IA_PUSH_VARUINT (-jv);
		    }
		    return;
		}
		break;
	    default:
		{
		    idio_ai_t i = idio_vm_extend_constants (j);

		    /* fprintf (stderr, "vm-extend-constants: %zd", i); */
		    /* idio_debug (" == %s\n", j); */

		    IDIO_IA_PUSH1 (IDIO_A_CONSTANT_REF);
		    IDIO_IA_PUSH_VARUINT (i);
		    return;
		}
	    }

	}
	break;
    case IDIO_VM_CODE_ALTERNATIVE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("ALTERNATIVE m1 m2 m3", mt, IDIO_C_LOCATION ("idio_vm_compile/ALTERNATIVE"));
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO m2 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO m3 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    /*
	     * Think about the code to be generated where we can only
	     * calculate the jump-false #6 when we have added the code
	     * for both m2 and goto #7 and the code for goto #7
	     * depends on the code for m3.

	      1: ...
	      2: m1
	      3: jump-false #6
	      4: m2
	      5: goto #7
	      6: m3
	      7: ...

	     * Slightly complicated by our choosing to use either a
	     * SHORT-JUMP-FALSE with one byte of offset of a
	     * LONG-JUMP-FALSE with 2+ bytes of offset.
	     */

	    /* 2: */
	    idio_vm_compile (thr, ia, m1, depth + 1);

	    idio_i_array_t *ia2 = idio_i_array (100);
	    idio_vm_compile (thr, ia2, m2, depth + 1);

	    idio_i_array_t *ia3 = idio_i_array (100);
	    idio_vm_compile (thr, ia3, m3, depth + 1);

	    idio_i_array_t *g7;
	    size_t g7_len = 0;

	    /*
	     * include the size of the jump instruction!
	     *
	     * g7_len == size(ins) + size(off)
	     */
	    if (ia3->i < IDIO_I_MAX) {
		g7_len = 1 + 1;
	    } else {
		g7 = idio_i_array_compute_varuint (ia3->i);
		g7_len = 1 + g7->i;
	    }

	    size_t jf6_off = ia2->i + g7_len;

	    /* 3: */
	    if (jf6_off < IDIO_I_MAX) {
		IDIO_IA_PUSH1 (IDIO_A_SHORT_JUMP_FALSE);
		IDIO_IA_PUSH1 (jf6_off);
	    } else {
		idio_i_array_t *jf6 = idio_i_array_compute_varuint (jf6_off);
		IDIO_IA_PUSH1 (IDIO_A_LONG_JUMP_FALSE);
		idio_i_array_append (ia, jf6);
		idio_i_array_free (jf6);
	    }
	    
	    /* 4: */
	    idio_i_array_append (ia, ia2);

	    /* 5: */
	    if (ia3->i < IDIO_I_MAX) {
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (ia3->i);
	    } else {
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_i_array_append (ia, g7);
		idio_i_array_free (g7);
	    }

	    /* 6: */
	    idio_i_array_append (ia, ia3);

	    idio_i_array_free (ia2);
	    idio_i_array_free (ia3);
	}
	break;
    case IDIO_VM_CODE_SEQUENCE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("SEQUENCE m1 m+", mt, IDIO_C_LOCATION ("idio_vm_compile/SEQUENCE"));
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    idio_vm_compile (thr, ia, mp, depth + 1);
	}
	break;
    case IDIO_VM_CODE_AND:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 1) {
		idio_vm_error_compile_param_args ("AND m+", mt, IDIO_C_LOCATION ("idio_vm_compile/AND"));
		return;
	    }

	    /*
	     * We don't know how far to jump to the end of the AND
	     * sequence until we have generated the code for the
	     * remaining clauses *and* the inter-clause jumps
	     */
	    IDIO m1 = IDIO_PAIR_H (mt);
	    mt = IDIO_PAIR_T (mt);

	    idio_vm_compile (thr, ia, m1, depth + 1);

	    size_t n = idio_list_length (mt);
	    
	    idio_i_array_t *iac[n];
	    intptr_t i;

	    /*
	     * 1st pass, l is only the accumulated code size for
	     * allocating iar/iat
	     */
	    size_t l = 0;
	    for (i = 0; i < n ; i++) {
		iac[i] = idio_i_array (100);
		idio_vm_compile (thr, iac[i], IDIO_PAIR_H (mt), depth + 1);
		l += iac[i]->i;
		mt = IDIO_PAIR_T (mt);
	    }

	    /*
	     * How much temporary instruction space?
	     * 
	     * largest varuint is 9 bytes, jump ins is one and there
	     * will be n of them.
	     *
	     * To grow the result we need (jump+iac[i]) in iat then
	     * append iar then copy iat back to iar
	     */
	    idio_i_array_t *iar = idio_i_array (l + n * (1 + 9));
	    idio_i_array_t *iat = idio_i_array (l + n * (1 + 9));

	    /*
	     * 2nd pass, l now includes jump sizes
	     */
	    l = 0;
	    for (i = n - 1; i >= 0 ; i--) {
		l += iac[i]->i;
		if (l < IDIO_I_MAX) {
		    idio_i_array_push (iat, IDIO_A_SHORT_JUMP_FALSE);
		    idio_i_array_push (iat, l);
		} else {
		    idio_i_array_t *jf = idio_i_array_compute_varuint (l);
		    idio_i_array_push (iat, IDIO_A_LONG_JUMP_FALSE);
		    idio_i_array_append (iat, jf);
		    idio_i_array_free (jf);	    
		}

		idio_i_array_append (iat, iac[i]);
		idio_i_array_free (iac[i]);

		idio_i_array_append (iat, iar);
		idio_i_array_copy (iar, iat);
		iat->i = 0;
		l = iar->i;
	    }


	    idio_i_array_append (ia, iar);
	    idio_i_array_free (iar);
	    idio_i_array_free (iat);
	}
	break;
    case IDIO_VM_CODE_OR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 1) {
		idio_vm_error_compile_param_args ("OR m+", mt, IDIO_C_LOCATION ("idio_vm_compile/OR"));
		return;
	    }
	    
	    /*
	     * We don't know how far to jump to the end of the OR
	     * sequence until we have generated the code for the
	     * remaining clauses *and* the inter-clause jumps
	     */
	    IDIO m1 = IDIO_PAIR_H (mt);
	    mt = IDIO_PAIR_T (mt);

	    idio_vm_compile (thr, ia, m1, depth + 1);

	    size_t n = idio_list_length (mt);
	    
	    idio_i_array_t *iac[n];
	    intptr_t i;

	    /*
	     * 1st pass, l is only the accumulated code size for
	     * allocating iar/iat
	     */
	    size_t l = 0;
	    for (i = 0; i < n ; i++) {
		iac[i] = idio_i_array (100);
		idio_vm_compile (thr, iac[i], IDIO_PAIR_H (mt), depth + 1);
		l += iac[i]->i;
		mt = IDIO_PAIR_T (mt);
	    }

	    /*
	     * How much temporary instruction space?
	     * 
	     * largest varuint is 9 bytes, jump ins is one and there
	     * will be n of them.
	     *
	     * To grow the result we need (jump+iac[i]) in iat then
	     * append iar then copy iat backto iar
	     */
	    idio_i_array_t *iar = idio_i_array (l + n * (1 + 9));
	    idio_i_array_t *iat = idio_i_array (l + n * (1 + 9));

	    /*
	     * 2nd pass, l now includes jump sizes
	     */
	    l = 0;
	    for (i = n - 1; i >= 0 ; i--) {
		l += iac[i]->i;
		if (l < IDIO_I_MAX) {
		    idio_i_array_push (iat, IDIO_A_SHORT_JUMP_TRUE);
		    idio_i_array_push (iat, l);
		} else {
		    idio_i_array_t *jf = idio_i_array_compute_varuint (l);
		    idio_i_array_push (iat, IDIO_A_LONG_JUMP_TRUE);
		    idio_i_array_append (iat, jf);
		    idio_i_array_free (jf);	    
		}

		idio_i_array_append (iat, iac[i]);
		idio_i_array_free (iac[i]);

		idio_i_array_append (iat, iar);
		idio_i_array_copy (iar, iat);
		iat->i = 0;
		l = iar->i;
	    }

	    idio_i_array_append (ia, iar);
	    idio_i_array_free (iar);
	    idio_i_array_free (iat);
	}
	break;
    case IDIO_VM_CODE_BEGIN:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 1) {
		idio_vm_error_compile_param_args ("BEGIN m+", mt, IDIO_C_LOCATION ("idio_vm_compile/BEGIN"));
		return;
	    }
	    
	    while (idio_S_nil != mt) {
		idio_vm_compile (thr, ia, IDIO_PAIR_H (mt), depth + 1);
		mt = IDIO_PAIR_T (mt);
	    }
	}
	break;
    case IDIO_VM_CODE_TR_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("TR-FIX-LET m* m+", mt, IDIO_C_LOCATION ("idio_vm_compile/TR-FIX-LET"));
		return;
	    }
	    
	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_FRAME);
	    idio_vm_compile (thr, ia, mp, depth + 1);
	}
	break;
    case IDIO_VM_CODE_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("FIX-LET m* m+", mt, IDIO_C_LOCATION ("idio_vm_compile/FIX-LET"));
		return;
	    }
	    
	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_FRAME);
	    idio_vm_compile (thr, ia, mp, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_UNLINK_FRAME);
	}
	break;
    case IDIO_VM_CODE_PRIMCALL0:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("CALL0 ins", mt, IDIO_C_LOCATION ("idio_vm_compile/CALL0"));
		return;
	    }
	    
	    IDIO ins = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_VM_CODE_PRIMCALL1:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("CALL1 ins m1", mt, IDIO_C_LOCATION ("idio_vm_compile/CALL1"));
		return;
	    }
	    
	    IDIO ins = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_VM_CODE_PRIMCALL2:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("CALL2 ins m1 m2", mt, IDIO_C_LOCATION ("idio_vm_compile/CALL2"));
		return;
	    }
	    
	    IDIO ins = IDIO_PAIR_H (mt);
	    
	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO m2 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));
	    
	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, m2, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POP_REG1);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_VM_CODE_PRIMCALL3:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 4) {
		idio_vm_error_compile_param_args ("CALL3 ins m1 m2 m3", mt, IDIO_C_LOCATION ("idio_vm_compile/CALL3"));
		return;
	    }
	    
	    IDIO ins = IDIO_PAIR_H (mt);
	    
	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO m2 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));
	    IDIO m3 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (IDIO_PAIR_T (mt))));

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, m2, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, m3, depth + 1);
	    IDIO_IA_PUSH2 (IDIO_A_POP_REG2, IDIO_A_POP_REG1);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_VM_CODE_FIX_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("FIX-CLOSURE m+ arity", mt, IDIO_C_LOCATION ("idio_vm_compile/FIX-CLOSURE"));
		return;
	    }
	    
	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (arity)) {
		idio_vm_error_compile_param_type ("fixnum", arity, IDIO_C_LOCATION ("idio_vm_compile/FIX-CLOSURE"));
		return;
	    }

	    /*
	     * Think about the code to be generated where we can only
	     * calculate the length-of #3 when we have added the code
	     * for goto #5 and the code for goto #5 depends on the
	     * code for the-function (which depends on m+).

	      1: ...
	      2: create-closure (length-of #3)
	      3: goto #5
	      4: the-function (from m+)
	      5: ...

	     * XXX if we want the closure object to know about its own
	     * arity (and varargs) then we would need to pass them to
	     * CREATE_CLOSURE:

	      1: ...
	      2: create-closure arity varargs (length-of #3)
	      3: goto #5
	      4: the-function (from m+)
	      5: ...

	     * and have IDIO_A_CREATE_CLOSURE read two more varuints
	     * and then pass them onto idio_closure.  Not a huge change.

	     * Why would we do that?  The only current reason is to
	     * test if a closure is a thunk, ie. arity 0 and no
	     * varargs.  Currently there's no pressing need for a
	     * thunk? predicate.
	     */

	    /* the-function */
	    idio_i_array_t *iap = idio_i_array (100);
	    switch (IDIO_FIXNUM_VAL (arity) + 1) {
	    case 1: idio_i_array_push (iap, IDIO_A_ARITYP1); break;
	    case 2: idio_i_array_push (iap, IDIO_A_ARITYP2); break;
	    case 3: idio_i_array_push (iap, IDIO_A_ARITYP3); break;
	    case 4: idio_i_array_push (iap, IDIO_A_ARITYP4); break;
	    default:
		{
		    idio_i_array_push (iap, IDIO_A_ARITYEQP);
		    idio_i_array_t *a = idio_i_array_compute_varuint (IDIO_FIXNUM_VAL (arity) + 1);
		    idio_i_array_append (iap, a);
		    idio_i_array_free (a);
		}
		break;
	    }
	    idio_i_array_push (iap, IDIO_A_EXTEND_FRAME);
	    idio_vm_compile (thr, iap, mp, depth + 1);
	    idio_i_array_push (iap, IDIO_A_RETURN);

	    if (iap->i < IDIO_I_MAX) {
		/* 2: */
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, 2);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (iap->i);
	    } else {
		/* 2: */
		idio_i_array_t *g5 = idio_i_array_compute_varuint (iap->i);
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, (1 + g5->i));

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_i_array_append (ia, g5);
		idio_i_array_free (g5);
	    }
	    
	    /* 4: */
	    idio_i_array_append (ia, iap);

	    idio_i_array_free (iap);
	}
	break;
    case IDIO_VM_CODE_NARY_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("NARY-CLOSURE m+ arity", mt, IDIO_C_LOCATION ("idio_vm_compile/NARY-CLOSURE"));
		return;
	    }
	    
	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (arity)) {
		idio_vm_error_compile_param_type ("fixnum", arity, IDIO_C_LOCATION ("idio_vm_compile/NARY-CLOSURE"));
		return;
	    }

	    /*
	     * Think about the code to be generated where we can only
	     * calculate the length-of #3 when we have added the code
	     * for goto #5 and the code for goto #5 depends on the
	     * code for the-function (which depends on m+).

	      1: ...
	      2: create-closure (length-of #3)
	      3: goto #5
	      4: the-function
	      5: ...

	     */

	    /* the-function */
	    idio_i_array_t *iap = idio_i_array (100);
	    idio_i_array_push (iap, IDIO_A_ARITYGEP);
	    idio_i_array_t *a = idio_i_array_compute_varuint (IDIO_FIXNUM_VAL (arity) + 1);
	    idio_i_array_append (iap, a);
	    idio_i_array_free (a);

	    idio_i_array_push (iap, IDIO_A_PACK_FRAME);
	    a = idio_i_array_compute_varuint (IDIO_FIXNUM_VAL (arity));
	    idio_i_array_append (iap, a);
	    idio_i_array_free (a);

	    idio_i_array_push (iap, IDIO_A_EXTEND_FRAME);
	    idio_vm_compile (thr, iap, mp, depth + 1);
	    idio_i_array_push (iap, IDIO_A_RETURN);

	    if (iap->i < IDIO_I_MAX) {
		/* 2: */
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, 2);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (iap->i);
	    } else {
		/* 2: */
		idio_i_array_t *g5 = idio_i_array_compute_varuint (iap->i);
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, (1 + g5->i));

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_i_array_append (ia, g5);
		idio_i_array_free (g5);
	    }

	    /* 4: */
	    idio_i_array_append (ia, iap);

	    idio_i_array_free (iap);
	}
	break;
    case IDIO_VM_CODE_TR_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("TR-REGULAR-CALL m1 m*", mt, IDIO_C_LOCATION ("idio_vm_compile/TR-REGULAR-CALL"));
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH2 (IDIO_A_POP_FUNCTION, IDIO_A_FUNCTION_GOTO);
	}
	break;
    case IDIO_VM_CODE_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("REGULAR-CALL m1 m*", mt, IDIO_C_LOCATION ("idio_vm_compile/REGULAR-CALL"));
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH4 (IDIO_A_POP_FUNCTION, IDIO_A_PRESERVE_STATE, IDIO_A_FUNCTION_INVOKE, IDIO_A_RESTORE_STATE);
	}
	break;
    case IDIO_VM_CODE_STORE_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("STORE-ARGUMENT m1 m* rank", mt, IDIO_C_LOCATION ("idio_vm_compile/STORE-ARGUMENT"));
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO rank = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    if (! idio_isa_fixnum (rank)) {
		idio_vm_error_compile_param_type ("fixnum", rank, IDIO_C_LOCATION ("idio_vm_compile/STORE-ARGUMENT"));
		return;
	    }

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms, depth + 1);
	    switch (IDIO_FIXNUM_VAL (rank)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME0); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME1); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME2); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME3); break;
	    default:
		IDIO_IA_PUSH1 (IDIO_A_POP_FRAME);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (rank));
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_CONS_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("CONS-ARGUMENT m1 m* arity", mt, IDIO_C_LOCATION ("idio_vm_compile/CONS-ARGUMENT"));
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POP_CONS_FRAME);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (arity));
	}
	break;
    case IDIO_VM_CODE_ALLOCATE_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("ALLOCATE-FRAME size", mt, IDIO_C_LOCATION ("idio_vm_compile/ALLOCATE-FRAME"));
		return;
	    }
	    
	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_vm_error_compile_param_type ("fixnum", size, IDIO_C_LOCATION ("idio_vm_compile/ALLOCATE-FRAME"));
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (size)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME1); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME2); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME3); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME4); break;
	    case 4: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME5); break;
	    default:
		IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (size) + 1);
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("ALLOCATE-DOTTED-FRAME size", mt, IDIO_C_LOCATION ("idio_vm_compile/ALLOCATE-DOTTED-FRAME"));
		return;
	    }
	    
	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_vm_error_compile_param_type ("fixnum", size, IDIO_C_LOCATION ("idio_vm_compile/ALLOCATE-DOTTED-FRAME"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_DOTTED_FRAME);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (size) + 1);
	}
	break;
    case IDIO_VM_CODE_FINISH:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 0) {
		idio_vm_error_compile_param_args ("FINISH", mt, IDIO_C_LOCATION ("idio_vm_compile/FINISH"));
		return;
	    }
	    
	    IDIO_IA_PUSH1 (IDIO_A_FINISH);
	}
	break;
    case IDIO_VM_CODE_PUSH_DYNAMIC:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("PUSH-DYNAMIC index m", mt, IDIO_C_LOCATION ("idio_vm_compile/PUSH-DYNAMIC"));
		return;
	    }

	    IDIO index = IDIO_PAIR_H (mt);
	    IDIO m = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_DYNAMIC);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (index));
	}
	break;
    case IDIO_VM_CODE_POP_DYNAMIC:
	{
	    if (idio_S_nil != mt) {
		idio_vm_error_compile_param_args ("POP-DYNAMIC", mt, IDIO_C_LOCATION ("idio_vm_compile/POP-DYNAMIC"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_DYNAMIC);
	}
	break;
    case IDIO_VM_CODE_DYNAMIC_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("DYNAMIC-REF index", mt, IDIO_C_LOCATION ("idio_vm_compile/DYNAMIC-REF"));
		return;
	    }

	    IDIO index = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (index));
	}
	break;
    case IDIO_VM_CODE_DYNAMIC_FUNCTION_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("DYNAMIC-FUNCTION-REF index", mt, IDIO_C_LOCATION ("idio_vm_compile/DYNAMIC-FUNCTION-REF"));
		return;
	    }

	    IDIO index = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_FUNCTION_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (index));
	}
	break;
    case IDIO_VM_CODE_PUSH_ENVIRON:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("PUSH-ENVIRON index m", mt, IDIO_C_LOCATION ("idio_vm_compile/PUSH-ENVIRON"));
		return;
	    }

	    IDIO index = IDIO_PAIR_H (mt);
	    IDIO m = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_ENVIRON);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (index));
	}
	break;
    case IDIO_VM_CODE_POP_ENVIRON:
	{
	    if (idio_S_nil != mt) {
		idio_vm_error_compile_param_args ("POP-ENVIRON", mt, IDIO_C_LOCATION ("idio_vm_compile/POP-ENVIRON"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_ENVIRON);
	}
	break;
    case IDIO_VM_CODE_ENVIRON_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("ENVIRON-REF index", mt, IDIO_C_LOCATION ("idio_vm_compile/ENVIRON-REF"));
		return;
	    }

	    IDIO index = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH1 (IDIO_A_ENVIRON_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (index));
	}
	break;
    case IDIO_VM_CODE_PUSH_HANDLER:
	{
	    if (idio_S_nil != mt) {
		idio_vm_error_compile_param_args ("PUSH-HANDLER", mt, IDIO_C_LOCATION ("idio_vm_compile/PUSH-HANDLER"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_HANDLER);
	}
	break;
    case IDIO_VM_CODE_POP_HANDLER:
	{
	    if (idio_S_nil != mt) {
		idio_vm_error_compile_param_args ("POP-HANDLER", mt, IDIO_C_LOCATION ("idio_vm_compile/POP-HANDLER"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_HANDLER);
	}
	break;
    case IDIO_VM_CODE_EXPANDER:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("EXPANDER i m", mt, IDIO_C_LOCATION ("idio_vm_compile/EXPANDER"));
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);
	    IDIO m = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    
	    idio_vm_compile (thr, ia, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXPANDER);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	}
	break;
    case IDIO_VM_CODE_INFIX_OPERATOR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("OPERATOR i p m", mt, IDIO_C_LOCATION ("idio_vm_compile/OPERATOR"));
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);
	    IDIO p = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO m = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));
	    
	    idio_vm_compile (thr, ia, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_INFIX_OPERATOR);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (p));
	}
	break;
    case IDIO_VM_CODE_POSTFIX_OPERATOR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("OPERATOR i p m", mt, IDIO_C_LOCATION ("idio_vm_compile/OPERATOR"));
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);
	    IDIO p = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO m = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));
	    
	    idio_vm_compile (thr, ia, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POSTFIX_OPERATOR);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (p));
	}
	break;
    case IDIO_VM_CODE_NOP:
	break;
    default:
	idio_error_C ("bad instruction", IDIO_LIST1 (mh), IDIO_C_LOCATION ("idio_vm_compile"));
	break;
    }
}

static void idio_vm_code_prologue (idio_i_array_t *ia)
{
    idio_vm_NCE_pc = idio_all_code->i;
    IDIO_IA_PUSH1 (IDIO_A_NON_CONT_ERR);

    idio_vm_FINISH_pc = idio_all_code->i;
    IDIO_IA_PUSH1 (IDIO_A_FINISH);

    idio_vm_CR_pc = idio_all_code->i;
    IDIO_IA_PUSH3 (IDIO_A_RESTORE_HANDLER, IDIO_A_RESTORE_STATE, IDIO_A_RETURN);

    idio_vm_IHR_pc = idio_all_code->i;
    IDIO_IA_PUSH2 (IDIO_A_RESTORE_ALL_STATE, IDIO_A_RETURN);
}

static void idio_vm_invoke (IDIO thr, IDIO func, int tailp);
static void idio_vm_restore_continuation (IDIO k, IDIO val);

/*
 * XXX base_error_handler must not raise an exception otherwise we'll
 * loop forever -- or until the C stack blows up.
 */
IDIO_DEFINE_PRIMITIVE2 ("base-error-handler", base_error_handler, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);
    IDIO_TYPE_ASSERT (boolean, cont);
    IDIO_TYPE_ASSERT (condition, cond); 

    IDIO thr = idio_current_thread ();

    if (idio_isa_condition (cond)) {
	IDIO st = IDIO_STRUCT_INSTANCE_TYPE (cond);
	IDIO stf = IDIO_STRUCT_TYPE_FIELDS (st);
	IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond);

	if (idio_struct_type_isa (st, idio_condition_idio_error_type)) {
	    IDIO eh = idio_current_error_handle ();
	    int printed = 0;

	    idio_display_C ("\n", eh);
	    IDIO m = idio_array_get_index (sif, IDIO_ERROR_TYPE_MESSAGE);
	    if (idio_S_nil != m) {
		idio_display (m, eh);
		printed = 1;
	    }
	    IDIO l = idio_array_get_index (sif, IDIO_ERROR_TYPE_LOCATION);
	    if (idio_S_nil != l) {
		if (printed) {
		    idio_display_C (": ", eh);
		}
		idio_display (l, eh);
		printed = 1;
	    }
	    IDIO d = idio_array_get_index (sif, IDIO_ERROR_TYPE_DETAIL);
	    if (idio_S_nil != d) {
		if (printed) {
		    idio_display_C (": ", eh);
		}
		idio_display (d, eh);
	    }
	    idio_display_C ("\n", eh);
	}

	fprintf (stderr, "\nbase-error-handler -- condition details:\n");
	fprintf (stderr, "%20s: ", "type");
	idio_debug ("%s\n", IDIO_STRUCT_TYPE_NAME (st));

	idio_ai_t al = idio_array_size (stf);
	idio_ai_t ai;
	for (ai = 0; ai < al; ai++) {
	    idio_debug ("%20s: ", idio_array_get_index (stf, ai));
	    idio_debug ("%s\n", idio_array_get_index (sif, ai));
	}
    } else {
	fprintf (stderr, "base-error-handler: expected a condition not a %s\n", idio_type2string (cond));
	idio_debug ("%s\n", cond);
    }

#ifdef IDIO_DEBUG
    idio_debug ("THR %s\n", thr);
    idio_debug ("STK %.1000s\n", IDIO_THREAD_STACK (thr));
#endif

    if (idio_S_true == cont) {
	/* fprintf (stderr, "should continue but won't!\n"); */
	/* idio_debug ("THREAD:\t%s\n", thr); */
	/* idio_debug ("STACK:\t%s\n", IDIO_THREAD_STACK (thr)); */

	/*
	 * An Idio function would RETURN, ie. set the PC to the value
	 * on the top of the stack...
	 */
	IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());
	return idio_S_void;
    }

    fprintf (stderr, "this is a non-continuable condition\n");
    idio_vm_unwind_thread (thr, 1);

    return idio_S_unspec;
}

void idio_vm_codegen (IDIO thr, IDIO m)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (pair, m);
    
    IDIO_THREAD_PC (thr) = idio_all_code->i;
    
    idio_i_array_t *ia = idio_i_array (100);
    
    idio_vm_compile (thr, ia, m, 0);

    /* IDIO_IA_PUSH1 (IDIO_A_RETURN); */

    idio_i_array_append (idio_all_code, ia);
    idio_i_array_free (ia);
}

static uint64_t idio_vm_fetch_varuint (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    int i = IDIO_THREAD_FETCH_NEXT ();
    if (i <= 240) {
	return i;
    } else if (i <= 248) {
	int j = IDIO_THREAD_FETCH_NEXT ();

	return (240 + 256 * (i - 241) + j);
    } else if (249 == i) {
	int j = IDIO_THREAD_FETCH_NEXT ();
	int k = IDIO_THREAD_FETCH_NEXT ();

	return (2288 + 256 * j + k);
    } else {
	int n = (i - 250) + 3;

	uint64_t r = 0;
	for (i = 0; i < n; i++) {
	    r <<= 8;
	    r |= IDIO_THREAD_FETCH_NEXT ();
	}

	return r;
    }
}

/*
 * For a function with varargs, ie.
 *
 * (define (func x & rest) ...)
 *
 * we need to rewrite the call such that the non-mandatory args are
 * bundled up as a list:
 *
 * (func a b c d) => (func a (b c d))
 */
static void idio_vm_listify (IDIO frame, size_t arity)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);
    
    size_t index = IDIO_FRAME_NARGS (frame) - 1;
    IDIO result = idio_S_nil;

    for (;;) {
	if (arity == index) {
	    idio_array_insert_index (IDIO_FRAME_ARGS (frame), result, arity);
	    return;
	} else {
	    result = idio_pair (idio_array_get_index (IDIO_FRAME_ARGS (frame), index - 1),
				result);
	    index--;
	}
    }
}

static void idio_vm_preserve_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_ENVIRON_SP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_DYNAMIC_SP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_HANDLER_SP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_FRAME (thr));
}

static void idio_vm_preserve_all_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_vm_preserve_state (thr);
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_REG1 (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_REG2 (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_FUNC (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
}

static void idio_vm_restore_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_debug ("ivre %s\n", IDIO_THREAD_STACK (thr));  */
    IDIO_THREAD_FRAME (thr) = IDIO_THREAD_STACK_POP ();
    if (idio_S_nil != IDIO_THREAD_FRAME (thr)) {
	IDIO_TYPE_ASSERT (frame, IDIO_THREAD_FRAME (thr));
    }

    IDIO_THREAD_HANDLER_SP (thr) = IDIO_THREAD_STACK_POP ();
    idio_ai_t hsp = IDIO_FIXNUM_VAL (IDIO_THREAD_HANDLER_SP (thr));
    if (hsp < 1) {
	/*
	 * As we've just ascertained we don't have an error handler
	 * this will end in even more tears...
	 */
	idio_error_C ("bad HANDLER SP: < 1", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_LOCATION ("idio_vm_restore_state"));
    }

    if (hsp >= idio_array_size (IDIO_THREAD_STACK (thr))) {
	idio_error_C ("bad HANDLER SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_LOCATION ("idio_vm_restore_state"));
    }

    IDIO_THREAD_DYNAMIC_SP (thr) = IDIO_THREAD_STACK_POP ();
    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
    if (dsp >= idio_array_size (IDIO_THREAD_STACK (thr))) {
	idio_error_C ("bad DYNAMIC SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_LOCATION ("idio_vm_restore_state"));
    }

    IDIO_THREAD_ENVIRON_SP (thr) = IDIO_THREAD_STACK_POP ();

    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
    if (esp >= idio_array_size (IDIO_THREAD_STACK (thr))) {
	idio_error_C ("bad ENVIRON SP: > stack", IDIO_LIST2 (thr, IDIO_THREAD_STACK (thr)), IDIO_C_LOCATION ("idio_vm_restore_state"));
    }

}

static void idio_vm_restore_all_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_debug ("ivras: THR %s\n", thr); */
    /* idio_debug ("ivras: STK %s\n", IDIO_THREAD_STACK (thr)); */
    IDIO_THREAD_VAL (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_FUNC (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_REG2 (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_REG1 (thr) = IDIO_THREAD_STACK_POP ();
    idio_vm_restore_state (thr);
}

static void idio_vm_invoke (IDIO thr, IDIO func, int tailp)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (func);
    IDIO_TYPE_ASSERT (thread, thr);

    switch ((intptr_t) func & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CHARACTER_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    idio_vm_error_function_invoke ("cannot invoke constant type", IDIO_LIST1 (func), IDIO_C_LOCATION ("idio_vm_invoke"));
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	    /* fprintf (stderr, "invoke: tailp=%d closure @%zd ", tailp, IDIO_CLOSURE_CODE (func));  */
	    /* idio_debug ("%s\n", IDIO_FRAME_ARGS (IDIO_THREAD_VAL (thr)));  */

	    if (0 == tailp) {
		IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
	    }
	    IDIO_THREAD_FRAME (thr) = IDIO_CLOSURE_ENV (func);
	    IDIO_THREAD_PC (thr) = IDIO_CLOSURE_CODE (func);

	    if (idio_vm_tracing &&
		0 == tailp) {
		idio_vm_tracing++;
	    }
	}
	break;
    case IDIO_TYPE_PRIMITIVE:
	{
	    /*
	     * PC shenanigans for primitives.
	     *
	     * If we are not in tail position then we should push the
	     * current PC onto the stack so that when the invoked code
	     * calls RETURN it will return to whomever called us -- as
	     * the CLOSURE code does above.
	     *
	     * By and large, though, primitives do not change the PC
	     * as they are entirely within the realm of C.  So we
	     * don't really care if they were called in tail position
	     * or not they just do whatever and set VAL.
	     *
	     * However, (apply proc . args) will prepare some
	     * procedure which may well be a closure which *will*
	     * alter the PC.  (As an aside, apply always invokes proc
	     * in tail position -- as proc *is* in tail position from
	     * apply's point of view).  The closure will, of course,
	     * RETURN to whatever is on top of the stack.
	     *
	     * But we haven't put anything there because this is a
	     * primitive and primitives don't change the PC...
	     *
	     * So, if, after invoking the primitive, the PC has
	     * changed (ie. apply prepared a closure which has set the
	     * PC ready to run the closure when we return from here)
	     * *and* we are not in tail position then we push the
	     * saved pc0 onto the stack.
	     *
	     * NB. If you push PC before calling the primitive (with a
	     * view to popping it off if the PC didn't change) and the
	     * primitive calls idio_raise_condition then there is an
	     * extraneous PC on the stack.
	     */
	    size_t pc0 = IDIO_THREAD_PC (thr); 
	    IDIO val = IDIO_THREAD_VAL (thr);
	    IDIO args_a = IDIO_FRAME_ARGS (val);
	    
	    /* fprintf (stderr, "invoke: primitive: %s arity=%zd%s: nargs=%zd/%zd: ", IDIO_PRIMITIVE_NAME (func),  IDIO_PRIMITIVE_ARITY (func), IDIO_PRIMITIVE_VARARGS (func) ? "+" : "", idio_array_size (args_a), IDIO_FRAME_NARGS (val)); */
	    /* idio_debug ("%s\n", args_a); */

	    IDIO last = idio_array_pop (args_a);
	    IDIO_FRAME_NARGS (val) -= 1;

	    if (idio_S_nil != last) {
		idio_error_C ("primitive: varargs?", IDIO_LIST1 (last), IDIO_C_LOCATION ("idio_vm_invoke"));
	    }

	    switch (IDIO_PRIMITIVE_ARITY (func)) {
	    case 0:
		{
		    IDIO args = idio_array_to_list (args_a);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (args);
		}
		break;
	    case 1:
		{
		    IDIO arg1 = idio_array_shift (args_a);
		    IDIO args = idio_array_to_list (args_a);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, args);
		}
		break;
	    case 2:
		{
		    IDIO arg1 = idio_array_shift (args_a);
		    IDIO arg2 = idio_array_shift (args_a);
		    IDIO args = idio_array_to_list (args_a);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, args);
		}
		break;
	    case 3:
		{
		    IDIO arg1 = idio_array_shift (args_a);
		    IDIO arg2 = idio_array_shift (args_a);
		    IDIO arg3 = idio_array_shift (args_a);
		    IDIO args = idio_array_to_list (args_a);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, args);
		}
		break;
	    case 4:
		{
		    IDIO arg1 = idio_array_shift (args_a);
		    IDIO arg2 = idio_array_shift (args_a);
		    IDIO arg3 = idio_array_shift (args_a);
		    IDIO arg4 = idio_array_shift (args_a);
		    IDIO args = idio_array_to_list (args_a);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, arg4, args);
		}
		break;
	    case 5:
		{
		    IDIO arg1 = idio_array_shift (args_a);
		    IDIO arg2 = idio_array_shift (args_a);
		    IDIO arg3 = idio_array_shift (args_a);
		    IDIO arg4 = idio_array_shift (args_a);
		    IDIO arg5 = idio_array_shift (args_a);
		    IDIO args = idio_array_to_list (args_a);
		    IDIO_THREAD_VAL (thr) = (IDIO_PRIMITIVE_F (func)) (arg1, arg2, arg3, arg4, arg5, args);
		}
		break;
	    default:
		idio_vm_error_function_invoke ("arity unexpected", IDIO_LIST2 (func, args_a), IDIO_C_LOCATION ("idio_vm_invoke"));
		break;
	    }

	    size_t pc = IDIO_THREAD_PC (thr); 

	    if (0 == tailp &&
		pc != pc0) {
		IDIO_THREAD_STACK_PUSH (idio_fixnum (pc0));
	    }
	    
	    if (idio_vm_tracing) {
		fprintf (stderr, "                               %*.s", idio_vm_tracing, "");
		/* XXX - why is idio_vm_tracing one less hence an extra space? */
		idio_debug (" => %s\n", IDIO_THREAD_VAL (thr));
	    }

	    return;
	}
	break;
    case IDIO_TYPE_CONTINUATION:
	{
	    IDIO val = IDIO_THREAD_VAL (thr);
	    IDIO args_a = IDIO_FRAME_ARGS (val);
	    
	    IDIO last = idio_array_pop (args_a);
	    IDIO_FRAME_NARGS (val) -= 1;

	    if (idio_S_nil != last) {
		idio_error_C ("continuation: varargs?", IDIO_LIST1 (last), IDIO_C_LOCATION ("idio_vm_invoke"));
	    }

	    if (IDIO_FRAME_NARGS (val) != 1) {
		idio_vm_error_function_invoke ("unary continuation", IDIO_LIST2 (func, args_a), IDIO_C_LOCATION ("idio_vm_invoke"));
	    }
	    
	    idio_vm_restore_continuation (func, idio_array_get_index (args_a, 0));
	}
	break;
    case IDIO_TYPE_SYMBOL:
	{
	    char *pathname = idio_command_find_exe (func);
	    if (NULL != pathname) {
		IDIO_THREAD_VAL (thr) = idio_command_invoke (func, thr, pathname);
		free (pathname);
	    } else {
		idio_vm_error_function_invoke ("command not found",
					       IDIO_LIST2 (func, IDIO_FRAME_ARGS (IDIO_THREAD_VAL (thr))),
					       IDIO_C_LOCATION ("idio_vm_invoke"));
	    }
	}
	break;
    default:
	{
	    idio_vm_error_function_invoke ("cannot invoke",
					   IDIO_LIST2 (func, IDIO_FRAME_ARGS (IDIO_THREAD_VAL (thr))),
					   IDIO_C_LOCATION ("idio_vm_invoke"));
	}
	break;
    }
}

/*
 * Given a command as a list, (foo bar baz), run the code
 *
 * WARNING: in the calling environment idio_gc_protect() any IDIO
 * objects you want to use after calling this function (as it may call
 * idio_gc_collect())
 */
IDIO idio_vm_invoke_C (IDIO thr, IDIO command)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (command);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
    idio_vm_preserve_all_state (thr);

    switch (command->type) {
    case IDIO_TYPE_PAIR:
	{
	    /*
	     * (length command) will give us the +1 frame allocation we need
	     * because it will allocate a slot for the command name even
	     * though it won't go there.
	     */
	    IDIO vs = idio_frame_allocate (idio_list_length (command));
	    idio_ai_t fai;
	    IDIO args = IDIO_PAIR_T (command);
	    for (fai = 0; idio_S_nil != args; fai++) {
		idio_frame_update (vs, 0, fai, IDIO_PAIR_H (args));
		args = IDIO_PAIR_T (args);
	    }
	    IDIO_THREAD_VAL (thr) = vs;
	    /* IDIO func = idio_module_current_symbol_value (IDIO_PAIR_H (command)); */
	    idio_vm_invoke (thr, IDIO_PAIR_H (command), IDIO_VM_INVOKE_TAIL_CALL);
	}
	break;
    case IDIO_TYPE_CLOSURE:
	{
	    /*
	     * Must be a thunk
	     */
	    IDIO vs = idio_frame_allocate (1);
	    IDIO_THREAD_VAL (thr) = vs;
	    idio_vm_invoke (thr, command, IDIO_VM_INVOKE_TAIL_CALL);
	}
    }
    
    idio_vm_run (thr);
    IDIO r = IDIO_THREAD_VAL (thr);

    idio_vm_restore_all_state (thr);
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());

    return r;
}

static void idio_vm_push_dynamic (idio_ai_t index, IDIO thr, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_DYNAMIC_SP (thr));
    idio_array_push (stack, val);
    IDIO_THREAD_DYNAMIC_SP (thr) = idio_fixnum (idio_array_size (stack));
    idio_array_push (stack, idio_fixnum (index));
}

static void idio_vm_pop_dynamic (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_pop (stack);
    idio_array_pop (stack);
    IDIO_THREAD_DYNAMIC_SP (thr) = idio_array_pop (stack);
}

IDIO idio_vm_dynamic_ref (idio_ai_t index, IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));

    for (;;) {
	if (sp >= 0) {
	    IDIO si = idio_array_get_index (stack, sp);
	    
	    if (IDIO_FIXNUM_VAL (si) == index) {
		return idio_array_get_index (stack, sp - 1);
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    IDIO v = idio_vm_values_ref (index);

	    if (idio_S_undef == v) {
		idio_error_dynamic_unbound (index, IDIO_C_LOCATION ("idio_vm_dynamic_ref"));
	    }

	    return v;
	}
    }
}

void idio_vm_dynamic_set (idio_ai_t index, IDIO v, IDIO thr)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));

    for (;;) {
	if (sp >= 0) {
	    IDIO si = idio_array_get_index (stack, sp);
	    
	    if (IDIO_FIXNUM_VAL (si) == index) {
		idio_array_insert_index (stack, v, sp - 1);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    idio_array_insert_index (idio_vm_values, v, index);
	    break;
	}
    }
}

static void idio_vm_push_environ (idio_ai_t index, IDIO thr, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_ENVIRON_SP (thr));
    idio_array_push (stack, val);
    IDIO_THREAD_ENVIRON_SP (thr) = idio_fixnum (idio_array_size (stack));
    idio_array_push (stack, idio_fixnum (index));
}

static void idio_vm_pop_environ (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_pop (stack);
    idio_array_pop (stack);
    IDIO_THREAD_ENVIRON_SP (thr) = idio_array_pop (stack);
}

IDIO idio_vm_environ_ref (idio_ai_t index, IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));

    for (;;) {
	if (sp >= 0) {
	    IDIO si = idio_array_get_index (stack, sp);
	    
	    if (IDIO_FIXNUM_VAL (si) == index) {
		return idio_array_get_index (stack, sp - 1);
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    IDIO v = idio_vm_values_ref (index);

	    if (idio_S_undef == v) {
		idio_error_environ_unbound (index, IDIO_C_LOCATION ("idio_vm_environ_ref"));
	    }

	    return v;
	}
    }
}

void idio_vm_environ_set (idio_ai_t index, IDIO v, IDIO thr)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));

    for (;;) {
	if (sp >= 0) {
	    IDIO si = idio_array_get_index (stack, sp);
	    
	    if (IDIO_FIXNUM_VAL (si) == index) {
		idio_array_insert_index (stack, v, sp - 1);
		break;
	    } else {
		sp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, sp - 2));
	    }
	} else {
	    idio_array_insert_index (idio_vm_values, v, index);
	    break;
	}
    }
}

void idio_vm_computed_ref (idio_ai_t index)
{
    IDIO gns = idio_array_get_index (idio_vm_values, index);
    
    if (idio_isa_pair (gns)) {
	IDIO get = IDIO_PAIR_H (gns);
	if (idio_isa_primitive (get) ||
	    idio_isa_closure (get)) {
	    idio_apply (get, IDIO_LIST1 (idio_S_nil));
	} else {
	    idio_vm_error_computed ("no get accessor", index, IDIO_C_LOCATION ("idio_vm_computed_ref"));
	}
    } else {
	idio_vm_error_computed ("no accessors", index, IDIO_C_LOCATION ("idio_vm_computed_ref"));
    }
}

void idio_vm_computed_set (idio_ai_t index, IDIO v)
{
    IDIO_ASSERT (v);

    IDIO gns = idio_array_get_index (idio_vm_values, index);
    
    if (idio_isa_pair (gns)) {
	IDIO set = IDIO_PAIR_T (gns);
	if (idio_isa_primitive (set) ||
	    idio_isa_closure (set)) {
	    idio_apply (set, IDIO_LIST1 (v));
	} else {
	    idio_vm_error_computed ("no set accessor", index, IDIO_C_LOCATION ("idio_vm_computed_set"));
	}
    } else {
	idio_vm_error_computed ("no accessors", index, IDIO_C_LOCATION ("idio_vm_computed_set"));
    }
}

static void idio_vm_push_handler (IDIO thr, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_HANDLER_SP (thr));
    IDIO_THREAD_HANDLER_SP (thr) = idio_fixnum (idio_array_size (stack));
    idio_array_push (stack, val);
}

static void idio_vm_pop_handler (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    /* idio_debug ("POPH: %s\n", stack); */
    
    idio_array_pop (stack);
    IDIO_THREAD_HANDLER_SP (thr) = idio_array_pop (stack);
}

static void idio_vm_restore_handler (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    /* idio_debug ("RESTOREH: %s\n", thr);  */
    /* idio_debug ("RESTOREH: %s\n", stack);  */
    /* idio_debug ("RESTOREH: %s -> ", IDIO_THREAD_HANDLER_SP (thr)); */
    IDIO_THREAD_HANDLER_SP (thr) = idio_array_pop (stack);
    /* idio_debug ("%s\n", IDIO_THREAD_HANDLER_SP (thr)); */
}

void idio_raise_condition (IDIO continuablep, IDIO e)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (e);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    /* idio_debug ("\n\nsignal-exception: %s", continuablep);  */
    /* idio_debug (" %s\n", e);  */

    IDIO thr = idio_current_thread ();
    /* idio_debug ("SE: %s\n", thr); */
    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t handler_sp = IDIO_FIXNUM_VAL (IDIO_THREAD_HANDLER_SP (thr));

    if (handler_sp >= idio_array_size (stack)) {
	idio_vm_panic (thr, "handler SP >= sizeof (stack)");
    }
    IDIO handler = idio_array_get_index (stack, handler_sp);

    IDIO vs = idio_frame (idio_S_nil, IDIO_LIST2 (continuablep, e));
    IDIO_THREAD_VAL (thr) = vs;

    idio_array_push (stack, idio_fixnum (IDIO_THREAD_PC (thr)));
    idio_vm_preserve_state (thr);

    /*
     * We need to run this code in the care of the next handler on the
     * stack (not the current handler).  Unless the next handler is
     * the base handler in which case it gets reused (ad infinitum).
     *
     * We can do that easily by just changing IDIO_THREAD_HANDLER_SP
     * but if that handler RETURNs then we must restore the current
     * handler.
     */
    idio_array_push (stack, IDIO_THREAD_HANDLER_SP (thr));
    IDIO_THREAD_HANDLER_SP (thr) = idio_array_get_index (stack, handler_sp - 1);

    /*
     * Whether we are continuable or not determines where in the
     * prologue we set the PC for the RETURNee.
     */
    if (idio_S_true == continuablep) {
	/* idio_debug ("SE: continuable %s\n", e); */
	/* idio_debug ("SE: continuable %s\n", thr); */
	idio_array_push (stack, idio_fixnum (idio_vm_CR_pc)); /* => RESTORE-HANDLER, RESTORE-FRAME, RETURN */
    } else {
	idio_array_push (stack, idio_fixnum (idio_vm_NCE_pc)); /* => NON-CONT-ERR */
    }

    /* God speed! */
    idio_vm_invoke (thr, handler, IDIO_VM_INVOKE_TAIL_CALL);

    /*
     * Actually, for a user-defined error handler, which will be a
     * closure, idio_vm_invoke did nothing much, the error
     * handling closure will only be run when we continue looping
     * around idio_vm_run1.
     *
     * Wait, though, consider how we've got to here.  Some
     * (user-level) code called a C primitive which stumbled over an
     * erroneous condition.  That C code called an idio_*error*
     * routine which eventually called us, idio_raise_condition.
     *
     * Whilst we are ready to process the OOB exception handler in
     * Idio-land, the C language part of us still thinks we've in a
     * regular function call tree from the point of origin of the
     * error, ie. we still have a trail of C language frames back
     * through the caller (of idio_raise_condition) -- and, in turn,
     * back up through to idio_vm_run1 where the C primitive was first
     * called from.  We need to make that trail disappear otherwise
     * either we'll accidentally call C's 'return' back down that C
     * stack (erroneously) or, after enough idio_raise_condition()s,
     * we'll blow up the C stack.
     *
     * That means longjmp(3).
     *
     * XXX longjmp means that we won't be free()ing any memory
     * allocated during the life of that C stack.  Unless we think of
     * something clever...well?...er, still waiting...
     */
    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
	longjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_LONGJMP_SIGNAL_EXCEPTION);
    } else {
	fprintf (stderr, "WARNING: raise: unable to use jmp_buf\n");
	return;
    }

    /* not reached */
    IDIO_C_ASSERT (0);
}

IDIO_DEFINE_PRIMITIVE2 ("raise", raise, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);
    IDIO_VERIFY_PARAM_TYPE (boolean, cont);
    IDIO_VERIFY_PARAM_TYPE (condition, cond);

    idio_raise_condition (cont, cond);

    /* notreached */
    return idio_S_unspec;
}

IDIO idio_apply (IDIO fn, IDIO args)
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (args);

    /* idio_debug ("apply: %s", fn);    */
    /* idio_debug (" %s\n", args);    */

    size_t nargs = idio_list_length (args);
    
    /*
     * (apply + 1 2 '(3 4 5))
     *
     * fn == +
     * args == (1 2 (3 4 5))
     *
     * nargs == 3
     *
     * size => (nargs - 1) + len (args[nargs-1])
     */

    IDIO larg = args;
    while (idio_S_nil != larg &&
	   idio_S_nil != IDIO_PAIR_T (larg)) {
	larg = IDIO_PAIR_T (larg);
    }
    if (idio_S_nil != larg) {
	larg = IDIO_PAIR_H (larg);
    }

    /* idio_debug ("apply: %s", fn);   */
    /* idio_debug (" larg=%s\n", larg);    */
    
    size_t size = (nargs - 1) + idio_list_length (larg);

    /* idio_debug ("apply: %s", fn);   */
    /* fprintf (stderr, " -> %zd args\n", size);    */
    
    IDIO vs = idio_frame_allocate (size + 1);

    idio_ai_t vsi;
    for (vsi = 0; vsi < nargs - 1; vsi++) {
	idio_frame_update (vs, 0, vsi, IDIO_PAIR_H (args));
	args = IDIO_PAIR_T (args);
    }
    args = larg;
    for (; idio_S_nil != args; vsi++) {
	idio_frame_update (vs, 0, vsi, IDIO_PAIR_H (args));
	args = IDIO_PAIR_T (args);
    }
    
    IDIO thr = idio_current_thread ();
    IDIO_THREAD_VAL (thr) = vs;

    idio_vm_invoke (thr, fn, IDIO_VM_INVOKE_TAIL_CALL);

    return IDIO_THREAD_VAL (thr);
}

IDIO_DEFINE_PRIMITIVE1V ("apply", apply, (IDIO fn, IDIO args))
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (args);

    return idio_apply (fn, args);
}

IDIO_DEFINE_PRIMITIVE0 ("%%make-continuation", make_continuation, ())
{
    IDIO thr = idio_current_thread ();

    IDIO k = idio_continuation (thr);
    
    return k;
}

static void idio_vm_restore_continuation (IDIO k, IDIO val)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);

    IDIO thr = idio_current_thread ();

    /*
     * *copy* the stack -- in case this continuation is used again
     */
    /* idio_debug ("K restore: %s\n", k); */
    /* idio_debug ("K restore: %s\n", IDIO_CONTINUATION_STACK (k)); */
    IDIO_THREAD_STACK (thr) = idio_array_copy (IDIO_CONTINUATION_STACK (k), 0);
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());
    IDIO_C_ASSERT (IDIO_THREAD_PC (thr) < idio_all_code->i);
    idio_vm_restore_state (thr);

    IDIO_THREAD_VAL (thr) = val;
    IDIO_THREAD_JMP_BUF (thr) = IDIO_CONTINUATION_JMP_BUF (k);
    
    /* idio_debug ("K restore: THR %s\n", thr); */
    /* idio_debug ("K restore: STK %s\n", IDIO_THREAD_STACK (thr)); */

    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
	longjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_LONGJMP_CONTINUATION);
    } else {
	fprintf (stderr, "WARNING: restore-continuation: unable to use jmp_buf\n");
	return;
    }
}

IDIO_DEFINE_PRIMITIVE2 ("%%restore-continuation", restore_continuation, (IDIO k, IDIO val))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (continuation, k);
    
    idio_vm_restore_continuation (k, val);
    
    /* not reached */
    IDIO_C_ASSERT (0);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("%%call/cc", call_cc, (IDIO proc))
{
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (closure, proc);

    IDIO thr = idio_current_thread ();

    IDIO k = idio_continuation (thr);

    /* idio_debug ("%%%%call/cc: %s\n", k); */
    
    IDIO_THREAD_VAL (thr) = idio_frame (IDIO_THREAD_VAL (thr), IDIO_LIST1 (k));

    /* idio_debug ("%%%%call/cc: THR %s\n", thr); */
    /* idio_debug ("%%%%call/cc: STK %s\n", IDIO_THREAD_STACK (thr)); */
    
    idio_vm_invoke (thr, proc, IDIO_VM_INVOKE_REGULAR);

    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
	longjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_LONGJMP_CALLCC);
    } else {
	fprintf (stderr, "WARNING: %%call/cc: unable to use jmp_buf\n");
	return idio_S_unspec;
    }

    /* not reached */
    IDIO_C_ASSERT (0);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("%%vm-trace", vm_trace, (IDIO trace))
{
    IDIO_ASSERT (trace);

    IDIO_VERIFY_PARAM_TYPE (fixnum, trace);

    idio_vm_tracing = IDIO_FIXNUM_VAL (trace);
    
    return idio_S_unspec;
}

#ifdef IDIO_DEBUG
IDIO_DEFINE_PRIMITIVE1 ("%%vm-dis", vm_dis, (IDIO dis))
{
    IDIO_ASSERT (dis);

    IDIO_VERIFY_PARAM_TYPE (fixnum, dis);

    idio_vm_dis = IDIO_FIXNUM_VAL (dis);
    
    return idio_S_unspec;
}

#define IDIO_VM_RUN_DIS(...)	if (idio_vm_dis) { fprintf (stderr, __VA_ARGS__); }
#else
#define IDIO_VM_RUN_DIS(...)	((void) 0)
#endif

static void idio_vm_function_trace (IDIO_I ins, IDIO thr)
{
    IDIO func = IDIO_THREAD_FUNC (thr);
    IDIO val = IDIO_THREAD_VAL (thr);
    IDIO args = idio_S_nil;
    if (IDIO_FRAME_NARGS (val) > 1) {
	IDIO fargs = idio_array_copy (IDIO_FRAME_ARGS (val), 0);
	idio_array_pop (fargs);
	args = idio_array_to_list (fargs);
    }
    IDIO expr = idio_list_append2 (IDIO_LIST1 (func), args);

    /*
     * %7zd	- PC of ins
     * SPACE
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent
     * %s	- expression
     */
    fprintf (stderr, "%7zd ", IDIO_THREAD_PC (thr) - 1);

    if (idio_isa_closure (func)) {
	IDIO name = idio_hash_get (idio_vm_closure_name, idio_fixnum (IDIO_CLOSURE_CODE (func)));
	if (idio_S_unspec != name) {
	    idio_debug ("%20s ", name);
	} else {
	    fprintf (stderr, "              -anon- ");
	}
    } else {
	fprintf (stderr, "                     ");
    }

    switch (ins) {
    case IDIO_A_FUNCTION_GOTO:
	fprintf (stderr, "TC ");
	break;
    case IDIO_A_FUNCTION_INVOKE:
	fprintf (stderr, "   ");
	break;
    }
    fprintf (stderr, "%*.s", idio_vm_tracing, "");
    idio_debug ("%s", expr);
    fprintf (stderr, "\n");
}

static void idio_vm_primitive_call_trace (char *name, IDIO thr, int nargs)
{
    /*
     * %7zd	- PC of ins
     * SPACE
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent (>= 1)
     * %s	- expression
     */
    fprintf (stderr, "%7zd ", IDIO_THREAD_PC (thr) - 1);

    fprintf (stderr, "        __primcall__    ");

    fprintf (stderr, "%*.s", idio_vm_tracing, "");
    fprintf (stderr, "(%s", name);
    if (nargs > 1) {
	idio_debug (" %s", IDIO_THREAD_REG1 (thr));
    }
    if (nargs > 0) {
	idio_debug (" %s", IDIO_THREAD_VAL (thr));
    }
    fprintf (stderr, ")\n");
}

static void idio_vm_primitive_result_trace (IDIO thr)
{
    IDIO val = IDIO_THREAD_VAL (thr);

    /*
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent (>= 1)
     * %s	- expression
     */
    fprintf (stderr, "                                ");

    fprintf (stderr, "%*.s", idio_vm_tracing, "");
    idio_debug ("=> %s\n", val);
}

int idio_vm_run1 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (IDIO_THREAD_PC(thr) > idio_all_code->i) {
	fprintf (stderr, "\n\nPC %" PRIdPTR " > max code PC %" PRIdPTR"\n", IDIO_THREAD_PC (thr), idio_all_code->i);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %.1000s\n", IDIO_THREAD_STACK (thr));
	idio_vm_panic (thr, "bad PC!");
    }
    IDIO_I ins = IDIO_THREAD_FETCH_NEXT ();

    IDIO_VM_RUN_DIS ("idio_vm_run1: %p %3d: ", thr, ins);
    
    switch (ins) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 0");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), 0, 0);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 1");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), 0, 1);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 2");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), 0, 2);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 3");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), 0, 3);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:
	{
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF %" PRId64 "", j);
	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), 0, j);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-REF %" PRId64 " %" PRId64 "", i, j);
	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_FRAME (thr), i, j);
	}
	break;
    case IDIO_A_GLOBAL_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO sym = idio_vm_symbols_ref (i); 
	    IDIO_VM_RUN_DIS ("GLOBAL-REF %" PRId64 " %s", i, IDIO_SYMBOL_S (sym)); 
	    IDIO_THREAD_VAL (thr) = idio_vm_values_ref (i);
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO sym = idio_vm_symbols_ref (i); 
	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-REF %" PRId64 " %s", i, IDIO_SYMBOL_S (sym)); 
	    IDIO val = idio_vm_values_ref (i);
	    if (idio_S_undef == val) {
		/*
		idio_debug ("\nCHECKED-GLOBAL-REF: %s", sym);
		fprintf (stderr, " #%" PRId64, i);
		idio_debug (" #undef in symbols %s\n", idio_vm_symbols);
		idio_dump (thr, 2);
		idio_debug ("c-m: %s\n", idio_current_module ());
		idio_debug ("/main symbols %s\n", idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (idio_main_module ())));
		*/
		/* idio_error_printf ("undefined toplevel: %" PRId64 "", i); */

		IDIO_THREAD_VAL (thr) = sym; 
	    } else if (idio_S_unspec == val) {
		idio_debug ("\nCHECKED-GLOBAL-REF: %s", sym); 
		fprintf (stderr, " #%" PRId64, i);
		idio_debug (" #unspec in symbols %s\n", idio_vm_symbols);
		idio_dump (thr, 2);
		idio_debug ("c-m: %s\n", idio_current_module ());
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/CHECKED-GLOBAL-REF"), "unspecified toplevel: %" PRId64 "", i);
	    } else {
		IDIO_THREAD_VAL (thr) = val;
	    }
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_FUNCTION_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO sym = idio_vm_symbols_ref (i); 
	    IDIO_VM_RUN_DIS ("CHECKED-GLOBAL-FUNCTION-REF %" PRId64 " %s", i, IDIO_SYMBOL_S (sym)); 
	    IDIO val = idio_vm_values_ref (i);
	    if (idio_S_undef == val) {
		/*
		idio_debug ("\nCHECKED-GLOBAL-FUNCTION-REF: %s", sym);
		fprintf (stderr, " #%" PRId64, i);
		idio_debug (" #undef in symbols %s\n", idio_vm_symbols);
		idio_dump (thr, 2);
		idio_debug ("c-m: %s\n", idio_current_module ());
		*/
		/*
		idio_error_printf ("undefined toplevel: %" PRId64 "", i);
		*/
		IDIO_THREAD_VAL (thr) = sym; 
	    } else if (idio_S_unspec == val) {
		idio_debug ("\nCHECKED-GLOBAL-FUNCTION-REF:) %s", sym); 
		fprintf (stderr, " #%" PRId64, i);
		idio_debug (" #unspec in symbols %s\n", idio_vm_symbols);
		idio_dump (thr, 2);
		idio_debug ("c-m: %s\n", idio_current_module ());
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/CHECKED-GLOBAL-FUNCTION-REF"), "unspecified toplevel: %" PRId64 "", i);
	    } else {
		IDIO_THREAD_VAL (thr) = val;
	    }
	}
	break;
    case IDIO_A_CONSTANT_REF: 
    	{ 
    	    idio_ai_t i = idio_vm_fetch_varuint (thr); 
    	    IDIO_VM_RUN_DIS ("CONSTANT %td", i); 
    	    IDIO_THREAD_VAL (thr) = idio_vm_constants_ref (i); 
    	} 
    	break; 
    case IDIO_A_COMPUTED_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO sym = idio_vm_symbols_ref (i); 
	    IDIO_VM_RUN_DIS ("COMPUTED-REF %" PRId64 " %s", i, IDIO_SYMBOL_S (sym)); 
	    idio_vm_computed_ref (i);
	}
	break;
    case IDIO_A_PREDEFINED0:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 0 #t");
	    IDIO_THREAD_VAL (thr) = idio_S_true;
	}
	break;
    case IDIO_A_PREDEFINED1:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 1 #f");
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	}
	break;
    case IDIO_A_PREDEFINED2:
	{
	    IDIO_VM_RUN_DIS ("PREDEFINED 2 #nil");
	    IDIO_THREAD_VAL (thr) = idio_S_nil;
	}
	break;
    case IDIO_A_PREDEFINED3:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (3);
	    idio_error_C ("predef 3", idio_S_nil, IDIO_C_LOCATION ("idio_vm_run1/PREDEFINED3"));
	}
	break;
    case IDIO_A_PREDEFINED4:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (4);
	    idio_error_C ("predef 4", idio_S_nil, IDIO_C_LOCATION ("idio_vm_run1/PREDEFINED4"));
	}
	break;
    case IDIO_A_PREDEFINED5:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (5);
	    idio_error_C ("predef 5", idio_S_nil, IDIO_C_LOCATION ("idio_vm_run1/PREDEFINED5"));
	}
	break;
    case IDIO_A_PREDEFINED6:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (6);
	    idio_error_C ("predef 6", idio_S_nil, IDIO_C_LOCATION ("idio_vm_run1/PREDEFINED6"));
	}
	break;
    case IDIO_A_PREDEFINED7:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (7);
	    idio_error_C ("predef 7", idio_S_nil, IDIO_C_LOCATION ("idio_vm_run1/PREDEFINED7"));
	}
	break;
    case IDIO_A_PREDEFINED8:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (8);
	    idio_error_C ("predef 8", idio_S_nil, IDIO_C_LOCATION ("idio_vm_run1/PREDEFINED8"));
	}
	break;
    case IDIO_A_PREDEFINED:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO pd = idio_vm_primitives_ref (i);
	    IDIO_VM_RUN_DIS ("PREDEFINED %" PRId64 " %s", i, IDIO_PRIMITIVE_NAME (pd));
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (i);
	}
	break;
    case IDIO_A_FINISH:
	{
	    /* invoke exit handler... */
	    IDIO_VM_RUN_DIS ("FINISH\n");
	    return 0;
	}
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET0:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 0");
	idio_frame_update (IDIO_THREAD_FRAME (thr), 0, 0, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 1");
	idio_frame_update (IDIO_THREAD_FRAME (thr), 0, 1, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 2");
	idio_frame_update (IDIO_THREAD_FRAME (thr), 0, 2, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 3");
	idio_frame_update (IDIO_THREAD_FRAME (thr), 0, 3, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET %" PRId64 "", i);
	    idio_frame_update (IDIO_THREAD_FRAME (thr), 0, i, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-SET %" PRId64 " %" PRId64 "", i, j);
	    idio_frame_update (IDIO_THREAD_FRAME (thr), i, j, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_GLOBAL_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO sym = idio_vm_symbols_ref (i); 
	    IDIO_VM_RUN_DIS ("GLOBAL-SET %" PRId64 " %s", i, IDIO_SYMBOL_S (sym)); 
	    IDIO val = IDIO_THREAD_VAL (thr);
	    idio_vm_values_set (i, val);

	    if (idio_isa_closure (val)) {
		idio_hash_put (idio_vm_closure_name, idio_fixnum (IDIO_CLOSURE_CODE (val)), sym);
	    }
	}
	break;
    case IDIO_A_COMPUTED_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO sym = idio_vm_symbols_ref (i); 
	    IDIO_VM_RUN_DIS ("COMPUTED-SET %" PRId64 " %s", i, IDIO_SYMBOL_S (sym)); 
	    IDIO val = IDIO_THREAD_VAL (thr);
	    idio_vm_computed_set (i, val);
	}
	break;
    case IDIO_A_LONG_GOTO:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-GOTO %" PRId64 "", i);
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_LONG_JUMP_FALSE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-JUMP-FALSE %" PRId64 "", i);
	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_LONG_JUMP_TRUE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("LONG-JUMP-TRUE %" PRId64 "", i);
	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_GOTO:
	{
	    int i = IDIO_THREAD_FETCH_NEXT ();
	    IDIO_VM_RUN_DIS ("SHORT-GOTO %d", i);
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_SHORT_JUMP_FALSE:
	{
	    int i = IDIO_THREAD_FETCH_NEXT ();
	    IDIO_VM_RUN_DIS ("SHORT-JUMP-FALSE %d", i);
	    if (idio_S_false == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_JUMP_TRUE:
	{
	    int i = IDIO_THREAD_FETCH_NEXT ();
	    IDIO_VM_RUN_DIS ("SHORT-JUMP-TRUE %d", i);
	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_EXTEND_FRAME:
	{
	    IDIO_VM_RUN_DIS ("EXTEND-FRAME");
	    IDIO_THREAD_FRAME (thr) = idio_frame_extend (IDIO_THREAD_FRAME (thr), IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_UNLINK_FRAME:
	{
	    IDIO_VM_RUN_DIS ("UNLINK-FRAME");
	    IDIO_THREAD_FRAME (thr) = IDIO_FRAME_NEXT (IDIO_THREAD_FRAME (thr));
	}
	break;
    case IDIO_A_PUSH_VALUE:
	{
	    IDIO_VM_RUN_DIS ("PUSH-VALUE");
	    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_VALUE:
	{
	    IDIO_VM_RUN_DIS ("POP-VALUE");
	    IDIO_THREAD_VAL (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_POP_REG1:
	{
	    IDIO_VM_RUN_DIS ("POP-REG1");
	    IDIO_THREAD_REG1 (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_POP_REG2:
	{
	    IDIO_VM_RUN_DIS ("POP-REG2");
	    IDIO_THREAD_REG2 (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_PRESERVE_STATE:
	{
	    IDIO_VM_RUN_DIS ("PRESERVE-STATE");
	    idio_vm_preserve_state (thr);
	}
	break;
    case IDIO_A_RESTORE_STATE:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-STATE");
	    idio_vm_restore_state (thr);
	}
	break;
    case IDIO_A_RESTORE_ALL_STATE:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-ALL-STATE");
	    idio_vm_restore_all_state (thr);
	}
	break;
    case IDIO_A_POP_FUNCTION:
	{
	    IDIO_VM_RUN_DIS ("POP-FUNCTION");
	    IDIO_THREAD_FUNC (thr) = IDIO_THREAD_STACK_POP ();
	}
	break;
    case IDIO_A_CREATE_CLOSURE:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CREATE-CLOSURE @ +%" PRId64 "", i);
	    IDIO_THREAD_VAL (thr) = idio_closure (IDIO_THREAD_PC (thr) + i, IDIO_THREAD_FRAME (thr));
	}
	break;
    case IDIO_A_RETURN:
	{
	    IDIO_VM_RUN_DIS ("RETURN");
	    IDIO ipc = IDIO_THREAD_STACK_POP ();
	    if (! IDIO_TYPE_FIXNUMP (ipc)) {
		idio_debug ("RETURN {fixnum}: not %s\n", ipc);
		idio_debug ("THR %s\n", thr);
		idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
		idio_error_C ("RETURN: not a number", IDIO_LIST1 (ipc), IDIO_C_LOCATION ("idio_vm_run1/RETURN"));
	    }
	    idio_ai_t pc = IDIO_FIXNUM_VAL (ipc);
	    if (pc > idio_all_code->i ||
		pc < 0) {
		fprintf (stderr, "\n\nPC= %td?\n", pc);
		idio_dump (thr, 1);
		idio_dump (IDIO_THREAD_STACK (thr), 1);
		idio_vm_panic (thr, "RETURN: impossible PC on stack top");
	    }
	    IDIO_THREAD_PC (thr) = pc;
	    if (idio_vm_tracing) {
		fprintf (stderr, "                               %*.s", idio_vm_tracing, "");
		idio_debug ("=> %s\n", IDIO_THREAD_VAL (thr));
		if (idio_vm_tracing <= 1) {
		    /* fprintf (stderr, "XXX RETURN to %td: tracing depth <= 1!\n", pc); */
		} else {
		    idio_vm_tracing--;
		}
	    }
	}
	break;
    case IDIO_A_PACK_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("PACK-FRAME %" PRId64 "", arity);
	    idio_vm_listify (IDIO_THREAD_VAL (thr), arity);
	}
	break;
    case IDIO_A_FUNCTION_INVOKE:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-INVOKE ... ");
	    if (idio_vm_tracing) {
		idio_vm_function_trace (ins, thr);
	    }
	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), IDIO_VM_INVOKE_REGULAR);
	}
	break;
    case IDIO_A_FUNCTION_GOTO:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-GOTO ...");
	    if (idio_vm_tracing) {
		idio_vm_function_trace (ins, thr);
	    }
	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), IDIO_VM_INVOKE_TAIL_CALL);
	}
	break;
    case IDIO_A_POP_CONS_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);
	    
	    IDIO_VM_RUN_DIS ("POP-CONS-FRAME %" PRId64 "", arity);
	    idio_frame_update (IDIO_THREAD_VAL (thr),
			       0,
			       arity,
			       idio_pair (IDIO_THREAD_STACK_POP (),
					  idio_frame_fetch (IDIO_THREAD_VAL (thr), 0, arity)));
	}
	break;
    case IDIO_A_ALLOCATE_FRAME1:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 1");
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (1);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME2:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 2");
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (2);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME3:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 3");
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (3);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME4:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 4");
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (4);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME5:
	{
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME 5");
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (5);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-FRAME %" PRId64 "", i);
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (i);
	}
	break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:
	{
	    uint64_t arity = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ALLOCATE-DOTTED-FRAME %" PRId64 "", arity);
	    IDIO vs = idio_frame_allocate (arity);
	    idio_frame_update (vs, 0, arity - 1, idio_S_nil);
	    IDIO_THREAD_VAL (thr) = vs;
	}
	break;
    case IDIO_A_POP_FRAME0:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 0");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 0, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME1:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 1");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 1, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME2:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 2");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 2, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME3:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 3");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 3, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_FRAME:
	{
	    uint64_t rank = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("POP-FRAME %" PRId64 "", rank);
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, rank, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_ARITYP1:
	{
	    IDIO_VM_RUN_DIS ("ARITY=1?");
	    idio_ai_t nargs = -1;
	    if (idio_S_nil != IDIO_THREAD_VAL (thr)) {
		nargs = IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr));
	    }
	    if (1 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, 0, IDIO_C_LOCATION ("idio_vm_run1/ARITYP1"));
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYP2:
	{
	    IDIO_VM_RUN_DIS ("ARITY=2?");
	    idio_ai_t nargs = -1;
	    if (idio_S_nil != IDIO_THREAD_VAL (thr)) {
		nargs = IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr));
	    }
	    if (2 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, 1, IDIO_C_LOCATION ("idio_vm_run1/ARITYP2"));
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYP3:
	{
	    IDIO_VM_RUN_DIS ("ARITY=3?");
	    idio_ai_t nargs = -1;
	    if (idio_S_nil != IDIO_THREAD_VAL (thr)) {
		nargs = IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr));
	    }
	    if (3 != nargs) {
		fprintf (stderr, "\n\nTHR; ");
		idio_dump (thr, 2);
		fprintf (stderr, "\n\nVAL; ");
		idio_dump (IDIO_THREAD_VAL (thr), 2);
		fprintf (stderr, "\n\nARGS; ");
		idio_gc_set_verboseness (3);
		idio_dump (IDIO_FRAME_ARGS (IDIO_THREAD_VAL (thr)), 10);
		idio_gc_set_verboseness (0);
		
		idio_vm_error_arity (ins, thr, nargs - 1, 2, IDIO_C_LOCATION ("idio_vm_run1/ARITYP3"));
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYP4:
	{
	    IDIO_VM_RUN_DIS ("ARITY=4?");
	    idio_ai_t nargs = -1;
	    if (idio_S_nil != IDIO_THREAD_VAL (thr)) {
		nargs = IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr));
	    }
	    if (4 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, 3, IDIO_C_LOCATION ("idio_vm_run1/ARITYP4"));
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYEQP:
	{
	    uint64_t arityp1 = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ARITY=? %" PRId64 "", arityp1);
	    idio_ai_t nargs = -1;
	    if (idio_S_nil != IDIO_THREAD_VAL (thr)) {
		nargs = IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr));
	    }
	    if (arityp1 != nargs) {
		idio_vm_error_arity (ins, thr, nargs - 1, arityp1 - 1, IDIO_C_LOCATION ("idio_vm_run1/ARITYEQP"));
		return 0;
	    }
	}
	break;
    case IDIO_A_ARITYGEP:
	{
	    uint64_t arityp1 = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ARITY>=? %" PRId64 "", arityp1);
	    idio_ai_t nargs = -1;
	    if (idio_S_nil != IDIO_THREAD_VAL (thr)) {
		nargs = IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr));
	    }
	    if (nargs < arityp1) {
		fprintf (stderr, "\n\nTHR; ");
		idio_dump (thr, 2);
		fprintf (stderr, "\n\nVAL; ");
		idio_dump (IDIO_THREAD_VAL (thr), 2);
		fprintf (stderr, "\n\nARGS; ");
		idio_gc_set_verboseness (3);
		idio_dump (IDIO_FRAME_ARGS (IDIO_THREAD_VAL (thr)), 4);
		idio_gc_set_verboseness (0);
		idio_vm_error_arity_varargs (ins, thr, nargs - 1, arityp1 - 1, IDIO_C_LOCATION ("idio_vm_run1/ARITYGEP"));
		return 0;
	    }
	}
	break;
    case IDIO_A_FIXNUM:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("FIXNUM %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/FIXNUM"), "FIXNUM OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);
	    }
	    IDIO_THREAD_VAL (thr) = idio_fixnum ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_FIXNUM:
	{
	    int64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-FIXNUM %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/NEG-FIXNUM"), "FIXNUM OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);
	    }
	    IDIO_THREAD_VAL (thr) = idio_fixnum ((intptr_t) v);
	}
	break;
    case IDIO_A_CHARACTER:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CHARACTER %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/CHARACTER"), "CHARACTER OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CHARACTER ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_CHARACTER:
	{
	    int64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-CHARACTER %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/NEG-CHARACTER"), "CHARACTER OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CHARACTER ((intptr_t) v);
	}
	break;
    case IDIO_A_CONSTANT:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CONSTANT %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/CONSTANT"), "CONSTANT OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_CONSTANT:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-CONSTANT %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
		idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/NEG-CONSTANT"), "CONSTANT OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT ((intptr_t) v);
	}
	break;
    case IDIO_A_CONSTANT_0:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 0");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (0);
	}
	break;
    case IDIO_A_CONSTANT_1:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 1");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (1);
	}
	break;
    case IDIO_A_CONSTANT_2:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 2");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (2);
	}
	break;
    case IDIO_A_CONSTANT_3:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 3");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (3);
	}
	break;
    case IDIO_A_CONSTANT_4:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 4");
	    IDIO_THREAD_VAL (thr) = idio_fixnum (4);
	}
	break;
    case IDIO_A_PRIMCALL0_NEWLINE:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE0 newline");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("newline", thr, 0);
	    }
	    IDIO_THREAD_VAL (thr) = idio_character_lookup ("newline");
	}
	break;
    case IDIO_A_PRIMCALL0_READ:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE0 read");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("read", thr, 0);
	    }
	    IDIO_THREAD_VAL (thr) = idio_read (IDIO_THREAD_INPUT_HANDLE (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL0:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_primitives_ref (index);
	    IDIO_VM_RUN_DIS ("PRIMITIVE0 %" PRId64 " %s", index, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 0);
	    }
	    
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) ();
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_CAR:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 car");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("car", thr, 1);
	    }
	    IDIO_THREAD_VAL (thr) = idio_list_head (IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_CDR:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 cdr");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("cdr", thr, 1);
	    }
	    IDIO_THREAD_VAL (thr) = idio_list_tail (IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_PAIRP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 pair?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("pair?", thr, 1);
	    }
	    if (idio_isa_pair (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_SYMBOLP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 symbol?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("symbol?", thr, 1);
	    }
	    if (idio_isa_symbol (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_DISPLAY:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 display");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("display", thr, 1);
	    }
	    IDIO h = IDIO_THREAD_OUTPUT_HANDLE (thr);
	    char *vs = idio_display_string (IDIO_THREAD_VAL (thr));
	    IDIO_HANDLE_M_PUTS (h) (h, vs, strlen (vs));
	    free (vs);
	}
	break;
    case IDIO_A_PRIMCALL1_PRIMITIVEP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 primitive?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("primitive?", thr, 1);
	    }
	    if (idio_isa_primitive (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_NULLP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 null?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("null?", thr, 1);
	    }
	    if (idio_S_nil == IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1_CONTINUATIONP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 continuation?");
	    idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1/CONTINUATION"), "continuation?");
	}
	break;
    case IDIO_A_PRIMCALL1_EOFP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 eof?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("eof?", thr, 1);
	    }
	    if (idio_handle_eofp (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL1:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_primitives_ref (index);
	    IDIO_VM_RUN_DIS ("PRIMITIVE1 %" PRId64 " %s", index, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 1);
	    }
	    
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) (IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_CONS:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 cons");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("cons", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_pair (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_EQP:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 eq?");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("eq?", thr, 2);
	    }
	    if (idio_eqp (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    } else {
		IDIO_THREAD_VAL (thr) = idio_S_false;
	    }
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_SET_CAR:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 set-car!");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("set-car!", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_pair_set_head (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_SET_CDR:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 set-cdr!");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("set-cdr!", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_pair_set_tail (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_ADD:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 add");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("add", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_add (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								       IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_SUBTRACT:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 subtract");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("subtract", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_subtract (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									    IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_EQ:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 =");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("=", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_eq (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_LT:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 <");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("<", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_lt (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_GT:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 >");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (">", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_gt (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_MULTIPLY:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 *");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("*", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_multiply (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									    IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_LE:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 <=");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("<=", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_le (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_GE:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 >=");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (">=", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_ge (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
								      IDIO_THREAD_VAL (thr)));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2_REMAINDER:
	{
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 remainder");
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace ("remainder", thr, 2);
	    }
	    IDIO_THREAD_VAL (thr) = idio_defprimitive_remainder (IDIO_THREAD_REG1 (thr),
								 IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_PRIMCALL2:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO primdata = idio_vm_primitives_ref (index);
	    IDIO_VM_RUN_DIS ("PRIMITIVE2 %" PRId64 " %s", index, IDIO_PRIMITIVE_NAME (primdata));
	    if (idio_vm_tracing) {
		idio_vm_primitive_call_trace (IDIO_PRIMITIVE_NAME (primdata), thr, 2);
	    }
	    
	    IDIO_THREAD_VAL (thr) = IDIO_PRIMITIVE_F (primdata) (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	    if (idio_vm_tracing) {
		idio_vm_primitive_result_trace (thr);
	    }
	}
	break;
    case IDIO_A_NOP:
	{
	    IDIO_VM_RUN_DIS ("NOP");
	}
	break;
    case IDIO_A_PUSH_DYNAMIC:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("PUSH-DYNAMIC %" PRId64, index);
	    idio_vm_push_dynamic (index, thr, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_DYNAMIC:
	{
	    IDIO_VM_RUN_DIS ("POP-DYNAMIC");
	    idio_vm_pop_dynamic (thr);
	}
	break;
    case IDIO_A_DYNAMIC_REF:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DYNAMIC-REF %" PRId64 "", index);
	    IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (index, thr);
	}
	break;
    case IDIO_A_DYNAMIC_FUNCTION_REF:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DYNAMIC-FUNCTION-REF %" PRId64 "", index);
	    IDIO_THREAD_VAL (thr) = idio_vm_dynamic_ref (index, thr);
	}
	break;
    case IDIO_A_PUSH_ENVIRON:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("PUSH-ENVIRON %" PRId64, index);
	    idio_vm_push_environ (index, thr, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_ENVIRON:
	{
	    IDIO_VM_RUN_DIS ("POP-ENVIRON");
	    idio_vm_pop_environ (thr);
	}
	break;
    case IDIO_A_ENVIRON_REF:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("ENVIRON-REF %" PRId64 "", index);
	    IDIO_THREAD_VAL (thr) = idio_vm_environ_ref (index, thr);
	}
	break;
    case IDIO_A_PUSH_HANDLER:
	{
	    IDIO_VM_RUN_DIS ("PUSH-HANDLER");
	    idio_vm_push_handler (thr, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_HANDLER:
	{
	    IDIO_VM_RUN_DIS ("POP-HANDLER");
	    idio_vm_pop_handler (thr);
	}
	break;
    case IDIO_A_RESTORE_HANDLER:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-HANDLER");
	    idio_vm_restore_handler (thr);
	}
	break;
    case IDIO_A_NON_CONT_ERR:
	{
	    /* fprintf (stderr, "vm-run1: NON-CONT-ERROR\n"); */
	    /* idio_debug ("THR %s\n", thr); */
	    /* idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr)); */
	    IDIO_VM_RUN_DIS ("NON-CONT-ERROR\n");
	    idio_raise_condition (idio_S_false, idio_struct_instance (idio_condition_idio_error_type,
								      IDIO_LIST3 (idio_string_C ("non-cont-error"),
										  IDIO_C_LOCATION ("idio_vm_run1/NON_CONT_ERR"),
										  IDIO_THREAD_VAL (thr))));
	}
	break;
    case IDIO_A_EXPANDER:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("EXPANDER %" PRId64 "", index);
	    IDIO sym = idio_vm_symbols_ref (index);
	    idio_install_expander (sym, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_INFIX_OPERATOR:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    uint64_t pri = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("OPERATOR %" PRId64 "", index);
	    IDIO sym = idio_vm_symbols_ref (index);
	    idio_install_infix_operator (sym, IDIO_THREAD_VAL (thr), pri);
	}
	break;
    case IDIO_A_POSTFIX_OPERATOR:
	{
	    uint64_t index = idio_vm_fetch_varuint (thr);
	    uint64_t pri = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("OPERATOR %" PRId64 "", index);
	    IDIO sym = idio_vm_symbols_ref (index);
	    idio_install_postfix_operator (sym, IDIO_THREAD_VAL (thr), pri);
	}
	break;
    default:
	{
	    idio_ai_t pc = IDIO_THREAD_PC (thr);
	    idio_ai_t pcm = pc + 10;
	    pc = pc - 10;
	    if (pc < 0) {
		pc = 0;
	    }
	    if (pc % 10) {
		idio_ai_t pc1 = pc - (pc % 10);
		fprintf (stderr, "\n  %5td ", pc1);
		for (; pc1 < pc; pc1++) {
		    fprintf (stderr, "    ");
		}
	    }
	    for (; pc < pcm; pc++) {
		if (0 == (pc % 10)) {
		    fprintf (stderr, "\n  %5td ", pc);
		}
		fprintf (stderr, "%3d ", idio_all_code->ae[pc]);
	    }
	    fprintf (stderr, "\n");
	    idio_error_printf (IDIO_C_LOCATION ("idio_vm_run1"), "unexpected instruction: %3d @%" PRId64 "\n", ins, IDIO_THREAD_PC (thr) - 1);
	}
	break;
    }

    IDIO_VM_RUN_DIS ("\n");
    return 1;
}

void idio_vm_thread_init (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * craft the base error handler's stack data with its parent
     * handler is itself (sp+1)
     */
    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (thr));
    idio_ai_t hsp = IDIO_FIXNUM_VAL (IDIO_THREAD_HANDLER_SP (thr));
    IDIO_C_ASSERT (hsp <= sp);

    if (0 == hsp) {
	/*
	 * Special case.  We can't call the generic
	 * idio_vm_push_handler as that assumes a sensible SP to be
	 * pushed on the stack first.  We don't have that yet.
	 */
	IDIO_THREAD_STACK_PUSH (idio_fixnum (sp + 1));
	IDIO_THREAD_STACK_PUSH (idio_vm_base_error_handler_primdata);
	IDIO_THREAD_HANDLER_SP (thr) = idio_fixnum (sp + 1);
    }
}

void idio_vm_default_pc (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * The problem for an external user, eg. idio_meaning_expander, is
     * that if the expander is a primitive then idio_vm_run() pushes
     * idio_vm_FINISH_pc on the stack expecting the code to run through
     * to the NOP/RETURN it added on the end.  However, for a
     * primitive idio_vm_invoke() will simply do it's thing without
     * changing the PC.  Which will not be about to walk into
     * NOP/RETURN.
     *
     * So we need to preset the PC to be ready to walk into
     * NOP/RETURN.
     *
     * If we put on real code the idio_vm_invoke will set PC after
     * this.
     */
    IDIO_THREAD_PC (thr) = idio_all_code->i;
}

static uintptr_t idio_vm_run_loops = 0;

IDIO idio_vm_run (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_debug ("vm-run: b THR %s\n", thr);  */
    /* idio_vm_thread_init (thr); */

    idio_ai_t ss0 = idio_array_size (IDIO_THREAD_STACK (thr));
    
    /*
     * make sure this segment returns to idio_vm_FINISH_pc
     *
     * XXX should this be in idio_vm_compile?
     */
    IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_FINISH_pc));
    /* idio_i_array_push (idio_all_code, IDIO_A_NOP); */
    idio_i_array_push (idio_all_code, IDIO_A_NOP);
    idio_i_array_push (idio_all_code, IDIO_A_RETURN);

    struct timeval t0;
    gettimeofday (&t0, NULL);

    uintptr_t loops0 = idio_vm_run_loops;
    
    jmp_buf jb;
    jmp_buf *ojb = IDIO_THREAD_JMP_BUF (thr);
    IDIO_THREAD_JMP_BUF (thr) = &jb;

    /*
     * Ready ourselves for idio_raise_condition/continuations to
     * clear the decks.
     *
     * NB Keep counters/timers above this setjmp (otherwise they get
     * reset -- duh)
     */
    int sjv = setjmp (*(IDIO_THREAD_JMP_BUF (thr)));

    /*
     * Hmm, we really should consider caring whether we got here from
     * a longjmp...shouldn't we?
     *
     * I'm not sure we do care.
     */
    switch (sjv) {
    case 0:
	break;
    case IDIO_VM_LONGJMP_SIGNAL_EXCEPTION:
	/* fprintf (stderr, "longjmp from exception\n");  */
	break;
    case IDIO_VM_LONGJMP_CONTINUATION:
	/* fprintf (stderr, "longjmp from continuation\n");  */
	break;
    case IDIO_VM_LONGJMP_CALLCC:
	/* fprintf (stderr, "longjmp from callcc\n");  */
	break;
    case IDIO_VM_LONGJMP_EVENT:
	/* fprintf (stderr, "longjmp from event\n");  */
	break;
    default:
	fprintf (stderr, "setjmp: unexpected value: %d\n", sjv);
	break;
    }

    /*
     * Finally, run the VM code with idio_vm_run1(), one instruction
     * at a time.
     *
     * Every so often we'll poke the GC to tidy up.  It has its own
     * view on whether it is time and/or safe to garbage collect but
     * we need to poke it to find out.
     *
     * It's also a good time to react to any asynchronous events,
     * signals. etc..
     *
     *
     * NB. Perhaps counter-inuitively, not running the GC slows things
     * down dramatically as the Unix process struggles to jump around
     * in masses of virtual memory.  What you really want to be doing
     * is constantly trimming the deadwood -- but only when you've
     * done enough work to generate some deadwood.
     *
     * That said, at the opposite extreme, calling the GC every time
     * round the loop is equally slow as you waste CPU cycles running
     * over the (same) allocated memory to little effect.
     *
     * Often enough, then, but not too often.
     *
     * You can experiment by changing the mask, Oxff, from smallest
     * (0x0) to large (0xffffffff).  s9-test.idio has enough happening
     * to show the effect of using a large value -- virtual memory
     * usage should reach 1+GB.
     *
     * Setting it very small is a good way to find out if you have
     * successfully protected all your values in C-land.  Random SEGVs
     * occur if you haven't.
     */
    for (;;) {
	if (idio_vm_run1 (thr)) {

	    /*
	     * Has anything interesting happened of late while we were
	     * busy doing other things?
	     */
	    if (idio_command_sigchld_flag) {
		idio_command_sigchld_flag = 0;
		IDIO exists = idio_module_symbol_recurse (idio_vm_sigchld_handler_name, idio_main_module (), 1);
		if (idio_S_unspec != exists) {
		    idio_vm_sigchld_handler = idio_module_symbol_value_recurse (idio_vm_sigchld_handler_name, idio_main_module ());
		}
		/* idio_debug ("\nSIGCHLD: %s\n", idio_vm_sigchld_handler); */
		
		if (idio_S_nil != idio_vm_sigchld_handler) {
		    /*
		     * We're about to call an event handler which
		     * could be either a primitive or a closure.
		     *
		     * Either way, we are in the middle of some
		     * sequence of instructions -- we are *not* at a
		     * safe point, eg. in between two lines of source
		     * code (assuming such a point would itself be
		     * "safe").  So we need to preserve all state on
		     * the stack.
		     *
		     * Including the current PC.  We'll replace it
		     * with idio_vm_IHR_pc which knows how to unpick
		     * what we've just pushed onto the stack and
		     * RETURN to the current PC.
		     *
		     * In case the handler is a closure, we need to
		     * create an empty argument frame in
		     * IDIO_THREAD_VAL(thr).
		     *
		     * If the handler is a primitive then it'll run
		     * through to completion and we'll immediately
		     * start running idio_vm_IHR_pc to get us back to
		     * where we interrupted.  All good.
		     *
		     * If it is a closure then we need to invoke it
		     * such that it will return back to the current
		     * PC, idio_vm_IHR_pc.  It we must invoke it as a
		     * regular call, ie. not in tail position.
		     */
		    
		    /* idio_debug ("SIGCHLD: pre-inv: %s\n", thr);  */
		    /* idio_debug ("SIGCHLD: pre-inv: %s\n", IDIO_THREAD_STACK (thr)); */
		    IDIO_THREAD_STACK_PUSH (idio_fixnum (IDIO_THREAD_PC (thr)));
		    idio_vm_preserve_all_state (thr);
		    /* IDIO_THREAD_STACK_PUSH (idio_fixnum (idio_vm_IHR_pc));  */
		    IDIO_THREAD_PC (thr) = idio_vm_IHR_pc; 
		    /* idio_debug ("SIGCHLD: pre-inv pres: %s\n", thr);  */
		    /* idio_debug ("SIGCHLD: pre-inv pres: %s\n", IDIO_THREAD_STACK (thr));  */
			
		    IDIO vs = idio_frame_allocate (1);
		    IDIO_THREAD_VAL (thr) = vs;
		    idio_vm_invoke (thr, idio_vm_sigchld_handler, IDIO_VM_INVOKE_REGULAR);

		    if (NULL != IDIO_THREAD_JMP_BUF (thr)) {
			longjmp (*(IDIO_THREAD_JMP_BUF (thr)), IDIO_VM_LONGJMP_EVENT);
		    } else {
			fprintf (stderr, "WARNING: SIGCHLD: unable to use jmp_buf\n");
		    }
		} else {
		    fprintf (stderr, "VM: no sighandler\n");
		}
	    }

	    if ((idio_vm_run_loops++ & 0xff) == 0) {
		idio_gc_possibly_collect ();
	    }
	} else {
	    break;
	}
    }

    IDIO_THREAD_JMP_BUF (thr) = ojb;

    struct timeval tr;
    gettimeofday (&tr, NULL);
	
    time_t s = tr.tv_sec - t0.tv_sec;
    suseconds_t us = tr.tv_usec - t0.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }

    /*
     * If we've taken long enough and done enough then record OPs/ms
     *
     * test.idio	=> ~1750/ms (~40/ms under valgrind)
     * counter.idio	=> ~6000/ms (~10-15k/ms in a lean build)
     */
    uintptr_t loops = (idio_vm_run_loops - loops0);
    if (loops > 500000 &&
	(s ||
	 us > 100000)) {
	uintptr_t ipms = loops / (s * 1000 + us / 1000);
	fprintf (stderr, "vm_run: %" PRIdPTR " ins in time %ld.%03ld => %" PRIdPTR " i/ms\n", loops, s, (long) us / 1000, ipms);
    }

    /* idio_debug ("vm-run: a THR %s\n", thr); */
    IDIO r = IDIO_THREAD_VAL (thr);
    
    /*
     * Check we are where we think we should be...wherever that is!
     *
     * There's an element of having fallen down the rabbit hole here
     * so we do what we can.  We shouldn't be anywhere other than one
     * beyond idio_vm_FINISH_pc having successfully run through the code
     * we were passed and we shouldn't have left the stack in a mess.
     *
     * XXX except if a handler went off from a signal handler...
     */
    int bail = 0;
    if (IDIO_THREAD_PC (thr) != (idio_vm_FINISH_pc + 1)) {
	fprintf (stderr, "vm-run: THREAD FAIL: PC %zu != %td\n", IDIO_THREAD_PC (thr), (idio_vm_FINISH_pc + 1));
	bail = 1;
    }
    
    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

    if (ss != ss0) {
	fprintf (stderr, "vm-run: THREAD FAIL: SP %td != SP0 %td\n", ss - 1, ss0 - 1);
	if (ss < ss0) {
	    fprintf (stderr, "\n\nNOTICE: current stack smaller than when we started\n");
	    idio_vm_thread_state ();
	}
	else {
	    /* idio_debug ("THR %s\n", thr); */
	    /* idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr)); */
	    while (ss > ss0) {
		IDIO v = IDIO_THREAD_STACK_POP ();
#ifdef IDIO_DEBUG
		fprintf (stderr, "popping %3td: ", ss - 1);
		if (idio_isa_frame (v)) {
		    fprintf (stderr, "%20s ", "frame");
		    idio_debug ("%s\n", IDIO_FRAME_ARGS (v));
		} else if (idio_isa_closure (v)) {
		    IDIO name = idio_hash_get (idio_vm_closure_name, idio_fixnum (IDIO_CLOSURE_CODE (v)));
		    if (idio_S_unspec != name) {
			idio_debug ("%20s ", name);
		    } else {
			fprintf (stderr, "              -anon- ");
		    }
		    idio_debug ("%s\n", v);
		} else {
		    fprintf (stderr, "%20s ", "");
		    idio_debug ("%s\n", v);
		}
#endif
		ss--;
	    }
	    /* bail = 1; */
	}
    }

    if (bail) {
	fprintf (stderr, "vm-run: thread bailed out\n");

	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %.1000s\n", IDIO_THREAD_STACK (thr));
	sleep (0);
    }

    return r;
}

idio_ai_t idio_vm_extend_constants (IDIO v)
{
    IDIO_ASSERT (v);
    
    idio_ai_t i = idio_array_size (idio_vm_constants);
    idio_array_push (idio_vm_constants, v);
    return i;
}

IDIO idio_vm_constants_ref (idio_ai_t i)
{
    return idio_array_get_index (idio_vm_constants, i);
}

idio_ai_t idio_vm_extend_symbols (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    idio_ai_t i = idio_array_size (idio_vm_values);
    idio_array_push (idio_vm_symbols, name);
    idio_array_push (idio_vm_values, idio_S_undef);
    return i;
}

IDIO idio_vm_symbols_ref (idio_ai_t i)
{
    return idio_array_get_index (idio_vm_symbols, i);
}

idio_ai_t idio_vm_symbols_lookup (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    idio_ai_t al = idio_array_size (idio_vm_symbols);
    idio_ai_t i;
    for (i = 0 ; i < al; i++) {
	if (idio_eqp (name, idio_array_get_index (idio_vm_symbols, i))) {
	    return i;
	}
    }

    return -1;
}

IDIO idio_vm_values_ref (idio_ai_t i)
{
    IDIO v = idio_array_get_index (idio_vm_values, i);

    if (idio_isa_struct_instance (v)) {
	if (idio_struct_type_isa (IDIO_STRUCT_INSTANCE_TYPE (v), idio_path_type)) {
	    v = idio_path_expand (v);
	}
    }

    return v;
}

void idio_vm_values_set (idio_ai_t i, IDIO v)
{
    idio_array_insert_index (idio_vm_values, v, i);
}

idio_ai_t idio_vm_extend_primitives (IDIO primdata)
{
    IDIO_ASSERT (primdata);
    IDIO_TYPE_ASSERT (primitive, primdata);
    
    idio_ai_t i = idio_array_size (idio_vm_primitives);
    idio_array_push (idio_vm_primitives, primdata);
    return i;
}

IDIO idio_vm_primitives_ref (idio_ai_t i)
{
    return idio_array_get_index (idio_vm_primitives, i);
}

void idio_vm_thread_state ()
{
    IDIO thr = idio_current_thread ();
    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_debug ("thread-state: THR %s\n", thr);
    idio_debug ("thread-state: STK %.1000s\n", stack);

    IDIO frame = IDIO_THREAD_FRAME (thr);
    while (idio_S_nil != frame) {
	idio_debug ("thread-state: frame: %s\n", IDIO_FRAME_ARGS (frame));
	frame = IDIO_FRAME_NEXT (frame);
    }

    idio_ai_t hsp = IDIO_FIXNUM_VAL (IDIO_THREAD_HANDLER_SP (thr));
    while (hsp != 1) {
	fprintf (stderr, "thread-state: handler: SP %3td ", hsp);
	idio_debug ("%s\n", idio_array_get_index (stack, hsp));
	hsp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, hsp - 1));
    }

    idio_ai_t dsp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMIC_SP (thr));
    while (dsp != -1) {
	fprintf (stderr, "thread-state: dynamic: SP %3td ", dsp);
	idio_ai_t index = IDIO_FIXNUM_VAL (idio_array_get_index (stack, dsp));
	idio_debug ("%s = ", idio_vm_symbols_ref (index));
	idio_debug ("%s\n", idio_array_get_index (stack, dsp - 1));
	dsp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, dsp - 2));
    }

    idio_ai_t esp = IDIO_FIXNUM_VAL (IDIO_THREAD_ENVIRON_SP (thr));
    while (esp != -1) {
	fprintf (stderr, "thread-state: environ: SP %3td ", esp);
	idio_ai_t index = IDIO_FIXNUM_VAL (idio_array_get_index (stack, esp));
	idio_debug ("%s = ", idio_vm_symbols_ref (index));
	idio_debug ("%s\n", idio_array_get_index (stack, esp - 1));
	esp = IDIO_FIXNUM_VAL (idio_array_get_index (stack, esp - 2));
    }
}

IDIO_DEFINE_PRIMITIVE0 ("idio-thread-state", idio_thread_state, ())
{
    idio_vm_thread_state ();

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("exit", exit, (IDIO istatus))
{
    IDIO_ASSERT (istatus);

    int status = -1;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else if (idio_isa_C_int (istatus)) {
	status = IDIO_C_TYPE_INT (istatus);
    } else {
	idio_error_param_type ("fixnum|C_int istatus", istatus, IDIO_C_LOCATION ("exit"));
    }

    idio_handle_flush (idio_current_output_handle ());
    idio_handle_flush (idio_current_error_handle ());
    
    exit (status);
}

void idio_vm_unwind_thread (IDIO thr, int verbose)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (verbose) {
	fprintf (stderr, "\nTHREAD UNWIND\n");

	IDIO stack = IDIO_THREAD_STACK (thr);
	IDIO frame = IDIO_THREAD_FRAME (thr);

	idio_debug ("THR:\t%s\n", thr);
	idio_debug ("STK:\t%.1000s\n", stack);

	while (idio_S_nil != frame) {
	    idio_debug ("call frame: %s\n", IDIO_FRAME_ARGS (frame));
	    frame = IDIO_FRAME_NEXT (frame);
	}
    
	idio_debug ("MODULE:\t%s\n", IDIO_MODULE_NAME (IDIO_THREAD_MODULE (thr)));
	idio_debug ("INPUT:\t%s\n", IDIO_THREAD_INPUT_HANDLE (thr));
	idio_debug ("OUTPUT:\t%s\n", IDIO_THREAD_OUTPUT_HANDLE (thr));
    }

    /*
     * There was code to pop the stack here as where better to clear
     * down the stack than in the unwind code?  But the question was,
     * clear down to what to ensure the engine kept running?  Whatever
     * value was chosen always seemed to end in tears.
     *
     * However, idio_vm_run() knows the SP for when it was started and
     * given that we're about to tell it to FINISH the current run
     * then it would make sense for it to clear down to the last known
     * good value, the value it had when it started.
     */
    IDIO_THREAD_PC (thr) = idio_vm_FINISH_pc;
}

void idio_init_vm_values ()
{
    idio_vm_values = idio_array (400);
    idio_gc_protect (idio_vm_values);
    idio_vm_symbols = idio_array (400);
    idio_gc_protect (idio_vm_symbols);
}

void idio_init_vm ()
{
    idio_all_code = idio_i_array (200000);
    
    idio_vm_code_prologue (idio_all_code);
    idio_prologue_len = idio_all_code->i;

    idio_vm_constants = idio_array (8000);
    idio_gc_protect (idio_vm_constants);
    idio_vm_primitives = idio_array (400);
    idio_gc_protect (idio_vm_primitives);

    /*
     * XXX we need idio_vm_base_error_handler_primdata before anyone
     * can create a thread
     */
    IDIO index = IDIO_ADD_SPECIAL_PRIMITIVE (base_error_handler);
    idio_vm_base_error_handler_primdata = idio_vm_primitives_ref (IDIO_FIXNUM_VAL (index));

    idio_vm_sigchld_handler_name = idio_symbols_C_intern ("do-job-notification");
    idio_gc_protect (idio_vm_sigchld_handler_name);
    idio_vm_sigchld_handler = idio_S_nil;
    
    idio_S_sigchld = idio_symbols_C_intern ("sigchld");

    idio_vm_closure_name = IDIO_HASH_EQP (256);
    idio_gc_protect (idio_vm_closure_name);
}

void idio_vm_add_primitives ()
{
    IDIO_ADD_SPECIAL_PRIMITIVE (raise);
    IDIO_ADD_SPECIAL_PRIMITIVE (apply);
    IDIO_ADD_SPECIAL_PRIMITIVE (make_continuation);
    IDIO_ADD_SPECIAL_PRIMITIVE (restore_continuation);
    IDIO_ADD_SPECIAL_PRIMITIVE (call_cc);
    IDIO_ADD_SPECIAL_PRIMITIVE (vm_trace);
#ifdef IDIO_DEBUG
    IDIO_ADD_SPECIAL_PRIMITIVE (vm_dis);
#endif
    IDIO_ADD_PRIMITIVE (idio_thread_state);
    IDIO_ADD_PRIMITIVE (exit);
}

void idio_final_vm ()
{
    fprintf (stderr, "final-vm: created %zu instructions\n", idio_all_code->i);
    idio_i_array_free (idio_all_code);
    fprintf (stderr, "final-vm: created %td constants\n", idio_array_size (idio_vm_constants));
    idio_gc_expose (idio_vm_constants);
    fprintf (stderr, "final-vm: created %td symbols\n", idio_array_size (idio_vm_symbols));
    idio_gc_expose (idio_vm_symbols);
    idio_gc_expose (idio_vm_values);
    fprintf (stderr, "final-vm: created %td primitives\n", idio_array_size (idio_vm_primitives));
    idio_gc_expose (idio_vm_primitives);
    idio_gc_expose (idio_vm_closure_name);
    idio_gc_expose (idio_vm_sigchld_handler_name);
}
