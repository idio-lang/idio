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
 * command.c
 *
 * job and process code is taken from GNU Lib C info pages
 */

#include "idio.h"

static pid_t idio_command_pgid;
static struct termios idio_command_tcattrs;
static int idio_command_terminal;
static int idio_command_interactive;

static IDIO idio_command_process_type;
static IDIO idio_command_job_type;
static IDIO idio_command_status_type;
static IDIO idio_command_status_types;
static IDIO idio_command_jobs;

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
	
#define IDIO_PROCESS_TYPE_ARGV		0
#define IDIO_PROCESS_TYPE_PID		1
#define IDIO_PROCESS_TYPE_NOTIFIED	2
#define IDIO_PROCESS_TYPE_COMPLETED	3
#define IDIO_PROCESS_TYPE_STOPPED	4
#define IDIO_PROCESS_TYPE_STATUS	5
	
#define IDIO_COMMAND_STATUS_RUNNING	0
#define IDIO_COMMAND_STATUS_EXITED	1
#define IDIO_COMMAND_STATUS_KILLED	2
#define IDIO_COMMAND_STATUS_STOPPED	3
#define IDIO_COMMAND_STATUS_CONTINUED	4

static void idio_command_error_glob (IDIO pattern)
{
    IDIO_ASSERT (pattern);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("pattern glob failed", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_glob_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       pattern));
    idio_signal_exception (idio_S_true, c);
}

static void idio_command_error_exec ()
{
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("exec", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_command_exec_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       idio_fixnum ((intptr_t) errno)));
    idio_signal_exception (idio_S_true, c);
}

static void idio_command_error_status (IDIO status)
{
    IDIO_ASSERT (status);
    IDIO_TYPE_ASSERT (struct_instance, status)

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("command status", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_command_status_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       status));
    idio_signal_exception (idio_S_true, c);
}

static char **idio_command_get_envp ()
{
    IDIO symbols = idio_module_visible_symbols (idio_current_module (), idio_S_environ);
    size_t n = idio_list_length (symbols);

    char **envp = idio_alloc ((n + 1) * sizeof (char *));

    n = 0;
    while (idio_S_nil != symbols) {
	IDIO symbol = IDIO_PAIR_H (symbols);
	size_t slen = strlen (IDIO_SYMBOL_S (symbol));
	IDIO val = idio_module_current_symbol_value_recurse (symbol);

	size_t vlen = 0;
	if (idio_S_unset != val) {
	    IDIO_TYPE_ASSERT (string, val);
	    vlen = IDIO_STRING_BLEN (val);
	}

	envp[n] = idio_alloc (slen + 1 + vlen + 1);
	strcpy (envp[n], IDIO_SYMBOL_S (symbol));
	strcat (envp[n], "=");
	if (idio_S_undef != val) {
	    strncat (envp[n], IDIO_STRING_S (val), vlen);
	}
	envp[n][slen + 1 + vlen] = '\0';

	/* fprintf (stderr, "E: %s\n", envp[n]); */

	symbols = IDIO_PAIR_T (symbols);
	n++;
    }
    envp[n] = NULL;

    return envp;
}

