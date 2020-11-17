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
 * bignum.c
 *
 */

#include "idio.h"

size_t idio_bignums = 0;
size_t idio_bignums_max = 0;
size_t idio_bignum_seg_max = 0;

static void idio_bignum_error_conversion (char *msg, IDIO bn, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO c = idio_struct_instance (idio_condition_rt_bignum_conversion_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_S_nil,
					       bn));
    idio_raise_condition (idio_S_true, c);
}

static void idio_bignum_error_divide_by_zero (IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_divide_by_zero ("bignum divide by zero", c_location);
}

IDIO_BSA idio_bsa (size_t n)
{
    if (n <= 0) {
	n = IDIO_BIGNUM_SIG_SEGMENTS;
    }

    if (n > idio_bignum_seg_max) {
	idio_bignum_seg_max = n;
    }

    /* fprintf (stderr, "idio_bsa: %zd\n", n); */
    IDIO_BSA bsa = idio_alloc (sizeof (idio_bsa_t));
    bsa->ae = idio_alloc (n * sizeof (IDIO_BS_T));
    bsa->avail = n;
    bsa->size = n;
    bsa->refs = 0;

    size_t i;
    for (i = 0; i < bsa->size ; i++) {
	bsa->ae[i] = 0;
    }

    /* fprintf (stderr, "idio_bsa %zd: %p %p\n", n, bsa, bsa->ae);   */
    idio_bignums++;
    if (idio_bignums > idio_bignums_max) {
	idio_bignums_max = idio_bignums;
    }

    return bsa;
}

void idio_bsa_free (IDIO_BSA bsa)
{
    /* fprintf (stderr, "idio_bsa_free %zd: %p %p\n", bsa->size, bsa, bsa->ae);   */

    if (bsa->refs > 1) {
	bsa->refs--;
    } else {
	IDIO_GC_FREE (bsa->ae);
	IDIO_GC_FREE (bsa);
	idio_bignums--;
    }
}

static void idio_bsa_resize_by (IDIO_BSA bsa, size_t n)
{
    /* fprintf (stderr, "idio_bsa_resize: %p from %zd by %zd\n", bsa, bsa->size, n);    */
    bsa->size += n;
    if (bsa->size > bsa->avail) {
	bsa->ae = idio_realloc (bsa->ae, bsa->size * sizeof (IDIO_BS_T));
	bsa->avail = bsa->size;
    }

    IDIO_C_ASSERT (bsa->size < 200); /* reading pi with 61 sig digits */
}

IDIO_BS_T idio_bsa_get (IDIO_BSA bsa, size_t i)
{
    if (i >= bsa->size) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "bignum significand array access OOB: get %zd/%zd", i, bsa->size);

	/* notreached */
	return -1;
    }

    /* fprintf (stderr, "idio_bsa_get: %p %zd/%zd => %zd\n", bsa, i, bsa->size, bsa->ae[i]);  */
    return bsa->ae[i];
}

void idio_bsa_set (IDIO_BSA bsa, IDIO_BS_T v, size_t i)
{
    /* fprintf (stderr, "idio_bsa_set: %p %zd/%zd => %zd\n", bsa, i, bsa->size, v);    */
    if (i >= bsa->size) {
	/* one beyond the current usage is OK */
	/* fprintf (stderr, "idio_bsa_set: %p i >= bsa->size\n", bsa);   */
	if (bsa->size == i) {
	    /* fprintf (stderr, "idio_bsa_set: %p bsa->size == i\n", bsa);   */
	    idio_bsa_resize_by (bsa, 1);
	} else {
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "bignum significand array access OOB: set %zd/%zd", i, bsa->size);

	    /* notreached */
	    return;
	}
    }

    bsa->ae[i] = v;
}

void idio_bsa_shift (IDIO_BSA bsa)
{
    /* fprintf (stderr, "idio_bsa_shift: %p %zd\n", bsa, bsa->size);  */
    if (bsa->size) {
	size_t i;
	for (i = 1; i < bsa->size ; i++) {
	    bsa->ae[i-1] = bsa->ae[i];
	}
	bsa->size--;
    } else {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "bignum significand shift: zero length already");

	/* notreached */
	return;
    }
}

void idio_bsa_pop (IDIO_BSA bsa)
{
    /* fprintf (stderr, "idio_bsa_pop: %p %zd\n", bsa, bsa->size);  */
    if (bsa->size) {
	bsa->size--;
    } else {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "bignum significand pop: zero length already");

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

    /* fprintf (stderr, "idio_bsa_copy %zd: %p -> %p\n", bsa->size, bsa, bsac);  */
    return bsac;
}


void idio_bignum_dump (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    int64_t exp = IDIO_BIGNUM_EXP (bn);
    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);

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
	fprintf (stderr, "!");
    }

    int segs = al - 1;
    fprintf (stderr, " segs[%2zd%s]: ", al, (segs > IDIO_BIGNUM_SIG_SEGMENTS) ? "!" : "");
    int first = 1;

    /*
      To make visual comparison of numbers easier, always print out
	IDIO_BIGNUM_SIG_SEGMENTS even if the number doesn't
	have that many.  We can then compare columnally.  Much easier
	on the eye.
     */
    ptrdiff_t i;
    for (i = segs; i >= 0; i--) {
	if (i > al - 1) {
	    fprintf (stderr, "%*s ", IDIO_BIGNUM_DPW, "");
	} else {
	    char *fmt;
	    if (first) {
		first = 0;
		fmt = "%*zd ";
	    } else {
		fmt = "%0*zd ";
	    }

	    if (IDIO_BSA_AE (sig_a, i) > IDIO_BIGNUM_INT_SEG_LIMIT) {
		fprintf (stderr, "!");
	    }
	    fprintf (stderr, fmt, IDIO_BIGNUM_DPW, IDIO_BSA_AE (sig_a, i));
	}
    }
    fprintf (stderr, "e%" PRId64 "\n", exp);
}

