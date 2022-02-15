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
 * thread.c
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
#include <stdlib.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "fixnum.h"
#include "file-handle.h"
#include "handle.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static IDIO idio_running_threads;
static IDIO idio_running_thread = idio_S_nil;
IDIO idio_threading_module = idio_S_nil;

IDIO idio_thread_base (idio_ai_t stack_size)
{
    IDIO t = idio_gc_get (IDIO_TYPE_THREAD);
    t->vtable = idio_vtable (IDIO_TYPE_THREAD);

    IDIO_GC_ALLOC (t->u.thread, sizeof (idio_thread_t));

    IDIO main_module = idio_Idio_module_instance ();

    IDIO_THREAD_GREY (t) = NULL;
    IDIO_THREAD_PC (t) = 0;
    IDIO_THREAD_STACK (t) = idio_array (stack_size);
    IDIO_THREAD_VAL (t) = idio_S_unspec;
    IDIO_THREAD_FRAME (t) = idio_S_nil;
    IDIO_THREAD_ENV (t) = main_module;

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    /*
     * 0 is used as a marker for bootstrapping the first thread when
     * there are no previous trap handlers -- see
     * idio_vm_thread_init
     */
    IDIO_THREAD_TRAP_SP (t) = idio_fixnum (0);

    IDIO_THREAD_DYNAMIC_SP (t) = idio_fixnum (-1);
    IDIO_THREAD_ENVIRON_SP (t) = idio_fixnum (-1);
#endif
    if (sigsetjmp (IDIO_THREAD_JMP_BUF (t), 1)) {
	fprintf (stderr, "idio_thread: C stack reverted to init\n");
	idio_vm_panic (t, "thread reverted to init");

	return idio_S_notreached;
    }
    IDIO_THREAD_FUNC (t) = idio_S_unspec;
    IDIO_THREAD_REG1 (t) = idio_S_unspec;
    IDIO_THREAD_REG2 (t) = idio_S_unspec;
    IDIO_THREAD_EXPR (t) = idio_fixnum (0);
    IDIO_THREAD_INPUT_HANDLE (t) = idio_stdin_file_handle ();
    IDIO_THREAD_OUTPUT_HANDLE (t) = idio_stdout_file_handle ();
    IDIO_THREAD_ERROR_HANDLE (t) = idio_stderr_file_handle ();
    IDIO_THREAD_MODULE (t) = main_module;
    IDIO_THREAD_HOLES (t) = idio_S_nil;

    return t;
}

IDIO idio_thread (idio_ai_t stack_size)
{
    IDIO t = idio_thread_base (stack_size);

    idio_vm_thread_init (t);

    return t;
}

int idio_isa_thread (IDIO t)
{
    IDIO_ASSERT (t);

    return idio_isa (t, IDIO_TYPE_THREAD);
}

void idio_free_thread (IDIO t)
{
    IDIO_ASSERT (t);
    IDIO_TYPE_ASSERT (thread, t);

    IDIO_GC_FREE (t->u.thread, sizeof (idio_thread_t));
}

IDIO idio_thread_current_thread ()
{
    if (idio_S_nil == idio_running_thread) {
	idio_error_error_message ("idio_running_thread unset");
	abort ();
    }

    return idio_running_thread;
}

IDIO_DEFINE_PRIMITIVE0_DS ("current-thread", current_thread, (void), "", "\
Return the current thread			\n\
						\n\
:return: current thread				\n\
:rtype: thread					\n\
")
{
    return idio_thread_current_thread ();
}

void idio_thread_set_current_thread (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    idio_running_thread = thr;
}

void idio_thread_codegen (IDIO code)
{
    IDIO_ASSERT (code);
    IDIO_TYPE_ASSERT (pair, code);

    idio_coding_error_C ("unimplemented", idio_S_nil, IDIO_C_FUNC_LOCATION ());
}

IDIO idio_thread_current_env ()
{
    IDIO thr = idio_thread_current_thread ();
    IDIO_TYPE_ASSERT (thread, thr);
    IDIO env = IDIO_THREAD_ENV (thr);
    IDIO_TYPE_ASSERT (module, env);
    return env;
}

IDIO idio_thread_current_input_handle ()
{
    IDIO thr = idio_thread_current_thread ();
    return IDIO_THREAD_INPUT_HANDLE (thr);
}

