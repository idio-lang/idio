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
 * command.c
 *
 */

/**
 * DOC: Idio job control
 *
 * Job Control data structures and algorithms are a straight-forward
 * port from the GNU Lib C info pages: "info libc" then menu items
 * "Job Control" then "Implementing a Shell".
 *
 */

#include "idio.h"

IDIO idio_command_module = idio_S_nil;
static pid_t idio_command_pid;
static pid_t idio_command_pgid;
static IDIO idio_command_tcattrs;
static int idio_command_terminal;
int idio_command_interactive;

static IDIO idio_command_process_type;
static IDIO idio_command_job_type;
static IDIO idio_command_jobs_sym;
static IDIO idio_command_last_job;

static IDIO idio_S_background_job;
static IDIO idio_S_exit;
static IDIO idio_S_foreground_job;
static IDIO idio_S_killed;
static IDIO idio_S_wait_for_job;
static IDIO idio_S_stdin_fileno;
static IDIO idio_S_stdout_fileno;
static IDIO idio_S_stderr_fileno;

/*
 * Indexes into structures for direct references
 */
#define IDIO_JOB_TYPE_PIPELINE		0
#define IDIO_JOB_TYPE_PROCS		1
#define IDIO_JOB_TYPE_PGID		2
#define IDIO_JOB_TYPE_NOTIFIED		3
#define IDIO_JOB_TYPE_TCATTRS		4
#define IDIO_JOB_TYPE_STDIN		5
#define IDIO_JOB_TYPE_STDOUT		6
#define IDIO_JOB_TYPE_STDERR		7

#define IDIO_PROCESS_TYPE_ARGV		0
#define IDIO_PROCESS_TYPE_PID		1
#define IDIO_PROCESS_TYPE_COMPLETED	2
#define IDIO_PROCESS_TYPE_STOPPED	3
#define IDIO_PROCESS_TYPE_STATUS	4

/*
 * Don't overplay our hand in a signal handler.  What's the barest
 * minimum?  We can set (technically, not even read) a sig_atomic_t.
 *
 * https://www.securecoding.cert.org/confluence/display/c/SIG31-C.+Do+not+access+shared+objects+in+signal+handlers
 *
 * What this document doesn't say is if we can access an array of
 * sig_atomic_t.
 *
 * NB Make the array IDIO_LIBC_NSIG + 1 as idio_vm_run1() will be
 * trying to access [IDIO_LIBC_NSIG] itself, not up to IDIO_LIBC_NSIG.
 */
volatile sig_atomic_t idio_command_signal_record[IDIO_LIBC_NSIG+1];

void idio_command_sa_signal (int signum)
{
    idio_command_signal_record[signum] = 1;
}

static void idio_command_error_glob (IDIO pattern, IDIO loc)
{
    IDIO_ASSERT (pattern);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("pattern glob failed", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_glob_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       pattern));
    idio_raise_condition (idio_S_true, c);
}

static void idio_command_error_argv_type (IDIO arg, char *reason_C, IDIO loc)
{
    IDIO_ASSERT (arg);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("argument '", sh);
    idio_display (arg, sh);
    idio_display_C ("': can't convert a ", sh);
    idio_display_C ((char *) idio_type2string (arg), sh);
    idio_display_C (" to an execve argument: ", sh);
    idio_display_C (reason_C, sh);

    IDIO c = idio_struct_instance (idio_condition_rt_command_argv_type_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       arg));

    idio_raise_condition (idio_S_true, c);
}

static void idio_command_error_env_type (IDIO name, IDIO loc)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("environment variable '", sh);
    idio_display (name, sh);
    idio_display_C ("' is not a string", sh);

    IDIO c = idio_struct_instance (idio_condition_rt_command_env_type_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       name));

    idio_raise_condition (idio_S_true, c);
}

static void idio_command_error_exec (char **argv, char **envp, IDIO loc)
{
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("exec:", sh);
    int j;
    for (j = 0; NULL != argv[j]; j++) {
	/*
	 * prefix each argv[*] with a space
	 */
    	idio_display_C (" ", sh);

	/*
	 * quote argv[*] if necessary
	 *
	 * XXX needs smarter quoting for "s, 's, etc.
	 *
	 * try:

	 char *qs = NULL;
	 qs = "\"";
	 idio_display_C (qs, sh);

	 */
	int q = 0;
	if (strchr (argv[j], ' ') != NULL) {
	    q = 1;
	}
	if (q) {
	    idio_display_C ("\"", sh);
	}
    	idio_display_C (argv[j], sh);
	if (q) {
	    idio_display_C ("\"", sh);
	}
    }
    IDIO c = idio_struct_instance (idio_condition_rt_command_exec_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       loc,
					       idio_S_nil,
					       idio_fixnum ((intptr_t) errno)));
    idio_raise_condition (idio_S_true, c);
}

