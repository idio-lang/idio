/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

void idio_free_C_type (struct idio_s *co);
struct idio_s *idio_C_int (intmax_t v);
int idio_isa_C_int (struct idio_s *co);
intmax_t idio_C_int_get (struct idio_s *co);
struct idio_s *idio_C_uint (uintmax_t v);
int idio_isa_C_uint (struct idio_s *co);
uintmax_t idio_C_uint_get (struct idio_s *co);
struct idio_s *idio_C_float (float v);
int idio_isa_C_float (struct idio_s *co);
float idio_C_float_get (struct idio_s *co);
struct idio_s *idio_C_double (double v);
int idio_isa_C_double (struct idio_s *co);
double idio_C_double_get (struct idio_s *co);
struct idio_s *idio_C_pointer (void * v);
int idio_isa_C_pointer (struct idio_s *co);
void * idio_C_pointer_get (struct idio_s *co);
struct idio_s *idio_C_pointer_free_me (void * v);
void idio_free_C_pointer (struct idio_s *co);
IDIO idio_C_number_cast (IDIO co, idio_type_e type);

void idio_init_c_type ();

#endif /* AUTO_C_TYPE_H */

