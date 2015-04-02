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

static void idio_error_vm_compile_param_args (char *m)
{
    idio_error_message ("expected arguments: %s", m);
}

static void idio_error_vm_compile_param_type (char *m, IDIO t)
{
    char *ts = idio_display_string (t);
    idio_error_message ("not a %s: %s", m, ts);
    free (ts);
}

static void idio_error_static_invoke (IDIO desc)
{
    idio_error_message ("cannot invoke %s", IDIO_PRIMITIVE_NAME (desc));
}

static void idio_error_arity (size_t given, size_t arity)
{
    idio_error_message ("incorrect arity: %zd for a %zd function", given, arity);
}

typedef struct idio_i_array_s {
    size_t n;
    size_t i;
    IDIO_I *ae;
} idio_i_array_t;
    
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

void idio_i_array_resize (idio_i_array_t *ia)
{
    fprintf (stderr, "idio_i_array_resize: from %zd\n", ia->n);
    ia->n += ia->n >> 1;
    ia->ae = idio_realloc (ia->ae, ia->n);
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

void idio_i_array_append (idio_i_array_t *ia1, idio_i_array_t *ia2)
{
    if ((ia1->n - ia1->i) < ia2->i) {
	idio_i_array_resize (ia1);
    }
    size_t i;
    for (i = 0; i < ia2->i; i++) {
	ia1->ae[ia1->i++] = ia2->ae[i];
    }
}

IDIO_I *idio_i_array_copy_instructions (idio_i_array_t *ia)
{
    IDIO_I *is = idio_alloc (ia->i * sizeof (IDIO_I));

    size_t i;
    for (i = 0; i < ia->i; i++) {
	is[i] = ia->ae[i];
    }

    return is;
}

idio_i_array_t *idio_i_array_compute_varuint (IDIO_I cmd, size_t offset)
{
    idio_i_array_t *ia = idio_i_array (100);

    if (offset > IDIO_I_MAX) {
	idio_error_message ("big cmd: %zd", offset);
	switch (cmd) {
	case IDIO_A_SHORT_JUMP_FALSE: cmd = IDIO_A_LONG_JUMP_FALSE; break;
	case IDIO_A_SHORT_GOTO: cmd = IDIO_A_LONG_GOTO; break;
	case IDIO_A_PREDEFINED: break;
	case IDIO_A_CONSTANT: break;
	default:
	    idio_error_message ("unexpected varuint CMD");
	    return NULL;
	}
	IDIO_IA_PUSH1 (cmd);
	/*
	 * SQLite4 varuint: https://sqlite.org/src4/doc/trunk/www/varint.wiki
	 */
	if (offset <= 240) {
	    IDIO_IA_PUSH1 (offset);
	} else if (offset <= 2287) {
	    IDIO_IA_PUSH1 (((offset -240) / 256) + 241);
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

	    int i;
	    for (i = 0; i < n; i++) {
		IDIO_IA_PUSH1 (offset & 0xff);
		offset >>= 8;
	    }
	}
    } else {
	IDIO_IA_PUSH2 (cmd, offset);
    }
	    

    return ia;
}

#define IDIO_STREQP(s,cs)	(strlen (s) == strlen (cs) && strncmp (s, cs, strlen (cs)) == 0)


/*
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
void idio_vm_compile (IDIO thr, idio_i_array_t *ia, IDIO m)
{
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (pair, m);

    char *ms = idio_as_string (m, 1);
    fprintf (stderr, "compile: %s\n", ms);
    free (ms);
    
    IDIO mh = IDIO_PAIR_H (m);
    IDIO mt = IDIO_PAIR_T (m);

    if (! IDIO_TYPE_CONSTANTP (mh)) {
	char *mhs = idio_as_string (mh, 1);
	fprintf (stderr, "WARNING: unexpected intermediate code: %s", mhs);
	free (mhs);
	return;
    }
    
    switch (IDIO_CONSTANT_VAL (mh)) {
    case IDIO_VM_CODE_SHALLOW_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("SHALLOW-ARGUMENT-REF j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_error_vm_compile_param_type ("fixnum", j);
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (j)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF0); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF1); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF2); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_REF3); break;
	    default:
		IDIO_IA_PUSH2 (IDIO_A_SHALLOW_ARGUMENT_REF, IDIO_FIXNUM_VAL (j));
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_PREDEFINED:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("PREDEFINED i");
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_error_vm_compile_param_type ("fixnum", i);
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (i)) {
	    default:
		IDIO_IA_PUSH2 (IDIO_A_PREDEFINED, IDIO_FIXNUM_VAL (i));
		break;
	    }

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
		idio_i_array_t *iap = idio_i_array_compute_varuint (IDIO_A_PREDEFINED, IDIO_FIXNUM_VAL (i));
		idio_i_array_append (ia, iap);
		idio_i_array_free (iap);
	    }
	}
	break;
    case IDIO_VM_CODE_DEEP_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("DEEP-ARGUMENT-REF i j");
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_error_vm_compile_param_type ("fixnum", i);
		return;
	    }

	    IDIO j = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (j)) {
		idio_error_vm_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO_IA_PUSH3 (IDIO_A_DEEP_ARGUMENT_REF, IDIO_FIXNUM_VAL (i), IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_SHALLOW_ARGUMENT_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("SHALLOW-ARGUMENT-SET j m1");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_error_vm_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1);

	    switch (IDIO_FIXNUM_VAL (j)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET0); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET1); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET2); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET3); break;
	    default:
		IDIO_IA_PUSH2 (IDIO_A_SHALLOW_ARGUMENT_SET, IDIO_FIXNUM_VAL (j));
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_DEEP_ARGUMENT_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_error_vm_compile_param_args ("DEEP-ARGUMENT-SET i j m1");
		return;
	    }
	    
	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_error_vm_compile_param_type ("fixnum", i);
		return;
	    }

	    IDIO j = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (j)) {
		idio_error_vm_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    idio_vm_compile (thr, ia, m1);

	    IDIO_IA_PUSH3 (IDIO_A_DEEP_ARGUMENT_SET, IDIO_FIXNUM_VAL (i), IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_GLOBAL_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("GLOBAL-REF j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_error_vm_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO_IA_PUSH2 (IDIO_A_GLOBAL_REF, IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_CHECKED_GLOBAL_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("CHECKED-GLOBAL-REF j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_error_vm_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO_IA_PUSH2 (IDIO_A_CHECKED_GLOBAL_REF, IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_GLOBAL_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("GLOBAL-SET j m1");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_error_vm_compile_param_type ("fixnum", j);
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    
	    idio_vm_compile (thr, ia, m1);

	    IDIO_IA_PUSH2 (IDIO_A_GLOBAL_SET, IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_CONSTANT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("CONSTANT j");
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    switch ((intptr_t) j & 3) {
	    case IDIO_TYPE_FIXNUM_MARK:
		{
		    int jv = IDIO_FIXNUM_VAL (j);
		    switch (jv) {
		    case -1:
			IDIO_IA_PUSH1 (IDIO_A_CONSTANT_M1);
			return;
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
			break;
		    }
		}
		/* XXX no break -- fall through into CHARACTER */
	    
	    case IDIO_TYPE_CHARACTER_MARK:
		{
		    idio_i_array_t *ia2 = idio_i_array_compute_varuint (IDIO_A_SHORT_NUMBER, (uintptr_t) j);
		    idio_i_array_append (ia, ia2);
		    idio_i_array_free (ia2);

		    return;
		}
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

		    idio_i_array_t *ia2 = idio_i_array_compute_varuint (IDIO_A_SHORT_NUMBER, (uintptr_t) j);
		    idio_i_array_append (ia, ia2);
		    idio_i_array_free (ia2);
		}
		break;
	    default:
		IDIO_THREAD_CONSTANTS (thr) = idio_pair (j, IDIO_THREAD_CONSTANTS (thr));
		size_t l = idio_list_length (IDIO_THREAD_CONSTANTS (thr));
		IDIO_IA_PUSH2 (IDIO_A_CONSTANT, l - 1);
		return;
	    }
	}
	break;
    case IDIO_VM_CODE_ALTERNATIVE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_error_vm_compile_param_args ("ALTERNATIVE m1 m2 m3");
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

	     */
	    
	    idio_vm_compile (thr, ia, m1);

	    idio_i_array_t *ia2 = idio_i_array (100);
	    idio_vm_compile (thr, ia2, m2);
	    idio_i_array_t *ia3 = idio_i_array (100);
	    idio_vm_compile (thr, ia3, m3);

	    idio_i_array_t *g7 = idio_i_array_compute_varuint (IDIO_A_SHORT_GOTO, ia3->i);
	    
	    idio_i_array_t *jf6 = idio_i_array_compute_varuint (IDIO_A_SHORT_JUMP_FALSE, ia2->i + g7->i);

	    idio_i_array_append (ia, jf6);
	    idio_i_array_append (ia, ia2);
	    idio_i_array_append (ia, g7);
	    idio_i_array_append (ia, ia3);

	    idio_i_array_free (ia2);
	    idio_i_array_free (ia3);
	    idio_i_array_free (g7);
	    idio_i_array_free (jf6);
	}
	break;
    case IDIO_VM_CODE_SEQUENCE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("SEQUENCE m1 m+");
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1);
	    idio_vm_compile (thr, ia, mp);
	}
	break;
    case IDIO_VM_CODE_TR_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("TR-FIX-LET m* m+");
		return;
	    }
	    
	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, ms);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_ENV);
	    idio_vm_compile (thr, ia, mp);
	}
	break;
    case IDIO_VM_CODE_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("FIX-LET m* m+");
		return;
	    }
	    
	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, ms);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_ENV);
	    idio_vm_compile (thr, ia, mp);
	    IDIO_IA_PUSH1 (IDIO_A_UNLINK_ENV);
	}
	break;
    case IDIO_VM_CODE_CALL0:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("CALL0 address");
		return;
	    }
	    
	    IDIO a = IDIO_PAIR_H (mt);

	    char *name = IDIO_PRIMITIVE_NAME (a);
	    
	    if (IDIO_STREQP (name, "read")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL0_READ);
	    } else if (IDIO_STREQP (name, "newline")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL0_NEWLINE);
	    } else {
		idio_error_static_invoke (a);
		return;
	    }
	}
	break;
    case IDIO_VM_CODE_CALL1:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("CALL1 address m1");
		return;
	    }
	    
	    IDIO a = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1);
	    
	    char *name = IDIO_PRIMITIVE_NAME (a);
	    
	    if (IDIO_STREQP (name, "car")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_CAR);
	    } else if (IDIO_STREQP (name, "cdr")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_CDR);
	    } else if (IDIO_STREQP (name, "pair?")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_PAIRP);
	    } else if (IDIO_STREQP (name, "symbol?")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_SYMBOLP);
	    } else if (IDIO_STREQP (name, "display")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_DISPLAY);
	    } else if (IDIO_STREQP (name, "primitive?")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_PRIMITIVEP);
	    } else if (IDIO_STREQP (name, "null?")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_NULLP);
	    } else if (IDIO_STREQP (name, "continuation?")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_CONTINUATIONP);
	    } else if (IDIO_STREQP (name, "eof?")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL1_EOFP);
	    } else {
		idio_error_static_invoke (a);
		return;
	    }
	}
	break;
    case IDIO_VM_CODE_CALL2:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_error_vm_compile_param_args ("CALL2 address m1 m2");
		return;
	    }
	    
	    IDIO a = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO m2 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));
	    
	    idio_vm_compile (thr, ia, m1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, m2);
	    IDIO_IA_PUSH1 (IDIO_A_POP_REG1);
	    
	    char *name = IDIO_PRIMITIVE_NAME (a);
	    
	    if (IDIO_STREQP (name, "cons")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_CONS);
	    } else if (IDIO_STREQP (name, "eq?")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_EQP);
	    } else if (IDIO_STREQP (name, "set-car!")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_SET_CAR);
	    } else if (IDIO_STREQP (name, "set-cdr!")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_SET_CDR);
	    } else if (IDIO_STREQP (name, "+")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_ADD);
	    } else if (IDIO_STREQP (name, "-")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_SUBTRACT);
	    } else if (IDIO_STREQP (name, "=")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_EQ);
	    } else if (IDIO_STREQP (name, "<")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_LT);
	    } else if (IDIO_STREQP (name, ">")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_GT);
	    } else if (IDIO_STREQP (name, "*")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_MULTIPLY);
	    } else if (IDIO_STREQP (name, "<=")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_LE);
	    } else if (IDIO_STREQP (name, ">=")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_GE);
	    } else if (IDIO_STREQP (name, "remainder")) {
		IDIO_IA_PUSH1 (IDIO_A_CALL2_REMAINDER);
	    } else {
		idio_error_static_invoke (a);
		return;
	    }
	}
	break;
    case IDIO_VM_CODE_CALL3:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_error_vm_compile_param_args ("CALL3 address m1 m2 m3");
		return;
	    }
	    
	    IDIO a = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO m2 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));
	    IDIO m3 = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (IDIO_PAIR_T (mt))));

	    idio_vm_compile (thr, ia, m1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, m2);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, m3);
	    IDIO_IA_PUSH2 (IDIO_A_POP_REG2, IDIO_A_POP_REG1);

	    char *as = idio_as_string (a, 1);
	    idio_error_message ("there are no CALL3 invocations: CALL3 %s m1 m2 m3", as);
	    free (as);
	    return;
	}
	break;
    case IDIO_VM_CODE_FIX_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("FIX-CLOSURE m+ arity");
		return;
	    }
	    
	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (arity)) {
		idio_error_vm_compile_param_type ("fixnum", arity);
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
	    idio_i_array_push (iap, IDIO_A_ARITYEQP);
	    idio_i_array_push (iap, IDIO_FIXNUM_VAL (arity) + 1);
	    idio_i_array_push (iap, IDIO_A_EXTEND_ENV);
	    idio_vm_compile (thr, iap, mp);
	    idio_i_array_push (iap, IDIO_A_RETURN);

	    idio_i_array_t *g5 = idio_i_array_compute_varuint (IDIO_A_SHORT_GOTO, iap->i);
	    IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, g5->i);
	    idio_i_array_append (ia, g5);
	    idio_i_array_append (ia, iap);

	    idio_i_array_free (iap);
	    idio_i_array_free (g5);
	}
	break;
    case IDIO_VM_CODE_NARY_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("NARY-CLOSURE m+ arity");
		return;
	    }
	    
	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    if (! idio_isa_fixnum (arity)) {
		idio_error_vm_compile_param_type ("fixnum", arity);
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
	    idio_i_array_push (iap, IDIO_FIXNUM_VAL (arity) + 1);
	    idio_i_array_push (iap, IDIO_A_PACK_FRAME);
	    idio_i_array_push (iap, IDIO_FIXNUM_VAL (arity));
	    idio_i_array_push (iap, IDIO_A_EXTEND_ENV);
	    idio_vm_compile (thr, iap, mp);
	    idio_i_array_push (iap, IDIO_A_RETURN);

	    idio_i_array_t *g5 = idio_i_array_compute_varuint (IDIO_A_SHORT_GOTO, iap->i);
	    IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, g5->i);
	    idio_i_array_append (ia, g5);
	    idio_i_array_append (ia, iap);

	    idio_i_array_free (iap);
	    idio_i_array_free (g5);
	}
	break;
    case IDIO_VM_CODE_TR_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("TR-REGULAR-CALL m1 m*");
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms);
	    IDIO_IA_PUSH2 (IDIO_A_POP_FUNCTION, IDIO_A_FUNCTION_GOTO);
	}
	break;
    case IDIO_VM_CODE_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("REGULAR-CALL m1 m*");
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));

	    idio_vm_compile (thr, ia, m1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms);
	    IDIO_IA_PUSH4 (IDIO_A_POP_FUNCTION, IDIO_A_PRESERVE_ENV, IDIO_A_FUNCTION_INVOKE, IDIO_A_RESTORE_ENV);
	}
	break;
    case IDIO_VM_CODE_STORE_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_error_vm_compile_param_args ("STORE-ARGUMENT m1 m* rank");
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO rank = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    if (! idio_isa_fixnum (rank)) {
		idio_error_vm_compile_param_type ("fixnum", rank);
		return;
	    }

	    idio_vm_compile (thr, ia, m1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms);
	    switch (IDIO_FIXNUM_VAL (rank)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME0); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME1); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME2); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_POP_FRAME3); break;
	    default:
		IDIO_IA_PUSH2 (IDIO_A_POP_FRAME, IDIO_FIXNUM_VAL (rank));
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_CONS_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_error_vm_compile_param_args ("CONS-ARGUMENT m1 m* arity");
		return;
	    }
	    
	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_H (IDIO_PAIR_T (mt));
	    IDIO arity = IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (mt)));

	    idio_vm_compile (thr, ia, m1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_vm_compile (thr, ia, ms);
	    IDIO_IA_PUSH2 (IDIO_A_POP_CONS_FRAME, IDIO_FIXNUM_VAL (arity));
	}
	break;
    case IDIO_VM_CODE_ALLOCATE_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("ALLOCATE-FRAME size");
		return;
	    }
	    
	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_error_vm_compile_param_type ("fixnum", size);
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (size)) {
	    case 0: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME1); break;
	    case 1: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME2); break;
	    case 2: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME3); break;
	    case 3: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME4); break;
	    case 4: IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME5); break;
	    default:
		IDIO_IA_PUSH2 (IDIO_A_ALLOCATE_FRAME, IDIO_FIXNUM_VAL (size) + 1);
		break;
	    }
	}
	break;
    case IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_error_vm_compile_param_args ("ALLOCATE-DOTTED-FRAME size");
		return;
	    }
	    
	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_error_vm_compile_param_type ("fixnum", size);
		return;
	    }

	    IDIO_IA_PUSH2 (IDIO_A_ALLOCATE_DOTTED_FRAME, IDIO_FIXNUM_VAL (size) + 1);
	}
	break;
    case IDIO_VM_CODE_FINISH:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 0) {
		idio_error_vm_compile_param_args ("FINISH");
		return;
	    }
	    
	    IDIO_IA_PUSH1 (IDIO_A_FINISH);
	}
	break;
    default:
	idio_error_message ("bad instruction: %s", idio_as_string (mh, 1));
	break;
    }
}

