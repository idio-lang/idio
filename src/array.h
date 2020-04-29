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
 * array.h
 *
 */

#ifndef ARRAY_H
#define ARRAY_H

#include "idio.h"

/*
 * Array Indices
 *
 * I've misread things.  size_t is for *arrays* and arrays can be
 * indexed up to SIZE_MAX.  Arrays are not pointers!
 *
 * On normal machines size_t is probably the same as intptr_t (an
 * integral type that can contain a pointer).  On segmented
 * architectures, SIZE_MAX might be 65535, the index of the largest
 * addressable element of an array on this architecture if it has
 * 16bit segments.
 *
 * Following that, ptrdiff_t is the difference between the indices of
 * two elements *in the same array* (and is not well defined
 * otherwise).  Technically, ptrdiff_t requires one more bit than
 * size_t (as it can be negative) but otherwise is the same (broad)
 * size and unrelated to pointers.
 *
 * Hence, if you are using array indexing, "a[i]", then you should be
 * using size_t/ptrdiff_t to be portable.  We are using array
 * indexing, usually via "IDIO_ARRAY_AE (a, i)" although the odd a[i]
 * creeps in.
 *
 * So, let's revisit the original comment:
 *
 * C99 suggests that sizes should be size_t so we could create an
 * array with SIZE_MAX elements.  On non-segmented architectures, such
 * a memory allocation will almost certainly fail(1) but, sticking to
 * principles, someone might want to create a just-over-half-of-memory
 * (2**(n-1))+1 element array.
 *
 * (1) as every Idio array element is a pointer, ie 4 or 8 bytes, then
 * we can't physically allocate nor address 2**32 * 4 bytes or 2**64 *
 * 8 bytes just for the array as those are 4 and 8 times larger than
 * addressable memory.  So, in practice, we're limited to arrays of
 * length 2**30 or 2**61 -- with no room for any other data!
 *
 *   As a real-world example, on an OpenSolaris 4GB/32bit machine:
 *
 *     make-array ((expt 2 29) - 1)
 *
 *   was successful.  2**30-1 was not.
 *
 * However, at some point we should accomodate negative array indices,
 * eg. the nominal, array[-i], which we take to mean the i'th last
 * index.  The means using a signed type even if we won't ever
 * actually use a[-i] -- as we'll convert it into a[size-i].
 *
 * So, the type we use must be ptrdiff_t and therefore the largest
 * positive index is PTRDIFF_MAX.
 *
 * in gc.h:
 *
 * typedef ptrdiff_t idio_ai_t;
 */

void idio_assign_array (IDIO a, idio_ai_t size, IDIO dv);
IDIO idio_array (idio_ai_t size);
int idio_isa_array (IDIO a);
void idio_free_array (IDIO a);
void idio_array_resize (IDIO a);
idio_ai_t idio_array_size (IDIO a);
void idio_array_insert_index (IDIO a, IDIO io, idio_ai_t index);
void idio_array_push (IDIO a, IDIO io);
IDIO idio_array_pop (IDIO a);
IDIO idio_array_shift (IDIO a);
void idio_array_unshift (IDIO a, IDIO io);
IDIO idio_array_head (IDIO a);
IDIO idio_array_top (IDIO a);
IDIO idio_array_get_index (IDIO a, idio_ai_t index);
idio_ai_t idio_array_find_free_index (IDIO a, idio_ai_t index);
idio_ai_t idio_array_find_eqp (IDIO a, IDIO e, idio_ai_t index);
void idio_array_bind (IDIO a, idio_ai_t nargs, ...);
IDIO idio_array_copy (IDIO a, int depth, idio_ai_t extra);
IDIO idio_array_to_list (IDIO a);
int idio_array_delete_index (IDIO a, idio_ai_t index);
IDIO idio_array_ref (IDIO a, IDIO index);
IDIO idio_array_set (IDIO a, IDIO index, IDIO v);

void idio_init_array ();
void idio_array_add_primitives ();
void idio_final_array ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
