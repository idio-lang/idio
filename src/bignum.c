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
 * bignum.c
 * 
 */

#include "idio.h"

void idio_bignum_dump (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);
    
    int64_t exp = IDIO_BIGNUM_EXP (bn);	    
    IDIO sig_a = IDIO_BIGNUM_SIG (bn);	    
    size_t al = idio_array_size (sig_a);
    int64_t i;
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

    int segs = al - 1;
    fprintf (stderr, " a[%2zd%s]: ", al, (segs > IDIO_BIGNUM_SIG_SEGMENTS) ? "!" : "");
    int first = 1;

    /*
      To make visual comparison of numbers easier, always print out
	IDIO_BIGNUM_SIG_SEGMENTS even if the number doesn't
	have that many.  We can then compare columnally.  Much easier
	on the eye.
     */
    for (i = segs; i >= 0; i--) {
	if (i > al - 1) {
	    fprintf (stderr, "%*s ", IDIO_BIGNUM_DPW, "");
	} else {
	    char *fmt;
	    if (first) {
		first = 0;
		fmt = "%*ld ";
	    } else {
		fmt = "%0*ld ";
	    }
	    
	    fprintf (stderr, fmt, IDIO_BIGNUM_DPW, IDIO_C_TYPE_INT64 (IDIO_ARRAY_AE (sig_a, i)));
	}
    }
    fprintf (stderr, "e%zd\n", exp);
}

/* bignum code from S9fES */
IDIO idio_bignum (int flags, int64_t exp, IDIO sig_a)
{
    IDIO_ASSERT (sig_a);

    IDIO_TYPE_ASSERT (array, sig_a);
    
    IDIO o = idio_gc_get (IDIO_TYPE_BIGNUM);

    IDIO_GC_ALLOC (o->u.bignum, sizeof (idio_bignum_t));

    IDIO_BIGNUM_GREY (o) = NULL;
    IDIO_BIGNUM_NUMS (o) = NULL;
    IDIO_BIGNUM_FLAGS (o) = flags;
    IDIO_BIGNUM_EXP (o) = exp;
    IDIO_BIGNUM_SIG (o) = sig_a;

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

    idio_gc_stats_free (sizeof (idio_bignum_t));

    free (bn->u.bignum);
}

IDIO idio_bignum_copy (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);
    
    IDIO sig_ac = idio_array_copy (IDIO_BIGNUM_SIG (bn), 0);

    /* all the C_int64 are referenced in both arrays so we should copy
	those as well otherwise things that change the sign of an
	integer with affect the source too */

    size_t al = idio_array_size (sig_ac);
    int64_t i;
    for (i = 0 ; i < al; i++) {
	IDIO v = idio_array_get_index (sig_ac, i);
	IDIO vn = idio_C_int64 (IDIO_C_TYPE_INT64 (v));
	idio_array_insert_index (sig_ac, vn, i);
    }
    
    IDIO bnc = idio_bignum (IDIO_BIGNUM_FLAGS(bn), IDIO_BIGNUM_EXP (bn), sig_ac);

    return bnc;
}

IDIO idio_bignum_integer_int64 (int64_t i)
{

    IDIO sig_a = idio_array (1);
    
    idio_array_insert_index (sig_a, idio_C_int64 (i), 0);
    
    IDIO r = idio_bignum (IDIO_BIGNUM_FLAG_INTEGER, 0, sig_a);

    return r;
}

IDIO idio_bignum_integer (IDIO sig_a)
{
    IDIO_ASSERT (sig_a);

    IDIO_TYPE_ASSERT (array, sig_a);

    return idio_bignum (IDIO_BIGNUM_FLAG_INTEGER, 0, sig_a);
}

IDIO idio_bignum_copy_to_integer (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);
    
    IDIO bnc = idio_bignum_copy (bn);

    IDIO bn_i = idio_bignum_integer (IDIO_BIGNUM_SIG (bnc));

    return bn_i;
}

