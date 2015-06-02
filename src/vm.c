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
 * return value.  You can enable/disable it in code with %%vm-trace
 * {val}.
 *
 * idio_vm_dis reports the byte-instruction by byte-instruction flow.
 * You can enable/disable it in code with %%vm-dis {val}.  It is very
 * verbose.  You were warned.  I don't feel bad about your pain.
 */
static int idio_vm_tracing = 0;
static int idio_vm_dis = 0;

/*
 * We don't know if some arbitrary code is going to set a global value
 * to be a closure.  If it does, we need to retain the code for the
 * closure.  Hence a global list of all known code.
 *
 * Prologue
 *
 * There is a prologue which defines some universal get-out functions
 * (from Queinnec).  idio_finish_pc is the PC for the FINISH
 * instruction and idio_prologue_len how big the prologue is.
 */
static idio_i_array_t *idio_all_code;
static idio_ai_t idio_finish_pc;
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
 */
static IDIO idio_vm_constants;
static IDIO idio_vm_symbols;
static IDIO idio_vm_values;
static IDIO idio_vm_primitives;
static IDIO idio_vm_closure_name;

static IDIO idio_vm_base_error_handler_primdata;

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

static void idio_vm_error_compile_param_args (char *m)
{
    idio_error_message ("expected arguments: %s", m);
}

static void idio_vm_error_compile_param_type (char *m, IDIO t)
{
    char *ts = idio_display_string (t);
    idio_error_message ("not a %s: %s", m, ts);
    free (ts);
}

static void idio_vm_error_function_invoke (char *msg, IDIO func)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (func);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);
    idio_display_C (" ", sh);
    idio_display (func, sh);
    IDIO c = idio_struct_instance (idio_condition_rt_function_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil));
    idio_signal_exception (idio_S_true, c);
}

static void idio_vm_function_trace (IDIO_I ins, IDIO thr);
static void idio_vm_error_arity (IDIO_I ins, IDIO thr, size_t given, size_t arity)
{
    fprintf (stderr, "\n\nARITY != %zd\n", arity);
    idio_vm_function_trace (ins, thr);

    IDIO sh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "incorrect arity: %zd args for an arity-%zd function", given, arity);
    idio_display_C (em, sh);
    IDIO c = idio_struct_instance (idio_condition_rt_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil));
    idio_signal_exception (idio_S_true, c);
}

static void idio_vm_error_arity_varargs (IDIO_I ins, IDIO thr, size_t given, size_t arity)
{
    fprintf (stderr, "\n\nARITY < %zd\n", arity);
    idio_vm_function_trace (ins, thr);

    IDIO sh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "incorrect arity: %zd args for an arity-%zd+ function", given, arity);
    idio_display_C (em, sh);
    IDIO c = idio_struct_instance (idio_condition_rt_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil));
    idio_signal_exception (idio_S_true, c);
}

static void idio_error_dynamic_unbound (idio_ai_t index)
{
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("no such dynamic binding", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_dynamic_variable_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       idio_vm_symbols_ref (index)));
    idio_signal_exception (idio_S_true, c);
}

/*
 * Idio instruction arrays.
 *
 * We generate our byte compiled code into these.  There's often
 * several floating around at once as in order to determine how far to
 * jump over some unnecessary code to move onto the next (source code)
 * statement we need to have byte compiled the unnecessary code.
 * Which we can then copy.
 *
 * XXX These are reference counted for reasons to do with an
 * undiscovered bug.
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

#define IDIO_IA_PUSH1(i1)          (idio_i_array_push (ia, i1))
#define IDIO_IA_PUSH2(i1,i2)       IDIO_IA_PUSH1 (i1); IDIO_IA_PUSH1 (i2)
#define IDIO_IA_PUSH3(i1,i2,i3)    IDIO_IA_PUSH1 (i1); IDIO_IA_PUSH1 (i2); IDIO_IA_PUSH1 (i3)
#define IDIO_IA_PUSH4(i1,i2,i3,i4) IDIO_IA_PUSH1 (i1); IDIO_IA_PUSH1 (i2); IDIO_IA_PUSH1 (i3); IDIO_IA_PUSH1 (i4)

/*
 * Variable length integers
 *
 * We want to be able to encode integers -- usually indexes but also
 * fixnums and characters -- reasonably efficiently but there is the
 * potential for some large values.

 * SQLite4 varuint: https://sqlite.org/src4/doc/trunk/www/varint.wiki
 *
 * This encoding supports positive numbers only hence extra
 * instructions to reference negative values.
 */
