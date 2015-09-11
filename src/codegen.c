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
 * codegen.c
 * 
 */

#include "idio.h"

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

idio_i_array_t *idio_i_array_compute_fixuint (int n, uint64_t offset)
{
    IDIO_C_ASSERT (n < 9 && n > 0);
    
    idio_i_array_t *ia = idio_i_array (n);

    ia->i = n;
    for (n--; n >= 0; n--) {
	ia->ae[n] = offset & 0xff;
	offset >>= 8;
    }

    /*
     * Check there's nothing left!
     */
    IDIO_C_ASSERT (0 == offset);

    return ia;
}

idio_i_array_t *idio_i_array_compute_8uint (uint64_t offset)
{
    return idio_i_array_compute_fixuint (1, offset);
}

idio_i_array_t *idio_i_array_compute_16uint (uint64_t offset)
{
    return idio_i_array_compute_fixuint (2, offset);
}

idio_i_array_t *idio_i_array_compute_32uint (uint64_t offset)
{
    return idio_i_array_compute_fixuint (4, offset);
}

idio_i_array_t *idio_i_array_compute_64uint (uint64_t offset)
{
    return idio_i_array_compute_fixuint (8, offset);
}

#define IDIO_IA_PUSH_VARUINT(n)    { idio_i_array_t *ia2 = idio_i_array_compute_varuint (n); idio_i_array_append (ia, ia2); idio_i_array_free (ia2); }
#define IDIO_IA_PUSH_8UINT(n)    { idio_i_array_t *ia2 = idio_i_array_compute_8uint (n); idio_i_array_append (ia, ia2); idio_i_array_free (ia2); }
#define IDIO_IA_PUSH_16UINT(n)    { idio_i_array_t *ia2 = idio_i_array_compute_16uint (n); idio_i_array_append (ia, ia2); idio_i_array_free (ia2); }
#define IDIO_IA_PUSH_32UINT(n)    { idio_i_array_t *ia2 = idio_i_array_compute_32uint (n); idio_i_array_append (ia, ia2); idio_i_array_free (ia2); }
#define IDIO_IA_PUSH_64UINT(n)    { idio_i_array_t *ia2 = idio_i_array_compute_64uint (n); idio_i_array_append (ia, ia2); idio_i_array_free (ia2); }

/*
 * Check this aligns with IDIO_VM_FETCH_REF
 *
 * At the time of writing the tests used around 2000 symbols in a
 * couple of modules
 */
