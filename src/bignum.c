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
 * bignum.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "bignum.h"
#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

size_t idio_bignums = 0;
size_t idio_bignums_max = 0;
size_t idio_bignum_seg_max = 0;

/*
 * Code coverage:
 *
 * No Idio code can reach the calls to this function.  Requires some C
 * unit tests.
 */
static void idio_bignum_error (char const *msg, const IDIO bn, const IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_bignum_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       bn));

    /* notreached */
}

static void idio_bignum_conversion_error (char const *msg, const IDIO bn, const IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_bignum_conversion_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       bn));

    /* notreached */
}

static void idio_bignum_error_divide_by_zero (IDIO nums, IDIO c_location)
{
    IDIO_ASSERT (nums);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_divide_by_zero ("bignum divide by zero", nums, c_location);
}

IDIO_BSA idio_bsa (size_t const n0)
{
    size_t n = n0;

    if (n <= 0) {
	/*
	 * Code coverage: C unit test??
	 */
	n = IDIO_BIGNUM_SIG_SEGMENTS;
    }

    if (n > idio_bignum_seg_max) {
	idio_bignum_seg_max = n;
    }

    IDIO_BSA bsa = idio_alloc (sizeof (idio_bsa_t));
    bsa->ae = idio_alloc (n * sizeof (IDIO_BS_T));
    bsa->avail = n;
    bsa->size = n;
    bsa->refs = 0;

    size_t i;
    for (i = 0; i < bsa->size ; i++) {
	bsa->ae[i] = 0;
    }

    idio_bignums++;
    if (idio_bignums > idio_bignums_max) {
	idio_bignums_max = idio_bignums;
    }

    return bsa;
}

void idio_bsa_free (IDIO_BSA bsa)
{
    if (bsa->refs > 1) {
	bsa->refs--;
    } else {
	idio_free (bsa->ae);
	idio_free (bsa);
	idio_bignums--;
    }
}

static void idio_bsa_resize_by (IDIO_BSA bsa, size_t const n)
{
    bsa->size += n;
    if (bsa->size > bsa->avail) {
	bsa->ae = idio_realloc (bsa->ae, bsa->size * sizeof (IDIO_BS_T));
	bsa->avail = bsa->size;
    }

    IDIO_C_ASSERT (bsa->size < 200); /* reading pi with 61 sig digits */
}

