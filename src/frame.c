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
 * frame.c
 * 
 */

#include "idio.h"

IDIO idio_G_frame;

void idio_error_frame_range (IDIO fo, size_t d, size_t i)
{
    idio_error_message ("%zd/%zd out of range for %p", d, i, fo);
}

void idio_init_frame ()
{
}

void idio_final_frame ()
{
}

IDIO idio_frame_allocate (idio_ai_t nargs)
{
    IDIO_C_ASSERT (nargs);
    
    IDIO fo = idio_gc_get (IDIO_TYPE_FRAME);

    IDIO_GC_ALLOC (fo->u.frame, sizeof (idio_frame_t));
    
    IDIO_FRAME_GREY (fo) = NULL;
    IDIO_FRAME_FLAGS (fo) = IDIO_FRAME_FLAG_NONE;
    IDIO_FRAME_NEXT (fo) = idio_S_nil;

    IDIO_FRAME_NARGS (fo) = nargs;
    IDIO_FRAME_ARGS (fo) = idio_array (nargs);

    return fo;
}

IDIO idio_frame (IDIO next, IDIO args)
{
    IDIO_ASSERT (next);
    IDIO_ASSERT (args);
    
    idio_ai_t nargs = idio_list_length (args);
    
    IDIO fo = idio_frame_allocate (nargs);

    IDIO_FRAME_NEXT (fo) = next;

    while (idio_S_nil != args) {
	idio_array_push (IDIO_FRAME_ARGS (fo), IDIO_PAIR_H (args));
	args = IDIO_PAIR_T (args);
    }

    return fo;
}

int idio_isa_frame (IDIO fo)
{
    IDIO_ASSERT (fo);

    return idio_isa (fo, IDIO_TYPE_FRAME);
}

void idio_free_frame (IDIO fo)
{
    IDIO_ASSERT (fo);

    IDIO_TYPE_ASSERT (frame, fo);

    idio_gc_stats_free (sizeof (idio_frame_t));

    free (fo->u.frame);
}

IDIO idio_frame_fetch (IDIO fo, size_t d, size_t i)
{
    IDIO_ASSERT (fo);
    IDIO_TYPE_ASSERT (frame, fo);

    for (; d; d--) {
	fo = IDIO_FRAME_NEXT (fo);
	IDIO_ASSERT (fo);
	IDIO_TYPE_ASSERT (frame, fo);
    }

    if (i >= idio_array_size (IDIO_FRAME_ARGS (fo))) {
	idio_error_frame_range (fo, d, i);
	return idio_S_unspec;
    }
    
    return idio_array_get_index (IDIO_FRAME_ARGS (fo), i);
}

void idio_frame_update (IDIO fo, size_t d, size_t i, IDIO v)
{
    IDIO_ASSERT (fo);
    IDIO_TYPE_ASSERT (frame, fo);
    IDIO_ASSERT (v);

    for (; d; d--) {
	fo = IDIO_FRAME_NEXT (fo);
	IDIO_ASSERT (fo);
	IDIO_TYPE_ASSERT (frame, fo);
    }

    if (i >= idio_array_size (IDIO_FRAME_ARGS (fo))) {
	idio_error_frame_range (fo, d, i);
	return;
    }
    
    idio_array_insert_index (IDIO_FRAME_ARGS (fo), v, i);
}

void idio_frame_extend (IDIO f1, IDIO f2)
{
    IDIO_ASSERT (f1);
    IDIO_ASSERT (f2);
    IDIO_TYPE_ASSERT (frame, f1);
    IDIO_TYPE_ASSERT (frame, f2);

    IDIO_FRAME_NEXT (f2) = f1;
}

