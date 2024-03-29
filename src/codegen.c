/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "codegen.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "hash.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "read.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm-asm.h"
#include "vm.h"
#include "vtable.h"

static IDIO idio_codegen_module = idio_S_nil;
IDIO_C_STRUCT_IDENT_DECL (idio_ia_s);

static void idio_codegen_error_param_args (char const *m, IDIO mt, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (mt);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_coding_error_C (m, mt, c_location);
}

static void idio_codegen_error_param_type (char const *m, IDIO t, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (t);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_param_type (m, t, c_location);
}

IDIO_IA_T idio_ia (size_t const asize)
{
    size_t sz = asize;
    if (sz <= 0) {
	sz = 100;
    }

    IDIO_IA_T ia = idio_alloc (sizeof (idio_ia_t));
    ia->ae = idio_alloc (sz * sizeof (IDIO_I));
    ia->refcnt = 1;
    ia->asize = sz;
    ia->usize = 0;

    return ia;
}

void idio_ia_free (IDIO_IA_T ia)
{
    if (ia) {
	IDIO_IA_REFCNT (ia)--;

	if (0 == IDIO_IA_REFCNT (ia)) {
	    idio_free (ia->ae);
	    idio_free (ia);
	}
    } else {
	fprintf (stderr, "idio_ia_free: already freed ia %10p?\n", ia);
    }
}

static void idio_ia_resize_by (IDIO_IA_T ia, size_t const n)
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

    IDIO_C_ASSERT (IDIO_IA_USIZE (ia1) <= IDIO_IA_ASIZE (ia1));
    IDIO_C_ASSERT (IDIO_IA_USIZE (ia2) <= IDIO_IA_ASIZE (ia2));

    if ((IDIO_IA_ASIZE (ia1) - IDIO_IA_USIZE (ia1)) < IDIO_IA_USIZE (ia2)) {
	idio_ia_resize_by (ia1, IDIO_IA_USIZE (ia2));
    }
    idio_pc_t i;
    for (i = 0; i < IDIO_IA_USIZE (ia2); i++) {
	IDIO_IA_AE (ia1, IDIO_IA_USIZE (ia1)++) = IDIO_IA_AE (ia2, i);
    }
}

void idio_ia_append_free (IDIO_IA_T ia1, IDIO_IA_T ia2)
{
    idio_ia_append (ia1, ia2);
    idio_ia_free (ia2);
}

void idio_ia_copy (IDIO_IA_T iad, IDIO_IA_T ias)
{
    if (IDIO_IA_ASIZE (iad) < IDIO_IA_USIZE (ias)) {
	idio_ia_resize_by (iad, IDIO_IA_USIZE (ias) - IDIO_IA_ASIZE (iad));
    }
    idio_pc_t i;
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

IDIO_IA_T idio_ia_compute_fixuint (const int n, const uint64_t offset)
{
    IDIO_C_ASSERT (n < 9 && n > 0);

    IDIO_IA_T ia = idio_ia (n);

    IDIO_IA_USIZE (ia) = n;
    int i = n;
    uint64_t off = offset;
    for (i--; i >= 0; i--) {
	IDIO_IA_AE (ia, i) = off & 0xff;
	off >>= 8;
    }

    /*
     * Check there's nothing left!
     */
    IDIO_C_ASSERT (0 == off);

    return ia;
}

IDIO_IA_T idio_ia_compute_8uint (uint64_t offset)
{
    IDIO_C_ASSERT (offset <= 0xff);
    return idio_ia_compute_fixuint (1, offset);
}

IDIO_IA_T idio_ia_compute_16uint (uint64_t offset)
{
    IDIO_C_ASSERT (offset <= 0xffff);
    return idio_ia_compute_fixuint (2, offset);
}

IDIO_IA_T idio_ia_compute_32uint (uint64_t offset)
{
    IDIO_C_ASSERT (offset <= 0xffffffff);
    return idio_ia_compute_fixuint (4, offset);
}

IDIO_IA_T idio_ia_compute_64uint (uint64_t offset)
{
    return idio_ia_compute_fixuint (8, offset);
}

#define IDIO_IA_PUSH_VARUINT_BC(bc, n)				\
    {								\
	IDIO_IA_T ia_pv = idio_ia_compute_varuint (n);	\
	idio_ia_append_free ((bc), ia_pv);			\
    }

/*
 * These macros assume {ia} is an accessible value
 */
#define IDIO_IA_PUSH_VARUINT(n)   { IDIO_IA_PUSH_VARUINT_BC (ia, n); }
#define IDIO_IA_PUSH_8UINT(n)     { IDIO_IA_T ia_p8 = idio_ia_compute_8uint (n);   idio_ia_append_free (ia, ia_p8); }
#define IDIO_IA_PUSH_16UINT(n)    { IDIO_IA_T ia_p16 = idio_ia_compute_16uint (n);  idio_ia_append_free (ia, ia_p16); }
#define IDIO_IA_PUSH_32UINT(n)    { IDIO_IA_T ia_p32 = idio_ia_compute_32uint (n);  idio_ia_append_free (ia, ia_p32); }
#define IDIO_IA_PUSH_64UINT(n)    { IDIO_IA_T ia_p64 = idio_ia_compute_64uint (n);  idio_ia_append_free (ia, ia_p64); }

IDIO_DEFINE_PRIMITIVE1_DS ("idio-ia-length", idio_ia_length, (IDIO ia), "ia", "\
Return the number of opcodes in `ia`		\n\
						\n\
:param ia: byte code				\n\
:type eenv: struct-idio-ia			\n\
:return: length					\n\
:type args: integer				\n\
")
{
    IDIO_ASSERT (ia);

    if (idio_CSI_idio_ia_s != IDIO_C_TYPE_POINTER_PTYPE (ia)) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_exp ("idio-ia-length", "ia", ia, "struct-idio-ia", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    IDIO_IA_T bc = IDIO_C_TYPE_POINTER_P (ia);

    return idio_integer (IDIO_IA_USIZE (bc));
}

IDIO_DEFINE_PRIMITIVE1_DS ("idio-ia->string", idio_ia2string, (IDIO ia), "ia", "\
Return the byte code in `ia` as a string	\n\
						\n\
:param ia: byte code				\n\
:type ia: struct-idio-ia			\n\
:return: string					\n\
:type args: string				\n\
")
{
    IDIO_ASSERT (ia);

    if (idio_CSI_idio_ia_s != IDIO_C_TYPE_POINTER_PTYPE (ia)) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_exp ("idio-ia->string", "ia", ia, "struct-idio-ia", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    IDIO_IA_T bc = IDIO_C_TYPE_POINTER_P (ia);

    IDIO os = idio_octet_string_C_len ((char const *) bc->ae, IDIO_IA_USIZE (bc));

    return os;
}

IDIO_IA_T idio_codegen_string2idio_ia (IDIO bs)
{
    IDIO_ASSERT (bs);

    IDIO_TYPE_ASSERT (octet_string, bs);

    size_t n = idio_string_len (bs);

    IDIO_IA_T ia = idio_ia (n);

    size_t size = 0;
    char *C_bs = idio_string_as_C (bs, &size);

    if (n != size) {
	fprintf (stderr, "string->idio-ia: %zu != %zu\n", n, size);
    }

    memcpy ((char *) ia->ae, C_bs, n);
    IDIO_IA_USIZE (ia) = n;

    IDIO_GC_FREE (C_bs, size);

    return ia;
}

idio_as_t idio_codegen_extend_constants (IDIO eenv, IDIO v)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO cs = IDIO_MEANING_EENV_CS (eenv);

    idio_as_t ci = idio_array_size (cs);
    idio_array_push (cs, v);

    if (idio_S_nil != v) {
	idio_hash_put (idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_CH), v, idio_fixnum (ci));
    }

    return ci;
}

idio_ai_t idio_codegen_constants_lookup (IDIO eenv, IDIO v)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO cs = IDIO_MEANING_EENV_CS (eenv);

    if (idio_S_nil != v) {
	IDIO fci = idio_hash_ref (idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_CH), v);

	if (idio_S_unspec == fci) {
	    /*
	     * hash(pair) is pair specific not generalised to the
	     * equal?-ness of pair
	     *
	     * So, walk through the array anyway.  Instances mean we
	     * have to use eq?.
	     */
	    return -1;
	} else {
	    return IDIO_FIXNUM_VAL (fci);
	}
    }

    /*
     * This should only be for #n and we should have put that in slot
     * 0...
     */
    return idio_array_find_eqp (cs, v, 0);
}

idio_as_t idio_codegen_constants_lookup_or_extend (IDIO eenv, IDIO v)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, eenv);

    idio_ai_t ci = idio_codegen_constants_lookup (eenv, v);

    if (-1 == ci) {
	ci = idio_codegen_extend_constants (eenv, v);
    }

    return ci;
}

IDIO_DEFINE_PRIMITIVE2_DS ("codegen-constants-lookup-or-extend", codegen_constants_lookup_or_extend, (IDIO eenv, IDIO v), "eenv v", "\
Find `v` in `eenv` or extend `eenv`		\n\
						\n\
:param eenv: tuple				\n\
:type eenv: evaluation environment		\n\
:param v: constant				\n\
:type v: any					\n\
:return: index					\n\
:type args: integer				\n\
")
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, eenv);

    idio_as_t ci = idio_codegen_constants_lookup_or_extend (eenv, v);

    return idio_integer (ci);
}

idio_as_t idio_codegen_extend_src_exprs (IDIO eenv, IDIO src)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (src);

    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO scs = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_SES);
    IDIO_TYPE_ASSERT (array, scs);

    idio_ai_t sci = idio_array_size (scs);
    idio_array_push (scs, src);

    IDIO sps = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_SPS);
    IDIO_TYPE_ASSERT (array, sps);

    IDIO sp = idio_S_nil;
    if (idio_isa_pair (src)) {
	IDIO lo = idio_hash_ref (idio_src_properties, src);

	if (idio_S_unspec != lo){
	    /*
	     * The src_props entry wants to look like (fci line) where
	     * {fci} is the constants index for the filename.
	     *
	     * Otherwise we'll have the compiled byte code referencing
	     * long filenames repeatedly.
	     */
	    IDIO fn = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_NAME);
	    idio_as_t ci = idio_codegen_constants_lookup_or_extend (eenv, fn);

	    sp = IDIO_LIST2 (idio_fixnum (ci),
			     idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_LINE));
	} else {
#ifdef IDIO_DEBUG
	    fprintf (stderr, "icese: no src_properties for entry .%-4zd ", idio_array_size (sps));
	    idio_debug ("%.80s\n", src);
#endif
	}
    } else {
#ifdef IDIO_DEBUG
	fprintf (stderr, "icese: src is not a pair for entry .%-4zd ", idio_array_size (sps));
	idio_debug ("%s\n", src);
#endif
    }
    idio_array_push (sps, sp);

    return sci;
}

