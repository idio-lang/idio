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
 * codegen.c
 *
 */

#include "idio.h"

static void idio_codegen_error_param_args (char *m, IDIO mt, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (mt);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_error_C (m, mt, loc);
}

static void idio_codegen_error_param_type (char *m, IDIO t, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (t);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_error_param_type (m, t, loc);
}

IDIO_IA_T idio_ia (size_t asize)
{
    if (asize <= 0) {
	asize = 100;
    }

    IDIO_IA_T ia = idio_alloc (sizeof (idio_ia_t));
    ia->ae = idio_alloc (asize * sizeof (IDIO_I));
    ia->asize = asize;
    ia->usize = 0;

    return ia;
}

void idio_ia_free (IDIO_IA_T ia)
{
    free (ia->ae);
    free (ia);
}

static void idio_ia_resize_by (IDIO_IA_T ia, size_t n)
{
    IDIO_IA_ASIZE (ia) += n;
    ia->ae = idio_realloc (ia->ae, IDIO_IA_ASIZE (ia));
}

void idio_ia_resize (IDIO_IA_T ia)
{
    /*
     * Default resize is 50% more.
     */
    idio_ia_resize_by (ia, IDIO_IA_ASIZE (ia) >> 1);
}

void idio_ia_append (IDIO_IA_T ia1, IDIO_IA_T ia2)
{
    if (NULL == ia2) {
	return;
    }

    if ((IDIO_IA_ASIZE (ia1) - IDIO_IA_USIZE (ia1)) < IDIO_IA_USIZE (ia2)) {
	idio_ia_resize_by (ia1, IDIO_IA_USIZE (ia2));
    }
    size_t i;
    for (i = 0; i < IDIO_IA_USIZE (ia2); i++) {
	IDIO_IA_AE (ia1, IDIO_IA_USIZE (ia1)++) = IDIO_IA_AE (ia2, i);
    }
}

void idio_ia_copy (IDIO_IA_T iad, IDIO_IA_T ias)
{
    if (IDIO_IA_ASIZE (iad) < IDIO_IA_USIZE (ias)) {
	idio_ia_resize_by (iad, IDIO_IA_USIZE (ias) - IDIO_IA_ASIZE (iad));
    }
    size_t i;
    for (i = 0; i < IDIO_IA_USIZE (ias); i++) {
	IDIO_IA_AE (iad, i) = IDIO_IA_AE (ias, i);
    }
    IDIO_IA_USIZE (iad) = IDIO_IA_USIZE (ias);
}

void idio_ia_push (IDIO_IA_T ia, IDIO_I ins)
{
    if (IDIO_IA_USIZE (ia) >= IDIO_IA_ASIZE (ia)) {
	idio_ia_resize (ia);
    }
    IDIO_IA_AE (ia, IDIO_IA_USIZE (ia)++) = ins;
}

/*
 * These macros assume {ia} is an accessible value
 */
#define IDIO_IA_PUSH1(i1)		(idio_ia_push (ia, i1))
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

#define IDIO_IA_VARUINT_1BYTE	240
#define IDIO_IA_VARUINT_2BYTES	2287
#define IDIO_IA_VARUINT_3BYTES	67823

IDIO_IA_T idio_ia_compute_varuint (uint64_t offset)
{
    IDIO_IA_T ia = idio_ia (10);

    if (offset <= IDIO_IA_VARUINT_1BYTE) {
	IDIO_IA_PUSH1 (offset);
    } else if (offset <= IDIO_IA_VARUINT_2BYTES) {
	IDIO_IA_PUSH1 (((offset - 240) / 256) + 241);
	IDIO_IA_PUSH1 ((offset - 240) % 256);
    } else if (offset <= IDIO_IA_VARUINT_3BYTES) {
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
	int i0 = IDIO_IA_USIZE (ia);
	int i;
	for (i = 0; i < n; i++) {
	    IDIO_IA_AE (ia, i0 + n -1 - i) = (offset & 0xff);
	    offset >>= 8;
	}
	IDIO_IA_USIZE (ia) += n;
    }

    return ia;
}

IDIO_IA_T idio_ia_compute_fixuint (int n, uint64_t offset)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    IDIO_IA_T ia = idio_ia (n);

    IDIO_IA_USIZE (ia) = n;
    for (n--; n >= 0; n--) {
	IDIO_IA_AE (ia, n) = offset & 0xff;
	offset >>= 8;
    }

    /*
     * Check there's nothing left!
     */
    IDIO_C_ASSERT (0 == offset);

    return ia;
}

IDIO_IA_T idio_ia_compute_8uint (uint64_t offset)
{
    return idio_ia_compute_fixuint (1, offset);
}

IDIO_IA_T idio_ia_compute_16uint (uint64_t offset)
{
    return idio_ia_compute_fixuint (2, offset);
}

IDIO_IA_T idio_ia_compute_32uint (uint64_t offset)
{
    return idio_ia_compute_fixuint (4, offset);
}

IDIO_IA_T idio_ia_compute_64uint (uint64_t offset)
{
    return idio_ia_compute_fixuint (8, offset);
}

/*
 * These macros assume {ia} is an accessible value
 */