idio_ai_t idio_vm_code_prologue (idio_i_array_t *ia)
{
    IDIO_IA_PUSH2 (IDIO_A_NOP, IDIO_A_FINISH);

    /* index of IDIO_A_FINISH */
    return 1;
}

#define idio_thread_fetch_next()	(*IDIO_THREAD_PC(thr)++)
#define idio_thread_stack_push(v)	(idio_array_push (IDIO_THREAD_STACK(thr), v))
#define idio_thread_stack_pop()		(idio_array_pop (IDIO_THREAD_STACK(thr)))

void idio_vm_codegen (IDIO thr, IDIO m)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (pair, m);
    
    char *ms = idio_as_string (m, 1);
    fprintf (stderr, "codegen: in %s\n", ms);
    free (ms);

    idio_i_array_t *ia = idio_i_array (100);
    idio_ai_t finish_pc = idio_vm_code_prologue (ia);
    size_t prologue_len = ia->i;
    
    idio_thread_stack_push (IDIO_FIXNUM (finish_pc));
    
    idio_vm_compile (idio_current_thread (), ia, m);
    fprintf (stderr, "vm-codegen: %zd ins: ", ia->i);
    size_t i;
    for (i = 0; i < ia->i; i++) {
	fprintf (stderr, "%3d ", ia->ae[i]);
    }
    fprintf (stderr, "\n");
    sleep (0);

    IDIO_IA_PUSH1 (IDIO_A_RETURN);
    IDIO_THREAD_PC (thr) = idio_i_array_copy_instructions (ia);
    IDIO_THREAD_PC (thr) += prologue_len;
    idio_i_array_free (ia);
}

