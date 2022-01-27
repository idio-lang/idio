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
 * bignum.h
 *
 */

#ifndef BIGNUM_H
#define BIGNUM_H

/*
  Inspiration and algorithms from Nils M Holm's Scheme 9 from Empty
  Space

  A bignum object has:

  - some flags (integer/real/inexact/real-negative)

  - an int32_t exponent (allowing n * 10^(+/- 2^31) which is "quite
    large" and probably covers enough of the number-space for our
    shell-orientated purposes).

    An int32_t might seem excessive but the layout of a C struct means
    we have an architecture-dependent word to play with -- if we don't
    use a whole word, say uint16_t for exponent, then the rest of the
    word is wasted space.

    In this case, we'll use 32 bits (and therefore waste 32 bits on
    64-bit systems) for consistency across platforms.

        Using an int8_t for exponent seems to provoke some issues so
        the exponent should be at least int16_t (until the issue is
        figured out).

    For comparison, https://en.wikipedia.org/wiki/IEEE_754 suggests
    the number of bits in IEEE 754 exponents are:

     binary32	8	~ 10^+/-38	float
     binary64	11	~ 10^+/-308	double
     binary128	15	~ 10^+/-4934	long double(*)
     binary256	19	~ 10^+/-78958	?

     (*) See https://en.wikipedia.org/wiki/Long_double for your
     likelihood of getting a binary128 implementation.

  - up to IDIO_BIGNUM_SIG_MAX_DIGITS significant decimal digits spread
    across several machine words.

    Those words are organized as a dynamically sized (reference
    counted!) array of IDIO_BS_T.

    The first word in the array represents the least significant word
    of the bignum meaning that index zero remains the LSS as the
    bignum grows.  This is the opposite of S9fES' linked lists of
    MSS->...->LSS.

  Sign: For a real the sign is in a distinct flag.  For an integer,
  the most significant segment is signed.

  Technically, the exponent range isn't 10^(+/- 2^31) because it is
  modified by the number of significant digits (which is dynamic).
  The point being that the significant digits are always without a
  decimal place and that is modified by the exponent.  So 123 and
  0.123 have identical significant digits, 123, but different
  exponents, 0 and -3.  The point being that the int32_t exponent
  allows for "quite large" or "quite small" numbers.  Bigger/smaller
  than is practically useful.  (Unless you can think if a practical
  use.)


  How many decimal digits can fit in a machine word?  log2(10) is
  3.322, ie each base10 digit uses 3.322 binary bits.  19 of those
  would be 63.12 bits which means we'd not be able to store the sign
  of the value (which only applies to the MSS of an integer).  So 18
  decimal digits in 64 bits.  Similarly, 9 in 32.

  IDIO_BIGNUM_INT_SEG_LIMIT is the decimal representation of one more
  than IDIO_BIGNUM_DPW (digits per word), ie. if DPW was 3 then
  INT_SEG_LIMIT would be 1000 (one more than 999, the largest three
  digit decimal number).  It gets used to check when we are
  overflowing IDIO_BIGNUM_DPW.

  IDIO_BIGNUM_SIG_SEGMENTS is simply the number of elements we want to
  have in our bignum array and therefore defines the number of
  significant digits we can keep.  How many significant digits do you
  want?  There are, by several estimates, approximately 10^80 atoms in
  the universe.  By any measure of sensible characteristics of a
  programming language we should be able to count to such a number
  accurately...

  NB. IDIO_BIGNUM_MDPW (max digits per word) is used if you happen to
  set IDIO_BIGNUM_DPW below that value for debugging.  Otherwise we
  need a dynamic calculation as to how many segments of DPW digits
  will fit in an intptr_t.

  Using a large IDIO_BIGNUM_SIG_SEGMENTS means your calculations are
  more accurate.  However, remember that even relatively simple
  calculations can result in an arbitrarily large number of
  significant digits.  Consider how many significant digits are
  required to accurately represent dividing one by three.

  In addition, IDIO_BIGNUM_SIG_SEGMENTS != 1 makes for exponentially
  slow performance...

  The S9fES bignum tests work well with 18 significant digits.

  In addition, we have occasion to convert back into a C integer.  C's
  *_MAX are generally represented by significand arrays which are
  greater than one, eg. INT64_MAX is 9223372036854775807, 19 digits
  long, UINT_MAX is 18446744073709551616, 20 digits, so two or three
  segments depending on OS+ARCH.

  In the case of UINT_MAX the significant digits in each segment will
  be 18, 446744073 and 709551616 on a 32-bit system and 18 and
  446744073709551616 on 64-bit systems.

  That means we need to know how many segments will be used to
  represent such a number and what the first digit of that number is
  (both of which we can use as accelerators to decide if it's worth
  bothering to convert at all).
 */

#ifdef __LP64__
#define IDIO_BIGNUM_MDPW          18
/*
#define IDIO_BIGNUM_DPW           18
#define IDIO_BIGNUM_INT_SEG_LIMIT 1000000000000000000LL
#define IDIO_BIGNUM_SIG_SEGMENTS  5
*/