char *idio_command_find_exe_C (char *command)
{
    IDIO_C_ASSERT (command);
    
    size_t cmdlen = strlen (command);
    
    IDIO PATH = idio_module_current_symbol_value_recurse (idio_env_PATH_sym);

    char *path;
    char *pathe;
    if (idio_S_undef == PATH ||
	! idio_isa_string (PATH)) {
	path = idio_env_PATH_default;
	pathe = path + strlen (path);
    } else {
	path = IDIO_STRING_S (PATH);
	pathe = path + IDIO_STRING_BLEN (PATH);
    }

    /*
     * PATH_MAX varies: POSIX is 256, CentOS 7 is 4096
     *
     * The Linux man page for realpath(3) suggests that calling
     * pathconf(3) for _PC_PATH_MAX doesn't improve matters a whole
     * bunch as it can return a value that is infeasible to allocate
     * in memory.
     */
    char exename[PATH_MAX];
    char cwd[PATH_MAX];
    if (getcwd (cwd, PATH_MAX) == NULL) {
	idio_error_system_errno ("cannot access CWD", idio_S_nil);
    }
    size_t cwdlen = strlen (cwd);
    
    int done = 0;
    while (! done) {
	size_t pathlen = pathe - path;
	char * colon = NULL;
	
	if (0 == pathlen) {
	    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
		idio_error_system ("cwd+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
	    }
	    
	    strcpy (exename, cwd);
	} else {
	    colon = memchr (path, ':', pathlen);

	    if (NULL == colon) {
		if ((pathlen + 1 + cmdlen + 1) >= PATH_MAX) {
		    idio_error_system ("dir+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
		}
	    
		memcpy (exename, path, pathlen);
		exename[pathlen] = '\0';
	    } else {
		size_t dirlen = colon - path;
	    
		if (0 == dirlen) {
		    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
			idio_error_system ("cwd+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
		    }
	    
		    strcpy (exename, cwd);
		} else {
		    if ((dirlen + 1 + cmdlen + 1) >= PATH_MAX) {
			idio_error_system ("dir+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
		    }
	    
		    memcpy (exename, path, dirlen);
		    exename[dirlen] = '\0';
		}
	    }
	}

	strcat (exename, "/");
	strcat (exename, command);

	if (access (exename, X_OK) == 0) {
	    done = 1;
	    break;
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
    IDIO_TYPE_ASSERT (symbol, arg);

    char *match = idio_command_glob_charp (IDIO_SYMBOL_S (arg));

    if (NULL == match) {
	return 0;
    } else {
	if (glob (IDIO_SYMBOL_S (arg), GLOB_NOCHECK, NULL, gp) == 0) {
	    return gp->gl_pathc;
	} else {
	    idio_command_error_glob (arg);
	}
    }
    
    idio_error_C ("dropped out", IDIO_LIST1 (arg));
    return -1;
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
	
	switch ((intptr_t) arg & 3) {
	case IDIO_TYPE_FIXNUM_MARK:
	case IDIO_TYPE_CONSTANT_MARK:
	case IDIO_TYPE_CHARACTER_MARK:
	    {
		argv[i++] = idio_as_string (arg, 1);
	    }
	    break;
	case IDIO_TYPE_POINTER_MARK:
	    {
		switch (idio_type (arg)) {
		case IDIO_TYPE_STRING:
		    if (asprintf (&argv[i++], "%.*s", (int) IDIO_STRING_BLEN (arg), IDIO_STRING_S (arg)) == -1) {
			return NULL;
		    }
		    break;
		case IDIO_TYPE_SUBSTRING:
		    if (asprintf (&argv[i++], "%.*s", (int) IDIO_SUBSTRING_BLEN (arg), IDIO_SUBSTRING_S (arg)) == -1) {
			return NULL;
		    }
		    break;
		case IDIO_TYPE_SYMBOL:
		    {
			glob_t g;
			size_t n = idio_command_possible_filename_glob (arg, &g);

			if (0 == n) {
			    if (asprintf (&argv[i++], "%s", IDIO_SYMBOL_S (arg)) == -1) {
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
			argv[i++] = idio_as_string (arg, 1);
		    }
		    break;
		case IDIO_TYPE_CLOSURE:
		case IDIO_TYPE_PRIMITIVE:
		case IDIO_TYPE_MODULE:
		case IDIO_TYPE_FRAME:
		case IDIO_TYPE_HANDLE:
		case IDIO_TYPE_STRUCT_TYPE:
		case IDIO_TYPE_STRUCT_INSTANCE:
		case IDIO_TYPE_THREAD:
		case IDIO_TYPE_C_TYPEDEF:
		case IDIO_TYPE_C_STRUCT:
		case IDIO_TYPE_C_INSTANCE:
		case IDIO_TYPE_C_FFI:
		case IDIO_TYPE_OPAQUE:
		default:
		    idio_warning_message ("unexpected object type: %s", idio_type2string (arg));
		    break;
		}
	    }
	    break;
	default:
	    idio_warning_message ("unexpected object type: %s", idio_type2string (arg));
	    break;
	}

	args = IDIO_PAIR_T (args);
    }
    argv[i] = NULL;

    return argv;
}

IDIO idio_command_invoke (IDIO func, IDIO thr, char *pathname)
{
    IDIO val = IDIO_THREAD_VAL (thr);
    IDIO args_a = IDIO_FRAME_ARGS (val);
    IDIO last = idio_array_pop (args_a);
    IDIO_FRAME_NARGS (val) -= 1;

    if (idio_S_nil != last) {
	char *ls = idio_as_string (last, 1);
	fprintf (stderr, "invoke: last arg != nil: %s\n", ls);
	free (ls);
	IDIO_C_ASSERT (0);
    }

    IDIO args = idio_array_to_list (IDIO_FRAME_ARGS (val));

    char **argv = idio_command_argv (args);

    if (NULL == argv) {
	idio_error_C ("bad argv", IDIO_LIST2 (func, args));
    }
    argv[0] = pathname;

    idio_debug ("command-invoke: %s\n", func);
    /*
    pid_t cpid;
    cpid = fork ();
    if (-1 == cpid) {
	perror ("fork");
	idio_error_system_errno ("fork failed", IDIO_LIST2 (func, args));
	exit (EXIT_FAILURE);
    }
    
    if (0 == cpid) {
	char **envp = idio_command_get_envp ();

	execve (argv[0], argv, envp);
	perror ("execv");
	idio_command_error_exec ();
	exit (1);
    }
    */

    execv (argv[0], argv);
    perror ("execv");
    idio_command_error_exec ();
    exit (1);
    /*
     * NB don't free pathname, argv[0] -- we didn't allocate it
     */
    int j; 
    for (j = 1; NULL != argv[j]; j++) { 
    	free (argv[j]); 
    }
    free (argv);

    /*
    IDIO fn_cpid = idio_fixnum ((intptr_t) cpid);
    IDIO cstate = idio_struct_instance (idio_command_status_type,
					IDIO_LIST3 (fn_cpid,
						    idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_RUNNING),
						    idio_S_nil));
    idio_hash_put (idio_command_jobs, fn_cpid, cstate);

    int status;
    pid_t w;
    do {
	w = waitpid (cpid, &status, WUNTRACED | WCONTINUED);
	if (1 == -w) {
	    perror ("waitpid");
	    idio_error_system_errno ("waitpid failed", IDIO_LIST2 (func, args));
	    exit (EXIT_FAILURE);
	}

	IDIO fn_w = idio_fixnum ((intptr_t) w);
	cstate = idio_hash_get (idio_command_jobs, fn_w);
	if (WIFEXITED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_EXITED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     idio_fixnum ((intptr_t) WEXITSTATUS (status)));
	} else if (WIFSIGNALED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_KILLED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     idio_fixnum ((intptr_t) WTERMSIG (status)));
	} else if (WIFSTOPPED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_STOPPED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     idio_fixnum ((intptr_t) WSTOPSIG (status)));
	} else if (WIFCONTINUED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_CONTINUED));
	}
    } while (!WIFEXITED (status) && !WIFSIGNALED (status));

    if (WIFEXITED (status) &&
	0 == WEXITSTATUS (status)) {
	return idio_struct_instance_ref_direct (cstate, 2);
    }

    idio_command_error_status (cstate);
    */

    /* notreached */
    return idio_S_unspec;
}

static int idio_command_job_is_stopped (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
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

static int idio_command_job_is_completed (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
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

static int idio_command_mark_process_status (pid_t pid, int status)
{
    if (pid > 0) {
	/*
	 * Some arbitrary process has a status update so we need to
	 * dig it out.
	 */
	IDIO jobs = idio_hash_keys_to_list (idio_command_jobs);
	while (idio_S_nil != jobs) {
	    IDIO job = IDIO_PAIR_H (jobs);

	    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
	    while (idio_S_nil != procs) {
		IDIO proc = IDIO_PAIR_H (procs);

		int proc_pid = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_PID));

		if (proc_pid == pid) {
		    idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_STATUS, idio_fixnum (status));

		    if (WIFSTOPPED (status)) {
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_STOPPED, idio_S_true);
		    } else {
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_COMPLETED, idio_S_true);
			if (WIFSIGNALED (status)) {
			    fprintf (stderr, "%d. Terminated by signal %d.\n", (int) pid, WTERMSIG (status));
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
	idio_error_system_errno ("waitpid failed", idio_S_nil);
    }

    return -1;
}

static void idio_command_update_status (void)
{
    int status;
    pid_t pid;

    do
	pid = waitpid (WAIT_ANY, &status, WUNTRACED);
    while (!idio_command_mark_process_status (pid, status));

}

static void idio_command_wait_for_job (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    int status;
    pid_t pid;

    do
	pid = waitpid (WAIT_ANY, &status, WUNTRACED);
    while (!idio_command_mark_process_status (pid, status) &&
	   !idio_command_job_is_stopped (job) &&
	   !idio_command_job_is_completed (job));

}

static void idio_command_format_job_info (IDIO job, char *msg)
{
    IDIO_ASSERT (job);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    
    pid_t job_pgid = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));

    fprintf (stderr, "%ld (%s): ", (long) job_pgid, msg);
    idio_debug ("%s\n", idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PIPELINE));
}

void idio_command_do_job_notification (void)
{
    /*
     * Get up to date info
     */
    idio_command_update_status ();

    IDIO jobs = idio_hash_keys_to_list (idio_command_jobs);
    while (idio_S_nil != jobs) {
	IDIO job = IDIO_PAIR_H (jobs);

	if (idio_command_job_is_completed (job)) {
	    idio_command_format_job_info (job, "completed");
	    idio_hash_delete (idio_command_jobs, job);
	} else if (idio_command_job_is_stopped (job)) {
	    IDIO ntfy = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_NOTIFIED);
	    if (idio_S_false == ntfy) {
		idio_command_format_job_info (job, "stopped");
		idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_NOTIFIED, idio_S_true);
	    }
	}

	/*
	 * else: no need to say anything about running jobs
	 */
	
	jobs = IDIO_PAIR_T (jobs);
    }
}

static void idio_command_foreground_job (IDIO job, int cont)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    /*
     * Put the job in the foreground
     */
    pid_t job_pgid = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));
    if (tcsetpgrp (idio_command_terminal, job_pgid) < 0) {
	idio_error_system ("tcsetpgrp", IDIO_LIST3 (idio_fixnum (idio_command_terminal),
						    idio_fixnum (job_pgid),
						    job), errno);		
    }

    if (cont) {
	IDIO otcattrs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_TCATTRS);
	IDIO_TYPE_ASSERT (opaque, otcattrs);
	struct termios *tcattrs = IDIO_OPAQUE_P (otcattrs);

	if (tcsetattr (idio_command_terminal, TCSADRAIN, tcattrs) < 0) {
	    idio_error_system_errno ("tcsetattr", IDIO_LIST1 (idio_fixnum (idio_command_terminal)));		
	}

	if (kill (-job_pgid, SIGCONT) < 0) {
	    idio_error_system_errno ("kill SIGCONT", IDIO_LIST1 (idio_fixnum (-job_pgid)));		
	}
    }

    idio_command_wait_for_job (job);

    /*
     * Put the shell back in the foreground.
     */
    if (tcsetpgrp (idio_command_terminal, idio_command_pgid) < 0) {
	idio_error_system ("tcsetpgrp",
			   IDIO_LIST3 (idio_fixnum (idio_command_terminal),
				       idio_fixnum (idio_command_pgid),
				       job),
			   errno);		
    }

    /*
     * Save the job's current terminal state
     */
    IDIO otcattrs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_TCATTRS);
    struct termios *tcattrs;
    if (idio_S_nil == otcattrs) {
	tcattrs = idio_alloc (sizeof (struct termios));
	otcattrs = idio_opaque (tcattrs);
	idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_TCATTRS, otcattrs);
    }

    if (tcgetattr (idio_command_terminal, tcattrs) < 0) {
	idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_fixnum (idio_command_terminal)));
    }

    /*
     * Restore the shell's terminal state
     */
    if (tcsetattr (idio_command_terminal, TCSADRAIN, &idio_command_tcattrs) < 0) {
	idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_fixnum (idio_command_terminal)));
    }
}

