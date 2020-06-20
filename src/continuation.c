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
 * continuation.c
 *
 */

#include "idio.h"

IDIO idio_continuation (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO k = idio_gc_get (IDIO_TYPE_CONTINUATION);

    IDIO_GC_ALLOC (k->u.continuation, sizeof (idio_continuation_t));

    IDIO_CONTINUATION_GREY (k) = NULL;
    IDIO_CONTINUATION_JMP_BUF (k) = IDIO_THREAD_JMP_BUF (thr);
    IDIO_CONTINUATION_STACK (k) = idio_array_copy (IDIO_THREAD_STACK (thr), IDIO_COPY_SHALLOW, 8);

    /*
     * XXX same order as idio_vm_preserve_state() !!!
     */
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_ENVIRON_SP (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_DYNAMIC_SP (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_TRAP_SP (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_FRAME (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_ENV (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), idio_SM_preserve_state);

    idio_ai_t pc = IDIO_THREAD_PC (thr);

    /*
     * Yuk!
     *
     * A FUNCTION_INVOKE (as opposed to a FUNCTION_GOTO) will preserve
     * the thread state before calling the function.  Quite right.
     *
     * However, that means the PC is pointing at a RESTORE_STATE
     * instruction which isn't quite what we want when we restore this continuation.
     *
     * If we nudge the PC for the continuation along one instruction
     * then we're just about in the right position for someone to
     * invoke this continuation with a value and the right thing
     * happens.
     */
    if (pc > 1 &&
	IDIO_A_FUNCTION_INVOKE == idio_all_code->ae[pc - 1]) {
	fprintf (stderr, "make-continuation: frigged with PC=%td\n", pc);
	/* pc++; */
    }
    idio_array_push (IDIO_CONTINUATION_STACK (k), idio_fixnum (pc));

    idio_array_push (IDIO_CONTINUATION_STACK (k), idio_SM_preserve_continuation);

    return k;
}

int idio_isa_continuation (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_CONTINUATION);
}

IDIO_DEFINE_PRIMITIVE1 ("continuation?", continuation_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_continuation (o)) {
	r = idio_S_true;
    }

    return r;
}

void idio_free_continuation (IDIO k)
{
    IDIO_ASSERT (k);
    IDIO_TYPE_ASSERT (continuation, k);

    idio_gc_stats_free (sizeof (idio_continuation_t));

    free (k->u.continuation);
}

void idio_init_continuation ()
{
}

void idio_continuation_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (continuation_p);
}

void idio_final_continuation ()
{
}