int64_t idio_bignum_int64_value (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bn_i = idio_bignum_integer_argument (bn);
    if (idio_S_nil == bn_i) {
	return 0;
    }

    IDIO sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = idio_array_size (sig_a);

#if IDIO_BIGNUM_DPW == 18
    if (al > 1) {
	idio_error_add_C ("bignum too large to convert to int_64_t");
	return 0;
    }
    
    IDIO i = idio_array_get_index (sig_a, al - 1);

    return IDIO_C_TYPE_INT64 (i);
#else
    idio_error_message ("idio_bignum_int64_value: too hard to compute for dpw of %d!", IDIO_BIGNUM_DPW);
    return idio_S_unspec;
#endif
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

    IDIO sig_a = IDIO_BIGNUM_SIG (bn_i);
    size_t al = idio_array_size (sig_a);

    if (al > 1) {
	return idio_S_nil;
    }
    
    IDIO i = idio_array_get_index (sig_a, al - 1);

    int64_t iv = IDIO_C_TYPE_INT64 (i);

    if (iv < IDIO_FIXNUM_MAX &&
	iv > IDIO_FIXNUM_MIN) {
	return IDIO_FIXNUM (iv);
    }

    return idio_S_nil;
}

IDIO idio_bignum_abs (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bnc = idio_bignum_copy (bn);

    IDIO sig_a = IDIO_BIGNUM_SIG (bnc);
    size_t al = idio_array_size (sig_a);
    IDIO i = idio_array_get_index (sig_a, al - 1);
    IDIO_C_TYPE_INT64 (i) = llabs (IDIO_C_TYPE_INT64 (i));
    idio_array_insert_index (sig_a, i, al - 1);
    
    return bnc;
}

int idio_bignum_negative_p (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = idio_array_size (sig_a);

    IDIO i = idio_array_get_index (sig_a, al - 1);
    
    return (IDIO_C_TYPE_INT64 (i) < 0);
}

