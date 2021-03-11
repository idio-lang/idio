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
 * continuation.c
 *
 */

#include "idio.h"

IDIO idio_continuation (IDIO thr, int kind)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO k = idio_gc_get (IDIO_TYPE_CONTINUATION);

    IDIO_GC_ALLOC (k->u.continuation, sizeof (idio_continuation_t));

    IDIO_CONTINUATION_GREY (k) = NULL;
    IDIO_CONTINUATION_FLAGS (k) = IDIO_CONTINUATION_FLAG_NONE;
    IDIO_CONTINUATION_JMP_BUF (k) = IDIO_THREAD_JMP_BUF (thr);
    if (IDIO_CONTINUATION_CALL_DC == kind) {
	IDIO_CONTINUATION_STACK (k) = idio_S_nil;
	IDIO_CONTINUATION_FLAGS (k) = IDIO_CONTINUATION_FLAG_DELIMITED;
    } else {
	IDIO_CONTINUATION_STACK (k) = idio_copy_array (IDIO_THREAD_STACK (thr), IDIO_COPY_SHALLOW, 0);
#ifdef IDIO_VM_DYNAMIC_REGISTERS
	IDIO_CONTINUATION_ENVIRON_SP (k) = IDIO_THREAD_ENVIRON_SP (thr);
	IDIO_CONTINUATION_DYNAMIC_SP (k) = IDIO_THREAD_DYNAMIC_SP (thr);
	IDIO_CONTINUATION_TRAP_SP (k) = IDIO_THREAD_TRAP_SP (thr);
#endif
	IDIO_CONTINUATION_FRAME (k) = IDIO_THREAD_FRAME (thr);
	IDIO_CONTINUATION_ENV (k) = IDIO_THREAD_ENV (thr);
	IDIO_CONTINUATION_PC (k) = IDIO_THREAD_PC (thr);
	IDIO_CONTINUATION_THR (k) = thr;
    }

    return k;
}

int idio_isa_continuation (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_CONTINUATION);
}

IDIO_DEFINE_PRIMITIVE1_DS ("continuation?", continuation_p, (IDIO o), "o", "\
test if `o` is a continuation			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a continuation #f otherwise\n\
")
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

    IDIO_GC_FREE (k->u.continuation);
}

void idio_continuation_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (continuation_p);
}

void idio_init_continuation ()
{
    idio_module_table_register (idio_continuation_add_primitives, NULL);
}

