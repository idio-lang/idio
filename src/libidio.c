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

pid_t idio_pid = 0;
int idio_state = IDIO_STATE_BOOTSTRAP;
int idio_exit_status = 0;
IDIO idio_k_exit = NULL;
IDIO idio_default_eenv = idio_S_nil;
IDIO idio_bootstrap_eenv = idio_S_nil;

void idio_add_primitives ();

#ifdef IDIO_VM_PROF
#define IDIO_VM_PROF_FILE_NAME "idio-vm-perf"
FILE *idio_vm_perf_FILE;
#endif

typedef struct idio_module_table_s {
    size_t size;
    size_t used;
    void (**table) (void);
    void (**handle) (void *handle);		/* final only */
} idio_module_table_t;

static idio_module_table_t idio_add_primitives_table;
static idio_module_table_t idio_final_table;

void idio_module_table_init ()
{
    idio_add_primitives_table.used = 0;
    idio_add_primitives_table.size = 40;
    idio_add_primitives_table.table = idio_alloc (40 * sizeof (void *));
    idio_add_primitives_table.handle = NULL;
    idio_final_table.used = 0;
    idio_final_table.size = 40;
    idio_final_table.table = idio_alloc (40 * sizeof (void *));
    idio_final_table.handle = idio_alloc (40 * sizeof (void *));
}

void idio_module_table_register (void (*ap_func) (void), void (*f_func) (void), void *handle)
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
	    idio_final_table.handle = idio_realloc (idio_final_table.handle, (idio_final_table.size + 10) * sizeof (void *));
	}
	idio_final_table.table[idio_final_table.used] = f_func;
	idio_final_table.handle[idio_final_table.used] = handle;
	idio_final_table.used++;
    }

    /*
     * If we are already running, rather than in bootstrap (or
     * shutdown??)  then call the add_primitives function now.
     */
    if (IDIO_STATE_RUNNING == idio_state &&
	NULL != ap_func) {
	(ap_func) ();
    }
}