#define IDIO_BIGNUM_DPW           18
#define IDIO_BIGNUM_DPW_FMT       "%018zd"
#define IDIO_BIGNUM_INT_SEG_LIMIT 1000000000000000000LL
#define IDIO_BIGNUM_SIG_SEGMENTS  1

#define IDIO_BIGNUM_WORD_OFFSET	  1
#define IDIO_BIGNUM_INT64_WORDS   2
#define IDIO_BIGNUM_int64_t_WORDS 2
/*
#define IDIO_BIGNUM_DPW           1
#define IDIO_BIGNUM_INT_SEG_LIMIT 10LL
#define IDIO_BIGNUM_SIG_SEGMENTS  18
*/
#else
#define IDIO_BIGNUM_MDPW          9
#define IDIO_BIGNUM_DPW           9
#define IDIO_BIGNUM_DPW_FMT       "%09zd"
#define IDIO_BIGNUM_INT_SEG_LIMIT 1000000000L
#define IDIO_BIGNUM_SIG_SEGMENTS  2

#define IDIO_BIGNUM_WORD_OFFSET	  2
#define IDIO_BIGNUM_INT64_WORDS   3
#define IDIO_BIGNUM_int64_t_WORDS 3
#endif

#define IDIO_BIGNUM_SIG_MAX_DIGITS	  (IDIO_BIGNUM_SIG_SEGMENTS * IDIO_BIGNUM_DPW)

/*
 * We can use some accelerators when converting a bignum into a C
 * type.  In particular, for large integers near to the limits of a C
 * type, we can look at the value of the first (most significant)
 * segment which will contain a small integer (with the bulk of the
 * digits in subsequent segments).
 *
 * INT64_MAX is 9223372036854775807 which, at 19 digits, is a segment
 * of one digit, 9, and a segment of 18 digits, 223372036854775807.
 *
 * If the number of segments is at the limit and this first segement
 * is <= 9 then we're OK to continue.
 *
 * Clearly, the number could have been 93nnn... so we have a safety
 * check that we haven't overflowed.
 *
 * In all cases, the first digit(s) of a signed integer segment is -D
 * so we only need one macro.
 *
 * The lower limit for an unsigned quantity is 0.
 */

#if PTRDIFF_MAX == 9223372036854775807LL
#if __LP64__
#define IDIO_BIGNUM_ptrdiff_t_WORDS 2
#else
#define IDIO_BIGNUM_ptrdiff_t_WORDS 3
#endif
#define IDIO_BIGNUM_ptrdiff_t_FIRST 9
#else
#if PTRDIFF_MAX == 2147483647L
#define IDIO_BIGNUM_ptrdiff_t_WORDS 2
#define IDIO_BIGNUM_ptrdiff_t_FIRST 2
#else
#error unexpected PTRDIFF_MAX
#endif
#endif

#if INTPTR_MAX == 9223372036854775807LL
#define IDIO_BIGNUM_intptr_t_WORDS 2
#define IDIO_BIGNUM_intptr_t_FIRST 9  /*  9 223372036854775807 */
#else
#if INTPTR_MAX == 2147483647L
#define IDIO_BIGNUM_intptr_t_WORDS 2
#define IDIO_BIGNUM_intptr_t_FIRST 2 /* 2 147483647 */
#else
#error unexpected INTPTR_MAX
#endif
#endif

#if INTMAX_MAX == 9223372036854775807LL
#if __LP64__
#define IDIO_BIGNUM_intmax_t_WORDS  2
#define IDIO_BIGNUM_uintmax_t_WORDS 2
#else
#define IDIO_BIGNUM_intmax_t_WORDS  3
#define IDIO_BIGNUM_uintmax_t_WORDS 3
#endif
#define IDIO_BIGNUM_intmax_t_FIRST  9  /*  9 223372036854775807 */
#define IDIO_BIGNUM_uintmax_t_FIRST 18 /* 18 446744073709551615 */
#else
#if INTMAX_MAX == 2147483647L
#define IDIO_BIGNUM_intmax_t_WORDS  2
#define IDIO_BIGNUM_uintmax_t_WORDS 2
#define IDIO_BIGNUM_intmax_t_FIRST  2 /* 2 147483647 */
#define IDIO_BIGNUM_uintmax_t_FIRST 4 /* 4 294967295 */
#else
#error unexpected INTMAX_MAX
#endif
#endif

#define IDIO_BIGNUM_int64_t_FIRST	9  /*  9 223372036854775807 */

#define IDIO_BIGNUM_uint64_t_WORDS	IDIO_BIGNUM_int64_t_WORDS
#define IDIO_BIGNUM_uint64_t_FIRST	18 /* 18 446744073709551615 */

#define IDIO_BIGNUM_EXP_CHAR(c)	('d' == c || 'D' == c ||	 \
				 'e' == c || 'E' == c ||	 \
				 'f' == c || 'F' == c ||	 \
				 'l' == c || 'L' == c ||	 \
				 's' == c || 'S' == c)

