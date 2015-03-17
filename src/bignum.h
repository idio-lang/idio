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

  - an int64_t exponent (allowing n * 10^(+/- 2^63) which is quite
    large and probably covers enough of the number-space for our
    purposes)

  - up to IDIO_BIGNUM_SIG_MAX_DIGITS significant decimal digits spread
    across several machine words.

  Those words are organized as a dynamically sized array of C_int64.
  That could/should be an array of int64_t and would be proportionally
  quicker but there are some fleeting aspirations to making the data
  structure user-visible.

  The first word in the array represents the least significant word of
  the bignum meaning that index zero remains the LSS as the bignum
  grows.  This is the opposite of S9fES' linked lists of MSS->...->LSS.

  Sign: For a real the sign is in a distinct flag.  For an integer,
  the most significant segment is signed.
 */

/*
  How many decimal digits can fit in a machine word?  log2(10) is
  3.322, ie each base10 digit uses 3.322 binary bits.  19 of those
  would be 63.12 bits which means we'd not be able to store the sign
  of the value (which only applies to the MSS of an integer).  So 18
  decimal digits in 64 bits.  Ditto, 9 in 32.
  
  IDIO_BIGNUM_INT_SEG_LIMIT is the decimal representation of one more
  than IDIO_BIGNUM_DPW (digits per word), ie. if DPW was 3 then
  INT_SEG_LIMIT would be 1000 (one more than 999, the largest three
  digit decimal number).  It gets used to check when we are
  overflowing IDIO_BIGNUM_DPW.

  IDIO_BIGNUM_SIG_SEGMENTS is simply the number of elements we want to
  have in our bignum array and therefore the number of significant
  digits we can keep.  How many significant digits do you want?  There
  are 10^80 atoms in the universe.  By any measure of sensible
  characteristics of a programming language we should be able to
  accurately count that many...
  
 */

#ifdef __LP64__
#define IDIO_BIGNUM_DPW           18
#define IDIO_BIGNUM_INT_SEG_LIMIT 1000000000000000000LL
#define IDIO_BIGNUM_SIG_SEGMENTS  5
/*
#define IDIO_BIGNUM_DPW           3
#define IDIO_BIGNUM_INT_SEG_LIMIT 1000L
#define IDIO_BIGNUM_SIG_SEGMENTS  2
*/
#else
#define IDIO_BIGNUM_DPW           9
#define IDIO_BIGNUM_INT_SEG_LIMIT 1000000000L
#define IDIO_BIGNUM_SIG_SEGMENTS  10
#endif

#define IDIO_BIGNUM_SIG_MAX_DIGITS	  (IDIO_BIGNUM_SIG_SEGMENTS * IDIO_BIGNUM_DPW)

#define IDIO_BIGNUM_EXP_CHAR(c)	('d' == c || 'D' == c ||	 \
				 'e' == c || 'E' == c ||	 \
				 'f' == c || 'F' == c ||	 \
				 'l' == c || 'L' == c ||	 \
				 's' == c || 'S' == c)

#define IDIO_BIGNUM_INTEGER_P(bn)	(IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_INTEGER)
#define IDIO_BIGNUM_REAL_P(bn)		(IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_REAL)
#define IDIO_BIGNUM_REAL_NEGATIVE_P(bn)	((IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_REAL_NEGATIVE) && \
					 ! idio_bignum_real_zero_p (f, (bn)))
#define IDIO_BIGNUM_REAL_POSITIVE_P(bn)	(!IDIO_BIGNUM_REAL_NEGATIVE_P (bn))
#define IDIO_BIGNUM_REAL_INEXACT_P(bn)	(IDIO_BIGNUM_FLAGS (bn) & IDIO_BIGNUM_FLAG_REAL_INEXACT)

#define IDIO_BIGNUM_NAN			"NaN"

IDIO idio_bignum (IDIO f, int flags, int64_t exp, IDIO sig_a);
int idio_isa_bignum (IDIO f, IDIO bn);
void idio_free_bignum (IDIO f, IDIO bn);
IDIO idio_bignum_integer_int64 (IDIO f, int64_t i);
IDIO idio_bignum_integer (IDIO f, IDIO sig_a);
int idio_bignum_real_zero_p (IDIO f, IDIO a);
int idio_bignum_real_equal_p (IDIO f, IDIO a, IDIO b);
IDIO idio_bignum_scale_significand (IDIO f, IDIO bn, int64_t desired_exp, int max_size);
IDIO idio_bignum_integer_argument (IDIO f, IDIO bn);
int64_t idio_bignum_int64_value (IDIO f, IDIO bn);
IDIO idio_bignum_abs (IDIO f, IDIO bn);
int idio_bignum_negativep (IDIO f, IDIO bn);
IDIO idio_bignum_negate (IDIO f, IDIO bn);
IDIO idio_bignum_add (IDIO f, IDIO a, IDIO b);
int idio_bignum_zero_p (IDIO f, IDIO a);
int idio_bignum_lt_p (IDIO f, IDIO a, IDIO b);
int idio_bignum_equal_p (IDIO f, IDIO a, IDIO b);
IDIO idio_bignum_subtract (IDIO f, IDIO a, IDIO b);
IDIO idio_bignum_shift_left (IDIO f, IDIO a, int fill);
IDIO idio_bignum_shift_right (IDIO f, IDIO a);
IDIO idio_bignum_multiply (IDIO f, IDIO a, IDIO b);
IDIO idio_bignum_equalize (IDIO f, IDIO a, IDIO b);
IDIO idio_bignum_divide (IDIO f, IDIO a, IDIO b);
IDIO idio_bignum_real (IDIO f, int flags, int64_t exp, IDIO sig_a);
IDIO idio_bignum_real_to_integer (IDIO f, IDIO bn);
IDIO idio_bignum_real_to_inexact (IDIO f, IDIO bn);
IDIO idio_bignum_real_to_exact (IDIO f, IDIO bn);
char *idio_bignum_integer_as_string (IDIO f, IDIO o);
char *idio_bignum_real_as_string (IDIO f, IDIO o);
char *idio_bignum_as_string (IDIO f, IDIO o);
int idio_bignum_count_digits (IDIO f, IDIO sig_a);
IDIO idio_bignum_normalize (IDIO f, IDIO o);
char *idio_bignum_C_without_inexact (IDIO f, char *nums);
IDIO idio_bignum_integer_C (IDIO f, char *nums, int req_exact);
IDIO idio_bignum_real_C (IDIO f, char *nums);
IDIO idio_bignum_C (IDIO f, char *nums);

IDIO idio_bignum_primitive_add (IDIO f, IDIO args);
IDIO idio_bignum_primitive_subtract (IDIO f, IDIO args);
IDIO idio_bignum_primitive_multiply (IDIO f, IDIO args);
IDIO idio_bignum_primitive_divide (IDIO f, IDIO args);
IDIO idio_bignum_primitive_lt (IDIO f, IDIO args);
IDIO idio_bignum_primitive_le (IDIO f, IDIO args);
IDIO idio_bignum_primitive_gt (IDIO f, IDIO args);
IDIO idio_bignum_primitive_ge (IDIO f, IDIO args);
IDIO idio_bignum_primitive_eq (IDIO f, IDIO args);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
