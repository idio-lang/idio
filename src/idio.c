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
 * idio.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <ffi.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "bitset.h"
#include "c-struct.h"
#include "c-type.h"
#include "character.h"
#include "closure.h"
#include "codegen.h"
#include "command.h"
#include "condition.h"
#include "continuation.h"
#include "env.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "job-control.h"
#include "keyword.h"
#include "libc-wrap.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "posix-regex.h"
#include "primitive.h"
#include "read.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vars.h"
#include "vm.h"

pid_t idio_pid = 0;
int idio_state = IDIO_STATE_BOOTSTRAP;
int idio_exit_status = 0;
IDIO idio_k_exit = NULL;

void idio_add_primitives ();

#ifdef IDIO_VM_PROF
#define IDIO_VM_PROF_FILE_NAME "vm-perf.log"
FILE *idio_vm_perf_FILE;
#endif

typedef struct idio_module_table_s {
    size_t size;
    size_t used;
    void (**table) (void);
} idio_module_table_t;

static idio_module_table_t idio_add_primitives_table;
static idio_module_table_t idio_final_table;

void idio_module_table_init ()
{
    idio_add_primitives_table.used = 0;
    idio_add_primitives_table.size = 40;
    idio_add_primitives_table.table = idio_alloc (40 * sizeof (void *));
    idio_final_table.used = 0;
    idio_final_table.size = 40;
    idio_final_table.table = idio_alloc (40 * sizeof (void *));
}

void idio_module_table_register (void (*ap_func) (void), void (*f_func) (void))
{
    if (NULL != ap_func) {
	if (idio_add_primitives_table.used >= idio_add_primitives_table.size) {
	    idio_add_primitives_table.table = idio_realloc (idio_add_primitives_table.table, (idio_add_primitives_table.size + 10) * sizeof (void *));
	}
	idio_add_primitives_table.table[idio_add_primitives_table.used++] = ap_func;
    }
    if (NULL != f_func) {
	if (idio_final_table.used >= idio_final_table.size) {
	    idio_final_table.table = idio_realloc (idio_final_table.table, (idio_final_table.size + 10) * sizeof (void *));
	}
	idio_final_table.table[idio_final_table.used++] = f_func;
    }
}

void idio_module_table_remove (idio_module_table_t *table, void (*func) (void))
{
    size_t i;
    for (i = 0; i < table->used; i++) {
	if (func == table->table[i]) {
	    if (i < table->size - 1) {
		for (; i < table->used - 1; i++) {
		    table->table[i] = table->table[i + 1];
		}
	    }
	    table->table[i] = NULL;
	    break;
	}
    }
}

void idio_module_table_deregister (void (*ap_func) (void), void (*f_func) (void))
{
    if (NULL != ap_func) {
	idio_module_table_remove (&idio_add_primitives_table, ap_func);
    }
    if (NULL != f_func) {
	idio_module_table_remove (&idio_final_table, f_func);
    }
}

void idio_module_table_add_primitives ()
{
    size_t i;
    for (i = 0; i < idio_add_primitives_table.used; i++) {
	(idio_add_primitives_table.table[i]) ();
    }
}

void idio_module_table_final ()
{
    ptrdiff_t i;
    for (i = idio_final_table.used - 1; i >= 0; i--) {
	(idio_final_table.table[i]) ();
    }
}

void idio_init (void)
{

#ifdef IDIO_VM_PROF
    idio_vm_perf_FILE = fopen (IDIO_VM_PROF_FILE_NAME, "w");
    if (NULL == idio_vm_perf_FILE) {
	perror ("fopen " IDIO_VM_PROF_FILE_NAME);
	exit (1);
    }
#endif

    idio_pid = getpid ();

    /* GC first then symbol for the symbol table then modules */
    idio_init_gc ();
    idio_init_vm_values ();

    idio_init_symbol ();
    idio_init_module ();
    idio_init_thread ();

    idio_init_struct ();
    idio_init_condition ();
    idio_init_evaluate ();
    idio_init_expander ();
    idio_init_pair ();
    idio_init_handle ();
    idio_init_string_handle ();
    idio_init_file_handle ();
    idio_init_c_type ();
    idio_init_C_struct ();
    idio_init_frame ();
    idio_init_util ();
    idio_init_primitive ();
    idio_init_character ();
    idio_init_unicode ();
    idio_init_string ();
    idio_init_array ();
    idio_init_hash ();
    idio_init_fixnum ();
    idio_init_bignum ();
    idio_init_bitset ();
    idio_init_closure ();
    idio_init_error ();
    idio_init_keyword ();
    idio_init_read ();
    idio_init_vars ();
    idio_init_env ();
    idio_init_path ();
    idio_init_vm ();
    idio_init_codegen ();
    idio_init_continuation ();

    idio_init_libc_wrap ();
    idio_init_posix_regex ();

    idio_init_command ();
    idio_init_job_control ();

    idio_add_primitives ();
}