#define IDIO_IA_PUSH_VARUINT(n)   { IDIO_IA_T ia2 = idio_ia_compute_varuint (n); idio_ia_append (ia, ia2); idio_ia_free (ia2); }
#define IDIO_IA_PUSH_8UINT(n)     { IDIO_IA_T ia2 = idio_ia_compute_8uint (n);   idio_ia_append (ia, ia2); idio_ia_free (ia2); }
#define IDIO_IA_PUSH_16UINT(n)    { IDIO_IA_T ia2 = idio_ia_compute_16uint (n);  idio_ia_append (ia, ia2); idio_ia_free (ia2); }
#define IDIO_IA_PUSH_32UINT(n)    { IDIO_IA_T ia2 = idio_ia_compute_32uint (n);  idio_ia_append (ia, ia2); idio_ia_free (ia2); }
#define IDIO_IA_PUSH_64UINT(n)    { IDIO_IA_T ia2 = idio_ia_compute_64uint (n);  idio_ia_append (ia, ia2); idio_ia_free (ia2); }

/*
 * Check this aligns with IDIO_VM_FETCH_REF
 *
 * At the time of writing the tests used around 2000 symbols in a
 * couple of modules
 */
#define IDIO_IA_PUSH_REF(n)	IDIO_IA_PUSH_16UINT (n)

idio_ai_t idio_codegen_extend_constants (IDIO cs, IDIO v)
{
    IDIO_ASSERT (cs);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (array, cs);

    idio_ai_t gci = idio_array_size (cs);
    idio_array_push (cs, v);
    return gci;
}

idio_ai_t idio_codegen_constants_lookup (IDIO cs, IDIO v)
{
    IDIO_ASSERT (cs);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (array, cs);

    idio_ai_t al = idio_array_size (cs);
    idio_ai_t i;
    for (i = 0 ; i < al; i++) {
	if (idio_equalp (v, idio_array_get_index (cs, i))) {
	    return i;
	}
    }

    return -1;
}

idio_ai_t idio_codegen_constants_lookup_or_extend (IDIO cs, IDIO v)
{
    IDIO_ASSERT (cs);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (array, cs);

    idio_ai_t gci = idio_codegen_constants_lookup (cs, v);

    if (-1 == gci) {
	gci = idio_codegen_extend_constants (cs, v);
    }

    return gci;
}

/*
 * Compiling
 *
 * Compiling the intermediate code (idio_I_*) is a reasonably
 * straightforward swap to IDIO_A_*.
 *
 * There's some specialisation for particularly common tuples to
 * reduce the size of the byte code and, hopefully, make the resultant
 * interpretation quicker as there's less fetching and decoding of
 * arguments to do.
 *
 * The flip side of that is more C code dedicated to compilation and
 * interpretation of the resultant byte code.
 */