#define IDIO_IA_PUSH_REF(n)	IDIO_IA_PUSH_16UINT (n)

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
    
    if (! idio_isa_pair (m)) {
	idio_error_param_type ("pair", IDIO_LIST1 (m), IDIO_C_LOCATION ("idio_vm_compile"));

	/* notreached */
	return;
    }

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
		idio_vm_error_compile_param_args ("PREDEFINED vi", mt, IDIO_C_LOCATION ("idio_vm_compile/PREDEFINED"));
		return;
	    }
	    
	    IDIO vi = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (vi)) {
		idio_vm_error_compile_param_type ("fixnum", vi, IDIO_C_LOCATION ("idio_vm_compile/PREDEFINED"));
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (vi)) {
	    default:
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (vi));
		break;
	    }

	    /*
	     * Specialization.  Or will be...
	     */
	    if (idio_S_true == vi) {
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED0);
	    } else if (idio_S_false == vi) {
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED1);
	    } else if (idio_S_nil == vi) {
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED2);
	    } else if (idio_S_nil == vi) { /* cons */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED3);
	    } else if (idio_S_nil == vi) { /* car */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED4);
	    } else if (idio_S_nil == vi) { /* cdr */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED5);
	    } else if (idio_S_nil == vi) { /* pair? */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED6);
	    } else if (idio_S_nil == vi) { /* symbol? */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED7);
	    } else if (idio_S_nil == vi) { /* eq? */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED8);
	    } else {
		/* see above */
		/* IDIO_IA_PUSH1 (IDIO_A_PREDEFINED); */
		/* IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (vi)); */
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

	    IDIO j = IDIO_PAIR_HT (mt);

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

	    IDIO m1 = IDIO_PAIR_HT (mt);

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

	    IDIO j = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/DEEP-ARGUMENT-SET"));
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HTT (mt);

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
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (j));
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
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (j));
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
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (j));
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
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_GLOBAL_DEF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("GLOBAL-DEF name kind", mt, IDIO_C_LOCATION ("idio_vm_compile/GLOBAL-DEF"));
		return;
	    }
	    
	    IDIO name = IDIO_PAIR_H (mt);
	    IDIO kind = IDIO_PAIR_HT (mt);

	    if (! idio_isa_symbol (name)) {
		idio_vm_error_compile_param_type ("symbol", name, IDIO_C_LOCATION ("idio_vm_compile/GLOBAL-DEF"));
		return;
	    }

	    if (! (idio_S_predef == kind ||
		   idio_S_toplevel == kind ||
		   idio_S_dynamic == kind ||
		   idio_S_environ == kind ||
		   idio_S_computed == kind)) {
		idio_vm_error_compile_param_type ("symbol predef|toplevel|dynamic|environ|computed", kind, IDIO_C_LOCATION ("idio_vm_compile/GLOBAL-DEF"));
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_DEF);
	    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
	    IDIO_IA_PUSH_REF (mci);
	    mci = idio_vm_constants_lookup_or_extend (kind);
	    IDIO_IA_PUSH_VARUINT (mci);
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

	    IDIO m1 = IDIO_PAIR_HT (mt);
	    
	    idio_vm_compile (thr, ia, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_SET);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (j));
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

	    IDIO m1 = IDIO_PAIR_HT (mt);
	    
	    idio_vm_compile (thr, ia, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_SET);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_VM_CODE_COMPUTED_DEFINE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("COMPUTED-DEFINE j m1", mt, IDIO_C_LOCATION ("idio_vm_compile/COMPUTED-DEFINE"));
		return;
	    }
	    
	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_vm_error_compile_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_vm_compile/COMPUTED-DEFINE"));
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);
	    
	    idio_vm_compile (thr, ia, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_DEFINE);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (j));
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
		    idio_ai_t gci = idio_vm_constants_lookup_or_extend (j);
		    IDIO fgci = idio_fixnum (gci);
		    idio_module_vci_set (idio_thread_current_env (), fgci, fgci);
		    
		    IDIO_IA_PUSH1 (IDIO_A_CONSTANT_REF);
		    IDIO_IA_PUSH_VARUINT (gci);
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
	    IDIO m2 = IDIO_PAIR_HT (mt);
	    IDIO m3 = IDIO_PAIR_HTT (mt);

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
	    IDIO mp = IDIO_PAIR_HT (mt);

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
	    IDIO mp = IDIO_PAIR_HT (mt);

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
	    IDIO mp = IDIO_PAIR_HT (mt);

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
	    IDIO m1 = IDIO_PAIR_HT (mt);

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
	    
	    IDIO m1 = IDIO_PAIR_HT (mt);
	    IDIO m2 = IDIO_PAIR_HTT (mt);
	    
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
	    
	    IDIO m1 = IDIO_PAIR_HT (mt);
	    IDIO m2 = IDIO_PAIR_HTT (mt);
	    IDIO m3 = IDIO_PAIR_HTTT (mt);

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
	    IDIO arity = IDIO_PAIR_HT (mt);

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
	    IDIO arity = IDIO_PAIR_HT (mt);

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
	    IDIO ms = IDIO_PAIR_HT (mt);

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
	    IDIO ms = IDIO_PAIR_HT (mt);

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
	    IDIO ms = IDIO_PAIR_HT (mt);
	    IDIO rank = IDIO_PAIR_HTT (mt);

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
	    IDIO ms = IDIO_PAIR_HT (mt);
	    IDIO arity = IDIO_PAIR_HTT (mt);

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
		idio_vm_error_compile_param_args ("PUSH-DYNAMIC mci m", mt, IDIO_C_LOCATION ("idio_vm_compile/PUSH-DYNAMIC"));
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);
	    IDIO m = IDIO_PAIR_HT (mt);

	    idio_vm_compile (thr, ia, m, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_DYNAMIC);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
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
		idio_vm_error_compile_param_args ("DYNAMIC-REF mci", mt, IDIO_C_LOCATION ("idio_vm_compile/DYNAMIC-REF"));
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_VM_CODE_DYNAMIC_FUNCTION_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_vm_error_compile_param_args ("DYNAMIC-FUNCTION-REF mci", mt, IDIO_C_LOCATION ("idio_vm_compile/DYNAMIC-FUNCTION-REF"));
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_FUNCTION_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_VM_CODE_PUSH_ENVIRON:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_vm_error_compile_param_args ("PUSH-ENVIRON mci m", mt, IDIO_C_LOCATION ("idio_vm_compile/PUSH-ENVIRON"));
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);
	    IDIO m = IDIO_PAIR_HT (mt);

	    idio_vm_compile (thr, ia, m, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_ENVIRON);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
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
		idio_vm_error_compile_param_args ("ENVIRON-REF mci", mt, IDIO_C_LOCATION ("idio_vm_compile/ENVIRON-REF"));
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);
	    
	    IDIO_IA_PUSH1 (IDIO_A_ENVIRON_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
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
	    IDIO m = IDIO_PAIR_HT (mt);
	    
	    idio_vm_compile (thr, ia, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXPANDER);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (i));
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
	    IDIO p = IDIO_PAIR_HT (mt);
	    IDIO m = IDIO_PAIR_HTT (mt);
	    
	    idio_vm_compile (thr, ia, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_INFIX_OPERATOR);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (i));
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
	    IDIO p = IDIO_PAIR_HT (mt);
	    IDIO m = IDIO_PAIR_HTT (mt);
	    
	    idio_vm_compile (thr, ia, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POSTFIX_OPERATOR);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (i));
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

