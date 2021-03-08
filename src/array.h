/*
 * Copyright (c) 2015, 2020, 2021, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * array.h
 *
 */

#ifndef ARRAY_H
#define ARRAY_H

#include "idio.h"

void idio_assign_array (IDIO a, idio_ai_t size, IDIO dv);
IDIO idio_array_dv (idio_ai_t size, IDIO dv);
IDIO idio_array (idio_ai_t size);
int idio_isa_array (IDIO a);
void idio_free_array (IDIO a);
void idio_resize_array (IDIO a);
idio_ai_t idio_array_size (IDIO a);
void idio_array_insert_index (IDIO a, IDIO io, idio_ai_t index);
void idio_array_push (IDIO a, IDIO io);
void idio_array_push_n (IDIO a, size_t nargs, ...);
IDIO idio_array_pop (IDIO a);
IDIO idio_array_shift (IDIO a);
void idio_array_unshift (IDIO a, IDIO io);
IDIO idio_array_top (IDIO a);
IDIO idio_array_ref_index (IDIO a, idio_ai_t index);
idio_ai_t idio_array_find_eqp (IDIO a, IDIO e, idio_ai_t index);
idio_ai_t idio_array_find_equalp (IDIO a, IDIO e, idio_ai_t index);
IDIO idio_copy_array (IDIO a, int depth, idio_ai_t extra);
void idio_duplicate_array (IDIO a, IDIO o, idio_ai_t n, int depth);
IDIO idio_array_to_list_from (IDIO a, idio_ai_t index);
IDIO idio_array_to_list (IDIO a);
IDIO idio_array_ref (IDIO a, IDIO index);
IDIO idio_array_set (IDIO a, IDIO index, IDIO v);

void idio_init_array ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