IDIO_BS_T idio_bsa_get (IDIO_BSA bsa, size_t const i)
{
    if (i >= bsa->size) {
	/*
	 * Test Case: ??
	 *
	 * Requires bad developer code.
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "bignum significand array access OOB: get %zd/%zd", i, bsa->size);

	idio_bignum_error (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    return bsa->ae[i];
}

void idio_bsa_set (IDIO_BSA bsa, IDIO_BS_T v, size_t const i)
{
    if (i >= bsa->size) {
	/* one beyond the current usage is OK */
	if (bsa->size == i) {
	    idio_bsa_resize_by (bsa, 1);
	} else {
	    /*
	     * Test Case: ??
	     *
	     * Requires bad developer code.
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "bignum significand array access OOB: set %zd/%zd", i, bsa->size);

	    idio_bignum_error (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    bsa->ae[i] = v;
}

void idio_bsa_shift (IDIO_BSA bsa)
{
    if (bsa->size) {
	size_t i;
	for (i = 1; i < bsa->size ; i++) {
	    bsa->ae[i-1] = bsa->ae[i];
	}
	bsa->size--;
    } else {
	/*
	 * Test Case: ??
	 *
	 * Requires bad developer code.
	 */
	idio_bignum_error ("bignum significand shift: zero length already", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
}

void idio_bsa_pop (IDIO_BSA bsa)
{
    if (bsa->size) {
	bsa->size--;
    } else {
	/*
	 * Test Case: ??
	 *
	 * Requires bad developer code.
	 */
	idio_bignum_error ("bignum significand pop: zero length already", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
}

IDIO_BSA idio_bsa_copy (IDIO_BSA bsa)
{
    IDIO_BSA bsac = idio_bsa (bsa->size);

    size_t i;
    for (i = 0; i < bsa->size; i++) {
	bsac->ae[i] = bsa->ae[i];
    }

    return bsac;
}


void idio_bignum_dump (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);
    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    fprintf (stderr, "idio_bignum_dump: ");
    if (IDIO_BIGNUM_INTEGER_P (bn)) {
	fprintf (stderr, "I");
    }
    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	fprintf (stderr, "-");
    } else {
	fprintf (stderr, " ");
    }
    if (IDIO_BIGNUM_REAL_P (bn)) {
	fprintf (stderr, "R");
    }
    if (IDIO_BIGNUM_REAL_INEXACT_P (bn)) {
	fprintf (stderr, "i");
    } else {
	fprintf (stderr, "e");
    }
    if (IDIO_BIGNUM_NAN_P (bn)) {
	/*
	 * Code coverage: nothing manipulates NaN (yet?)
	 */
	fprintf (stderr, "!");
    }

    size_t segs = al - 1;
    fprintf (stderr, " segs[%2zd%s]: ", al, (segs > IDIO_BIGNUM_SIG_SEGMENTS) ? "!" : "");
    int first = 1;

    /*
      To make visual comparison of numbers easier, always print out
	IDIO_BIGNUM_SIG_SEGMENTS even if the number doesn't
	have that many.  We can then compare columnally.  Much easier
	on the eye.
     */
    ssize_t ai;
    for (ai = segs; ai >= 0; ai--) {
	char const *fmt;
	if (first) {
	    first = 0;
	    fmt = "%*zd ";
	} else {
	    fmt = "%0*zd ";
	}

	if (IDIO_BSA_AE (sig_a, ai) > IDIO_BIGNUM_INT_SEG_LIMIT) {
	    /*
	     * Code coverage: Hmm, must have put this in for a
	     * reason.
	     */
	    fprintf (stderr, "!");
	}
	fprintf (stderr, fmt, IDIO_BIGNUM_DPW, IDIO_BSA_AE (sig_a, ai));
    }
    fprintf (stderr, "e%" PRId32 "\n", exp);
}

/* bignum code from S9fES */
IDIO idio_bignum (int flags, IDIO_BE_T exp, IDIO_BSA sig_a)
{
    IDIO o = idio_gc_get (IDIO_TYPE_BIGNUM);
    o->vtable = idio_vtable (IDIO_TYPE_BIGNUM);

    IDIO_BIGNUM_FLAGS (o) = flags;
    IDIO_BIGNUM_EXP (o) = exp;
    IDIO_BIGNUM_SIG (o) = sig_a;
    sig_a->refs++;

    return o;
}

int idio_isa_bignum (IDIO bn)
{
    IDIO_ASSERT (bn);

    return idio_isa (bn, IDIO_TYPE_BIGNUM);
}

int idio_isa_integer_bignum (IDIO bn)
{
    IDIO_ASSERT (bn);

    int r = 0;

    if (idio_isa_bignum (bn)) {
	if (IDIO_BIGNUM_INTEGER_P (bn)) {
	    r = 1;
	} else {
	    IDIO i = idio_bignum_real_to_integer (bn);
	    if (idio_S_nil != i) {
		r = 1;
	    }
	}
    }

    return r;
}

void idio_free_bignum (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    idio_bsa_free (IDIO_BIGNUM_SIG (bn));
}

IDIO idio_bignum_copy (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    return idio_bignum (IDIO_BIGNUM_FLAGS (bn), IDIO_BIGNUM_EXP (bn), idio_bsa_copy (IDIO_BIGNUM_SIG (bn)));
}

IDIO idio_bignum_integer_intmax_t (intmax_t i)
{
    IDIO_BSA sig_a = idio_bsa (1);

    int neg = 0;
    int carry = 0;

    if (i < 0) {
	neg = 1;
	if (INTMAX_MIN == i) {
	    carry = 1;
	    i = INTMAX_MAX;
	} else {
	    /*
	     * Code coverage: C unit test?
	     */
	    i = -i;
	}
    }

    size_t ai = 0;
    if (i >= IDIO_BIGNUM_INT_SEG_LIMIT) {
	while (i) {
	    /*
	     * This is slightly uglier than it needs to be.
	     * INTMAX_MIN has no negated value we can represent in an
	     * intmax_t -- hence the {carry} flag.
	     *
	     * We need to retain the modulus {m} (to subtract from {i}
	     * each time round the loop) but calculate a separate {v}
	     * (which will include {carry} when appropriate) to be set
	     * in the sig_a.
	     *
	     * This code is generic but we know that {carry} is only
	     * set for -2**n which is never(?) going to align with
	     * IDIO_BIGNUM_INT_SEG_LIMIT (which is a 10**n).  No
	     * -(2**X)+1 will align with 10**Y so we could simply say
	     * that if {carry} is set, add 1 to {m} and be done.
	     */
	    IDIO_BS_T m = i % IDIO_BIGNUM_INT_SEG_LIMIT;
	    IDIO_BS_T v = m;
	    if (carry) {
		carry = 0;
		v++;
		if (v == IDIO_BIGNUM_INT_SEG_LIMIT) {
		    /*
		     * Code coverage: C unit test?
		     */
		    carry = 1;
		    v = 0;
		}
	    }
	    idio_bsa_set (sig_a, v, ai++);
	    i -= m;
	    i /= IDIO_BIGNUM_INT_SEG_LIMIT;
	}
    } else {
	idio_bsa_set (sig_a, i, ai++);
    }

    if (neg) {
	ai--;
	IDIO_BS_T v = idio_bsa_get (sig_a, ai);
	idio_bsa_set (sig_a, -v, ai);
    }

    return idio_bignum (IDIO_BIGNUM_FLAG_INTEGER, 0, sig_a);
}

/*
 * Code coverage:
 *
 * This is called from idio_uinteger() in fixnum.c which is called
 * from ->integer in c-type.c
 */
IDIO idio_bignum_integer_uintmax_t (uintmax_t ui)
{
    IDIO_BSA sig_a = idio_bsa (1);

    size_t ai = 0;
    if (ui >= IDIO_BIGNUM_INT_SEG_LIMIT) {
	while (ui) {
	    intptr_t m = ui % IDIO_BIGNUM_INT_SEG_LIMIT;
	    idio_bsa_set (sig_a, m, ai++);
	    ui -= m;
	    ui /= IDIO_BIGNUM_INT_SEG_LIMIT;
	}
    } else {
	/*
	 * Code coverage: Not on 64-bit, maybe 32-bit??
	 */
	idio_bsa_set (sig_a, ui, ai++);
    }

    return idio_bignum (IDIO_BIGNUM_FLAG_INTEGER, 0, sig_a);
}

IDIO idio_bignum_integer (IDIO_BSA sig_a)
{
    return idio_bignum (IDIO_BIGNUM_FLAG_INTEGER, 0, sig_a);
}

IDIO idio_bignum_copy_to_integer (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    return idio_bignum_integer (idio_bsa_copy (IDIO_BIGNUM_SIG (bn)));
}

#define IDIO_BIGNUM_C_TYPE_VALUE(T,P,M)					\
    T idio_bignum_ ## T ## _value (IDIO bn)				\
    {									\
	IDIO_ASSERT (bn);						\
	IDIO_TYPE_ASSERT (bignum, bn);					\
	IDIO bn_i = idio_bignum_integer_argument (bn);			\
	if (idio_S_nil == bn_i) {					\
	    return 0;							\
	}								\
	IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);			\
	ssize_t ai = IDIO_BSA_SIZE (sig_a);				\
	IDIO_C_ASSERT (ai);						\
	if (ai > 1) {							\
	    IDIO fn = idio_bignum_to_fixnum (bn_i);			\
	    if (bn_i == fn) {						\
		IDIO_BS_T a1 = idio_bsa_get (sig_a, ai - 1);		\
		if (ai < IDIO_BIGNUM_ ## T ## _WORDS ||			\
		    (ai == IDIO_BIGNUM_ ## T ## _WORDS &&		\
		     (a1 <= IDIO_BIGNUM_ ## T ## _FIRST &&		\
		      a1 >= -IDIO_BIGNUM_ ## T ## _FIRST))) {		\
		    int neg = (a1 < 0);					\
		    T v = a1;						\
		    if (a1 < 0) {					\
			v = -a1;					\
		    }							\
		    for (ai--; ai > 0 ; ai--) {				\
			v *= IDIO_BIGNUM_INT_SEG_LIMIT;			\
			T sig = idio_bsa_get (sig_a, ai - 1);		\
			v += sig;					\
		    }							\
		    if (neg) {						\
			v = -v;						\
		    }							\
		    if ((a1 < 0 &&					\
			 v < 0) ||					\
			(a1 >= 0 &&					\
			 v >= 0)) {					\
			return v;					\
		    }							\
		}							\
		size_t size = 0;					\
		char *bn_is = idio_bignum_as_string (bn_i, &size);	\
		char em[BUFSIZ];					\
		idio_snprintf (em, BUFSIZ, "%s is too large for " #T " (%" P ")", bn_is, M); \
		IDIO_GC_FREE (bn_is, size);				\
		idio_bignum_conversion_error (em, bn, IDIO_C_FUNC_LOCATION ());	\
		return -1;						\
	    } else {							\
		return IDIO_FIXNUM_VAL (fn);				\
	    }								\
	}								\
	return (T) idio_bsa_get (sig_a, ai - 1);			\
    }

#define IDIO_BIGNUM_C_TYPE_UVALUE(T,P,M)				\
    T idio_bignum_ ## T ## _value (IDIO bn)				\
    {									\
	IDIO_ASSERT (bn);						\
	IDIO_TYPE_ASSERT (bignum, bn);					\
	IDIO bn_i = idio_bignum_integer_argument (bn);			\
	if (idio_S_nil == bn_i) {					\
	    return 0;							\
	}								\
	IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);			\
	ssize_t ai = IDIO_BSA_SIZE (sig_a);				\
	IDIO_C_ASSERT (ai);						\
	if (ai > 1) {							\
	    IDIO fn = idio_bignum_to_fixnum (bn_i);			\
	    if (bn_i == fn) {						\
		IDIO_BS_T a1 = idio_bsa_get (sig_a, ai - 1);		\
		if (ai < IDIO_BIGNUM_ ## T ## _WORDS ||			\
		    (ai == IDIO_BIGNUM_ ## T ## _WORDS &&		\
		     (a1 <= IDIO_BIGNUM_ ## T ## _FIRST &&		\
		      a1 >= 0))) {					\
		    T v = a1;						\
		    for (ai--; ai > 0 ; ai--) {				\
			v *= IDIO_BIGNUM_INT_SEG_LIMIT;			\
			T sig = idio_bsa_get (sig_a, ai - 1);		\
			v += sig;					\
		    }							\
		    return v;						\
		}							\
		size_t size = 0;					\
		char *bn_is = idio_bignum_as_string (bn_i, &size);	\
		char em[BUFSIZ];					\
		idio_snprintf (em, BUFSIZ, "%s is too large for " #T " (%" P ")", bn_is, M); \
		IDIO_GC_FREE (bn_is, size);				\
		idio_bignum_conversion_error (em, bn, IDIO_C_FUNC_LOCATION ());	\
		return -1;						\
	    } else {							\
		return IDIO_FIXNUM_VAL (fn);				\
	    }								\
	}								\
	return (T) idio_bsa_get (sig_a, ai - 1);			\
    }

IDIO_BIGNUM_C_TYPE_VALUE  (int64_t,   PRId64,  INT64_MAX)
IDIO_BIGNUM_C_TYPE_UVALUE (uint64_t,  PRIu64,  UINT64_MAX)
IDIO_BIGNUM_C_TYPE_VALUE  (ptrdiff_t, PRIdPTR, PTRDIFF_MAX)
IDIO_BIGNUM_C_TYPE_VALUE  (intptr_t,  PRIdPTR, INTPTR_MAX)
IDIO_BIGNUM_C_TYPE_VALUE  (intmax_t,  PRIdMAX, INTMAX_MAX)
IDIO_BIGNUM_C_TYPE_UVALUE (uintmax_t, PRIuMAX, UINTMAX_MAX)

/*
 * bignums from C floating point
 *
 * IEEE 754 is hard and there's plenty of information on the
 * Intertubes to help you come to that conclusion.
 *
 * Ultimately, what we want is to derive the base-10 significant
 * figures and base-10 exponent from a base-2 floating number in order
 * that we can construct a bignum.
 *
 * I'm not smart enough to do that and looking at printf_fp.c from
 * glibc I'm not about to get all GMP'd up here either.
 *
 * Of interest, in
 * https://randomascii.wordpress.com/2012/03/08/float-precisionfrom-zero-to-100-digits-2/,
 * Bruce Dawson notes that a C float can be reworked as a 128.149
 * bitfield: 128 bits before the binary point -- because of the
 * possible +127 exponent and the implied leading 1; and 149 bits
 * after the binary point -- because of the possible -126 exponent
 * plus the 23 post-point bits in the mantissa anyway.
 *
 * He's trying to generate the complete printed decimal string (which
 * can run to over 100 digits) whereas we are only looking for the
 * IDIO_BIGNUM_SIG_MAX_DIGITS.
 *
 * We don't have arbitrary bit-length objects to do maths on either
 * (although, coincidentally, we have arbitrary length
 * coded-decimals).
 *
 * Ultimately, whatever we do we're going to end up indirecting
 * through a string of some kind so in deference to my limited
 * abilities let's combine an efficient IEEE 754 to string
 * implementation from libc together with our own trusty bignum reader
 * code.
 *
 * It's not Art but it gets us out of an implementation pickle.
 *
 * Of note, the precision used here, IDIO_BIGNUM_SIG_MAX_DIGITS - 1,
 * is because with %e there is always an additional digit before the
 * decimal point.  Avoiding a extra segment prevents the normalisation
 * of the number becoming inexact.
 *
 * You will immediately discover the frailties of IEEE 754:
 *
 * C/->number (C/number-> 123.456 'float)
 * 1.23456001281738281e+2
 *
 * The complete value is 123.45600128173828125 as seen at
 * https://www.h-schmidt.net/FloatConverter/IEEE754.html and others.
 * We, of course, have rounded to IDIO_BIGNUM_SIG_MAX_DIGITS digits.
 */
IDIO idio_bignum_float (float f)
{
    idio_C_float_ULP_t uf;
    uf.f = f;

    if (255 == uf.parts.exponent) {
	if (uf.parts.mantissa) {
	    char em[30];
	    idio_snprintf (em, 30, "NaN %07x non-special float", uf.parts.mantissa);

	    idio_error_param_value_msg_only ("C/->number", "i", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    char em[30];
	    idio_snprintf (em, 30, "%cinf non-special float", uf.parts.sign ? '-' : '+');

	    idio_error_param_value_msg_only ("C/->number", "i", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    char fs[30];
    size_t fs_len = idio_snprintf (fs, 30, "%.*e", IDIO_BIGNUM_SIG_MAX_DIGITS - 1, f);

    return idio_bignum_real_C (fs, fs_len);
}

IDIO idio_bignum_double (double d)
{
    idio_C_double_ULP_t ud;
    ud.d = d;

    if (2047 == ud.parts.exponent) {
	if (ud.parts.mantissa) {
	    char em[60];
	    idio_snprintf (em, 60, "NaN %016llx non-special double", (unsigned long long int) ud.parts.mantissa);

	    idio_error_param_value_msg_only ("C/->number", "i", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    char em[60];
	    idio_snprintf (em, 60, "%cinf non-special double", ud.parts.sign ? '-' : '+');

	    idio_error_param_value_msg_only ("C/->number", "i", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    char fs[30];
    size_t fs_len = idio_snprintf (fs, 30, "%.*le", IDIO_BIGNUM_SIG_MAX_DIGITS - 1, d);

    return idio_bignum_real_C (fs, fs_len);
}

IDIO idio_bignum_longdouble (long double ld)
{
    /*
     * Test Case: ??
     *
     * The call from ->number is guarded by a similar message
     */
    idio_error_param_type_msg ("bignum from C long double is not supported", IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
    idio_C_longdouble_ULP_t uld;
    uld.ld = ld;

    /*
     * XXX How do I know this is a 96bit and not an 80bit long double?
     */
    if (32767 == uld.parts_96bit.exponent) {
	if (uld.parts_96bit.mantissa) {
	    char em[60];
	    idio_snprintf (em, 60, "NaN %020llx non-special long double", uld.parts_96bit.mantissa);

	    idio_error_param_value_msg_only ("C/->number", "i", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else {
	    char em[60];
	    idio_snprintf (em, 60, "%cinf non-special long double", uld.parts_96bit.sign ? '-' : '+');

	    idio_error_param_value_msg_only ("C/->number", "i", em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    char fs[30];
    size_t fs_len = idio_snprintf (fs, 30, "%.*Le", IDIO_BIGNUM_SIG_MAX_DIGITS - 1, ld);

    return idio_bignum_real_C (fs, fs_len);
}

/*
 * Until there's a pressing need I'm extremely reluctant to load in
 * math.h and libm just for pow().
 *
 * wikipedia suggests
 * https://en.wikipedia.org/wiki/Exponentiation_by_squaring
 *
 * This is the iterative version
 */
double idio_bignum_pow (double x, IDIO_BE_T y)
{
    if (y < 0) {
	x = 1 / x;
	y = -y;
    }
    if (0 == y) {
	return 1;
    }
    double n = 1;
    while (y > 1) {
	if (y % 2) {
	    n = x * n;
	    x = x * x;
	    y = (y - 1) / 2;
	} else {
	    x = x * x;
	    y = y / 2;
	}
    }

    return x * n;
}

float idio_bignum_float_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    float r = 0;

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    for (; al > 0 ; al--) {
	/*
	 * clang warns: implicit conversion from 'long long' to
	 * 'float' changes value from 1000000000000000000 to
	 * 999999984306749440
	 *
	 * Those numbers don't look The Same which casting to float
	 * only hides.
	 *
	 * gcc-11 doesn't warn, mind.
	 */
	r *= (float) IDIO_BIGNUM_INT_SEG_LIMIT;
	if (r < 0) {
	    r -= idio_bsa_get (sig_a, al - 1);
	} else {
	    r += idio_bsa_get (sig_a, al - 1);
	}
    }

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);

    /*
     * We should range check at this point (to avoid pow()) for
     * extreme exp as the chances are we're using IEEE 754 floating
     * point and those have exponent limits (2^+/-126 or roughly
     * 10^+/-38)
     */
    if (exp > 38 ||
	exp < -38) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "C float range conversion issue pending? exp=%" PRId32 " exceeds (+/-38)\n", exp);
#endif
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	r = -r;
    }

    return r * (float) idio_bignum_pow (10, exp);
}

double idio_bignum_double_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    double r = 0;

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    for (; al > 0 ; al--) {
	r *= IDIO_BIGNUM_INT_SEG_LIMIT;
	if (r < 0) {
	    r -= idio_bsa_get (sig_a, al - 1);
	} else {
	    r += idio_bsa_get (sig_a, al - 1);
	}
    }

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);

    /*
     * We should range check at this point (to avoid pow()) for
     * extreme exp as the chances are we're using IEEE 754 floating
     * point and those have exponent limits (2^+/-1022 or roughly
     * 10^+/-308)
     */
    if (exp > 308 ||
	exp < -308) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "double range conversion issue pending? exp=%" PRId32 " exceeds (+/-308)\n", exp);
#endif
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	r = -r;
    }

    return r * idio_bignum_pow (10, exp);
}

long double idio_bignum_longdouble_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    long double r = 0;

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    for (; al > 0 ; al--) {
	r *= IDIO_BIGNUM_INT_SEG_LIMIT;
	if (r < 0) {
	    r -= idio_bsa_get (sig_a, al - 1);
	} else {
	    r += idio_bsa_get (sig_a, al - 1);
	}
    }

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);

    /*
     * We should range check at this point (to avoid pow()) for
     * extreme exp as the chances are we're using IEEE 754 floating
     * point and those have exponent limits (2^+/-16382 or roughly
     * 10^+/-4934)
     */
    if (exp > 4934 ||
	exp < -4934) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "long double range conversion issue pending? exp=%" PRId32 " exceeds (+/-4934)\n", exp);
#endif
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	r = -r;
    }

    return r * idio_bignum_pow (10, exp);
}


/*
 * The original interface returned #n on failure but as most uses
 * promptly tested for #n and then used the original {bn} parameter
 * let's pivot to do that by default.
 *
 * Those that do care can use an identity check to see if the returned
 * value is the original and do whatever.
 */
IDIO idio_bignum_to_fixnum (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    if (! IDIO_BIGNUM_INTEGER_P (bn)) {
	return bn;
    }

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	/*
	 * Code coverage: C unit test??
	 */
	return bn;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    size_t segs = al - 1;
    IDIO_BS_T a1 = idio_bsa_get (sig_a, segs);

    if (al > 1) {
	if (al > IDIO_BIGNUM_fixnum_WORDS) {
	    return bn;
	} else if (al == IDIO_BIGNUM_fixnum_WORDS) {
	    if (a1 > IDIO_BIGNUM_fixnum_FIRST ||
		a1 < -IDIO_BIGNUM_fixnum_FIRST) {
		return bn;
	    }
	}
    }

    /*
     * Around the loop, iv is treated as positive and will be negated,
     * if required, afterwards.
     *
     * That means we can use the previous value, piv, to check if the
     * addition of the segment value, sv, has overflowed.
     */
    intptr_t iv = 0;
    int ivnz = 0;
    intptr_t piv = 0;
    int neg = 0;

    ssize_t ai;
    for (ai = segs; ai >= 0 ; ai--) {
	iv *= IDIO_BIGNUM_INT_SEG_LIMIT;
	piv = iv;
	IDIO_BS_T sv = idio_bsa_get (sig_a, ai);
	if (sv < 0) {
	    IDIO_C_ASSERT (ai == (ssize_t) segs);
	    iv += -sv;
	    neg = 1;
	} else {
	    iv += sv;
	}

	if (iv < piv) {
	    return bn;
	}

	if (ivnz &&
	    0 == iv) {
	    return bn;
	}

	if (iv) {
	    ivnz = 1;
	}
    }

    if (neg) {
	iv = -iv;
    }

    if (iv <= IDIO_FIXNUM_MAX &&
	iv >= IDIO_FIXNUM_MIN) {
	idio_gc_stats_inc (IDIO_TYPE_FIXNUM);
	return idio_fixnum (iv);
    }

    /*
     * Code coverage: C unit test??
     */
    return bn;
}

IDIO idio_bignum_abs (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bnc = idio_bignum_copy (bn);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bnc);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    size_t segs = al - 1;

    IDIO_BS_T i = idio_bsa_get (sig_a, segs);
    idio_bsa_set (sig_a, llabs (i), segs);

    return bnc;
}

int idio_bignum_negative_p (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    IDIO_BS_T i = idio_bsa_get (sig_a, al - 1);

    return (i < 0);
}

IDIO idio_bignum_negate (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bnc = idio_bignum_copy (bn);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bnc);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    size_t segs = al - 1;
    IDIO_BS_T i = idio_bsa_get (sig_a, segs);
    idio_bsa_set (sig_a, -i, segs);

    return bnc;
}

IDIO idio_bignum_add (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    /* we want to avoid operations with negative numbers */
    if (idio_bignum_negative_p (a)) {
	if (idio_bignum_negative_p (b)) {
	    /* -a + -b => -(|a| + |b|) */
	    a = idio_bignum_abs (a);
	    b = idio_bignum_abs (b);

	    IDIO r = idio_bignum_add (a, b);

	    return idio_bignum_negate (r);
	} else {
	    /* -a + b => b - |a| */
	    a = idio_bignum_abs (a);

	    return idio_bignum_subtract (b, a);
	}
    } else if (idio_bignum_negative_p (b)) {
	/* a + -b => a - |b| */
	b = idio_bignum_abs (b);

	return idio_bignum_subtract (a, b);
    }

    /* regular a + b */
    size_t al = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (a));
    size_t bl = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (b));

    int carry = 0;
    size_t rl;

    if (al >= bl) {
	rl = al;
    } else {
	rl = bl;
    }

    IDIO_BSA ra = idio_bsa (rl);

    size_t ai = 0;
    size_t bi = 0;
    size_t ri = 0;

    while (ai < al ||
	   bi < bl ||
	   carry) {
	IDIO_BS_T ia = 0;
	IDIO_BS_T ib = 0;

	if (ai < al) {
	    ia = idio_bsa_get (IDIO_BIGNUM_SIG (a), ai);
	    IDIO_C_ASSERT (ia < IDIO_BIGNUM_INT_SEG_LIMIT);
	}

	if (bi < bl) {
	    ib = idio_bsa_get (IDIO_BIGNUM_SIG (b), bi);
	    IDIO_C_ASSERT (ib < IDIO_BIGNUM_INT_SEG_LIMIT);
	}

	IDIO_BS_T ir = ia + ib + carry;
	carry = 0;

	if (ir >= IDIO_BIGNUM_INT_SEG_LIMIT) {
	    ir -= IDIO_BIGNUM_INT_SEG_LIMIT;
	    carry = 1;
	}

	idio_bsa_set (ra, ir, ri);

	ai++;
	bi++;
	ri++;
    }

    IDIO r = idio_bignum_integer (ra);

    return r;
}

int idio_bignum_zero_p (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (bignum, a);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = IDIO_BSA_SIZE (sig_a);
    if (1 == al) {
	IDIO_BS_T i = idio_bsa_get (sig_a, 0);
	return (0 == i);
    }

    return 0;
}

int idio_bignum_lt_p (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    /* idio_debug ("bignum_lt %s", a); */
    /* idio_debug (" < %s == ", b); */

    int na = idio_bignum_negative_p (a);
    int nb = idio_bignum_negative_p (b);

    if (na &&
	!nb) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt (- 2fnm) 2fnm
	 */
	return 1;
    }

    if (!na &&
	nb) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt 2fnm (- 2fnm)
	 */
	return 0;
    }

    size_t al = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (a));
    size_t bl = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (b));
    IDIO_C_ASSERT (al);

    if (al < bl) {
	return na ? 0 : 1;
    }

    if (al > bl) {
	return na ? 1 : 0;
    }

    IDIO aa = idio_bignum_abs (a);
    IDIO_BSA sig_aa = IDIO_BIGNUM_SIG (aa);

    IDIO ab = idio_bignum_abs (b);
    IDIO_BSA sig_ab = IDIO_BIGNUM_SIG (ab);

    ssize_t ai;
    for (ai = al - 1; ai >= 0 ; ai--) {
	IDIO_BS_T iaa = idio_bsa_get (sig_aa, ai);
	IDIO_BS_T iab = idio_bsa_get (sig_ab, ai);

	if (iaa < iab) {
	    return na ? 0 : 1;
	}

	if (iaa > iab) {
	    return na ? 1 : 0;
	}
    }

    return 0;
}

