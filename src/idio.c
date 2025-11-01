/*
 * Copyright (c) 2015-2023, 2025 Ian Fitchet <idf(at)idio-lang.org>
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

#include <dlfcn.h>
#include <poll.h>

#include "array.h"
#include "bignum.h"
#include "bitset.h"
#include "c-type.h"
#include "closure.h"
#include "codegen.h"
#include "command.h"
#include "compile.h"
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
#include "libc-poll.h"
#include "libc-wrap.h"
#include "module.h"
#include "object.h"
#include "pair.h"
#include "path.h"
#include "posix-regex.h"
#include "primitive.h"
#include "read.h"
#include "rfc6234.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "usi-wrap.h"
#include "util.h"
#include "vars.h"
#include "vm-dasm.h"
#include "vm.h"
#include "vtable.h"

static void idio_usage (char *argv0)
{
    fprintf (stderr, "\n");
    fprintf (stderr, "usage: %s [Idio-options] [script-name [script-args]]\n\n", argv0);
    fprintf (stderr, "Idio options:\n\n");
    fprintf (stderr, "  --load NAME             load NAME and continue processing\n");
    fprintf (stderr, "  --debugger              enable the debugger if interactive\n");
    fprintf (stderr, "  --vm-reports            print various VM reports on exit\n");
    fprintf (stderr, "  --vm-tables             print various VM tables on exit\n");

    fprintf (stderr, "\n");
    fprintf (stderr, "  -V                      print the version number and quit\n");
    fprintf (stderr, "  --version               invoke 'idio-version -v' and quit\n");
    fprintf (stderr, "  -h                      print this message and quit\n");
    fprintf (stderr, "  --help                  print this message and quit\n");

    fprintf (stderr, "\n");
    fprintf (stderr, "An invalid Idio option will print this message and quit with an error\n");
}

int idio_static_match (char * const arg, char const * const str, size_t const str_size)
{
    int strncmp_r = strncmp (arg, str, str_size);

    if (0 == strncmp_r) {
	if ('\0' == arg[str_size]) {
	    return 1;
	} else {
	    return 0;
	}
    } else {
	return 0;
    }
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
    int volatile sargc = 0;
    char ** volatile sargv = (char **) idio_alloc ((argc + 1) * sizeof (char *));
    sargv[0] = argv[0];
    sargv[1] = NULL;	/* just in case */

    /*
     * Kick Idio into life
     */
    if (idio_main (argv[0])) {
	idio_free (sargv);
	idio_coding_error_C ("failed to initialize Idio", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	exit (126);
    }

    IDIO thr = idio_thread_current_thread ();
    IDIO_v v_thr = thr;

    /*
     * Dig out the (post-bootstrap) definition of "load" which will
     * now be continuation- and module-aware.
     */
    IDIO load = idio_module_symbol_value_xi (IDIO_THREAD_XI (thr),
					     idio_S_load,
					     idio_Idio_module,
					     IDIO_LIST1 (idio_S_false));
    if (idio_S_false == load) {
	idio_free (sargv);
	idio_coding_error_C ("cannot lookup 'load'", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	exit (125);
    }

    /*
     * It would scan better if we don't report the script failing in
     * bootstrap when it emerges from siglongjmp with
     * IDIO_VM_SIGLONGJMP_EXIT.
     *
     * However, many continuations created during bootstrap cached the
     * bootstrap's jmp_buf as this one didn't exist at the time.
     *
     * That's not to stop us redefining the idio_k_exit continuation
     * or, more importantly, adding the redefined value to to set of
     * VM kruns
     */
    switch (sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1)) {
    case 0:
	break;
    case IDIO_VM_SIGLONGJMP_EXIT:
#ifdef IDIO_DEBUG
	if (idio_exit_status) {
	    fprintf (stderr, "NOTICE: script/exit (%d) for PID %" PRIdMAX "\n", idio_exit_status, (intmax_t) getpid ());
	}
#endif
	idio_free (sargv);
	idio_final ();
	exit (idio_exit_status);
	break;
    default:
	fprintf (stderr, "sigsetjmp: script failed: exit (%d)\n", idio_exit_status);
	idio_free (sargv);
	idio_final ();
	exit (idio_exit_status);
	break;
    }

    /*
     * Not strictly necessary... see above.
     */
    thr = v_thr;

    /*
     * Handle options
     *
     * The enum options is to maintain state round the loop.
     * Non-argument options can just set a flag.
     */
    volatile int import_debugger = 0;
    enum options {
	OPTION_NONE,
	OPTION_LOAD,
    };
    volatile int in_idio_options = 1;
    enum options option = OPTION_NONE;
    volatile int i;

    /*
     * Declare ARGC/ARGV with dummy values otherwise "idio --load
     * test" will have test.idio try to access the symbol ARGC which
     * wasn't defined until after options had been processed.
     *
     * Of course, these are not useful values because "idio [options]
     * [script [args]]" requires that options be processed before
     * calculating both script and args.
     */
    idio_module_set_symbol_value (IDIO_SYMBOL ("ARGC"), idio_integer (0), idio_Idio_module);
    idio_module_set_symbol_value (IDIO_SYMBOL ("ARGV"), idio_S_nil, idio_Idio_module);

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

			switch (sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1)) {
			case 0:
			    idio_vm_invoke_C (IDIO_LIST2 (load, filename));
			    break;
			case IDIO_VM_SIGLONGJMP_CONTINUATION:
			    fprintf (stderr, "load %s: continuation was invoked => pending exit (1)\n", argv[i]);
			    idio_exit_status = 1;
			    break;
			case IDIO_VM_SIGLONGJMP_EXIT:
			    fprintf (stderr, "load %s/exit (%d)\n", argv[i], idio_exit_status);
			    idio_free (sargv);
			    idio_final ();
			    exit (idio_exit_status);
			    break;
			default:
			    fprintf (stderr, "sigsetjmp: load %s: failed\n", argv[i]);
			    idio_free (sargv);
			    idio_final ();
			    exit (1);
			    break;
			}

			/*
			 * Not strictly necessary... see above.
			 */
			thr = v_thr;
		    }
		    break;
		default:
		    fprintf (stderr, "option handling: unexpected option %d\n", option);
		    break;
		}

		option = OPTION_NONE;
	    } else if (strncmp (argv[i], "--", 2) == 0) {
		if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("--vm-reports"))) {
		    idio_vm_reports = 1;
		} else if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("--vm-tables"))) {
		    idio_vm_tables = 1;
		} else if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("--debugger"))) {
		    import_debugger = 1;
		} else if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("--load"))) {
		    option = OPTION_LOAD;
		} else if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("--version"))) {
		    idio_vm_invoke_C (IDIO_LIST2 (idio_module_symbol_value_xi (IDIO_THREAD_XI (thr),
									       IDIO_SYMBOL ("idio-version"),
									       idio_Idio_module,
									       IDIO_LIST1 (idio_S_false)),
						  IDIO_SYMBOL ("-v")));

		    exit (0);
		} else if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("--save-xenvs"))) {
		    idio_vm_save_xenvs (IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (idio_bootstrap_eenv, IDIO_EENV_ST_XI)));
		} else if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("--help"))) {
		    idio_usage (argv[0]);
		    exit (0);
		} else if ('\0' == argv[i][2]) {
		    in_idio_options = 0;
		} else {
		    fprintf (stderr, "ERROR: unrecognized Idio option: %s\n", argv[i]);
		    idio_usage (argv[0]);
		    exit (1);
		}
	    } else if (strncmp (argv[i], "-", 1) == 0) {
		if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("-V"))) {
		    printf ("Idio %s\n", IDIO_SYSTEM_VERSION);
		    exit (0);
		} else if (idio_static_match (argv[i], IDIO_STATIC_STR_LEN ("-h"))) {
		    idio_usage (argv[0]);
		    exit (0);
		} else {
		    fprintf (stderr, "ERROR: unrecognized Idio option: %s\n", argv[i]);
		    idio_usage (argv[0]);
		    exit (1);
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
    IDIO filename = idio_pathname_C (sargv[0]);
    idio_module_set_symbol_value (IDIO_SYMBOL ("ARGV0"), filename, idio_Idio_module);

    IDIO args = idio_array (sargc);
    if (sargc) {
	for (i = 1; i < sargc; i++) {
	    idio_array_insert_index (args, idio_octet_string_C (sargv[i]), i - 1);
	}
    }

    idio_module_set_symbol_value (IDIO_SYMBOL ("ARGC"), idio_integer (sargc - 1), idio_Idio_module);
    idio_module_set_symbol_value (IDIO_SYMBOL ("ARGV"), args, idio_Idio_module);

    if (sargc) {
	/*
	 * We are about to loop over files in a non-interactive way.
	 * So turn interactivity off.
	 */
	idio_job_control_set_interactive (0);

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

	switch (sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1)) {
	case 0:
	    idio_vm_invoke_C (IDIO_LIST2 (load, filename));
	    break;
	case IDIO_VM_SIGLONGJMP_CONTINUATION:
	    fprintf (stderr, "load %s: continuation was invoked => pending exit (1)\n", sargv[0]);
	    idio_exit_status = 1;
	    break;
	case IDIO_VM_SIGLONGJMP_EXIT:
	    fprintf (stderr, "load/exit (%d)\n", idio_exit_status);
	    idio_free (sargv);
	    idio_final ();
	    exit (idio_exit_status);
	    break;
	default:
	    fprintf (stderr, "sigsetjmp: load %s: failed\n", sargv[0]);
	    idio_free (sargv);
	    idio_final ();
	    exit (1);
	    break;
	}

	/*
	 * Not strictly necessary... see above.
	 */
	thr = v_thr;
    } else {
	/*
	 * If the terminal isn't a tty perhaps we shouldn't start the
	 * REPL.  In practice, though, this acts like a crude:
	 * load-handle *stdin*
	 */
	if (idio_job_control_tty_isatty) {
	    idio_job_control_set_interactive (idio_job_control_tty_isatty);

	    idio_module_set_symbol_value (idio_S_suppress_pipefail, idio_S_true, idio_Idio_module);
	    idio_module_set_symbol_value (idio_S_suppress_exit_on_error, idio_S_true, idio_Idio_module);

	    if (import_debugger) {
		IDIO lsh = idio_open_input_string_handle_C (IDIO_STATIC_STR_LEN ("import debugger"));

		idio_load_handle_C (lsh, idio_read, idio_evaluate_func, idio_default_eenv);
	    }
	}

	int gc_pause = idio_gc_get_pause ("REPL");

	/*
	 * See commentary above re: sigsetjmp.
	 */

	switch (sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1)) {
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
	    idio_free (sargv);
	    idio_final ();
	    exit (idio_exit_status);
	default:
	    fprintf (stderr, "sigsetjmp: repl failed\n");
	    idio_free (sargv);
	    exit (1);
	    break;
	}

	/*
	 * Not strictly necessary... see above.
	 */
	thr = v_thr;

	IDIO cm = IDIO_THREAD_MODULE (thr);
	IDIO cih = IDIO_THREAD_INPUT_HANDLE (thr);

	IDIO dsh = idio_open_output_string_handle_C ();
	idio_display (IDIO_MODULE_NAME (cm), dsh);
	idio_display_C ("> load ", dsh);
	idio_display (IDIO_HANDLE_FILENAME (cih), dsh);
	idio_display_C (" (REPL)", dsh);
	IDIO desc = idio_get_output_string (dsh);

	IDIO repl_eenv = idio_evaluate_eenv (thr, desc, cm);
	idio_gc_protect (repl_eenv);

	/* repl */
	idio_load_handle_C (cih, idio_read, idio_evaluate_func, repl_eenv);
    }

    idio_free (sargv);
    idio_final ();

    return idio_exit_status;
}