static size_t idio_thread_fetch_varuint (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    int i = idio_thread_fetch_next ();
    if (i <= 240) {
	return i;
    } else if (i <= 248) {
	int j = idio_thread_fetch_next ();

	return (240 + 256 * (i - 240) + j);
    } else if (249 == i) {
	int j = idio_thread_fetch_next ();
	int k = idio_thread_fetch_next ();

	return (2288 + 256 * j + k);
    } else {
	int n = (i - 250) + 3;

	int r = 0;
	for (i = 0; i < n; i++) {
	    r <<= 8;
	    r |= idio_thread_fetch_next ();
	}

	return r;
    }
}

/*
 * For a function (define (func x . rest))
 *
 * (func a b c d) => (func a (b c d))
 */
void idio_thread_listify (IDIO frame, size_t arity)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);
    
    fprintf (stderr, "listify! %zd\n", arity);
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

void idio_thread_invoke (IDIO thr, IDIO func, int tailp)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (func);
    IDIO_TYPE_ASSERT (thread, thr);

    switch ((intptr_t) func & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CHARACTER_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    char *funcs = idio_as_string (func, 1);
	    idio_error_message ("cannot invoke constant type: %s", funcs);
	    free (funcs);
	    return;
	}
    default:
	break;
    }

    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
	{
	    if (0 == tailp) {
		idio_thread_stack_push ((IDIO) IDIO_THREAD_PC (thr));
	    }
	    IDIO_THREAD_ENV (thr) = IDIO_CLOSURE_ENV (func);
	    IDIO_THREAD_PC (thr) = IDIO_CLOSURE_CODE (func);
	}
	break;
    default:
	{
	    char *funcs = idio_as_string (func, 1);
	    idio_error_message ("cannot invoke: %s", funcs);
	    free (funcs);
	}
	break;
    }
}