int idio_bignum_equal_p (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    IDIO_BSA sig_aa = IDIO_BIGNUM_SIG (a);
    size_t al = IDIO_BSA_SIZE (sig_aa);
    IDIO_BSA sig_ab = IDIO_BIGNUM_SIG (b);
    size_t bl = IDIO_BSA_SIZE (sig_ab);

    if (al != bl) {
	/*
	 * Code coverage: C unit test??
	 */
	return 0;
    }

    size_t i;
    for (i = 0; i < al ; i++) {
	IDIO_BS_T ia = idio_bsa_get (sig_aa, i);
	IDIO_BS_T ib = idio_bsa_get (sig_ab, i);

	if (ia != ib) {
	    return 0;
	}
    }

    return 1;
}

IDIO idio_bignum_subtract (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    /* we want to avoid operations with negative numbers */
    if (idio_bignum_negative_p (a)) {
	/*
	 * Code coverage: C unit test??
	 */
	if (idio_bignum_negative_p (b)) {
	    /* -a - -b => -a + |b| => |b| - |a| */
	    a = idio_bignum_abs (a);
	    b = idio_bignum_abs (b);

	    return idio_bignum_subtract (b, a);
	} else {
	    /* -a - b => |a| + b */
	    a = idio_bignum_abs (a);

	    IDIO r = idio_bignum_add (a, b);

	    return idio_bignum_negate (r);
	}
    } else if (idio_bignum_negative_p (b)) {
	/*
	 * Code coverage: C unit test??
	 */
	/* a - -b => a + |b| */
	b = idio_bignum_abs (b);

	return idio_bignum_add (a, b);
    }

    /* regular a - b: a < b => -(b - a) */
    if (idio_bignum_lt_p (a, b)) {
	IDIO r = idio_bignum_subtract (b, a);

	return idio_bignum_negate (r);
    }

    /* regular a - b: a >= b */
    IDIO_BSA sig_aa = IDIO_BIGNUM_SIG (a);
    size_t al = IDIO_BSA_SIZE (sig_aa);
    IDIO_BSA sig_ab = IDIO_BIGNUM_SIG (b);
    size_t bl = IDIO_BSA_SIZE (sig_ab);

    int borrow = 0;
    size_t rl;

    if (al >= bl) {
	rl = al;
    } else {
	/*
	 * Code coverage: C unit test??
	 */
	rl = bl;
    }

    IDIO_BSA ra = idio_bsa (rl);

    size_t ai = 0;
    size_t bi = 0;
    size_t ri = 0;

    int borrow_bug = 0;
    while (ai < al ||
	   bi < bl ||
	   borrow) {
	IDIO_BS_T ia = 0;
	IDIO_BS_T ib = 0;

	if (ai < al) {
	    ia = idio_bsa_get (sig_aa, ai);
	}

	if (bi < bl) {
	    ib = idio_bsa_get (sig_ab, bi);
	}

	IDIO_BS_T ir = ia - ib - borrow;

	borrow = 0;

	if (ir < 0) {
	    ir += IDIO_BIGNUM_INT_SEG_LIMIT;
	    borrow = 1;
	    borrow_bug++;
	    IDIO_C_ASSERT (borrow_bug < (10 + IDIO_BIGNUM_SIG_SEGMENTS));
	}

	idio_bsa_set (ra, ir, ri);

	ai++;
	bi++;
	ri++;
    }

    /* remove leading zeroes */
    IDIO_BS_T ir = idio_bsa_get (ra, rl - 1);
    while (0 == ir &&
	   rl > 1) {
	idio_bsa_pop (ra);
	rl--;
	ir = idio_bsa_get (ra, rl - 1);
    }

    return idio_bignum_integer (ra);
}

