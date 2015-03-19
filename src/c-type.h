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
 * c-type.h
 * 
 */

#ifndef C_TYPE_H
#define C_TYPE_H

IDIO idio_C_int8 (int8_t i);
int idio_isa_C_int8 (IDIO co);
IDIO idio_C_uint8 (uint8_t i);
int idio_isa_C_uint8 (IDIO co);
IDIO idio_C_int16 (int16_t i);
int idio_isa_C_int16 (IDIO co);
IDIO idio_C_uint16 (uint16_t i);
int idio_isa_C_uint16 (IDIO co);
IDIO idio_C_int32 (int32_t i);
int idio_isa_C_int32 (IDIO co);
IDIO idio_C_uint32 (uint32_t i);
int idio_isa_C_uint32 (IDIO co);
IDIO idio_C_int64 (int64_t i);
int idio_isa_C_int64 (IDIO co);
IDIO idio_C_uint64 (uint64_t i);
int idio_isa_C_uint64 (IDIO co);
IDIO idio_C_float (float fl);
int idio_isa_C_float (IDIO co);
IDIO idio_C_double (double d);
int idio_isa_C_double (IDIO co);
void idio_free_C_type (IDIO co);
IDIO idio_C_pointer (void *p);
int idio_isa_C_pointer (IDIO co);
IDIO idio_C_pointer_free_me (void *p);
void idio_free_C_pointer (IDIO co);

IDIO idio_C_number_cast (IDIO co, int type);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