void idio_thread_set_current_input_handle (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    if (IDIO_HANDLE_FLAGS (h) & IDIO_HANDLE_FLAG_CLOSED) {
	idio_debug ("set-input-handle! closed handle? %s\n", h);
    }

    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_INPUT_HANDLE (thr) = h;
}

IDIO idio_thread_current_output_handle ()
{
    IDIO thr = idio_thread_current_thread ();
    return IDIO_THREAD_OUTPUT_HANDLE (thr);
}

void idio_thread_set_current_output_handle (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    if (IDIO_HANDLE_FLAGS (h) & IDIO_HANDLE_FLAG_CLOSED) {
	idio_debug ("set-output-handle! closed handle? %s\n", h);
    }

    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_OUTPUT_HANDLE (thr) = h;
}

IDIO idio_thread_current_error_handle ()
{
    IDIO thr = idio_thread_current_thread ();
    return IDIO_THREAD_ERROR_HANDLE (thr);
}

void idio_thread_set_current_error_handle (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    if (IDIO_HANDLE_FLAGS (h) & IDIO_HANDLE_FLAG_CLOSED) {
	idio_debug ("set-error-handle! closed handle? %s\n", h);
    }

    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_ERROR_HANDLE (thr) = h;
}

IDIO idio_thread_env_module ()
{
    IDIO thr = idio_thread_current_thread ();
    return IDIO_THREAD_ENV (thr);
}

IDIO idio_thread_current_module ()
{
    IDIO thr = idio_thread_current_thread ();
    return IDIO_THREAD_MODULE (thr);
}

void idio_thread_set_current_module (IDIO m)
{
    IDIO_ASSERT (m);
    IDIO_TYPE_ASSERT (module, m);

    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_MODULE (thr) = m;
    IDIO_THREAD_ENV (thr) = m;
}

char *idio_thread_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (thread, v);

    char *r = NULL;

    /*
     * Code coverage:
     *
     * Not usually user-visible.
     */
    seen = idio_pair (v, seen);
    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (v));
    *sizep = idio_asprintf (&r, "#<THR %10p\n  pc=%6zd\n  sp/top=%2zd/",
			    v,
			    IDIO_THREAD_PC (v),
			    sp - 1);

    size_t t_size = 0;
    char *t = idio_as_string (idio_array_top (IDIO_THREAD_STACK (v)), &t_size, 4, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, t, t_size);

    IDIO_STRCAT (r, sizep, "\n  val=");
    t_size = 0;
    t = idio_as_string (IDIO_THREAD_VAL (v), &t_size, 4, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, t, t_size);

    IDIO_STRCAT (r, sizep, "\n  func=");
    t_size = 0;
    t = idio_as_string (IDIO_THREAD_FUNC (v), &t_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, t, t_size);
    if (1 == depth) {
	IDIO frame = IDIO_THREAD_FRAME (v);

	if (idio_S_nil == frame) {
	    IDIO_STRCAT (r, sizep, "\n  fr=nil");
	} else {
	    char *es;
	    size_t es_size = idio_asprintf (&es, "\n  fr=%10p n=%td ", frame, IDIO_FRAME_NPARAMS (frame));
	    IDIO_STRCAT_FREE (r, sizep, es, es_size);

	    size_t f_size = 0;
	    char *fs = idio_as_string (frame, &f_size, 4, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, fs, f_size);
	}
    }

    IDIO_STRCAT (r, sizep, "\n  env=");
    t_size = 0;
    t = idio_as_string (IDIO_THREAD_ENV (v), &t_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, t, t_size);

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    IDIO_STRCAT (r, sizep, "\n  t/sp=");
    t_size = 0;
    t = idio_as_string (IDIO_THREAD_TRAP_SP (v), &t_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, t, t_size);

    IDIO_STRCAT (r, sizep, "\n  d/sp=");
    t_size = 0;
    t = idio_as_string (IDIO_THREAD_DYNAMIC_SP (v), &t_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, t, t_size);

    IDIO_STRCAT (r, sizep, "\n  e/sp=");
    t_size = 0;
    t = idio_as_string (IDIO_THREAD_ENVIRON_SP (v), &t_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, t, t_size);
