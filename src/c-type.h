/*
 * Copyright (c) 2015, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

void idio_free_C_type (IDIO co);

IDIO idio_C_char (char v);
int idio_isa_C_char (IDIO co);
char idio_C_char_get (IDIO co);

IDIO idio_C_schar (signed char v);
int idio_isa_C_schar (IDIO co);
signed char idio_C_schar_get (IDIO co);

IDIO idio_C_uchar (unsigned char v);
int idio_isa_C_uchar (IDIO co);
unsigned char idio_C_uchar_get (IDIO co);

IDIO idio_C_short (short v);
int idio_isa_C_short (IDIO co);
short idio_C_short_get (IDIO co);

IDIO idio_C_ushort (unsigned short v);
int idio_isa_C_ushort (IDIO co);
unsigned short idio_C_ushort_get (IDIO co);

IDIO idio_C_int (int v);
int idio_isa_C_int (IDIO co);
int idio_C_int_get (IDIO co);

IDIO idio_C_uint (unsigned int v);
int idio_isa_C_uint (IDIO co);
unsigned int idio_C_uint_get (IDIO co);

IDIO idio_C_long (long v);
int idio_isa_C_long (IDIO co);
long idio_C_long_get (IDIO co);

IDIO idio_C_ulong (unsigned long v);
int idio_isa_C_ulong (IDIO co);
unsigned long idio_C_ulong_get (IDIO co);

IDIO idio_C_longlong (long long v);
int idio_isa_C_longlong (IDIO co);
long long idio_C_longlong_get (IDIO co);

IDIO idio_C_ulonglong (unsigned long long v);
int idio_isa_C_ulonglong (IDIO co);
unsigned long long idio_C_ulonglong_get (IDIO co);

IDIO idio_C_float (float v);
int idio_isa_C_float (IDIO co);
float idio_C_float_get (IDIO co);

IDIO idio_C_double (double v);
int idio_isa_C_double (IDIO co);
double idio_C_double_get (IDIO co);

IDIO idio_C_longdouble (long double v);
int idio_isa_C_longdouble (IDIO co);
long double idio_C_longdouble_get (IDIO co);

IDIO idio_C_pointer (void * v);
int idio_isa_C_pointer (IDIO co);
void * idio_C_pointer_get (IDIO co);

IDIO idio_C_pointer_free_me (void * v);
void idio_free_C_pointer (IDIO co);
IDIO idio_C_number_cast (IDIO co, idio_type_e type);

void idio_init_c_type ();

#endif /* AUTO_C_TYPE_H */