IDIO idio_bignum_shift_left (IDIO a, int const fill)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (bignum, a);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    IDIO_BSA ra = idio_bsa (al);

    /* idio_debug ("bignum-shift-left: a %s\n", a); */
    /* idio_bignum_dump (a); */

    int carry = fill;

    ssize_t ai;
    for (ai = 0; ai < (ssize_t) al; ai++) {
	IDIO_BS_T i = idio_bsa_get (sig_a, ai);
	IDIO_BS_T r;

	if (ai < (ssize_t) (al - 1) &&
	    i < 0) {

	    /*
	     * Test Case: ??
	     *
	     * Requires bad developer code.
	     */
	    idio_bignum_error ("non-last bignum segment < 0", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	if (i >= (IDIO_BIGNUM_INT_SEG_LIMIT / 10)) {
	    IDIO_BS_T c = i / (IDIO_BIGNUM_INT_SEG_LIMIT / 10);
	    r = i % (IDIO_BIGNUM_INT_SEG_LIMIT / 10) * 10;
	    r += carry;
	    IDIO_C_ASSERT (r >= 0);
	    carry = c;
	} else {
	    r = i * 10 + carry;
	    /* IDIO_C_ASSERT (r >= 0); */
	    carry = 0;
	}

	idio_bsa_set (ra, r, ai);
    }

    if (carry) {
	idio_bsa_set (ra, carry, ai);
    }

    IDIO r = idio_bignum_integer (ra);

    /* idio_debug ("bignum-shift-left: r %s\n", r); */
    /* idio_bignum_dump (r); */

    return r;
}

/* result: (a/10 . a%10) */
IDIO idio_bignum_shift_right (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (bignum, a);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    IDIO_BSA ra;

    /* repeated shift_rights result in an empty array! */
    if (al) {
	ra = idio_bsa (al);
    } else {
	/*
	 * Code coverage: C unit test??
	 */
	al++;
	ra = idio_bsa (al);

	/* plonk this int64 into sig_a as that's what we're about to
	get_index from */
	idio_bsa_set (sig_a, 0, 0);
    }

    int carry = 0;

    size_t segs = al - 1;
    ssize_t ai;
    for (ai = segs; ai >= 0; ai--) {
	IDIO_BS_T i = idio_bsa_get (sig_a, ai);

	IDIO_BS_T c = i % 10;
	IDIO_BS_T r = i / 10;
	r += carry * (IDIO_BIGNUM_INT_SEG_LIMIT / 10);
	carry = c;

	idio_bsa_set (ra, r, ai);
    }

    /* is more than one segment s and if the mss is zero, pop it
	off */
    if (al > 1) {
	IDIO_BS_T v = idio_bsa_get (ra, segs);
	if (0 == v) {
	    idio_bsa_pop (ra);
	}
    }

    IDIO c_i = idio_bignum_integer_intmax_t (carry);

    IDIO r_i = idio_bignum_integer (ra);

    return idio_pair (r_i, c_i);
}

IDIO idio_bignum_multiply (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    int neg = idio_bignum_negative_p (a) != idio_bignum_negative_p (b);
    IDIO aa = idio_bignum_abs (a);

    IDIO ab = idio_bignum_abs (b);

    /* idio_debug ("bignum-multiply: aa %s ", aa); */
    /* idio_debug ("* %s\n", ab); */

    IDIO r = idio_bignum_integer_intmax_t (0);

    /*
      1234 * 11 =>
      4 * 11 +
      3 * 110 +
      2 * 1100 +
      1 * 11000
     */
    while (! idio_bignum_zero_p (aa)) {
	IDIO ibsr = idio_bignum_shift_right (aa);

	IDIO ibsrt = IDIO_PAIR_T (ibsr);
	IDIO_BS_T i = idio_bsa_get (IDIO_BIGNUM_SIG (ibsrt), 0);

	aa = IDIO_PAIR_H (ibsr);

	while (i) {
	    r = idio_bignum_add (r, ab);
	    i--;
	}

	ab = idio_bignum_shift_left (ab, 0);
    }

    if (neg) {
	r = idio_bignum_negate (r);
    }

    return r;
}

/*
  Prepare for (long) division of a / b: find (r,f) such that r < a and
  r == b * 10^m and f is 10^m, eg.  12345 / 123 => (12300, 100)

  Note that 24680 / 123 => (12300, 100) as well as
  idio_bignum_equalize is only scaling by 10.

  r = scaled divisor
  f = factor (of scaled divisor)
 */
IDIO idio_bignum_equalize (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    IDIO rp = b;

    IDIO fp = idio_bignum_integer_intmax_t (1);

    IDIO rn = rp;
    IDIO fn = fp;

    while (idio_bignum_lt_p (rn, a)) {
	rp = rn;
	fp = fn;

	rn = idio_bignum_shift_left (rn, 0);
	fn = idio_bignum_shift_left (fn, 0);
    }

    return idio_pair (rp, fp);
}

/* result: (a/b . a%b) */
IDIO idio_bignum_divide (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    if (idio_bignum_zero_p (b)) {
	/*
	 * Test Case: bigum-errors/divide-float-zero.idio
	 *
	 * 1 / 0.0
	 *
	 * XXX Or would be except idio_bignum_primitive_divide()
	 * catches this (same) test before us.
	 */
	idio_bignum_error_divide_by_zero (IDIO_LIST2 (a, b), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int na = idio_bignum_negative_p (a);
    int neg = na != idio_bignum_negative_p (b);

    IDIO aa = idio_bignum_abs (a);
    IDIO ab = idio_bignum_abs (b);

    IDIO r_div = idio_bignum_integer_intmax_t (0);
    IDIO r_mod = idio_bignum_copy (aa);

    /*
      a / b for 12 / 123

      r_div = 0
      r_mod = 12
     */
    if (idio_bignum_lt_p (aa, ab)) {
	/*
	 * Code coverage: ??
	 */
	if (na) {
	    r_mod = a;

	    return idio_pair (r_div, r_mod);
	}
    }

    IDIO ibe = idio_bignum_equalize (aa, ab);
    IDIO sd = IDIO_PAIR_H (ibe);
    IDIO sf = IDIO_PAIR_T (ibe);

    /*
      a / b for 12345 / 123

      r_div = 0
      r_mod = 12345
      sd = 12300
      sf = 100

      !zero_p(100)
        c0=c=0
	while !(r_mod < c)
	  c0=c
	  c+=12300 ; 12300
	  i++      ; 1
	  c0=c
	  c+=12300 ; 24600
	  i++      ; 2
	  ...loop fail
	r_div = r_div*10 + 1 ; 1
	r_mod = 12345 - 12300; 45
	sf = 100/10; 10
	sd = 12300/10 ; 1230
      !zero_p (10)
        c0=c=0
	while !(r_mod < c)
	  c0=c
	  c+=1230 ; 1230
	  i++      ; 1
	  ...loop fail
	r_div = r_div*10 + 0; == 10
	r_mod = 45-0; == 45
	sf = 10/10; == 1
	sd = 1230/10; == 123
      !zero_p(1)
        c0=c=0
	while !(r_mod < c)
	  c0=c
	  c+=123 ; 123
	  i++      ; 1
	  ...loop fail
	r_div = r_div*10 + 0; == 100
	r_mod = 45-0; == 45
	sf = 1/10; == 0
	sd = 123/10; == 12

     */

    while (! idio_bignum_zero_p (sf)) {
	IDIO c = idio_bignum_integer_intmax_t (0);
	IDIO c0 = c;

	int i = 0;
	while (! idio_bignum_lt_p (r_mod, c)) {
	    c0 = c;

	    c = idio_bignum_add (c, sd);

	    i++;
	}

	r_div = idio_bignum_shift_left (r_div, i - 1);

	r_mod = idio_bignum_subtract (r_mod, c0);

	IDIO ibsr = idio_bignum_shift_right (sf);
	sf = IDIO_PAIR_H (ibsr);

	ibsr = idio_bignum_shift_right (sd);
	sd = IDIO_PAIR_H (ibsr);
    }

    if (neg) {
	/*
	 * Code coverage: ??
	 */
	r_div = idio_bignum_negate (r_div);
    }

    if (na) {
	/*
	 * Code coverage: ??
	 */
	r_mod = idio_bignum_negate (r_mod);
    }

    return idio_pair (r_div, r_mod);
}

/* floating point numbers */

IDIO idio_bignum_integer_argument (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    if (IDIO_BIGNUM_INTEGER_P (bn)) {
	return bn;
    }

    IDIO bn_i = idio_bignum_real_to_integer (bn);
    if (idio_S_nil == bn_i ||
	IDIO_BIGNUM_REAL_INEXACT_P (bn)) {
	/*
	 * Code coverage: ??
	 */
	return idio_S_nil;
    }

    return bn_i;
}

IDIO idio_bignum_real (int flags, IDIO_BE_T exp, IDIO_BSA sig_a)
{
    flags &= ~IDIO_BIGNUM_FLAG_INTEGER;

    return idio_bignum (flags | IDIO_BIGNUM_FLAG_REAL, exp, sig_a);
}

IDIO idio_bignum_real_to_integer (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    if (IDIO_BIGNUM_EXP (bn) >= 0) {
	IDIO bns = idio_bignum_scale_significand (bn, 0, IDIO_BIGNUM_SIG_MAX_DIGITS);

	if (idio_S_nil == bns) {
	    /*
	     * Code coverage: ??
	     */
	    return idio_S_nil;
	}

	IDIO bn_i = idio_bignum_copy_to_integer (bns);

	if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	    /*
	     * Code coverage: ??
	     */
	    bn_i = idio_bignum_negate (bn_i);
	}

	return bn_i;
    }

    return idio_S_nil;
}

IDIO idio_bignum_real_to_inexact (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO r = idio_bignum_real (IDIO_BIGNUM_FLAGS (bn) | IDIO_BIGNUM_FLAG_REAL_INEXACT,
			       IDIO_BIGNUM_EXP (bn),
			       IDIO_BIGNUM_SIG (bn));

    return r;
}

IDIO idio_bignum_real_to_exact (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO r = idio_bignum_real (IDIO_BIGNUM_FLAGS (bn) & ~IDIO_BIGNUM_FLAG_REAL_INEXACT,
			       IDIO_BIGNUM_EXP (bn),
			       IDIO_BIGNUM_SIG (bn));

    return r;
}

IDIO idio_bignum_real_negate (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    int flags = IDIO_BIGNUM_FLAGS (bn);

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	flags &= ~ IDIO_BIGNUM_FLAG_REAL_NEGATIVE;
    } else {
	/*
	 * Code coverage: - 0.0 1.1
	 */
	flags |= IDIO_BIGNUM_FLAG_REAL_NEGATIVE;
    }

    IDIO r = idio_bignum_real (flags, IDIO_BIGNUM_EXP (bn), IDIO_BIGNUM_SIG (bn));

    return r;
}

/*
  Remove trailing zeroes: 123000 => 123e3
  Shift decimal place to end: 1.23e0 => 123e-2

  Limit to IDIO_BIGNUM_SIG_MAX_DIGITS, a loss of precision
 */
IDIO idio_bignum_normalize (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);
    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);

    /* significand-only part */
    IDIO bn_s = idio_bignum_copy (bn);

    size_t digits = idio_bignum_count_digits (sig_a);

    IDIO ibsr;

    while (digits > IDIO_BIGNUM_SIG_MAX_DIGITS) {
	ibsr = idio_bignum_shift_right (bn_s);

	bn_s = IDIO_PAIR_H (ibsr);
	digits--;
	IDIO_BE_T exp0 = exp;
	exp++;
	if (exp < exp0) {
#ifdef IDIO_DEBUG
	    /*
	    idio_debug ("bn-norm sd %35s", bn);
	    fprintf (stderr, " exp=%" IDIO_BE_FMT " < exp0=%" IDIO_BE_FMT " digits=%zu\n", exp, exp0, digits);
	    */
#endif
	    /*
	     * Test Case: bignum-errors/bignum-normalise-digits-overflow.idio
	     *
	     * 100000000000000000000e2147483647
	     *
	     * NB Here, it is because there are more digits than
	     * IDIO_BIGNUM_SIG_MAX_DIGITS that trigger the overflow
	     */
	    idio_bignum_conversion_error ("exponent overflow", bn, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    while (! idio_bignum_zero_p (bn_s)) {
	ibsr = idio_bignum_shift_right (bn_s);

	if (! idio_bignum_zero_p (IDIO_PAIR_T (ibsr))) {
	    break;
	}

	bn_s = IDIO_PAIR_H (ibsr);
	IDIO_BE_T exp0 = exp;
	exp++;
	if (exp < exp0) {
#ifdef IDIO_DEBUG
	    /*
	    idio_debug ("bn-norm tz %35s", bn);
	    fprintf (stderr, " exp=%" IDIO_BE_FMT " < exp0=%" IDIO_BE_FMT "\n", exp, exp0);
	    */
#endif
	    /*
	     * Test Case: bignum-errors/bignum-normalise-overflow.idio
	     *
	     * 10e2147483647
	     *
	     * NB Here, it is because there are trailing zeroes
	     * triggering the overflow
	     */
	    idio_bignum_conversion_error ("exponent overflow", bn, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (idio_bignum_zero_p (bn_s)) {
	exp = 0;
    }

    /*
     * S9fES checks for over/under-flow in exp wrt
     * IDIO_BIGNUM_DPW. Not sure that's applicable here.
     */

    IDIO r = idio_bignum_real (IDIO_BIGNUM_FLAGS(bn), exp, IDIO_BIGNUM_SIG (bn_s));

    return r;
}

IDIO idio_bignum_to_real (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BE_T exp = 0;

    IDIO bnc = idio_bignum_copy (bn);
    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bnc);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    size_t segs = al - 1;
    IDIO_BS_T i = idio_bsa_get (sig_a, segs);
    idio_bsa_set (sig_a, llabs (i), segs);

    size_t nseg = al;

    IDIO r;

    if (nseg > IDIO_BIGNUM_SIG_SEGMENTS) {
	size_t digits = idio_bignum_count_digits (sig_a);
	while (digits > IDIO_BIGNUM_SIG_MAX_DIGITS) {
	    IDIO t = idio_bignum_shift_right (bnc);
	    bnc = IDIO_PAIR_H (t);
	    exp++;
	    digits--;
	}
    }

    int flags = IDIO_BIGNUM_FLAG_NONE;
    if (idio_bignum_negative_p (bn)) {
	/*
	 * Code coverage: ??
	 */
	flags |= IDIO_BIGNUM_FLAG_REAL_NEGATIVE;
    }

    r = idio_bignum_real (flags, exp, IDIO_BIGNUM_SIG (bnc));

    r = idio_bignum_normalize (r);

    return r;
}

int idio_bignum_real_zero_p (IDIO a)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (bignum, a);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = IDIO_BSA_SIZE (sig_a);

    if (al > 1) {
	/*
	 * Code coverage: ??
	 */
	return 0;
    }

    IDIO_BS_T ia = idio_bsa_get (sig_a, 0);

    return (0 == ia);
}

IDIO idio_bignum_real_abs (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    if (IDIO_BIGNUM_INTEGER_P (bn)) {
	return idio_bignum_abs (bn);
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	return idio_bignum_real_negate (bn);
    }

    return bn;
}

int idio_bignum_real_equal_p (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    if (IDIO_BIGNUM_INTEGER_P (a) &&
	IDIO_BIGNUM_INTEGER_P (b)) {
	return idio_bignum_equal_p (a, b);
    }

    IDIO ra = a;

    if (IDIO_BIGNUM_INTEGER_P (a)) {
	ra = idio_bignum_to_real (a);
    }

    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    if (IDIO_BIGNUM_REAL_INEXACT_P (ra) != IDIO_BIGNUM_REAL_INEXACT_P (rb)) {
	return 0;
    }

    if (IDIO_BIGNUM_EXP (ra) != IDIO_BIGNUM_EXP (rb)) {
	return 0;
    }

    if (idio_bignum_real_zero_p (ra) &&
	idio_bignum_real_zero_p (rb)) {
	return 1;
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (ra) != IDIO_BIGNUM_REAL_NEGATIVE_P (rb)) {
	return 0;
    }

    IDIO_BSA ras = IDIO_BIGNUM_SIG (ra);
    IDIO_BSA rbs = IDIO_BIGNUM_SIG (rb);

    size_t ral = IDIO_BSA_SIZE (ras);
    size_t rbl = IDIO_BSA_SIZE (rbs);
    IDIO_C_ASSERT (ral);

    if (ral != rbl) {
	/*
	 * Code coverage: ??
	 */
	return 0;
    }

    ssize_t rai;
    for (rai = ral - 1; rai >= 0; rai--) {
	IDIO_BS_T ia = idio_bsa_get (ras, rai);
	IDIO_BS_T ib = idio_bsa_get (rbs, rai);

	if (ia != ib) {
	    return 0;
	}
    }

    return 1;
}

/*
  1.0e0, -2, *

  =>

  100.0e-2
 */
IDIO idio_bignum_scale_significand (IDIO bn, IDIO_BE_T desired_exp, IDIO_BE_T const max_digits)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    size_t digits = idio_bignum_count_digits (IDIO_BIGNUM_SIG (bn));

    /* is there room to scale within the desired_exp (and max_digits)? */
    if ((max_digits - (ssize_t) digits) < (IDIO_BIGNUM_EXP (bn) - desired_exp)) {
	/*
	 * Code coverage: a side-effect of (+ -1e40 -1e0)
	 */
	return idio_S_nil;
    }

    IDIO bnc = idio_bignum_copy (bn);

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);
    while (exp > desired_exp) {
	bnc = idio_bignum_shift_left (bnc, 0);
	IDIO_BE_T exp0 = exp;
	exp--;
	if (exp > exp0) {
	    /*
	     * Test Case: ??
	     *
	     * Hmm, not sure how to provoke this from Idio
	     */
	    idio_bignum_conversion_error ("exponent underflow", bn, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return idio_bignum_real (IDIO_BIGNUM_FLAGS(bn), exp, IDIO_BIGNUM_SIG (bnc));
}

int idio_bignum_real_lt_p (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    /* idio_debug ("bignum_real_lt: %s", a); */
    /* idio_debug (" < %s == ", b); */

    if (IDIO_BIGNUM_INTEGER_P (a) &&
	IDIO_BIGNUM_INTEGER_P (b)) {
	return idio_bignum_lt_p (a, b);
    }

    IDIO ra0 = a;

    if (IDIO_BIGNUM_INTEGER_P (a)) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt 2fnm 3.0
	 */
	ra0 = idio_bignum_to_real (a);
    }

    IDIO rb0 = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt 3.0 2fnm
	 */
	rb0 = idio_bignum_to_real (b);
    }

    int na = IDIO_BIGNUM_REAL_NEGATIVE_P (ra0);
    int nb = IDIO_BIGNUM_REAL_NEGATIVE_P (rb0);

    if (na &&
	! nb) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt (- 2fnm) 3.0
	 */
	return 1;
    }

    if (nb &&
	! na) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt 3.0 (- 2fnm)
	 */
	return 0;
    }

    if (IDIO_BIGNUM_REAL_POSITIVE_P (ra0) &&
	idio_bignum_real_zero_p (rb0)) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt 2fnm 0.0
	 */
	return 0;
    }

    /* XXX S9fES has real_positive_p (a) bug?? */
    if (idio_bignum_real_zero_p (ra0) &&
	IDIO_BIGNUM_REAL_POSITIVE_P (rb0)) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * lt 0.0 2fnm
	 */
	return 1;
    }

    /*
     * dpa/dpb can be negative if their EXP is very negative
     */
    intptr_t dpa = idio_bignum_count_digits (IDIO_BIGNUM_SIG (ra0)) + IDIO_BIGNUM_EXP (ra0);
    intptr_t dpb = idio_bignum_count_digits (IDIO_BIGNUM_SIG (rb0)) + IDIO_BIGNUM_EXP (rb0);

    if (dpa < dpb) {
	return na ? 0 : 1;
    }

    if (dpa > dpb) {
	return na ? 1 : 0;
    }

    /*
     * I don't think we can get #n from the scaling in the next two
     * clauses.
     *
     * We are here because dpa == dpb, that is
     *
     * d(a) + E(a) == d(b) + E(b)
     *
     * or, transform #1:
     *
     * d(a) - d(b) == E(b) - E(a)
     *
     * To get #n scaling says (max-digits - d(bn) < E(bn) - desired-E)
     * or, for E(a) < E(b):
     *
     * 18 - d(b) < E(b) - E(a)
     *
     * which is the RHS from above:
     *
     * 18 - d(b) < d(a) - d(b)
     *
     * 18 < d(a)
     *
     * Can we have more than 18 digits in a (or b)?  Not from
     * user-space.  (Probably)
     */
    IDIO rb1 = rb0;
    if (IDIO_BIGNUM_EXP (ra0) < IDIO_BIGNUM_EXP (rb1)) {
	rb1 = idio_bignum_scale_significand (rb1, IDIO_BIGNUM_EXP (ra0), IDIO_BIGNUM_SIG_MAX_DIGITS);

	if (idio_S_nil == rb1) {
	    /* Code coverage: see above */
	    return na ? 0 : 1;
	}
    }

    IDIO ra1 = ra0;
    if (IDIO_BIGNUM_EXP (ra1) > IDIO_BIGNUM_EXP (rb1)) {
	ra1 = idio_bignum_scale_significand (ra1, IDIO_BIGNUM_EXP (rb1), IDIO_BIGNUM_SIG_MAX_DIGITS);

	if (idio_S_nil == ra1) {
	    /* Code coverage: see above */
	    return na ? 0 : 1;
	}
    }

    size_t ra1l = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (ra1));
    size_t rb1l = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (rb1));

    if (ra1l < rb1l) {
	return 1;
    }

    if (ra1l > rb1l) {
	return 0;
    }

    intptr_t i;

    for (i = ra1l - 1 ; i >= 0 ; i--) {
	IDIO_BS_T ia = idio_bsa_get (IDIO_BIGNUM_SIG (ra1), i);
	IDIO_BS_T ib = idio_bsa_get (IDIO_BIGNUM_SIG (rb1), i);

	if (ia < ib) {
	    return na ? 0 : 1;
	}

	if (ia > ib) {
	    return na ? 1 : 0;
	}
    }

    return 0;
}