static char **idio_command_get_envp ()
{
    IDIO symbols = idio_module_visible_symbols (idio_thread_env_module (), idio_S_environ);
    size_t n = idio_list_length (symbols);

    char **envp = idio_alloc ((n + 1) * sizeof (char *));

    n = 0;
    while (idio_S_nil != symbols) {
	IDIO symbol = IDIO_PAIR_H (symbols);
	size_t slen = strlen (IDIO_SYMBOL_S (symbol));

	/*
	 * NOTE
	 *
	 * We are recursing so that we pick up all environment
	 * variables including those defined by default in the Idio
	 * module (not just those in the current module).
	 *
	 * However, every environ variable will exist at the toplevel
	 * even if it was defined transiently inside a block (all
	 * environ variables have a unique gvi so we can distinguish
	 * them on the stack).
	 *
	 * That means we must ensure that, ultimately,
	 * idio_vm_environ_ref() doesn't invoke an error by passing a
	 * default value to the C function.  All environ variables
	 * must be a string so a boolean is a safe (and traditional)
	 * marker.
	 */
	IDIO val = idio_module_env_symbol_value_recurse (symbol, IDIO_LIST1 (idio_S_false));

	size_t vlen = 0;
	if (idio_S_false != val) {
	    if (idio_isa_string (val)) {
		vlen = idio_string_blen (val);

		envp[n] = idio_alloc (slen + 1 + vlen + 1);
		strcpy (envp[n], IDIO_SYMBOL_S (symbol));
		strcat (envp[n], "=");
		if (idio_S_undef != val) {
		    strncat (envp[n], idio_string_s (val), vlen);
		}
		envp[n][slen + 1 + vlen] = '\0';
		n++;
	    } else {
		if (0) {
		    idio_command_error_env_type (symbol, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		} else {
		    idio_debug ("icge: WARNING: env %s is not a string: ", symbol);
		    fprintf (stderr, "%s: ", idio_type2string (symbol));
		    idio_debug ("%s\n", val);
		}
	    }
	}

	symbols = IDIO_PAIR_T (symbols);
    }
    envp[n] = NULL;

    return envp;
}

char *idio_command_find_exe_C (char *command)
{
    IDIO_C_ASSERT (command);

    size_t cmdlen = strlen (command);

    IDIO PATH = idio_module_current_symbol_value_recurse (idio_env_PATH_sym, idio_S_nil);

    char *path;
    char *pathe;
    if (idio_S_undef == PATH ||
	! idio_isa_string (PATH)) {
	path = idio_env_PATH_default;
	pathe = path + strlen (path);
    } else {
	path = idio_string_s (PATH);
	pathe = path + idio_string_blen (PATH);
    }

    /*
     * See comment in libc-wrap.c re: getcwd(3)
     */
    char exename[PATH_MAX];
    char cwd[PATH_MAX];
    if (getcwd (cwd, PATH_MAX) == NULL) {
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }
    size_t cwdlen = strlen (cwd);

    int done = 0;
    while (! done) {
	size_t pathlen = pathe - path;
	char * colon = NULL;

	if (0 == pathlen) {
	    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
		idio_error_system ("cwd+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }

	    strcpy (exename, cwd);
	} else {
	    colon = memchr (path, ':', pathlen);

	    if (NULL == colon) {
		if ((pathlen + 1 + cmdlen + 1) >= PATH_MAX) {
		    idio_error_system ("dir+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		}

		memcpy (exename, path, pathlen);
		exename[pathlen] = '\0';
	    } else {
		size_t dirlen = colon - path;

		if (0 == dirlen) {
		    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
			idio_error_system ("cwd+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }

		    strcpy (exename, cwd);
		} else {
		    if ((dirlen + 1 + cmdlen + 1) >= PATH_MAX) {
			idio_error_system ("dir+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }

		    memcpy (exename, path, dirlen);
		    exename[dirlen] = '\0';
		}
	    }
	}

	strcat (exename, "/");
	strcat (exename, command);

	if (access (exename, X_OK) == 0) {
	    struct stat sb;

	    if (stat (exename, &sb) == -1) {
		idio_error_system_errno ("stat", IDIO_LIST1 (idio_string_C (exename)), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }

	    if (S_ISREG (sb.st_mode)) {
		done = 1;
		break;
	    }
	}

	if (NULL == colon) {
	    done = 1;
	    exename[0] = '\0';
	    break;
	}

	path = colon + 1;
    }

    char *pathname = NULL;

    if (0 != exename[0]) {
	pathname = idio_alloc (strlen (exename) + 1);
	strcpy (pathname, exename);
    }

    return pathname;
}

char *idio_command_find_exe (IDIO func)
{
    IDIO_ASSERT (func);
    IDIO_TYPE_ASSERT (symbol, func);

    return idio_command_find_exe_C (IDIO_SYMBOL_S (func));
}

static char *idio_command_glob_charp (char *src)
{
    IDIO_C_ASSERT (src);

    while (*src) {
	switch (*src) {
	case '*':
	case '?':
	case '[':
	    return src;
	default:
	    break;
	}
	src++;
    }

    return NULL;
}

static size_t idio_command_possible_filename_glob (IDIO arg, glob_t *gp)
{
    IDIO_ASSERT (arg);

    int free_me = 0;
    char *glob_C;

    if (idio_isa_symbol (arg)) {
	glob_C = IDIO_SYMBOL_S (arg);
    } else if (idio_isa_string (arg)) {
	glob_C = idio_string_as_C (arg);
	free_me = 1;
    } else {
	idio_error_param_type ("symbol|string", arg, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    char *match = idio_command_glob_charp (glob_C);

    size_t r = -1;

    if (NULL == match) {
	r = 0;
    } else {
	if (glob (glob_C, GLOB_NOCHECK, NULL, gp) == 0) {
	    r = gp->gl_pathc;
	} else {
	    idio_command_error_glob (arg, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	}
    }

    if (free_me) {
	free (glob_C);
    }

    return r;
}

char **idio_command_argv (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    /*
     * argv[] needs:
     * 1. path to command
     * 2+ arg1+
     * 3. NULL (terminator)
     *
     * Here we will flatten any lists and expand filename
     * patterns which means the arg list will grow as we
     * determine what each argument means.
     *
     * We know the basic size
     */
    int argc = 1 + idio_list_length (args) + 1;
    char **argv = idio_alloc (argc * sizeof (char *));
    int i = 1;		/* index into argv */

    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);

	switch ((intptr_t) arg & IDIO_TYPE_MASK) {
	case IDIO_TYPE_FIXNUM_MARK:
	    {
		argv[i++] = idio_display_string (arg);
	    }
	    break;
	case IDIO_TYPE_CONSTANT_MARK:
	    {
		switch ((intptr_t) arg & IDIO_TYPE_CONSTANT_MASK) {
		case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		    {
			argv[i++] = idio_display_string (arg);
		    }
		    break;
		default:
		    idio_command_error_argv_type (arg, "unconvertible value", IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		    break;
		}
	    }
	case IDIO_TYPE_PLACEHOLDER_MARK:
	    idio_command_error_argv_type (arg, "unconvertible value", IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	    break;
	case IDIO_TYPE_POINTER_MARK:
	    {
		switch (idio_type (arg)) {
		case IDIO_TYPE_STRING:
		    if (asprintf (&argv[i++], "%.*s", (int) IDIO_STRING_BLEN (arg), IDIO_STRING_S (arg)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    break;
		case IDIO_TYPE_SUBSTRING:
		    if (asprintf (&argv[i++], "%.*s", (int) IDIO_SUBSTRING_BLEN (arg), IDIO_SUBSTRING_S (arg)) == -1) {
			idio_error_alloc ("asprintf");

			/* notreached */
			return NULL;
		    }
		    break;
		case IDIO_TYPE_SYMBOL:
		    {
			glob_t g;
			size_t n = idio_command_possible_filename_glob (arg, &g);

			if (0 == n) {
			    if (asprintf (&argv[i++], "%s", IDIO_SYMBOL_S (arg)) == -1) {
				idio_error_alloc ("asprintf");

				/* notreached */
				return NULL;
			    }
			} else {
			    /*
			     * NB "gl_pathc - 1" as we reserved a slot
			     * for the original pattern so the
			     * increment is one less
			     */
			    argc += g.gl_pathc - 1;

			    argv = idio_realloc (argv, argc * sizeof (char *));

			    size_t n;
			    for (n = 0; n < g.gl_pathc; n++) {
				size_t plen = strlen (g.gl_pathv[n]);
				argv[i] = idio_alloc (plen + 1);
				strcpy (argv[i++], g.gl_pathv[n]);
			    }

			    globfree (&g);
			}
		    }
		    break;
		case IDIO_TYPE_PAIR:
		case IDIO_TYPE_ARRAY:
		case IDIO_TYPE_HASH:
		case IDIO_TYPE_BIGNUM:
		case IDIO_TYPE_C_INT:
		case IDIO_TYPE_C_UINT:
		case IDIO_TYPE_C_FLOAT:
		case IDIO_TYPE_C_DOUBLE:
		case IDIO_TYPE_C_POINTER:
		    {
			argv[i++] = idio_display_string (arg);
		    }
		    break;
		case IDIO_TYPE_STRUCT_INSTANCE:
		    {
			IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (arg);
			if (idio_struct_type_isa (sit, idio_path_type)) {
			    IDIO pattern = idio_struct_instance_ref_direct (arg, IDIO_PATH_PATTERN);

			    glob_t g;
			    size_t n = idio_command_possible_filename_glob (pattern, &g);

			    if (0 == n) {
				argv[i++] = idio_display_string (pattern);
			    } else {
				/*
				 * NB "gl_pathc - 1" as we reserved a slot
				 * for the original pattern so the
				 * increment is one less
				 */
				argc += g.gl_pathc - 1;

				argv = idio_realloc (argv, argc * sizeof (char *));

				size_t n;
				for (n = 0; n < g.gl_pathc; n++) {
				    size_t plen = strlen (g.gl_pathv[n]);
				    argv[i] = idio_alloc (plen + 1);
				    strcpy (argv[i++], g.gl_pathv[n]);
				}

				globfree (&g);
			    }
			} else {
			    idio_debug ("WARNING: idio_command_argv: unexpected struct instance: type: %s\n", sit);
			    idio_debug ("arg = %s\n", arg);
			    idio_command_error_argv_type (arg, "only path_type structs", IDIO_C_FUNC_LOCATION ());

			    /* notreached */
			    return NULL;
			}
		    }
		    break;

		    /*
		     * No useful representation of the following types
		     * for a command we are about to exec().
		     */
		case IDIO_TYPE_CLOSURE:
		case IDIO_TYPE_PRIMITIVE:
		case IDIO_TYPE_MODULE:
		case IDIO_TYPE_FRAME:
		case IDIO_TYPE_HANDLE:
		case IDIO_TYPE_STRUCT_TYPE:
		case IDIO_TYPE_THREAD:
		case IDIO_TYPE_C_TYPEDEF:
		case IDIO_TYPE_C_STRUCT:
		case IDIO_TYPE_C_INSTANCE:
		case IDIO_TYPE_C_FFI:
		case IDIO_TYPE_OPAQUE:
		default:
		    idio_error_warning_message ("idio_command_argv: unexpected object type: %s", idio_type2string (arg));
		    idio_debug ("arg = %s\n", arg);
		    idio_command_error_argv_type (arg, "unconvertible value", IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		    break;
		}
	    }
	    break;
	default:
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object type: %s", idio_type2string (arg));

	    /* notreached */
	    return NULL;
	    break;
	}

	args = IDIO_PAIR_T (args);
    }
    argv[i] = NULL;

    return argv;
}

static int idio_command_job_is_stopped (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	procs = IDIO_PAIR_T (procs);

	if (idio_S_false == idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_COMPLETED) &&
	    idio_S_false == idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_STOPPED)) {
	    return 0;
	}
    }

    return 1;
}

IDIO_DEFINE_PRIMITIVE1_DS ("job-is-stopped", job_is_stopped, (IDIO job), "job", "\
test if job `job` is stopped			\n\
						\n\
:param job: job to test				\n\
						\n\
:return: #t if job `job` is stopped, #f otherwise\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (idio_command_job_is_stopped (job)) {
	r = idio_S_true;
    }

    return r;
}

static int idio_command_job_is_completed (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	if (idio_S_false == idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_COMPLETED)) {
	    return 0;
	}

	procs = IDIO_PAIR_T (procs);
    }

    return 1;
}

IDIO_DEFINE_PRIMITIVE1_DS ("job-is-completed", job_is_completed, (IDIO job), "job", "\
test if job `job` has completed			\n\
						\n\
:param job: job to test				\n\
						\n\
:return: #t if job `job` has completed, #f otherwise\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (idio_command_job_is_completed (job)) {
	r = idio_S_true;
    }

    return r;
}

static int idio_command_job_failed (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    if (idio_command_job_is_completed (job)) {
	IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
	while (idio_S_nil != procs) {
	    IDIO proc = IDIO_PAIR_H (procs);
	    IDIO istatus = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_STATUS);
	    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

	    if (WIFEXITED (*statusp)) {
		if (WEXITSTATUS (*statusp)) {
		    return 1;
		}
	    } else if (WIFSIGNALED (*statusp)) {
		return 1;
	    }

	    procs = IDIO_PAIR_T (procs);
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("job-failed", job_failed, (IDIO job), "job", "\
test if job `job` has failed			\n\
						\n\
:param job: job to test				\n\
						\n\
:return: #t if job `job` has failed, #f otherwise\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (idio_command_job_failed (job)) {
	r = idio_S_true;
    }

    return r;
}

static IDIO idio_command_job_status (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	IDIO istatus = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_STATUS);
	int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

	if (WIFEXITED (*statusp)) {
	    if (WEXITSTATUS (*statusp)) {
		return idio_S_false;
	    }
	} else if (WIFSIGNALED (*statusp)) {
	    return idio_S_false;
	}

	procs = IDIO_PAIR_T (procs);
    }

    return idio_S_true;
}

IDIO_DEFINE_PRIMITIVE1_DS ("job-status", job_status, (IDIO job), "job", "\
test if job `job` has a process status		\n\
						\n\
:param job: job to test				\n\
						\n\
:return: #f if job `job` has a process status, #t otherwise\n\
						\n\
Note that this is the inverse behaviour you might expect.\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_command_job_status (job);
}

static IDIO idio_command_job_detail (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	IDIO istatus = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_STATUS);
	int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

	if (WIFEXITED (*statusp)) {
	    if (WEXITSTATUS (*statusp)) {
		return IDIO_LIST2 (idio_S_exit, idio_C_int (WEXITSTATUS (*statusp)));
	    }
	} else if (WIFSIGNALED (*statusp)) {
	    return IDIO_LIST2 (idio_S_killed, idio_C_int (WTERMSIG (*statusp)));
	}

	procs = IDIO_PAIR_T (procs);
    }

    return IDIO_LIST2 (idio_S_exit, idio_fixnum (0));
}

IDIO_DEFINE_PRIMITIVE1_DS ("job-detail", job_detail, (IDIO job), "job", "\
return the process status of job `job`		\n\
						\n\
:param job: job					\n\
						\n\
:return: a (kind value) list			\n\
						\n\
kind can be: 'exit or 'killed			\n\
value can be: exit status for 'exit or signal number for 'killed\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_command_job_detail (job);
}

static int idio_command_mark_process_status (pid_t pid, int status)
{
    if (pid > 0) {
	/*
	 * Some arbitrary process has a status update so we need to
	 * dig it out.
	 */
	IDIO jobs = idio_module_symbol_value (idio_command_jobs_sym, idio_command_module, idio_S_nil);
	while (idio_S_nil != jobs) {
	    IDIO job = IDIO_PAIR_H (jobs);

	    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
	    while (idio_S_nil != procs) {
		IDIO proc = IDIO_PAIR_H (procs);

		int proc_pid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_PID));

		if (proc_pid == pid) {
		    IDIO proc_status = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_STATUS);
		    if (idio_S_nil == proc_status) {
			int *statusp = idio_alloc (sizeof (int));
			*statusp = status;
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_STATUS, idio_C_pointer_free_me (statusp));
		    } else {
			int *statusp = IDIO_C_TYPE_POINTER_P (proc_status);
			*statusp = status;
		    }

		    if (WIFSTOPPED (status)) {
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_STOPPED, idio_S_true);
		    } else {
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_COMPLETED, idio_S_true);
			if (WIFSIGNALED (status)) {
			    fprintf (stderr, "Job Terminated: kill -%s %ld: ", idio_libc_signal_name (WTERMSIG (status)), (long) pid);
			    idio_debug ("%s\n", idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PIPELINE));
			}
		    }

		    return 0;
		}

		procs = IDIO_PAIR_T (procs);
	    }

	    jobs = IDIO_PAIR_T (jobs);
	}

	fprintf (stderr, "No child process %d.\n", (int) pid);
	return -1;
    } else if (0 == pid ||
	       ECHILD == errno) {
	/*
	 * No processes to report
	 */
	return -1;
    } else {
	idio_error_system_errno ("waitpid failed", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -2;
    }

    return -1;
}