#define IDIO_BIGNUM_INTEGER_P(bn)	(IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_INTEGER)
#define IDIO_BIGNUM_REAL_P(bn)		(IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_REAL)
#define IDIO_BIGNUM_REAL_NEGATIVE_P(bn)	((IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_REAL_NEGATIVE) && \
					 ! idio_bignum_real_zero_p ((bn)))
#define IDIO_BIGNUM_REAL_POSITIVE_P(bn)	(!IDIO_BIGNUM_REAL_NEGATIVE_P (bn))
#define IDIO_BIGNUM_REAL_INEXACT_P(bn)	(IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_REAL_INEXACT)
#define IDIO_BIGNUM_NAN_P(bn)		(IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_NAN)

#define IDIO_BIGNUM_NAN			"NaN"

IDIO idio_bignum (int flags, IDIO_BE_T exp, IDIO_BSA sig_a);
int idio_isa_bignum (IDIO bn);
int idio_isa_integer_bignum (IDIO bn);
void idio_free_bignum (IDIO bn);
IDIO idio_bignum_copy (IDIO bn);
IDIO idio_bignum_integer_intmax_t (intmax_t i);
IDIO idio_bignum_integer_uintmax_t (uintmax_t ui);
IDIO idio_bignum_integer (IDIO_BSA sig_a);
int idio_bignum_real_zero_p (IDIO a);
int idio_bignum_real_equal_p (IDIO a, IDIO b);
IDIO idio_bignum_scale_significand (IDIO bn, IDIO_BE_T desired_exp, size_t max_size);
IDIO idio_bignum_integer_argument (IDIO bn);
int64_t idio_bignum_int64_t_value (IDIO bn);
uint64_t idio_bignum_uint64_t_value (IDIO bn);
ptrdiff_t idio_bignum_ptrdiff_t_value (IDIO bn);
intptr_t idio_bignum_intptr_t_value (IDIO bn);
intmax_t idio_bignum_intmax_t_value (IDIO bn);
uintmax_t idio_bignum_uintmax_t_value (IDIO bn);
IDIO idio_bignum_float (float f);
IDIO idio_bignum_double (double d);
IDIO idio_bignum_longdouble (long double ld);
float idio_bignum_float_value (IDIO bn);
double idio_bignum_double_value (IDIO bn);
long double idio_bignum_longdouble_value (IDIO bn);
IDIO idio_bignum_to_fixnum (IDIO bn);
IDIO idio_bignum_abs (IDIO bn);
int idio_bignum_negative_p (IDIO bn);
IDIO idio_bignum_negate (IDIO bn);
IDIO idio_bignum_add (IDIO a, IDIO b);
int idio_bignum_zero_p (IDIO a);
int idio_bignum_lt_p (IDIO a, IDIO b);
int idio_bignum_equal_p (IDIO a, IDIO b);
IDIO idio_bignum_subtract (IDIO a, IDIO b);
IDIO idio_bignum_shift_left (IDIO a, int fill);
IDIO idio_bignum_shift_right (IDIO a);
IDIO idio_bignum_multiply (IDIO a, IDIO b);
IDIO idio_bignum_equalize (IDIO a, IDIO b);
IDIO idio_bignum_divide (IDIO a, IDIO b);
IDIO idio_bignum_real (int flags, IDIO_BE_T exp, IDIO_BSA sig_a);
IDIO idio_bignum_real_to_integer (IDIO bn);
IDIO idio_bignum_real_to_inexact (IDIO bn);
IDIO idio_bignum_real_to_exact (IDIO bn);
char *idio_bignum_integer_as_string (IDIO o, size_t *sizep);
char *idio_bignum_real_as_string (IDIO o, size_t *sizep);
char *idio_bignum_as_string (IDIO o, size_t *sizep);
size_t idio_bignum_count_digits (IDIO_BSA sig_a);
IDIO idio_bignum_normalize (IDIO o);
char *idio_bignum_C_without_inexact (char const *nums, size_t nums_len);
IDIO idio_bignum_integer_C (char const *nums, size_t nums_len, const int req_exact);
IDIO idio_bignum_real_C (char const *nums, size_t nums_len);
IDIO idio_bignum_C (char const *nums, size_t nums_len);

IDIO idio_bignum_primitive_add (IDIO args);
IDIO idio_bignum_primitive_subtract (IDIO args);
IDIO idio_bignum_primitive_multiply (IDIO args);
IDIO idio_bignum_primitive_divide (IDIO args);
IDIO idio_bignum_primitive_floor (IDIO bn);
IDIO idio_bignum_primitive_quotient (IDIO a, IDIO b);
IDIO idio_bignum_primitive_remainder (IDIO a, IDIO b);
IDIO idio_bignum_primitive_lt (IDIO args);
IDIO idio_bignum_primitive_le (IDIO args);
IDIO idio_bignum_primitive_eq (IDIO args);
IDIO idio_bignum_primitive_ne (IDIO args);
IDIO idio_bignum_primitive_gt (IDIO args);
IDIO idio_bignum_primitive_ge (IDIO args);

char *idio_bignum_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);

void idio_init_bignum ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