idio_i_array_t *idio_i_array_compute_varuint (uintptr_t offset)
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
	} else if (offset <= 4294967295) {
	    IDIO_IA_PUSH1 (251);
	    n = 4;
	} else if (offset <= 1099511627775) {
	    IDIO_IA_PUSH1 (252);
	    n = 5;
	} else if (offset <= 281474976710655) {
	    IDIO_IA_PUSH1 (253);
	    n = 6;
	} else if (offset <= 72057594037927935) {
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
	if (idio_isa_pair (mh)) {
	    while (idio_S_nil != m) {
		idio_vm_compile (thr, ia, IDIO_PAIR_H (m), depth + 1);
		m = IDIO_PAIR_T (m);
		if (! idio_isa_list (m)) {
		    idio_error_C ("compile: not a sequence", m);
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
		idio_vm_error_compile_param_args ("SHALLOW-ARGUMENT-REF j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
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
		idio_vm_error_compile_param_args ("PREDEFINED i");
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_vm_error_compile_param_type ("fixnum", i);
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
		idio_vm_error_compile_param_args ("DEEP-ARGUMENT-REF i j");
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_vm_error_compile_param_type ("fixnum", i);
		return;
	    }

	    IDIO j = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
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
		idio_vm_error_compile_param_args ("SHALLOW-ARGUMENT-SET j m1");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
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
		idio_vm_error_compile_param_args ("DEEP-ARGUMENT-SET i j m1");
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_vm_error_compile_param_type ("fixnum", i);
		return;
	    }

	    IDIO j = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
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
		idio_vm_error_compile_param_args ("GLOBAL-REF j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
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
		idio_vm_error_compile_param_args ("CHECKED-GLOBAL-REF j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
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
		idio_vm_error_compile_param_args ("CHECKED-GLOBAL-FUNCTION-REF j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_CHECKED_GLOBAL_FUNCTION_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_GLOBAL_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("GLOBAL-SET j m1");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    
	    idio_vm_compile (thr, ia, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_SET);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_CONSTANT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("CONSTANT j");
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
		idio_vm_error_compile_param_args ("ALTERNATIVE m1 m2 m3");
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
		idio_vm_error_compile_param_args ("SEQUENCE m1 m+");
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
		idio_vm_error_compile_param_args ("AND m+");
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
		idio_vm_error_compile_param_args ("OR m+");
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
		idio_vm_error_compile_param_args ("BEGIN m+");
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
		idio_vm_error_compile_param_args ("TR-FIX-LET m* m+");
		return;
	    }
	    
	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_ENV);
	    idio_vm_compile (thr, ia, mp, depth + 1);
	}
	break;
    case IDIO_VM_CODE_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("FIX-LET m* m+");
		return;
	    }
	    
	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_ENV);
	    idio_vm_compile (thr, ia, mp, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_UNLINK_ENV);
	}
	break;
    case IDIO_VM_CODE_PRIMCALL0:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("CALL0 ins");
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
		idio_vm_error_compile_param_args ("CALL1 ins m1");
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
		idio_vm_error_compile_param_args ("CALL2 ins m1 m2");
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
		idio_vm_error_compile_param_args ("CALL3 ins m1 m2 m3");
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
		idio_vm_error_compile_param_args ("FIX-CLOSURE m+ arity");
		return;
	    }
	    
	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (arity)) {
		idio_vm_error_compile_param_type ("fixnum", arity);
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
	    idio_i_array_push (iap, IDIO_A_EXTEND_ENV);
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
		idio_vm_error_compile_param_args ("NARY-CLOSURE m+ arity");
		return;
	    }
	    
	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (arity)) {
		idio_vm_error_compile_param_type ("fixnum", arity);
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

	    idio_i_array_push (iap, IDIO_A_EXTEND_ENV);
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
		idio_vm_error_compile_param_args ("TR-REGULAR-CALL m1 m*");
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
		idio_vm_error_compile_param_args ("REGULAR-CALL m1 m*");
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms, depth + 1);
	    IDIO_IA_PUSH4 (IDIO_A_POP_FUNCTION, IDIO_A_PRESERVE_ENV, IDIO_A_FUNCTION_INVOKE, IDIO_A_RESTORE_ENV);
	}
	break;
    case IDIO_VM_CODE_STORE_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("STORE-ARGUMENT m1 m* rank");
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO rank = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    if (! idio_isa_fixnum (rank)) {
		idio_vm_error_compile_param_type ("fixnum", rank);
		return;
	    }

	    idio_vm_compile (thr, ia, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms, depth + 1);
	    switch (IDIO_FIXNUM_VAL (rank)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_POP_ENV0); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_POP_ENV1); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_POP_ENV2); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_POP_ENV3); break;
	    default:
		IDIO_IA_PUSH1 (IDIO_A_POP_ENV);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (rank));
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_CONS_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_vm_error_compile_param_args ("CONS-ARGUMENT m1 m* arity");
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
		idio_vm_error_compile_param_args ("ALLOCATE-FRAME size");
		return;
	    }
	    
	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_vm_error_compile_param_type ("fixnum", size);
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
		idio_vm_error_compile_param_args ("ALLOCATE-DOTTED-FRAME size");
		return;
	    }
	    
	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_vm_error_compile_param_type ("fixnum", size);
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
		idio_vm_error_compile_param_args ("FINISH");
		return;
	    }
	    
	    IDIO_IA_PUSH1 (IDIO_A_FINISH);
	}
	break;
    case IDIO_VM_CODE_PUSH_DYNAMIC:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("PUSH-DYNAMIC index m");
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
		idio_vm_error_compile_param_args ("POP-DYNAMIC");
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_DYNAMIC);
	}
	break;
    case IDIO_VM_CODE_DYNAMIC_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("DYNAMIC-REF index");
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
		idio_vm_error_compile_param_args ("DYNAMIC-FUNCTION-REF index");
		return;
	    }

	    IDIO index = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_FUNCTION_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (index));
	}
	break;
    case IDIO_VM_CODE_PUSH_HANDLER:
	{
	    if (idio_S_nil != mt) {
		idio_vm_error_compile_param_args ("PUSH-HANDLER");
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_HANDLER);
	}
	break;
    case IDIO_VM_CODE_POP_HANDLER:
	{
	    if (idio_S_nil != mt) {
		idio_vm_error_compile_param_args ("POP-HANDLER");
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_HANDLER);
	}
	break;
    case IDIO_VM_CODE_EXPANDER:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("EXPANDER i m");
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);
	    IDIO m = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    
	    idio_vm_compile (thr, ia, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXPANDER);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	}
	break;
    case IDIO_VM_CODE_NOP:
	break;
    default:
	idio_error_message ("bad instruction: %s", idio_as_string (mh, 1));
	break;
    }
}