void idio_codegen_compile (IDIO thr, IDIO_IA_T ia, IDIO cs, IDIO m, int depth)
{
    IDIO_ASSERT (cs);
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (pair, m);

    /* idio_debug ("compile: %s\n", m);  */

    if (! idio_isa_pair (m)) {
	idio_error_param_type ("pair", IDIO_LIST1 (m), IDIO_C_LOCATION ("idio_codegen_compile"));

	/* notreached */
	return;
    }

    IDIO mh = IDIO_PAIR_H (m);
    IDIO mt = IDIO_PAIR_T (m);

    if (! IDIO_TYPE_CONSTANT_I_CODEP (mh)) {
	/*
	 * A sequence of idio_I_* code segments will appear here as a
	 * list so we simply recurse for each one.
	 */
	if (idio_isa_pair (mh)) {
	    while (idio_S_nil != m) {
		idio_codegen_compile (thr, ia, cs, IDIO_PAIR_H (m), depth + 1);
		m = IDIO_PAIR_T (m);
		if (! idio_isa_list (m)) {
		    idio_error_C ("compile: not a sequence", m, IDIO_C_LOCATION ("idio_codegen_compile"));

		    /* notreached */
		    return;
		}
	    }
	    return;
	} else {
	    idio_debug ("\nWARNING: not a CONSTANT|pair: unexpected intermediate code: %s\n\n", mh);
	    return;
	}
    }

    switch (IDIO_CONSTANT_I_CODE_VAL (mh)) {
    case IDIO_I_CODE_SHALLOW_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("SHALLOW-ARGUMENT-REF j", mt, IDIO_C_LOCATION ("idio_codegen_compile/SHALLOW-ARGUMENT-REF"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_codegen_compile/SHALLOW-ARGUMENT-REF"));

		/* notreached */
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
    case IDIO_I_CODE_PREDEFINED:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("PREDEFINED vi", mt, IDIO_C_LOCATION ("idio_codegen_compile/PREDEFINED"));

		/* notreached */
		return;
	    }

	    IDIO vi = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (vi)) {
		idio_codegen_error_param_type ("fixnum", vi, IDIO_C_LOCATION ("idio_codegen_compile/PREDEFINED"));

		/* notreached */
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
	    } else if (idio_S_nil == vi) { /* pair */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED3);
	    } else if (idio_S_nil == vi) { /* head */
		IDIO_IA_PUSH1 (IDIO_A_PREDEFINED4);
	    } else if (idio_S_nil == vi) { /* tail */
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
    case IDIO_I_CODE_DEEP_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("DEEP-ARGUMENT-REF i j", mt, IDIO_C_LOCATION ("idio_codegen_compile/DEEP-ARGUMENT-REF"));

		/* notreached */
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_codegen_error_param_type ("fixnum", i, IDIO_C_LOCATION ("idio_codegen_compile/DEEP-ARGUMENT-REF"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_codegen_compile/DEEP-ARGUMENT-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_DEEP_ARGUMENT_REF);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_I_CODE_SHALLOW_ARGUMENT_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("SHALLOW-ARGUMENT-SET j m1", mt, IDIO_C_LOCATION ("idio_codegen_compile/SHALLOW-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_codegen_compile/SHALLOW-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

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
    case IDIO_I_CODE_DEEP_ARGUMENT_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("DEEP-ARGUMENT-SET i j m1", mt, IDIO_C_LOCATION ("idio_codegen_compile/DEEP-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_codegen_error_param_type ("fixnum", i, IDIO_C_LOCATION ("idio_codegen_compile/DEEP-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_LOCATION ("idio_codegen_compile/DEEP-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_DEEP_ARGUMENT_SET);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_I_CODE_GLOBAL_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("GLOBAL-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_CHECKED_GLOBAL_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("CHECKED-GLOBAL-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/CHECKED-GLOBAL-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/CHECKED-GLOBAL-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_CHECKED_GLOBAL_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_GLOBAL_FUNCTION_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("GLOBAL-FUNCTION-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-FUNCTION-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-FUNCTION-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_FUNCTION_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_CHECKED_GLOBAL_FUNCTION_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("CHECKED-GLOBAL-FUNCTION-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/CHECKED-GLOBAL-FUNCTION-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/CHECKED-GLOBAL-FUNCTION-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_CHECKED_GLOBAL_FUNCTION_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_COMPUTED_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("COMPUTED-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/COMPUTED-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/COMPUTED-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_GLOBAL_DEF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("GLOBAL-DEF name kind mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-DEF"));

		/* notreached */
		return;
	    }

	    IDIO name = IDIO_PAIR_H (mt);
	    IDIO kind = IDIO_PAIR_HT (mt);
	    IDIO fmci = IDIO_PAIR_HTT (mt);

	    if (! idio_isa_symbol (name)) {
		idio_codegen_error_param_type ("symbol", name, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-DEF"));

		/* notreached */
		return;
	    }

	    if (! (idio_S_predef == kind ||
		   idio_S_toplevel == kind ||
		   idio_S_dynamic == kind ||
		   idio_S_environ == kind ||
		   idio_S_computed == kind)) {
		idio_codegen_error_param_type ("symbol predef|toplevel|dynamic|environ|computed", kind, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-DEF"));

		/* notreached */
		return;
	    }

	    if (! idio_isa_fixnum (fmci)) {
		idio_codegen_error_param_type ("fixnum", fmci, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-DEF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_DEF);
	    idio_ai_t mci = IDIO_FIXNUM_VAL (fmci);
	    IDIO_IA_PUSH_REF (mci);
	    mci = idio_codegen_constants_lookup_or_extend (cs, kind);
	    IDIO_IA_PUSH_VARUINT (mci);
	}
	break;
    case IDIO_I_CODE_GLOBAL_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("GLOBAL-SET mci m1", mt, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-SET"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/GLOBAL-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_GLOBAL_SET);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_COMPUTED_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("COMPUTED-SET mci m1", mt, IDIO_C_LOCATION ("idio_codegen_compile/COMPUTED-SET"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/COMPUTED-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_SET);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_COMPUTED_DEFINE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("COMPUTED-DEFINE mci m1", mt, IDIO_C_LOCATION ("idio_codegen_compile/COMPUTED-DEFINE"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/COMPUTED-DEFINE"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_DEFINE);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_CONSTANT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("CONSTANT c", mt, IDIO_C_LOCATION ("idio_codegen_compile/CONSTANT"));

		/* notreached */
		return;
	    }

	    IDIO c = IDIO_PAIR_H (mt);

	    /*
	     * A constant can be any quoted value or any non-symbol
	     * atom.
	     */

	    switch ((intptr_t) c & IDIO_TYPE_MASK) {
	    case IDIO_TYPE_FIXNUM_MARK:
		{
		    intptr_t v = IDIO_FIXNUM_VAL (c);
		    switch (v) {
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
			    if (v >= 0) {
				IDIO_IA_PUSH1 (IDIO_A_FIXNUM);
				IDIO_IA_PUSH_VARUINT (v);
			    } else {
				IDIO_IA_PUSH1 (IDIO_A_NEG_FIXNUM);
				IDIO_IA_PUSH_VARUINT (-v);
			    }
			    return;
			}
			break;
		    }
		}
		break;
	    case IDIO_TYPE_CONSTANT_MARK:
		{
		    switch ((intptr_t) c & IDIO_TYPE_CONSTANT_MASK) {
		    case IDIO_TYPE_CONSTANT_IDIO_MARK:
			{
			    if (idio_S_true == c) {
				IDIO_IA_PUSH1 (IDIO_A_PREDEFINED0);
				return;
			    } else if (idio_S_false == c) {
				IDIO_IA_PUSH1 (IDIO_A_PREDEFINED1);
				return;
			    } else if (idio_S_nil == c) {
				IDIO_IA_PUSH1 (IDIO_A_PREDEFINED2);
				return;
			    } else {
				intptr_t cv = IDIO_CONSTANT_IDIO_VAL (c);

				if (cv >= 0) {
				    IDIO_IA_PUSH1 (IDIO_A_CONSTANT);
				    IDIO_IA_PUSH_VARUINT (cv);
				} else {
				    IDIO_IA_PUSH1 (IDIO_A_NEG_CONSTANT);
				    IDIO_IA_PUSH_VARUINT (-cv);
				}
				return;
			    }
			    break;
			}
		    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
			{
			    intptr_t v = IDIO_CHARACTER_VAL (c);

			    if (v >= 0) {
				IDIO_IA_PUSH1 (IDIO_A_CHARACTER);
				IDIO_IA_PUSH_VARUINT (v);
			    } else {
				/*
				 * Can we even have negative characters?  Some
				 * casual cut'n'paste says yes!
				 */
				IDIO_IA_PUSH1 (IDIO_A_NEG_CHARACTER);
				IDIO_IA_PUSH_VARUINT (-v);
			    }
			    return;
			}
			break;
		    default:
			idio_error_C ("unexpected constant/CONSTANT/??", c, IDIO_C_LOCATION ("idio_codegen_compile/CONSTANT/CONSTANT"));
			break;
		    }
		}
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		idio_error_C ("unexpected constant/PLACEHOLDER", c, IDIO_C_LOCATION ("idio_codegen_compile/CONSTANT"));
		break;
	    default:
		{
		    /*
		     * A quoted value...probably.
		     */

		    IDIO_FLAGS (c) |= IDIO_FLAG_CONST;

		    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, c);
		    IDIO fmci = idio_fixnum (mci);
		    idio_module_set_vci (idio_thread_current_env (), fmci, fmci);

		    IDIO_IA_PUSH1 (IDIO_A_CONSTANT_REF);
		    IDIO_IA_PUSH_VARUINT (mci);
		    return;
		}
	    }

	}
	break;
    case IDIO_I_CODE_ALTERNATIVE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("ALTERNATIVE m1 m2 m3", mt, IDIO_C_LOCATION ("idio_codegen_compile/ALTERNATIVE"));

		/* notreached */
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
	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

	    IDIO_IA_T ia2 = idio_ia (100);
	    idio_codegen_compile (thr, ia2, cs, m2, depth + 1);

	    IDIO_IA_T ia3 = idio_ia (100);
	    idio_codegen_compile (thr, ia3, cs, m3, depth + 1);

	    IDIO_IA_T g7 = NULL;
	    size_t g7_len = 0;

	    /*
	     * include the size of the jump instruction!
	     *
	     * g7_len == size(ins) + size(off)
	     */
	    if (IDIO_IA_USIZE (ia3) <= IDIO_IA_VARUINT_1BYTE) {
		g7_len = 1 + 1;
	    } else {
		g7 = idio_ia_compute_varuint (IDIO_IA_USIZE (ia3));
		g7_len = 1 + IDIO_IA_USIZE (g7);
	    }

	    size_t jf6_off = IDIO_IA_USIZE (ia2) + g7_len;

	    /* 3: */
	    if (jf6_off <= IDIO_IA_VARUINT_1BYTE) {
		IDIO_IA_PUSH1 (IDIO_A_SHORT_JUMP_FALSE);
		IDIO_IA_PUSH1 (jf6_off);
	    } else {
		IDIO_IA_T jf6 = idio_ia_compute_varuint (jf6_off);
		IDIO_IA_PUSH1 (IDIO_A_LONG_JUMP_FALSE);
		idio_ia_append (ia, jf6);
		idio_ia_free (jf6);
	    }

	    /* 4: */
	    idio_ia_append (ia, ia2);

	    /* 5: */
	    if (IDIO_IA_USIZE (ia3) <= IDIO_IA_VARUINT_1BYTE) {
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (IDIO_IA_USIZE (ia3));
	    } else {
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_ia_append (ia, g7);
		idio_ia_free (g7);
	    }

	    /* 6: */
	    idio_ia_append (ia, ia3);

	    idio_ia_free (ia2);
	    idio_ia_free (ia3);
	}
	break;
    case IDIO_I_CODE_SEQUENCE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("SEQUENCE m1 m+", mt, IDIO_C_LOCATION ("idio_codegen_compile/SEQUENCE"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    idio_codegen_compile (thr, ia, cs, mp, depth + 1);
	}
	break;
    case IDIO_I_CODE_AND:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 1) {
		idio_codegen_error_param_args ("AND m+", mt, IDIO_C_LOCATION ("idio_codegen_compile/AND"));

		/* notreached */
		return;
	    }

	    /*
	     * We don't know how far to jump to the end of the AND
	     * sequence until we have generated the code for the
	     * remaining clauses *and* the inter-clause jumps
	     *
	     * 1: compile the first clause and append it to ia
	     * (nothing special here)
	     *
	     * 2: We can compile the meaning of each remaining clause
	     * into iac[].
	     *
	     * 3: create a couple of holding buffers: {iat} for the
	     * per-clause calculations, and {iar} for the accumulated
	     * result of all the per-clause shuffling
	     *
	     * 3: Working *backwards* from n to 1, on behalf of clause
	     * m-1 we can:
	     *
	     *   a: calculate the "jump to the end" which is the
	     *   accumulated "jump to the end" plus the size of iac[m]
	     *
	     *   b: generate the JUMP-FALSE "jump to the end" in a
	     *   temp buffer, iat
	     *
	     *   c: append iac[m] to iat
	     *
	     *   d: append iar to iat
	     *
	     *   e: copy iat back over iar
	     *
	     *   f: set the accumulated "jump to the end" to be the
	     *   size of iar
	     *
	     * 4: append iar to ia
	     */
	    IDIO m1 = IDIO_PAIR_H (mt);
	    mt = IDIO_PAIR_T (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

	    size_t n = idio_list_length (mt);

	    IDIO_IA_T iac[n];
	    intptr_t i;

	    /*
	     * 1st pass, l is only the accumulated code size for
	     * allocating iar/iat
	     */
	    size_t l = 0;
	    for (i = 0; i < n ; i++) {
		iac[i] = idio_ia (100);
		idio_codegen_compile (thr, iac[i], cs, IDIO_PAIR_H (mt), depth + 1);
		l += IDIO_IA_USIZE (iac[i]);
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
	    IDIO_IA_T iar = idio_ia (l + n * (1 + 9));
	    IDIO_IA_T iat = idio_ia (l + n * (1 + 9));

	    /*
	     * 2nd pass, l now includes jump sizes
	     */
	    l = 0;
	    for (i = n - 1; i >= 0 ; i--) {
		l += IDIO_IA_USIZE (iac[i]);
		if (l <= IDIO_IA_VARUINT_1BYTE) {
		    idio_ia_push (iat, IDIO_A_SHORT_JUMP_FALSE);
		    idio_ia_push (iat, l);
		} else {
		    IDIO_IA_T jf = idio_ia_compute_varuint (l);
		    idio_ia_push (iat, IDIO_A_LONG_JUMP_FALSE);
		    idio_ia_append (iat, jf);
		    idio_ia_free (jf);
		}

		idio_ia_append (iat, iac[i]);
		idio_ia_free (iac[i]);

		idio_ia_append (iat, iar);
		idio_ia_copy (iar, iat);
		IDIO_IA_USIZE (iat) = 0;
		l = IDIO_IA_USIZE (iar);
	    }


	    idio_ia_append (ia, iar);
	    idio_ia_free (iar);
	    idio_ia_free (iat);
	}
	break;
    case IDIO_I_CODE_OR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 1) {
		idio_codegen_error_param_args ("OR m+", mt, IDIO_C_LOCATION ("idio_codegen_compile/OR"));

		/* notreached */
		return;
	    }

	    /*
	     * We don't know how far to jump to the end of the OR
	     * sequence until we have generated the code for the
	     * remaining clauses *and* the inter-clause jumps
	     *
	     * See commentary for IDIO_I_CODE_AND, above
	     */
	    IDIO m1 = IDIO_PAIR_H (mt);
	    mt = IDIO_PAIR_T (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);

	    size_t n = idio_list_length (mt);

	    IDIO_IA_T iac[n];
	    intptr_t i;

	    /*
	     * 1st pass, l is only the accumulated code size for
	     * allocating iar/iat
	     */
	    size_t l = 0;
	    for (i = 0; i < n ; i++) {
		iac[i] = idio_ia (100);
		idio_codegen_compile (thr, iac[i], cs, IDIO_PAIR_H (mt), depth + 1);
		l += IDIO_IA_USIZE (iac[i]);
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
	    IDIO_IA_T iar = idio_ia (l + n * (1 + 9));
	    IDIO_IA_T iat = idio_ia (l + n * (1 + 9));

	    /*
	     * 2nd pass, l now includes jump sizes
	     */
	    l = 0;
	    for (i = n - 1; i >= 0 ; i--) {
		l += IDIO_IA_USIZE (iac[i]);
		if (l <= IDIO_IA_VARUINT_1BYTE) {
		    idio_ia_push (iat, IDIO_A_SHORT_JUMP_TRUE);
		    idio_ia_push (iat, l);
		} else {
		    IDIO_IA_T jf = idio_ia_compute_varuint (l);
		    idio_ia_push (iat, IDIO_A_LONG_JUMP_TRUE);
		    idio_ia_append (iat, jf);
		    idio_ia_free (jf);
		}

		idio_ia_append (iat, iac[i]);
		idio_ia_free (iac[i]);

		idio_ia_append (iat, iar);
		idio_ia_copy (iar, iat);
		IDIO_IA_USIZE (iat) = 0;
		l = IDIO_IA_USIZE (iar);
	    }

	    idio_ia_append (ia, iar);
	    idio_ia_free (iar);
	    idio_ia_free (iat);
	}
	break;
    case IDIO_I_CODE_BEGIN:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 1) {
		idio_codegen_error_param_args ("BEGIN m+", mt, IDIO_C_LOCATION ("idio_codegen_compile/BEGIN"));

		/* notreached */
		return;
	    }

	    while (idio_S_nil != mt) {
		idio_codegen_compile (thr, ia, cs, IDIO_PAIR_H (mt), depth + 1);
		mt = IDIO_PAIR_T (mt);
	    }
	}
	break;
    case IDIO_I_CODE_TR_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("TR-FIX-LET m* m+", mt, IDIO_C_LOCATION ("idio_codegen_compile/TR-FIX-LET"));

		/* notreached */
		return;
	    }

	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_FRAME);
	    idio_codegen_compile (thr, ia, cs, mp, depth + 1);
	}
	break;
    case IDIO_I_CODE_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("FIX-LET m* m+", mt, IDIO_C_LOCATION ("idio_codegen_compile/FIX-LET"));

		/* notreached */
		return;
	    }

	    IDIO ms = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXTEND_FRAME);
	    idio_codegen_compile (thr, ia, cs, mp, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_UNLINK_FRAME);
	}
	break;
    case IDIO_I_CODE_PRIMCALL0:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("CALL0 ins", mt, IDIO_C_LOCATION ("idio_codegen_compile/CALL0"));

		/* notreached */
		return;
	    }

	    IDIO ins = IDIO_PAIR_H (mt);

	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_I_CODE_PRIMCALL1:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("CALL1 ins m1", mt, IDIO_C_LOCATION ("idio_codegen_compile/CALL1"));

		/* notreached */
		return;
	    }

	    IDIO ins = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_I_CODE_PRIMCALL2:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("CALL2 ins m1 m2", mt, IDIO_C_LOCATION ("idio_codegen_compile/CALL2"));

		/* notreached */
		return;
	    }

	    IDIO ins = IDIO_PAIR_H (mt);

	    IDIO m1 = IDIO_PAIR_HT (mt);
	    IDIO m2 = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, cs, m2, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POP_REG1);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_I_CODE_PRIMCALL3:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 4) {
		idio_codegen_error_param_args ("CALL3 ins m1 m2 m3", mt, IDIO_C_LOCATION ("idio_codegen_compile/CALL3"));

		/* notreached */
		return;
	    }

	    IDIO ins = IDIO_PAIR_H (mt);

	    IDIO m1 = IDIO_PAIR_HT (mt);
	    IDIO m2 = IDIO_PAIR_HTT (mt);
	    IDIO m3 = IDIO_PAIR_HTTT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, cs, m2, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, cs, m3, depth + 1);
	    IDIO_IA_PUSH2 (IDIO_A_POP_REG2, IDIO_A_POP_REG1);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (ins));
	}
	break;
    case IDIO_I_CODE_FIX_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 4) {
		idio_codegen_error_param_args ("FIX-CLOSURE m+ arity sigstr docstr", mt, IDIO_C_LOCATION ("idio_codegen_compile/FIX-CLOSURE"));

		/* notreached */
		return;
	    }

	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_HT (mt);
	    IDIO sigstr = IDIO_PAIR_HTT (mt);
	    IDIO docstr = IDIO_PAIR_HTTT (mt);

	    if (! idio_isa_fixnum (arity)) {
		idio_codegen_error_param_type ("fixnum", arity, IDIO_C_LOCATION ("idio_codegen_compile/FIX-CLOSURE"));

		/* notreached */
		return;
	    }

	    idio_ai_t ssci = idio_codegen_constants_lookup_or_extend (cs, sigstr);
	    idio_ai_t dsci = idio_codegen_constants_lookup_or_extend (cs, docstr);

	    /*
	     * Remember this is the act of creating a closure *value*
	     * not running the (body of the) closure.

	     * {the-function}, the body of the function will be
	     * prefaced with an arity check then whatever m+ consists
	     * of.

	     * Obviously {the-function} is some arbitrary length and
	     * we don't want to run it now, we are only creating the
	     * closure value and that creation is essentially a call
	     * to idio_closure() with a "pointer" to the start of
	     * {the-function}.  Not really a pointer, of course, but
	     * rather where {the-function} starts relative to where we
	     * are now.

	     * Thinking ahead, having created the closure value we now
	     * want to jump over {the-function} -- we don't want to
	     * run it -- which will be a SHORT- or a LONG- GOTO.  A
	     * SHORT-GOTO is fixed at two bytes but a LONG-GOTO could
	     * be 9 bytes which means that the "pointer" to
	     * {the-function} in the CREATE-CLOSURE instruction will
	     * vary depending on how many bytes are required to
	     * instruct us to jump over {the-function}.

	     * Think about the code to be generated where we can only
	     * calculate the length-of #3 when we have added the code
	     * for goto #5 and the code for goto #5 depends on the
	     * code for the-function (which depends on m+).

	      1: ...
	      2: create-closure (length-of #3) [features]
	      3: goto #5
	      4: the-function (from m+)
	      5: ...

	     * [features] are things we always add and therefore
	     * always read off when implementing the CREATE-CLOSURE
	     * instruction.

	     * Here there are obvious features like the constants
	     * indexes for signature-string and documentation-string.

	     * A less obvious feature is the length of {the-function}.
	     * Actually it will get emitted twice, once as a feature
	     * and then as the GOTO.  We want it as a feature in order
	     * that we can add it to the closure value.  That means we
	     * can disassemble just this function.  If we don't pass
	     * it as a feature then, when we are about to diassemble
	     * the closure, it is impossible for us to reverse
	     * engineer the GOTO immediately before {the-function} as
	     * some LONG-GOTO can look like a combination of
	     * CREATE-CLOSURE len [features].

	     * XXX if we want the closure *value* to know about its
	     * own arity (and varargs) -- don't forget the closure
	     * value doesn't know about its arity/varargs,
	     * {the-function} does but not the value -- then we would
	     * need to pass them to CREATE_CLOSURE as more [features].

	     * Why would we do that?  The only current reason is to
	     * test if a closure is a thunk, ie. arity 0 and no
	     * varargs.  Currently there's no pressing need for a
	     * thunk? predicate.
	     */

	    /* the-function */
	    IDIO_IA_T iap = idio_ia (100);
	    switch (IDIO_FIXNUM_VAL (arity) + 1) {
	    case 1: idio_ia_push (iap, IDIO_A_ARITY1P); break;
	    case 2: idio_ia_push (iap, IDIO_A_ARITY2P); break;
	    case 3: idio_ia_push (iap, IDIO_A_ARITY3P); break;
	    case 4: idio_ia_push (iap, IDIO_A_ARITY4P); break;
	    default:
		{
		    idio_ia_push (iap, IDIO_A_ARITYEQP);
		    IDIO_IA_T a = idio_ia_compute_varuint (IDIO_FIXNUM_VAL (arity) + 1);
		    idio_ia_append (iap, a);
		    idio_ia_free (a);
		}
		break;
	    }

	    idio_ia_push (iap, IDIO_A_EXTEND_FRAME);
	    idio_codegen_compile (thr, iap, cs, mp, depth + 1);
	    idio_ia_push (iap, IDIO_A_RETURN);

	    idio_ai_t code_len = IDIO_IA_USIZE (iap);
	    if (code_len <= IDIO_IA_VARUINT_1BYTE) {
		/* 2: */
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, 2);
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (ssci);
		IDIO_IA_PUSH_VARUINT (dsci);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (code_len);
	    } else {
		/* 2: */
		IDIO_IA_T g5 = idio_ia_compute_varuint (code_len);
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, (1 + IDIO_IA_USIZE (g5)));
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (ssci);
		IDIO_IA_PUSH_VARUINT (dsci);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_ia_append (ia, g5);
		idio_ia_free (g5);
	    }

	    /* 4: */
	    idio_ia_append (ia, iap);

	    idio_ia_free (iap);
	}
	break;
    case IDIO_I_CODE_NARY_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 4) {
		idio_codegen_error_param_args ("NARY-CLOSURE m+ arity sigstr docstr", mt, IDIO_C_LOCATION ("idio_codegen_compile/NARY-CLOSURE"));

		/* notreached */
		return;
	    }

	    IDIO mp = IDIO_PAIR_H (mt);
	    IDIO arity = IDIO_PAIR_HT (mt);
	    IDIO sigstr = IDIO_PAIR_HTT (mt);
	    IDIO docstr = IDIO_PAIR_HTTT (mt);

	    if (! idio_isa_fixnum (arity)) {
		idio_codegen_error_param_type ("fixnum", arity, IDIO_C_LOCATION ("idio_codegen_compile/NARY-CLOSURE"));

		/* notreached */
		return;
	    }

	    idio_ai_t ssci = idio_codegen_constants_lookup_or_extend (cs, sigstr);
	    idio_ai_t dsci = idio_codegen_constants_lookup_or_extend (cs, docstr);

	    /*
	     * See IDIO_I_CODE_FIX_CLOSURE for commentary.
	     */

	    /* the-function */
	    IDIO_IA_T iap = idio_ia (100);
	    idio_ia_push (iap, IDIO_A_ARITYGEP);
	    IDIO_IA_T a = idio_ia_compute_varuint (IDIO_FIXNUM_VAL (arity) + 1);
	    idio_ia_append (iap, a);
	    idio_ia_free (a);

	    idio_ia_push (iap, IDIO_A_PACK_FRAME);
	    a = idio_ia_compute_varuint (IDIO_FIXNUM_VAL (arity));
	    idio_ia_append (iap, a);
	    idio_ia_free (a);

	    idio_ia_push (iap, IDIO_A_EXTEND_FRAME);
	    idio_codegen_compile (thr, iap, cs, mp, depth + 1);
	    idio_ia_push (iap, IDIO_A_RETURN);

	    idio_ai_t code_len = IDIO_IA_USIZE (iap);
	    if (code_len <= IDIO_IA_VARUINT_1BYTE) {
		/* 2: */
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, 2);
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (ssci);
		IDIO_IA_PUSH_VARUINT (dsci);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (code_len);
	    } else {
		/* 2: */
		IDIO_IA_T g5 = idio_ia_compute_varuint (code_len);
		IDIO_IA_PUSH2 (IDIO_A_CREATE_CLOSURE, (1 + IDIO_IA_USIZE (g5)));
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (ssci);
		IDIO_IA_PUSH_VARUINT (dsci);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_ia_append (ia, g5);
		idio_ia_free (g5);
	    }

	    /* 4: */
	    idio_ia_append (ia, iap);

	    idio_ia_free (iap);
	}
	break;
    case IDIO_I_CODE_TR_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("TR-REGULAR-CALL m1 m*", mt, IDIO_C_LOCATION ("idio_codegen_compile/TR-REGULAR-CALL"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, cs, ms, depth + 1);
	    IDIO_IA_PUSH2 (IDIO_A_POP_FUNCTION, IDIO_A_FUNCTION_GOTO);
	}
	break;
    case IDIO_I_CODE_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("REGULAR-CALL m1 m*", mt, IDIO_C_LOCATION ("idio_codegen_compile/REGULAR-CALL"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, cs, ms, depth + 1);
	    IDIO_IA_PUSH4 (IDIO_A_POP_FUNCTION, IDIO_A_PRESERVE_STATE, IDIO_A_FUNCTION_INVOKE, IDIO_A_RESTORE_STATE);
	}
	break;
    case IDIO_I_CODE_STORE_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("STORE-ARGUMENT m1 m* rank", mt, IDIO_C_LOCATION ("idio_codegen_compile/STORE-ARGUMENT"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_HT (mt);
	    IDIO rank = IDIO_PAIR_HTT (mt);

	    if (! idio_isa_fixnum (rank)) {
		idio_codegen_error_param_type ("fixnum", rank, IDIO_C_LOCATION ("idio_codegen_compile/STORE-ARGUMENT"));

		/* notreached */
		return;
	    }

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, cs, ms, depth + 1);
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
    case IDIO_I_CODE_CONS_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("CONS-ARGUMENT m1 m* arity", mt, IDIO_C_LOCATION ("idio_codegen_compile/CONS-ARGUMENT"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_HT (mt);
	    IDIO arity = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, cs, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, cs, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POP_CONS_FRAME);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (arity));
	}
	break;
    case IDIO_I_CODE_ALLOCATE_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("ALLOCATE-FRAME size", mt, IDIO_C_LOCATION ("idio_codegen_compile/ALLOCATE-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_codegen_error_param_type ("fixnum", size, IDIO_C_LOCATION ("idio_codegen_compile/ALLOCATE-FRAME"));

		/* notreached */
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
    case IDIO_I_CODE_ALLOCATE_DOTTED_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("ALLOCATE-DOTTED-FRAME size", mt, IDIO_C_LOCATION ("idio_codegen_compile/ALLOCATE-DOTTED-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO size = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (size)) {
		idio_codegen_error_param_type ("fixnum", size, IDIO_C_LOCATION ("idio_codegen_compile/ALLOCATE-DOTTED-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_DOTTED_FRAME);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (size) + 1);
	}
	break;
    case IDIO_I_CODE_FINISH:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 0) {
		idio_codegen_error_param_args ("FINISH", mt, IDIO_C_LOCATION ("idio_codegen_compile/FINISH"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_FINISH);
	}
	break;
    case IDIO_I_CODE_PUSH_DYNAMIC:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("PUSH-DYNAMIC mci m", mt, IDIO_C_LOCATION ("idio_codegen_compile/PUSH-DYNAMIC"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/PUSH-DYNAMIC"));

		/* notreached */
		return;
	    }

	    IDIO m = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_DYNAMIC);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_POP_DYNAMIC:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("POP-DYNAMIC", mt, IDIO_C_LOCATION ("idio_codegen_compile/POP-DYNAMIC"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_DYNAMIC);
	}
	break;
    case IDIO_I_CODE_DYNAMIC_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("DYNAMIC-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/DYNAMIC-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/DYNAMIC-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_DYNAMIC_FUNCTION_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("DYNAMIC-FUNCTION-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/DYNAMIC-FUNCTION-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/DYNAMIC-FUNCTION-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_FUNCTION_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_PUSH_ENVIRON:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("PUSH-ENVIRON mci m", mt, IDIO_C_LOCATION ("idio_codegen_compile/PUSH-ENVIRON"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/PUSH-ENVIRON"));

		/* notreached */
		return;
	    }

	    IDIO m = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_ENVIRON);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_POP_ENVIRON:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("POP-ENVIRON", mt, IDIO_C_LOCATION ("idio_codegen_compile/POP-ENVIRON"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_ENVIRON);
	}
	break;
    case IDIO_I_CODE_ENVIRON_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("ENVIRON-REF mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/ENVIRON-REF"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/ENVIRON-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_ENVIRON_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_PUSH_TRAP:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("PUSH-TRAP mci", mt, IDIO_C_LOCATION ("idio_codegen_compile/PUSH-TRAP"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/PUSH-TRAP"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_TRAP);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_POP_TRAP:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("POP-TRAP", mt, IDIO_C_LOCATION ("idio_codegen_compile/POP-TRAP"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_TRAP);
	}
	break;
    case IDIO_I_CODE_EXPANDER:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("EXPANDER mci m", mt, IDIO_C_LOCATION ("idio_codegen_compile/EXPANDER"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/EXPANDER"));

		/* notreached */
		return;
	    }

	    IDIO m = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, cs, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXPANDER);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	}
	break;
    case IDIO_I_CODE_INFIX_OPERATOR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("OPERATOR mci pri m", mt, IDIO_C_LOCATION ("idio_codegen_compile/INFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/INFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO pri = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (pri)) {
		idio_codegen_error_param_type ("fixnum", pri, IDIO_C_LOCATION ("idio_codegen_compile/INFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO m = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, cs, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_INFIX_OPERATOR);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (pri));
	}
	break;
    case IDIO_I_CODE_POSTFIX_OPERATOR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("OPERATOR mci pri m", mt, IDIO_C_LOCATION ("idio_codegen_compile/POSTFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO mci = IDIO_PAIR_H (mt);
	    if (! idio_isa_fixnum (mci)) {
		idio_codegen_error_param_type ("fixnum", mci, IDIO_C_LOCATION ("idio_codegen_compile/POSTFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO pri = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (pri)) {
		idio_codegen_error_param_type ("fixnum", pri, IDIO_C_LOCATION ("idio_codegen_compile/POSTFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO m = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, cs, m, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POSTFIX_OPERATOR);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (mci));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (pri));
	}
	break;
    case IDIO_I_CODE_NOP:
	break;
    default:
	idio_error_C ("bad instruction", mh, IDIO_C_LOCATION ("idio_codegen_compile"));
	break;
    }
}

void idio_codegen_code_prologue (IDIO_IA_T ia)
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
     * TRAP_SP, FRAME and ENV; and all of those should be preceded by
     * the real PC of the calling function: ie. <number:PC>
     * <number:ESP> <number:DSP> <number:HSP> <#FRAME> <#module>
     * {CR,AR}_pc.
     *
     * That's not proof but a useful debugging aid.
     */

    idio_vm_NCE_pc = IDIO_IA_USIZE (idio_all_code); /* PC == 0 */
    IDIO_IA_PUSH1 (IDIO_A_NON_CONT_ERR);

    idio_vm_FINISH_pc = IDIO_IA_USIZE (idio_all_code); /* PC == 1 */
    IDIO_IA_PUSH1 (IDIO_A_FINISH);

    idio_vm_CR_pc = IDIO_IA_USIZE (idio_all_code); /* PC == 2 */
    IDIO_IA_PUSH3 (IDIO_A_RESTORE_TRAP, IDIO_A_RESTORE_STATE, IDIO_A_RETURN);

    /*
     * Just the RESTORE_STATE, RETURN for apply
     */
    idio_vm_AR_pc = IDIO_IA_USIZE (idio_all_code); /* PC == 5 */
    IDIO_IA_PUSH2 (IDIO_A_RESTORE_STATE, IDIO_A_RETURN);

    idio_vm_IHR_pc = IDIO_IA_USIZE (idio_all_code); /* PC == 7 */
    IDIO_IA_PUSH2 (IDIO_A_RESTORE_ALL_STATE, IDIO_A_RETURN);
}

void idio_codegen (IDIO thr, IDIO m, IDIO cs)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (pair, m);

    IDIO_THREAD_PC (thr) = IDIO_IA_USIZE (idio_all_code);

    IDIO_IA_T ia = idio_ia (100);

    idio_codegen_compile (thr, ia, cs, m, 0);

    idio_ia_append (idio_all_code, ia);
    idio_ia_free (ia);

    /* idio_vm_add_module_constants (idio_Idio_module_instance (), cs); */
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
