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
    char *fos = idio_as_string (fo, 1);
    fprintf (stderr, "%s\n", fos);
    free (fos);
    idio_error_message ("frame #%zd index #%zd is out of range for %p", d, i, fo);
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

    /*
     * We must supply values into the array otherwise the array will
     * think it is empty (and flag errors when you try to access any
     * element).
     *
     * This array is arity+1 in length where the last argument is a
     * putative list of varargs.  It will have been be initialised by
     * the array code to idio_S_nil.  The others should be initialised
     * to idio_S_undef so that we might spot misuse.
     *
     * We don't touch the last argument otherwise the array will think
     * it is set and therefore it'll be passed as a list of nil,
     * "(nil)", rather than just nil.
     */
    idio_ai_t i;
    for (i = 0; i < nargs - 1; i++) {
	idio_array_insert_index (IDIO_FRAME_ARGS (fo), idio_S_undef, i);
    }

    return fo;
}

IDIO idio_frame (IDIO next, IDIO args)
{
    IDIO_ASSERT (next);
    IDIO_ASSERT (args);
    
    idio_ai_t nargs = idio_list_length (args);
    
    IDIO fo = idio_frame_allocate (nargs + 1);

    IDIO_FRAME_NEXT (fo) = next;

    idio_ai_t i = 0;
    while (idio_S_nil != args) {
	idio_array_insert_index (IDIO_FRAME_ARGS (fo), IDIO_PAIR_H (args), i++);
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
	fprintf (stderr, "\n\nARGS = ");
	idio_gc_verboseness (3);
	idio_dump (IDIO_FRAME_ARGS (fo), 10);
	idio_gc_verboseness (0);
	fprintf (stderr, "\n\nFRAME = ");
	idio_dump (fo, 10);
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

    if (i > idio_array_size (IDIO_FRAME_ARGS (fo))) {
	idio_error_frame_range (fo, d, i);
	return;
    }
    
    idio_array_insert_index (IDIO_FRAME_ARGS (fo), v, i);
}

IDIO idio_frame_extend (IDIO f1, IDIO f2)
{
    IDIO_ASSERT (f1);
    IDIO_ASSERT (f2);

    if (idio_S_nil != f1) {
	IDIO_TYPE_ASSERT (frame, f1);
    }
    IDIO_TYPE_ASSERT (frame, f2);

    IDIO_FRAME_NEXT (f2) = f1;

    return f2;
}

void idio_init_frame ()
{
}

void idio_frame_add_primitives ()
{
}

void idio_final_frame ()
{
}