/*
 * By and large we want to generate the source code expr describing
 * *this* function call after the code describing the argument
 * function calls (if any).  Otherwise (+ 1 (length foo)) means that
 * the source code EXPR for the argument expression (length foo) will
 * be the outstanding declaration when we come to make the + function
 * call.
 *
 * That won't always work as there's no sensible place for an {if}
 * expression where even the {condition} expression can be arbitrarily
 * complex.
 *
 * Also, ensure the ci is unique: the source properties of this (+ 1
 * 1) are different to the source properties of the next (+ 1 1)
 */
#define IDIO_CODEGEN_SRC_EXPR(iat,e,eenv)				\
    if (idio_isa_pair (e)) {						\
	IDIO_FLAGS (e) |= IDIO_FLAG_CONST;				\
	idio_ai_t sei = idio_codegen_extend_src_exprs (eenv, e);	\
	idio_ia_push (iat, IDIO_A_SRC_EXPR);				\
	IDIO_IA_PUSH_VARUINT_BC (iat, sei);				\
    }


/*
 * Compiling
 *
 * Compiling the intermediate code (IDIO_I_*) is a reasonably
 * straightforward swap to IDIO_A_*.
 *
 * There's some specialisation for particularly common tuples to
 * reduce the size of the byte code and, hopefully, make the resultant
 * interpretation quicker as there's less fetching and decoding of
 * arguments to do.
 *
 * The flip side of that is more C code dedicated to compilation and
 * interpretation of the resultant byte code.
 *
 * Much of the code below, particularly macros, assumes there is an
 * {ia} variable in scope.
 */

