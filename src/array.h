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
 * array.h
 * 
 */

#ifndef ARRAY_H
#define ARRAY_H

#include "idio.h"

/*
 * Array Indices
 *
 * C99 suggests that sizes should be size_t so we could create an
 * array with UINTMAX_MAX elements (2**n).  In practice, such a memory
 * allocation will almost certainly fail but, sticking to principles,
 * someone might create a just-over-half-of-memory (2**(n-1))+1
 * element array.
 *
 * However, at some point we should accomodate negative array indices,
 * eg. array[-1], the last index.  The means a signed type, the
 * largest integral signed type is intmax_t/ptrdiff_t and therefore
 * the largest positive index is PTRDIFF_MAX.  We're talking about
 * creating an array (of pointers) with (2**n)-1 elements.  We
 * shouldn't get too upset we can't create anything bigger, that's
 * still pretty big.
 *
 * in gc.h:
 *
 * typedef ptrdiff_t idio_index_t;
 */

void idio_assign_array (IDIO a, size_t size);
IDIO idio_array (size_t size);
int idio_isa_array (IDIO a);
void idio_free_array (IDIO a);
void idio_array_resize (IDIO a);
idio_index_t idio_array_size (IDIO a);
void idio_array_insert_index (IDIO a, IDIO io, idio_index_t index);
void idio_array_push (IDIO a, IDIO io);
IDIO idio_array_pop (IDIO a);
IDIO idio_array_shift (IDIO a);
void idio_array_unshift (IDIO a, IDIO io);
IDIO idio_array_head (IDIO a);
IDIO idio_array_top (IDIO a);
IDIO idio_array_get_index (IDIO a, idio_index_t index);
idio_index_t idio_array_find_free_index (IDIO a, idio_index_t index);
idio_index_t idio_array_find_eqp (IDIO a, IDIO e, idio_index_t index);
void idio_array_bind (IDIO a, size_t nargs, ...);
IDIO idio_array_copy (IDIO a, size_t extra);
IDIO idio_array_to_list (IDIO a);
int idio_array_delete_index (IDIO a, idio_index_t index);

void idio_init_array ();
void idio_final_array ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
