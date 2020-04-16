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
 * thread.c
 *
 */

#include "idio.h"

static IDIO idio_running_threads;

static IDIO idio_running_thread = idio_S_nil;

IDIO idio_thread_base (idio_ai_t stack_size)
{
    IDIO t = idio_gc_get (IDIO_TYPE_THREAD);

    IDIO_FPRINTF (stderr, "idio_thread: %10p\n", t);

    IDIO_GC_ALLOC (t->u.thread, sizeof (idio_thread_t));

    IDIO main_module = idio_Idio_module_instance ();

    IDIO_THREAD_GREY (t) = NULL;
    IDIO_THREAD_PC (t) = 0;
    IDIO_THREAD_STACK (t) = idio_array (stack_size);
    IDIO_THREAD_VAL (t) = idio_S_unspec;
    IDIO_THREAD_FRAME (t) = idio_S_nil;
    IDIO_THREAD_ENV (t) = main_module;

    /*
     * 0 is used as a marker for bootstrapping the first thread when
     * there are no previous trap handlers -- see
     * idio_vm_thread_init
     */
    IDIO_THREAD_TRAP_SP (t) = idio_fixnum (0);

    IDIO_THREAD_DYNAMIC_SP (t) = idio_fixnum (-1);
    IDIO_THREAD_ENVIRON_SP (t) = idio_fixnum (-1);
    IDIO_THREAD_JMP_BUF (t) = NULL;
    IDIO_THREAD_FUNC (t) = idio_S_unspec;
    IDIO_THREAD_REG1 (t) = idio_S_unspec;
    IDIO_THREAD_REG2 (t) = idio_S_unspec;
    IDIO_THREAD_INPUT_HANDLE (t) = idio_stdin_file_handle ();
    IDIO_THREAD_OUTPUT_HANDLE (t) = idio_stdout_file_handle ();
    IDIO_THREAD_ERROR_HANDLE (t) = idio_stderr_file_handle ();
    IDIO_THREAD_MODULE (t) = main_module;

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

    idio_gc_stats_free (sizeof (idio_thread_t));

    free (t->u.thread);
}

IDIO idio_thread_current_thread ()
{
    if (idio_S_nil == idio_running_thread) {
	idio_error_error_message ("current_module unset");
	IDIO_C_ASSERT (0);
    }

    return idio_running_thread;
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

    idio_error_C ("unimplemented", idio_S_nil, IDIO_C_LOCATION ("thread-codegen"));
}

IDIO idio_thread_current_env ()
{
    IDIO thr = idio_thread_current_thread ();
    return IDIO_THREAD_ENV (thr);
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

void idio_thread_save_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_debug ("thread: %p save-state: ", thr); */

    IDIO stack = IDIO_THREAD_STACK (thr);
    idio_array_push (stack, idio_fixnum (IDIO_THREAD_PC (thr)));
    idio_array_push (stack, idio_SM_return);
}

void idio_thread_restore_state (IDIO thr)
{
    IDIO_ASSERT (thr);
    IDIO_TYPE_ASSERT (thread, thr);

    /* idio_debug ("thread: %p restore-state: ", thr); */

    IDIO stack = IDIO_THREAD_STACK (thr);
    IDIO marker = idio_array_pop (stack);
    if (idio_SM_return != marker) {
	idio_debug ("itrs: marker: expected idio_SM_return not %s\n", marker);
	idio_vm_panic (thr, "itrs: unexpected stack marker");
    }
    IDIO_THREAD_PC (thr) = IDIO_FIXNUM_VAL (idio_array_pop (stack));
}

void idio_init_thread ()
{
    idio_running_threads = idio_array (8);
    idio_gc_protect (idio_running_threads);
}

void idio_thread_add_primitives ()
{
    /*
     * Required by environ stuff during add_primitives...
     */
    idio_running_thread = idio_thread_base (40);
}

void idio_init_first_thread ()
{
    idio_vm_thread_init (idio_running_thread);
    idio_array_push (idio_running_threads, idio_running_thread);

    idio_expander_thread = idio_thread (40);
    idio_gc_protect (idio_expander_thread);

    /* IDIO_THREAD_MODULE (idio_expander_thread) = idio_expander_module; */
    IDIO_THREAD_PC (idio_expander_thread) = 1;

}

void idio_final_thread ()
{
    idio_gc_expose (idio_running_threads);
}