void idio_vm_code_prologue (idio_i_array_t *ia)
{
    /*
     * Out of interest a PC on the stack is indistinguishable from any
     * other (fixnum) number.  However, the particular numbers for
     * idio_vm_{CR,AR,IHR}_pc should be identifiable because of what
     * precedes them on the stack.
     *
     * In each case you'll see some combination of what part of the
     * VM's state has been preserved.  Where IDIO_A_RESTORE_STATE is
     * used you'd expect to see instances of ENVIRON_SP, DYNAMIC_SP,
     * HANDLER_SP, FRAME and ENV; and all of those should be preceded
     * by the real PC of the calling function: ie. <number:PC>
     * <number:ESP> <number:DSP> <number:HSP> <#FRAME> <#module>
     * {CR,AR}_pc.
     *
     * That's not proof but a useful debugging aid.
     */

    idio_vm_NCE_pc = idio_all_code->i; /* PC == 0 */
    IDIO_IA_PUSH1 (IDIO_A_NON_CONT_ERR);

    idio_vm_FINISH_pc = idio_all_code->i; /* PC == 1 */
    IDIO_IA_PUSH1 (IDIO_A_FINISH);

    idio_vm_CR_pc = idio_all_code->i; /* PC == 2 */
    IDIO_IA_PUSH3 (IDIO_A_RESTORE_HANDLER, IDIO_A_RESTORE_STATE, IDIO_A_RETURN);

    /*
     * Just the RESTORE_STATE, RETURN for apply
     */
    idio_vm_AR_pc = idio_all_code->i; /* PC == 5 */
    IDIO_IA_PUSH2 (IDIO_A_RESTORE_STATE, IDIO_A_RETURN);

    idio_vm_IHR_pc = idio_all_code->i; /* PC == 7 */
    IDIO_IA_PUSH2 (IDIO_A_RESTORE_ALL_STATE, IDIO_A_RETURN);
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

void idio_init_codegen ()
{
}

void idio_codegen_add_primitives ()
{
}

void idio_final_codegen ()
{
}