idio_ai_t idio_vm_code_prologue (idio_i_array_t *ia)
{
    IDIO_IA_PUSH4 (IDIO_A_NON_CONT_ERR, IDIO_A_FINISH, IDIO_A_RESTORE_ENV, IDIO_A_RETURN);

    /* index of IDIO_A_FINISH */
    return 1;
}

/*
 * XXX base_error_handler must not raise an exception otherwise we'll
 * loop forever -- or until the C stack blows up.
 */
IDIO_DEFINE_PRIMITIVE2 ("base-error-handler", base_error_handler, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);
    /* IDIO_TYPE_ASSERT (condition, c); */

    fprintf (stderr, "\n\nbase-error-handler\n");
    int continuablep = 0;
    if (IDIO_TYPE_FIXNUMP (cont)) {
	continuablep = IDIO_FIXNUM_VAL (cont);
    }

    if (idio_isa_condition (cond)) {
	idio_debug ("type: %s\n", IDIO_STRUCT_TYPE_NAME (IDIO_STRUCT_INSTANCE_TYPE (cond)));
	IDIO st = IDIO_STRUCT_INSTANCE_TYPE (cond);
	IDIO stf = IDIO_STRUCT_TYPE_FIELDS (st);
	IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond);

	idio_ai_t al = idio_array_size (stf);
	idio_ai_t ai;
	for (ai = 0; ai < al; ai++) {
	    idio_debug ("%s: ", idio_array_get_index (stf, ai));
	    idio_debug ("%s\n", idio_array_get_index (sif, ai));
	}
    } else {
	fprintf (stderr, "base-error-handler: expected a condition not a %s\n", idio_type2string (cond));
	idio_debug ("%s\n", cond);
    }

    if (continuablep) {
	fprintf (stderr, "should continue but won't!\n");
    }
    idio_debug ("THREAD: %s\n", idio_current_thread ());
    idio_debug ("STACK:  %s\n", IDIO_THREAD_STACK (idio_current_thread ()));
    IDIO_C_ASSERT (0);
    return idio_S_unspec;
}

#define IDIO_THREAD_FETCH_NEXT()	(idio_all_code->ae[IDIO_THREAD_PC(thr)++])
#define IDIO_THREAD_STACK_PUSH(v)	(idio_array_push (IDIO_THREAD_STACK(thr), v))
#define IDIO_THREAD_STACK_POP()		(idio_array_pop (IDIO_THREAD_STACK(thr)))