void idio_module_table_remove (idio_module_table_t *tbl, void (*func) (void))
{
    size_t i;
    for (i = 0; i < tbl->used; i++) {
	if (func == tbl->table[i]) {
	    if (i < tbl->size - 1) {
		for (; i < tbl->used - 1; i++) {
		    tbl->table[i] = tbl->table[i + 1];
		}
	    }
	    tbl->table[i] = NULL;
	    if (NULL != tbl->handle) {
		tbl->handle[i] = NULL;
	    }
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
    /*
     * Pesky cross-referencing!
     *
     * 1) run all the idio_X_final() functions
     */
    for (i = idio_final_table.used - 1; i >= 0; i--) {
	(idio_final_table.table[i]) ();
    }

    /*
     * 2) dlclose() the shared libraries
     */
    for (i = idio_final_table.used - 1; i >= 0; i--) {
	if (NULL != idio_final_table.handle &&
	    NULL != idio_final_table.handle[i]) {
	    /*
	     * If we dlclose() a shared library when valgrind is
	     * running then any memory leaks are for ??? -- which
	     * isn't helpful.  Of course, if we do not dlclose() then
	     * we leak some dlopen() allocated memory.
	     *
	     * What would be really useful is if we could figure out
	     * if we'd leaked enough memory to be of interest --
	     * noting that sterror(3) and strsignal(3) both leak
	     * memory.
	     *
	     * According to https://stackoverflow.com/a/62364698 we
	     * can test an LD_PRELOAD environment variable
	     */
	    int valgrindp = 0;
	    char *p = getenv ("LD_PRELOAD");
	    if (NULL != p) {
		if (strstr (p, "/valgrind/") != NULL ||
		    strstr (p, "/vgpreload") != NULL) {
		    valgrindp = 1;
		}
	    }
	    if (0 == valgrindp &&
		dlclose (idio_final_table.handle[i])) {
		fprintf (stderr, "dlclose () => %s\n", dlerror ());
		perror ("dlclose");
	    }

	}
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

    /*
     * idio_init_gc() calls idio_pair() which means the idio_vtables
     * array must exist first.
     */
    idio_vtables_size = IDIO_TYPE_MAX;
    idio_vtables = idio_alloc (idio_vtables_size * sizeof (idio_vtable_t *));
    memset (idio_vtables, 0, idio_vtables_size * sizeof (idio_vtable_t *));

    /* GC first then symbol for the symbol table then modules */
    idio_init_gc ();
    idio_init_vtable ();
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
    idio_init_frame ();
    idio_init_util ();
    idio_init_primitive ();
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
    idio_init_usi_wrap ();
    idio_init_read ();
    idio_init_vars ();
    idio_init_env ();
    idio_init_path ();
    idio_init_vm ();
    idio_init_vm_dasm ();
    idio_init_codegen ();
    idio_init_compile ();
    idio_init_continuation ();
    idio_init_object ();

    idio_init_libc_wrap ();
    idio_init_libc_poll ();
    idio_init_posix_regex ();

    idio_init_rfc6234 ();

    idio_init_command ();
    idio_init_job_control ();

    /*
     * Not Art but idio_X_add_primitives() in env.c, vars.c are going
     * to use idio_default_eenv.
     */
    idio_default_eenv = idio_evaluate_eenv (idio_thread_current_thread (),
					    IDIO_STRING ("default evaluation environment"),
					    idio_Idio_module);
    idio_gc_protect_auto (idio_default_eenv);

    idio_module_set_symbol_value (IDIO_SYMBOL ("*expander-eenv*"),
				  idio_default_eenv,
				  idio_expander_module);

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

void idio_remove_terminal_signals ();

void idio_final ()
{
    if (IDIO_STATE_SHUTDOWN == idio_state) {
	fprintf (stderr, "idio_final: already shutting down? => assert (0)\n");
	assert (0);
    }

    idio_remove_terminal_signals ();
    idio_state = IDIO_STATE_SHUTDOWN;

    idio_module_table_final ();
    idio_final_xenv ();
    idio_final_vtable ();

#ifdef IDIO_VM_PROF
    if (idio_vm_perf_FILE != stderr &&
	fclose (idio_vm_perf_FILE)) {
	perror ("fclose " IDIO_VM_PROF_FILE_NAME);
    }
    idio_vm_perf_FILE = stderr;
#endif
}

void idio_sigaddset (sigset_t *ssp, int signum)
{
    int r = sigaddset (ssp, signum);

    if (-1 == r) {
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "sigaddset %s", idio_libc_signal_name (signum));

	idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
}

void idio_terminal_signal_handler (int sig)
{
    /*
     * It turns out, when you get a SIGTERM, we get a nice tight loop
     * of SIGTERMs -- if nothing else, thanks to the kill() at the
     * bottom.
     *
     * Following Bash, we'll avoiding repeating on ourselves.
     */
    static int idio_terminating = 0;
    if (idio_terminating) {
	return;
    }
    idio_terminating = 1;

    idio_remove_terminal_signals ();

    /*
     * Careful!  We can get signals during shutdown where we may have
     * already freed some key structures, notably %%idio-jobs,
     * idio-print-conversion-precision, etc..
     *
     * This came up where a short script had "pipe-from ..." and the
     * parent ended which calls
     * idio_job_control_SIGTERM_stopped_jobs() which SIGTERM'd the
     * async process "..." which was itself in the middle of shutting
     * down.  The async process was then forced to come here where it
     * tries to call these functions which try to use %%idio-jobs
     * etc. which it had just freed...  Doh!
     *
     * Confusingly, you get a complaint from an IDIO_ASSERT() which,
     * despite your best debugging doesn't appear to be a fault in the
     * parent.  Which, of course, it isn't.  An assert() that reports
     * the PID would have sped things up a bit.
     *
     * We can do two complimentary things: firstly, check we are not
     * already shutting down and secondly, disable terminal signals
     * (at least, set them back to default behaviour).
     */
    if (IDIO_STATE_RUNNING == idio_state) {
	/*
	 * restore the terminal state
	 */
	idio_job_control_restore_terminal ();

	if (SIGHUP == sig) {
	    idio_job_control_SIGHUP_signal_handler ();
	}

	idio_job_control_SIGTERM_stopped_jobs ();
    }

    idio_state = IDIO_STATE_SHUTDOWN;

    /*
     * Fall on our sword in the same way for clarity to our parent.
     *
     * Note: disable the signal handler pointing here, first...
     */
    struct sigaction nsa;

    nsa.sa_handler = SIG_DFL;
    nsa.sa_flags = 0;
    sigemptyset (&nsa.sa_mask);

    idio_sigaddset (&nsa.sa_mask, sig);

    if (sigaction (sig, &nsa, (struct sigaction *) NULL) == -1) {
	perror ("sigaction SIG_DFL");

	/*
	 * Desperate times call for desperate measures!
	 */
	exit (128 + sig);
    }

    kill (getpid (), sig);
}

void idio_add_signal_handler (int sig, void (*handler) (int))
{
    struct sigaction nsa;
    struct sigaction osa;

    nsa.sa_handler = handler;
    nsa.sa_flags = 0;
    sigemptyset (&nsa.sa_mask);

    idio_sigaddset (&nsa.sa_mask, sig);

    if (sigaction (sig, &nsa, &osa) == -1) {
	/*
	 * Test Case: ??
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "sigaction %s", idio_libc_signal_name (sig));

	idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    /*
     * For non-interactive shells, check if we were ignoring the
     * signal and undo our terminal_signal handling!
     */
    if (0 == idio_job_control_interactive &&
	SIG_IGN == osa.sa_handler) {
	if (sigaction (sig, &osa, &nsa) == -1) {
	    /*
	     * Test Case: ??
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "sigaction %s", idio_libc_signal_name (sig));

	    idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }
}

void idio_add_terminal_signal (int sig)
{
    idio_add_signal_handler (sig, idio_terminal_signal_handler);
    return;
    struct sigaction nsa;
    struct sigaction osa;

    nsa.sa_handler = idio_terminal_signal_handler;
    nsa.sa_flags = 0;
    sigemptyset (&nsa.sa_mask);

    idio_sigaddset (&nsa.sa_mask, sig);

    if (sigaction (sig, &nsa, &osa) == -1) {
	/*
	 * Test Case: ??
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "sigaction %s", idio_libc_signal_name (sig));

	idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    /*
     * For non-interactive shells, check if we were ignoring the
     * signal and undo our terminal_signal handling!
     */
    if (0 == idio_job_control_interactive &&
	SIG_IGN == osa.sa_handler) {
	if (sigaction (sig, &osa, &nsa) == -1) {
	    /*
	     * Test Case: ??
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "sigaction %s", idio_libc_signal_name (sig));

	    idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }
}

void idio_remove_terminal_signal (int sig)
{
    struct sigaction nsa;
    struct sigaction osa;

    nsa.sa_handler = SIG_DFL;
    nsa.sa_flags = 0;
    sigemptyset (&nsa.sa_mask);

    idio_sigaddset (&nsa.sa_mask, sig);

    if (sigaction (sig, &nsa, &osa) == -1) {
	/*
	 * Test Case: ??
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "sigaction %s", idio_libc_signal_name (sig));

	idio_error_system_errno (em, idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
}

void idio_add_terminal_signals ()
{
    idio_add_terminal_signal (SIGHUP);
    idio_add_terminal_signal (SIGTERM);
}

void idio_remove_terminal_signals ()
{
    idio_remove_terminal_signal (SIGHUP);
    idio_remove_terminal_signal (SIGTERM);
}

int idio_main (char *argv0)
{
    idio_module_table_init ();
    idio_init ();

    /*
     * I'm not sure there are any limits on the length of argv[0] so
     * we could be stuck with strlen.  PATH_MAX, maybe, for argv[0]
     * alone?
     */
    idio_env_init_idiolib (argv0, idio_strnlen (argv0, PATH_MAX));

    IDIO thr = idio_thread_current_thread ();
    IDIO_v v_thr = thr;

    /*
     * Conditions raised during the bootstrap will need a sigsetjmp in
     * place.  As the only place we can siglongjmp back to is here
     * then any kind of condition raised during bootstrap is a
     * precursor to bailing out.  Probably a good thing.
     *
     * Of course we don't want to come back here if the condition was
     * raised in post-bootstrap processing so there are separate
     * sigsetjmp statements for each "load" alternative.
     *
     * That said, so long as we can get as far as the code in
     * idio_vm_run() then we'll get a per-run sigsetjmp which will
     * override this.
     */

    switch (sigsetjmp (IDIO_THREAD_JMP_BUF (thr), 1)) {
    case 0:
	break;
    case IDIO_VM_SIGLONGJMP_EXIT:
	fprintf (stderr, "NOTICE: bootstrap/exit (%d) for PID %" PRIdMAX "\n", idio_exit_status, (intmax_t) getpid ());
	idio_final ();
	return (idio_exit_status);
	break;
    default:
	fprintf (stderr, "sigsetjmp: bootstrap failed: exit (%d)\n", idio_exit_status);
#ifdef IDIO_DEBUG
	/*
	 * Scrape together any clues we can get!  --vm-reports (and
	 * other arguments) are only processed post-bootstrap.
	 */
	idio_vm_reports = 1;
	idio_vm_reporting = 1;
	idio_vm_tables = 1;
#endif
	idio_final ();
	return (idio_exit_status);
	break;
    }

    /*
     * Not strictly necessary as any non-zero return from sigsetjmp()
     * results in an exit of some kind but we can leave it in to cover
     * any developer experimentation.
     */
    thr = v_thr;

    /*
     * Save a continuation for exit.
     */
    idio_k_exit = idio_continuation (thr, IDIO_CONTINUATION_CALL_CC);
    idio_gc_protect_auto (idio_k_exit);

    IDIO dosh = idio_open_output_string_handle_C ();

    idio_display_C ("ABORT to main/bootstrap => exit (probably badly)", dosh);

    idio_vm_push_abort (thr, IDIO_LIST2 (idio_k_exit, idio_get_output_string (dosh)));

    idio_bootstrap_eenv = idio_evaluate_eenv (thr,
					      IDIO_STRING ("> load bootstrap"),
					      IDIO_THREAD_MODULE (thr));
    idio_gc_protect_auto (idio_bootstrap_eenv);
    idio_load_file_name (IDIO_STRING ("bootstrap"), idio_bootstrap_eenv);

    idio_gc_collect_all ("post-bootstrap");
    idio_add_terminal_signals ();
    idio_state = IDIO_STATE_RUNNING;

    /*
     * Not strictly necessary... see above.
     */
    thr = v_thr;

    /*
     * We only want the following idio_k_exit on the ABORT stack
     */
    idio_vm_pop_abort (thr);

    /*
     * Save a continuation for exit.
     */
    idio_k_exit = idio_continuation (thr, IDIO_CONTINUATION_CALL_CC);
    idio_gc_protect_auto (idio_k_exit);

    dosh = idio_open_output_string_handle_C ();

    idio_display_C ("ABORT to main => exit (probably badly)", dosh);

    idio_vm_push_abort (thr, IDIO_LIST2 (idio_k_exit, idio_get_output_string (dosh)));

    /*
     * By default, no interactivity.
     */
    idio_job_control_set_interactive (0);

    return 0;
}