IDIO idio_bignum_negate (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO bnc = idio_bignum_copy (bn);

    size_t al = idio_array_size (IDIO_BIGNUM_SIG (bnc));
    
    IDIO i = idio_array_get_index (IDIO_BIGNUM_SIG (bnc), al - 1);
    IDIO_C_TYPE_INT64 (i) = - IDIO_C_TYPE_INT64 (i);
    
    idio_array_insert_index (IDIO_BIGNUM_SIG (bnc), i, al - 1);
    
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

	    r = idio_bignum_negate (r);

	    return r;
	} else {
	    /* -a + b => b - |a| */
	    a = idio_bignum_abs (a);

	    IDIO r = idio_bignum_subtract (b, a);

	    return r;
	}
    } else if (idio_bignum_negative_p (b)) {
	/* a + -b => a - |b| */
	b = idio_bignum_abs (b);

	IDIO r = idio_bignum_subtract (a, b);

	return r;
    }

    /* regular a + b */
    size_t al = idio_array_size (IDIO_BIGNUM_SIG (a));
    size_t bl = idio_array_size (IDIO_BIGNUM_SIG (b));

    int carry = 0;
    size_t rl;
    
    if (al >= bl) {
	rl = al;
    } else {
	rl = bl;
    }
    IDIO ra = idio_array (rl);
    
    int64_t ai = 0;
    int64_t bi = 0;
    int64_t ri = 0;
    
    while (ai < al ||
	   bi < bl ||
	   carry) {
	int64_t ia = 0;
	int64_t ib = 0;

	if (ai < al) {
	    IDIO i = idio_array_get_index (IDIO_BIGNUM_SIG (a), ai);
	    ia = IDIO_C_TYPE_INT64 (i);
	}

	if (bi < bl) {
	    IDIO i = idio_array_get_index (IDIO_BIGNUM_SIG (b), bi);
	    ib = IDIO_C_TYPE_INT64 (i);
	}

	int64_t ir = ia + ib + carry;
	carry = 0;

	if (ir >= IDIO_BIGNUM_INT_SEG_LIMIT) {
	    ir -= IDIO_BIGNUM_INT_SEG_LIMIT;
	    carry = 1;
	}

	idio_array_insert_index (ra, idio_C_int64 (ir), ri);

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

    IDIO sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = idio_array_size (sig_a);
    if (1 == al) {
	IDIO i = idio_array_get_index (sig_a, 0);
	return (IDIO_C_TYPE_INT64 (i) == 0);
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

    size_t al = idio_array_size (IDIO_BIGNUM_SIG (a));
    size_t bl = idio_array_size (IDIO_BIGNUM_SIG (b));

    if (al < bl) {
	return na ? 0 : 1;
    }

    if (al > bl) {
	return na ? 1 : 0;
    }

    IDIO aa = idio_bignum_abs (a);
    IDIO sig_aa = IDIO_BIGNUM_SIG (aa);

    IDIO ab = idio_bignum_abs (b);
    IDIO sig_ab = IDIO_BIGNUM_SIG (ab);

    int64_t i;
    for (i = al - 1; i >= 0 ; i--) {
	IDIO iaa = idio_array_get_index (sig_aa, i);
	IDIO iab = idio_array_get_index (sig_ab, i);

	if (IDIO_C_TYPE_INT64 (iaa) < IDIO_C_TYPE_INT64 (iab)) {
	    return na ? 0 : 1;
	}

	if (IDIO_C_TYPE_INT64 (iaa) > IDIO_C_TYPE_INT64 (iab)) {
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

    IDIO sig_aa = IDIO_BIGNUM_SIG (a);
    size_t al = idio_array_size (sig_aa);
    IDIO sig_ab = IDIO_BIGNUM_SIG (b);
    size_t bl = idio_array_size (sig_ab);

    if (al != bl) {
	return 0;
    }

    int64_t i;
    for (i = 0; i < al ; i++) {
	IDIO ia = idio_array_get_index (sig_aa, i);
	IDIO ib = idio_array_get_index (sig_ab, i);

	if (IDIO_C_TYPE_INT64 (ia) != IDIO_C_TYPE_INT64 (ib)) {
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
	    
	    IDIO r = idio_bignum_subtract (b, a);

	    return r;
	} else {
	    /* -a - b => |a| + b */
	    a = idio_bignum_abs (a);
	    
	    IDIO r = idio_bignum_add (a, b);
	    
	    r = idio_bignum_negate (r);

	    return r;
	}
    } else if (idio_bignum_negative_p (b)) {
	/* a - -b => a + |b| */
	b = idio_bignum_abs (b);
	
	IDIO r = idio_bignum_add (a, b);

	return r;
    }

    /* regular a - b: a < b => -(b - a) */
    if (idio_bignum_lt_p (a, b)) {
	IDIO r = idio_bignum_subtract (b, a);
	
	r = idio_bignum_negate (r);

	return r;
    }

    /* regular a - b: a >= b */
    IDIO sig_aa = IDIO_BIGNUM_SIG (a);
    size_t al = idio_array_size (sig_aa);
    IDIO sig_ab = IDIO_BIGNUM_SIG (b);
    size_t bl = idio_array_size (sig_ab);

    int borrow = 0;
    size_t rl;
    
    if (al >= bl) {
	rl = al;
    } else {
	rl = bl;
    }
    IDIO ra = idio_array (rl);

    int64_t ai = 0;
    int64_t bi = 0;
    int64_t ri = 0;

    int borrow_bug = 0;
    while (ai < al ||
	   bi < bl ||
	   borrow) {
	int64_t ia = 0;
	int64_t ib = 0;

	if (ai < al) {
	    IDIO i = idio_array_get_index (sig_aa, ai);
	    ia = IDIO_C_TYPE_INT64 (i);
	}

	if (bi < bl) {
	    IDIO i = idio_array_get_index (sig_ab, bi);
	    ib = IDIO_C_TYPE_INT64 (i);
	}

	int64_t ir = ia - ib - borrow;
	borrow = 0;

	if (ir < 0) {
	    ir += IDIO_BIGNUM_INT_SEG_LIMIT;
	    borrow = 1;
	    borrow_bug++;
	    IDIO_C_ASSERT (borrow_bug < 10);
	}

	idio_array_insert_index (ra, idio_C_int64 (ir), ri);

	ai++;
	bi++;
	ri++;
    }

    /* remove leading zeroes */
    IDIO ir = idio_array_get_index (ra, rl - 1);
    while (0 == IDIO_C_TYPE_INT64 (ir) &&
	   rl > 1) {
	idio_array_pop (ra);
	rl--;
	ir = idio_array_get_index (ra, rl - 1);
    }

    IDIO r = idio_bignum_integer (ra);

    return r;
}

IDIO idio_bignum_shift_left (IDIO a, int fill)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (bignum, a);

    IDIO sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = idio_array_size (sig_a);
    IDIO ra = idio_array (al);
    
    int64_t ai;

    int carry = fill;
    
    for (ai = 0; ai < al; ai++) {
	IDIO ia = idio_array_get_index (sig_a, ai);
	int64_t i = IDIO_C_TYPE_INT64 (ia);
	int64_t r;
	
	if (i >= (IDIO_BIGNUM_INT_SEG_LIMIT / 10)) {
	    int64_t c = i / (IDIO_BIGNUM_INT_SEG_LIMIT / 10);
	    r = i % (IDIO_BIGNUM_INT_SEG_LIMIT / 10) * 10;
	    r += carry;
	    carry = c;
	} else {
	    r = i * 10 + carry;
	    carry = 0;
	}

	idio_array_insert_index (ra, idio_C_int64 (r), ai);
    }

    if (carry) {
	idio_array_insert_index (ra, idio_C_int64 (carry), ai);
    }

    IDIO r = idio_bignum_integer (ra);

    return r;
}

/* result: (a/10 . a%10) */
IDIO idio_bignum_shift_right (IDIO a)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (bignum, a);

    IDIO sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = idio_array_size (sig_a);
    IDIO ra;

    /* repeated shift_rights result in an empty array! */
    if (al) {
	ra = idio_array (al);
    } else {
	al++;
	ra = idio_array (al);

	/* plonk this int64 into sig_a as that's what we're about to
	get_index from */
	idio_array_insert_index (sig_a, idio_C_int64 (0), 0);
    }
    
    int64_t ai;

    int carry = 0;
    
    for (ai = al - 1; ai >= 0; ai--) {
	IDIO ia = idio_array_get_index (sig_a, ai);
	int64_t i = IDIO_C_TYPE_INT64 (ia);

	int64_t c = i % 10;
	int64_t r = i / 10;
	r += carry * (IDIO_BIGNUM_INT_SEG_LIMIT / 10);
	carry = c;

	idio_array_insert_index (ra, idio_C_int64 (r), ai);
    }

    /* is more than one segment s and if the mss is zero, pop it
	off */
    if (al > 1) {
	IDIO v = idio_array_get_index (ra, al - 1);
	if (0 == IDIO_C_TYPE_INT64 (v)) {
	    idio_array_pop (ra);
	}
    }

    IDIO c_i = idio_bignum_integer_int64 (carry);
    
    IDIO r_i = idio_bignum_integer (ra);

    IDIO r = idio_pair (r_i, c_i);

    return r;
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
    
    IDIO r = idio_bignum_integer_int64 (0);
    
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
	IDIO ir = idio_array_get_index (IDIO_BIGNUM_SIG (ibsrt), 0);
	int64_t i = IDIO_C_TYPE_INT64 (ir);

	
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
    
    IDIO fp = idio_bignum_integer_int64 (1);
    
    IDIO rn = rp;
    IDIO fn = fp;

    while (idio_bignum_lt_p (rn, a)) {
	rp = rn;
	fp = fn;
	
	rn = idio_bignum_shift_left (rn, 0);
	
	fn = idio_bignum_shift_left (fn, 0);

    }

    IDIO r = idio_pair (rp, fp);
    
    return r;
}

/* result: (a/b . a%b) */
IDIO idio_bignum_divide (IDIO a, IDIO b)
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    IDIO_TYPE_ASSERT (bignum, a);
    IDIO_TYPE_ASSERT (bignum, b);

    if (idio_bignum_zero_p (b)) {
	idio_error_add_C ("divide by zero");
	return idio_S_nil;
    }

    int na = idio_bignum_negative_p (a);
    int neg = na != idio_bignum_negative_p (b);
    IDIO aa = idio_bignum_abs (a);
    
    IDIO ab = idio_bignum_abs (b);
    
    IDIO r_div = idio_bignum_integer_int64 (0);
    
    IDIO r_mod = idio_bignum_copy (aa);

    /*
      a / b for 12 / 123

      r_div = 0
      r_mod = 12
     */
    if (idio_bignum_lt_p (aa, ab)) {
	if (na) {
	    r_mod = a;

	    IDIO r = idio_pair (r_div, r_mod);

	    return r;
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
	IDIO c = idio_bignum_integer_int64 (0);
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

    IDIO r = idio_pair (r_div, r_mod);
    
    return r;
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
	IDIO_BIGNUM_REAL_INEXACT_P (bn_i)) {
	return idio_S_nil;
    }

    return bn_i;
}

IDIO idio_bignum_real (int flags, int64_t exp, IDIO sig_a)
{
    IDIO_ASSERT (sig_a);

    IDIO_TYPE_ASSERT (array, sig_a);

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

    int64_t exp = IDIO_BIGNUM_EXP (bn);
    IDIO sig_a = IDIO_BIGNUM_SIG (bn);

    /* significand-only part */
    IDIO bn_s = idio_bignum_copy (bn);
    
    int digits = idio_bignum_count_digits (sig_a);
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

    /* S9fES checks for over/under-flow in exp wrt IDIO_BIGNUM_DPW.
	Not sure that's applicable here. */

    IDIO r = idio_bignum_real (IDIO_BIGNUM_FLAGS(bn) | inexact, exp, IDIO_BIGNUM_SIG (bn_s));

    return r;
}

IDIO idio_bignum_to_real (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    int64_t exp = 0;

    IDIO bnc = idio_bignum_copy (bn);
    IDIO sig_a = IDIO_BIGNUM_SIG (bnc);
    size_t al = idio_array_size (sig_a);
    IDIO i = idio_array_get_index (sig_a, al - 1);
    IDIO_C_TYPE_INT64 (i) = llabs (IDIO_C_TYPE_INT64 (i));

    /*
      A much cheaper and lossier truncation of precision.  Do it by whole segments.

      With DPW of 3 and 1 seg then 3141 would become 3e3
     */
    int nseg = idio_array_size (sig_a);
    int inexact = 0;

    IDIO r;
    
    if (nseg > IDIO_BIGNUM_SIG_SEGMENTS) {
	int64_t nshift = (nseg - IDIO_BIGNUM_SIG_SEGMENTS);
	int64_t i;
	for (i = 0; i < nshift; i++) {
	    idio_array_shift (sig_a);
	}
	exp = nshift * IDIO_BIGNUM_DPW;
	inexact = IDIO_BIGNUM_FLAG_REAL_INEXACT;
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

    IDIO sig_a = IDIO_BIGNUM_SIG (a);
    size_t al = idio_array_size (sig_a);

    if (al > 1) {
	return 0;
    }

    IDIO ia = idio_array_get_index (sig_a, 0);
    return (IDIO_C_TYPE_INT64 (ia) == 0);
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

    IDIO ras = IDIO_BIGNUM_SIG (ra);
    IDIO rbs = IDIO_BIGNUM_SIG (rb);
    
    size_t ral = idio_array_size (ras);
    size_t rbl = idio_array_size (rbs);

    if (ral != rbl) {
	return 0;
    }

    int64_t i;

    for (i = ral - 1; i >= 0; i--) {
	IDIO ia = idio_array_get_index (ras, i);
	IDIO ib = idio_array_get_index (rbs, i);

	if (IDIO_C_TYPE_INT64 (ia) != IDIO_C_TYPE_INT64 (ib)) {
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
IDIO idio_bignum_scale_significand (IDIO bn, int64_t desired_exp, int max_size)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    int digits = idio_bignum_count_digits (IDIO_BIGNUM_SIG (bn));

    /* is there room to scale within the desired_exp (and max_size)? */
    if ((max_size - digits) < (IDIO_BIGNUM_EXP (bn) - desired_exp)) {
	return idio_S_nil;
    }

    IDIO bnc = idio_bignum_copy (bn);
    
    int64_t exp = IDIO_BIGNUM_EXP (bn);
    while (exp > desired_exp) {
	bnc = idio_bignum_shift_left (bnc, 0);
	exp--;
    }

    IDIO r = idio_bignum_real (IDIO_BIGNUM_FLAGS(bn), exp, IDIO_BIGNUM_SIG (bnc));

    return r;
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

    int dpa = idio_bignum_count_digits (IDIO_BIGNUM_SIG (ra)) + IDIO_BIGNUM_EXP (ra);
    int dpb = idio_bignum_count_digits (IDIO_BIGNUM_SIG (rb)) + IDIO_BIGNUM_EXP (rb);

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

    size_t ral = idio_array_size (IDIO_BIGNUM_SIG (ra));
    size_t rbl = idio_array_size (IDIO_BIGNUM_SIG (rb));

    if (ral < rbl) {
	return 1;
    }

    if (ral > rbl) {
	return 0;
    }

    int64_t i;

    for (i = ral - 1 ; i >= 0 ; i--) {
	IDIO ia = idio_array_get_index (IDIO_BIGNUM_SIG (ra), i);
	IDIO ib = idio_array_get_index (IDIO_BIGNUM_SIG (rb), i);

	if (IDIO_C_TYPE_INT64 (ia) < IDIO_C_TYPE_INT64 (ib)) {
	    return neg ? 0 : 1;
	}

	if (IDIO_C_TYPE_INT64 (ia) > IDIO_C_TYPE_INT64 (ib)) {
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

    int64_t exp = IDIO_BIGNUM_EXP (ra);
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

    r = idio_bignum_normalize (r);

    return r;
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
	
    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    int inexact = IDIO_BIGNUM_REAL_INEXACT_P (ra) | IDIO_BIGNUM_REAL_INEXACT_P (rb);

    int neg = IDIO_BIGNUM_REAL_NEGATIVE_P (ra) != IDIO_BIGNUM_REAL_NEGATIVE_P (rb);

    int64_t expa = IDIO_BIGNUM_EXP (ra);
    int64_t expb = IDIO_BIGNUM_EXP (rb);

    IDIO ra_i = idio_bignum_copy_to_integer (ra);
    
    IDIO rb_i = idio_bignum_copy_to_integer (rb);
    
    int64_t exp = expa + expb;

    IDIO r_i = idio_bignum_multiply (ra_i, rb_i);

    int flags = inexact | (neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0);
    
    IDIO r = idio_bignum_real (flags, exp, IDIO_BIGNUM_SIG (r_i));

    r = idio_bignum_normalize (r);
    
    return r;
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
	if (idio_bignum_real_zero_p (ra)) {
	    IDIO i0 = idio_bignum_integer_int64 (0);

	    IDIO r = idio_bignum_real (0, 0, IDIO_BIGNUM_SIG (i0));

	    return r;
	}
    }
	
    IDIO rb = b;

    if (IDIO_BIGNUM_INTEGER_P (b)) {
	rb = idio_bignum_to_real (b);
    }

    int inexact = IDIO_BIGNUM_REAL_INEXACT_P (ra) | IDIO_BIGNUM_REAL_INEXACT_P (rb);

    int neg = IDIO_BIGNUM_REAL_NEGATIVE_P (ra) != IDIO_BIGNUM_REAL_NEGATIVE_P (rb);

    int64_t expa = IDIO_BIGNUM_EXP (ra);
    int64_t expb = IDIO_BIGNUM_EXP (rb);

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
	for a division of what are real numbers: 13.0/4 is 3.0.
	Really?

	But wait, the integer division of 13000/4 is 3250, so if we
	bumped the numerator up by 10^n (and decremented its exponent
	by n), in this case n=3, then we'll have more significant
	digits in our answer and the combined exponent, now -3+0=-3,
	makes the resultant real 3250e-3 or 3.250.  Hurrah!

	So what value of n?  As big as we can!

	Here we can abuse our normal
	IDIO_BIGNUM_SIG_MAX_DIGITS limit and say that we want
	to make n such that digits(a*10^n) == digits(b)+MAX_DIGITS.
	This way, without losing precision in b (by shrinking it) we
	can bump a up such that the resultant integer division has
	MAX_DIGITS significant digits.
     */
    
    int nd = idio_bignum_count_digits (IDIO_BIGNUM_SIG (ra));
    int dd = IDIO_BIGNUM_SIG_MAX_DIGITS + idio_bignum_count_digits (IDIO_BIGNUM_SIG (rb));

    while (nd < dd) {
	ra_i = idio_bignum_shift_left (ra_i, 0);

	nd++;
	expa--;
    }

    int64_t exp = expa - expb;
    IDIO ibd = idio_bignum_divide (ra_i, rb_i);
    IDIO r_i = IDIO_PAIR_H (ibd);

    if (! idio_bignum_zero_p (r_i)) {
	inexact = IDIO_BIGNUM_FLAG_REAL_INEXACT;
    }

    int flags = inexact | (neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0);
    
    IDIO r = idio_bignum_real (flags, exp, IDIO_BIGNUM_SIG (r_i));

    r = idio_bignum_normalize (r);
    
    return r;
}
	
/* printers */

char *idio_bignum_integer_as_string (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    IDIO sig_a = IDIO_BIGNUM_SIG (bn);
    
    char *s = idio_alloc (1);
    *s = '\0';

    size_t al = idio_array_size (sig_a);
    int64_t i;
    for (i = al - 1; i >= 0; i--) {
	IDIO v = idio_array_get_index (sig_a, i);
	char buf[BUFSIZ];
	char fmt[BUFSIZ];
	if (i == al - 1) {
	    sprintf (fmt, "%%ld");
	} else {
	    sprintf (fmt, "%%0%dld", IDIO_BIGNUM_DPW);
	}
	sprintf (buf, fmt, IDIO_C_TYPE_INT64 (v));
	s = idio_strcat (s, buf);
    }

    return s;
}

char *idio_bignum_expanded_real_as_string (IDIO bn, int64_t exp, int digits, int neg)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    char *s = idio_alloc (1);
    *s = '\0';

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	IDIO_STRCAT (s, "-");
    }

    int dp_offset = exp + digits;

    if (dp_offset <= 0) {
	IDIO_STRCAT (s, "0");
    }

    if (dp_offset < 0) {
	IDIO_STRCAT (s, ".");
    }

    while (dp_offset < 0) {
	IDIO_STRCAT (s, "0");
	dp_offset++;
    }

    dp_offset = exp + digits;
    IDIO sig_a = IDIO_BIGNUM_SIG (bn);
    size_t al = idio_array_size (sig_a);

    int64_t ai;
    for (ai = al - 1; ai >= 0; ai--) {
	IDIO v = idio_array_get_index (sig_a, ai);
	IDIO_STRCAT_FREE (s, idio_as_string (v, 1));
    }

    if (dp_offset >= 0) {
	while (dp_offset > 0) {
	    IDIO_STRCAT (s, "0");
	    dp_offset--;
	}
	IDIO_STRCAT (s, ".0");
    }

    if (IDIO_BIGNUM_REAL_INEXACT_P (bn)) {
	IDIO_STRCAT (s, "-inexact");
    }

    return s;
}

char *idio_bignum_real_as_string (IDIO bn)
{
    IDIO_ASSERT (bn);

    IDIO_TYPE_ASSERT (bignum, bn);

    if (!IDIO_BIGNUM_REAL_P (bn)) {
	return NULL;
    }
    
    IDIO sig_a = IDIO_BIGNUM_SIG (bn);
    int digits = idio_bignum_count_digits (sig_a);
    int exp = IDIO_BIGNUM_EXP (bn);

    if (0 && (exp + digits) > -4 &&
	(exp + digits) <= 9) {
	return idio_bignum_expanded_real_as_string (bn, exp, digits, IDIO_BIGNUM_REAL_NEGATIVE_P (bn));
    }

    char *s = idio_alloc (1);
    *s = '\0';

    if (IDIO_BIGNUM_REAL_INEXACT_P (bn)) {
	IDIO_STRCAT (s, "#i");
    }

    if (IDIO_BIGNUM_REAL_NEGATIVE_P (bn)) {
	IDIO_STRCAT (s, "-");
    }

    size_t al = idio_array_size (sig_a);
    int64_t i = al - 1;
    IDIO v = idio_array_get_index (sig_a, i);

    /*
      vs can be n digits long (n >= 1).  We want to add vs[0] then
	".".  If vs is more than 1 digit then add the rest of vs.  If
	there are no more digits to add then add "0".
     */
    char *vs = idio_as_string (v, 1);
    IDIO_C_ASSERT (strlen (vs));
    char vs_rest[IDIO_BIGNUM_DPW];
    strcpy (vs_rest, vs + 1);
    vs[1] = '\0';

    s = idio_strcat (s, vs);
    IDIO_STRCAT (s, ".");

    if (strlen (vs_rest)) {
	s = idio_strcat (s, vs_rest);
    } else {
	if (0 == i) {
	    IDIO_STRCAT (s, "0");
	}
    }
    free (vs);
    
    for (i--; i >= 0; i--) {
	v = idio_array_get_index (sig_a, i);
	char buf[BUFSIZ];
	sprintf (buf, "%0*ld", IDIO_BIGNUM_DPW, IDIO_C_TYPE_INT64 (v));
	s = idio_strcat (s, buf);
	/* s = idio_strcat_free (s, idio_as_string (v, 1, 0)); */
    }

    IDIO_STRCAT (s, "e");
    if ((exp + digits - 1) >= 0) {
	IDIO_STRCAT (s, "+");
    }
    v = idio_C_int64 ((exp + digits - 1));
    s = idio_strcat_free (s, idio_as_string (v, 1));

    return s;
}

char *idio_bignum_as_string (IDIO bn)
{
    IDIO_ASSERT (bn);

    if (idio_S_NaN == bn) {
	char *s = idio_alloc (strlen (IDIO_BIGNUM_NAN) + 1);
	strcpy (s, IDIO_BIGNUM_NAN);

	return s;
    }

    if (IDIO_BIGNUM_INTEGER_P (bn)) {
	return idio_bignum_integer_as_string (bn);
    }
    return idio_bignum_real_as_string (bn);
}

/*
  count the digits in the first array element (by dividing by 10) then
  add DPW times the remaining number of elements
 */
int idio_bignum_count_digits (IDIO sig_a)
{
    IDIO_ASSERT (sig_a);

    IDIO_TYPE_ASSERT (array, sig_a);

    size_t al = idio_array_size (sig_a);
    IDIO_C_ASSERT (al);

    IDIO i = idio_array_get_index (sig_a, al - 1);
    int64_t v = IDIO_C_TYPE_INT64 (i);
    int d = 0;
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
    IDIO ra = idio_array (1);
    
    int64_t ri = 0;
    
    while (nl) {
	int eos = (nl < IDIO_BIGNUM_DPW) ? nl : IDIO_BIGNUM_DPW;
	char *end;
	int base = 10;
	errno = 0;
	int64_t i = strtoll (&s[nl-eos], &end, base);

	if ((errno == ERANGE &&
	     (i == LLONG_MAX ||
	      i == LLONG_MIN)) ||
	    (errno != 0 &&
	     i == 0)) {
	    char em[BUFSIZ];
	    sprintf (em, "strtoll (%s) = %ld: %s", nums, i, strerror (errno));
	    idio_error_add_C (em);
	    free (buf);

	    return idio_S_nil;
	}

	if (end == nums) {
	    char em[BUFSIZ];
	    sprintf (em, "strtoll (%s): No digits?", nums);
	    idio_error_add_C (em);
	    free (buf);

	    return idio_S_nil;
	}
	
	if ('\0' != *end) {
	    char em[BUFSIZ];
	    sprintf (em, "strtoll (%s) = %ld", nums, i);
	    idio_error_add_C (em);
	    free (buf);

	    return idio_S_nil;
	}

	s[nl-eos] = '\0';
	nl -= eos;
	if (0 == nl &&
	    (req_exact ||
	     is_exact)) {
	    i *= sign;
	}

	idio_array_insert_index (ra, idio_C_int64 (i), ri);
	ri++;
    }

    free (buf);
    
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

	r = idio_bignum_normalize (r);
	
	return r;
    }

    IDIO_C_ASSERT (0);
}

IDIO idio_bignum_real_C (char *nums)
{
    IDIO_C_ASSERT (nums);

    IDIO sig_bn = idio_bignum_integer_int64 (0);
    
    int64_t exp = 0;
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

	IDIO i = idio_bignum_integer_int64 (digit);
	
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

    IDIO r = idio_bignum_real (((neg ? IDIO_BIGNUM_FLAG_REAL_NEGATIVE : 0) |
				(exact ? 0 : IDIO_BIGNUM_FLAG_REAL_INEXACT)),
			       exp,
			       IDIO_BIGNUM_SIG (sig_bn));
    
    r = idio_bignum_normalize (r);
    
    return r;
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

    IDIO r = idio_bignum_integer_int64 (0);
    
    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_add: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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

    IDIO r;

    int first = 1;
    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_subtract: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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

    IDIO r = idio_bignum_integer_int64 (1);
    
    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_multiply: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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

    IDIO r = idio_bignum_integer_int64 (1);

    int first = 1;
    
    while (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);

        if (! idio_isa_bignum (h)) {
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_divide: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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
	    idio_error_add_C ("divide by zero");
	    break;
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

    int64_t exp = IDIO_BIGNUM_EXP (bn);

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
	    IDIO bn1 = idio_bignum_integer_int64 (1);
	    
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_lt: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_le: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_gt: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_ge: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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
	    char em[BUFSIZ];
	    sprintf (em, "idio_bignum_primitive_ge: expected a bignum, got a %s", idio_type2string (h));
	    idio_error_add_C (em);
	    break;
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
	n = idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (n));	\
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
	n = idio_bignum_integer_int64 (IDIO_FIXNUM_VAL (n));
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
        r = IDIO_FIXNUM (0);
    } else {
	int64_t exp = IDIO_BIGNUM_EXP (n);

	if (exp >= IDIO_FIXNUM_MIN &&
	    exp <= IDIO_FIXNUM_MAX) {
	    r = IDIO_FIXNUM (exp);
	} else {
	    r = idio_bignum_integer_int64 (exp);
	}
    }

    return r;
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
}

void idio_final_bignum ()
{
}