IDIO idio_bignum_real_add (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    if (IDIO_BIGNUM_INTEGER_P (a) &&
	IDIO_BIGNUM_INTEGER_P (b)) {
	return idio_bignum_add (a, b);
    }

    IDIO ra0 = a;

    if (IDIO_BIGNUM_INTEGER_P (a)) {
	ra0 = idio_bignum_to_real (a);
    }

    IDIO rb0 = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * + 3.0 2fnm
	 */
	rb0 = idio_bignum_to_real (b);
    }

    if (idio_bignum_real_zero_p (ra0)) {
	return rb0;
    }

    if (idio_bignum_real_zero_p (rb0)) {
	return ra0;
    }

    IDIO ra1 = ra0;
    IDIO rb1 = rb0;

    if (IDIO_BIGNUM_EXP (ra0) < IDIO_BIGNUM_EXP (rb0)) {
	rb1 = idio_bignum_scale_significand (rb0, IDIO_BIGNUM_EXP (ra0), IDIO_BIGNUM_SIG_MAX_DIGITS * 2);
    } else if (IDIO_BIGNUM_EXP (ra0) > IDIO_BIGNUM_EXP (rb0)) {
	ra1 = idio_bignum_scale_significand (ra0, IDIO_BIGNUM_EXP (rb0), IDIO_BIGNUM_SIG_MAX_DIGITS * 2);
    }

    if (idio_S_nil == ra1 ||
	idio_S_nil == rb1) {

	/*
	 * Code coverage: issues/2-2.idio
	 *
	 * -1e40 + -1e0
	 */

	IDIO ara0 = idio_bignum_real_abs (ra0);
	IDIO arb0 = idio_bignum_real_abs (rb0);

	if (idio_bignum_real_lt_p (ara0, arb0)) {
	    return b;
	} else {
	    return a;
	}
    }

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (ra1);
    int nra1 = IDIO_BIGNUM_REAL_NEGATIVE_P (ra1);
    int nrb1 = IDIO_BIGNUM_REAL_NEGATIVE_P (rb1);

    IDIO ra1_i = idio_bignum_copy_to_integer (ra1);

    if (nra1) {
	ra1_i = idio_bignum_negate (ra1_i);
    }

    IDIO rb1_i = idio_bignum_copy_to_integer (rb1);

    if (nrb1) {
	rb1_i = idio_bignum_negate (rb1_i);
    }

    IDIO r_i = idio_bignum_add (ra1_i, rb1_i);

    int flags = IDIO_BIGNUM_FLAG_NONE;

    if (idio_bignum_negative_p (r_i)) {
	flags |= IDIO_BIGNUM_FLAG_REAL_NEGATIVE;
    }

    IDIO r_ia = idio_bignum_abs (r_i);

    IDIO r = idio_bignum_real (flags, exp, IDIO_BIGNUM_SIG (r_ia));

    return idio_bignum_normalize (r);
}

IDIO idio_bignum_real_subtract (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    IDIO nb;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	nb = idio_bignum_negate (b);
    } else {
	nb = idio_bignum_real_negate (b);
    }

    IDIO r = idio_bignum_real_add (a, nb);

    return r;
}

