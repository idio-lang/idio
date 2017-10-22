/*
 * Copyright (c) 2015, 2017 Ian Fitchet <idf(at)idio-lang.org>
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

void idio_frame_error_range (IDIO fo, size_t d, size_t i, IDIO loc)
{
    IDIO_ASSERT (fo);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (frame, fo);
    IDIO_TYPE_ASSERT (string, loc);

    char em[BUFSIZ];
    sprintf (em, "frame #%zd index #%zd is out of range", d, i);
    idio_error_C (em, IDIO_LIST1 (fo), loc);
}

IDIO idio_frame_allocate (idio_ai_t arityp1)
{
    IDIO_C_ASSERT (arityp1);
    
    IDIO fo = idio_gc_get (IDIO_TYPE_FRAME);

    IDIO_GC_ALLOC (fo->u.frame, sizeof (idio_frame_t));
    
    IDIO_FRAME_GREY (fo) = NULL;
    IDIO_FRAME_FLAGS (fo) = IDIO_FRAME_FLAG_NONE;
    IDIO_FRAME_NEXT (fo) = idio_S_nil;

    IDIO_FRAME_NARGS (fo) = arityp1;
    IDIO_FRAME_ARGS (fo) = idio_array (arityp1);

    /*
     * We must supply values into the array otherwise the array code
     * will think it is nominally empty (and flag errors when you try
     * to access any element).
     *
     * This array is arity+1 in length where the last argument is a
     * putative list of varargs.  It will have been be initialised by
     * the array code to idio_S_nil but unless we set it then the
     * array code will only allow access to the first arity elements.
     * The others should be initialised to idio_S_undef so that we
     * might spot misuse.
     */
    idio_ai_t i;
    for (i = 0; i < arityp1 - 1; i++) {
	idio_array_insert_index (IDIO_FRAME_ARGS (fo), idio_S_undef, i);
    }
    idio_array_insert_index (IDIO_FRAME_ARGS (fo), idio_S_nil, i);
    
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

    IDIO ofo = fo;
    
    for (; d; d--) {
	fo = IDIO_FRAME_NEXT (fo);
	IDIO_ASSERT (fo);
	IDIO_TYPE_ASSERT (frame, fo);
    }

    if (i >= idio_array_size (IDIO_FRAME_ARGS (fo))) {
	fprintf (stderr, "\n\nidio_frame_fetch (");
	idio_debug ("%s", ofo);
	fprintf (stderr, ", %zd, %zd)\n", d, i);
	fprintf (stderr, "ARGS = ");
	idio_gc_verboseness (3);
	idio_dump (IDIO_FRAME_ARGS (fo), 10);
	idio_gc_verboseness (0);
	fprintf (stderr, "\n\nFRAME = ");
	idio_dump (fo, 10);
	idio_frame_error_range (fo, d, i, IDIO_C_LOCATION ("idio_frame_fetch"));
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
	idio_frame_error_range (fo, d, i, IDIO_C_LOCATION ("idio_frame_update"));
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

    if (! idio_isa_frame (f2)) {
	/*
	 * The reason we should be here is because we've computed an
	 * argument frame and want to extend the current frame.  If f2
	 * isn't a frame value then something has gone horribly wrong.
	 *
	 * Abort!  Abort!  Abort!
	 *
	 * Hmm.  Aborting isn't that easy.  We can unwind the stack
	 * and then invoke a condition.  It shouldn't matter if the
	 * condition is continuable as we've just unwound the stack so
	 * there's no handlers to do anything with it.
	 */
	idio_vm_reset_thread (idio_thread_current_thread (), 1);
	idio_error_C ("not a frame", IDIO_LIST1 (f2), IDIO_C_LOCATION ("idio_frame_extend"));
    }

    IDIO_FRAME_NEXT (f2) = f1;

    return f2;
}

IDIO idio_frame_args_as_list (IDIO frame)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);

    IDIO r = idio_S_nil;
    
    IDIO args = IDIO_FRAME_ARGS (frame);
    idio_ai_t al = idio_array_size (args);
    if (al > 0) {
	idio_ai_t i;
	for (i = 0; i < al - 1; i++) {
	    r = idio_pair (idio_array_get_index (args, i), r);
	}

	r = idio_improper_list_reverse (r, idio_array_get_index (args, i));
    } else {
	fprintf (stderr, "idio_frame_args_as_list: al == %zd\n", al);
    }

    return r;
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