void idio_add_primitives ()
{
    /*
     * race condition!  We can't bind any symbols into the "current
     * module" in idio_init_symbol() until we have modules initialised
     * which can't happen until after symbols have been initialised
     * because modules interns the names of the default modules...
     */

    idio_module_table_add_primitives ();

    /*
     * We can't patch up the first thread's IO handles until modules
     * are available which required that threads were available to
     * find the current module...
     */
    idio_init_first_thread ();
}

void idio_final ()
{
    idio_module_table_final ();

#ifdef IDIO_VM_PROF
    if (fclose (idio_vm_perf_FILE)) {
	perror ("fclose " IDIO_VM_PROF_FILE_NAME);
    }
    idio_vm_perf_FILE = stderr;
#endif
}

int main (int argc, char **argv, char **envp)
{
    /*
     * Argument processing for any interpreter is a mixed bag.
     *
     * There'll be arguments to Idio and arguments to the script Idio
     * is running -- which we'll denote sargc, sargv.
     *
     * Nominally, we process arguments as for Idio until we hit a
     * non-option argument (or "--") whereon the remaining arguments
     * are deemed to be the script and its arguments.
     *
     * In the very first instance we'll allocate enough room for a
     * full copy (reference!) of argv and copy argv[0] for two
     * reasons:
     *
     * 1. idio_env_init_idiolib(argv[0]) wants some clue as to reverse
     * engineer a default IDIOLIB.  (Not always successful.)
     *
     * 2. the calls to siglongjmp() wants to free() sargv so we need
     * to have set it up before hand.
     *
     * After all that, run the bootstrap so we have a sentient system
     * and then we can process any option arguments that load
     * libraries etc..
     */
    int sargc = 0;
    char **sargv = (char **) idio_alloc ((argc + 1) * sizeof (char *));
    sargv[0] = argv[0];
    sargv[1] = NULL;	/* just in case */

    idio_module_table_init ();
    idio_init ();

    idio_env_init_idiolib (argv[0]);

    IDIO thr = idio_thread_current_thread ();

    /*
     * Conditions raised during the bootstrap will need a sigsetjmp in
     * place.  As the only place we can siglongjmp back to is here
     * then any kind of condition raised during bootstrap is a
     * precursor to bailing out.  Probably a good thing.
     *
     * Of course we don't want to come back here (immediately prior to
     * looping over argc/argv) if the condition was raised whilst
     * processing argc/argv so there are separate sigsetjmp statements
     * for each "load" alternative.
     *
     * That said, so long as we can get as far as the code in
     * idio_vm_run() then we'll get a per-run sigsetjmp which will
     * override this.
     */
    int sjv = sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1);

    switch (sjv) {
    case 0:
	break;
    case IDIO_VM_SIGLONGJMP_EXIT:
	fprintf (stderr, "NOTICE: bootstrap/exit (%d) for PID %d\n", idio_exit_status, getpid ());
	IDIO_GC_FREE (sargv);
	idio_final ();
	exit (idio_exit_status);
	break;
    default:
	fprintf (stderr, "sigsetjmp: bootstrap failed with sjv %d: exit (%d)\n", sjv, idio_exit_status);
	IDIO_GC_FREE (sargv);
	idio_final ();
	exit (idio_exit_status);
	break;
    }

    /*
     * Save a continuation for exit.
     */
    idio_k_exit = idio_continuation (thr, IDIO_CONTINUATION_CALL_CC);
    idio_gc_protect_auto (idio_k_exit);

    IDIO dosh = idio_open_output_string_handle_C ();

    idio_display_C ("ABORT to main => exit (probably badly)", dosh);

    idio_array_push (idio_vm_krun, IDIO_LIST2 (idio_k_exit, idio_get_output_string (dosh)));

    idio_load_file_name (idio_string_C ("bootstrap"), idio_vm_constants);

    idio_state = IDIO_STATE_RUNNING;
    idio_gc_collect_all ("post-bootstrap");

    /*
     * Dig out the (post-bootstrap) definition of "load" which will
     * now be continuation and module aware.
     */
    IDIO load = idio_module_symbol_value (idio_S_load, idio_Idio_module_instance (), IDIO_LIST1 (idio_S_false));
    if (idio_S_false == load) {
	IDIO_GC_FREE (sargv);
	idio_error_C ("cannot lookup 'load'", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	exit (3);
    }

    enum options {
	OPTION_NONE,
	OPTION_LOAD,
    };
    int in_idio_options = 1;
    enum options option = OPTION_NONE;
    int i;
    for (i = 1; i < argc; i++) {
	if (in_idio_options) {
	    if (OPTION_NONE != option) {
		switch (option) {
		case OPTION_LOAD:
		    {
			IDIO filename = idio_string_C (argv[i]);

			/*
			 * If we're given an option to load a file
			 * then any conditions raised (prior to the
			 * idio_vm_run() sigsetjmp being invoked)
			 * should bring us back here and we can bail.
			 *
			 * What might that be?  Well, we try to invoke
			 * the Idio function "load" which has several
			 * variants: a primitive (file-handle.c); a
			 * basic continuation error catcher
			 * (common.idio); a module/load variant
			 * (module.idio).
			 *
			 * It's entirely possible a condition can be
			 * raised in that code for which we need a
			 * suitable sigsetjmp for the condition to
			 * siglongjmp to.
			 *
			 * Given that all we do is bail we could have
			 * just left it with the "bootstrap" sigsetjmp
			 * outside of this condition/loop but at least
			 * here we can print the offending filename in
			 * case no-one else did.
			 */
			sjv = sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1);

			switch (sjv) {
			case 0:
			    idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST2 (load, filename));
			    break;
			case IDIO_VM_SIGLONGJMP_CONTINUATION:
			    fprintf (stderr, "load %s: continuation was invoked => pending exit (1)\n", argv[i]);
			    idio_exit_status = 1;
			    break;
			case IDIO_VM_SIGLONGJMP_EXIT:
			    fprintf (stderr, "load/exit (%d)\n", idio_exit_status);
			    IDIO_GC_FREE (sargv);
			    idio_final ();
			    exit (idio_exit_status);
			    break;
			default:
			    fprintf (stderr, "sigsetjmp: load %s: failed with sjv %d\n", argv[i], sjv);
			    IDIO_GC_FREE (sargv);
			    idio_final ();
			    exit (1);
			    break;
			}
		    }
		    break;
		default:
		    fprintf (stderr, "option handling: unexpected option %d\n", option);
		    break;
		}
		
		option = OPTION_NONE;
	    } else if (strncmp (argv[i], "--", 2) == 0) {
		if (strncmp (argv[i], "--vm-reports", 12) == 0) {
		    idio_vm_reports = 1;
		} else if (strncmp (argv[i], "--load", 6) == 0) {
		    option = OPTION_LOAD;
		} else if (strlen (argv[i]) == 2) {
		    in_idio_options = 0;
		}
	    } else {
		if (in_idio_options) {
		    /*
		     * Rewrite sargv[0] from the name of the
		     * executable (from argv[0]) to the name of the
		     * script we are going to run.
		     */
		    sargv[0] = argv[i];
		} else {
		    sargv[sargc] = argv[i];
		}
		sargc++;
		in_idio_options = 0;
	    }
	} else {
	    sargv[sargc++] = argv[i];
	}
    }

    /*
     * Script Arguments
     *
     * We'll have a separate ARGV0, a la Bash's BASH_ARGV0, then
     * remaining args in ARGC/ARGV.
     *
     * Remember, sargv started out pointing at argv so if there were
     * no arguments sargv[0] is argv[0].
     */
    idio_module_set_symbol_value (idio_symbols_C_intern ("ARGV0"), idio_string_C (sargv[0]), idio_Idio_module_instance ());

    IDIO args = idio_array (sargc);
    if (sargc) {
	for (i = 1; i < sargc; i++) {
	    idio_array_insert_index (args, idio_string_C (sargv[i]), i - 1);
	}
    }

    idio_module_set_symbol_value (idio_symbols_C_intern ("ARGC"), idio_integer (sargc - 1), idio_Idio_module_instance ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("ARGV"), args, idio_Idio_module_instance ());

    if (sargc) {
	/*
	 * idio_job_control_interactive is set to 1 if isatty (0) is
	 * true however we are about to loop over files in a
	 * non-interactive way.  So turn it off.
	 */
	idio_job_control_set_interactive (0);

	IDIO filename = idio_string_C (sargv[0]);

	/*
	 * If we're given a sequence of files to load then any
	 * conditions raised (prior to the idio_vm_run() sigsetjmp
	 * being invoked) should bring us back here and we can bail.
	 *
	 * What might that be?  Well, we try to invoke the Idio
	 * function "load" which has several variants: a primitive
	 * (file-handle.c); a basic continuation error catcher
	 * (common.idio); a module/load variant (module.idio).
	 *
	 * It's entirely possible a condition can be raised in that
	 * code for which we need a suitable sigsetjmp for the
	 * condition to siglongjmp to.
	 *
	 * Given that all we do is bail we could have just left it
	 * with the "bootstrap" sigsetjmp outside of this
	 * condition/loop but at least here we can print the offending
	 * filename in case no-one else did.
	 */
	sjv = sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1);

	switch (sjv) {
	case 0:
	    idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST2 (load, filename));
	    break;
	case IDIO_VM_SIGLONGJMP_CONTINUATION:
	    fprintf (stderr, "load %s: continuation was invoked => pending exit (1)\n", sargv[0]);
	    idio_exit_status = 1;
	    break;
	case IDIO_VM_SIGLONGJMP_EXIT:
	    fprintf (stderr, "load/exit (%d)\n", idio_exit_status);
	    IDIO_GC_FREE (sargv);
	    idio_final ();
	    exit (idio_exit_status);
	    break;
	default:
	    fprintf (stderr, "sigsetjmp: load %s: failed with sjv %d\n", sargv[0], sjv);
	    IDIO_GC_FREE (sargv);
	    idio_final ();
	    exit (1);
	    break;
	}
    } else {
	/*
	 * idio_command_interactive is set to 1 if isatty (0) is true
	 * and so will be 0 if stdin is a file (or other non-tty
	 * entity).
	 */

	int gc_pause = idio_gc_get_pause ("REPL");

	/*
	 * See commentary above re: sigsetjmp.
	 */
	sjv = sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1);

	switch (sjv) {
	case 0:
	    break;
	case IDIO_VM_SIGLONGJMP_CONDITION:
	    idio_gc_reset ("REPL/condition", gc_pause);
	    break;
	case IDIO_VM_SIGLONGJMP_CONTINUATION:
	    idio_gc_reset ("REPL/continuation", gc_pause);
	    break;
	case IDIO_VM_SIGLONGJMP_CALLCC:
	    idio_gc_reset ("REPL/callcc", gc_pause);
	    break;
	case IDIO_VM_SIGLONGJMP_EVENT:
	    idio_gc_reset ("REPL/event", gc_pause);
	    break;
	case IDIO_VM_SIGLONGJMP_EXIT:
	    idio_gc_reset ("REPL/exit", gc_pause);
	    IDIO_GC_FREE (sargv);
	    idio_final ();
	    exit (idio_exit_status);
	default:
	    fprintf (stderr, "sigsetjmp: repl failed with sjv %d\n", sjv);
	    IDIO_GC_FREE (sargv);
	    exit (1);
	    break;
	}

	/* repl */
	idio_load_handle_C (idio_thread_current_input_handle (), idio_read, idio_evaluate, idio_vm_constants);
    }

    IDIO_GC_FREE (sargv);
    idio_final ();

    return idio_exit_status;
}