/* bignum code from S9fES */
IDIO idio_bignum (int flags, IDIO_BS_T exp, IDIO_BSA sig_a)
{
    IDIO o = idio_gc_get (IDIO_TYPE_BIGNUM);

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

void idio_free_bignum (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    /* idio_gc_stats_free (sizeof (idio_bignum_t)); */

    idio_bsa_free (IDIO_BIGNUM_SIG (bn));
    /* IDIO_GC_FREE (bn->u.bignum); */
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

int64_t idio_bignum_int64_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	return 0;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = IDIO_BSA_SIZE (sig_a);

    if (al > 1) {
	IDIO fn = idio_bignum_to_fixnum (bn_i);
	if (idio_S_nil == fn) {
	    /*
	     * Grr! *shakes fist*
	     *
	     * INT64_MAX is 9223372036854775807, 19 digits long, just
	     * over the 64bit DPW yet small enough to fit into an
	     * int64_t -- and well over the 32bit DPW
	     */
	    if (IDIO_BIGNUM_INT64_WORDS == al) {
		IDIO_BS_T a1 = idio_bsa_get (sig_a, al - 1);

		if (a1 <= 9 &&
		    a1 >= -9) {
		    int64_t v = 0;
		    for (; al > 0 ; al--) {
		      v *= IDIO_BIGNUM_INT_SEG_LIMIT;
		      v += idio_bsa_get (sig_a, al - 1);
		    }

		    /*
		     * Check we haven't overflowed the C int64_t
		     */
		    if ((a1 < 0 &&
			 v < 0) ||
			(a1 >= 0 &&
			 v >= 0)) {

			/* idio_debug ("b->i64: %s ", bn); */
			/* fprintf (stderr, "%" PRId64 "\n", v); */
			return v;
		    }
		}
	    }
	    size_t size = 0;
	    char *bn_is = idio_bignum_as_string (bn_i, &size);
	    char em[BUFSIZ];
	    sprintf (em, "%s is too large for int64_t (%" PRId64 ")", bn_is, INT64_MAX);
	    IDIO_GC_FREE (bn_is);
	    idio_bignum_error_conversion (em, bn, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	} else {
	    return IDIO_FIXNUM_VAL (fn);
	}
    }

    return (int64_t) idio_bsa_get (sig_a, al - 1);
}

uint64_t idio_bignum_uint64_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	return 0;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = IDIO_BSA_SIZE (sig_a);

    if (al > 1) {
	IDIO fn = idio_bignum_to_fixnum (bn_i);
	if (idio_S_nil == fn) {
	    /*
	     * Grr! *shakes fist*
	     *
	     * UINT64_MAX is 18446744073709551615, 20 digits long,
	     * just over the default DPW yet small enough to fit into
	     * a uint64_t -- and well over the 32bit DPW
	     */
	    if (IDIO_BIGNUM_INT64_WORDS == al) {
		IDIO_BS_T a1 = idio_bsa_get (sig_a, al - 1);

		if (a1 <= 18 &&
		    a1 >= 0) {
		    uint64_t v = 0;
		    for (; al > 0 ; al--) {
		      v *= IDIO_BIGNUM_INT_SEG_LIMIT;
		      v += idio_bsa_get (sig_a, al - 1);
		    }

		    /*
		     * XXX how do we tell if we've overflowed?
		     */

		    /* idio_debug ("b->ui64: %s ", bn); */
		    /* fprintf (stderr, "%" PRIu64 "\n", v); */
		    return v;
		}
	    }
	    size_t size = 0;
	    char *bn_is = idio_bignum_as_string (bn_i, &size);
	    char em[BUFSIZ];
	    sprintf (em, "%s is too large for uint64_t (%" PRIu64 ")", bn_is, UINT64_MAX);
	    IDIO_GC_FREE (bn_is);
	    idio_bignum_error_conversion (em, bn, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	} else {
	    return IDIO_FIXNUM_VAL (fn);
	}
    }

    return (uint64_t) idio_bsa_get (sig_a, al - 1);
}

ptrdiff_t idio_bignum_ptrdiff_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	return 0;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = IDIO_BSA_SIZE (sig_a);

    if (al > 1) {
	IDIO fn = idio_bignum_to_fixnum (bn_i);
	if (idio_S_nil == fn) {
	    /*
	     * Grr! *shakes fist*
	     *
	     * LP64 PTRDIFF_MAX is 9223372036854775807, 19 digits
	     * long, just over the default DPW yet small enough to fit
	     * into an ptrdiff_t.
	     *
	     * LP32 PTRDIFF_MAX is 2147483647, 10 digits long, just
	     * over the default DPW yet small enough to fit into an
	     * ptrdiff_t.
	     */
	    if (IDIO_BIGNUM_PTRDIFF_WORDS == al) {
		IDIO_BS_T a1 = idio_bsa_get (sig_a, al - 1);

		if (a1 <= IDIO_BIGNUM_PTRDIFF_FIRST &&
		    a1 >= -IDIO_BIGNUM_PTRDIFF_FIRST) {
		    ptrdiff_t v = 0;
		    for (; al > 0 ; al--) {
		      v *= IDIO_BIGNUM_INT_SEG_LIMIT;
		      v += idio_bsa_get (sig_a, al - 1);
		    }

		    /*
		     * Check we haven't overflowed the C ptrdiff_t
		     */
		    if ((a1 < 0 &&
			 v < 0) ||
			(a1 >= 0 &&
			 v >= 0)) {

			/* idio_debug ("b->pd: %s ", bn); */
			/* fprintf (stderr, "%tu\n", v); */
			return v;
		    }
		}
	    }
	    size_t size = 0;
	    char *bn_is = idio_bignum_as_string (bn_i, &size);
	    char em[BUFSIZ];
	    sprintf (em, "%s is too large for ptrdiff_t (%td)", bn_is, (ptrdiff_t) PTRDIFF_MAX);
	    IDIO_GC_FREE (bn_is);
	    idio_bignum_error_conversion (em, bn, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	} else {
	    return IDIO_FIXNUM_VAL (fn);
	}
    }

    return (ptrdiff_t) idio_bsa_get (sig_a, al - 1);
}

intptr_t idio_bignum_intptr_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	return 0;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = IDIO_BSA_SIZE (sig_a);

    if (al > 1) {
	IDIO fn = idio_bignum_to_fixnum (bn_i);
	if (idio_S_nil == fn) {
	    /*
	     * Grr! *shakes fist*
	     *
	     * LP64 INTPTR_MAX is 9223372036854775807, 19 digits long
	     * -- just over the default 64bit DPW yet small enough to
	     * fit into an intptr_t.
	     *
	     * LP32 INTPTR_MAX is 2147483647, 10 digits long, just
	     * over the default DPW yet small enough to fit into an
	     * intptr_t.
	     */
	    if (IDIO_BIGNUM_INTPTR_WORDS == al) {
		IDIO_BS_T a1 = idio_bsa_get (sig_a, al - 1);

		if (a1 <= IDIO_BIGNUM_INTPTR_FIRST &&
		    a1 >= -IDIO_BIGNUM_INTPTR_FIRST) {
		    intptr_t v = 0;
		    for (; al > 0 ; al--) {
		      v *= IDIO_BIGNUM_INT_SEG_LIMIT;
		      v += idio_bsa_get (sig_a, al - 1);
		    }

		    /*
		     * Check we haven't overflowed the C intptr_t
		     */
		    if ((a1 < 0 &&
			 v < 0) ||
			(a1 >= 0 &&
			 v >= 0)) {

			/* idio_debug ("b->ip: %s ", bn); */
			/* fprintf (stderr, "%" PRIdPTR "\n", v); */
			return v;
		    }
		}
	    }
	    size_t size = 0;
	    char *bn_is = idio_bignum_as_string (bn_i, &size);
	    char em[BUFSIZ];
	    sprintf (em, "%s is too large for intptr_t (%" PRIdPTR ")", bn_is, (intptr_t) INTPTR_MAX);
	    IDIO_GC_FREE (bn_is);
	    idio_bignum_error_conversion (em, bn, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	} else {
	    return IDIO_FIXNUM_VAL (fn);
	}
    }

    return (intptr_t) idio_bsa_get (sig_a, al - 1);
}

intmax_t idio_bignum_intmax_value (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	return 0;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = IDIO_BSA_SIZE (sig_a);

    if (al > 1) {
	IDIO fn = idio_bignum_to_fixnum (bn_i);
	if (idio_S_nil == fn) {
	    /*
	     * Grr! *shakes fist*
	     *
	     * LP64 INTMAX_MAX is 9223372036854775807, 19 digits long
	     * -- just over the default DPW yet small enough to fit
	     * into an intmax_t.
	     *
	     * LP32 INTMAX_MAX is 2147483647, 10 digits long, just
	     * over the default DPW yet small enough to fit into an
	     * intmax_t.
	     */
	    if (IDIO_BIGNUM_INTMAX_WORDS == al) {
		IDIO_BS_T a1 = idio_bsa_get (sig_a, al - 1);

		if (a1 <= IDIO_BIGNUM_INTMAX_FIRST &&
		    a1 >= -IDIO_BIGNUM_INTMAX_FIRST) {
		    intmax_t v = 0;
		    for (; al > 0 ; al--) {
		      v *= IDIO_BIGNUM_INT_SEG_LIMIT;
			if (v < 0) {
			    v -= idio_bsa_get (sig_a, al - 1);
			} else {
			    v += idio_bsa_get (sig_a, al - 1);
			}
		    }

		    /*
		     * Check we haven't overflowed the C intmax_t
		     */
		    if ((a1 < 0 &&
			 v < 0) ||
			(a1 >= 0 &&
			 v >= 0)) {

			/* idio_debug ("b->im: %s ", bn); */
			/* fprintf (stderr, "%jd\n", v); */
			return v;
		    }
		}
	    }
	    size_t size = 0;
	    char *bn_is = idio_bignum_as_string (bn_i, &size);
	    char em[BUFSIZ];
	    sprintf (em, "%s is too large for intmax_t (%jd)", bn_is, (intmax_t) INTMAX_MAX);
	    IDIO_GC_FREE (bn_is);
	    idio_bignum_error_conversion (em, bn, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	} else {
	    return IDIO_FIXNUM_VAL (fn);
	}
    }

    return (intmax_t) idio_bsa_get (sig_a, al - 1);
}

IDIO idio_bignum_to_fixnum (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    if (! IDIO_BIGNUM_INTEGER_P (bn)) {
	return idio_S_nil;
    }

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	return idio_S_nil;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = IDIO_BSA_SIZE (sig_a);

    if (al * IDIO_BIGNUM_DPW > IDIO_BIGNUM_MDPW) {
	return idio_S_nil;
    }

    intptr_t iv = 0;
    int neg = 0;

    intptr_t ai;
    for (ai = al -1 ; ai >= 0 ; ai--) {
	iv *= IDIO_BIGNUM_INT_SEG_LIMIT;
	IDIO_BS_T v = idio_bsa_get (sig_a, ai);
	if (v < 0) {
	    IDIO_C_ASSERT (ai == al - 1);
	    iv += -v;
	    neg = 1;
	} else {
	    iv += v;
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

    return idio_S_nil;
}

IDIO idio_bignum_abs (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bnc = idio_bignum_copy (bn);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bnc);
    size_t al = IDIO_BSA_SIZE (sig_a);

    IDIO_BS_T i = idio_bsa_get (sig_a, al - 1);
    idio_bsa_set (sig_a, llabs (i), al - 1);

    return bnc;
}

int idio_bignum_negative_p (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = IDIO_BSA_SIZE (sig_a);

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

    IDIO_BS_T i = idio_bsa_get (sig_a, al - 1);
    idio_bsa_set (sig_a, -i, al - 1);

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
	return 1;
    }

    if (!na &&
	nb) {
	return 0;
    }

    size_t al = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (a));
    size_t bl = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (b));

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

    intptr_t i;
    for (i = al - 1; i >= 0 ; i--) {
	IDIO_BS_T iaa = idio_bsa_get (sig_aa, i);
	IDIO_BS_T iab = idio_bsa_get (sig_ab, i);

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

IDIO idio_bignum_shift_left (IDIO a, int fill)
{
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (bignum, a);

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_BSA ra = idio_bsa (al);

    /* idio_debug ("bignum-shift-left: a %s\n", a); */
    /* idio_bignum_dump (a); */
    size_t ai;

    int carry = fill;

    for (ai = 0; ai < al; ai++) {
	IDIO_BS_T i = idio_bsa_get (sig_a, ai);
	IDIO_BS_T r;

	if (ai < (al - 1) &&
	    i < 0) {
	    idio_error_C ("non-last bignum segment < 0", idio_S_nil, IDIO_C_FUNC_LOCATION ());

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
    IDIO_BSA ra;

    /* repeated shift_rights result in an empty array! */
    if (al) {
	ra = idio_bsa (al);
    } else {
	al++;
	ra = idio_bsa (al);

	/* plonk this int64 into sig_a as that's what we're about to
	get_index from */
	idio_bsa_set (sig_a, 0, 0);
    }

    intptr_t ai;

    int carry = 0;

    for (ai = al - 1; ai >= 0; ai--) {
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
	IDIO_BS_T v = idio_bsa_get (ra, al - 1);
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
	idio_bignum_error_divide_by_zero (IDIO_C_FUNC_LOCATION ());

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
	r_div = idio_bignum_negate (r_div);
    }

    if (na) {
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
	return idio_S_nil;
    }

    return bn_i;
}

IDIO idio_bignum_real (int flags, IDIO_BS_T exp, IDIO_BSA sig_a)
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
	    return idio_S_nil;
	}

	IDIO bn_i = idio_bignum_copy_to_integer (bns);

	if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
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
	flags |= IDIO_BIGNUM_FLAG_REAL_NEGATIVE;
    }

    IDIO r = idio_bignum_real (flags, IDIO_BIGNUM_EXP (bn), IDIO_BIGNUM_SIG (bn));

    return r;
}

/*
  Remove trailing zeroes: 123000 => 123e3
  Shift decimal place to end: 1.23e0 => 123e-2

  Limit to IDIO_BIGNUM_SIG_MAX_DIGITS, a loss of precision =>
	IDIO_BIGNUM_FLAG_REAL_INEXACT
 */
IDIO idio_bignum_normalize (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BS_T exp = IDIO_BIGNUM_EXP (bn);
    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);

    /* significand-only part */
    IDIO bn_s = idio_bignum_copy (bn);

    size_t digits = idio_bignum_count_digits (sig_a);
    int inexact = IDIO_BIGNUM_REAL_INEXACT_P (bn);

    IDIO ibsr;

    while (digits > IDIO_BIGNUM_SIG_MAX_DIGITS) {
	ibsr = idio_bignum_shift_right (bn_s);

	if (! idio_bignum_zero_p (IDIO_PAIR_T (ibsr))) {
	    inexact = IDIO_BIGNUM_FLAG_REAL_INEXACT;
	}

	bn_s = IDIO_PAIR_H (ibsr);
	digits--;
	exp++;
    }

    while (! idio_bignum_zero_p (bn_s)) {
	ibsr = idio_bignum_shift_right (bn_s);

	if (! idio_bignum_zero_p (IDIO_PAIR_T (ibsr))) {
	    break;
	}

	bn_s = IDIO_PAIR_H (ibsr);
	exp++;
    }

    if (idio_bignum_zero_p (bn_s)) {
	exp = 0;
    }

    /*
     * S9fES checks for over/under-flow in exp wrt
     * IDIO_BIGNUM_DPW. Not sure that's applicable here.
     */

    IDIO r = idio_bignum_real (IDIO_BIGNUM_FLAGS(bn) | inexact, exp, IDIO_BIGNUM_SIG (bn_s));

    return r;
}

IDIO idio_bignum_to_real (IDIO bn)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO_BS_T exp = 0;

    IDIO bnc = idio_bignum_copy (bn);
    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bnc);
    size_t al = IDIO_BSA_SIZE (sig_a);
    IDIO_BS_T i = idio_bsa_get (sig_a, al - 1);
    idio_bsa_set (sig_a, llabs (i), al - 1);

    /*
      A much cheaper and lossier truncation of precision.  Do it by whole segments.

      With DPW of 3 and 1 seg then 3141 would become 3e3
     */
    size_t nseg = al;
    int inexact = 0;

    IDIO r;

    if (nseg > IDIO_BIGNUM_SIG_SEGMENTS) {
	size_t nshift = (nseg - IDIO_BIGNUM_SIG_SEGMENTS);
	size_t i;
	for (i = 0; i < nshift; i++) {
	    if (sig_a->ae[0]) {
		inexact = IDIO_BIGNUM_FLAG_REAL_INEXACT;
	    }
	    idio_bsa_shift (sig_a);
	}
	exp = nshift * IDIO_BIGNUM_DPW;
    }

    int flags = inexact;
    if (idio_bignum_negative_p (bn)) {
	flags |= IDIO_BIGNUM_FLAG_REAL_NEGATIVE;
    }

    r = idio_bignum_real (flags, exp, sig_a);

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
	return 0;
    }

    IDIO_BS_T ia = idio_bsa_get (sig_a, 0);

    return (0 == ia);
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

    if (ral != rbl) {
	return 0;
    }

    intptr_t i;

    for (i = ral - 1; i >= 0; i--) {
	IDIO_BS_T ia = idio_bsa_get (ras, i);
	IDIO_BS_T ib = idio_bsa_get (rbs, i);

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
IDIO idio_bignum_scale_significand (IDIO bn, IDIO_BS_T desired_exp, size_t max_size)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    size_t digits = idio_bignum_count_digits (IDIO_BIGNUM_SIG (bn));

    /* is there room to scale within the desired_exp (and max_size)? */
    if ((max_size - digits) < (IDIO_BIGNUM_EXP (bn) - desired_exp)) {
	return idio_S_nil;
    }

    IDIO bnc = idio_bignum_copy (bn);

    IDIO_BS_T exp = IDIO_BIGNUM_EXP (bn);
    while (exp > desired_exp) {
	bnc = idio_bignum_shift_left (bnc, 0);
	exp--;
    }

    return idio_bignum_real (IDIO_BIGNUM_FLAGS(bn), exp, IDIO_BIGNUM_SIG (bnc));
}