void idio_vm_codegen (IDIO thr, IDIO m)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (pair, m);
    
    IDIO_THREAD_PC (thr) = idio_all_code->i;
    
    idio_i_array_t *ia = idio_i_array (100);
    
    idio_vm_compile (thr, ia, m, 0);

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
	    idio_vm_error_function_invoke ("cannot invoke constant type", func);
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
		IDIO_THREAD_STACK_PUSH (IDIO_FIXNUM (IDIO_THREAD_PC (thr)));
	    }
	    IDIO_THREAD_ENV (thr) = IDIO_CLOSURE_ENV (func);
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
	     * primitive calls idio_signal_exception then there is an
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
		char *ls = idio_as_string (last, 1);
		fprintf (stderr, "invoke: last arg != nil: %s\n", ls);
		free (ls);
		IDIO_C_ASSERT (0);
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
		idio_vm_error_function_invoke ("arity unexpected", func);
		break;
	    }

	    size_t pc = IDIO_THREAD_PC (thr); 

	    if (0 == tailp &&
		pc != pc0) {
		IDIO_THREAD_STACK_PUSH (IDIO_FIXNUM (pc0));
	    }
	    
	    if (idio_vm_tracing) {
		fprintf (stderr, "                       %*.s", idio_vm_tracing, "");
		/* XXX - why is idio_vm_tracing one less hence an extra space? */
		idio_debug (" => %s\n", IDIO_THREAD_VAL (thr));
		if (idio_vm_tracing < 1) {
		    fprintf (stderr, "XXX PRIM tracing depth < 1!\n");
		} else {
		    /* idio_vm_tracing--; */
		}
	    }

	    return;
	}
	break;
    case IDIO_TYPE_SYMBOL:
	{
	    char *pathname = idio_find_command (func);
	    if (NULL != pathname) {
		IDIO_THREAD_VAL (thr) = idio_invoke_command (func, thr, pathname);
	    } else {
		idio_vm_error_function_invoke ("command not found", func);
	    }
	}
	break;
    default:
	{
	    idio_vm_error_function_invoke ("cannot invoke", func);
	}
	break;
    }
}

static void idio_vm_preserve_environment (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_DYNAMICSP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_HANDLERSP (thr));
    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_ENV (thr));
}

static void idio_vm_restore_environment (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_THREAD_ENV (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_HANDLERSP (thr) = IDIO_THREAD_STACK_POP ();
    IDIO_THREAD_DYNAMICSP (thr) = IDIO_THREAD_STACK_POP ();
    if (idio_S_nil != IDIO_THREAD_ENV (thr)) {
	IDIO_TYPE_ASSERT (frame, IDIO_THREAD_ENV (thr));
    }
}

static void idio_vm_push_dynamic (idio_ai_t index, IDIO thr, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_DYNAMICSP (thr));
    idio_array_push (stack, val);
    IDIO_THREAD_DYNAMICSP (thr) = IDIO_FIXNUM (idio_array_size (stack));
    idio_array_push (stack, IDIO_FIXNUM (index));
}

static void idio_vm_pop_dynamic (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_pop (stack);
    idio_array_pop (stack);
    IDIO_THREAD_DYNAMICSP (thr) = idio_array_pop (stack);
}

static IDIO idio_vm_dynamic_ref (idio_ai_t index, IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_DYNAMICSP (thr));

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
		idio_error_dynamic_unbound (index);
	    }

	    return v;
	}
    }
}

static void idio_vm_push_handler (IDIO thr, IDIO val)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (val);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);

    idio_array_push (stack, IDIO_THREAD_HANDLERSP (thr));
    IDIO_THREAD_HANDLERSP (thr) = IDIO_FIXNUM (idio_array_size (stack));
    idio_array_push (stack, val);
}

static void idio_vm_pop_handler (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    
    idio_array_pop (stack);
    IDIO_THREAD_HANDLERSP (thr) = idio_array_pop (stack);
}

IDIO idio_signal_exception (IDIO continuablep, IDIO e)
{
    IDIO_ASSERT (continuablep);
    IDIO_ASSERT (e);
    IDIO_TYPE_ASSERT (boolean, continuablep);

    /* idio_debug ("\n\nsignal-exception: %s", continuablep);  */
    /* idio_debug (" %s\n", e);  */

    IDIO thr = idio_current_thread ();
    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_ai_t handlersp = IDIO_FIXNUM_VAL (IDIO_THREAD_HANDLERSP (thr));
    IDIO handler = idio_array_get_index (stack, handlersp);

    IDIO vs = idio_frame (idio_S_nil, IDIO_LIST2 (continuablep, e));
    IDIO_THREAD_VAL (thr) = vs;

    if (idio_S_true == continuablep) {
	idio_array_push (stack, IDIO_FIXNUM (IDIO_THREAD_PC (thr)));
	idio_vm_preserve_environment (thr);
	idio_array_push (stack, IDIO_FIXNUM (idio_finish_pc + 1)); /* POP-HANDLER, RESTORE-ENV, RETURN */
    } else {
	idio_array_push (stack, IDIO_FIXNUM (idio_finish_pc - 1)); /* NON-CONT-ERR */
    }

    /*
     * have this handler run using the next handler as its safety net
     */
    IDIO_THREAD_HANDLERSP (thr) = idio_array_get_index (stack, handlersp - 1);

    /* God speed! */
    idio_vm_invoke (thr, handler, 1);

    /*
     * Actually, for a user-defined error handler, which will be a
     * closure, idio_vm_invoke did nothing much, the error
     * handling closure will only be run when we continue looping
     * around idio_vm_run1.
     *
     * Wait, though, consider how we've got to here.  Some
     * (user-level) code called a C primitive which stumbled over an
     * erroneous condition.  That C code called us,
     * idio_signal_exception.
     *
     * Whilst we are ready to process the OOB exception handler in
     * Idio-land, the C language part of us still thinks we've in a
     * regular function call tree from the point of origin of the
     * error, ie. we still have a trail of C language frames back
     * through the caller (of idio_signal_exception) -- and, in turn,
     * back up through to idio_vm_run1.  We need to make that trail
     * disappear otherwise either we'll accidentally return() back
     * down that C stack (erroneously) or, after enough
     * idio_signal_exception()s, we'll blow up the C stack.
     *
     * That means longjmp(3).
     *
     * XXX longjmp means that we won't be free()ing any memory
     * allocated during the life of that C stack.  Unless we think of
     * something clever...
     */
    longjmp (IDIO_THREAD_JMP_BUF (thr), IDIO_VM_LONGJMP_SIGNAL_EXCEPTION);

    /* not reached */
    IDIO_C_ASSERT (0);
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

    idio_vm_invoke (thr, fn, 1);

    return IDIO_THREAD_VAL (thr);
}

