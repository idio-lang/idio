/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

void idio_frame_error_range (IDIO fo, size_t d, size_t i, IDIO c_location)
{
    IDIO_ASSERT (fo);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (frame, fo);
    IDIO_TYPE_ASSERT (string, c_location);

    char em[BUFSIZ];
    sprintf (em, "frame #%zd index #%zd is out of range", d, i);
    idio_error_C (em, fo, c_location);
}

IDIO idio_frame_allocate (idio_ai_t arityp1)
{
    IDIO_C_ASSERT (arityp1);

    IDIO fo = idio_gc_get (IDIO_TYPE_FRAME);

    IDIO_GC_ALLOC (fo->u.frame, sizeof (idio_frame_t) + arityp1 * sizeof (idio_t));

    IDIO_FRAME_GREY (fo) = NULL;
    IDIO_FRAME_FLAGS (fo) = IDIO_FRAME_FLAG_NONE;
    IDIO_FRAME_NEXT (fo) = idio_S_nil;

    IDIO_FRAME_NARGS (fo) = arityp1;
    IDIO_FRAME_NAMES (fo) = idio_S_nil;

    idio_ai_t i;
    for (i = 0; i < arityp1 - 1; i++) {
	IDIO_FRAME_ARGS (fo, i) = idio_S_undef;
    }
    IDIO_FRAME_ARGS (fo, i) = idio_S_nil;

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
	IDIO_FRAME_ARGS (fo, i++) = IDIO_PAIR_H (args);
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

    if (i >= IDIO_FRAME_NARGS (fo)) {
	idio_dump (ofo, 10);
	idio_dump (fo, 10);
	idio_frame_error_range (fo, d, i, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_FRAME_ARGS (fo, i);
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

    if (i >= IDIO_FRAME_NARGS (fo)) {
	idio_frame_error_range (fo, d, i, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO_FRAME_ARGS (fo, i) = v;
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
	idio_error_C ("not a frame", IDIO_LIST1 (f2), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_FRAME_NEXT (f2) = f1;

    return f2;
}

IDIO idio_frame_args_as_list_from (IDIO frame, idio_ai_t from)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);

    IDIO r = idio_S_nil;

    idio_ai_t nargs = IDIO_FRAME_NARGS (frame);
    if (nargs > 0) {
	idio_ai_t i;
	for (i = nargs - 1; i >= from; i--) {
	    r = idio_pair (IDIO_FRAME_ARGS (frame, i),
			   r);
	}
    }

    return r;
}

IDIO idio_frame_args_as_list (IDIO frame)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);

    return idio_frame_args_as_list_from (frame, 0);
}

/*
 * Not quite the same as idio_frame_args_as_list because we don't
 * include the varargs element if it is #n.
 *
 * It's only real use is pretty-printing function calls.
 */
IDIO idio_frame_params_as_list (IDIO frame)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);

    IDIO r = idio_S_nil;
    idio_ai_t nargs = IDIO_FRAME_NARGS (frame);

    if (idio_S_nil != IDIO_FRAME_ARGS (frame, nargs - 1)) {
	r = idio_pair (IDIO_FRAME_ARGS (frame, nargs - 1), r);
    }

    if (nargs > 1) {
	idio_ai_t i;
	for (i = nargs - 2; i >= 0; i--) {
	    r = idio_pair (IDIO_FRAME_ARGS (frame, i),
			   r);
	}
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