int idio_bignum_real_lt_p (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    /* idio_debug ("bignum_real_lt: %s", a);  */
    /* idio_debug (" < %s == ", b);  */

    if (IDIO_BIGNUM_INTEGER_P (a) &&
	IDIO_BIGNUM_INTEGER_P (b)) {
	return idio_bignum_lt_p (a, b);
    }

    IDIO ra = a;

    if (IDIO_BIGNUM_INTEGER_P (a)) {
	ra = idio_bignum_to_real (a);
    }

    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (ra) &&
	IDIO_BIGNUM_REAL_POSITIVE_P (rb)) {
	return 1;
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (rb) &&
	IDIO_BIGNUM_REAL_POSITIVE_P (ra)) {
	return 0;
    }

    if (IDIO_BIGNUM_REAL_POSITIVE_P (ra) &&
	idio_bignum_real_zero_p (rb)) {
	return 0;
    }

    /* XXX S9fES has real_positive_p (a) bug?? */
    if (IDIO_BIGNUM_REAL_POSITIVE_P (rb) &&
	idio_bignum_real_zero_p (ra)) {
	return 1;
    }

    int neg = IDIO_BIGNUM_REAL_NEGATIVE_P (ra);

    /*
     * dpa/dpb can be negative is their EXP is very negative
     */
    intptr_t dpa = idio_bignum_count_digits (IDIO_BIGNUM_SIG (ra)) + IDIO_BIGNUM_EXP (ra);
    intptr_t dpb = idio_bignum_count_digits (IDIO_BIGNUM_SIG (rb)) + IDIO_BIGNUM_EXP (rb);

    if (dpa < dpb) {
	return neg ? 0 : 1;
    }

    if (dpa > dpb) {
	return neg ? 1 : 0;
    }

    if (IDIO_BIGNUM_EXP (ra) < IDIO_BIGNUM_EXP (rb)) {
	rb = idio_bignum_scale_significand (rb, IDIO_BIGNUM_EXP (ra), IDIO_BIGNUM_SIG_MAX_DIGITS);

	if (idio_S_nil == rb) {
	    return neg ? 0 : 1;
	}
    }

    if (IDIO_BIGNUM_EXP (ra) > IDIO_BIGNUM_EXP (rb)) {
	ra = idio_bignum_scale_significand (ra, IDIO_BIGNUM_EXP (rb), IDIO_BIGNUM_SIG_MAX_DIGITS);

	if (idio_S_nil == ra) {
	    return neg ? 0 : 1;
	}
    }

    size_t ral = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (ra));
    size_t rbl = IDIO_BSA_SIZE (IDIO_BIGNUM_SIG (rb));

    if (ral < rbl) {
	return 1;
    }

    if (ral > rbl) {
	return 0;
    }

    intptr_t i;

    for (i = ral - 1 ; i >= 0 ; i--) {
	IDIO_BS_T ia = idio_bsa_get (IDIO_BIGNUM_SIG (ra), i);
	IDIO_BS_T ib = idio_bsa_get (IDIO_BIGNUM_SIG (rb), i);

	if (ia < ib) {
	    return neg ? 0 : 1;
	}

	if (ia > ib) {
	    return neg ? 1 : 0;
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

    IDIO ra = a;

    if (IDIO_BIGNUM_INTEGER_P (a)) {
	ra = idio_bignum_to_real (a);
    }

    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    int inexact = IDIO_BIGNUM_REAL_INEXACT_P (ra) | IDIO_BIGNUM_REAL_INEXACT_P (rb);

    if (IDIO_BIGNUM_EXP (ra) < IDIO_BIGNUM_EXP (rb)) {
	rb = idio_bignum_scale_significand (rb, IDIO_BIGNUM_EXP (ra), IDIO_BIGNUM_SIG_MAX_DIGITS * 2);
    } else if (IDIO_BIGNUM_EXP (ra) > IDIO_BIGNUM_EXP (rb)) {
	ra = idio_bignum_scale_significand (ra, IDIO_BIGNUM_EXP (rb), IDIO_BIGNUM_SIG_MAX_DIGITS * 2);
    }

    if (idio_S_nil == ra ||
	idio_S_nil == rb) {

	if (idio_bignum_real_lt_p (a, b)) {
	    return idio_bignum_real_to_inexact (b);
	} else {
	    return idio_bignum_real_to_inexact (a);
	}
    }

    IDIO_BS_T exp = IDIO_BIGNUM_EXP (ra);
    int na = IDIO_BIGNUM_REAL_NEGATIVE_P (ra);
    int nb = IDIO_BIGNUM_REAL_NEGATIVE_P (rb);

    IDIO ra_i = idio_bignum_copy_to_integer (ra);

    if (na) {
	ra_i = idio_bignum_negate (ra_i);
    }

    IDIO rb_i = idio_bignum_copy_to_integer ( rb);

    if (nb) {
	rb_i = idio_bignum_negate (rb_i);
    }

    IDIO r_i = idio_bignum_add (ra_i, rb_i);

    int flags = inexact;
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
	return ra;
    }

    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    if (idio_bignum_real_zero_p (rb)) {
	return rb;
    }

    int inexact = IDIO_BIGNUM_REAL_INEXACT_P (ra) | IDIO_BIGNUM_REAL_INEXACT_P (rb);

    int neg = IDIO_BIGNUM_REAL_NEGATIVE_P (ra) != IDIO_BIGNUM_REAL_NEGATIVE_P (rb);

    IDIO_BS_T expa = IDIO_BIGNUM_EXP (ra);
    IDIO_BS_T expb = IDIO_BIGNUM_EXP (rb);

    IDIO ra_i = idio_bignum_copy_to_integer (ra);

    IDIO rb_i = idio_bignum_copy_to_integer (rb);

    IDIO_BS_T exp = expa + expb;

    IDIO r_i = idio_bignum_multiply (ra_i, rb_i);

    int flags = inexact | (neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0);

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
	IDIO i0 = idio_bignum_integer_intmax_t (0);

	IDIO r = idio_bignum_real (0, 0, IDIO_BIGNUM_SIG (i0));

	return r;
    }

    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    int inexact = IDIO_BIGNUM_REAL_INEXACT_P (ra) | IDIO_BIGNUM_REAL_INEXACT_P (rb);

    int neg = IDIO_BIGNUM_REAL_NEGATIVE_P (ra) != IDIO_BIGNUM_REAL_NEGATIVE_P (rb);

    IDIO_BS_T expa = IDIO_BIGNUM_EXP (ra);
    IDIO_BS_T expb = IDIO_BIGNUM_EXP (rb);

    IDIO ra_i = idio_bignum_copy_to_integer (ra);
    IDIO rb_i = idio_bignum_copy_to_integer (rb);

    if (idio_bignum_zero_p (rb)) {

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
	expa--;
    }

    IDIO_BS_T exp = expa - expb;
    IDIO ibd = idio_bignum_divide (ra_i, rb_i);
    IDIO r_i = IDIO_PAIR_H (ibd);

    if (! idio_bignum_zero_p (IDIO_PAIR_T (ibd))) {
	inexact = IDIO_BIGNUM_FLAG_REAL_INEXACT;
    }

    int flags = inexact | (neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0);

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

    int prec = 0;
    if (idio_S_nil != idio_print_conversion_precision_sym) {
	IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
					      idio_Idio_module,
					      IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcp) {
	    if (idio_isa_fixnum (ipcp)) {
		prec = IDIO_FIXNUM_VAL (ipcp);
	    } else {
		idio_error_param_type ("fixnum", ipcp, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }

    size_t al = IDIO_BSA_SIZE (sig_a);
    intptr_t i;
    for (i = al - 1; i >= 0; i--) {
	IDIO_BS_T v = idio_bsa_get (sig_a, i);
	char buf[BUFSIZ];
	char fmt[BUFSIZ];
	if (i == al - 1) {
	    sprintf (fmt, "%%zd");
	    sprintf (buf, fmt, v);

	    size_t bn_digits = strlen (buf) + i * IDIO_BIGNUM_DPW;
	    if (prec > bn_digits) {
		int pad = prec - bn_digits;
		char pads[pad + 1];
		sprintf (fmt, "%%.%dd", pad);
		sprintf (pads, fmt, 0);
		idio_strcat (s, sizep, pads, pad);
	    }
	} else {
	    sprintf (fmt, "%%0%dzd", IDIO_BIGNUM_DPW);
	    sprintf (buf, fmt, v);
	}
	size_t buf_size = strlen (buf);
	s = idio_strcat (s, sizep, buf, buf_size);
    }

    return s;
}

char *idio_bignum_expanded_real_as_string (IDIO bn, IDIO_BS_T exp, int digits, int neg, size_t *sizep)
{
    IDIO_ASSERT (bn);
    IDIO_TYPE_ASSERT (bignum, bn);

    char *s = idio_alloc (1);
    *s = '\0';

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	IDIO_STRCAT (s, sizep, "-");
    }

    IDIO_BS_T dp_offset = exp + digits;

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

    intptr_t ai;
    for (ai = al - 1; ai >= 0; ai--) {
	IDIO_BS_T v = idio_bsa_get (sig_a, ai);
	char *vs;
	if (IDIO_ASPRINTF (&vs, "%" PRIdPTR, v) == -1) {
	    idio_error_alloc ("asprintf");

	    /* notreached */
	    return NULL;
	}
	size_t vs_size = strlen (vs);
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
	return NULL;
    }

    IDIO_BSA sig_a = IDIO_BIGNUM_SIG (bn);
    size_t digits = idio_bignum_count_digits (sig_a);
    IDIO_BS_T exp = IDIO_BIGNUM_EXP (bn);

    if (0 && (exp + digits) > -4 &&
	(exp + digits) <= 9) {
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
		     * A generic: printf "%s" e
		     */
		    format = IDIO_BIGNUM_CONVERSION_FORMAT_e;
		    break;
		default:
		    fprintf (stderr, "bignum-as-string: unexpected conversion format: %c (%#x).  Using 'e'.\n", (int) f, (int) f);
		    format = IDIO_BIGNUM_CONVERSION_FORMAT_e;
		    break;
		}
	    } else {
		idio_error_param_type ("unicode", ipcf, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }

    /*
     * The default precision for both e and f formats is 6
     */
    int prec = 6;
    if (idio_S_nil != idio_print_conversion_precision_sym) {
	IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
					      idio_Idio_module,
					      IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcp) {
	    if (idio_isa_fixnum (ipcp)) {
		prec = IDIO_FIXNUM_VAL (ipcp);
	    } else {
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
	IDIO_STRCAT (s, sizep, "-");
    }

    size_t al = IDIO_BSA_SIZE (sig_a);
    intptr_t i = al - 1;
    IDIO_BS_T v = idio_bsa_get (sig_a, i);

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
	    char vs[IDIO_BIGNUM_DPW + 1];
	    sprintf (vs, "%" PRIdPTR, v);
	    char *vs_rest = vs + 1;

	    size_t vs_size = strlen (vs);
	    s = idio_strcat (s, sizep, vs, 1);

	    if (prec) {
		IDIO_STRCAT (s, sizep, ".");
	    }

	    size_t vs_rest_size = strlen (vs_rest);
	    if (vs_rest_size) {
		s = idio_strcat (s, sizep, vs_rest, vs_rest_size);
	    } else {
		if (0 == i) {
		    IDIO_STRCAT (s, sizep, "0");
		}
	    }

	    for (i--; i >= 0; i--) {
		v = idio_bsa_get (sig_a, i);
		sprintf (vs, "%0*" PRIdPTR, IDIO_BIGNUM_DPW, v);
		vs_size = strlen (vs);
		s = idio_strcat (s, sizep, vs, vs_size);
	    }

	    IDIO_STRCAT (s, sizep, "e");
	    v = exp + digits - 1;
	    sprintf (vs, "%+" PRIdPTR, v);
	    vs_size = strlen (vs);
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
	    char vs[IDIO_BIGNUM_DPW + 1];
	    sprintf (vs, "%" PRIdPTR, v);
	    char *vs_rest = vs + 1;

	    size_t vs_size = strlen (vs);
	    s = idio_strcat (s, sizep, vs, 1);

	    if (prec) {
		IDIO_STRCAT (s, sizep, ".");
	    }

	    size_t vs_rest_size = strlen (vs_rest);
	    size_t vs_rest_prec = vs_rest_size;
	    if (prec < vs_rest_prec) {
		vs_rest_prec = prec;
	    }
	    if (vs_rest_size) {
		s = idio_strcat (s, sizep, vs_rest, vs_rest_prec);
	    }
	    prec -= vs_rest_prec;

	    for (i--; i >= 0; i--) {
		v = idio_bsa_get (sig_a, i);
		sprintf (vs, "%0*" PRIdPTR, IDIO_BIGNUM_DPW, v);
		vs_size = strlen (vs);
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
		sprintf (pads, "%.*d", pad, 0);
		s = idio_strcat (s, sizep, pads, pad);
	    }

	    IDIO_STRCAT (s, sizep, "e");
	    v = exp + digits - 1;
	    sprintf (vs, "%+03" PRIdPTR, v);
	    vs_size = strlen (vs);
	    s = idio_strcat (s, sizep, vs, vs_size);
	}
	break;
    case IDIO_BIGNUM_CONVERSION_FORMAT_f:
	{
	    char vs[IDIO_BIGNUM_DPW + 1];
	    sprintf (vs, "%" PRIdPTR, v);
	    size_t vs_size = strlen (vs);
	    int pre_dp_digits = digits + exp;

	    if (exp >= 0) {
		s = idio_strcat (s, sizep, vs, vs_size);
		int pad = exp;
		if (pad > 0) {
		    char pads[pad + 1];
		    sprintf (pads, "%.*d", pad, 0);
		    s = idio_strcat (s, sizep, pads, pad);
		}
		if (prec) {
		    IDIO_STRCAT (s, sizep, ".");
		    pad = prec;
		    char pads[pad + 1];
		    sprintf (pads, "%.*d", pad, 0);
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
			int pad = - pre_dp_digits;
			if (prec < pad) {
			    pad = prec;
			}
			char pads[pad + 1];
			sprintf (pads, "%.*d", pad, 0);
			s = idio_strcat (s, sizep, pads, pad);
			prec -= pad;
		    }

		    size_t vs_rest_size = strlen (vs_rest);
		    size_t vs_rest_prec = vs_rest_size;
		    if (prec < vs_rest_prec) {
			vs_rest_prec = prec;
		    }
		    if (vs_rest_size) {
			s = idio_strcat (s, sizep, vs_rest, vs_rest_prec);
		    }
		    prec -= vs_rest_prec;

		    for (i--; i >= 0; i--) {
			v = idio_bsa_get (sig_a, i);
			sprintf (vs, "%0*" PRIdPTR, IDIO_BIGNUM_DPW, v);
			vs_size = strlen (vs);
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
			sprintf (pads, "%.*d", pad, 0);
			s = idio_strcat (s, sizep, pads, pad);
		    }
		}
	    }
	    break;
	}
	break;
    default:
	{
	    fprintf (stderr, "bignum-as-string: unimplemented conversion format: %c (%#x)\n", (int) format, (int) format);
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "bignum-as-string unimplemented conversion format");

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
	char *s = idio_alloc (strlen (IDIO_BIGNUM_NAN) + 1);
	strcpy (s, IDIO_BIGNUM_NAN);

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

    IDIO_BS_T v = idio_bsa_get (sig_a, al - 1);

    size_t d = 0;
    while (v) {
	v /= 10;
	d++;
    }

    if (0 == d) {
	d++;
    }

    d += ((al - 1) * IDIO_BIGNUM_DPW);

    return d;
}