static void idio_command_background_job (IDIO job, int cont)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    if (cont) {
	int job_pgid = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));

	if (kill (-job_pgid, SIGCONT) < 0) {
	    idio_error_system_errno ("kill SIGCONT", IDIO_LIST1 (idio_fixnum (-job_pgid)));		
	}
    }
}

static void idio_command_mark_job_as_running (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	procs = IDIO_PAIR_T (procs);

	idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_STOPPED, idio_S_false);
    }

    idio_struct_instance_set_direct (job, IDIO_PROCESS_TYPE_NOTIFIED, idio_S_false);
}

static void idio_command_continue_job (IDIO job, int foreground)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    idio_command_mark_job_as_running (job);
    
    if (foreground) {
	idio_command_foreground_job (job, 1);
    } else {
	idio_command_background_job (job, 1);
    }
}

static IDIO idio_command_launch_process (IDIO proc, char **envp, pid_t job_pgid, int infile, int outfile, int foreground)
{
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (struct_instance, proc);

    if (! idio_struct_instance_isa (proc, idio_command_process_type)) {
	idio_error_param_type ("proc", proc);
    }

    idio_debug ("launch-process: %s\n", proc);


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
	    idio_error_system_errno ("setpgid", IDIO_LIST3 (idio_fixnum (pid), idio_fixnum (job_pgid), proc));
	}

	if (foreground) {
	    /*
	     * Give the terminal to the process group.  Dupe of parent
	     * to avoid race conditions.
	     */
	    if (tcsetpgrp (idio_command_terminal, job_pgid) < 0) {
		idio_error_system ("tcsetpgrp",
				   IDIO_LIST3 (idio_fixnum (idio_command_terminal),
					       idio_fixnum (job_pgid),
					       proc),
				   errno);		
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

    /*
     * Use the supplied stdin/stdout
     */
	fprintf (stderr, "%d stdin %d\n", (int) getpid (), infile);
    if (infile != STDIN_FILENO) {
	fprintf (stderr, "%d stdin %d\n", (int) getpid (), infile);
	if (dup2 (infile, STDIN_FILENO) < 0) {
	    idio_error_system ("dup2", IDIO_LIST3 (idio_fixnum (infile),
						   idio_fixnum (STDIN_FILENO),
						   proc), errno);		
	}
	if (close (infile) < 0) {
	    idio_error_system ("close", IDIO_LIST2 (idio_fixnum (infile),
						   proc), errno);		
	}
    }

    if (outfile != STDOUT_FILENO) {
	fprintf (stderr, "%d stdout %d\n", (int) getpid (), outfile);
	if (dup2 (outfile, STDOUT_FILENO) < 0) {
	    idio_error_system ("dup2", IDIO_LIST3 (idio_fixnum (outfile),
						   idio_fixnum (STDOUT_FILENO),
						   proc), errno);		
	}
	if (close (outfile) < 0) {
	    idio_error_system ("close", IDIO_LIST2 (idio_fixnum (outfile),
						   proc), errno);		
	}
    }

    /*
    char **argv = idio_command_argv (idio_struct_instance_ref_direct (proc, IDIO_PROCESS_TYPE_ARGV));

    if (NULL == argv) {
	idio_error_C ("bad argv", IDIO_LIST1 (proc));
    }

    execve (argv[0], argv, envp);
    perror ("execv");
    idio_command_error_exec ();
    exit (1);
    */
    return idio_S_unspec;
}

static void idio_command_launch_job (IDIO job, int foreground)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    idio_debug ("launch-job: %s\n", job);
    
    char **envp = idio_command_get_envp ();

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    int job_pgrp = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));
    int job_stdin = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDIN));
    int job_stdout = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDOUT));
    int infile = job_stdin;
    int outfile;
    int proc_pipe[2];
		  
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	procs = IDIO_PAIR_T (procs);

	if (idio_S_nil != procs) {
	    if (pipe (proc_pipe) < 0) {
		idio_error_system_errno ("pipe", IDIO_LIST2 (proc, job));
	    }
	    outfile = proc_pipe[1];
	} else {
	    outfile = IDIO_FIXNUM_VAL (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDOUT));
	}

	pid_t pid = fork ();
	if (pid < 0) {
	    idio_error_system_errno ("fork", IDIO_LIST2 (proc, job));
	} else if (0 == pid) {
	    idio_command_launch_process (proc,
					 envp,
					 job_pgrp,
					 infile,
					 outfile,
					 foreground);

	    /*
	     * In the info example, we would have execv'd a command in
	     * launch_process whereas we have merely gotten everything
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
	    idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_PID, idio_fixnum (pid));
	    if (idio_command_interactive) {
		if (0 == job_pgrp) {
		    job_pgrp = pid;
		    idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_PGID, idio_fixnum (job_pgrp));
		}
		if (setpgid (pid, job_pgrp) < 0) {
		    idio_error_system ("setpgid", IDIO_LIST4 (idio_fixnum (pid),
							      idio_fixnum (job_pgrp),
							      proc,
							      job), errno);
		}
	    }
	}

	/*
	 * Tidy up any trailing pipes!
	 */
	if (infile != job_stdin) {
	    if (close (infile) < 0) {
		idio_error_system_errno ("close", IDIO_LIST3 (idio_fixnum (infile), proc, job));
	    }
	}
	if (outfile != job_stdout) {
	    if (close (outfile) < 0) {
		idio_error_system_errno ("close", IDIO_LIST3 (idio_fixnum (outfile), proc, job));
	    }
	}

	infile = proc_pipe[0];
    }

    int j; 
    for (j = 0; NULL != envp[j]; j++) { 
    	free (envp[j]); 
    }
    free (envp);

    idio_debug ("launched %s\n", idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PIPELINE));

    if (! idio_command_interactive) {
	idio_command_wait_for_job (job);
    } else if (foreground) {
	idio_command_foreground_job (job, 0);
    } else {
	idio_command_background_job (job, 0);
    }
}