#endif
    if (depth > 1) {
	IDIO_STRCAT (r, sizep, "\n  fr=");
	t_size = 0;
	t = idio_as_string (IDIO_THREAD_FRAME (v), &t_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, t, t_size);

	if (depth > 2) {
	    IDIO_STRCAT (r, sizep, "\n  reg1=");
	    t_size = 0;
	    t = idio_as_string (IDIO_THREAD_REG1 (v), &t_size, 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, t, t_size);

	    IDIO_STRCAT (r, sizep, "\n  reg2=");
	    t_size = 0;
	    t = idio_as_string (IDIO_THREAD_REG2 (v), &t_size, 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, t, t_size);

	    IDIO_STRCAT (r, sizep, "\n  expr=");
	    IDIO fmci = IDIO_THREAD_EXPR (v);
	    if (idio_isa_fixnum (fmci)) {
		IDIO fgci = idio_module_get_or_set_vci (idio_thread_current_env (), fmci);
		idio_ai_t gci = IDIO_FIXNUM_VAL (fgci);

		IDIO src = idio_vm_src_constants_ref (gci);

		t_size = 0;
		t = idio_as_string (src, &t_size, 4, seen, 0);
		IDIO_STRCAT_FREE (r, sizep, t, t_size);
	    }

	    IDIO_STRCAT (r, sizep, "\n  input_handle=");
	    t_size = 0;
	    t = idio_as_string (IDIO_THREAD_INPUT_HANDLE (v), &t_size, 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, t, t_size);

	    IDIO_STRCAT (r, sizep, "\n  output_handle=");
	    t_size = 0;
	    t = idio_as_string (IDIO_THREAD_OUTPUT_HANDLE (v), &t_size, 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, t, t_size);

	    IDIO_STRCAT (r, sizep, "\n  error_handle=");
	    t_size = 0;
	    t = idio_as_string (IDIO_THREAD_ERROR_HANDLE (v), &t_size, 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, t, t_size);

	    IDIO_STRCAT (r, sizep, "\n  module=");
	    t_size = 0;
	    t = idio_as_string (IDIO_THREAD_MODULE (v), &t_size, 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, t, t_size);

	    char *hs;
	    size_t hs_size = idio_asprintf (&hs, "\n  holes=%zd ", idio_list_length (IDIO_THREAD_HOLES (v)));
	    IDIO_STRCAT_FREE (r, sizep, hs, hs_size);

	    t_size = 0;
	    t = idio_as_string (IDIO_THREAD_HOLES (v), &t_size, 2, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, t, t_size);
	}
    }
    IDIO_STRCAT (r, sizep, ">");

    return r;
}

IDIO idio_thread_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    IDIO seen = va_arg (ap, IDIO);
    int depth = va_arg (ap, int);
    va_end (ap);

    char *C_r = idio_thread_as_C_string (v, sizep, 0, seen, depth);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_thread_add_primitives ()
{
    /*
     * Required by environ stuff during add_primitives...
     */
    idio_running_thread = idio_thread_base (40);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_threading_module, current_thread);
}

void idio_init_thread ()
{
    idio_module_table_register (idio_thread_add_primitives, NULL, NULL);

    idio_running_threads = idio_array (8);
    idio_gc_protect_auto (idio_running_threads);

    idio_threading_module = idio_module (IDIO_SYMBOLS_C_INTERN ("threading"));
}

void idio_init_first_thread ()
{
    idio_vm_thread_init (idio_running_thread);
    idio_array_push (idio_running_threads, idio_running_thread);

    /*
     * We also need the expander thread "early doors"
     */
    idio_expander_thread = idio_thread (40);

    /* IDIO_THREAD_MODULE (idio_expander_thread) = idio_expander_module; */
    IDIO_THREAD_PC (idio_expander_thread) = 1;

    IDIO ethr = IDIO_SYMBOLS_C_INTERN ("*expander-thread*");
    idio_module_set_symbol_value (ethr, idio_expander_thread, idio_expander_module);

    idio_vtable_t *t_vt = idio_vtable (IDIO_TYPE_THREAD);

    idio_vtable_add_method (t_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_thread));

    idio_vtable_add_method (t_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_thread_method_2string));
}
