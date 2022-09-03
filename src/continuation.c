/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "continuation.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "idio-string.h"
#include "pair.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

IDIO idio_continuation (IDIO thr, int kind)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    IDIO k = idio_gc_get (IDIO_TYPE_CONTINUATION);
    k->vtable = idio_vtable (IDIO_TYPE_CONTINUATION);

    IDIO_GC_ALLOC (k->u.continuation, sizeof (idio_continuation_t));

    IDIO_CONTINUATION_GREY (k)		= NULL;
    IDIO_CONTINUATION_PC (k)		= IDIO_THREAD_PC (thr);
    IDIO_CONTINUATION_XI (k)		= IDIO_THREAD_XI (thr);
    if (IDIO_CONTINUATION_CALL_DC == kind) {
	IDIO_CONTINUATION_STACK (k)	= idio_fixnum (idio_array_size (IDIO_THREAD_STACK (thr)));
	IDIO_CONTINUATION_FLAGS (k)	= IDIO_CONTINUATION_FLAG_DELIMITED;
    } else {
	IDIO_CONTINUATION_STACK (k)	= idio_copy_array (IDIO_THREAD_STACK (thr), IDIO_COPY_SHALLOW, 0);
    }
    IDIO_CONTINUATION_FRAME (k)		= IDIO_THREAD_FRAME (thr);
    IDIO_CONTINUATION_ENV (k)		= IDIO_THREAD_ENV (thr);
    memcpy (IDIO_CONTINUATION_JMP_BUF (k), IDIO_THREAD_JMP_BUF (thr), sizeof (sigjmp_buf));
#ifdef IDIO_CONTINUATION_HANDLES
    /*
     * See the comment in idio_vm_restore_continuation_data where
     * restoring the continuation's saved file descriptors ends in
     * disappointment.
     */
    IDIO_CONTINUATION_INPUT_HANDLE (k)	= IDIO_THREAD_INPUT_HANDLE (thr);
    IDIO_CONTINUATION_OUTPUT_HANDLE (k) = IDIO_THREAD_OUTPUT_HANDLE (thr);
    IDIO_CONTINUATION_ERROR_HANDLE (k)	= IDIO_THREAD_ERROR_HANDLE (thr);
#endif
    IDIO_CONTINUATION_MODULE (k)	= IDIO_THREAD_MODULE (thr);
    IDIO_CONTINUATION_HOLES (k)		= idio_copy_pair (IDIO_THREAD_HOLES (thr), IDIO_COPY_DEEP);
    IDIO_CONTINUATION_THR (k)		= thr;
    IDIO_CONTINUATION_FLAGS (k)		= IDIO_CONTINUATION_FLAG_NONE;

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
:return: ``#t`` if `o` is a continuation ``#f`` otherwise\n\
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

    IDIO_GC_FREE (k->u.continuation, sizeof (idio_continuation_t));
}

char *idio_continuation_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (continuation, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#<K>");

    return r;
}

char *idio_continuation_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (continuation, v);

    char *r = NULL;

    IDIO ks = IDIO_CONTINUATION_STACK (v);
    idio_sp_t kss;
    char *kind = "";
    if (IDIO_CONTINUATION_FLAGS (v) & IDIO_CONTINUATION_FLAG_DELIMITED) {
	kind = "D";
	kss = IDIO_FIXNUM_VAL (ks);
    } else {
	kss = idio_array_size (ks);
    }

#ifdef IDIO_DEBUG
    *sizep = idio_asprintf (&r, "#<K%s %10p ss=%zu PC=[%zu]@%td>", kind, v, kss, IDIO_CONTINUATION_XI (v), IDIO_CONTINUATION_PC (v));
#else
    *sizep = idio_asprintf (&r, "#<K%s ss=%zu PC=[%zu]@%td>", kind, kss, IDIO_CONTINUATION_XI (v), IDIO_CONTINUATION_PC (v));
#endif

    return r;
}

IDIO idio_continuation_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_continuation_as_C_string (v, sizep, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_continuation_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (continuation_p);
}

void idio_init_continuation ()
{
    idio_module_table_register (idio_continuation_add_primitives, NULL, NULL);

    idio_vtable_t *c_vt = idio_vtable (IDIO_TYPE_CONTINUATION);

    idio_vtable_add_method (c_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_continuation));

    idio_vtable_add_method (c_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_continuation_method_2string));
}

