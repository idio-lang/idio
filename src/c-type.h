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

IDIO idio_C_int8 (IDIO f, int8_t i);
int idio_isa_C_int8 (IDIO f, IDIO co);
IDIO idio_C_uint8 (IDIO f, uint8_t i);
int idio_isa_C_uint8 (IDIO f, IDIO co);
IDIO idio_C_int16 (IDIO f, int16_t i);
int idio_isa_C_int16 (IDIO f, IDIO co);
IDIO idio_C_uint16 (IDIO f, uint16_t i);
int idio_isa_C_uint16 (IDIO f, IDIO co);
IDIO idio_C_int32 (IDIO f, int32_t i);
int idio_isa_C_int32 (IDIO f, IDIO co);
IDIO idio_C_uint32 (IDIO f, uint32_t i);
int idio_isa_C_uint32 (IDIO f, IDIO co);
IDIO idio_C_int64 (IDIO f, int64_t i);
int idio_isa_C_int64 (IDIO f, IDIO co);
IDIO idio_C_uint64 (IDIO f, uint64_t i);
int idio_isa_C_uint64 (IDIO f, IDIO co);
IDIO idio_C_float (IDIO f, float fl);
int idio_isa_C_float (IDIO f, IDIO co);
IDIO idio_C_double (IDIO f, double d);
int idio_isa_C_double (IDIO f, IDIO co);
void idio_free_C_type (IDIO f, IDIO co);
IDIO idio_C_pointer (IDIO f, void *p);
int idio_isa_C_pointer (IDIO f, IDIO co);
IDIO idio_C_pointer_free_me (IDIO f, void *p);
void idio_free_C_pointer (IDIO f, IDIO co);

IDIO idio_C_number_cast (IDIO f, IDIO co, int type);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