IDIO_DEFINE_PRIMITIVE1 ("%launch-job", launch_job, (IDIO job))
{
    IDIO_ASSERT (job);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_command_job_type)) {
	idio_error_param_type ("job", job);
    }

    idio_debug ("%launch-job: %s\n", job);
    idio_command_launch_job (job, 1);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V ("%launch-pipeline", launch_pipeline, (IDIO commands))
{
    IDIO_ASSERT (commands);
    IDIO_VERIFY_PARAM_TYPE (list, commands);

    idio_debug ("%launch-pipeline: %s\n", commands);

    IDIO procs = idio_S_nil;

    IDIO cmds = commands;
    while (idio_S_nil != cmds) {
	IDIO proc = idio_struct_instance (idio_command_process_type,
					  IDIO_LIST5 (IDIO_PAIR_H (cmds),
						      idio_fixnum (-1),
						      idio_S_false,
						      idio_S_false,
						      idio_S_nil));

	procs = idio_pair (proc, procs);
	
	cmds = IDIO_PAIR_T (cmds);
    }

    procs = idio_list_reverse (procs);

    IDIO job_stdin = idio_C_int (STDIN_FILENO);
    IDIO job_stdout = idio_C_int (STDOUT_FILENO);
    
    IDIO job = idio_struct_instance (idio_command_job_type,
				     idio_pair (commands,
				     idio_pair (procs,
				     idio_pair (idio_fixnum (0),
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_nil,
				     idio_pair (job_stdin,
				     idio_pair (job_stdout,
				     idio_S_nil))))))));
    
    idio_command_launch_job (job, 1);
    return idio_S_unspec;}


