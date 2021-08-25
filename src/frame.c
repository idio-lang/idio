/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ffi.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "error.h"
#include "frame.h"
#include "idio-string.h"
#include "pair.h"
#include "thread.h"
#include "vm.h"

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

    IDIO_GC_ALLOC (fo->u.frame, sizeof (idio_frame_t));
    IDIO_GC_ALLOC (fo->u.frame->args, arityp1 * sizeof (IDIO));

    IDIO_FRAME_GREY (fo) = NULL;
    IDIO_FRAME_FLAGS (fo) = IDIO_FRAME_FLAG_NONE;
    IDIO_FRAME_NEXT (fo) = idio_S_nil;

    IDIO_FRAME_NPARAMS (fo) = arityp1 - 1;
    IDIO_FRAME_NALLOC (fo) = arityp1;
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

    if (!(idio_S_nil == next ||
	  idio_isa_frame (next))) {
	idio_error_param_type ("frame", next, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

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

    IDIO_GC_FREE (fo->u.frame->args);
    IDIO_GC_FREE (fo->u.frame);
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

    if (i >= IDIO_FRAME_NALLOC (fo)) {
	idio_vm_frame_tree (idio_S_nil);
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

    if (i >= IDIO_FRAME_NALLOC (fo)) {
	idio_vm_frame_tree (idio_S_nil);
	idio_frame_error_range (fo, d, i, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO_FRAME_ARGS (fo, i) = v;
}

IDIO idio_link_frame (IDIO f1, IDIO f2)
{
    IDIO_ASSERT (f1);
    IDIO_ASSERT (f2);

    if (idio_S_nil != f1) {
	IDIO_TYPE_ASSERT (frame, f1);
    }

    if (! idio_isa_frame (f2)) {
	/*
	 * The reason we should be here is because we've computed an
	 * argument frame and want to link it into the current frame.
	 * If f2 isn't a frame value then something has gone horribly
	 * wrong.
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

void idio_extend_frame (IDIO fo, size_t nalloc)
{
    IDIO_ASSERT (fo);
    IDIO_C_ASSERT (nalloc);

    size_t nparams = IDIO_FRAME_NPARAMS (fo);
    size_t oalloc = IDIO_FRAME_NALLOC (fo);

    if ((nparams + 1) == nalloc) {
	return;
    } else if (nalloc < oalloc) {
	/*
	 * The original frame was created for a varargs function which
	 * subsequently packed the frame.  Hence NALLOC > NPARAMS+1
	 * because the extra are now a list *in* NPARAMS+1.
	 *
	 * The code in the function doesn't know that and is now
	 * simply trying to use slot NPARAMS+2 which is (usually) <<
	 * NALLOC
	 */
	return;
    }

    fo->u.frame->args = idio_realloc (fo->u.frame->args, nalloc * sizeof (IDIO));

    IDIO_FRAME_NALLOC (fo) = nalloc;

    idio_ai_t i;
    for (i = oalloc; i < nalloc; i++) {
	IDIO_FRAME_ARGS (fo, i) = idio_S_undef;
    }
}

IDIO idio_frame_args_as_list_from (IDIO frame, idio_ai_t from)
{
    IDIO_ASSERT (frame);
    IDIO_TYPE_ASSERT (frame, frame);

    idio_ai_t nargs = IDIO_FRAME_NPARAMS (frame);
    IDIO r = IDIO_FRAME_ARGS (frame, nargs);

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

    idio_ai_t nargs = IDIO_FRAME_NPARAMS (frame);
    IDIO r = IDIO_FRAME_ARGS (frame, nargs);

    if (nargs > 0) {
	idio_ai_t i;
	for (i = nargs - 1; i >= 0; i--) {
	    r = idio_pair (IDIO_FRAME_ARGS (frame, i),
			   r);
	}
    }

    return r;
}

void idio_init_frame ()
{
    /*
     * XXX nothing to do here
     */
    idio_module_table_register (NULL, NULL, NULL);
}

