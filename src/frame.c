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

void idio_init_frame ()
{
}

void idio_final_frame ()
{
}

IDIO idio_frame (size_t esize, size_t ssize)
{

    IDIO fo = idio_gc_get (IDIO_TYPE_FRAME);

    IDIO_GC_ALLOC (fo->u.frame, sizeof (idio_frame_t));
    
    IDIO_FRAME_GREY (fo) = NULL;
    IDIO_FRAME_FORM (fo) = NULL;
    IDIO_FRAME_FLAGS (fo) = IDIO_FRAME_FLAG_NONE;

    /*
      IDIO_FRAME_PFRAME (fo) = f;
      IDIO_FRAME_NAMESPACE (fo) = IDIO_FRAME_NAMESPACE (f);
    
    if (esize) {
	IDIO_FRAME_ENV (fo) = IDIO_HASH_EQP (esize);
    } else {
	IDIO_FRAME_ENV (fo) = IDIO_FRAME_ENV (f);
    }
    if (ssize) {
	IDIO_FRAME_STACK (fo) = idio_array (ssize);
    } else {
	IDIO_FRAME_STACK (fo) = IDIO_FRAME_STACK (f);
    }
    IDIO_FRAME_THREADS (fo) = IDIO_FRAME_THREADS (f);
    IDIO_FRAME_ERROR (fo) = IDIO_FRAME_ERROR (f);
    */

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