void idio_init_command ()
{
    idio_command_terminal = STDIN_FILENO;
    idio_command_interactive = isatty (idio_command_terminal);

    if (idio_command_interactive < 0) {
	idio_error_system_errno ("isatty", IDIO_LIST1 (idio_fixnum (idio_command_terminal)));
    }

    idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-terminal"),
				  idio_C_int (idio_command_terminal),
				  idio_main_module ());
    
    idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-interactive"),
				  idio_command_interactive ? idio_S_true : idio_S_false,
				  idio_main_module ());
    
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
		idio_error_system_errno ("kill SIGTTIN", IDIO_LIST1 (idio_fixnum (-idio_command_pgid)));
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
	idio_command_pgid = getpid ();
	if (setpgid (idio_command_pgid, idio_command_pgid) < 0) {
	    idio_error_system_errno ("setpgid", IDIO_LIST1 (idio_fixnum (idio_command_pgid)));
	}

	idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-pgid"),
				      idio_C_int (idio_command_pgid),
				      idio_main_module ());
    
	/*
	 * Grab control of the terminal.
	 */
	if (tcsetpgrp (idio_command_terminal, idio_command_pgid) < 0) {
	    idio_error_system ("tcsetpgrp",
			       IDIO_LIST2 (idio_fixnum (idio_command_terminal),
					   idio_fixnum (idio_command_pgid)),
			       errno);
	}

	/*
	 * Save default terminal attributes for shell.
	 */
	if (tcgetattr (idio_command_terminal, &idio_command_tcattrs) < 0) {
	    idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_fixnum (idio_command_terminal)));
	}

	idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-tcattrs"),
				      idio_C_pointer (&idio_command_tcattrs),
				      idio_main_module ());
    
    }

    idio_command_jobs = IDIO_HASH_EQP (8);
    idio_gc_protect (idio_command_jobs);

    idio_module_set_symbol_value (idio_symbols_C_intern ("%command-jobs"), idio_S_nil, idio_main_module ());

    IDIO name;

    name = idio_symbols_C_intern ("%command-process");
    idio_command_process_type = idio_struct_type (name,
						  idio_S_nil,
						  idio_pair (idio_symbols_C_intern ("argv"),
						  idio_pair (idio_symbols_C_intern ("pid"),
						  idio_pair (idio_symbols_C_intern ("completed"),
						  idio_pair (idio_symbols_C_intern ("stopped"),
						  idio_pair (idio_symbols_C_intern ("status"),
						  idio_S_nil))))));
    idio_module_set_symbol_value (name, idio_command_process_type, idio_main_module ());
						  
    name = idio_symbols_C_intern ("%command-job");
    idio_command_job_type = idio_struct_type (name,
					      idio_S_nil,
					      idio_pair (idio_symbols_C_intern ("pipeline"),
					      idio_pair (idio_symbols_C_intern ("procs"),
					      idio_pair (idio_symbols_C_intern ("pgid"),
					      idio_pair (idio_symbols_C_intern ("notified"),
					      idio_pair (idio_symbols_C_intern ("tcattrs"),
					      idio_pair (idio_symbols_C_intern ("stdin"),
					      idio_pair (idio_symbols_C_intern ("stdout"),
					      idio_S_nil))))))));
    idio_module_set_symbol_value (name, idio_command_job_type, idio_main_module ());
						  
    name = idio_symbols_C_intern ("%command-status");
    idio_command_status_type = idio_struct_type (name,
						 idio_S_nil,
						 IDIO_LIST3 (idio_symbols_C_intern ("pid"),
							     idio_symbols_C_intern ("state"),
							     idio_symbols_C_intern ("reason")));
    idio_module_set_symbol_value (name, idio_command_status_type, idio_main_module ());

    idio_command_status_types = idio_array (5);
    idio_gc_protect (idio_command_status_types);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("running"), 0);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("exited"), 1);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("killed"), 2);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("stopped"), 3);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("continued"), 4);
}

void idio_command_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (launch_job);
    IDIO_ADD_PRIMITIVE (launch_pipeline);
}

void idio_final_command ()
{
    /*
     * restore the terminal state
     */
    tcsetattr (idio_command_terminal, TCSADRAIN, &idio_command_tcattrs);

    idio_gc_expose (idio_command_jobs);
    idio_gc_expose (idio_command_status_types);
}