IDIO_DEFINE_PRIMITIVE2_DS ("mark-process-status", mark_process_status, (IDIO ipid, IDIO istatus), "pid status", "\
update the process status of pid `pid` with `status`\n\
						\n\
:param pid: process id				\n\
:param status: Unix process status		\n\
						\n\
:return: #t if the update was successfull, #f otherwise\n\
")
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int pid = IDIO_C_TYPE_INT (ipid);
    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (idio_command_mark_process_status (pid, *statusp)) {
	r = idio_S_true;
    }

    return r;
}

static void idio_command_update_status (void)
{
    int status;
    pid_t pid;

    do
	pid = waitpid (WAIT_ANY, &status, WUNTRACED | WNOHANG);
    while (!idio_command_mark_process_status (pid, status));

}

IDIO_DEFINE_PRIMITIVE0_DS ("update-status", update_status, (), "", "\
update the process status of any outstanding pids\n\
						\n\
:return: #<unspec>				\n\
")
{
    idio_command_update_status ();

    return idio_S_unspec;
}

static IDIO idio_command_wait_for_job (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int status;
    pid_t pid;

    do
	pid = waitpid (WAIT_ANY, &status, WUNTRACED);
    while (!idio_command_mark_process_status (pid, status) &&
	   !idio_command_job_is_stopped (job) &&
	   !idio_command_job_is_completed (job));

    if (idio_command_job_failed (job)) {
	IDIO c = idio_struct_instance (idio_condition_rt_command_status_error_type,
				       IDIO_LIST4 (idio_string_C ("job failed"),
						   IDIO_C_FUNC_LOCATION (),
						   job,
						   idio_command_job_status (job)));

	idio_raise_condition (idio_S_true, c);

	return idio_S_notreached;
    }

    return idio_command_job_status (job);
}

IDIO_DEFINE_PRIMITIVE1_DS ("wait-for-job", wait_for_job, (IDIO job), "job", "\
wait for job `job` to be stopped or completed	\n\
						\n\
:param job: job					\n\
						\n\
:return: job status				\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_command_wait_for_job (job);
}

static void idio_command_format_job_info (IDIO job, char *msg)
{
    IDIO_ASSERT (job);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (0 == idio_command_interactive) {
	return;
    }

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    pid_t job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));

    fprintf (stderr, "%ld (%s): ", (long) job_pgid, msg);
    idio_debug ("%s\n", idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PIPELINE));
}

IDIO_DEFINE_PRIMITIVE2_DS ("format-job-info", format_job_info, (IDIO job, IDIO msg), "job msg", "\
display to stderr `msg` alongside job `job` details\n\
						\n\
:param job: job					\n\
:param msg: string				\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (job);
    IDIO_ASSERT (msg);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);
    IDIO_VERIFY_PARAM_TYPE (string, msg);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    char *msgs = idio_string_as_C (msg);
    idio_command_format_job_info (job, msgs);
    free (msgs);

    return idio_S_unspec;
}

