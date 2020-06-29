/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * thread.h
 *
 */

#ifndef THREAD_H
#define THREAD_H

IDIO idio_thread (idio_ai_t stack_size);
int idio_isa_thread (IDIO o);
void idio_free_thread (IDIO t);
IDIO idio_thread_current_thread ();
void idio_thread_set_current_thread (IDIO thr);
void idio_thread_codegen (IDIO code);
IDIO idio_thread_current_env ();
IDIO idio_thread_current_input_handle ();
void idio_thread_set_current_input_handle (IDIO h);
IDIO idio_thread_current_output_handle ();
void idio_thread_set_current_output_handle (IDIO h);
IDIO idio_thread_current_error_handle ();
void idio_thread_set_current_error_handle (IDIO h);
IDIO idio_thread_env_module ();
IDIO idio_thread_current_module ();
void idio_thread_set_current_module (IDIO h);

void idio_init_thread ();
void idio_thread_add_primitives ();
void idio_init_first_thread ();
void idio_final_thread ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