void idio_codegen_compile (IDIO thr, IDIO_IA_T ia, IDIO eenv, IDIO m, int depth)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (m);

    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO_TYPE_ASSERT (pair, m);

    /* idio_debug ("compile: %s\n", m); */

    if (! idio_isa_pair (m)) {
	idio_error_param_type ("pair", m, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO mh = IDIO_PAIR_H (m);
    IDIO mt = IDIO_PAIR_T (m);

    if (! IDIO_TYPE_CONSTANT_I_CODEP (mh)) {
	/*
	 * A sequence of IDIO_I_* code segments will appear here as a
	 * list so we simply recurse for each one.
	 */
	if (idio_isa_pair (mh)) {
	    while (idio_S_nil != m) {
		idio_codegen_compile (thr, ia, eenv, IDIO_PAIR_H (m), depth + 1);
		m = IDIO_PAIR_T (m);
		if (! idio_isa_list (m)) {
		    idio_error_param_type ("list", m, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return;
		}
	    }
	    return;
	} else {
	    idio_debug ("\nWARNING: codegen: not a CONSTANT|pair: unexpected intermediate code: %s\n", mh);
	    idio_debug ("%s\n\n", m);
	    idio_dump (mh, 1);
	    idio_codegen_error_param_args ("unknown I-code", m, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    IDIO CTP_bc = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_BYTE_CODE);
    if (idio_CSI_idio_ia_s != IDIO_C_TYPE_POINTER_PTYPE (CTP_bc)) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_exp ("codegen-compile", "byte-code", CTP_bc, "struct-idio-ia-s", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    IDIO_IA_T byte_code = IDIO_C_TYPE_POINTER_P (CTP_bc);

    switch (IDIO_CONSTANT_I_CODE_VAL (mh)) {
    case IDIO_I_CODE_SHALLOW_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("SHALLOW-ARGUMENT-REF j", mt, IDIO_C_FUNC_LOCATION_S ("SHALLOW-ARGUMENT-REF"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_FUNC_LOCATION_S ("SHALLOW-ARGUMENT-REF"));

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
    case IDIO_I_CODE_DEEP_ARGUMENT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("DEEP-ARGUMENT-REF i j", mt, IDIO_C_FUNC_LOCATION_S ("DEEP-ARGUMENT-REF"));

		/* notreached */
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_codegen_error_param_type ("fixnum", i, IDIO_C_FUNC_LOCATION_S ("DEEP-ARGUMENT-REF"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_FUNC_LOCATION_S ("DEEP-ARGUMENT-REF"));

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
		idio_codegen_error_param_args ("SHALLOW-ARGUMENT-SET j m1", mt, IDIO_C_FUNC_LOCATION_S ("SHALLOW-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_FUNC_LOCATION_S ("SHALLOW-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

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
		idio_codegen_error_param_args ("DEEP-ARGUMENT-SET i j m1", mt, IDIO_C_FUNC_LOCATION_S ("DEEP-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO i = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (i)) {
		idio_codegen_error_param_type ("fixnum", i, IDIO_C_FUNC_LOCATION_S ("DEEP-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO j = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (j)) {
		idio_codegen_error_param_type ("fixnum", j, IDIO_C_FUNC_LOCATION_S ("DEEP-ARGUMENT-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_DEEP_ARGUMENT_SET);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (j));
	}
	break;
    case IDIO_I_CODE_SYM_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("SYM-REF si", mt, IDIO_C_FUNC_LOCATION_S ("SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_SYM_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_FUNCTION_SYM_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("FUNCTION-SYM-REF si", mt, IDIO_C_FUNC_LOCATION_S ("FUNCTION-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("FUNCTION-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_FUNCTION_SYM_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_CONSTANT_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("CONSTANT c", mt, IDIO_C_FUNC_LOCATION_S ("CONSTANT"));

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
			}
			break;
		    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
			{
			    intptr_t v = IDIO_UNICODE_VAL (c);

			    IDIO_IA_PUSH1 (IDIO_A_UNICODE);
			    IDIO_IA_PUSH_VARUINT (v);
			    return;
			}
			break;
		    default:
			idio_coding_error_C ("unexpected constant/CONSTANT/??", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT/CONSTANT"));

			/* notreached */
			break;
		    }
		}
		break;
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		idio_coding_error_C ("unexpected constant/PLACEHOLDER", c, IDIO_C_FUNC_LOCATION_S ("CONSTANT"));

		/* notreached */
		break;
	    default:
		{
		    /*
		     * A quoted value...probably.
		     */

		    IDIO_FLAGS (c) |= IDIO_FLAG_CONST;

		    idio_as_t ci = idio_codegen_constants_lookup_or_extend (eenv, c);

		    IDIO_IA_PUSH1 (IDIO_A_CONSTANT_REF);
		    IDIO_IA_PUSH_VARUINT (ci);
		    return;
		}
	    }

	}
	break;
    case IDIO_I_CODE_COMPUTED_SYM_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("COMPUTED-SYM-REF si", mt, IDIO_C_FUNC_LOCATION_S ("COMPUTED-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("COMPUTED-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_SYM_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_SYM_DEF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("SYM-DEF name kind si", mt, IDIO_C_FUNC_LOCATION_S ("SYM-DEF"));

		/* notreached */
		return;
	    }

	    IDIO name = IDIO_PAIR_H (mt);
	    IDIO kind = IDIO_PAIR_HT (mt);
	    IDIO fsi = IDIO_PAIR_HTT (mt);

	    if (! idio_isa_symbol (name)) {
		idio_codegen_error_param_type ("symbol", name, IDIO_C_FUNC_LOCATION_S ("SYM-DEF"));

		/* notreached */
		return;
	    }

	    if (! (idio_S_predef == kind ||
		   idio_S_toplevel == kind ||
		   idio_S_dynamic == kind ||
		   idio_S_environ == kind ||
		   idio_S_computed == kind)) {
		idio_codegen_error_param_type ("symbol predef|toplevel|dynamic|environ|computed", kind, IDIO_C_FUNC_LOCATION_S ("SYM-DEF"));

		/* notreached */
		return;
	    }

	    if (! idio_isa_fixnum (fsi)) {
		idio_codegen_error_param_type ("fixnum", fsi, IDIO_C_FUNC_LOCATION_S ("SYM-DEF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_SYM_DEF);
	    idio_ai_t si = IDIO_FIXNUM_VAL (fsi);
	    IDIO_IA_PUSH_REF (si);
	    idio_ai_t kci = idio_codegen_constants_lookup_or_extend (eenv, kind);
	    IDIO_IA_PUSH_VARUINT (kci);
	}
	break;
    case IDIO_I_CODE_SYM_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("SYM-SET e si m1", mt, IDIO_C_FUNC_LOCATION_S ("SYM-SET"));

		/* notreached */
		return;
	    }

	    IDIO e   = IDIO_PAIR_H (mt);
	    IDIO si = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("SYM-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    IDIO_IA_PUSH1 (IDIO_A_SYM_SET);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_COMPUTED_SYM_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("COMPUTED-SYM-SET si m1", mt, IDIO_C_FUNC_LOCATION_S ("COMPUTED-SYM-SET"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("COMPUTED-SYM-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_SYM_SET);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_COMPUTED_SYM_DEF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("COMPUTED-SYM-DEF si m1", mt, IDIO_C_FUNC_LOCATION_S ("COMPUTED-SYM-DEF"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("COMPUTED-SYM-DEF"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_COMPUTED_SYM_DEF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_VAL_SET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("VAL-SET e vi m1", mt, IDIO_C_FUNC_LOCATION_S ("VAL-SET"));

		/* notreached */
		return;
	    }

	    IDIO e  = IDIO_PAIR_H (mt);
	    IDIO vi = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (vi)) {
		idio_codegen_error_param_type ("fixnum", vi, IDIO_C_FUNC_LOCATION_S ("VAL-SET"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    IDIO_IA_PUSH1 (IDIO_A_VAL_SET);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (vi));
	}
	break;
    case IDIO_I_CODE_PREDEFINED:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("PREDEFINED vi", mt, IDIO_C_FUNC_LOCATION_S ("PREDEFINED"));

		/* notreached */
		return;
	    }

	    IDIO vi = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (vi)) {
		idio_codegen_error_param_type ("fixnum", vi, IDIO_C_FUNC_LOCATION_S ("PREDEFINED"));

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
	     *
	     * $grep PREDEFINED vm-dasm | sed -e 's/.*: //' | sort | uniq -c | sort -nr | head

	     * 1728 PREDEFINED 2 #nil
	     * 1402 PREDEFINED 1 #f
	     *  873 PREDEFINED 0 #t
	     *  216 PREDEFINED 656 PRIM not
	     *  213 PREDEFINED 754 PRIM lt
	     *  203 PREDEFINED 749 PRIM +
	     *  140 PREDEFINED 665 PRIM value-index
	     *  138 PREDEFINED 770 PRIM error
	     *  131 PREDEFINED 843 PRIM apply
	     *  127 PREDEFINED 750 PRIM -

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
    case IDIO_I_CODE_ALTERNATIVE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 4) {
		idio_codegen_error_param_args ("ALTERNATIVE src m1 m2 m3", mt, IDIO_C_FUNC_LOCATION_S ("ALTERNATIVE"));

		/* notreached */
		return;
	    }

	    IDIO src = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_HT (mt);
	    IDIO m2 = IDIO_PAIR_HTT (mt);
	    IDIO m3 = IDIO_PAIR_HTTT (mt);

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
	     * SHORT-JUMP-FALSE with one byte of offset or a
	     * LONG-JUMP-FALSE with 2+ bytes of offset.
	     */

	    /* 2: */
	    IDIO_CODEGEN_SRC_EXPR (ia, src, eenv);

	    IDIO_IA_PUSH1 (IDIO_A_SUPPRESS_RCSE);
	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POP_RCSE);

	    IDIO_IA_T ia_m2 = idio_ia (100);
	    idio_codegen_compile (thr, ia_m2, eenv, m2, depth + 1);

	    IDIO_IA_T ia_m3 = idio_ia (100);
	    idio_codegen_compile (thr, ia_m3, eenv, m3, depth + 1);

	    IDIO_IA_T g7 = NULL;
	    size_t g7_len = 0;

	    /*
	     * include the size of the jump instruction!
	     *
	     * g7_len == size(ins) + size(off)
	     */
	    if (IDIO_IA_USIZE (ia_m3) <= IDIO_IA_VARUINT_1BYTE) {
		g7_len = 1 + 1;
	    } else {
		g7 = idio_ia_compute_varuint (IDIO_IA_USIZE (ia_m3));
		g7_len = 1 + IDIO_IA_USIZE (g7);
	    }

	    size_t jf6_off = IDIO_IA_USIZE (ia_m2) + g7_len;

	    /* 3: */
	    if (jf6_off <= IDIO_IA_VARUINT_1BYTE) {
		IDIO_IA_PUSH1 (IDIO_A_SHORT_JUMP_FALSE);
		IDIO_IA_PUSH1 (jf6_off);
	    } else {
		IDIO_IA_PUSH1 (IDIO_A_LONG_JUMP_FALSE);
		IDIO_IA_PUSH_VARUINT (jf6_off);
	    }

	    /* 4: */
	    idio_ia_append_free (ia, ia_m2);

	    /* 5: */
	    if (IDIO_IA_USIZE (ia_m3) <= IDIO_IA_VARUINT_1BYTE) {
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (IDIO_IA_USIZE (ia_m3));
	    } else {
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_ia_append_free (ia, g7);
	    }

	    /* 6: */
	    idio_ia_append_free (ia, ia_m3);
	}
	break;
    case IDIO_I_CODE_SEQUENCE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("SEQUENCE m1 m+", mt, IDIO_C_FUNC_LOCATION_S ("SEQUENCE"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    idio_codegen_compile (thr, ia, eenv, mp, depth + 1);
	}
	break;
    case IDIO_I_CODE_TR_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 4) {
		idio_codegen_error_param_args ("TR-FIX-LET m* m+ formals src", mt, IDIO_C_FUNC_LOCATION_S ("TR-FIX-LET"));

		/* notreached */
		return;
	    }

	    IDIO ms      = IDIO_PAIR_H (mt);
	    IDIO mp      = IDIO_PAIR_HT (mt);
	    IDIO formals = IDIO_PAIR_HTT (mt);
	    IDIO src     = IDIO_PAIR_HTTT (mt);

	    idio_as_t fci = idio_codegen_constants_lookup_or_extend (eenv, formals);
	    idio_as_t sei = idio_codegen_extend_src_exprs (eenv, src);

	    idio_codegen_compile (thr, ia, eenv, ms, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_LINK_FRAME);
	    IDIO_IA_PUSH_VARUINT (fci);
	    IDIO_IA_PUSH_VARUINT (sei);

	    idio_codegen_compile (thr, ia, eenv, mp, depth + 1);
	}
	break;
    case IDIO_I_CODE_FIX_LET:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 4) {
		idio_codegen_error_param_args ("FIX-LET m* m+ formals src", mt, IDIO_C_FUNC_LOCATION_S ("FIX-LET"));

		/* notreached */
		return;
	    }

	    IDIO ms      = IDIO_PAIR_H (mt);
	    IDIO mp      = IDIO_PAIR_HT (mt);
	    IDIO formals = IDIO_PAIR_HTT (mt);
	    IDIO src     = IDIO_PAIR_HTTT (mt);

	    idio_as_t fci = idio_codegen_constants_lookup_or_extend (eenv, formals);
	    idio_as_t sei = idio_codegen_extend_src_exprs (eenv, src);

	    idio_codegen_compile (thr, ia, eenv, ms, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_LINK_FRAME);
	    IDIO_IA_PUSH_VARUINT (fci);
	    IDIO_IA_PUSH_VARUINT (sei);

	    idio_codegen_compile (thr, ia, eenv, mp, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_UNLINK_FRAME);
	}
	break;
    case IDIO_I_CODE_LOCAL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 5) {
		idio_codegen_error_param_args ("LOCAL e i m1 m+ formal*", mt, IDIO_C_FUNC_LOCATION_S ("LOCAL"));

		/* notreached */
		return;
	    }

	    IDIO e       = IDIO_PAIR_H (mt);
	    IDIO i       = IDIO_PAIR_HT (mt);
	    IDIO m1      = IDIO_PAIR_HTT (mt);
	    IDIO mp      = IDIO_PAIR_HTTT (mt);
	    IDIO formals = IDIO_PAIR_HTTTT (mt);

	    idio_as_t fci = idio_codegen_constants_lookup_or_extend (eenv, formals);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    idio_ia_push (ia, IDIO_A_EXTEND_FRAME);
	    IDIO_IA_PUSH_VARUINT (idio_list_length (formals));
	    IDIO_IA_PUSH_VARUINT (fci);

	    IDIO_IA_PUSH1 (IDIO_A_SHALLOW_ARGUMENT_SET);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (i));

	    idio_codegen_compile (thr, ia, eenv, mp, depth + 1);
	}
	break;
    case IDIO_I_CODE_PRIMCALL0:
	{
	    if (! idio_isa_pair (mt) ||
		(idio_list_length (mt) != 3)) {
		idio_codegen_error_param_args ("CALL0 e ins pd", mt, IDIO_C_FUNC_LOCATION_S ("CALL0"));

		/* notreached */
		return;
	    }

	    IDIO e   = IDIO_PAIR_H (mt);
	    /* IDIO ins = IDIO_PAIR_HT (mt); */
	    IDIO pd  = IDIO_PAIR_HTT (mt);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    IDIO_IA_PUSH_VARUINT (IDIO_A_PRIMCALL0);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (pd));
	}
	break;
    case IDIO_I_CODE_PRIMCALL1:
	{
	    if (! idio_isa_pair (mt) ||
		(idio_list_length (mt) != 4)) {
		idio_codegen_error_param_args ("CALL1 e ins m1 pd", mt, IDIO_C_FUNC_LOCATION_S ("CALL1"));

		/* notreached */
		return;
	    }

	    IDIO e   = IDIO_PAIR_H (mt);
	    /* IDIO ins = IDIO_PAIR_HT (mt); */
	    IDIO m1  = IDIO_PAIR_HTT (mt);
	    IDIO pd  = IDIO_PAIR_HTTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    IDIO_IA_PUSH_VARUINT (IDIO_A_PRIMCALL1);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (pd));
	}
	break;
    case IDIO_I_CODE_PRIMCALL2:
	{
	    if (! idio_isa_pair (mt) ||
		(idio_list_length (mt) != 5)) {
		idio_codegen_error_param_args ("CALL2 e ins m1 m2 pd", mt, IDIO_C_FUNC_LOCATION_S ("CALL2"));

		/* notreached */
		return;
	    }

	    IDIO e   = IDIO_PAIR_H (mt);
	    /* IDIO ins = IDIO_PAIR_HT (mt); */
	    IDIO m1  = IDIO_PAIR_HTT (mt);
	    IDIO m2  = IDIO_PAIR_HTTT (mt);
	    IDIO pd  = IDIO_PAIR_HTTTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, eenv, m2, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POP_REG1);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    IDIO_IA_PUSH_VARUINT (IDIO_A_PRIMCALL2);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (pd));
	}
	break;
    case IDIO_I_CODE_TR_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("TR-REGULAR-CALL e m1 m*", mt, IDIO_C_FUNC_LOCATION_S ("TR-REGULAR-CALL"));

		/* notreached */
		return;
	    }

	    IDIO e = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_HT (mt);
	    IDIO ms = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, eenv, ms, depth + 1);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    IDIO_IA_PUSH2 (IDIO_A_POP_FUNCTION, IDIO_A_FUNCTION_GOTO);
	}
	break;
    case IDIO_I_CODE_REGULAR_CALL:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("REGULAR-CALL e m1 m*", mt, IDIO_C_FUNC_LOCATION_S ("REGULAR-CALL"));

		/* notreached */
		return;
	    }

	    IDIO e = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_HT (mt);
	    IDIO ms = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, eenv, ms, depth + 1);

	    IDIO_CODEGEN_SRC_EXPR (ia, e, eenv);

	    IDIO_IA_PUSH4 (IDIO_A_POP_FUNCTION, IDIO_A_PRESERVE_STATE, IDIO_A_FUNCTION_INVOKE, IDIO_A_RESTORE_STATE);
	}
	break;
    case IDIO_I_CODE_FIX_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 7) {
		idio_codegen_error_param_args ("FIX-CLOSURE m+ name arity formals docstr src vi", mt, IDIO_C_FUNC_LOCATION_S ("FIX-CLOSURE"));

		/* notreached */
		return;
	    }

	    IDIO mp      = idio_list_nth (mt, 0, idio_S_nil);
	    IDIO name    = idio_list_nth (mt, 1, idio_S_nil);
	    IDIO arity   = idio_list_nth (mt, 2, idio_S_nil);
	    IDIO formals = idio_list_nth (mt, 3, idio_S_nil);
	    IDIO docstr  = idio_list_nth (mt, 4, idio_S_nil);
	    IDIO src     = idio_list_nth (mt, 5, idio_S_nil);
	    IDIO vi      = idio_list_nth (mt, 6, idio_S_nil);

	    if (! idio_isa_fixnum (arity)) {
		idio_codegen_error_param_type ("fixnum", arity, IDIO_C_FUNC_LOCATION_S ("FIX-CLOSURE"));

		/* notreached */
		return;
	    }

	    idio_as_t nci  = idio_codegen_constants_lookup_or_extend (eenv, name);
	    idio_as_t fci  = idio_codegen_constants_lookup_or_extend (eenv, formals);
	    idio_as_t dsci = idio_codegen_constants_lookup_or_extend (eenv, docstr);

	    idio_as_t sei = idio_codegen_extend_src_exprs (eenv, src);

	    /*
	     * **** In the Beginning
	     *
	     * Remember this is the act of creating a closure *value*
	     * not running the (body of the) closure.
	     *
	     * {the-function}, the body of the function, will be
	     * prefaced with an arity check then whatever m+ consists
	     * of.
	     *
	     * Obviously {the-function} is some arbitrary length and
	     * we don't want to run it now, we are only creating the
	     * closure value and that creation is essentially a call
	     * to idio_closure() with a "pointer" to the start of
	     * {the-function}.  Not really a pointer, of course, but
	     * rather where {the-function} starts relative to where we
	     * are now.
	     *
	     * Thinking ahead, having created the closure value we now
	     * want to jump over {the-function} -- we don't want to
	     * run it -- which will be a SHORT- or a LONG- GOTO.  A
	     * SHORT-GOTO is fixed at two bytes but a LONG-GOTO could
	     * be 9 bytes which means that the "pointer" to
	     * {the-function} in the CREATE-CLOSURE instruction will
	     * vary depending on how many bytes are required to
	     * instruct us to jump over {the-function}.
	     *
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
	     *
	     * Here there are obvious features like the constants
	     * indexes for signature-string and documentation-string.
	     *
	     * A less obvious feature is the length of {the-function}.
	     * Actually it will get emitted twice, once as a feature
	     * and then as the GOTO.  We want it as a feature in order
	     * that we can add it to the closure value.  That means we
	     * can disassemble just this function.  If we don't pass
	     * it as a feature then, when we are about to disassemble
	     * the closure, it is impossible for us to reverse
	     * engineer the GOTO immediately before {the-function} as
	     * some LONG-GOTO can look like a combination of
	     * CREATE-CLOSURE len [features].
	     *
	     * XXX if we want the closure *value* to know about its
	     * own arity (and varargs) -- don't forget the closure
	     * value doesn't know about its arity/varargs,
	     * {the-function} does but not the value -- then we would
	     * need to pass them to CREATE_CLOSURE as more [features].
	     *
	     * Why would we do that?  The only current reason is to
	     * test if a closure is a thunk, ie. arity 0 and no
	     * varargs.  Currently there's no pressing need for a
	     * thunk? predicate.
	     *
	     * **** Then, post-0.2...
	     *
	     * We can always create an anonymous function directly in
	     * byte_code and keep a note of the {vi} for it.  This
	     * uses a new CREATE-FUNCTION opcode which is ostensibly
	     * the old CREATE-CLOSURE opcode.  The only real
	     * difference being that the (debug) stats need to be
	     * shared with future CREATE-CLOSURE instances.
	     *
	     * All the heavy lifting of creating a closure/function
	     * instance (the PC, the name, signature and documentation
	     * strings etc.) are set up here, once.
	     *
	     * The entity returned by the CREATE-FUNCTION opcode will
	     * be a valid closure -- top level because the VM won't
	     * have a non-#n frame to give it when it is processed --
	     * but, at this stage, nothing is going to call it.  Which
	     * is partly why it doesn't need a name.  Hence the
	     * lifted-{name} code being #define'd out.
	     *
	     * Then, at the original place in the (possibly nested)
	     * code, we can insert the (new) CREATE-CLOSURE opcode
	     * which simply dereferences the {vi} for the top level
	     * function, copies the useful bits, adds the current VM
	     * frame and reuses the stats area.
	     *
	     * Note, however, that the functionality[sic] is
	     * unchanged.  There's no lambda lifting per se and all
	     * actual, runnable closures are the CREATE-CLOSUREs.  All
	     * we've done is some slight shuffling of the physical
	     * location of the function's byte code from being
	     * embedded to being "up there" somewhere.
	     *
	     * Driving this change is that there are, roughly 2200
	     * abstractions seen during a run of the test suite which
	     * become 189,000 closures.  Removing the duplicated
	     * creation of 187k sets of closure properties is a
	     * reasonable timesaver.
	     */

	    /* the-function */
	    IDIO_IA_T ia_tf = idio_ia (200);
	    switch (IDIO_FIXNUM_VAL (arity) + 1) {
	    case 1: idio_ia_push (ia_tf, IDIO_A_ARITY1P); break;
	    case 2: idio_ia_push (ia_tf, IDIO_A_ARITY2P); break;
	    case 3: idio_ia_push (ia_tf, IDIO_A_ARITY3P); break;
	    case 4: idio_ia_push (ia_tf, IDIO_A_ARITY4P); break;
	    default:
		{
		    idio_ia_push (ia_tf, IDIO_A_ARITYEQP);
		    IDIO_IA_PUSH_VARUINT_BC (ia_tf, IDIO_FIXNUM_VAL (arity) + 1);
		}
		break;
	    }

	    idio_ia_push (ia_tf, IDIO_A_LINK_FRAME);
	    IDIO_IA_PUSH_VARUINT_BC (ia_tf, fci);
	    IDIO_IA_PUSH_VARUINT_BC (ia_tf, sei);

	    idio_codegen_compile (thr, ia_tf, eenv, mp, depth + 1);
	    idio_ia_push (ia_tf, IDIO_A_RETURN);

	    IDIO_IA_T ia_orig = ia;
	    IDIO_IA_T ia = idio_ia (1024);

	    idio_pc_t code_len = IDIO_IA_USIZE (ia_tf);
	    if (code_len <= IDIO_IA_VARUINT_1BYTE) {
		/* 2: */
		IDIO_IA_PUSH2 (IDIO_A_CREATE_FUNCTION, 2);
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (nci);
		IDIO_IA_PUSH_VARUINT (fci);
		IDIO_IA_PUSH_VARUINT (dsci);
		IDIO_IA_PUSH_VARUINT (sei);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (code_len);
	    } else {
		/* 2: */
		IDIO_IA_T g5 = idio_ia_compute_varuint (code_len);
		IDIO_IA_PUSH2 (IDIO_A_CREATE_FUNCTION, (1 + IDIO_IA_USIZE (g5)));
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (nci);
		IDIO_IA_PUSH_VARUINT (fci);
		IDIO_IA_PUSH_VARUINT (dsci);
		IDIO_IA_PUSH_VARUINT (sei);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_ia_append_free (ia, g5);
	    }

	    /* 4: */
	    idio_ia_append_free (ia, ia_tf);

	    /*
	     * We want to emulate a top level assignment to a new
	     * symbol and, post-CREATE-FUNCTION, new global value
	     * index which can be referred to by the CREATE-CLOSURE
	     * code.
	     *
	     * However, we don't need the new (lifted) symbol as
	     * nothing has a need to refer to it.
	     */
	    IDIO_IA_T ia_fn = ia;
	    ia = idio_ia (1024);