IDIO_DEFINE_PRIMITIVE1V ("apply", apply, (IDIO fn, IDIO args))
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (args);

    return idio_apply (fn, args);
}

IDIO_DEFINE_PRIMITIVE1 ("%%vm-trace", vm_trace, (IDIO trace))
{
    IDIO_ASSERT (trace);

    IDIO_VERIFY_PARAM_TYPE (fixnum, trace);

    idio_vm_tracing = IDIO_FIXNUM_VAL (trace);
    
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("%%vm-dis", vm_dis, (IDIO dis))
{
    IDIO_ASSERT (dis);

    IDIO_VERIFY_PARAM_TYPE (fixnum, dis);

    idio_vm_dis = IDIO_FIXNUM_VAL (dis);
    
    return idio_S_unspec;
}

#define IDIO_VM_RUN_DIS(...)	if (idio_vm_dis) { fprintf (stderr, __VA_ARGS__); }

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
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent
     * %s	- expression
     */
    if (idio_isa_closure (func)) {
	IDIO name = idio_hash_get (idio_vm_closure_name, IDIO_FIXNUM (IDIO_CLOSURE_CODE (func)));
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
     * %20s	- closure name (if available)
     * SPACE
     * %3s	- tail call indicator
     * %*s	- trace-depth indent
     * %s	- expression
     */
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
     * %*s	- trace-depth indent
     * %s	- expression
     */
    fprintf (stderr, "                        ");

    fprintf (stderr, "%*.s", idio_vm_tracing, "");
    idio_debug ("=> %s\n", val);
}

int idio_vm_run1 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    if (IDIO_THREAD_PC(thr) > idio_all_code->i) {
	IDIO_C_ASSERT (0);
    }
    IDIO_I ins = IDIO_THREAD_FETCH_NEXT ();

    IDIO_VM_RUN_DIS ("idio_vm_run1: %p %3d: ", thr, ins);
    
    switch (ins) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 0");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 0);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 1");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 1);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 2");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 2);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF 3");
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 3);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:
	{
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-REF %" PRId64 "", j);
	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, j);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_REF:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-REF %" PRId64 " %" PRId64 "", i, j);
	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), i, j);
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
		/* idio_error_message ("undefined toplevel: %" PRId64 "", i); */

		IDIO_THREAD_VAL (thr) = sym; 
	    } else if (idio_S_unspec == val) {
		idio_debug ("\nCHECKED-GLOBAL-REF: %s", sym); 
		fprintf (stderr, " #%" PRId64, i);
		idio_debug (" #unspec in symbols %s\n", idio_vm_symbols);
		idio_dump (thr, 2);
		idio_debug ("c-m: %s\n", idio_current_module ());
		idio_error_message ("unspecified toplevel: %" PRId64 "", i);
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
		idio_error_message ("undefined toplevel: %" PRId64 "", i);
		*/
		IDIO_THREAD_VAL (thr) = sym; 
	    } else if (idio_S_unspec == val) {
		idio_debug ("\nCHECKED-GLOBAL-FUNCTION-REF:) %s", sym); 
		fprintf (stderr, " #%" PRId64, i);
		idio_debug (" #unspec in symbols %s\n", idio_vm_symbols);
		idio_dump (thr, 2);
		idio_debug ("c-m: %s\n", idio_current_module ());
		idio_error_message ("unspecified toplevel: %" PRId64 "", i);
	    } else {
		IDIO_THREAD_VAL (thr) = val;
	    }
	}
	break;
    case IDIO_A_CONSTANT_REF: 
    	{ 
    	    idio_ai_t i = idio_vm_fetch_varuint (thr); 
    	    IDIO_VM_RUN_DIS ("CONSTANT %" PRIdPTR "", i); 
    	    IDIO_THREAD_VAL (thr) = idio_vm_constants_ref (i); 
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
	    idio_error_message ("predef 3");
	}
	break;
    case IDIO_A_PREDEFINED4:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (4);
	    idio_error_message ("predef 4");
	}
	break;
    case IDIO_A_PREDEFINED5:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (5);
	    idio_error_message ("predef 5");
	}
	break;
    case IDIO_A_PREDEFINED6:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (6);
	    idio_error_message ("predef 6");
	}
	break;
    case IDIO_A_PREDEFINED7:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (7);
	    idio_error_message ("predef 7");
	}
	break;
    case IDIO_A_PREDEFINED8:
	{
	    IDIO_THREAD_VAL (thr) = idio_vm_primitives_ref (8);
	    idio_error_message ("predef 8");
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
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 0, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 1");
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 1, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 2");
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 2, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:
	IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET 3");
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 3, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("SHALLOW-ARGUMENT-SET %" PRId64 "", i);
	    idio_frame_update (IDIO_THREAD_ENV (thr), 0, i, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_SET:
	{
	    uint64_t i = idio_vm_fetch_varuint (thr);
	    uint64_t j = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("DEEP-ARGUMENT-SET %" PRId64 " %" PRId64 "", i, j);
	    idio_frame_update (IDIO_THREAD_ENV (thr), i, j, IDIO_THREAD_VAL (thr));
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
		idio_hash_put (idio_vm_closure_name, IDIO_FIXNUM (IDIO_CLOSURE_CODE (val)), sym);
	    }
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
    case IDIO_A_EXTEND_ENV:
	{
	    IDIO_VM_RUN_DIS ("EXTEND-ENV");
	    IDIO_THREAD_ENV (thr) = idio_frame_extend (IDIO_THREAD_ENV (thr), IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_UNLINK_ENV:
	{
	    IDIO_VM_RUN_DIS ("UNLINK-ENV");
	    IDIO_THREAD_ENV (thr) = IDIO_FRAME_NEXT (IDIO_THREAD_ENV (thr));
	}
	break;
    case IDIO_A_PUSH_VALUE:
	{
	    IDIO_VM_RUN_DIS ("PUSH-VALUE");
	    IDIO_THREAD_STACK_PUSH (IDIO_THREAD_VAL (thr));
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
    case IDIO_A_PRESERVE_ENV:
	{
	    IDIO_VM_RUN_DIS ("PRESERVE-ENV");
	    idio_vm_preserve_environment (thr);
	}
	break;
    case IDIO_A_RESTORE_ENV:
	{
	    IDIO_VM_RUN_DIS ("RESTORE-ENV");
	    idio_vm_restore_environment (thr);
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
	    IDIO_THREAD_VAL (thr) = idio_closure (IDIO_THREAD_PC (thr) + i, IDIO_THREAD_ENV (thr));
	}
	break;
    case IDIO_A_RETURN:
	{
	    IDIO_VM_RUN_DIS ("RETURN");
	    idio_ai_t sp = IDIO_FIXNUM_VAL (IDIO_THREAD_STACK_POP ());
	    if (sp > idio_all_code->i ||
		sp < 0) {
		fprintf (stderr, "\n\n");
		idio_dump (thr, 1);
		idio_dump (IDIO_THREAD_STACK (thr), 1);
		idio_vm_panic (thr, "RETURN: impossible stack pointer on stack top");
	    }
	    IDIO_THREAD_PC (thr) = sp;
	    if (idio_vm_tracing) {
		fprintf (stderr, "                       %*.s", idio_vm_tracing, "");
		idio_debug ("=> %s\n", IDIO_THREAD_VAL (thr));
		if (idio_vm_tracing <= 1) {
		    fprintf (stderr, "XXX RETURN tracing depth <= 1!\n");
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
	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), 0);
	}
	break;
    case IDIO_A_FUNCTION_GOTO:
	{
	    IDIO_VM_RUN_DIS ("FUNCTION-GOTO ...");
	    if (idio_vm_tracing) {
		idio_vm_function_trace (ins, thr);
	    }
	    idio_vm_invoke (thr, IDIO_THREAD_FUNC (thr), 1);
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
    case IDIO_A_POP_ENV0:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 0");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 0, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_ENV1:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 1");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 1, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_ENV2:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 2");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 2, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_ENV3:
	{
	    IDIO_VM_RUN_DIS ("POP-FRAME 3");
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 3, IDIO_THREAD_STACK_POP ());
	}
	break;
    case IDIO_A_POP_ENV:
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
		idio_vm_error_arity (ins, thr, nargs - 1, 0);
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
		idio_vm_error_arity (ins, thr, nargs - 1, 1);
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
		
		idio_vm_error_arity (ins, thr, nargs - 1, 2);
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
		idio_vm_error_arity (ins, thr, nargs - 1, 3);
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
		idio_vm_error_arity (ins, thr, nargs - 1, arityp1 - 1);
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
		idio_vm_error_arity_varargs (ins, thr, nargs - 1, arityp1 - 1);
		return 0;
	    }
	}
	break;
    case IDIO_A_FIXNUM:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("FIXNUM %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
	      idio_error_message ("FIXNUM OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM ((intptr_t) v);
	}
	break;
    case IDIO_A_NEG_FIXNUM:
	{
	    int64_t v = idio_vm_fetch_varuint (thr);
	    v = -v;
	    IDIO_VM_RUN_DIS ("NEG-FIXNUM %" PRId64 "", v);
	    if (IDIO_FIXNUM_MIN > v) {
	      idio_error_message ("FIXNUM OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM ((intptr_t) v);
	}
	break;
    case IDIO_A_CHARACTER:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CHARACTER %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
	      idio_error_message ("CHARACTER OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);
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
	      idio_error_message ("CHARACTER OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CHARACTER ((intptr_t) v);
	}
	break;
    case IDIO_A_CONSTANT:
	{
	    uint64_t v = idio_vm_fetch_varuint (thr);
	    IDIO_VM_RUN_DIS ("CONSTANT %" PRId64 "", v);
	    if (IDIO_FIXNUM_MAX < v) {
	      idio_error_message ("CONSTANT OOB: %" PRIu64 " > %" PRIu64, v, IDIO_FIXNUM_MAX);
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
	      idio_error_message ("CONSTANT OOB: %" PRIu64 " < %" PRIu64, v, IDIO_FIXNUM_MIN);
	    }
	    IDIO_THREAD_VAL (thr) = IDIO_CONSTANT ((intptr_t) v);
	}
	break;
    case IDIO_A_CONSTANT_0:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 0");
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (0);
	}
	break;
    case IDIO_A_CONSTANT_1:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 1");
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (1);
	}
	break;
    case IDIO_A_CONSTANT_2:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 2");
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (2);
	}
	break;
    case IDIO_A_CONSTANT_3:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 3");
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (3);
	}
	break;
    case IDIO_A_CONSTANT_4:
	{
	    IDIO_VM_RUN_DIS ("CONSTANT 4");
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (4);
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
	    idio_error_message ("continuation?");
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
    case IDIO_A_NON_CONT_ERR:
	{
	    IDIO_VM_RUN_DIS ("NON-CONT-ERROR\n");
	    idio_signal_exception (idio_S_false, idio_struct_instance (idio_condition_idio_error_type, IDIO_LIST3 (idio_string_C ("non-cont-error"), idio_S_nil, idio_S_nil)));
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
		fprintf (stderr, "\n  %5zd ", pc1);
		for (; pc1 < pc; pc1++) {
		    fprintf (stderr, "    ");
		}
	    }
	    for (; pc < pcm; pc++) {
		if (0 == (pc % 10)) {
		    fprintf (stderr, "\n  %5zd ", pc);
		}
		fprintf (stderr, "%3d ", idio_all_code->ae[pc]);
	    }
	    fprintf (stderr, "\n");
	    idio_error_message ("unexpected instruction: %3d @%" PRId64 "\n", ins, IDIO_THREAD_PC (thr) - 1);
	}
	break;
    }

    IDIO_VM_RUN_DIS ("\n");
    if (idio_vm_dis) {
	idio_debug ("vm-run1: after: %s\n", thr); 
    }
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
    idio_ai_t hsp = IDIO_FIXNUM_VAL (IDIO_THREAD_HANDLERSP (thr));
    IDIO_C_ASSERT (hsp <= sp);

    if (0 == hsp) {
	IDIO_THREAD_STACK_PUSH (IDIO_FIXNUM (sp + 1));
	IDIO_THREAD_STACK_PUSH (idio_vm_base_error_handler_primdata);
	IDIO_THREAD_HANDLERSP (thr) = IDIO_FIXNUM (sp + 1);
    }
}