void idio_command_do_job_notification (void)
{
    /*
     * Get up to date info
     */
    idio_command_update_status ();

    IDIO jobs = idio_module_symbol_value (idio_command_jobs_sym, idio_command_module, idio_S_nil);
    IDIO njobs = idio_S_nil;
    IDIO failed_jobs = idio_S_nil;

    while (idio_S_nil != jobs) {
	IDIO job = IDIO_PAIR_H (jobs);

	if (idio_command_job_is_completed (job)) {
	    idio_command_format_job_info (job, "completed");

	    if (idio_command_job_failed (job)) {
		failed_jobs = idio_pair (job, failed_jobs);
	    }
	} else if (idio_command_job_is_stopped (job)) {
	    IDIO ntfy = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_NOTIFIED);
	    if (idio_S_false == ntfy) {
		idio_command_format_job_info (job, "stopped");
		idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_NOTIFIED, idio_S_true);
	    }
	    njobs = idio_pair (job, njobs);
	} else {
	    njobs = idio_pair (job, njobs);
	}

	/*
	 * else: no need to say anything about running jobs
	 */

	jobs = IDIO_PAIR_T (jobs);
    }

    idio_module_set_symbol_value (idio_command_jobs_sym, njobs, idio_command_module);

    if (0) {
    while (idio_S_nil != failed_jobs) {
	IDIO job = IDIO_PAIR_H (failed_jobs);

	IDIO c = idio_struct_instance (idio_condition_rt_command_status_error_type,
				       IDIO_LIST4 (idio_string_C ("job failed"),
						   IDIO_C_FUNC_LOCATION (),
						   job,
						   idio_command_job_status (job)));

	idio_raise_condition (idio_S_true, c);

	/* notreached */
	return;

	/*
	 * Unlike an Idio-variant of this function, we won't return
	 * here with our C hats on because of the siglongjmp(3) in
	 * idio_raise_condition() that jumps back into idio_vm_run().
	 *
	 * If we (somehow) did, then we'd loop around again.
	 */
	failed_jobs = IDIO_PAIR_T (failed_jobs);
    }
    }
}

IDIO_DEFINE_PRIMITIVE0_DS ("do-job-notification", do_job_notification, (), "", "\
notify of any job status changes		\n\
						\n\
")
{
    idio_command_do_job_notification ();

    return idio_S_unspec;
}