#ifdef IDIO_CODEGEN_LIFTED_NAME
	    IDIO lifted_name = IDIO_GENSYM ("lifted");
	    idio_as_t ci = idio_codegen_constants_lookup_or_extend (eenv, lifted_name);

	    IDIO_IA_PUSH1 (IDIO_A_SYM_DEF);
	    IDIO_IA_PUSH_REF (ci);
	    ci = idio_codegen_constants_lookup_or_extend (eenv, idio_S_toplevel);
	    IDIO_IA_PUSH_VARUINT (ci);
#endif

	    idio_ia_append (ia, ia_fn);

	    idio_as_t C_vi = IDIO_FIXNUM_VAL (vi);

	    IDIO_IA_PUSH1 (IDIO_A_VAL_SET);
	    IDIO_IA_PUSH_REF (C_vi);

	    /*
	     * 1. don't free {ia}, we're about to reuse it
	     *
	     * 2. pushing directly onto byte_code seems a bit dubious.
	     *    In particular, the code will be outside any ABORTs.
	     *
	     * 3. do a regular CREATE-CLOSURE which refers to the
	     *    CREATE-FUNCTION {vi} we just created
	     */
	    idio_ia_append (byte_code, ia);

	    /* reset ia to reuse it */
	    IDIO_IA_USIZE (ia) = 0;
	    IDIO_IA_PUSH1 (IDIO_A_CREATE_CLOSURE);
	    IDIO_IA_PUSH_REF (C_vi);
	    idio_ia_append_free (ia_orig, ia);
	}
	break;
    case IDIO_I_CODE_NARY_CLOSURE:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 7) {
		idio_codegen_error_param_args ("NARY-CLOSURE m+ name arity formals docstr src vi", mt, IDIO_C_FUNC_LOCATION_S ("NARY-CLOSURE"));

		/* notreached */
		return;
	    }

	    IDIO mp      = idio_list_nth (mt, 0, idio_S_nil);
	    IDIO name    = idio_list_nth (mt, 1, idio_S_nil);
	    IDIO arity   = idio_list_nth (mt, 2, idio_S_nil);
	    IDIO formals = idio_list_nth (mt, 3, idio_S_nil);
	    IDIO docstr  = idio_list_nth (mt, 4, idio_S_nil);
	    IDIO src     = idio_list_nth (mt, 5, idio_S_nil);
	    IDIO vi      = idio_list_nth (mt, 6, idio_S_nil);

	    if (! idio_isa_fixnum (arity)) {
		idio_codegen_error_param_type ("fixnum", arity, IDIO_C_FUNC_LOCATION_S ("NARY-CLOSURE"));

		/* notreached */
		return;
	    }

	    idio_as_t nci  = idio_codegen_constants_lookup_or_extend (eenv, name);
	    idio_as_t fci  = idio_codegen_constants_lookup_or_extend (eenv, formals);
	    idio_as_t dsci = idio_codegen_constants_lookup_or_extend (eenv, docstr);

	    idio_as_t sei = idio_codegen_extend_src_exprs (eenv, src);

	    /*
	     * See IDIO_I_CODE_FIX_CLOSURE for commentary.
	     */

	    /* the-function */
	    IDIO_IA_T ia_tf = idio_ia (200);
	    idio_ia_push (ia_tf, IDIO_A_ARITYGEP);
	    IDIO_IA_PUSH_VARUINT_BC (ia_tf, IDIO_FIXNUM_VAL (arity) + 1);

	    idio_ia_push (ia_tf, IDIO_A_PACK_FRAME);
	    IDIO_IA_PUSH_VARUINT_BC (ia_tf, IDIO_FIXNUM_VAL (arity));

	    idio_ia_push (ia_tf, IDIO_A_LINK_FRAME);
	    IDIO_IA_PUSH_VARUINT_BC (ia_tf, fci);
	    IDIO_IA_PUSH_VARUINT_BC (ia_tf, sei);

	    idio_codegen_compile (thr, ia_tf, eenv, mp, depth + 1);
	    idio_ia_push (ia_tf, IDIO_A_RETURN);

	    IDIO_IA_T ia_orig = ia;
	    IDIO_IA_T ia = idio_ia (1024);

	    idio_pc_t code_len = IDIO_IA_USIZE (ia_tf);
	    if (code_len <= IDIO_IA_VARUINT_1BYTE) {
		/* 2: */
		IDIO_IA_PUSH2 (IDIO_A_CREATE_FUNCTION, 2);
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (nci);
		IDIO_IA_PUSH_VARUINT (fci);
		IDIO_IA_PUSH_VARUINT (dsci);
		IDIO_IA_PUSH_VARUINT (sei);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_SHORT_GOTO);
		IDIO_IA_PUSH1 (code_len);
	    } else {
		/* 2: */
		IDIO_IA_T g5 = idio_ia_compute_varuint (code_len);
		IDIO_IA_PUSH2 (IDIO_A_CREATE_FUNCTION, (1 + IDIO_IA_USIZE (g5)));
		IDIO_IA_PUSH_VARUINT (code_len);
		IDIO_IA_PUSH_VARUINT (nci);
		IDIO_IA_PUSH_VARUINT (fci);
		IDIO_IA_PUSH_VARUINT (dsci);
		IDIO_IA_PUSH_VARUINT (sei);

		/* 3: */
		IDIO_IA_PUSH1 (IDIO_A_LONG_GOTO);
		idio_ia_append_free (ia, g5);
	    }

	    /* 4: */
	    idio_ia_append_free (ia, ia_tf);

	    /*
	     * Again, see notes above
	     */
	    IDIO_IA_T ia_fn = ia;
	    ia = idio_ia (1024);