void idio_vm_run1 (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO_I ins = idio_thread_fetch_next ();

    fprintf (stderr, "idio_vm_run1: %p %3d\n", thr, ins);
    fprintf (stderr, "before: ");
    idio_dump (thr, 1);

    switch (ins) {
    case IDIO_A_SHALLOW_ARGUMENT_REF0:
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 0);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF1:
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 1);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF2:
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 2);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF3:
	IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, 3);
	break;
    case IDIO_A_SHALLOW_ARGUMENT_REF:
	{
	    int j = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), 0, j);
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_REF:
	{
	    int i = idio_thread_fetch_next ();
	    int j = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_frame_fetch (IDIO_THREAD_ENV (thr), i, j);
	}
	break;
    case IDIO_A_GLOBAL_REF:
	{
	    int i = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_toplevel_ref (i);
	}
	break;
    case IDIO_A_CHECKED_GLOBAL_REF:
	{
	    int i = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_toplevel_ref (i);
	    if (idio_S_undef == IDIO_THREAD_VAL (thr)) {
		idio_error_message ("undefined toplevel: %d", i);
	    }
	}
	break;
    case IDIO_A_CONSTANT:
	{
	    int i = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_array_get_index (IDIO_THREAD_CONSTANTS (thr), i);
	}
	break;
    case IDIO_A_PREDEFINED0:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_true;
	}
	break;
    case IDIO_A_PREDEFINED1:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	}
	break;
    case IDIO_A_PREDEFINED2:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_nil;
	}
	break;
    case IDIO_A_PREDEFINED3:
	{
	    IDIO_THREAD_VAL (thr) = idio_predef_ref (3);
	    idio_error_message ("predef 3");
	}
	break;
    case IDIO_A_PREDEFINED4:
	{
	    IDIO_THREAD_VAL (thr) = idio_predef_ref (4);
	    idio_error_message ("predef 4");
	}
	break;
    case IDIO_A_PREDEFINED5:
	{
	    IDIO_THREAD_VAL (thr) = idio_predef_ref (5);
	    idio_error_message ("predef 5");
	}
	break;
    case IDIO_A_PREDEFINED6:
	{
	    IDIO_THREAD_VAL (thr) = idio_predef_ref (6);
	    idio_error_message ("predef 6");
	}
	break;
    case IDIO_A_PREDEFINED7:
	{
	    IDIO_THREAD_VAL (thr) = idio_predef_ref (7);
	    idio_error_message ("predef 7");
	}
	break;
    case IDIO_A_PREDEFINED8:
	{
	    IDIO_THREAD_VAL (thr) = idio_predef_ref (8);
	    idio_error_message ("predef 8");
	}
	break;
    case IDIO_A_PREDEFINED:
	{
	    int i = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_predef_ref (i);
	}
	break;
    case IDIO_A_FINISH:
	{
	    idio_error_message ("FINISH");
	}
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET0:
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 0, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET1:
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 1, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET2:
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 2, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET3:
	idio_frame_update (IDIO_THREAD_ENV (thr), 0, 3, IDIO_THREAD_VAL (thr));
	break;
    case IDIO_A_SHALLOW_ARGUMENT_SET:
	{
	    int i = idio_thread_fetch_next ();
	    idio_frame_update (IDIO_THREAD_ENV (thr), 0, i, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_DEEP_ARGUMENT_SET:
	{
	    int i = idio_thread_fetch_next ();
	    int j = idio_thread_fetch_next ();
	    idio_frame_update (IDIO_THREAD_ENV (thr), i, j, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_GLOBAL_SET:
	{
	    int i = idio_thread_fetch_next ();
	    idio_toplevel_update (i, IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_LONG_GOTO:
	{
	    int i = idio_thread_fetch_varuint (thr);
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_LONG_JUMP_FALSE:
	{
	    int i = idio_thread_fetch_varuint (thr);
	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_SHORT_GOTO:
	{
	    int i = idio_thread_fetch_next ();
	    IDIO_THREAD_PC (thr) += i;
	}
	break;
    case IDIO_A_SHORT_JUMP_FALSE:
	{
	    int i = idio_thread_fetch_next ();
	    if (idio_S_false != IDIO_THREAD_VAL (thr)) {
		IDIO_THREAD_PC (thr) += i;
	    }
	}
	break;
    case IDIO_A_EXTEND_ENV:
	{
	    idio_frame_extend (IDIO_THREAD_ENV (thr), IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_UNLINK_ENV:
	{
	    IDIO_THREAD_ENV (thr) = IDIO_FRAME_NEXT (IDIO_THREAD_ENV (thr));
	}
	break;
    case IDIO_A_PUSH_VALUE:
	{
	    idio_thread_stack_push (IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_POP_REG1:
	{
	    IDIO_THREAD_REG1 (thr) = idio_thread_stack_pop ();
	}
	break;
    case IDIO_A_POP_REG2:
	{
	    IDIO_THREAD_REG2 (thr) = idio_thread_stack_pop ();
	}
	break;
    case IDIO_A_PRESERVE_ENV:
	{
	    idio_thread_stack_push (IDIO_THREAD_ENV (thr));
	}
	break;
    case IDIO_A_RESTORE_ENV:
	{
	    IDIO_THREAD_ENV (thr) = idio_thread_stack_pop ();
	}
	break;
    case IDIO_A_POP_FUNCTION:
	{
	    IDIO_THREAD_FUNC (thr) = idio_thread_stack_pop ();
	}
	break;
    case IDIO_A_CREATE_CLOSURE:
	{
	    int i = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_closure (IDIO_THREAD_PC (thr) + i, IDIO_THREAD_ENV (thr));
	}
	break;
    case IDIO_A_RETURN:
	{
	    fprintf (stderr, "RETURN\n");
	    IDIO_THREAD_PC (thr) = (IDIO_I *) idio_thread_stack_pop ();
	}
	break;
    case IDIO_A_PACK_FRAME:
	{
	    int arity = idio_thread_fetch_next ();
	    idio_thread_listify (IDIO_THREAD_VAL (thr), arity);
	}
	break;
    case IDIO_A_FUNCTION_INVOKE:
	{
	    idio_thread_invoke (thr, IDIO_THREAD_FUNC (thr), 0);
	}
	break;
    case IDIO_A_FUNCTION_GOTO:
	{
	    idio_thread_invoke (thr, IDIO_THREAD_FUNC (thr), 1);
	}
	break;
    case IDIO_A_POP_CONS_FRAME:
	{
	    int arity = idio_thread_fetch_next ();
	    
	    idio_frame_update (IDIO_THREAD_VAL (thr),
			       0,
			       arity,
			       idio_pair (idio_thread_stack_pop (),
					  idio_frame_fetch (IDIO_THREAD_VAL (thr), 0, arity)));
	}
	break;
    case IDIO_A_ALLOCATE_FRAME1:
	{
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (1);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME2:
	{
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (2);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME3:
	{
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (3);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME4:
	{
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (4);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME5:
	{
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (5);
	}
	break;
    case IDIO_A_ALLOCATE_FRAME:
	{
	    int i = idio_thread_fetch_next ();
	    IDIO_THREAD_VAL (thr) = idio_frame_allocate (i);
	}
	break;
    case IDIO_A_ALLOCATE_DOTTED_FRAME:
	{
	    int arity = idio_thread_fetch_next ();
	    IDIO vs = idio_frame_allocate (arity);
	    idio_frame_update (vs, 0, arity - 1, idio_S_nil);
	    IDIO_THREAD_VAL (thr) = vs;
	}
	break;
    case IDIO_A_POP_FRAME0:
	{
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 0, idio_thread_stack_pop ());
	}
	break;
    case IDIO_A_POP_FRAME1:
	{
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 1, idio_thread_stack_pop ());
	}
	break;
    case IDIO_A_POP_FRAME2:
	{
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 2, idio_thread_stack_pop ());
	}
	break;
    case IDIO_A_POP_FRAME3:
	{
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, 3, idio_thread_stack_pop ());
	}
	break;
    case IDIO_A_POP_FRAME:
	{
	    int rank = idio_thread_fetch_next ();
	    idio_frame_update (IDIO_THREAD_VAL (thr), 0, rank, idio_thread_stack_pop ());
	}
	break;
    case IDIO_A_ARITYP1:
	{
	    if (1 != IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr))) {
		idio_error_arity (IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr)), 1);
		return;
	    }
	}
	break;
    case IDIO_A_ARITYP2:
	{
	    if (2 != IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr))) {
		idio_error_arity (IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr)), 2);
		return;
	    }
	}
	break;
    case IDIO_A_ARITYP3:
	{
	    if (3 != IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr))) {
		idio_error_arity (IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr)), 3);
		return;
	    }
	}
	break;
    case IDIO_A_ARITYP4:
	{
	    if (4 != IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr))) {
		idio_error_arity (IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr)), 4);
		return;
	    }
	}
	break;
    case IDIO_A_ARITYEQP:
	{
	    int arityp1 = idio_thread_fetch_next ();
	    if (arityp1 != IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr))) {
		idio_error_arity (IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr)), arityp1 - 1);
		return;
	    }
	}
	break;
    case IDIO_A_ARITYGEP:
	{
	    int arityp1 = idio_thread_fetch_next ();
	    if (arityp1 < IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr))) {
		idio_error_arity (IDIO_FRAME_NARGS (IDIO_THREAD_VAL (thr)), arityp1 - 1);
		return;
	    }
	}
	break;
    case IDIO_A_SHORT_NUMBER:
	{
	    uintptr_t v = idio_thread_fetch_varuint (thr);
	    IDIO_THREAD_VAL (thr) = (IDIO) v;
	}
	break;
    case IDIO_A_CONSTANT_M1:
	{
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (-1);
	}
	break;
    case IDIO_A_CONSTANT_0:
	{
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (0);
	}
	break;
    case IDIO_A_CONSTANT_1:
	{
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (1);
	}
	break;
    case IDIO_A_CONSTANT_2:
	{
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (2);
	}
	break;
    case IDIO_A_CONSTANT_4:
	{
	    IDIO_THREAD_VAL (thr) = IDIO_FIXNUM (4);
	}
	break;
    case IDIO_A_CALL0_NEWLINE:
	{
	    IDIO_THREAD_VAL (thr) = idio_character_lookup ("newline");
	}
	break;
    case IDIO_A_CALL0_READ:
	{
	    IDIO_THREAD_VAL (thr) = idio_scm_read (idio_current_input_handle ());
	}
	break;
    case IDIO_A_CALL1_CAR:
	{
	    IDIO_THREAD_VAL (thr) = idio_list_head (IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_CALL1_CDR:
	{
	    IDIO_THREAD_VAL (thr) = idio_list_tail (IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_CALL1_PAIRP:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	    if (idio_isa_pair (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    }
	}
	break;
    case IDIO_A_CALL1_SYMBOLP:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	    if (idio_isa_symbol (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    }
	}
	break;
    case IDIO_A_CALL1_DISPLAY:
	{
	    idio_error_message ("display");
	}
	break;
    case IDIO_A_CALL1_PRIMITIVEP:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	    if (idio_isa_primitive (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    }
	}
	break;
    case IDIO_A_CALL1_CONTINUATIONP:
	{
	    idio_error_message ("continuation?");
	}
	break;
    case IDIO_A_CALL1_EOFP:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	    if (idio_handle_eofp (IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    }
	}
	break;
    case IDIO_A_CALL2_CONS:
	{
	    IDIO_THREAD_VAL (thr) = idio_pair (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_CALL2_EQP:
	{
	    IDIO_THREAD_VAL (thr) = idio_S_false;
	    if (idio_eqp (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr))) {
		IDIO_THREAD_VAL (thr) = idio_S_true;
	    }
	}
	break;
    case IDIO_A_CALL2_SET_CAR:
	{
	    IDIO_THREAD_VAL (thr) = idio_pair_set_head (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_CALL2_SET_CDR:
	{
	    IDIO_THREAD_VAL (thr) = idio_pair_set_tail (IDIO_THREAD_REG1 (thr), IDIO_THREAD_VAL (thr));
	}
	break;
    case IDIO_A_CALL2_ADD:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_add (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									   IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_SUBTRACT:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_subtract (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
										IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_EQ:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_eq (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									  IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_LT:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_lt (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									  IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_GT:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_gt (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									  IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_MULTIPLY:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_multiply (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
										IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_LE:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_le (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									  IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_GE:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_ge (IDIO_LIST2 (IDIO_THREAD_REG1 (thr),
									  IDIO_THREAD_VAL (thr)));
	}
	break;
    case IDIO_A_CALL2_REMAINDER:
	{
	    IDIO_THREAD_VAL (thr) = idio_fixnum_primitive_remainder (IDIO_THREAD_REG1 (thr),
								     IDIO_THREAD_VAL (thr));
	}
	break;
    default:
	idio_error_message ("unexpected instruction: %3d\n", ins);
	break;
    }

    fprintf (stderr, "after:  ");
    idio_dump (thr, 1);
    return;
}

void idio_vm_run (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    for (;;) {
	idio_vm_run1 (thr);
	sleep (1);
    }
}

void idio_init_vm ()
{
}

void idio_final_vm ()
{
}