static IDIO idio_command_foreground_job (IDIO job, int cont)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    pid_t job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));

    if (idio_command_interactive) {
	/*
	 * Put the job in the foreground
	 */
	if (tcsetpgrp (idio_command_terminal, job_pgid) < 0) {
	    idio_error_system ("icfg tcsetpgrp",
			       IDIO_LIST3 (idio_C_int (idio_command_terminal),
					   idio_C_int (job_pgid),
					   job),
			       errno,
			       IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (cont) {
	if (idio_command_interactive) {
	    IDIO job_tcattrs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_TCATTRS);
	    IDIO_TYPE_ASSERT (C_pointer, job_tcattrs);
	    struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (job_tcattrs);

	    if (tcsetattr (idio_command_terminal, TCSADRAIN, tcattrsp) < 0) {
		idio_error_system_errno ("tcsetattr", IDIO_LIST1 (idio_C_int (idio_command_terminal)), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	if (kill (-job_pgid, SIGCONT) < 0) {
	    idio_error_system_errno ("kill SIGCONT", IDIO_LIST1 (idio_C_int (-job_pgid)), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO r = idio_command_wait_for_job (job);

    if (idio_command_interactive) {
	/*
	 * Put the shell back in the foreground.
	 */
	if (tcsetpgrp (idio_command_terminal, idio_command_pgid) < 0) {
	    idio_error_system ("tcsetpgrp",
			       IDIO_LIST3 (idio_C_int (idio_command_terminal),
					   idio_C_int (idio_command_pgid),
					   job),
			       errno,
			       IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * Save the job's current terminal state -- creating a struct
	 * termios if necessary
	 */
	IDIO job_tcattrs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_TCATTRS);
	struct termios *tcattrsp = NULL;
	if (idio_S_nil == job_tcattrs) {
	    tcattrsp = idio_alloc (sizeof (struct termios));
	    job_tcattrs = idio_C_pointer_free_me (tcattrsp);
	    idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_TCATTRS, job_tcattrs);
	}

	if (tcgetattr (idio_command_terminal, tcattrsp) < 0) {
	    idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_C_int (idio_command_terminal)), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * Restore the shell's terminal state
	 */
	tcattrsp = IDIO_C_TYPE_POINTER_P (idio_command_tcattrs);
	if (tcsetattr (idio_command_terminal, TCSADRAIN, tcattrsp) < 0) {
	    idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_C_int (idio_command_terminal)), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("foreground-job", foreground_job, (IDIO job, IDIO icont), "job cont", "\
place job `job` in the foreground\n\
						\n\
:param job: job					\n\
:param cont: boolean				\n\
						\n\
:return: job status				\n\
						\n\
If `cont` is set a SIGCONT is sent to the process group\n\
")
{
    IDIO_ASSERT (job);
    IDIO_ASSERT (icont);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);
    IDIO_VERIFY_PARAM_TYPE (boolean, icont);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int cont = 0;

    if (idio_S_true == icont) {
	cont = 1;
    }

    return idio_command_foreground_job (job, cont);
}

static IDIO idio_command_background_job (IDIO job, int cont)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (cont) {
	int job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));

	if (kill (-job_pgid, SIGCONT) < 0) {
	    idio_error_system_errno ("kill SIGCONT", IDIO_LIST1 (idio_C_int (-job_pgid)), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    /*
     * A backgrounded job is always successful
     */
    return idio_fixnum (0);
}

IDIO_DEFINE_PRIMITIVE2_DS ("background-job", background_job, (IDIO job, IDIO icont), "job cont", "\
place job `job` in the background\n\
						\n\
:param job: job					\n\
:param cont: boolean				\n\
						\n\
:return: 0					\n\
						\n\
If `cont` is set a SIGCONT is sent to the process group\n\
						\n\
Backgrounding a job is always successful hence returns 0.\n\
")
{
    IDIO_ASSERT (job);
    IDIO_ASSERT (icont);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);
    IDIO_VERIFY_PARAM_TYPE (boolean, icont);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int cont = 0;

    if (idio_S_true == icont) {
	cont = 1;
    }

    return idio_command_background_job (job, cont);
}

static void idio_command_hangup_job (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO ipgid = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID);
    pid_t job_pgid = -1;
    if (idio_isa_fixnum (ipgid)) {
	job_pgid = IDIO_FIXNUM_VAL (ipgid);
    } else if (idio_isa_C_int (ipgid)) {
	job_pgid = IDIO_C_TYPE_INT (ipgid);
    } else {
	idio_error_param_type ("fixnum|C_int", ipgid, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (kill (-job_pgid, SIGCONT) < 0) {
	if (ESRCH != errno) {
	    idio_error_system_errno ("kill SIGCONT", IDIO_LIST1 (idio_C_int (-job_pgid)), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    if (kill (-job_pgid, SIGHUP) < 0) {
	if (ESRCH != errno) {
	    idio_error_system_errno ("kill SIGHUP", IDIO_LIST1 (idio_C_int (-job_pgid)), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("hangup-job", hangup_job, (IDIO job), "job", "\
hangup job `job`				\n\
						\n\
:param job: job					\n\
						\n\
:return: #<unspec>				\n\
						\n\
Send the process group of `job` a SIGCONT then a SIGHUP\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_command_hangup_job (job);

    return idio_S_unspec;
}

void idio_command_SIGHUP_signal_handler (IDIO signum)
{
    IDIO_ASSERT (signum);
    IDIO_TYPE_ASSERT (C_int, signum);

    IDIO jobs = idio_module_symbol_value (idio_command_jobs_sym, idio_command_module, idio_S_nil);
    if (idio_S_nil != jobs) {
	/* fprintf (stderr, "There are outstanding jobs\n"); */
	while (idio_S_nil != jobs) {
	    IDIO job = IDIO_PAIR_H (jobs);
	    idio_command_hangup_job (job);
	    jobs = IDIO_PAIR_T (jobs);
	}
    }
}

IDIO_DEFINE_PRIMITIVE2 ("SIGHUP-condition-handler", SIGHUP_condition_handler, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);
    IDIO_TYPE_ASSERT (boolean, cont);
    IDIO_TYPE_ASSERT (condition, cond);

    if (idio_isa_condition (cond)) {
	IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (cond);
	IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond);

	if (idio_struct_type_isa (sit, idio_condition_rt_signal_type)) {
	    IDIO isignum = idio_array_get_index (sif, IDIO_SI_RT_SIGNAL_TYPE_SIGNUM);
	    int signum = IDIO_FIXNUM_VAL (isignum);

	    if (SIGHUP == signum) {
		idio_command_SIGHUP_signal_handler (isignum);

		return idio_S_unspec;
	    }
	}
    } else {
	fprintf (stderr, "SIGHUP-condition-handler: expected a condition, not a %s\n", idio_type2string (cond));
	idio_debug ("%s\n", cond);

	IDIO sh = idio_open_output_string_handle_C ();
	idio_display_C ("SIGHUP-condition-handler: expected a condition not a '", sh);
	idio_display (cond, sh);
	idio_display_C ("'", sh);
	IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				       IDIO_LIST3 (idio_get_output_string (sh),
						   IDIO_C_FUNC_LOCATION (),
						   idio_S_nil));

	idio_raise_condition (idio_S_true, c);

	return idio_S_notreached;
    }

    idio_raise_condition (cont, cond);

    /* notreached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

void idio_command_SIGCHLD_signal_handler (IDIO signum)
{
    IDIO_ASSERT (signum);
    IDIO_TYPE_ASSERT (C_int, signum);

    /*
     * do-job-notification is a thunk so we can call it direct
     */
    idio_vm_invoke_C (idio_thread_current_thread (),
		      idio_module_symbol_value (idio_symbols_C_intern ("do-job-notification"),
						idio_command_module,
						idio_S_nil));
}

IDIO_DEFINE_PRIMITIVE2 ("SIGCHLD-condition-handler", SIGCHLD_condition_handler, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);
    IDIO_TYPE_ASSERT (boolean, cont);
    IDIO_TYPE_ASSERT (condition, cond);

    if (idio_isa_condition (cond)) {
	IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (cond);
	IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond);

	if (idio_struct_type_isa (sit, idio_condition_rt_signal_type)) {
	    IDIO isignum = idio_array_get_index (sif, IDIO_SI_RT_SIGNAL_TYPE_SIGNUM);
	    int signum = IDIO_FIXNUM_VAL (isignum);

	    if (SIGCHLD == signum) {
		idio_command_SIGCHLD_signal_handler (isignum);
		return idio_S_unspec;
	    }
	}
    } else {
	fprintf (stderr, "SIGCHLD-condition-handler: expected a condition, not a %s\n", idio_type2string (cond));
	idio_debug ("%s\n", cond);

	IDIO sh = idio_open_output_string_handle_C ();
	idio_display_C ("SIGCHLD-condition-handler: expected a condition not a '", sh);
	idio_display (cond, sh);
	idio_display_C ("'", sh);
	IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				       IDIO_LIST3 (idio_get_output_string (sh),
						   IDIO_C_FUNC_LOCATION (),
						   idio_S_nil));

	idio_raise_condition (idio_S_true, c);

	return idio_S_notreached;
    }

    idio_raise_condition (cont, cond);

    /* notreached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2 ("condition-handler-rt-command-status", condition_handler_rt_command_status, (IDIO cont, IDIO cond))
{
    IDIO_ASSERT (cont);
    IDIO_ASSERT (cond);
    IDIO_TYPE_ASSERT (boolean, cont);
    IDIO_TYPE_ASSERT (condition, cond);

    if (idio_isa_condition (cond)) {
	IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (cond);
	/* IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (cond); */

	if (idio_struct_type_isa (sit, idio_condition_rt_command_status_error_type)) {
	    return idio_S_unspec;
	}
    } else {
	fprintf (stderr, "condition-handler-rt-command-status: expected a condition, not a %s\n", idio_type2string (cond));
	idio_debug ("%s\n", cond);

	IDIO sh = idio_open_output_string_handle_C ();
	idio_display_C ("condition-handler-rt-command-status: expected a condition not a '", sh);
	idio_display (cond, sh);
	idio_display_C ("'", sh);
	IDIO c = idio_struct_instance (idio_condition_rt_parameter_type_error_type,
				       IDIO_LIST3 (idio_get_output_string (sh),
						   IDIO_C_FUNC_LOCATION (),
						   idio_S_nil));

	idio_raise_condition (idio_S_true, c);

	return idio_S_notreached;
    }

    idio_raise_condition (cont, cond);

    /* notreached */
    IDIO_C_ASSERT (0);

    return idio_S_notreached;
}

static void idio_command_mark_job_as_running (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	procs = IDIO_PAIR_T (procs);

	idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_STOPPED, idio_S_false);
    }

    idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_NOTIFIED, idio_S_false);
}

IDIO_DEFINE_PRIMITIVE1_DS ("mark-job-as-running", mark_job_as_running, (IDIO job), "job", "\
mark job `job` as running			\n\
						\n\
:param job: job					\n\
						\n\
:return: #<unspec>				\n\
						\n\
In particular, mark job `job` as not stopped\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_command_mark_job_as_running (job);

    return idio_S_unspec;
}

static void idio_command_continue_job (IDIO job, int foreground)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_command_mark_job_as_running (job);

    if (foreground) {
	idio_command_foreground_job (job, 1);
    } else {
	idio_command_background_job (job, 1);
    }
}

IDIO_DEFINE_PRIMITIVE2_DS ("continue-job", continue_job, (IDIO job, IDIO iforeground), "job foreground", "\
mark job `job` as running and foreground it if required\n\
						\n\
:param job: job					\n\
:param foreground: boolean			\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (job);
    IDIO_ASSERT (iforeground);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);
    IDIO_VERIFY_PARAM_TYPE (boolean, iforeground);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int foreground = 0;

    if (idio_S_true == iforeground) {
	foreground = 1;
    }

    idio_command_continue_job (job, foreground);

    return idio_S_unspec;
}

static void idio_command_prep_io (int infile, int outfile, int errfile)
{
    /* fprintf (stderr, "idio_command_prep_io: %d %d %d\n", infile, outfile, errfile); */
    /*
     * Use the supplied stdin/stdout/stderr
     *
     * Unlike the equivalent code in Idio-land, prep-io, we really
     * must dup2() otherwise no-one will!
     *
     * By and large, the FD_CLOEXEC flag (close-on-exec) will have
     * been set on any file descriptors > STDERR_FILENO.
     */
    if (infile != STDIN_FILENO) {
	if (dup2 (infile, STDIN_FILENO) < 0) {
	    idio_error_system ("dup2", IDIO_LIST2 (idio_C_int (infile),
						   idio_C_int (STDIN_FILENO)),
			       errno,
			       IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	/* if (infile > STDERR_FILENO  && */
	/*     (infile != outfile && */
	/*      infile != errfile)) { */
	/*     if (close (infile) < 0) { */
	/* 	idio_error_system ("close", IDIO_LIST1 (idio_C_int (infile)), errno, IDIO_C_FUNC_LOCATION ()); */
	/*     } */
	/* } */
    }

    if (outfile != STDOUT_FILENO) {
	if (dup2 (outfile, STDOUT_FILENO) < 0) {
	    idio_error_system ("dup2", IDIO_LIST2 (idio_C_int (outfile),
						   idio_C_int (STDOUT_FILENO)),
			       errno,
			       IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	/* if (outfile > STDERR_FILENO && */
	/*     outfile != errfile) { */
	/*     if (close (outfile) < 0) { */
	/* 	idio_error_system ("close", IDIO_LIST1 (idio_C_int (outfile)), errno, IDIO_C_FUNC_LOCATION ()); */
	/*     } */
	/* } */
    }

    if (errfile != STDERR_FILENO) {
	if (dup2 (errfile, STDERR_FILENO) < 0) {
	    idio_error_system ("dup2", IDIO_LIST2 (idio_C_int (errfile),
						   idio_C_int (STDERR_FILENO)),
			       errno,
			       IDIO_C_FUNC_LOCATION ());


	    /* notreached */
	    return;
	}

	/* if (errfile > STDERR_FILENO) { */
	/*     if (close (errfile) < 0) { */
	/* 	idio_error_system ("close", IDIO_LIST1 (idio_C_int (errfile)), errno, IDIO_C_FUNC_LOCATION ()); */
	/*     } */
	/* } */
    }
}

static void idio_command_prep_process (pid_t job_pgid, int infile, int outfile, int errfile, int foreground)
{
    pid_t pid;

    if (idio_command_interactive) {
	pid = getpid ();
	if (0 == job_pgid) {
	    job_pgid = pid;
	}

	/*
	 * Put the process in the process group.  Dupe of parent to
	 * avoid race conditions.
	 */
	if (setpgid (pid, job_pgid) < 0) {
	    idio_error_system_errno ("setpgid", IDIO_LIST2 (idio_C_int (pid), idio_C_int (job_pgid)), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	if (foreground) {
	    /*
	     * Give the terminal to the process group.  Dupe of parent
	     * to avoid race conditions.
	     */
	    if (tcsetpgrp (idio_command_terminal, job_pgid) < 0) {
		idio_error_system ("icpp tcsetpgrp",
				   IDIO_LIST2 (idio_C_int (idio_command_terminal),
					       idio_C_int (job_pgid)),
				   errno,
				   IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}

	/*
	 * Set the handling for job control signals back to the default.
	 */
	signal (SIGINT, SIG_DFL);
	signal (SIGQUIT, SIG_DFL);
	signal (SIGTSTP, SIG_DFL);
	signal (SIGTTIN, SIG_DFL);
	signal (SIGTTOU, SIG_DFL);
	signal (SIGCHLD, SIG_DFL);
    }

    idio_command_prep_io (infile, outfile, errfile);
}

IDIO_DEFINE_PRIMITIVE4_DS ("%prep-process", prep_process, (IDIO ipgid, IDIO iinfile, IDIO ioutfile, IDIO ierrfile, IDIO iforeground), "pgid infile outfile errfile foreground", "\
prepare the current process			\n\
						\n\
:param pgid: process group id			\n\
:param infile: file descriptor for stdin	\n\
:param outfile: file descriptor for stdout	\n\
:param errfile: file descriptor for stderr	\n\
:param foreground: boolean			\n\
						\n\
:return: #<unspec>				\n\
						\n\
Place the current process in `pgid` and dup() stdin, stdout and stderr.\n\
Place the current process in the foreground if requested.\n\
						\n\
File descriptors are C integers.		\n\
")
{
    IDIO_ASSERT (ipgid);
    IDIO_ASSERT (iinfile);
    IDIO_ASSERT (ioutfile);
    IDIO_ASSERT (ierrfile);
    IDIO_ASSERT (iforeground);

    IDIO_VERIFY_PARAM_TYPE (C_int, iinfile);
    IDIO_VERIFY_PARAM_TYPE (C_int, ioutfile);
    IDIO_VERIFY_PARAM_TYPE (C_int, ierrfile);
    IDIO_VERIFY_PARAM_TYPE (boolean, iforeground);

    pid_t pgid = 0;
    if (idio_isa_fixnum (ipgid)) {
	pgid = IDIO_FIXNUM_VAL (ipgid);
    } else if (idio_isa_C_int (ipgid)) {
	pgid = IDIO_C_TYPE_INT (ipgid);
    } else {
	idio_error_param_type ("fixnum|C_int", ipgid, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int infile = IDIO_C_TYPE_INT (iinfile);
    int outfile = IDIO_C_TYPE_INT (ioutfile);
    int errfile = IDIO_C_TYPE_INT (ierrfile);

    int foreground = 0;

    if (idio_S_true == iforeground) {
	foreground = 1;
    }

    idio_command_prep_process (pgid, infile, outfile, errfile, foreground);

    return idio_S_unspec;
}

static void idio_command_launch_job (IDIO job, int foreground)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    pid_t job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));
    int job_stdin = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDIN));
    int job_stdout = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDOUT));
    int job_stderr = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDERR));
    int infile = job_stdin;
    int outfile;
    int proc_pipe[2];

    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	procs = IDIO_PAIR_T (procs);

	if (idio_S_nil != procs) {
	    if (pipe (proc_pipe) < 0) {
		idio_error_system_errno ("pipe", IDIO_LIST2 (proc, job), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	    outfile = proc_pipe[1];
	} else {
	    outfile = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDOUT));
	}

	pid_t pid = fork ();
	if (pid < 0) {
	    idio_error_system_errno ("fork", IDIO_LIST2 (proc, job), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	} else if (0 == pid) {
	    idio_command_prep_process (job_pgid,
				       infile,
				       outfile,
				       job_stderr,
				       foreground);
	    /*
	     * In the info example, we would have execv'd a command in
	     * prep_process whereas we have merely gotten everything
	     * ready here -- as the "command" is more Idio code albeit
	     * quite likely to be an external command since we're in a
	     * pipeline.
	     *
	     * If we don't return we'll fall through to the parent's
	     * code to report on launching the pipeline etc.  Which is
	     * confusing.
	     */
	    return;
	} else {
	    idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_PID, idio_C_int (pid));
	    if (idio_command_interactive) {
		if (0 == job_pgid) {
		    job_pgid = pid;
		    idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_PGID, idio_C_int (job_pgid));
		}
		if (setpgid (pid, job_pgid) < 0) {
		    idio_error_system ("setpgid",
				       IDIO_LIST4 (idio_C_int (pid),
						   idio_C_int (job_pgid),
						   proc,
						   job),
				       errno,
				       IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return;
		}
	    }
	}

	/*
	 * Tidy up any trailing pipes!
	 */
	if (infile != job_stdin) {
	    if (close (infile) < 0) {
		idio_error_system_errno ("close", IDIO_LIST3 (idio_C_int (infile), proc, job), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
	if (outfile != job_stdout) {
	    if (close (outfile) < 0) {
		idio_error_system_errno ("close", IDIO_LIST3 (idio_C_int (outfile), proc, job), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}

	infile = proc_pipe[0];
    }

    if (! idio_command_interactive) {
	idio_command_wait_for_job (job);
    } else if (foreground) {
	idio_command_foreground_job (job, 0);
    } else {
	idio_command_background_job (job, 0);
    }
}

static IDIO idio_command_launch_1proc_job (IDIO job, int foreground, char **argv)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    /* fprintf (stderr, "icl1pj %d/%d", idio_command_pid, getpid ());  */
    /* idio_debug (" %s\n", job);  */

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    IDIO proc = IDIO_PAIR_H (procs);
    pid_t job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));
    int job_stdin = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDIN));
    int job_stdout = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDOUT));
    int job_stderr = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDERR));

    /*
     * We're here because the VM saw a symbol in functional position
     * -- which we have found is an external command on PATH -- but we
     * don't know whether we're in a pipeline or the command was
     * inline.
     *
     * If we're in a pipeline then our pid will be different to the
     * original Idio's pid.
     */
    if (getpid () == idio_command_pid) {
	IDIO jobs = idio_module_symbol_value (idio_command_jobs_sym, idio_command_module, idio_S_nil);
	idio_module_set_symbol_value (idio_command_jobs_sym, idio_pair (job, jobs), idio_command_module);

	idio_module_set_symbol_value (idio_command_last_job, job, idio_command_module);

	/*
	 * Even launching a single process we can get caught with
	 * synchronisation issues (a process can have execve()'d
	 * before the parent has setpgid()'d) so we'll use the same
	 * pgrp_pipe trick as per a pipeline (see operator | in
	 * operator.idio).
	 */
	int pgrp_pipe[2];
	if (pipe (pgrp_pipe) < 0) {
	    idio_error_system_errno ("pipe", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	pid_t pid = fork ();
	if (pid < 0) {
	    idio_error_system_errno ("fork", IDIO_LIST2 (proc, job), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	} else if (0 == pid) {
	    idio_command_prep_process (job_pgid,
				       job_stdin,
				       job_stdout,
				       job_stderr,
				       foreground);

	    char **envp = idio_command_get_envp ();

	    if (close (pgrp_pipe[1]) < 0) {
		idio_error_system_errno ("close", IDIO_LIST1 (idio_fixnum (pgrp_pipe[1])), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    /*
	     * Block reading the pgrp_pipe
	     */
	    char buf[BUFSIZ];
	    read (pgrp_pipe[0], buf, 1);
	    if (close (pgrp_pipe[0]) < 0) {
		idio_error_system_errno ("close", IDIO_LIST1 (idio_fixnum (pgrp_pipe[0])), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    execve (argv[0], argv, envp);
	    perror ("execv");
	    idio_command_error_exec (argv, envp, IDIO_C_FUNC_LOCATION ());

	    exit (33);
	    return idio_S_notreached;
	} else {
	    idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_PID, idio_C_int (pid));
	    if (idio_command_interactive) {
		if (0 == job_pgid) {
		    job_pgid = pid;
		    idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_PGID, idio_C_int (job_pgid));
		}
		if (setpgid (pid, job_pgid) < 0) {
		    /*
		     * Duplicate check as per c/setpgid in libc-wrap.c
		     */
		    if (EACCES != errno) {
			idio_error_system ("setpgid",
					   IDIO_LIST4 (idio_C_int (pid),
						       idio_C_int (job_pgid),
						       proc,
						       job),
					   errno,
					   IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }
		}
	    }

	    /*
	     * synchronise!
	     */
	    if (close (pgrp_pipe[0]) < 0) {
		idio_error_system_errno ("close", IDIO_LIST1 (idio_fixnum (pgrp_pipe[0])), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    if (close (pgrp_pipe[1]) < 0) {
		idio_error_system_errno ("close", IDIO_LIST1 (idio_fixnum (pgrp_pipe[1])), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    /*
	     * We want to prefer the most recently defined versions of
	     * the following functions.  If not we'll always use the C
	     * variant which means we're maintaining two versions.
	     * Which we ought to do but you know how it is...  The C
	     * version won't get used once the Idio version is
	     * defined.
	     */
	    IDIO cmd = idio_S_nil;
	    if (! idio_command_interactive) {
		IDIO wfj = idio_module_symbol_value (idio_S_wait_for_job, idio_command_module, idio_S_nil);
		cmd = IDIO_LIST2 (wfj, job);
	    } else if (foreground) {
		IDIO fj = idio_module_symbol_value (idio_S_foreground_job, idio_command_module, idio_S_nil);
		cmd = IDIO_LIST3 (fj, job, idio_S_false);
	    } else {
		IDIO bj = idio_module_symbol_value (idio_S_background_job, idio_command_module, idio_S_nil);
		cmd = IDIO_LIST3 (bj, job, idio_S_false);
	    }

	    /*
	     * As we simply return the result of idio_vm_invoke_C(),
	     * no need to protect anything here.
	     */
	    return idio_vm_invoke_C (idio_thread_current_thread (), cmd);
	}
    } else {
	/*
	 * In a pipeline, just exec -- the %prep-process has been done
	 */
	idio_command_prep_io (job_stdin,
			      job_stdout,
			      job_stderr);

	char **envp = idio_command_get_envp ();

	execve (argv[0], argv, envp);
	perror ("execv");
	idio_command_error_exec (argv, envp, IDIO_C_FUNC_LOCATION ());

	exit (1);
	return idio_S_notreached;
    }

    /*
     * In the above flow we either exec'd or forked and the child
     * exec'd and the parent called return.  So this is...
     *
     * notreached
     */
    idio_error_C ("post-launch: cannot be here", idio_S_nil, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%launch-job", launch_job, (IDIO job), "job", "\
launch job `job`				\n\
						\n\
:param job: job					\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_debug ("%launch-job: %s\n", job);
    idio_command_launch_job (job, 1);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%launch-pipeline", launch_pipeline, (IDIO commands), "commands", "\
launch a pipeline of `commands`			\n\
						\n\
:param commands: list of commands		\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (commands);
    IDIO_VERIFY_PARAM_TYPE (list, commands);

    idio_debug ("%launch-pipeline: %s\n", commands);

    IDIO procs = idio_S_nil;

    IDIO cmds = commands;
    while (idio_S_nil != cmds) {
	IDIO proc = idio_struct_instance (idio_command_process_type,
					  IDIO_LIST5 (IDIO_PAIR_H (cmds),
						      idio_C_int (-1),
						      idio_S_false,
						      idio_S_false,
						      idio_S_nil));

	procs = idio_pair (proc, procs);

	cmds = IDIO_PAIR_T (cmds);
    }

    procs = idio_list_reverse (procs);

    IDIO job_stdin = idio_C_int (STDIN_FILENO);
    IDIO job_stdout = idio_C_int (STDOUT_FILENO);
    IDIO job_stderr = idio_C_int (STDERR_FILENO);

    IDIO job = idio_struct_instance (idio_command_job_type,
				     idio_pair (commands,
				     idio_pair (procs,
				     idio_pair (idio_C_int (0),
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_nil,
				     idio_pair (job_stdin,
				     idio_pair (job_stdout,
				     idio_pair (job_stderr,
				     idio_S_nil)))))))));

    idio_command_launch_job (job, 1);
    return idio_S_unspec;
}

/*
 * idio_vm_invoke() has spotted that the argument in functional
 * position is a symbol *and* that the "symbol" exists as an
 * executable entry on PATH so it's calling us to exec() the command
 * with whatever args were passed.
 *
 * It would be better if we could call the Idio code that does this
 * and not have everything duplicated.
 */
IDIO idio_command_invoke (IDIO func, IDIO thr, char *pathname)
{
    IDIO val = IDIO_THREAD_VAL (thr);
    IDIO args_a = IDIO_FRAME_ARGS (val);
    IDIO last = idio_array_pop (args_a);
    IDIO_FRAME_NARGS (val) -= 1;

    if (idio_S_nil != last) {
	idio_error_C ("last arg != nil", last, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO args = idio_array_to_list (IDIO_FRAME_ARGS (val));

    char **argv = idio_command_argv (args);

    argv[0] = pathname;

    /*
     * We're going to call idio_vm_invoke_C() which may in turn call
     * idio_gc_collect().  That means that any IDIO objects we have
     * here are at risk of being collected unless we protect them.
     *
     * Given we have a hotchpotch of objects, create a (protected)
     * array and just push anything we want to keep on the end.
     */
    IDIO protected = idio_array (10);
    idio_gc_protect (protected);

    IDIO command = idio_list_append2 (IDIO_LIST1 (func), args);
    idio_array_push (protected, command);

    IDIO proc = idio_struct_instance (idio_command_process_type,
				      IDIO_LIST5 (command,
						  idio_C_int (-1),
						  idio_S_false,
						  idio_S_false,
						  idio_S_nil));
    idio_array_push (protected, proc);

    IDIO cmd_sym;
    cmd_sym = idio_module_symbol_value (idio_S_stdin_fileno, idio_libc_wrap_module, idio_S_nil);
    IDIO job_stdin = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST1 (cmd_sym));
    IDIO close_stdin = idio_S_false;
    if (idio_isa_pair (job_stdin)) {
	job_stdin = IDIO_PAIR_H (job_stdin);
	close_stdin = job_stdin;
    }
    idio_array_push (protected, job_stdin);
    idio_array_push (protected, close_stdin);

    cmd_sym = idio_module_symbol_value (idio_S_stdout_fileno, idio_libc_wrap_module, idio_S_nil);
    IDIO job_stdout = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST1 (cmd_sym));
    IDIO recover_stdout = idio_S_false;
    if (idio_isa_pair (job_stdout)) {
	recover_stdout = IDIO_PAIR_HT (job_stdout);
	job_stdout = IDIO_PAIR_H (job_stdout);
    }
    idio_array_push (protected, job_stdout);
    idio_array_push (protected, recover_stdout);

    cmd_sym = idio_module_symbol_value (idio_S_stderr_fileno, idio_libc_wrap_module, idio_S_nil);
    IDIO job_stderr = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST1 (cmd_sym));
    IDIO recover_stderr = idio_S_false;
    if (idio_isa_pair (job_stderr)) {
	recover_stderr = IDIO_PAIR_HT (job_stderr);
	job_stderr = IDIO_PAIR_H (job_stderr);
    }
    idio_array_push (protected, job_stderr);
    idio_array_push (protected, recover_stderr);

    /*
     * That was the last call to idio_vm_invoke_C() in this function
     * but idio_command_launch_1proc_job() also calls
     * idio_vm_invoke_C() so keep protecting objects!
     */

    IDIO job = idio_struct_instance (idio_command_job_type,
				     idio_pair (command,
				     idio_pair (IDIO_LIST1 (proc),
				     idio_pair (idio_C_int (0),
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_nil,
				     idio_pair (job_stdin,
				     idio_pair (job_stdout,
				     idio_pair (job_stderr,
				     idio_S_nil)))))))));
    idio_array_push (protected, job);

    IDIO r = idio_command_launch_1proc_job (job, 1, argv);

    /*
     * OK, we're in the clear now!
     */
    idio_gc_expose (protected);

    if (idio_S_false != close_stdin) {
	if (close (IDIO_C_TYPE_INT (close_stdin)) < 0) {
	    idio_error_system_errno ("close", IDIO_LIST1 (close_stdin), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (idio_S_false != recover_stdout) {
	FILE *filep = fdopen (IDIO_C_TYPE_INT (job_stdout), "r");

	if (NULL == filep) {
	    idio_error_system_errno ("fdopen", IDIO_LIST1 (job_stdout), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * NB the temporary file has just been written to so the file
	 * pointer is currently at the end, we need to set it back to
	 * the start to be able to read anything!
	 */
	if (fseek (filep, 0, SEEK_SET) < 0) {
	    idio_error_system_errno ("fseek 0 SEEK_SET", IDIO_LIST1 (job_stdout), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	int done = 0;

	while (! done) {
	    int c = fgetc (filep);

	    switch (c) {
	    case EOF:
		done = 1;
		break;
	    default:
		idio_putc_string_handle (recover_stdout, c);
		break;
	    }
	}
    }

    if (idio_S_false != recover_stderr) {
	FILE *filep = fdopen (IDIO_C_TYPE_INT (job_stderr), "r");

	if (NULL == filep) {
	    idio_error_system_errno ("fdopen", IDIO_LIST1 (job_stderr), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * NB the temporary file has just been written to so the file
	 * pointer is currently at the end, we need to set it back to
	 * the start to be able to read anything!
	 */
	if (fseek (filep, 0, SEEK_SET) < 0) {
	    idio_error_system_errno ("fseek 0 SEEK_SET", IDIO_LIST1 (job_stderr), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	int done = 0;

	while (! done) {
	    int c = fgetc (filep);

	    switch (c) {
	    case EOF:
		done = 1;
		break;
	    default:
		idio_putc_string_handle (recover_stderr, c);
		break;
	    }
	}
    }

    /*
     * NB don't free pathname, argv[0] -- we didn't allocate it
     */
    int j;
    for (j = 1; NULL != argv[j]; j++) {
    	free (argv[j]);
    }
    free (argv);

    return r;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%exec", exec, (IDIO command, IDIO args), "command [", "\
exec `command` `args`				\n\
						\n\
:param command: command name			\n\
:param args: (optional) arguments		\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (command);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (symbol, command);

    char *pathname = idio_command_find_exe (command);
    if (NULL == pathname) {
	idio_error_C ("command not found", IDIO_LIST2 (command, args), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    char **argv = idio_command_argv (args);

    argv[0] = pathname;

    char **envp = idio_command_get_envp ();

    execve (argv[0], argv, envp);
    perror ("execv");
    idio_command_error_exec (argv, envp, IDIO_C_FUNC_LOCATION ());

    exit (1);
    return idio_S_notreached;
}

void idio_init_command ()
{
    idio_command_module = idio_module (idio_symbols_C_intern ("*command*"));
    IDIO_MODULE_IMPORTS (idio_command_module) = IDIO_LIST2 (idio_Idio_module, idio_primitives_module);

    idio_S_background_job = idio_symbols_C_intern ("background-job");
    idio_S_exit = idio_symbols_C_intern ("exit");
    idio_S_foreground_job = idio_symbols_C_intern ("foreground-job");
    idio_S_killed = idio_symbols_C_intern ("killed");
    idio_S_wait_for_job = idio_symbols_C_intern ("wait-for-job");
    idio_S_stdin_fileno = idio_symbols_C_intern ("stdin-fileno");
    idio_S_stdout_fileno = idio_symbols_C_intern ("stdout-fileno");
    idio_S_stderr_fileno = idio_symbols_C_intern ("stderr-fileno");

    struct termios *tcattrsp = idio_alloc (sizeof (struct termios));
    idio_command_tcattrs = idio_C_pointer_free_me (tcattrsp);

    idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-tcattrs"),
				  idio_command_tcattrs,
				  idio_command_module);

    int signum;
    for (signum = IDIO_LIBC_FSIG; signum <= IDIO_LIBC_NSIG; signum++) {
	idio_command_signal_record[signum] = 0;
    }

    struct sigaction nsa, osa;
    nsa.sa_handler = idio_command_sa_signal;
    sigemptyset (& nsa.sa_mask);
    nsa.sa_flags = SA_RESTART;

    if (sigaction (SIGCHLD, &nsa, &osa) < 0) {
	idio_error_system_errno ("sigaction/SIGCHLD", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (osa.sa_handler == SIG_IGN) {
	fprintf (stderr, "WARNING: SIGCHLD == SIG_IGN\n");
    }

    idio_command_pid = getpid ();
    idio_command_terminal = STDIN_FILENO;
    idio_command_interactive = isatty (idio_command_terminal);

    if (idio_command_interactive < 0) {
	idio_error_system_errno ("isatty", IDIO_LIST1 (idio_C_int (idio_command_terminal)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-terminal"),
				  idio_C_int (idio_command_terminal),
				  idio_command_module);

    idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-interactive"),
				  idio_command_interactive ? idio_S_true : idio_S_false,
				  idio_command_module);

    /*
     * Not noted in the Job Control docs is that if we are launched
     * non-interactively then we never set
     * idio_command_pgid/%idio-pgid with a later complaint about a
     * symbol not being a C_int when the variable is accessed in
     * foreground-job in command.idio.
     *
     * Arguably foreground-job shouldn't be changing pgid if the shell
     * is not interactive -- but there's still a sense of using unset
     * variables which we should avoid.
     */
    idio_command_pgid = getpgrp ();
    idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-pgid"),
				  idio_C_int (idio_command_pgid),
				  idio_command_module);

    if (idio_command_interactive) {
	/*
	 * If we should be interactive then loop until we are in the
	 * foreground.
	 *
	 * How tight is this loop?  Presumably the kill suspends us
	 * until we check again.
	 */
	while (tcgetpgrp (idio_command_terminal) != (idio_command_pgid = getpgrp ())) {
	    if (kill (-idio_command_pgid, SIGTTIN) < 0) {
		idio_error_system_errno ("kill SIGTTIN", IDIO_LIST1 (idio_C_int (-idio_command_pgid)), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}

	/*
	 * Ignore interactive and job-control signals.
	 */
	signal (SIGINT, SIG_IGN);
	signal (SIGQUIT, SIG_IGN);
	signal (SIGTSTP, SIG_IGN);
	signal (SIGTTIN, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);

	/*
	 * XXX ignoring SIGCHLD means and explicit waitpid (<pid>)
	 * will get ECHILD
	 */
	/* signal (SIGCHLD, SIG_IGN); */

	/*
	 * Put ourselves in our own process group.
	 */
	idio_command_pgid = idio_command_pid;
	if (setpgid (idio_command_pgid, idio_command_pgid) < 0) {
	    idio_error_system_errno ("setpgid", IDIO_LIST1 (idio_C_int (idio_command_pgid)), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-pgid"),
				      idio_C_int (idio_command_pgid),
				      idio_command_module);

	/*
	 * Grab control of the terminal.
	 */
	if (tcsetpgrp (idio_command_terminal, idio_command_pgid) < 0) {
	    idio_error_system ("tcsetpgrp",
			       IDIO_LIST2 (idio_C_int (idio_command_terminal),
					   idio_C_int (idio_command_pgid)),
			       errno,
			       IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	/*
	 * Save default terminal attributes for shell.
	 */
	struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (idio_command_tcattrs);
	if (tcgetattr (idio_command_terminal, tcattrsp) < 0) {
	    idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_C_int (idio_command_terminal)), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    idio_command_jobs_sym = idio_symbols_C_intern ("%idio-jobs");
    idio_module_set_symbol_value (idio_command_jobs_sym, idio_S_nil, idio_command_module);
    idio_command_last_job = idio_symbols_C_intern ("%%last-job");
    idio_module_set_symbol_value (idio_command_last_job, idio_S_nil, idio_command_module);

    IDIO name;

    name = idio_symbols_C_intern ("%idio-process");
    idio_command_process_type = idio_struct_type (name,
						  idio_S_nil,
						  idio_pair (idio_symbols_C_intern ("argv"),
						  idio_pair (idio_symbols_C_intern ("pid"),
						  idio_pair (idio_symbols_C_intern ("completed"),
						  idio_pair (idio_symbols_C_intern ("stopped"),
						  idio_pair (idio_symbols_C_intern ("status"),
						  idio_S_nil))))));
    idio_module_set_symbol_value (name, idio_command_process_type, idio_command_module);

    name = idio_symbols_C_intern ("%idio-job");
    idio_command_job_type = idio_struct_type (name,
					      idio_S_nil,
					      idio_pair (idio_symbols_C_intern ("pipeline"),
					      idio_pair (idio_symbols_C_intern ("procs"),
					      idio_pair (idio_symbols_C_intern ("pgid"),
					      idio_pair (idio_symbols_C_intern ("notified"),
					      idio_pair (idio_symbols_C_intern ("tcattrs"),
					      idio_pair (idio_symbols_C_intern ("stdin"),
					      idio_pair (idio_symbols_C_intern ("stdout"),
					      idio_pair (idio_symbols_C_intern ("stderr"),
					      idio_S_nil)))))))));
    idio_module_set_symbol_value (name, idio_command_job_type, idio_command_module);
}

void idio_command_add_primitives ()
{
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, job_is_stopped);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, job_is_completed);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, job_failed);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, job_status);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, job_detail);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, mark_process_status);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, update_status);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, wait_for_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, format_job_info);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, do_job_notification);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, foreground_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, background_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, hangup_job);

    IDIO fvi;
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, condition_handler_rt_command_status);
    idio_condition_handler_rt_command_status = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, SIGHUP_condition_handler);
    idio_condition_SIGHUP_signal_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    fvi = IDIO_ADD_MODULE_PRIMITIVE (idio_Idio_module, SIGCHLD_condition_handler);
    idio_condition_SIGCHLD_signal_handler = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));

    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, mark_job_as_running);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, continue_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, prep_process);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, launch_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, launch_pipeline);
    IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, exec);
}

void idio_final_command ()
{
    /*
     * restore the terminal state
     */
    struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (idio_command_tcattrs);
    tcsetattr (idio_command_terminal, TCSADRAIN, tcattrsp);

    /*
     * Be a good citizen and tidy up.  This will reported completed
     * jobs, though.  Maybe we should suppress the reports.
     */
    idio_command_interactive = 0;

    /*
     * This deliberately uses the C versions of these functions as
     * other modules have been shutting down -- we don't want to be
     * running any more Idio code here!
     */
    idio_command_do_job_notification ();

    idio_command_SIGHUP_signal_handler (idio_C_int (SIGHUP));
}