#ifdef IDIO_CODEGEN_LIFTED_NAME
	    IDIO lifted_name = IDIO_GENSYM ("lifted");
	    idio_as_t ci = idio_codegen_constants_lookup_or_extend (eenv, lifted_name);

	    IDIO_IA_PUSH1 (IDIO_A_SYM_DEF);
	    IDIO_IA_PUSH_REF (ci);
	    ci = idio_codegen_constants_lookup_or_extend (eenv, idio_S_toplevel);
	    IDIO_IA_PUSH_VARUINT (ci);
#endif

	    idio_ia_append (ia, ia_fn);

	    idio_as_t C_vi = IDIO_FIXNUM_VAL (vi);

	    IDIO_IA_PUSH1 (IDIO_A_VAL_SET);
	    IDIO_IA_PUSH_REF (C_vi);

	    idio_ia_append (byte_code, ia);

	    IDIO_IA_USIZE (ia) = 0;
	    IDIO_IA_PUSH1 (IDIO_A_CREATE_CLOSURE);
	    IDIO_IA_PUSH_REF (C_vi);
	    idio_ia_append_free (ia_orig, ia);
	}
	break;
    case IDIO_I_CODE_STORE_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("STORE-ARGUMENT m1 m* rank", mt, IDIO_C_FUNC_LOCATION_S ("STORE-ARGUMENT"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_HT (mt);
	    IDIO rank = IDIO_PAIR_HTT (mt);

	    if (! idio_isa_fixnum (rank)) {
		idio_codegen_error_param_type ("fixnum", rank, IDIO_C_FUNC_LOCATION_S ("STORE-ARGUMENT"));

		/* notreached */
		return;
	    }

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, eenv, ms, depth + 1);
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
    case IDIO_I_CODE_LIST_ARGUMENT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("LIST-ARGUMENT m1 m* arity", mt, IDIO_C_FUNC_LOCATION_S ("LIST-ARGUMENT"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);
	    IDIO ms = IDIO_PAIR_HT (mt);
	    IDIO arity = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_VALUE);
	    idio_codegen_compile (thr, ia, eenv, ms, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POP_LIST_FRAME);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (arity));
	}
	break;
    case IDIO_I_CODE_ALLOCATE_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("ALLOCATE-FRAME arity", mt, IDIO_C_FUNC_LOCATION_S ("ALLOCATE-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO arity = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (arity)) {
		idio_codegen_error_param_type ("fixnum", arity, IDIO_C_FUNC_LOCATION_S ("ALLOCATE-FRAME"));

		/* notreached */
		return;
	    }

	    switch (IDIO_FIXNUM_VAL (arity)) {
	    case 0:
		IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME1);
		/* no args, no need to push an empty list ref */
		break;
	    case 1:
		IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME2);
		break;
	    case 2:
		IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME3);
		break;
	    case 3:
		IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME4);
		break;
	    case 4:
		IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME5);
		break;
	    default:
		IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_FRAME);
		IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (arity) + 1);
		break;
	    }
	}
	break;
    case IDIO_I_CODE_ALLOCATE_DOTTED_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("ALLOCATE-DOTTED-FRAME arity", mt, IDIO_C_FUNC_LOCATION_S ("ALLOCATE-DOTTED-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO arity = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (arity)) {
		idio_codegen_error_param_type ("fixnum", arity, IDIO_C_FUNC_LOCATION_S ("ALLOCATE-DOTTED-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_ALLOCATE_DOTTED_FRAME);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (arity) + 1);
	}
	break;
    case IDIO_I_CODE_REUSE_FRAME:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("REUSE-FRAME arity", mt, IDIO_C_FUNC_LOCATION_S ("REUSE-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO arity = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (arity)) {
		idio_codegen_error_param_type ("fixnum", arity, IDIO_C_FUNC_LOCATION_S ("REUSE-FRAME"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_REUSE_FRAME);
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (arity) + 1);
	}
	break;
    case IDIO_I_CODE_PUSH_DYNAMIC:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("PUSH-DYNAMIC si m1", mt, IDIO_C_FUNC_LOCATION_S ("PUSH-DYNAMIC"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("PUSH-DYNAMIC"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_DYNAMIC);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_POP_DYNAMIC:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("POP-DYNAMIC", mt, IDIO_C_FUNC_LOCATION_S ("POP-DYNAMIC"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_DYNAMIC);
	}
	break;
    case IDIO_I_CODE_DYNAMIC_SYM_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("DYNAMIC-SYM-REF si", mt, IDIO_C_FUNC_LOCATION_S ("DYNAMIC-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("DYNAMIC-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_SYM_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_DYNAMIC_FUNCTION_SYM_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("DYNAMIC-FUNCTION-SYM-REF si", mt, IDIO_C_FUNC_LOCATION_S ("DYNAMIC-FUNCTION-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("DYNAMIC-FUNCTION-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_DYNAMIC_FUNCTION_SYM_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_PUSH_ENVIRON:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("PUSH-ENVIRON si m1", mt, IDIO_C_FUNC_LOCATION_S ("PUSH-ENVIRON"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("PUSH-ENVIRON"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_ENVIRON);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_POP_ENVIRON:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("POP-ENVIRON", mt, IDIO_C_FUNC_LOCATION_S ("POP-ENVIRON"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_ENVIRON);
	}
	break;
    case IDIO_I_CODE_ENVIRON_SYM_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("ENVIRON-SYM-REF si", mt, IDIO_C_FUNC_LOCATION_S ("ENVIRON-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("ENVIRON-SYM-REF"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_ENVIRON_SYM_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_PUSH_TRAP:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("PUSH-TRAP si", mt, IDIO_C_FUNC_LOCATION_S ("PUSH-TRAP"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("PUSH-TRAP"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_TRAP);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_POP_TRAP:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("POP-TRAP", mt, IDIO_C_FUNC_LOCATION_S ("POP-TRAP"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_TRAP);
	}
	break;
    case IDIO_I_CODE_PUSH_ESCAPER:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		fprintf (stderr, "ll mt=%zu\n", idio_list_length (mt));
		idio_codegen_error_param_args ("PUSH-ESCAPER ci m+", mt, IDIO_C_FUNC_LOCATION_S ("PUSH-ESCAPER"));

		/* notreached */
		return;
	    }

	    IDIO ci = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (ci)) {
		idio_codegen_error_param_type ("fixnum", ci, IDIO_C_FUNC_LOCATION_S ("PUSH-ESCAPER"));

		/* notreached */
		return;
	    }

	    IDIO_IA_T ia_m = idio_ia (200);
	    idio_codegen_compile (thr, ia_m, eenv, mp, depth + 1);

	    /*
	     * This is an "absolute" offset -- which feels wrong.
	     *
	     * However, we don't know what the PC is at the time an
	     * escape is invoked so we'll have to live with an
	     * absolute value rather than a relative one.
	     */
	    IDIO_IA_PUSH1 (IDIO_A_PUSH_ESCAPER);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (ci));
	    IDIO_IA_PUSH_VARUINT (IDIO_IA_USIZE (ia_m));

	    idio_ia_append_free (ia, ia_m);
	}
	break;
    case IDIO_I_CODE_POP_ESCAPER:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("POP-ESCAPER", mt, IDIO_C_FUNC_LOCATION_S ("POP-ESCAPER"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_POP_ESCAPER);
	}
	break;
    case IDIO_I_CODE_ESCAPER_LABEL_REF:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("ESCAPER-LABEL_REF ci m+", mt, IDIO_C_FUNC_LOCATION_S ("ESCAPER-LABEL_REF"));

		/* notreached */
		return;
	    }

	    IDIO ci = IDIO_PAIR_H (mt);
	    IDIO mp = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (ci)) {
		idio_codegen_error_param_type ("fixnum", ci, IDIO_C_FUNC_LOCATION_S ("ESCAPER-LABEL_REF"));

		/* notreached */
		return;
	    }

	    idio_codegen_compile (thr, ia, eenv, mp, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_ESCAPER_LABEL_REF);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (ci));
	}
	break;
    case IDIO_I_CODE_AND:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 2) {
		idio_codegen_error_param_args ("AND tail? m+", mt, IDIO_C_FUNC_LOCATION_S ("AND"));

		/* notreached */
		return;
	    }

	    /*
	     * We don't know how far to jump to the end of the AND
	     * sequence until we have generated the code for the
	     * remaining clauses *and* the inter-clause jumps
	     *
	     * 1: compile the first clause and append it to ia
	     *    (nothing special here)
	     *
	     * 2: We can compile the meaning of each remaining clause
	     *    into iac[].
	     *
	     * 3: create a couple of holding buffers: {iat} for the
	     *    per-clause calculations, and {iar} for the
	     *    accumulated result of all the per-clause shuffling
	     *
	     * 3: Working *backwards* from n to 1, on behalf of clause
	     *    m-1 we can:
	     *
	     *   a: calculate the "jump to the end" which is the
	     *      accumulated "jump to the end" plus the size of
	     *      iac[m]
	     *
	     *   b: generate the JUMP-FALSE "jump to the end" in a
	     *      temp buffer, iat
	     *
	     *   c: append iac[m] to iat
	     *
	     *   d: append iar to iat
	     *
	     *   e: copy iat back over iar
	     *
	     *   f: set the accumulated "jump to the end" to be the
	     *      size of iar
	     *
	     * 4: append iar to ia
	     */
	    IDIO tailp = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_HT (mt);
	    mt = IDIO_PAIR_TT (mt);

	    ssize_t n = idio_list_length (mt);

	    if (n ||
		idio_S_false == tailp) {
		IDIO_IA_PUSH1 (IDIO_A_SUPPRESS_RCSE);
	    }
	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    if (n ||
		idio_S_false == tailp) {
		IDIO_IA_PUSH1 (IDIO_A_POP_RCSE);
	    }

	    if (n) {
		IDIO_IA_T iac[n];
		intptr_t i;

		/*
		 * 1st pass, l is only the accumulated code size for
		 * allocating iar/iat
		 */
		size_t l = 0;
		for (i = 0; i < n ; i++) {
		    iac[i] = idio_ia (100);

		    if (i < (n - 1) ||
			idio_S_false == tailp) {
			idio_ia_push (iac[i], IDIO_A_SUPPRESS_RCSE);
		    }
		    idio_codegen_compile (thr, iac[i], eenv, IDIO_PAIR_H (mt), depth + 1);
		    if (i < (n - 1) ||
			idio_S_false == tailp) {
			idio_ia_push (iac[i], IDIO_A_POP_RCSE);
		    }

		    l += IDIO_IA_USIZE (iac[i]);
		    mt = IDIO_PAIR_T (mt);
		}

		/*
		 * How much temporary instruction space?
		 *
		 * largest varuint is 9 bytes, jump ins is one and
		 * there will be n of them.
		 *
		 * To grow the result we need (jump+iac[i]) in iat
		 * then append iar then copy iat back to iar
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
			idio_ia_push (iat, IDIO_A_LONG_JUMP_FALSE);
			IDIO_IA_PUSH_VARUINT_BC (iat, l);
		    }

		    idio_ia_append_free (iat, iac[i]);

		    idio_ia_append (iat, iar);
		    idio_ia_copy (iar, iat);
		    IDIO_IA_USIZE (iat) = 0;
		    l = IDIO_IA_USIZE (iar);
		}

		idio_ia_append_free (ia, iar);
		idio_ia_free (iat);
	    }
	}
	break;
    case IDIO_I_CODE_OR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 2) {
		idio_codegen_error_param_args ("OR tail? m+", mt, IDIO_C_FUNC_LOCATION_S ("OR"));

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
	    IDIO tailp = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_HT (mt);
	    mt = IDIO_PAIR_TT (mt);

	    ssize_t n = idio_list_length (mt);

	    if (n ||
		idio_S_false == tailp) {
		IDIO_IA_PUSH1 (IDIO_A_SUPPRESS_RCSE);
	    }
	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    if (n ||
		idio_S_false == tailp) {
		IDIO_IA_PUSH1 (IDIO_A_POP_RCSE);
	    }

	    if (n) {
		IDIO_IA_T iac[n];
		intptr_t i;

		/*
		 * 1st pass, l is only the accumulated code size for
		 * allocating iar/iat
		 */
		size_t l = 0;
		for (i = 0; i < n ; i++) {
		    iac[i] = idio_ia (100);

		    if (i < (n - 1) ||
			idio_S_false == tailp) {
			idio_ia_push (iac[i], IDIO_A_SUPPRESS_RCSE);
		    }
		    idio_codegen_compile (thr, iac[i], eenv, IDIO_PAIR_H (mt), depth + 1);
		    if (i < (n - 1) ||
			idio_S_false == tailp) {
			idio_ia_push (iac[i], IDIO_A_POP_RCSE);
		    }

		    l += IDIO_IA_USIZE (iac[i]);
		    mt = IDIO_PAIR_T (mt);
		}

		/*
		 * How much temporary instruction space?
		 *
		 * largest varuint is 9 bytes, jump ins is one and
		 * there will be n of them.
		 *
		 * To grow the result we need (jump+iac[i]) in iat
		 * then append iar then copy iat back to iar
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
			idio_ia_push (iat, IDIO_A_LONG_JUMP_TRUE);
			IDIO_IA_PUSH_VARUINT_BC (iat, l);
		    }

		    idio_ia_append_free (iat, iac[i]);

		    idio_ia_append (iat, iar);
		    idio_ia_copy (iar, iat);
		    IDIO_IA_USIZE (iat) = 0;
		    l = IDIO_IA_USIZE (iar);
		}

		idio_ia_append_free (ia, iar);
		idio_ia_free (iat);
	    }
	}
	break;
    case IDIO_I_CODE_BEGIN:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) < 2) {
		idio_codegen_error_param_args ("BEGIN tail? m+", mt, IDIO_C_FUNC_LOCATION_S ("BEGIN"));

		/* notreached */
		return;
	    }

	    /* IDIO tailp = IDIO_PAIR_H (mt); */
	    mt = IDIO_PAIR_T (mt);

	    while (idio_S_nil != mt) {
		idio_codegen_compile (thr, ia, eenv, IDIO_PAIR_H (mt), depth + 1);
		mt = IDIO_PAIR_T (mt);
	    }
	}
	break;
    case IDIO_I_CODE_NOT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("NOT tail? m1", mt, IDIO_C_FUNC_LOCATION_S ("NOT"));

		/* notreached */
		return;
	    }

	    IDIO tailp = IDIO_PAIR_H (mt);
	    IDIO m1 = IDIO_PAIR_HT (mt);

	    if (idio_S_false == tailp) {
		IDIO_IA_PUSH1 (IDIO_A_SUPPRESS_RCSE);
	    }
	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    if (idio_S_false == tailp) {
		IDIO_IA_PUSH1 (IDIO_A_POP_RCSE);
	    }
	    IDIO_IA_PUSH1 (IDIO_A_NOT);
	}
	break;
    case IDIO_I_CODE_EXPANDER:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 2) {
		idio_codegen_error_param_args ("EXPANDER si m1", mt, IDIO_C_FUNC_LOCATION_S ("EXPANDER"));

		/* notreached */
		return;
	    }

	    IDIO si = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (si)) {
		idio_codegen_error_param_type ("fixnum", si, IDIO_C_FUNC_LOCATION_S ("EXPANDER"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_EXPANDER);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (si));
	}
	break;
    case IDIO_I_CODE_INFIX_OPERATOR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("OPERATOR oi pri m1", mt, IDIO_C_FUNC_LOCATION_S ("INFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO oi = IDIO_PAIR_H (mt);

	    if (! idio_isa_fixnum (oi)) {
		idio_codegen_error_param_type ("fixnum", oi, IDIO_C_FUNC_LOCATION_S ("INFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO pri = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (pri)) {
		idio_codegen_error_param_type ("fixnum", pri, IDIO_C_FUNC_LOCATION_S ("INFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_INFIX_OPERATOR);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (oi));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (pri));
	}
	break;
    case IDIO_I_CODE_POSTFIX_OPERATOR:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 3) {
		idio_codegen_error_param_args ("OPERATOR oi pri m1", mt, IDIO_C_FUNC_LOCATION_S ("POSTFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO oi = IDIO_PAIR_H (mt);
	    if (! idio_isa_fixnum (oi)) {
		idio_codegen_error_param_type ("fixnum", oi, IDIO_C_FUNC_LOCATION_S ("POSTFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO pri = IDIO_PAIR_HT (mt);

	    if (! idio_isa_fixnum (pri)) {
		idio_codegen_error_param_type ("fixnum", pri, IDIO_C_FUNC_LOCATION_S ("POSTFIX-OPERATOR"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_HTT (mt);

	    idio_codegen_compile (thr, ia, eenv, m1, depth + 1);
	    IDIO_IA_PUSH1 (IDIO_A_POSTFIX_OPERATOR);
	    IDIO_IA_PUSH_REF (IDIO_FIXNUM_VAL (oi));
	    IDIO_IA_PUSH_VARUINT (IDIO_FIXNUM_VAL (pri));
	}
	break;
    case IDIO_I_CODE_RETURN:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("RETURN", mt, IDIO_C_FUNC_LOCATION_S ("RETURN"));

		/* notreached */
		return;
	    }

	    IDIO_IA_PUSH1 (IDIO_A_RETURN);
	}
	break;
    case IDIO_I_CODE_FINISH:
	{
	    if (idio_S_nil != mt) {
		idio_codegen_error_param_args ("FINISH", mt, IDIO_C_FUNC_LOCATION_S ("FINISH"));

		/* notreached */
		return;
	    }

	    /*
	     * Wait!  No-one should be encoding a FINISH instruction!
	     */
	    IDIO_C_ASSERT (0);
	    IDIO_IA_PUSH1 (IDIO_A_FINISH);
	}
	break;
    case IDIO_I_CODE_PUSH_ABORT:
	{
	    if (! idio_isa_pair (mt) ||
		idio_list_length (mt) != 1) {
		idio_codegen_error_param_args ("PUSH_ABORT m1", mt, IDIO_C_FUNC_LOCATION_S ("PUSH_ABORT"));

		/* notreached */
		return;
	    }

	    IDIO m1 = IDIO_PAIR_H (mt);

	    IDIO_IA_T ia_m1 = idio_ia (100);
	    idio_codegen_compile (thr, ia_m1, eenv, m1, depth + 1);

	    IDIO_IA_PUSH1 (IDIO_A_PUSH_ABORT);
	    IDIO_IA_PUSH_VARUINT (IDIO_IA_USIZE (ia_m1));

	    idio_ia_append_free (ia, ia_m1);
	}
	break;
    case IDIO_I_CODE_POP_ABORT:
	    IDIO_IA_PUSH1 (IDIO_A_POP_ABORT);
	break;
    case IDIO_I_CODE_NOP:
	    IDIO_IA_PUSH1 (IDIO_A_NOP);
	break;
    default:
	idio_coding_error_C ("bad instruction", mh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	break;
    }
}

void idio_codegen_code_prologue (IDIO_IA_T ia)
{
    /*
     * Out of interest a PC on the stack is indistinguishable from any
     * other (fixnum) number.  However, the particular numbers for
     * idio_vm_{CHR,AR,IHR}_pc should be identifiable because of what
     * precedes them on the stack.
     *
     * In each case you'll see some combination of what part of the
     * VM's state has been preserved.  Where IDIO_A_RESTORE_STATE is
     * used you'd expect to see instances of (originally: ENVIRON_SP,
     * DYNAMIC_SP, TRAP_SP and) FRAME and ENV; and all of those should
     * be preceded by the real PC (and now XI) of the calling
     * function: ie. (originally:
     *
     * <number:PC> <number:ESP> <number:DSP> <number:HSP> <#FRAME>
     * <#module> {CHR,AR}_pc
     *
     * and now)
     *
     * <number:PC> <number:XI> <#FRAME> <#module> {CHR,AR}_pc
     *
     * That's not proof but a useful debugging aid.
     * idio_vm_decode_stack() makes all these assumptions.
     *
     * I've toggled the nominal LiSP first two entries around.  It was
     * NON-CONT-ERROR then FINISH however it transpires that, during
     * development, under $CIRCUMSTANCES the VM chooses to continue
     * processing the current XI/PC rather than having necessarily
     * restored to some original XI/PC.  That used to lead to some
     * unexpected POP-TRAP etc., ie. we've walking into the CHR part
     * of the prologue, which, whilst usually fatal (because there
     * isn't a TRAP on the top of the stack) was a bit head-scratchy.
     * Here, now, at least, we fall over because we shouldn't ever get
     * a NON-CONT-ERROR in normal operation so it's a more obvious
     * clue that we've gone awry.
     *
     * We could have an IDIO_A_I_CANT_DO_THAT_DAVE opcode to catch
     * this specifically but it seems a waste when it's really a
     * (transient?) developer coding issue.
     */

    idio_vm_FINISH_pc = IDIO_IA_USIZE (ia); /* PC == 0 */
    IDIO_IA_PUSH1 (IDIO_A_FINISH);

    idio_vm_NCE_pc = IDIO_IA_USIZE (ia); /* PC == 1 */
    IDIO_IA_PUSH1 (IDIO_A_NON_CONT_ERR);

    idio_vm_CHR_pc = IDIO_IA_USIZE (ia); /* PC == 2 */
    IDIO_IA_PUSH3 (IDIO_A_POP_TRAP, IDIO_A_RESTORE_STATE, IDIO_A_RETURN);

    /*
     * Just the RESTORE_STATE, RETURN for apply
     */
    idio_vm_AR_pc = IDIO_IA_USIZE (ia) - 2; /* PC == 3 */

    /*
     * Just the RETURN for, uh, RETURN
     */
    idio_vm_RETURN_pc = IDIO_IA_USIZE (ia) - 1; /* PC == 4 */

    idio_vm_IHR_pc = IDIO_IA_USIZE (ia); /* PC == 5 */
    IDIO_IA_PUSH3 (IDIO_A_POP_TRAP, IDIO_A_RESTORE_ALL_STATE, IDIO_A_RETURN);
}

idio_pc_t idio_codegen (IDIO thr, IDIO m, IDIO eenv)
{
    IDIO_ASSERT (thr);
    IDIO_ASSERT (m);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (thread, thr);
    IDIO_TYPE_ASSERT (pair, m);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO CTP_bc = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_BYTE_CODE);
    if (idio_CSI_idio_ia_s != IDIO_C_TYPE_POINTER_PTYPE (CTP_bc)) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_exp ("codegen", "byte-code", CTP_bc, "struct-idio-ia-s", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }
    IDIO_IA_T byte_code = IDIO_C_TYPE_POINTER_P (CTP_bc);

    idio_pc_t PC0 = IDIO_IA_USIZE (byte_code);

    IDIO_IA_T ia = idio_ia (1024);

    idio_codegen_compile (thr, ia, eenv, m, 0);

    if (0 == IDIO_IA_USIZE (ia)) {
	fprintf (stderr, "codegen: no code in m's %zu entries => NOP\n", idio_list_length (m));
	idio_ia_push (ia, IDIO_A_NOP);
    }

    idio_ia_append_free (byte_code, ia);

    idio_struct_instance_set_direct (eenv,
				     IDIO_EENV_ST_PCS,
				     idio_pair (idio_fixnum (PC0),
						idio_struct_instance_ref_direct (eenv,
										 IDIO_EENV_ST_PCS)));

    return PC0;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("codegen", codegen, (IDIO m, IDIO args), "m [eenv]", "\
Generate the code for `m` using `eenv`		\n\
						\n\
:param m: evaluation meaning			\n\
:type m: list					\n\
:param eenv: evaluation environment		\n\
:type eenv: tuple, optional			\n\
:return: PC of start of code			\n\
")
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (list, m);
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO eenv = idio_S_nil;

    if (idio_isa_pair (args)) {
	eenv = IDIO_PAIR_H (args);

	IDIO_USER_TYPE_ASSERT (struct_instance, eenv);
    }

    if (idio_S_nil == eenv) {
	eenv = idio_default_eenv;
    }

    IDIO thr = idio_thread_current_thread ();

    idio_pc_t PC0 = idio_codegen (thr, m, eenv);

    /*
     * When some Idio code calls {codegen} then we are already *in*
     * idio_vm_run() which means that we don't enter idio_vm_run() to
     * have a RETURN added to the code
     */
    IDIO CTP_bc = idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_BYTE_CODE);
    if (idio_CSI_idio_ia_s != IDIO_C_TYPE_POINTER_PTYPE (CTP_bc)) {
	/*
	 * Test Case: ??
	 */
	idio_error_param_value_exp ("codegen", "byte-code", CTP_bc, "struct-idio-ia-s", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_fixnum (PC0);
}

char *idio_constant_i_code_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    char *r = NULL;
    char *t;

    intptr_t C_v = IDIO_CONSTANT_I_CODE_VAL (v);

    char m[BUFSIZ];

    switch (C_v) {

    case IDIO_I_CODE_SHALLOW_ARGUMENT_REF:     t = "I-SHALLOW-ARGUMENT-REF";     break;
    case IDIO_I_CODE_DEEP_ARGUMENT_REF:        t = "I-DEEP-ARGUMENT-REF";        break;

    case IDIO_I_CODE_SHALLOW_ARGUMENT_SET:     t = "I-SHALLOW-ARGUMENT-SET";     break;
    case IDIO_I_CODE_DEEP_ARGUMENT_SET:        t = "I-DEEP-ARGUMENT-SET";        break;

    case IDIO_I_CODE_SYM_REF:                  t = "I-SYM-REF";                  break;
    case IDIO_I_CODE_FUNCTION_SYM_REF:         t = "I-FUNCTION-SYM-REF";         break;
    case IDIO_I_CODE_CONSTANT_REF:             t = "I-CONSTANT";                 break;
    case IDIO_I_CODE_COMPUTED_SYM_REF:         t = "I-COMPUTED-SYM-REF";         break;

    case IDIO_I_CODE_SYM_DEF:                  t = "I-SYM-DEF";                  break;
    case IDIO_I_CODE_SYM_SET:                  t = "I-SYM-SET";                  break;
    case IDIO_I_CODE_COMPUTED_SYM_SET:         t = "I-COMPUTED-SYM-SET";         break;
    case IDIO_I_CODE_COMPUTED_SYM_DEF:         t = "I-COMPUTED-SYM-DEF";         break;

    case IDIO_I_CODE_VAL_REF:                  t = "I-VAL-REF";                  break;
    case IDIO_I_CODE_FUNCTION_VAL_REF:         t = "I-FUNCTION-VAL-REF";         break;

    case IDIO_I_CODE_VAL_SET:                  t = "I-VAL-SET";                  break;

    case IDIO_I_CODE_PREDEFINED:               t = "I-PREDEFINED";               break;
    case IDIO_I_CODE_ALTERNATIVE:              t = "I-ALTERNATIVE";              break;
    case IDIO_I_CODE_SEQUENCE:                 t = "I-SEQUENCE";                 break;
    case IDIO_I_CODE_TR_FIX_LET:               t = "I-TR-FIX-LET";               break;
    case IDIO_I_CODE_FIX_LET:                  t = "I-FIX-LET";                  break;
    case IDIO_I_CODE_LOCAL:                    t = "I-LOCAL";                    break;

    case IDIO_I_CODE_PRIMCALL0:                t = "I-PRIMCALL0";                break;
    case IDIO_I_CODE_PRIMCALL1:                t = "I-PRIMCALL1";                break;
    case IDIO_I_CODE_PRIMCALL2:                t = "I-PRIMCALL2";                break;
    case IDIO_I_CODE_TR_REGULAR_CALL:          t = "I-TR-REGULAR-CALL";          break;
    case IDIO_I_CODE_REGULAR_CALL:             t = "I-REGULAR-CALL";             break;

    case IDIO_I_CODE_FIX_CLOSURE:              t = "I-FIX-CLOSURE";              break;
    case IDIO_I_CODE_NARY_CLOSURE:             t = "I-NARY-CLOSURE";             break;

    case IDIO_I_CODE_STORE_ARGUMENT:           t = "I-STORE-ARGUMENT";           break;
    case IDIO_I_CODE_LIST_ARGUMENT:            t = "I-LIST-ARGUMENT";            break;

    case IDIO_I_CODE_ALLOCATE_FRAME:           t = "I-ALLOCATE-FRAME";           break;
    case IDIO_I_CODE_ALLOCATE_DOTTED_FRAME:    t = "I-ALLOCATE-DOTTED-FRAME";    break;
    case IDIO_I_CODE_REUSE_FRAME:              t = "I-REUSE-FRAME";              break;

    case IDIO_I_CODE_DYNAMIC_SYM_REF:          t = "I-DYNAMIC-SYM-REF";          break;
    case IDIO_I_CODE_DYNAMIC_FUNCTION_SYM_REF: t = "I-DYNAMIC-FUNCTION-SYM-REF"; break;
    case IDIO_I_CODE_PUSH_DYNAMIC:             t = "I-PUSH-DYNAMIC";             break;
    case IDIO_I_CODE_POP_DYNAMIC:              t = "I-POP-DYNAMIC";              break;

    case IDIO_I_CODE_ENVIRON_SYM_REF:          t = "I-ENVIRON-SYM-REF";          break;
    case IDIO_I_CODE_PUSH_ENVIRON:             t = "I-PUSH-ENVIRON";             break;
    case IDIO_I_CODE_POP_ENVIRON:              t = "I-POP-ENVIRON";              break;

    case IDIO_I_CODE_PUSH_TRAP:                t = "I-PUSH-TRAP";                break;
    case IDIO_I_CODE_POP_TRAP:                 t = "I-POP-TRAP";                 break;
    case IDIO_I_CODE_PUSH_ESCAPER:             t = "I-PUSH-ESCAPER";             break;
    case IDIO_I_CODE_POP_ESCAPER:              t = "I-POP-ESCAPER";              break;
    case IDIO_I_CODE_ESCAPER_LABEL_REF:        t = "I-ESCAPER-LABEL-REF";        break;

    case IDIO_I_CODE_AND:                      t = "I-AND";                      break;
    case IDIO_I_CODE_OR:                       t = "I-OR";                       break;
    case IDIO_I_CODE_BEGIN:                    t = "I-BEGIN";                    break;
    case IDIO_I_CODE_NOT:                      t = "I-NOT";                      break;

    case IDIO_I_CODE_EXPANDER:                 t = "I-EXPANDER";                 break;
    case IDIO_I_CODE_INFIX_OPERATOR:           t = "I-INFIX-OPERATOR";           break;
    case IDIO_I_CODE_POSTFIX_OPERATOR:         t = "I-POSTFIX-OPERATOR";         break;

    case IDIO_I_CODE_RETURN:                   t = "I-RETURN";                   break;
    case IDIO_I_CODE_FINISH:                   t = "I-FINISH";                   break;
    case IDIO_I_CODE_PUSH_ABORT:               t = "I-PUSH-ABORT";               break;
    case IDIO_I_CODE_POP_ABORT:                t = "I-POP-ABORT";                break;
    case IDIO_I_CODE_NOP:                      t = "I-NOP";                      break;

    default:
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There should be a case
	 * clause above.
	 */
	idio_snprintf (m, BUFSIZ, "#<type/constant/i_code?? o=%10p VAL(o)=%td>", v, C_v);
	idio_error_warning_message ("unhandled I-CODE: %s\n", m);
	t = m;
	break;
    }

    if (NULL == t) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There should be a case
	 * clause above.
	 */
	*sizep = idio_asprintf (&r, "#<I-CODE? %10p>", v);
    } else {
	*sizep = idio_asprintf (&r, "%s", t);
    }

    return r;
}

IDIO idio_constant_i_code_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_constant_i_code_as_C_string (v, sizep, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

typedef struct idio_codegen_symbol_s {
    char const *name;
    IDIO value;
} idio_codegen_symbol_t;

static idio_codegen_symbol_t idio_codegen_symbols[] = {
    { "I-SHALLOW-ARGUMENT-REF",     IDIO_I_SHALLOW_ARGUMENT_REF },
    { "I-DEEP-ARGUMENT-REF",        IDIO_I_DEEP_ARGUMENT_REF },

    { "I-SHALLOW-ARGUMENT-SET",     IDIO_I_SHALLOW_ARGUMENT_SET },
    { "I-DEEP-ARGUMENT-SET",        IDIO_I_DEEP_ARGUMENT_SET },

    { "I-SYM-REF",                  IDIO_I_SYM_REF },
    { "I-FUNCTION-SYM-REF",         IDIO_I_FUNCTION_SYM_REF },
    { "I-CONSTANT-REF",             IDIO_I_CONSTANT_REF },
    { "I-COMPUTED-SYM-REF",         IDIO_I_COMPUTED_SYM_REF },

    { "I-SYM-DEF",                  IDIO_I_SYM_DEF },
    { "I-SYM-SET",                  IDIO_I_SYM_SET },
    { "I-COMPUTED-SYM-SET",         IDIO_I_COMPUTED_SYM_SET },
    { "I-COMPUTED-SYM-DEF",         IDIO_I_COMPUTED_SYM_DEF },

    { "I-VAL-REF",                  IDIO_I_VAL_REF },
    { "I-FUNCTION-VAL-REF",         IDIO_I_FUNCTION_VAL_REF },

    { "I-VAL-SET",                  IDIO_I_VAL_SET },

    { "I-PREDEFINED",               IDIO_I_PREDEFINED },
    { "I-ALTERNATIVE",              IDIO_I_ALTERNATIVE },
    { "I-SEQUENCE",                 IDIO_I_SEQUENCE },
    { "I-TR-FIX-LET",               IDIO_I_TR_FIX_LET },
    { "I-FIX-LET",                  IDIO_I_FIX_LET },
    { "I-LOCAL",                    IDIO_I_LOCAL },

    { "I-PRIMCALL0",                IDIO_I_PRIMCALL0 },
    { "I-PRIMCALL1",                IDIO_I_PRIMCALL1 },
    { "I-PRIMCALL2",                IDIO_I_PRIMCALL2 },
    { "I-TR-REGULAR-CALL",          IDIO_I_TR_REGULAR_CALL },
    { "I-REGULAR-CALL",             IDIO_I_REGULAR_CALL },

    { "I-FIX-CLOSURE",              IDIO_I_FIX_CLOSURE },
    { "I-NARY-CLOSURE",             IDIO_I_NARY_CLOSURE },

    { "I-STORE-ARGUMENT",           IDIO_I_STORE_ARGUMENT },
    { "I-LIST-ARGUMENT",            IDIO_I_LIST_ARGUMENT },
    { "I-ALLOCATE-FRAME",           IDIO_I_ALLOCATE_FRAME },
    { "I-ALLOCATE-DOTTED-FRAME",    IDIO_I_ALLOCATE_DOTTED_FRAME },
    { "I-REUSE-FRAME",              IDIO_I_REUSE_FRAME },

    { "I-PUSH-DYNAMIC",             IDIO_I_PUSH_DYNAMIC },
    { "I-POP-DYNAMIC",              IDIO_I_POP_DYNAMIC },
    { "I-DYNAMIC-SYM-REF",          IDIO_I_DYNAMIC_SYM_REF },
    { "I-DYNAMIC-FUNCTION-SYM-REF", IDIO_I_DYNAMIC_FUNCTION_SYM_REF },

    { "I-PUSH-ENVIRON",             IDIO_I_PUSH_ENVIRON },
    { "I-POP-ENVIRON",              IDIO_I_POP_ENVIRON },
    { "I-ENVIRON-SYM-REF",          IDIO_I_ENVIRON_SYM_REF },

    { "I-PUSH-TRAP",                IDIO_I_PUSH_TRAP },
    { "I-POP-TRAP",                 IDIO_I_POP_TRAP },

    { "I-PUSH-ESCAPER",             IDIO_I_PUSH_ESCAPER },
    { "I-POP-ESCAPER",              IDIO_I_POP_ESCAPER },
    { "I-ESCAPER-LABEL-REF",        IDIO_I_ESCAPER_LABEL_REF },

    { "I-AND",                      IDIO_I_AND },
    { "I-OR",                       IDIO_I_OR },
    { "I-BEGIN",                    IDIO_I_BEGIN },
    { "I-NOT",                      IDIO_I_NOT },

    { "I-EXPANDER",                 IDIO_I_EXPANDER },
    { "I-INFIX-OPERATOR",           IDIO_I_INFIX_OPERATOR },
    { "I-POSTFIX-OPERATOR",         IDIO_I_POSTFIX_OPERATOR },

    { "I-RETURN",                   IDIO_I_RETURN },
    { "I-FINISH",                   IDIO_I_FINISH },
    { "I-PUSH-ABORT",               IDIO_I_PUSH_ABORT },
    { "I-POP-ABORT",                IDIO_I_POP_ABORT },
    { "I-NOP",                      IDIO_I_NOP },

    { NULL, NULL }
};

void idio_codegen_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_codegen_module, idio_ia_length);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_codegen_module, idio_ia2string);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_codegen_module, codegen_constants_lookup_or_extend);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_codegen_module, codegen);
}

void idio_init_codegen ()
{
    idio_module_table_register (idio_codegen_add_primitives, NULL, NULL);

    idio_codegen_module = idio_module (IDIO_SYMBOL ("codegen"));

    idio_codegen_symbol_t *cs = idio_codegen_symbols;
    for (; cs->name != NULL; cs++) {
	/*
	 * The longest existing cs->name is 33 chars so the strnlen
	 * (..., 40) magic number gives us a bit of future leeway.
	 *
	 * strnlen rather than idio_strnlen during bootstrap.
	 */
	IDIO sym = idio_symbols_C_intern (cs->name, strnlen (cs->name, 40));
	idio_module_export_symbol_value (sym, cs->value, idio_codegen_module);
    }

    idio_vtable_t *ci_vt = idio_vtable (IDIO_TYPE_CONSTANT_I_CODE);

    idio_vtable_add_method (ci_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_constant_i_code));

    idio_vtable_add_method (ci_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_constant_i_code_method_2string));

    IDIO_C_STRUCT_IDENT_DEF (IDIO_SYMBOL ("struct-idio-ia-s"), idio_S_nil, idio_ia_s, idio_fixnum0);
}