IDIO idio_bignum_real_multiply (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    if (IDIO_BIGNUM_INTEGER_P (a) &&
	IDIO_BIGNUM_INTEGER_P (b)) {
	return idio_bignum_multiply (a, b);
    }

    IDIO ra = a;

    if (IDIO_BIGNUM_INTEGER_P (a)) {
	ra = idio_bignum_to_real (a);
    }

    if (idio_bignum_real_zero_p (ra)) {
	/*
	 * Code coverage:
	 *
	 * * 0.0 1
	 */
	return ra;
    }

    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	/*
	 * Code coverage:
	 *
	 * 2fnm := 2 * FIXNUM-MAX
	 * * 1.0 2fnm
	 */
	rb = idio_bignum_to_real (b);
    }

    if (idio_bignum_real_zero_p (rb)) {
	return rb;
    }

    int neg = IDIO_BIGNUM_REAL_NEGATIVE_P (ra) != IDIO_BIGNUM_REAL_NEGATIVE_P (rb);

    IDIO_BE_T expa = IDIO_BIGNUM_EXP (ra);
    IDIO_BE_T expb = IDIO_BIGNUM_EXP (rb);

    IDIO ra_i = idio_bignum_copy_to_integer (ra);

    IDIO rb_i = idio_bignum_copy_to_integer (rb);

    IDIO_BE_T exp = expa + expb;
    if (expb < 0 &&
	exp > expa) {
	/*
	 * Test Case: bignum-errors/bignum-multiply-underflow.idio
	 *
	 * 1e-2147483648 * 0.1
	 */
	idio_bignum_conversion_error ("exponent underflow", a, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    } else if (expb >= 0 &&
	       exp < expa) {
	/*
	 * Test Case: bignum-errors/bignum-multiply-overflow.idio
	 *
	 * 1e2147483647 * 10
	 */
	idio_bignum_conversion_error ("exponent overflow", a, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r_i = idio_bignum_multiply (ra_i, rb_i);

    int flags = (neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0);

    IDIO r = idio_bignum_real (flags, exp, IDIO_BIGNUM_SIG (r_i));

    return idio_bignum_normalize (r);
}

IDIO idio_bignum_real_divide (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    IDIO ra = a;

    if (IDIO_BIGNUM_INTEGER_P (a)) {
	ra = idio_bignum_to_real (a);
    }

    if (idio_bignum_real_zero_p (ra)) {
	/*
	 * Code coverage:
	 *
	 * / 0.0 1
	 */
	IDIO i0 = idio_bignum_integer_intmax_t (0);

	IDIO r = idio_bignum_real (0, 0, IDIO_BIGNUM_SIG (i0));

	return r;
    }

    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    int neg = IDIO_BIGNUM_REAL_NEGATIVE_P (ra) != IDIO_BIGNUM_REAL_NEGATIVE_P (rb);

    IDIO_BE_T expa = IDIO_BIGNUM_EXP (ra);
    IDIO_BE_T expb = IDIO_BIGNUM_EXP (rb);

    IDIO ra_i = idio_bignum_copy_to_integer (ra);
    IDIO rb_i = idio_bignum_copy_to_integer (rb);

    if (idio_bignum_zero_p (rb)) {
	/*
	 * Code coverage: ^rt-divide-by-zero-error is generated in
	 * preference.
	 */
	return idio_S_NaN;
    }

    /*
      The actual division is an integer division of the significand
      digits (keeping track of the exponents separately).

      However, the integer division of 13/4 is 3.  We don't seem to
      have as many significant digits in the result as we would like
      for a division of what are real numbers: 13.0/4 is 3.0. Really?

      But wait, the integer division of 13000/4 is 3250, so if we
      bumped the numerator up by 10^n (and decremented its exponent by
      n), in this case n=3, then we'll have more significant digits in
      our answer and the combined exponent, now -3+0=-3, makes the
      resultant real 3250e-3 or 3.250.  Hurrah!

      So what value of n?  As big as we can!

      Here we can abuse our normal IDIO_BIGNUM_SIG_MAX_DIGITS limit
      and say that we want to make n such that digits(a*10^n) ==
      digits(b)+MAX_DIGITS. This way, without losing precision in b
      (by shrinking it) we can bump a up such that the resultant
      integer division has MAX_DIGITS significant digits.

      Note that if digits(a) is MAX_DIGITS and digits(b) is one then
      digits(a) after this will be 2*MAX_DIGITS.
     */

    size_t nd = idio_bignum_count_digits (IDIO_BIGNUM_SIG (ra));
    size_t dd = IDIO_BIGNUM_SIG_MAX_DIGITS + idio_bignum_count_digits (IDIO_BIGNUM_SIG (rb));

    while (nd < dd) {
	ra_i = idio_bignum_shift_left (ra_i, 0);

	nd++;
	IDIO_BE_T expa0 = expa;
	expa--;
	if (expa > expa0) {
	    /*
	     * Test Case: bignum-errors/bignum-divide-digits-underflow.idio
	     *
	     * 1e-2147483648 / 10
	     *
	     * Careful, this should be because the number of digits in
	     * the numerator is less than the number of digits in the
	     * denominator
	     */
	    idio_bignum_conversion_error ("exponent underflow", ra, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO_BE_T exp = expa - expb;
    if (expb > 0 &&
	exp > expa) {
	/*
	 * Test Case: bignum-errors/bignum-divide-underflow.idio
	 *
	 * 12345678901234567890e-2147483648 / 1e100
	 *
	 * NB the twenty digits in the numerator mean its exponent is
	 * twenty bigger than the number below which means we need to
	 * divide by at least a power of twenty
	 */
	idio_bignum_conversion_error ("exponent underflow", ra, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    } else if (expb < 0 &&
	       exp < expa) {
	/*
	 * Test Case: bignum-errors/bignum-divide-overflow.idio
	 *
	 * 12345678901234567890e2147483627 / 1e-100
	 *
	 * NB the twenty digits in the numerator mean its exponent is
	 * twenty bigger than the number above -- notice it is ...27
	 * and not ...47, the limit -- which means we need to divide
	 * by at least a power of minus twenty
	 */
	idio_bignum_conversion_error ("exponent overflow", ra, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    IDIO ibd = idio_bignum_divide (ra_i, rb_i);
    IDIO r_i = IDIO_PAIR_H (ibd);

    int flags = (neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0);

    IDIO r = idio_bignum_real (flags, exp, IDIO_BIGNUM_SIG (r_i));

    return idio_bignum_normalize (r);
}

/* printers */

char *idio_bignum_integer_as_string (IDIO bn, size_t *sizep)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);

    char *s = idio_alloc (1);
    *s = '\0';

    /*
     * We only check for idio-print-conversion-precision here and NOT
     * idio-print-conversion-format because I don't know how to
     * convert the decimalised bignum into a hexadecimal or octal (or
     * binary!) number without an extremely expensive reworking.
     *
     * The problem relates to us having split the value of the bignum
     * into DPW decimal segments.  If I've got the second segment in
     * my hands and its value is 1, that won't guarantee to be 1 in
     * any other format, in the same way that the 1 in 1234 won't
     * guarantee to be a 1 in the hex (#x4D2), octal (#o2322) or
     * binary (#b10011010010) -- OK, a reasonable chance with the
     * binary.
     */

    unsigned int prec = 0;
    if (idio_S_nil != idio_print_conversion_precision_sym) {
	IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
					      idio_Idio_module,
					      IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcp) {
	    if (idio_isa_fixnum (ipcp)) {
		/*
		 * Code coverage:
		 *
		 * 2fnm := 2 * FIXNUM-MAX
		 * printf "%.3d" 2fnm
		 */
		prec = IDIO_FIXNUM_VAL (ipcp);
	    } else {
		/*
		 * Test Case: ??
		 *
		 * If we set idio-print-conversion-precision to
		 * something not a fixnum (nor #f) then it affects
		 * *everything* in the codebase that uses
		 * idio-print-conversion-precision before we get here.
		 */
		idio_error_param_type ("fixnum", ipcp, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }

    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    size_t segs = al - 1;
    ssize_t ai;
    for (ai = segs; ai >= 0; ai--) {
	IDIO_BS_T v = idio_bsa_get (sig_a, ai);
	char buf[BUFSIZ];
	if (ai == (ssize_t) segs) {
	    idio_snprintf (buf, BUFSIZ, "%zd", v);

	    size_t buf_len = idio_strnlen (buf, BUFSIZ);
	    size_t bn_digits = buf_len + ai * IDIO_BIGNUM_DPW;
	    if (prec > bn_digits) {
		/*
		 * Code coverage:
		 *
		 * 2fnm := 2 * FIXNUM-MAX
		 * printf "%.40d" 2fnm
		 *
		 * XXX %.40d -- rather then %.30d -- triggered a
		 * realloc bug just below where I didn't re-assign {s}
		 */
		int pad = prec - bn_digits;
		char pads[pad + 1];
		idio_snprintf (pads, pad + 1, "%.*d", pad, 0);
		s = idio_strcat (s, sizep, pads, pad);
	    }
	} else {
	    idio_snprintf (buf, BUFSIZ, IDIO_BIGNUM_DPW_FMT, v);
	}
	size_t buf_size = idio_strnlen (buf, BUFSIZ);
	s = idio_strcat (s, sizep, buf, buf_size);
    }

    return s;
}

/*
 * Code coverage: not called, see idio_bignum_real_as_string()
 */
char *idio_bignum_expanded_real_as_string (IDIO bn, IDIO_BE_T exp, int digits, int neg, size_t *sizep)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    char *s = idio_alloc (1);
    *s = '\0';

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	IDIO_STRCAT (s, sizep, "-");
    }

    IDIO_BE_T exp0 = exp;
    IDIO_BE_T dp_offset = exp + digits;
    if (dp_offset < exp0) {
	idio_bignum_conversion_error ("exponent overflow", bn, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    if (dp_offset <= 0) {
	IDIO_STRCAT (s, sizep, "0");
    }

    if (dp_offset < 0) {
	IDIO_STRCAT (s, sizep, ".");
    }

    while (dp_offset < 0) {
	IDIO_STRCAT (s, sizep, "0");
	dp_offset++;
    }

    dp_offset = exp + digits;
    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    ssize_t ai;
    for (ai = al - 1; ai >= 0; ai--) {
	IDIO_BS_T v = idio_bsa_get (sig_a, ai);
	char *vs;
	size_t vs_size = idio_asprintf (&vs, "%" PRIdPTR, v);

	IDIO_STRCAT_FREE (s, sizep, vs, vs_size);
    }

    if (dp_offset >= 0) {
	while (dp_offset > 0) {
	    IDIO_STRCAT (s, sizep, "0");
	    dp_offset--;
	}
	IDIO_STRCAT (s, sizep, ".0");
    }

    if (IDIO_BIGNUM_REAL_INEXACT_P (bn)) {
	IDIO_STRCAT (s, sizep, "-inexact");
    }

    return s;
}

#define IDIO_BIGNUM_CONVERSION_FORMAT_SCHEME	0
#define IDIO_BIGNUM_CONVERSION_FORMAT_e		0x65
#define IDIO_BIGNUM_CONVERSION_FORMAT_f		0x66

#define IDIO_BIGNUM_CONVERSION_FORMAT_s		0x73

char *idio_bignum_real_as_string (IDIO bn, size_t *sizep)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    if (!IDIO_BIGNUM_REAL_P (bn)) {
	/*
	 * Code coverage: *probably* shoudn't get here as integers are
	 * diverted in idio_bignum_as_string()
	 */
	return NULL;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t digits = idio_bignum_count_digits (sig_a);
    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);

    if (0 && (exp + digits) > -4 &&
	(exp + digits) <= 9) {
	/*
	 * Code coverage: deliberately excluded for reasons I don't
	 * recall.  Consistency in output?
	 */
	return idio_bignum_expanded_real_as_string (bn, exp, digits, IDIO_BIGNUM_REAL_NEGATIVE_P (bn), sizep);
    }

    idio_unicode_t format = IDIO_BIGNUM_CONVERSION_FORMAT_SCHEME;
    if (idio_S_nil != idio_print_conversion_format_sym) {
	IDIO ipcf = idio_module_symbol_value (idio_print_conversion_format_sym,
					      idio_Idio_module,
					      IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcf) {
	    if (idio_isa_unicode (ipcf)) {
		idio_unicode_t f = IDIO_UNICODE_VAL (ipcf);
		switch (f) {
		case IDIO_BIGNUM_CONVERSION_FORMAT_e:
		case IDIO_BIGNUM_CONVERSION_FORMAT_f:
		    format = f;
		    break;
		case IDIO_BIGNUM_CONVERSION_FORMAT_s:
		    /*
		     * Code coverage:
		     *
		     * printf "%s" pi
		     */
		    /*
		     * A generic: printf "%s" e
		     */
		    format = IDIO_BIGNUM_CONVERSION_FORMAT_e;
		    break;
		default:
		    /*
		     * Code coverage:
		     *
		     * printf "%d" pi
		     */
#ifdef IDIO_DEBUG
		    fprintf (stderr, "bignum-as-string: unexpected conversion format: %c (%#x).  Using 'e'.\n", (int) f, (int) f);
#endif
		    format = IDIO_BIGNUM_CONVERSION_FORMAT_e;
		    break;
		}
	    } else {
		/*
		 * Test Case: bignum-errors/real-format-type.idio
		 *
		 * idio-print-conversion-format = 'foo
		 * string 4.0
		 *
		 * XXX the test is commented out as several other
		 * conversions occur before we can restore the format
		 */
		idio_error_param_type ("unicode", ipcf, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }

    /*
     * The default precision for both e and f formats is 6
     */
    unsigned int prec = 6;
    if (idio_S_nil != idio_print_conversion_precision_sym) {
	IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
					      idio_Idio_module,
					      IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcp) {
	    if (idio_isa_fixnum (ipcp)) {
		prec = IDIO_FIXNUM_VAL (ipcp);
	    } else {
		/*
		 * Test Case: ??
		 *
		 * If we set idio-print-conversion-precision to
		 * something not a fixnum (nor #f) then it affects
		 * *everything* in the codebase that uses
		 * idio-print-conversion-precision before we get here.
		 */
		idio_error_param_type ("fixnum", ipcp, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }

    char *s = idio_alloc (1);
    *s = '\0';

    if (IDIO_BIGNUM_REAL_INEXACT_P (bn)) {
	IDIO_STRCAT (s, sizep, "#i");
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	/*
	 * Code coverage:
	 *
	 * printf "%e" (- pi)
	 */
	IDIO_STRCAT (s, sizep, "-");
    }

    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    intptr_t ai = al - 1;
    IDIO_BS_T v = idio_bsa_get (sig_a, ai);

    /*
     * NB vs is used, separately, for both the mantissa,
     * IDIO_BIGNUM_DPW digits, and exponent (with a leading +/-).  The
     * exponent is an int32_t which requires 11 digits which is longer
     * than IDIO_BIGNUM_DPW on 32-bit systems.
     */
    size_t vs_len = 30;
    char vs[vs_len];

    switch (format) {
    case IDIO_BIGNUM_CONVERSION_FORMAT_SCHEME:
	{
	    /*
	     * Scheme?  Does it have an official floating point
	     * printed representation?  This is approximately S9fES.
	     */

	    /*
	     * vs can be n digits long (n >= 1).  We want to add vs[0] then
	     * ".".  If vs is more than 1 digit then add the rest of vs.  If
	     * there are no more digits to add then add "0".
	     */
	    idio_snprintf (vs, vs_len, "%" PRIdPTR, v);

	    char *vs_rest = vs + 1;

	    size_t vs_size = idio_strnlen (vs, vs_len);
	    s = idio_strcat (s, sizep, vs, 1);

	    if (prec) {
		IDIO_STRCAT (s, sizep, ".");
	    }

	    /*
	     * vs_rest == vs + 1 so strnlen one less that for vs
	     */
	    size_t vs_rest_size = idio_strnlen (vs_rest, vs_len - 1);
	    if (vs_rest_size) {
		s = idio_strcat (s, sizep, vs_rest, vs_rest_size);
	    } else {
		if (0 == ai) {
		    IDIO_STRCAT (s, sizep, "0");
		}
	    }

	    for (ai--; ai >= 0; ai--) {
		/*
		 * Code coverage: ??
		 */
		v = idio_bsa_get (sig_a, ai);
		vs_size = idio_snprintf (vs, vs_len, "%0*" PRIdPTR, IDIO_BIGNUM_DPW, v);

		s = idio_strcat (s, sizep, vs, vs_size);
	    }

	    IDIO_STRCAT (s, sizep, "e");
	    IDIO_BE_T e = exp + digits - 1;
	    vs_size = idio_snprintf (vs, vs_len, "%+" PRId32, e);

	    s = idio_strcat (s, sizep, vs, vs_size);
	}
	break;
    case IDIO_BIGNUM_CONVERSION_FORMAT_e:
	{
	    /*
	     * vs can be n digits long (n >= 1).  We want to add vs[0] then
	     * ".".  If vs is more than 1 digit then add the rest of vs.  If
	     * there are no more digits to add then add "0".
	     */
	    idio_snprintf (vs, vs_len, "%" PRIdPTR, v);

	    char *vs_rest = vs + 1;

	    size_t vs_size = idio_strnlen (vs, vs_len);
	    s = idio_strcat (s, sizep, vs, 1);

	    if (prec) {
		IDIO_STRCAT (s, sizep, ".");
	    }

	    /*
	     * vs_rest == vs + 1 so strnlen one less that for vs
	     */
	    size_t vs_rest_size = idio_strnlen (vs_rest, vs_len - 1);
	    size_t vs_rest_prec = vs_rest_size;
	    if (prec < vs_rest_prec) {
		vs_rest_prec = prec;
	    }
	    if (vs_rest_size) {
		s = idio_strcat (s, sizep, vs_rest, vs_rest_prec);
	    }
	    prec -= vs_rest_prec;

	    for (ai--; ai >= 0; ai--) {
		/*
		 * Code coverage: ??
		 */
		v = idio_bsa_get (sig_a, ai);
		idio_snprintf (vs, vs_len, "%0*" PRIdPTR, IDIO_BIGNUM_DPW, v);

		vs_size = idio_strnlen (vs, vs_len);
		if (prec < vs_size) {
		    vs_size = prec;
		}
		s = idio_strcat (s, sizep, vs, vs_size);
		if (prec == vs_size) {
		    break;
		}
		prec -= vs_size;
	    }
	    if (prec > 0) {
		int pad = prec;
		char pads[pad + 1];
		idio_snprintf (pads, pad + 1, "%.*d", pad, 0);

		s = idio_strcat (s, sizep, pads, pad);
	    }

	    IDIO_STRCAT (s, sizep, "e");
	    IDIO_BE_T e = exp + digits - 1;
	    vs_size = idio_snprintf (vs, vs_len, "%+03" PRId32, e);

	    s = idio_strcat (s, sizep, vs, vs_size);
	}
	break;
    case IDIO_BIGNUM_CONVERSION_FORMAT_f:
	{
	    size_t vs_size = idio_snprintf (vs, vs_len, "%" PRIdPTR, v);

	    int pre_dp_digits = digits + exp;
	    /* we should have detected over/underflow by now! */

	    if (exp >= 0) {
		s = idio_strcat (s, sizep, vs, vs_size);
		int pad = exp;
		if (pad > 0) {
		    char pads[pad + 1];
		    idio_snprintf (pads, pad + 1, "%.*d", pad, 0);

		    s = idio_strcat (s, sizep, pads, pad);
		}
		if (prec) {
		    IDIO_STRCAT (s, sizep, ".");
		    pad = prec;
		    char pads[pad + 1];
		    idio_snprintf (pads, pad + 1, "%.*d", pad, 0);

		    s = idio_strcat (s, sizep, pads, pad);
		}
	    } else {
		char *vs_rest = vs;
		if (pre_dp_digits > 0) {
		    s = idio_strcat (s, sizep, vs, pre_dp_digits);
		    vs_rest = vs + pre_dp_digits;
		} else {
		    IDIO_STRCAT (s, sizep, "0");
		}

		if (prec) {
		    IDIO_STRCAT (s, sizep, ".");
		    if (pre_dp_digits < 0) {
			unsigned int pad = - pre_dp_digits;
			if (prec < pad) {
			    pad = prec;
			}
			char pads[pad + 1];
			idio_snprintf (pads, pad + 1, "%.*d", pad, 0);

			s = idio_strcat (s, sizep, pads, pad);
			prec -= pad;
		    }

		    /*
		     * vs_rest == vs + 1 so strnlen one less that for vs
		     */
		    size_t vs_rest_size = idio_strnlen (vs_rest, vs_len - 1);
		    size_t vs_rest_prec = vs_rest_size;
		    if (prec < vs_rest_prec) {
			vs_rest_prec = prec;
		    }
		    if (vs_rest_size) {
			s = idio_strcat (s, sizep, vs_rest, vs_rest_prec);
		    }
		    prec -= vs_rest_prec;

		    for (ai--; ai >= 0; ai--) {
			/*
			 * Code coverage: issues/1-209.idio
			 *
			 * Only applies for systems which have
			 * IDIO_BIGNUM_SIG_SEGMENTS > 1.  In our case
			 * 32-bit systems.
			 */
			v = idio_bsa_get (sig_a, ai);
			vs_size = idio_snprintf (vs, vs_len, "%0*" PRIdPTR, IDIO_BIGNUM_DPW, v);

			if (prec < vs_size) {
			    vs_size = prec;
			}
			s = idio_strcat (s, sizep, vs, vs_size);

			/*
			 * A casual scan suggests this test should be
			 * (prec <= vs_size) except we reset vs_size
			 * in the (prec < vs_size) clause just above.
			 */
			if (prec == vs_size) {
			    prec = 0;
			    break;
			}
			prec -= vs_size;
		    }
		    if (prec > 0) {
			int pad = prec;
			char pads[pad + 1];
			idio_snprintf (pads, pad + 1, "%.*d", pad, 0);

			s = idio_strcat (s, sizep, pads, pad);
		    }
		}
	    }
	    break;
	}
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     *
	     * I don't think this code is reachable.  Unexpected
	     * idio-print-conversion-format values should have been
	     * handled in the value lookup above -- and set to 'e' if
	     * unexpected.
	     */
	    fprintf (stderr, "bignum-as-string: unimplemented conversion format: %c (%#x)\n", (int) format, (int) format);
	    idio_bignum_error ("bignum-as-string unimplemented conversion format", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}
	break;
    }

    return s;
}

char *idio_bignum_as_string (IDIO bn, size_t *sizep)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    if (idio_S_NaN == bn) {
	/*
	 * Code coverage: no use of NaN (yet?)
	 */
	size_t ibn_len = sizeof (IDIO_BIGNUM_NAN) - 1;
	char *s = idio_alloc (ibn_len + 1);
	memcpy (s, IDIO_BIGNUM_NAN, ibn_len);
	s[ibn_len] = '\0';

	return s;
    }

    if (IDIO_BIGNUM_INTEGER_P (bn)) {
	return idio_bignum_integer_as_string (bn, sizep);
    }
    return idio_bignum_real_as_string (bn, sizep);
}

/*
  count the digits in the first array element (by dividing by 10) then
  add DPW times the remaining number of elements
 */
size_t idio_bignum_count_digits (IDIO_BSA sig_a)
{
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_C_ASSERT (al);

    size_t segs = al - 1;
    IDIO_BS_T v = idio_bsa_get (sig_a, segs);

    size_t d = 0;
    while (v) {
	v /= 10;
	d++;
    }

    if (0 == d) {
	d++;
    }

    d += (segs * IDIO_BIGNUM_DPW);

    return d;
}

char *idio_bignum_C_without_inexact (char const *nums, size_t const nums_len)
{
    IDIO_C_ASSERT (nums);
    IDIO_C_ASSERT (nums_len > 0);

    char *buf = idio_alloc (nums_len + 1);
    memcpy (buf, nums, nums_len);
    buf[nums_len] = '\0';

    char *s = buf;
    while (*s) {
	if ('#' == *s) {
	    *s = '5';
	}
	s++;
    }

    return buf;
}

IDIO idio_bignum_integer_C (char const *nums, size_t const nums_len, const int req_exact)
{
    IDIO_C_ASSERT (nums);
    IDIO_C_ASSERT (nums_len > 0);

    char *buf = idio_bignum_C_without_inexact (nums, nums_len);
    int is_exact = (NULL == strchr (nums, '#'));

    char *s = buf;
    size_t nl = nums_len;
    int sign = 1;
    if ('-' == *s) {
	sign = -1;
	s++;
	nl--;
    } else if ('+' == *s) {
	/*
	 * Code coverage:
	 *
	 * +123456789012345678
	 *
	 * Surprisingly tricky as we require an integer (so no period
	 * or exponent character) and the reader will try consume
	 * "small" integers with idio_fixnum_C()
	 */
	s++;
	nl--;
    }

    IDIO_BSA ra = idio_bsa (1);

    size_t ri = 0;

    while (nl) {
	int eos = (nl < IDIO_BIGNUM_DPW) ? nl : IDIO_BIGNUM_DPW;
	char *end;
	int base = 10;
	errno = 0;
	long long int i = strtoll (&s[nl-eos], &end, base);

	if ((errno == ERANGE &&
	     (i == LLONG_MAX ||
	      i == LLONG_MIN)) ||
	    (errno != 0 &&
	     i == 0)) {
	    idio_free (buf);

	    /*
	     * Test Case: ??
	     *
	     * I need more brainpower to figure out how to get here.
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "(%s) = %lld", nums, i);

	    idio_error_system_errno_msg ("strtoll", em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (end == nums) {
	    idio_free (buf);

	    /*
	     * Test Case: ??
	     *
	     * I need more brainpower to figure out how to get here.
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "(%s): No digits?", nums);

	    idio_error_system_errno_msg ("strtoll", em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if ('\0' != *end) {
	    idio_free (buf);

	    /*
	     * Test Case: ??
	     *
	     * I need more brainpower to figure out how to get here.
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "strtoll (%s) = %lld", nums, i);

	    idio_bignum_error (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	s[nl-eos] = '\0';
	nl -= eos;
	if (0 == nl &&
	    (req_exact ||
	     is_exact)) {
	    i *= sign;
	}

	idio_bsa_set (ra, i, ri);
	ri++;
    }

    idio_free (buf);

    /* remove leading zeroes */
    size_t rl = ri;
    IDIO_BS_T ir = idio_bsa_get (ra, rl - 1);
    while (0 == ir &&
	   rl > 1) {
	idio_bsa_pop (ra);
	rl--;
	ir = idio_bsa_get (ra, rl - 1);
    }

    if (req_exact ||
	is_exact) {
	IDIO r = idio_bignum_integer (ra);
	return r;
    } else {
	IDIO r = idio_bignum_real ((IDIO_BIGNUM_FLAG_REAL |
				    ((sign < 0) ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0) |
				    IDIO_BIGNUM_FLAG_REAL_INEXACT),
				   0,
				   ra);

	return idio_bignum_normalize (r);
    }
}

IDIO idio_bignum_real_C (char const *nums, size_t const nums_len)
{
    IDIO_C_ASSERT (nums);
    IDIO_C_ASSERT (nums_len > 0);

    IDIO sig_bn = idio_bignum_integer_intmax_t (0);

    IDIO_BE_T exp = 0;
    char *s = (char *) nums;
    size_t slen = nums_len;
    int neg = 0;

    if ('+' == *s) {
	/*
	 * Code coverage:
	 *
	 * +0.0
	 */
	s++;
	slen--;
    } else if ('-' == *s) {
	neg = 1;
	s++;
	slen--;
    }

    int found_period = 0;
    int exact = 1;
    int digit;

    while (isdigit (*s) ||
	   '#' == *s ||
	   '.' == *s) {

	if ('.' == *s) {
	    found_period = 1;
	    s++;
	    slen--;
	    continue;
	}

	if (found_period) {
	    exp--;
	}

	sig_bn = idio_bignum_shift_left (sig_bn, 0);

	if ('#' == *s) {
	    exact = 0;
	    digit = 5;
	} else {
	    digit = *s - '0';
	}

	IDIO i = idio_bignum_integer_intmax_t (digit);

	sig_bn = idio_bignum_add (sig_bn, i);

	s++;
	slen--;
    }

    /*
     * also unused in S9fES string_to_real

    int j = idio_array_size (IDIO_BIGNUM_SIG (sig_bn));
    */

    if (IDIO_BIGNUM_EXP_CHAR (*s)) {
	s++;
	slen--;
	IDIO n = idio_bignum_integer_C (s, slen, 1);
	int64_t exp_v = idio_bignum_int64_t_value (n);
	IDIO_BE_T exp0 = exp;
	exp += exp_v;
	if (exp_v < 0 &&
	    exp > exp0) {
#ifdef IDIO_DEBUG
	    /*
	    fprintf (stderr, "bn-real-C %35s exp_v=%" PRId64 " < 0 && exp=%" IDIO_BE_FMT " > exp0=%" IDIO_BE_FMT "\n", nums, exp_v, exp, exp0);
	    */
#endif
	    /*
	     * Test Case: bignum-errors/read-bignum-underflow.idio
	     *
	     * 1e-2147483649
	     */
	    idio_bignum_conversion_error ("exponent underflow", n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else if (exp_v >= 0 &&
		   exp < exp0) {
#ifdef IDIO_DEBUG
	    /*
	    fprintf (stderr, "bn-real-C %35s exp_v=%" PRId64 " >= 0 && exp=%" IDIO_BE_FMT " < exp0=%" IDIO_BE_FMT "\n", nums, exp_v, exp, exp0);
	    */
#endif
	    /*
	     * Test Case: bignum-errors/read-bignum-overflow.idio
	     *
	     * 1e2147483648
	     */
	    idio_bignum_conversion_error ("exponent overflow", n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    /* remove leading zeroes */
    IDIO_BSA ra = IDIO_BIGNUM_SIG (sig_bn);
    size_t rl = IDIO_BSA_SIZE (ra);
    IDIO_BS_T ir = idio_bsa_get (ra, rl - 1);
    while (0 == ir &&
	   rl > 1) {
	/*
	 * Code coverage: ??
	 */
	idio_bsa_pop (ra);
	rl--;
	ir = idio_bsa_get (ra, rl - 1);
    }

    IDIO r = idio_bignum_real (((neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0) |
				(exact ? 0 : IDIO_BIGNUM_FLAG_REAL_INEXACT)),
			       exp,
			       ra);

    return idio_bignum_normalize (r);
}

IDIO idio_bignum_C (char const *nums, size_t const nums_len)
{
    IDIO_C_ASSERT (nums);
    IDIO_C_ASSERT (nums_len > 0);

    char *s = (char *) nums;
    while (*s) {
	if ('.' == *s ||
	    IDIO_BIGNUM_EXP_CHAR (*s)) {
	    return idio_bignum_real_C (nums, nums_len);
	}
	s++;
    }

    return idio_bignum_integer_C (nums, nums_len, 0);
}

IDIO idio_bignum_primitive_add (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_bignum_integer_intmax_t (0);

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/add-non-bignum.idio
	     *
	     * + 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	r = idio_bignum_real_add (r, h);

        args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO idio_bignum_primitive_subtract (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_S_unspec;

    int first = 1;
    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/subtract-non-bignum.idio
	     *
	     * - 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (first) {
	    first = 0;

	    /*
	      a bit of magic for subtract:

	      (- 6)   => 0-6 => -6
	      (- 6 2) => 6-2 => 4
	    */

	    IDIO t = IDIO_PAIR_T (args);
	    if (idio_S_nil == t) {
		if (IDIO_BIGNUM_INTEGER_P (h)) {
		    r = idio_bignum_negate (h);
		} else {
		    r = idio_bignum_real_negate (h);
		}
		break;
	    } else {
		r = idio_bignum_copy (h);

		args = t;
		continue;
	    }
	}

	r = idio_bignum_real_subtract (r, h);

        args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO idio_bignum_primitive_multiply (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_bignum_integer_intmax_t (1);

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/multiply-non-bignum.idio
	     *
	     * * 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	r = idio_bignum_real_multiply (r, h);

        args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO idio_bignum_primitive_divide (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_bignum_integer_intmax_t (1);

    int first = 1;

    IDIO oargs = args;

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: ??
	     *
	     * For divide the C macro in fixnum checks for "numbers"
	     * so this code is potentially not reached.
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (first) {
	    first = 0;

	    /*
	      a bit of magic for divide:

	      (/ 6)   => 1/6 => 1/6
	      (/ 6 2) => 6/2 => 3
	    */

	    IDIO t = IDIO_PAIR_T (args);
	    if (idio_S_nil != t) {
		r = idio_bignum_copy (h);

		args = t;
		continue;
	    }
	}

	if (idio_bignum_zero_p (h)) {
	    /*
	     * Test Case: bignum-errors/divide-float-zero.idio
	     *
	     * / 1.0 0.0
	     */
	    idio_bignum_error_divide_by_zero (oargs, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	r = idio_bignum_real_divide (r, h);

        args = IDIO_PAIR_T (args);
    }

    return r;
}

IDIO idio_bignum_primitive_floor (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO r;

    IDIO_BE_T exp = IDIO_BIGNUM_EXP (bn);

    if (exp >= 0) {
	r = bn;
    } else {
	IDIO_BSA sig = IDIO_BIGNUM_SIG (bn);
	IDIO bn_i = idio_bignum_integer (sig);

	/*
	 * For very large (negative) exp the idio_bignum_shift_right()
	 * calls in the loop below will generate large numbers of
	 * bignums even though the value is zero and the machine will
	 * run out of memory (it transpires).  We can shortcut that by
	 * noting that the post-normalised digit count is
	 * IDIO_BIGNUM_SIG_MAX_DIGITS and if that number plus exp is
	 * still below zero then we would have shifted below 1 (one)
	 * anyway.
	 *
	 * We'll still allow floor 123456789012345678e-17 (for
	 * MAX_DIGITS of 18) to be 1.0e+0.
	 */
	if ((IDIO_BIGNUM_SIG_MAX_DIGITS + exp) < 0) {
	    IDIO i0 = idio_bignum_integer_intmax_t (0);

	    IDIO r = idio_bignum_real (0, 0, IDIO_BIGNUM_SIG (i0));

	    return r;
	}

	while (exp < 0) {
	    IDIO ibsr = idio_bignum_shift_right (bn_i);
	    bn_i = idio_list_head (ibsr);
	    /*
	     * NB we're incrementing a negative number up to zero.  We
	     * shouldn't have any overflow issues...
	     */
	    exp++;
	}

	if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	    IDIO bn1 = idio_bignum_integer_intmax_t (1);

	    bn_i = idio_bignum_add (bn_i, bn1);
	}

	r = idio_bignum_real (IDIO_BIGNUM_FLAGS (bn), exp, IDIO_BIGNUM_SIG (bn_i));

	r = idio_bignum_normalize (r);
    }

    return r;
}

IDIO idio_bignum_primitive_quotient (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    IDIO ia = idio_bignum_integer_argument (a);
    if (idio_S_nil == ia) {
	/*
	 * Test Case: bignum-errors/quotient-a-non-integer-bignum.idio
	 *
	 * quotient 1.1 1
	 */
	idio_error_param_value_exp ("quotient", "a", a, "integer bignum", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO ib = idio_bignum_integer_argument (b);
    if (idio_S_nil == ib) {
	/*
	 * Test Case: bignum-errors/quotient-b-non-integer-bignum.idio
	 *
	 * quotient 1.1 1
	 */
	idio_error_param_value_exp ("quotient", "b", b, "integer bignum", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO ibd = idio_bignum_divide (ia, ib);

    /* idio_debug ("bignum_quotient: %s\n", ibd); */

    return IDIO_PAIR_H (ibd);
}

IDIO idio_bignum_primitive_remainder (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    IDIO ia = idio_bignum_integer_argument (a);
    if (idio_S_nil == ia) {
	/*
	 * Test Case: bignum-errors/remainder-a-non-integer-bignum.idio
	 *
	 * remainder 1.1 1
	 */
	idio_error_param_value_exp ("remainder", "a", a, "integer bignum", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO ib = idio_bignum_integer_argument (b);
    if (idio_S_nil == ib) {
	/*
	 * Test Case: bignum-errors/remainder-b-non-integer-bignum.idio
	 *
	 * remainder 1.1 1
	 */
	idio_error_param_value_exp ("remainder", "b", b, "integer bignum", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO ibd = idio_bignum_divide (ia, ib);

    /* idio_debug ("bignum_remainder: %s\n", ibd); */

    return IDIO_PAIR_T (ibd);
}

IDIO idio_bignum_primitive_lt (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/lt-non-bignum.idio
	     *
	     * lt 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/* r < h */
	if (! idio_bignum_real_lt_p (r, h)) {
	    return idio_S_false;
	}

	r = h;
        args = idio_list_tail (args);
    }

    return idio_S_true;
}

IDIO idio_bignum_primitive_le (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/le-non-bignum.idio
	     *
	     * le 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/* r <= h => ! h < r */
	if (idio_bignum_real_lt_p (h, r)) {
	    return idio_S_false;
	}

	r = h;
        args = idio_list_tail (args);
    }

    return idio_S_true;
}

IDIO idio_bignum_primitive_eq (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/eq-non-bignum.idio
	     *
	     * eq 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (! idio_bignum_real_equal_p (r, h)) {
	    return idio_S_false;
	}

	r = h;
        args = idio_list_tail (args);
    }

    return idio_S_true;
}

IDIO idio_bignum_primitive_ne (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/ne-non-bignum.idio
	     *
	     * ne 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (idio_bignum_real_equal_p (r, h)) {
	    return idio_S_false;
	}

	r = h;
        args = idio_list_tail (args);
    }

    return idio_S_true;
}

IDIO idio_bignum_primitive_ge (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/ge-non-bignum.idio
	     *
	     * ge 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/* r >= h => ! r < h */
	if (idio_bignum_real_lt_p (r, h)) {
	    return idio_S_false;
	}

	r = h;
        args = idio_list_tail (args);
    }

    return idio_S_true;
}

IDIO idio_bignum_primitive_gt (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
	    /*
	     * Test Case: bignum-errors/gt-non-bignum.idio
	     *
	     * gt 1.0 #t
	     */
	    idio_error_param_type ("bignum", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/* r > h => h < r */
	if (! idio_bignum_real_lt_p (h, r)) {
	    return idio_S_false;
	}

	r = h;
        args = idio_list_tail (args);
    }

    return idio_S_true;
}

int idio_realp (IDIO n)
{
    IDIO_ASSERT (n);

    if (idio_isa_bignum (n) &&
	IDIO_BIGNUM_REAL_P (n)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("bignum?", bignump, (IDIO o), "o", "\
test if `o` is a bignum				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a bignum, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_bignum (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("real?", realp, (IDIO n), "n", "\
test if `n` is a real				\n\
						\n\
:param n: number to test			\n\
:type n: bignum					\n\
:return: ``#t`` if `n` is a real, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (n);

    IDIO r = idio_S_false;

    if (idio_realp (n)) {
	r = idio_S_true;
    }

    return r;
}

#define IDIO_BIGNUM_FIXNUM_TYPE(n)				\
    if (idio_isa_fixnum (n)) {					\
	n = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n));	\
    } else {							\
	IDIO_USER_TYPE_ASSERT (bignum, n);			\
    }

IDIO_DEFINE_PRIMITIVE1_DS ("exact?", exactp, (IDIO n), "n", "\
test if `n` is exact				\n\
						\n\
:param n: number to test			\n\
:type n: bignum	or fixnum			\n\
:return: ``#t`` if `n` is exact, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (n);

    IDIO r = idio_S_false;

    if (idio_isa_fixnum (n)) {
	r = idio_S_true;
    } else {
	/*
	 * Test Case: bignum-errors/exact-bad-type.idio
	 *
	 * exact? #t
	 */
	IDIO_USER_TYPE_ASSERT (bignum, n);

	if (IDIO_BIGNUM_INTEGER_P (n)) {
	    /*
	     * Code coverage:
	     *
	     * exact? (FIXNUM-MAX + 1)
	     */
	    r = idio_S_true;
	} else if (! IDIO_BIGNUM_REAL_INEXACT_P (n)) {
	    /*
	     * Code coverage:
	     *
	     * exact? 1.2
	     */
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("inexact?", inexactp, (IDIO n), "n", "\
test if `n` is inexact				\n\
						\n\
:param n: number to test			\n\
:type n: bignum	or fixnum			\n\
:return: ``#t`` if `n` is inexact, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (n);

    if (idio_isa_fixnum (n)) {
	return idio_S_false;
    }

    /*
     * Test Case: bignum-errors/exact-bad-type.idio
     *
     * inexact? #t
     */
    IDIO_USER_TYPE_ASSERT (bignum, n);

    IDIO r = idio_S_false;

    if (! IDIO_BIGNUM_INTEGER_P (n)) {
	if (IDIO_BIGNUM_REAL_INEXACT_P (n)) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("exact->inexact", exact2inexact, (IDIO n), "n", "\
return an inexact version of `n`		\n\
						\n\
:param n: number to convert			\n\
:type n: bignum	or fixnum			\n\
:return: inexact value of `n`			\n\
")
{
    IDIO_ASSERT (n);

    /*
     * Test Case: bignum-errors/exact2inexact-bad-type.idio
     *
     * exact->inexact #t
     */
    IDIO_BIGNUM_FIXNUM_TYPE (n);

    IDIO r = idio_S_unspec;

    if (IDIO_BIGNUM_INTEGER_P (n)) {
        int flags = (idio_bignum_negative_p (n) ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0) | IDIO_BIGNUM_FLAG_REAL_INEXACT;
        IDIO na = idio_bignum_abs (n);
        IDIO nr = idio_bignum_real (flags, 0, IDIO_BIGNUM_SIG (na));

        r = idio_bignum_normalize (nr);
    } else {
        r = idio_bignum_real_to_inexact (n);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("inexact->exact", inexact2exact, (IDIO n), "n", "\
return an exact version of `n`			\n\
						\n\
:param n: number to convert			\n\
:type n: bignum	or fixnum			\n\
:return: exact value of `n`			\n\
")
{
    IDIO_ASSERT (n);

    IDIO r = idio_S_unspec;

    if (idio_isa_fixnum (n)) {
	return n;
    } else {
	/*
	 * Test Case: bignum-errors/inexact2exact-bad-type.idio
	 *
	 * inexact->exact #t
	 */
	IDIO_USER_TYPE_ASSERT (bignum, n);

	if (IDIO_BIGNUM_INTEGER_P (n)) {
	    /*
	     * Code coverage: not sure we can get here (as inexact
	     * numbers are always real?)
	     */
	    r = n;
	} else {
	    r = idio_bignum_real_to_integer (n);
	    if (idio_S_nil == r) {
		r = idio_bignum_real_to_exact (n);
	    }
	}
    }

    r = idio_bignum_to_fixnum (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("mantissa", mantissa, (IDIO n), "n", "\
return the mantissa of `n`			\n\
						\n\
:param n: number to find mantissa of		\n\
:type n: bignum	or fixnum			\n\
:return: mantissa of `n`			\n\
")
{
    IDIO_ASSERT (n);

    if (idio_isa_fixnum (n)) {
	return n;
    }

    /*
     * Test Case: bignum-errors/mantissa-bad-type.idio
     *
     * mantissa #t
     */
    IDIO_USER_TYPE_ASSERT (bignum, n);

    IDIO r = idio_S_unspec;

    if (IDIO_BIGNUM_INTEGER_P (n)) {
        r = n;
    } else {
	r = idio_bignum_integer (IDIO_BIGNUM_SIG (n));

	if (IDIO_BIGNUM_REAL_NEGATIVE_P (n)) {
	    r = idio_bignum_negate (r);
	}
    }

    r = idio_bignum_to_fixnum (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("exponent", exponent, (IDIO n), "n", "\
return the exponent of `n`			\n\
						\n\
:param n: number to find exponent of		\n\
:type n: bignum					\n\
:return: exponent of `n`			\n\
")
{
    IDIO_ASSERT (n);

    /*
     * Test Case: bignum-errors/exponent-bad-type.idio
     *
     * exponent #t
     */
    IDIO_USER_TYPE_ASSERT (bignum, n);

    IDIO r = idio_S_unspec;

    if (IDIO_BIGNUM_INTEGER_P (n)) {
        r = idio_fixnum (0);
    } else {
	IDIO_BE_T exp = IDIO_BIGNUM_EXP (n);
	r = idio_integer (exp);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("bignum-dump", bignum_dump, (IDIO n), "n", "\
dump the bignum structure of `n` to	\n\
*stderr*				\n\
					\n\
:param n: number to dump		\n\
:type n: bignum				\n\
:return: ``#<unspec>``			\n\
")
{
    IDIO_ASSERT (n);

    /*
     * Test Case: bignum-errors/bignum-dump-bad-type.idio
     *
     * bignum-dump #t
     */
    IDIO_USER_TYPE_ASSERT (bignum, n);

    idio_bignum_dump (n);

    return idio_S_unspec;
}

char *idio_bignum_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (bignum, v);

    return idio_bignum_as_string (v, sizep);
}

IDIO idio_bignum_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_bignum_as_C_string (v, sizep, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_bignum_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (bignump);
    IDIO_ADD_PRIMITIVE (realp);
    IDIO_ADD_PRIMITIVE (exactp);
    IDIO_ADD_PRIMITIVE (inexactp);
    IDIO_ADD_PRIMITIVE (exact2inexact);
    IDIO_ADD_PRIMITIVE (inexact2exact);
    IDIO_ADD_PRIMITIVE (mantissa);
    IDIO_ADD_PRIMITIVE (exponent);
    IDIO_ADD_PRIMITIVE (bignum_dump);
}

void idio_final_bignum ()
{
#ifdef IDIO_DEBUG
    FILE *fh = stderr;

#ifdef IDIO_VM_PROF
    fh = idio_vm_perf_FILE;
#endif

    fprintf (fh, "bignums: current %zd of simultaneous max %zd; max segs %zd/%d (%zd significant digits)\n", idio_bignums, idio_bignums_max, idio_bignum_seg_max, IDIO_BIGNUM_SIG_SEGMENTS, idio_bignum_seg_max * IDIO_BIGNUM_DPW);
#endif
}

void idio_init_bignum ()
{
    idio_module_table_register (idio_bignum_add_primitives, idio_final_bignum, NULL);

    IDIO_BSA sig_a = idio_bsa (1);
    idio_bsa_set (sig_a, 1, 0);
    IDIO epsilon = idio_bignum_real (IDIO_BIGNUM_FLAG_REAL, - IDIO_BIGNUM_SIG_MAX_DIGITS, sig_a);
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("*epsilon*"), epsilon, idio_Idio_module);

    idio_vtable_t *b_vt = idio_vtable (IDIO_TYPE_BIGNUM);

    idio_vtable_add_method (b_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_bignum));

    idio_vtable_add_method (b_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_bignum_method_2string));
}