char *idio_bignum_C_without_inexact (char *nums)
{
    IDIO_C_ASSERT (nums);

    char *buf = idio_alloc (strlen (nums) + 1);
    strcpy (buf, nums);

    char *s = buf;
    while (*s) {
	if ('#' == *s) {
	    *s = '5';
	}
	s++;
    }

    return buf;
}

IDIO idio_bignum_integer_C (char *nums, int req_exact)
{
    IDIO_C_ASSERT (nums);

    char *buf = idio_bignum_C_without_inexact (nums);
    int is_exact = (NULL == strchr (nums, '#'));

    char *s = buf;
    int sign = 1;
    if ('-' == *s) {
	sign = -1;
	s++;
    } else if ('+' == *s) {
	s++;
    }

    int nl = strlen (s);
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
	    IDIO_GC_FREE (buf);
	    char em[BUFSIZ];
	    sprintf (em, "strtoll (%s) = %lld", nums, i);
	    idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (end == nums) {
	    IDIO_GC_FREE (buf);
	    char em[BUFSIZ];
	    sprintf (em, "strtoll (%s): No digits?", nums);
	    idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if ('\0' != *end) {
	    IDIO_GC_FREE (buf);
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "strtoll (%s) = %ld", nums, i);

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

    IDIO_GC_FREE (buf);

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

IDIO idio_bignum_real_C (char *nums)
{
    IDIO_C_ASSERT (nums);

    IDIO sig_bn = idio_bignum_integer_intmax_t (0);

    IDIO_BS_T exp = 0;
    char *s = nums;
    int neg = 0;

    if ('+' == *s) {
	s++;
    } else if ('-' == *s) {
	neg = 1;
	s++;
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
    }

    /*
     * also unused in S9fES string_to_real

    int j = idio_array_size (IDIO_BIGNUM_SIG (sig_bn));
    */

    if (IDIO_BIGNUM_EXP_CHAR (*s)) {
	s++;
	IDIO n = idio_bignum_integer_C (s, 1);
	exp += idio_bignum_int64_value (n);
    }

    /* remove leading zeroes */
    IDIO_BSA ra = IDIO_BIGNUM_SIG (sig_bn);
    size_t rl = IDIO_BSA_SIZE (ra);
    IDIO_BS_T ir = idio_bsa_get (ra, rl - 1);
    while (0 == ir &&
	   rl > 1) {
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

IDIO idio_bignum_C (char *nums)
{
    IDIO_C_ASSERT (nums);

    char *s = nums;
    while (*s) {
	if ('.' == *s ||
	    IDIO_BIGNUM_EXP_CHAR (*s)) {
	    return idio_bignum_real_C (nums);
	}
	s++;
    }

    return idio_bignum_integer_C (nums, 0);
}

IDIO idio_bignum_primitive_add (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_bignum_integer_intmax_t (0);

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
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

    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
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
	    idio_bignum_error_divide_by_zero (IDIO_C_FUNC_LOCATION ());

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

    IDIO_BS_T exp = IDIO_BIGNUM_EXP (bn);

    if (exp >= 0) {
	r = bn;
    } else {
	IDIO bn_i = idio_bignum_integer (IDIO_BIGNUM_SIG (bn));

	while (exp < 0) {
	    IDIO ibsr = idio_bignum_shift_right (bn_i);
	    bn_i = idio_list_head (ibsr);
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

    a = idio_bignum_integer_argument (a);
    b = idio_bignum_integer_argument (b);

    IDIO ibd = idio_bignum_divide (a, b);

    /* idio_debug ("bignum_quotient: %s\n", ibd); */

    return IDIO_PAIR_H (ibd);
}

IDIO idio_bignum_primitive_remainder (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);
    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    a = idio_bignum_integer_argument (a);
    b = idio_bignum_integer_argument (b);

    IDIO ibd = idio_bignum_divide (a, b);

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

IDIO idio_bignum_primitive_gt (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
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

IDIO idio_bignum_primitive_ge (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
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

IDIO idio_bignum_primitive_eq (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    IDIO r = idio_list_head (args);
    args = idio_list_tail (args);

    while (idio_S_nil != args) {
	IDIO h = idio_list_head (args);

        if (! idio_isa_bignum (h)) {
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

int idio_realp (IDIO n)
{
    IDIO_ASSERT (n);

    if (idio_isa_bignum (n) &&
	IDIO_BIGNUM_REAL_P (n)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1 ("bignum?", bignump, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO r = idio_S_false;

    if (idio_isa_bignum (n)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("real?", realp, (IDIO n))
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
	IDIO_VERIFY_PARAM_TYPE (bignum, n);			\
    }

IDIO_DEFINE_PRIMITIVE1 ("exact?", exactp, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO_BIGNUM_FIXNUM_TYPE (n);

    IDIO r = idio_S_false;

    if (idio_isa_fixnum (n)) {
	r = idio_S_true;
    } else if (IDIO_BIGNUM_INTEGER_P (n)) {
	r = idio_S_true;
    } else if (! IDIO_BIGNUM_REAL_INEXACT_P (n)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("inexact?", inexactp, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO_BIGNUM_FIXNUM_TYPE (n);

    IDIO r = idio_S_false;

    if (! idio_isa_fixnum (n)) {
	if (! IDIO_BIGNUM_INTEGER_P (n)) {
	    if (IDIO_BIGNUM_REAL_INEXACT_P (n)) {
		r = idio_S_true;
	    }
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("exact->inexact", exact2inexact, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO_BIGNUM_FIXNUM_TYPE (n);

    if (idio_isa_fixnum (n)) {
	n = idio_bignum_integer_intmax_t (IDIO_FIXNUM_VAL (n));
    }

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

IDIO_DEFINE_PRIMITIVE1 ("inexact->exact", inexact2exact, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO_BIGNUM_FIXNUM_TYPE (n);

    IDIO r = idio_S_unspec;

    if (idio_isa_fixnum (n)) {
	r = n;
    } else if (IDIO_BIGNUM_INTEGER_P (n)) {
	r = n;
    } else {
        r = idio_bignum_real_to_integer (n);
	if (idio_S_nil == r) {
	    r = idio_bignum_real_to_exact (n);
	}
    }

    IDIO fn = idio_bignum_to_fixnum (r);
    if (idio_S_nil != fn) {
	r = fn;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("mantissa", mantissa, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO_BIGNUM_FIXNUM_TYPE (n);

    if (idio_isa_fixnum (n)) {
	return n;
    }

    IDIO r = idio_S_unspec;

    if (IDIO_BIGNUM_INTEGER_P (n)) {
        r = n;
    } else {
	r = idio_bignum_integer (IDIO_BIGNUM_SIG (n));

	if (IDIO_BIGNUM_REAL_NEGATIVE_P (n)) {
	    r = idio_bignum_negate (r);
	}
    }

    IDIO fn = idio_bignum_to_fixnum (r);
    if (idio_S_nil != fn) {
	r = fn;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("exponent", exponent, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO_BIGNUM_FIXNUM_TYPE (n);

    IDIO r = idio_S_unspec;

    if (IDIO_BIGNUM_INTEGER_P (n)) {
        r = idio_fixnum (0);
    } else {
	intptr_t exp = IDIO_BIGNUM_EXP (n);
	r = idio_integer (exp);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("bignum-dump", bignum_dump, (IDIO n))
{
    IDIO_ASSERT (n);

    IDIO_TYPE_ASSERT (bignum, n);

    idio_bignum_dump (n);

    return idio_S_unspec;
}

void idio_init_bignum ()
{
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
