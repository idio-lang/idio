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
    IDIO_CONTINUATION_STACK (k) = idio_array_copy (IDIO_THREAD_STACK (thr), 5);

    /*
     * XXX same order as idio_vm_preserve_environment() !!!
     */
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_ENVIRON_SP (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_DYNAMIC_SP (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_HANDLER_SP (thr));
    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_THREAD_ENV (thr));

    idio_array_push (IDIO_CONTINUATION_STACK (k), IDIO_FIXNUM (IDIO_THREAD_PC (thr)));

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