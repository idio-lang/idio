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
 * idio.c
 * 
 */

#include "idio.h"

void idio_init ()
{
    /* GC first then symbol for the symbol table then modules */
    idio_init_gc ();
    idio_init_vm_values ();

    idio_init_symbol ();
    idio_init_module ();
    idio_init_thread ();

    idio_init_struct ();
    idio_init_condition ();
    idio_init_evaluate ();
    idio_init_scm_evaluate ();
    idio_init_pair ();
    idio_init_handle ();
    idio_init_string_handle ();
    idio_init_file_handle ();
    idio_init_c_type ();
    idio_init_C_struct ();
    idio_init_frame ();
    idio_init_util ();
    idio_init_primitive ();
    idio_init_specialform ();
    idio_init_character ();
    idio_init_string ();
    idio_init_array ();
    idio_init_hash ();
    idio_init_fixnum ();
    idio_init_bignum ();
    idio_init_closure ();
    idio_init_error ();
    idio_init_read ();
    idio_init_scm_read ();
    idio_init_env ();
    idio_init_command ();
    idio_init_vm ();

    idio_init_libc_wrap ();

    /*
     * race condition!  We can't bind any symbols into the "current
     * module" in idio_init_symbol() until we have modules initialised
     * which can't happen until after symbols have been initialised
     * because modules interns the names of the default modules...
     *
     * Neither of which can happen until scm_evaluate is up and
     * running...
     */
    idio_symbol_add_primitives ();
    idio_module_add_primitives ();
    idio_thread_add_primitives ();

    idio_struct_add_primitives ();
    idio_condition_add_primitives ();
    idio_evaluate_add_primitives ();
    idio_scm_evaluate_add_primitives ();
    idio_pair_add_primitives ();
    idio_handle_add_primitives ();
    idio_string_handle_add_primitives ();
    idio_file_handle_add_primitives ();
    idio_c_type_add_primtives ();
    idio_C_struct_add_primitives ();
    idio_frame_add_primitives ();
    idio_util_add_primitives ();
    idio_primitive_add_primitives ();
    idio_specialform_add_primitives ();
    idio_character_add_primitives ();
    idio_string_add_primitives ();
    idio_array_add_primitives ();
    idio_hash_add_primitives ();
    idio_fixnum_add_primitives ();
    idio_bignum_add_primitives ();
    idio_closure_add_primitives ();
    idio_error_add_primitives ();
    idio_read_add_primitives ();
    idio_scm_read_add_primitives ();
    idio_env_add_primitives ();
    idio_command_add_primitives ();
    idio_vm_add_primitives ();

    idio_libc_wrap_add_primitives ();
    
    /*
     * We can't patch up the first thread's IO handles until modules
     * are available which required that threads were available to
     * find the current module...
     */
    idio_init_first_thread ();
}

void idio_final ()
{
    /*
     * reverse order of idio_init () ??
     */
    idio_final_libc_wrap ();
    
    idio_final_vm ();
    idio_final_command ();
    idio_final_env ();
    idio_final_scm_read ();
    idio_final_read ();
    idio_final_error ();
    idio_final_closure ();
    idio_final_bignum ();
    idio_final_fixnum ();
    idio_final_hash ();
    idio_final_array ();
    idio_final_string ();
    idio_final_character ();
    idio_final_specialform ();
    idio_final_primitive ();
    idio_final_util ();
    idio_final_frame ();
    idio_final_C_struct ();
    idio_final_c_type ();
    idio_final_file_handle ();
    idio_final_string_handle ();
    idio_final_handle ();
    idio_final_pair ();
    idio_final_scm_evaluate ();
    idio_final_evaluate ();
    idio_final_condition ();
    idio_final_struct ();

    idio_final_thread ();
    idio_final_module ();
    idio_final_symbol ();

    idio_final_gc ();
}

int main (int argc, char **argv, char **envp)
{
    idio_init ();

    idio_env_init_idiolib (argv[0]);
    
    idio_load_file (idio_string_C ("bootstrap"));

    if (argc > 1) {
	int i;
	for (i = 1 ; i < argc; i++) {
	    idio_load_file (idio_string_C (argv[i]));
	}
    } else {
	/* repl */
	idio_load_filehandle (idio_current_input_handle (), idio_read, idio_evaluate);
    }

    idio_final ();

    return 0;
}