void idio_vm_default_pc (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /*
     * The problem for an external user, eg. idio_meaning_expander, is
     * that if the expander is a primitive then idio_vm_run() pushes
     * idio_finish_pc on the stack expecting the code to run through
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

IDIO idio_vm_run (IDIO thr, int run_gc)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_vm_thread_init (thr);

    idio_ai_t sp0 = idio_array_size (IDIO_THREAD_STACK (thr));

    /*
     * make sure this segment returns to idio_finish_pc
     */
    IDIO_THREAD_STACK_PUSH (IDIO_FIXNUM (idio_finish_pc));
    idio_i_array_push (idio_all_code, IDIO_A_NOP);
    idio_i_array_push (idio_all_code, IDIO_A_RETURN);

    /*
     * Ready ourselves for idio_signal_exception to clear the decks.
     */
    int sjv = setjmp (IDIO_THREAD_JMP_BUF (thr));

    /*
     * Hmm, we really should consider caring whether we got here from
     * a longjump...shouldn't we?
     *
     * I'm not sure we do care.
     */
    switch (sjv) {
    case 0:
	break;
    case IDIO_VM_LONGJMP_SIGNAL_EXCEPTION:
	break;
    default:
	fprintf (stderr, "setjmp: unexpected value: %d\n", sjv);
	break;
    }

    /*
     * Finally, run the VM code.  Every so often we'll poke the GC to
     * tidy up.  It has its own view on whether it is time and/or safe
     * to garbage collect but we need to poke it to find out.
     *
     * Often enough but not too often.
     */
    int loops = 0;
    for (;;) {
	if (idio_vm_run1 (thr)) {
	    sleep (0);
	    if (loops++ > 100) {
		idio_gc_possibly_collect ();
		loops = 0;
	    }
	} else {
	    sleep (0);
	    break;
	}
    }

    /*
     * remove that final return oterwise an innocent might run over
     * it.
     */
    idio_all_code->i -= 2;

    IDIO r = IDIO_THREAD_VAL (thr);
    
    /*
     * Check we are where we think we should be...wherever that might
     * be.
     *
     * There's an element of falling down the rabbit hole here so we
     * do what we can.  We shouldn't be anywhere other than one beyond
     * idio_finish_pc having successfully run through the code we were
     * passed and we shouldn't have left the stack in a mess.
     */
    int bail = 0;
    if (IDIO_THREAD_PC (thr) != (idio_finish_pc + 1)) {
	fprintf (stderr, "vm-run: THREAD FAIL: PC %zu != %" PRIdPTR "\n", IDIO_THREAD_PC (thr), (idio_finish_pc + 1));
	bail = 1;
    }
    
    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (thr));

    if (sp != sp0) {
	fprintf (stderr, "vm-run: THREAD FAIL: SP %" PRIdPTR " != SP0 %" PRIdPTR "\n", sp, sp0);
	idio_dump (IDIO_THREAD_STACK (thr), 1);
	while (sp > sp0) {
	    IDIO v = IDIO_THREAD_STACK_POP ();
	    char *vs = idio_as_string (v, 1);
	    fprintf (stderr, "vm-run: THREAD FAIL: popped %s\n", vs);
	    free (vs);
	    sp--;
	}
	bail = 1;
    }

    if (bail) {
	fprintf (stderr, "vm-run: thread bailed out\n");

	idio_dump (thr, 1);
	sleep (0);
    }

    if (run_gc) {
	idio_gc_collect ();
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
    return idio_array_get_index (idio_vm_values, i);
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

void idio_vm_abort_thread (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO stack = IDIO_THREAD_STACK (thr);
    IDIO env = IDIO_THREAD_ENV (thr);

    fprintf (stderr, "\n\nABORT\n");
    idio_debug ("thread: %s\n", thr);
    idio_debug ("stack: %s\n", stack);

    while (idio_S_nil != env) {
	idio_debug ("env: %s", env);
	env = IDIO_FRAME_NEXT (env);
    }
    
    IDIO_THREAD_PC (thr) = idio_finish_pc;
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
    
    idio_finish_pc = idio_vm_code_prologue (idio_all_code);
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

    idio_vm_closure_name = IDIO_HASH_EQP (256);
    idio_gc_protect (idio_vm_closure_name);
}

void idio_vm_add_primitives ()
{
    IDIO_ADD_SPECIAL_PRIMITIVE (apply);
    IDIO_ADD_SPECIAL_PRIMITIVE (vm_trace);
    IDIO_ADD_SPECIAL_PRIMITIVE (vm_dis);
}

void idio_final_vm ()
{
    fprintf (stderr, "final-vm: created %zu instructions\n", idio_all_code->i);
    idio_i_array_free (idio_all_code);
    fprintf (stderr, "final-vm: created %" PRIdPTR " constants\n", idio_array_size (idio_vm_constants));
    idio_gc_expose (idio_vm_constants);
    fprintf (stderr, "final-vm: created %" PRIdPTR " symbols\n", idio_array_size (idio_vm_symbols));
    idio_gc_expose (idio_vm_symbols);
    idio_gc_expose (idio_vm_values);
    fprintf (stderr, "final-vm: created %" PRIdPTR " primitives\n", idio_array_size (idio_vm_primitives));
    idio_gc_expose (idio_vm_primitives);
    idio_gc_expose (idio_vm_closure_name);
}
