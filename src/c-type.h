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
 * c-type.h
 *
 */

#ifndef C_TYPE_H
#define C_TYPE_H

/*
 * Comparing IEEE 754 numbers is not easy.  See
 * https://floating-point-gui.de/errors/comparison/ and
 * https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
 *
 * These often fall back to the Units in Last Place (ULP) comparisons.
 */
typedef union idio_C_float_ULP_u {
    int32_t i;
    float f;
    struct {
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int sign:1;
	unsigned int exponent:8;
	unsigned int mantissa:23;
#else
	unsigned int mantissa:23;
	unsigned int exponent:8;
	unsigned int sign:1;
#endif
    } parts;
} idio_C_float_ULP_t;
#define IDIO_C_FLOAT_EXP_BIAS 127U
#define IDIO_C_FLOAT_MAN_BITS 23

typedef union idio_C_double_ULP_u {
    double d;
    int64_t i;
    struct {
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int sign:1;
	unsigned int exponent:11;
	unsigned long long int mantissa:52;
#else
	unsigned long long int mantissa:52;
	unsigned int exponent:11;
	unsigned int sign:1;
#endif
    } parts;
} idio_C_double_ULP_t;
#define IDIO_C_DOUBLE_EXP_BIAS 1023U
#define IDIO_C_DOUBLE_MAN_BITS 52

/*
 * XXX this is a bit (read: extremely) ropey.
 *
 * I don't know how big a long double is.  In fact, I'm not sure it's
 * terribly clear, see https://en.wikipedia.org/wiki/Long_double for a
 * lack of clarity.
 *
 * About all we have is that it is (usually) more than the 8 bytes of
 * a double.  So, let's aim for 128 bits which we can emulate with a
 * pair of int64_t (albeit the second could be a uint64_t as it's
 * probably just mantissa bits).
 *
 * This assumes that the compiler packs a 10 byte (I saw a reference
 * to somewhere) or 12 byte (several 32-bit operating systems) or 14
 * byte (maybe, who knows) implementation at the front of any word
 * aligned implementation (because those numbers are the storage
 * allocation not necessarily the IEEE 754 implementation width) and
 * therefore the sign bit of i.i1 overlaps the sign bit of the long
 * double.  If not we're totally goosed.
 *
 * And rightly so.
 */
typedef union idio_C_longdouble_ULP_u {
    long double ld;
    struct {
#if BYTE_ORDER == BIG_ENDIAN
	int64_t i1;
	int64_t i2;
#else
	int64_t i2;
	int64_t i1;
#endif
    } i;
    struct {
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int sign:1;
	unsigned int exponent:15;
	unsigned long long int mantissa:64;
#else
	unsigned long long int mantissa:64;
	unsigned int exponent:15;
	unsigned int sign:1;
#endif
    } parts_80bit;
    struct {
#if BYTE_ORDER == BIG_ENDIAN
	unsigned int sign:1;
	unsigned int exponent:15;
	unsigned int pad:16;
	unsigned long long int mantissa:64;
#else
	unsigned long long int mantissa:64;
	unsigned int pad:16;
	unsigned int exponent:15;
	unsigned int sign:1;
#endif
    } parts_96bit;
} idio_C_longdouble_ULP_t;
#define IDIO_C_LONGDOUBLE_EXP_BIAS 16383U
#define IDIO_C_LONGDOUBLE_MAN_BITS 64

void idio_free_C_type (IDIO co);

IDIO idio_C_char (char v);
int idio_isa_C_char (IDIO co);

IDIO idio_C_schar (signed char v);
int idio_isa_C_schar (IDIO co);

IDIO idio_C_uchar (unsigned char v);
int idio_isa_C_uchar (IDIO co);

IDIO idio_C_short (short v);
int idio_isa_C_short (IDIO co);

IDIO idio_C_ushort (unsigned short v);
int idio_isa_C_ushort (IDIO co);

IDIO idio_C_int (int v);
int idio_isa_C_int (IDIO co);

IDIO idio_C_uint (unsigned int v);
int idio_isa_C_uint (IDIO co);

IDIO idio_C_long (long v);
int idio_isa_C_long (IDIO co);

IDIO idio_C_ulong (unsigned long v);
int idio_isa_C_ulong (IDIO co);

IDIO idio_C_longlong (long long v);
int idio_isa_C_longlong (IDIO co);

IDIO idio_C_ulonglong (unsigned long long v);
int idio_isa_C_ulonglong (IDIO co);

IDIO idio_C_float (float v);
int idio_isa_C_float (IDIO co);

IDIO idio_C_double (double v);
int idio_isa_C_double (IDIO co);

IDIO idio_C_longdouble (long double v);
int idio_isa_C_longdouble (IDIO co);

IDIO idio_C_pointer (void * v);
int idio_isa_C_pointer (IDIO co);

IDIO idio_C_pointer_free_me (void * v);
IDIO idio_C_pointer_type_add_vtable (IDIO t);
IDIO idio_C_pointer_type (IDIO t, void * v);
void idio_free_C_pointer (IDIO co);

int idio_isa_C_type (IDIO o);
int idio_isa_C_number (IDIO o);
int idio_isa_C_integral (IDIO o);
int idio_isa_C_unsigned (IDIO o);

char *idio_C_type_format_string (int type);

IDIO idio_C_primitive_unary_abs (IDIO n);

IDIO idio_C_primitive_binary_add (IDIO n1, IDIO n2);
IDIO idio_C_primitive_binary_subtract (IDIO n1, IDIO n2);
IDIO idio_C_primitive_binary_multiply (IDIO n1, IDIO n2);
IDIO idio_C_primitive_binary_divide (IDIO n1, IDIO n2);

IDIO idio_C_number_cast (IDIO co, idio_type_e type);

int idio_C_float_C_eq_ULP (float o1, float o2, unsigned int max);
int idio_C_float_C_ne_ULP (float o1, float o2, unsigned int max);
int idio_C_double_C_eq_ULP (double o1, double o2, unsigned int max);
int idio_C_double_C_ne_ULP (double o1, double o2, unsigned int max);
int idio_C_longdouble_C_eq_ULP (long double o1, long double o2, unsigned int max);
int idio_C_longdouble_C_ne_ULP (long double o1, long double o2, unsigned int max);

char *idio_C_char_as_C_string (IDIO v, size_t *sizep, idio_unicode_t cs, IDIO seen, int depth);
char *idio_C_number_as_C_string (IDIO v, size_t *sizep, idio_unicode_t cs, IDIO seen, int depth);
char *idio_C_pointer_report_string (IDIO v, size_t *sizep, idio_unicode_t cs, IDIO seen, int depth);
char *idio_C_pointer_as_C_string (IDIO v, size_t *sizep, idio_unicode_t cs, IDIO seen, int depth);

void idio_init_c_type ();

#endif /* AUTO_C_TYPE_H */

