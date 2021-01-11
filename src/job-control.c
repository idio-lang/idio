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
 * job_control.c
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

IDIO idio_job_control_module = idio_S_nil;
static pid_t idio_job_control_pid;
static pid_t idio_job_control_pgid;
static IDIO idio_job_control_tcattrs;
int idio_job_control_terminal;
int idio_job_control_interactive;

IDIO idio_job_control_process_type;
IDIO idio_job_control_job_type;
static IDIO idio_job_control_jobs_sym;
static IDIO idio_job_control_last_job;

static IDIO idio_S_background_job;
static IDIO idio_S_exit;
static IDIO idio_S_foreground_job;
static IDIO idio_S_killed;
static IDIO idio_S_wait_for_job;
IDIO idio_S_stdin_fileno;
IDIO idio_S_stdout_fileno;
IDIO idio_S_stderr_fileno;

static IDIO idio_job_control_default_child_handler_sym;

/*
 * Indexes into structures for direct references
 */
#define IDIO_JOB_TYPE_PIPELINE		0
#define IDIO_JOB_TYPE_PROCS		1
#define IDIO_JOB_TYPE_PGID		2
#define IDIO_JOB_TYPE_NOTIFIED		3
#define IDIO_JOB_TYPE_RAISED		4
#define IDIO_JOB_TYPE_TCATTRS		5
#define IDIO_JOB_TYPE_STDIN		6
#define IDIO_JOB_TYPE_STDOUT		7
#define IDIO_JOB_TYPE_STDERR		8

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
 * What this document doesn't say is if we can set an index in an
 * array of sig_atomic_t.
 *
 * NB Make the array IDIO_LIBC_NSIG + 1 as idio_vm_run1() will be
 * trying to access [IDIO_LIBC_NSIG] itself, not up to IDIO_LIBC_NSIG.
 */
volatile sig_atomic_t idio_job_control_signal_record[IDIO_LIBC_NSIG+1];

static void idio_job_control_error_exec (char **argv, char **envp, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

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
    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_command_exec_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       idio_fixnum ((intptr_t) errno)));
    idio_raise_condition (idio_S_true, c);
}

void idio_job_control_sa_signal (int signum)
{
    idio_job_control_signal_record[signum] = 1;
}

static int idio_job_control_job_is_stopped (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (idio_job_control_job_is_stopped (job)) {
	r = idio_S_true;
    }

    return r;
}

static int idio_job_control_job_is_completed (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (idio_job_control_job_is_completed (job)) {
	r = idio_S_true;
    }

    return r;
}

static int idio_job_control_job_failed (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    if (idio_job_control_job_is_completed (job)) {
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (idio_job_control_job_failed (job)) {
	r = idio_S_true;
    }

    return r;
}

static IDIO idio_job_control_job_status (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_job_control_job_status (job);
}

static IDIO idio_job_control_job_detail (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_job_control_job_detail (job);
}

static int idio_job_control_mark_process_status (pid_t pid, int status)
{
    if (pid > 0) {
	/*
	 * Some arbitrary process has a status update so we need to
	 * dig it out.
	 */
	IDIO jobs = idio_module_symbol_value (idio_job_control_jobs_sym, idio_job_control_module, idio_S_nil);
	while (idio_S_nil != jobs) {
	    IDIO job = IDIO_PAIR_H (jobs);

	    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
		idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return -3;
	    }

	    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
	    while (idio_S_nil != procs) {
		IDIO proc = IDIO_PAIR_H (procs);

		if (! idio_struct_instance_isa (proc, idio_job_control_process_type)) {
		    idio_error_param_type ("%idio-process", proc, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return -4;
		}

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

    IDIO_USER_TYPE_ASSERT (C_int, ipid);
    IDIO_USER_TYPE_ASSERT (C_pointer, istatus);

    int pid = IDIO_C_TYPE_INT (ipid);
    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (idio_job_control_mark_process_status (pid, *statusp)) {
	r = idio_S_true;
    }

    return r;
}

static void idio_job_control_update_status (void)
{
    int status;
    pid_t pid;

    do
	pid = waitpid (WAIT_ANY, &status, WUNTRACED | WNOHANG);
    while (!idio_job_control_mark_process_status (pid, status));

}

IDIO_DEFINE_PRIMITIVE0_DS ("update-status", update_status, (), "", "\
update the process status of any outstanding pids\n\
						\n\
:return: #<unspec>				\n\
")
{
    idio_job_control_update_status ();

    return idio_S_unspec;
}

static IDIO idio_job_control_wait_for_job (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int status;
    pid_t pid;

    do
	pid = waitpid (WAIT_ANY, &status, WUNTRACED);
    while (!idio_job_control_mark_process_status (pid, status) &&
	   !idio_job_control_job_is_stopped (job) &&
	   !idio_job_control_job_is_completed (job));

    if (idio_job_control_job_failed (job)) {
	IDIO raised = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_RAISED);
	if (idio_S_false == raised) {
	    IDIO c = idio_struct_instance (idio_condition_rt_command_status_error_type,
					   IDIO_LIST4 (idio_string_C ("C/job failed"),
						       IDIO_C_FUNC_LOCATION (),
						       job,
						       idio_job_control_job_status (job)));

	    idio_struct_instance_set_direct (job, IDIO_JOB_TYPE_RAISED, idio_S_true);
	    idio_reraise_condition (idio_S_true, c);

	    return idio_S_notreached;
	}
    }

    return idio_job_control_job_status (job);
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_job_control_wait_for_job (job);
}

static void idio_job_control_format_job_info (IDIO job, char *msg)
{
    IDIO_ASSERT (job);
    IDIO_C_ASSERT (msg);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (0 == idio_job_control_interactive) {
	return;
    }

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    pid_t job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));

    fprintf (stderr, "job %5ld (%s)", (long) job_pgid, msg);
    idio_debug (": %s\n", idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PIPELINE));
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);
    IDIO_USER_TYPE_ASSERT (string, msg);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    size_t size = 0;
    char *msgs = idio_string_as_C (msg, &size);
    idio_job_control_format_job_info (job, msgs);
    IDIO_GC_FREE (msgs);

    return idio_S_unspec;
}

void idio_job_control_do_job_notification ()
{
    /*
     * Get up to date info
     */
    idio_job_control_update_status ();

    IDIO jobs = idio_module_symbol_value (idio_job_control_jobs_sym, idio_job_control_module, idio_S_nil);
    IDIO njobs = idio_S_nil;

    while (idio_S_nil != jobs) {
	IDIO job = IDIO_PAIR_H (jobs);

	if (idio_job_control_job_is_completed (job)) {
	    idio_job_control_format_job_info (job, "completed");
	} else if (idio_job_control_job_is_stopped (job)) {
	    IDIO ntfy = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_NOTIFIED);
	    if (idio_S_false == ntfy) {
		idio_job_control_format_job_info (job, "stopped");
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

    idio_module_set_symbol_value (idio_job_control_jobs_sym, njobs, idio_job_control_module);

    /*
     * Scheduling the failed-jobs code here in C-land breaks the stack
     * in hard to debug ways.  Leave it in Idio-land.
     */
}

IDIO_DEFINE_PRIMITIVE0_DS ("do-job-notification", do_job_notification, (), "", "\
notify of any job status changes		\n\
						\n\
")
{
    idio_job_control_do_job_notification ();

    return idio_S_unspec;
}

static IDIO idio_job_control_foreground_job (IDIO job, int cont)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    pid_t job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));

    /*
     * Put the job in the foreground
     */
    if (tcsetpgrp (idio_job_control_terminal, job_pgid) < 0) {
	idio_error_system ("icfg tcsetpgrp",
			   IDIO_LIST3 (idio_C_int (idio_job_control_terminal),
				       idio_C_int (job_pgid),
				       job),
			   errno,
			   IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (cont) {
	IDIO job_tcattrs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_TCATTRS);
	IDIO_TYPE_ASSERT (C_pointer, job_tcattrs);
	struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (job_tcattrs);

	if (tcsetattr (idio_job_control_terminal, TCSADRAIN, tcattrsp) < 0) {
	    idio_error_system_errno ("tcsetattr", IDIO_LIST1 (idio_C_int (idio_job_control_terminal)), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (kill (-job_pgid, SIGCONT) < 0) {
	    idio_error_system_errno ("kill SIGCONT", IDIO_LIST1 (idio_C_int (-job_pgid)), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO r = idio_vm_invoke_C (idio_thread_current_thread (),
			       IDIO_LIST2 (idio_module_symbol_value (idio_S_wait_for_job,
								     idio_job_control_module,
								     idio_S_nil),
					   job));
    /* IDIO r = idio_job_control_wait_for_job (job); */

    /*
     * Put the shell back in the foreground.
     */
    if (tcsetpgrp (idio_job_control_terminal, idio_job_control_pgid) < 0) {
	idio_error_system ("tcsetpgrp",
			   IDIO_LIST3 (idio_C_int (idio_job_control_terminal),
				       idio_C_int (idio_job_control_pgid),
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

    if (tcgetattr (idio_job_control_terminal, tcattrsp) < 0) {
	idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_C_int (idio_job_control_terminal)), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Restore the shell's terminal state
     */
    tcattrsp = IDIO_C_TYPE_POINTER_P (idio_job_control_tcattrs);
    if (tcsetattr (idio_job_control_terminal, TCSADRAIN, tcattrsp) < 0) {
	idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_C_int (idio_job_control_terminal)), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);
    IDIO_USER_TYPE_ASSERT (boolean, icont);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int cont = 0;

    if (idio_S_true == icont) {
	cont = 1;
    }

    return idio_job_control_foreground_job (job, cont);
}

static IDIO idio_job_control_background_job (IDIO job, int cont)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);
    IDIO_USER_TYPE_ASSERT (boolean, icont);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int cont = 0;

    if (idio_S_true == icont) {
	cont = 1;
    }

    return idio_job_control_background_job (job, cont);
}

static void idio_job_control_hangup_job (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_job_control_format_job_info (job, "SIGHUP'ed");

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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_job_control_hangup_job (job);

    return idio_S_unspec;
}

IDIO idio_job_control_SIGHUP_signal_handler ()
{
    IDIO jobs = idio_module_symbol_value (idio_job_control_jobs_sym, idio_job_control_module, idio_S_nil);
    if (idio_S_nil != jobs) {
	fprintf (stderr, "HUP: There are outstanding jobs\n");
	idio_debug ("jobs %s\n", jobs);
	while (idio_S_nil != jobs) {
	    IDIO job = IDIO_PAIR_H (jobs);
	    idio_debug ("job %s\n", job);
	    idio_job_control_hangup_job (job);
	    jobs = IDIO_PAIR_T (jobs);
	}
    }

    return idio_S_unspec;
}

IDIO idio_job_control_SIGCHLD_signal_handler ()
{
    /*
     * do-job-notification is a thunk so we can call it direct
     */
    IDIO r = idio_vm_invoke_C (idio_thread_current_thread (),
			       idio_module_symbol_value (idio_symbols_C_intern ("do-job-notification"),
							 idio_job_control_module,
							 idio_S_nil));

    return r;
}

static void idio_job_control_mark_job_as_running (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);

	if (! idio_struct_instance_isa (proc, idio_job_control_process_type)) {
	    idio_error_param_type ("%idio-process", proc, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_STOPPED, idio_S_false);

	procs = IDIO_PAIR_T (procs);
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_job_control_mark_job_as_running (job);

    return idio_S_unspec;
}

static void idio_job_control_continue_job (IDIO job, int foreground)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_job_control_mark_job_as_running (job);

    if (foreground) {
	idio_job_control_foreground_job (job, 1);
    } else {
	idio_job_control_background_job (job, 1);
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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);
    IDIO_USER_TYPE_ASSERT (boolean, iforeground);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int foreground = 0;

    if (idio_S_true == iforeground) {
	foreground = 1;
    }

    idio_job_control_continue_job (job, foreground);

    return idio_S_unspec;
}

static void idio_job_control_prep_io (int infile, int outfile, int errfile)
{
    /* fprintf (stderr, "idio_job_control_prep_io: %d %d %d\n", infile, outfile, errfile); */
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

static void idio_job_control_prep_process (pid_t job_pgid, int infile, int outfile, int errfile, int foreground)
{
    pid_t pid;

    if (idio_job_control_interactive) {
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
	    if (tcsetpgrp (idio_job_control_terminal, job_pgid) < 0) {
		idio_error_system ("icpp tcsetpgrp",
				   IDIO_LIST2 (idio_C_int (idio_job_control_terminal),
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

    idio_job_control_prep_io (infile, outfile, errfile);
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

    IDIO_USER_TYPE_ASSERT (C_int, iinfile);
    IDIO_USER_TYPE_ASSERT (C_int, ioutfile);
    IDIO_USER_TYPE_ASSERT (C_int, ierrfile);
    IDIO_USER_TYPE_ASSERT (boolean, iforeground);

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

    idio_job_control_prep_process (pgid, infile, outfile, errfile, foreground);

    return idio_S_unspec;
}

static void idio_job_control_launch_job (IDIO job, int foreground)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

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

	if (! idio_struct_instance_isa (proc, idio_job_control_process_type)) {
	    idio_error_param_type ("%idio-process", proc, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

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
	    idio_condition_set_default_handler (idio_condition_idio_error_type,
						idio_module_symbol_value (idio_job_control_default_child_handler_sym,
									  idio_job_control_module,
									  idio_S_nil));

	    idio_job_control_prep_process (job_pgid,
					   infile,
					   outfile,
					   job_stderr,
					   foreground);
	    /*
	     * In the info example, we would have execv'd a job_control in
	     * prep_process whereas we have merely gotten everything
	     * ready here -- as the "job_control" is more Idio code albeit
	     * quite likely to be an external job_control since we're in a
	     * pipeline.
	     *
	     * If we don't return we'll fall through to the parent's
	     * code to report on launching the pipeline etc.  Which is
	     * confusing.
	     */
	    return;
	} else {
	    idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_PID, idio_C_int (pid));
	    if (idio_job_control_interactive) {
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

    if (! idio_job_control_interactive) {
	idio_job_control_wait_for_job (job);
    } else if (foreground) {
	idio_job_control_foreground_job (job, 0);
    } else {
	idio_job_control_background_job (job, 0);
    }
}

IDIO idio_job_control_launch_1proc_job (IDIO job, int foreground, char **argv)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PROCS);
    IDIO proc = IDIO_PAIR_H (procs);

    if (! idio_struct_instance_isa (proc, idio_job_control_process_type)) {
	idio_error_param_type ("%idio-process", proc, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    pid_t job_pgid = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_PGID));
    int job_stdin = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDIN));
    int job_stdout = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDOUT));
    int job_stderr = IDIO_C_TYPE_INT (idio_struct_instance_ref_direct (job, IDIO_JOB_TYPE_STDERR));

    /*
     * We're here because the VM saw a symbol in functional position
     * -- which we have found is an external job_control on PATH -- but we
     * don't know whether we're in a pipeline or the job_control was
     * inline.
     *
     * If we're in a pipeline then our pid will be different to the
     * original Idio's pid.
     */
    if (getpid () == idio_job_control_pid) {
	IDIO jobs = idio_module_symbol_value (idio_job_control_jobs_sym, idio_job_control_module, idio_S_nil);
	idio_module_set_symbol_value (idio_job_control_jobs_sym, idio_pair (job, jobs), idio_job_control_module);

	idio_module_set_symbol_value (idio_job_control_last_job, job, idio_job_control_module);

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
	    idio_condition_set_default_handler (idio_condition_idio_error_type,
						idio_module_symbol_value (idio_job_control_default_child_handler_sym,
									  idio_job_control_module,
									  idio_S_nil));

	    idio_job_control_prep_process (job_pgid,
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
	    idio_job_control_error_exec (argv, envp, IDIO_C_FUNC_LOCATION ());

	    exit (33);
	    return idio_S_notreached;
	} else {
	    idio_struct_instance_set_direct (proc, IDIO_PROCESS_TYPE_PID, idio_C_int (pid));
	    if (idio_job_control_interactive) {
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
	    if (! idio_job_control_interactive) {
		IDIO wfj = idio_module_symbol_value (idio_S_wait_for_job, idio_job_control_module, idio_S_nil);
		cmd = IDIO_LIST2 (wfj, job);
	    } else if (foreground) {
		IDIO fj = idio_module_symbol_value (idio_S_foreground_job, idio_job_control_module, idio_S_nil);
		cmd = IDIO_LIST3 (fj, job, idio_S_false);
	    } else {
		IDIO bj = idio_module_symbol_value (idio_S_background_job, idio_job_control_module, idio_S_nil);
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
	idio_job_control_prep_io (job_stdin,
				  job_stdout,
				  job_stderr);

	char **envp = idio_command_get_envp ();

	execve (argv[0], argv, envp);
	perror ("execv");
	idio_job_control_error_exec (argv, envp, IDIO_C_FUNC_LOCATION ());

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

    IDIO_USER_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_debug ("%launch-job: %s\n", job);
    idio_job_control_launch_job (job, 1);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("%launch-pipeline", launch_pipeline, (IDIO job_controls), "job_controls", "\
launch a pipeline of `job_controls`			\n\
						\n\
:param job_controls: list of job_controls		\n\
						\n\
:return: #<unspec>				\n\
")
{
    IDIO_ASSERT (job_controls);

    IDIO_USER_TYPE_ASSERT (list, job_controls);

    idio_debug ("%launch-pipeline: %s\n", job_controls);

    IDIO procs = idio_S_nil;

    IDIO cmds = job_controls;
    while (idio_S_nil != cmds) {
	IDIO proc = idio_struct_instance (idio_job_control_process_type,
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

    IDIO job = idio_struct_instance (idio_job_control_job_type,
				     idio_pair (job_controls,
				     idio_pair (procs,
				     idio_pair (idio_C_int (0),
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_nil,
				     idio_pair (job_stdin,
				     idio_pair (job_stdout,
				     idio_pair (job_stderr,
				     idio_S_nil)))))))));

    idio_job_control_launch_job (job, 1);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0_DS ("%interactive?", interactivep, (void), "", "\
get the current interactiveness			\n\
						\n\
:return: #t or #f				\n\
")
{
    IDIO r = idio_S_false;

    if (idio_job_control_interactive) {
	r = idio_S_true;
    }

    return r;
}

void idio_job_control_set_interactive (int interactive)
{
    if (interactive < 0) {
	idio_error_system_errno ("isatty", IDIO_LIST1 (idio_C_int (idio_job_control_terminal)), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_job_control_interactive = interactive;

    if (idio_job_control_interactive) {
	/*
	 * If we should be interactive then loop until we are in the
	 * foreground.
	 *
	 * How tight is this loop?  Presumably the kill suspends us
	 * until we check again.
	 */
	int c = 0;
	while (tcgetpgrp (idio_job_control_terminal) != (idio_job_control_pgid = getpgrp ())) {
	    fprintf (stderr, "%2d: tcgetpgrp(%d)=%d getpgrp()=%d\n", c, tcgetpgrp (idio_job_control_terminal), idio_job_control_terminal, getpgrp ());
	    c++;
	    if (c > 2) {
		exit (128 + 15);
	    }
	    if (kill (-idio_job_control_pgid, SIGTTIN) < 0) {
		idio_error_system_errno ("kill SIGTTIN", IDIO_LIST1 (idio_C_int (-idio_job_control_pgid)), IDIO_C_FUNC_LOCATION ());

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
	 * XXX ignoring SIGCHLD means an explicit waitpid (<pid>) will
	 * get ECHILD
	 */
	/* signal (SIGCHLD, SIG_IGN); */

	/*
	 * Put ourselves in our own process group.
	 */
	idio_job_control_pgid = idio_job_control_pid;

	pid_t sid = getsid (0);
	if (sid != idio_job_control_pgid) {
	    if (setpgid (idio_job_control_pgid, idio_job_control_pgid) < 0) {
		/*
		 * Test Case: ??
		 *
		 * 1: Triggered by rlwrap(1):
		 *
		 *    setpgid() returns EPERM ... or to change the
		 *    process group ID of a session leader.
		 *
		 *    That appears to be the case even if we are
		 *    setting it to ourselves.
		 *
		 * 2: Also, we can get here with an ESRCH if we have
		 *    an errant child which decides to run back
		 *    through this loop.  It will use
		 *    idio_job_control_pgid when it, itself, is
		 *    $CHILD_PID (even though that should be allowed).
		 *
		 *    I've had a very low hit-rate when trying to
		 *    provoke environ errors.
		 */
		idio_error_system_errno ("setpgid", IDIO_LIST1 (idio_C_int (idio_job_control_pgid)), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}

	idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-pgid"),
				      idio_C_int (idio_job_control_pgid),
				      idio_job_control_module);

	/*
	 * Grab control of the terminal.
	 */
	if (tcsetpgrp (idio_job_control_terminal, idio_job_control_pgid) < 0) {
	    idio_error_system ("tcsetpgrp",
			       IDIO_LIST2 (idio_C_int (idio_job_control_terminal),
					   idio_C_int (idio_job_control_pgid)),
			       errno,
			       IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	/*
	 * Save default terminal attributes for shell.
	 */
	struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (idio_job_control_tcattrs);
	if (tcgetattr (idio_job_control_terminal, tcattrsp) < 0) {
	    idio_error_system_errno ("tcgetattr", IDIO_LIST1 (idio_C_int (idio_job_control_terminal)), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }
}

void idio_job_control_add_primitives ()
{
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, job_is_stopped);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, job_is_completed);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, job_failed);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, job_status);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, job_detail);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, mark_process_status);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, update_status);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, wait_for_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, format_job_info);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, do_job_notification);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, foreground_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, background_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, hangup_job);

    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, mark_job_as_running);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, continue_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, prep_process);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, launch_job);
    IDIO_ADD_MODULE_PRIMITIVE (idio_job_control_module, launch_pipeline);
}

void idio_final_job_control ()
{
    /*
     * restore the terminal state
     */
    if (idio_job_control_interactive) {
	struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (idio_job_control_tcattrs);
	tcsetattr (idio_job_control_terminal, TCSADRAIN, tcattrsp);
    }

    /*
     * Be a good citizen and tidy up.  This will reported completed
     * jobs, though.  Maybe we should suppress the reports.
     */
    idio_job_control_set_interactive (0);

    /*
     * This deliberately uses the C versions of these functions as
     * other modules have been shutting down -- we don't want to be
     * running any more Idio code here!
     */
    idio_job_control_do_job_notification ();

    idio_job_control_SIGHUP_signal_handler (idio_C_int (SIGHUP));
}

void idio_init_job_control ()
{
    idio_module_table_register (idio_job_control_add_primitives, idio_final_job_control);

    idio_job_control_module = idio_module (idio_symbols_C_intern ("job-control"));
    IDIO_MODULE_IMPORTS (idio_job_control_module) = IDIO_LIST2 (IDIO_LIST1 (idio_Idio_module),
								IDIO_LIST1 (idio_primitives_module));

    idio_S_background_job = idio_symbols_C_intern ("background-job");
    idio_S_exit = idio_symbols_C_intern ("exit");
    idio_S_foreground_job = idio_symbols_C_intern ("foreground-job");
    idio_S_killed = idio_symbols_C_intern ("killed");
    idio_S_wait_for_job = idio_symbols_C_intern ("wait-for-job");
    idio_S_stdin_fileno = idio_symbols_C_intern ("stdin-fileno");
    idio_S_stdout_fileno = idio_symbols_C_intern ("stdout-fileno");
    idio_S_stderr_fileno = idio_symbols_C_intern ("stderr-fileno");

    struct termios *tcattrsp = idio_alloc (sizeof (struct termios));
    idio_job_control_tcattrs = idio_C_pointer_free_me (tcattrsp);

    idio_module_set_symbol_value (idio_symbols_C_intern ("%idio-tcattrs"),
				  idio_job_control_tcattrs,
				  idio_job_control_module);

    int signum;
    for (signum = IDIO_LIBC_FSIG; signum <= IDIO_LIBC_NSIG; signum++) {
	idio_job_control_signal_record[signum] = 0;
    }

    struct sigaction nsa, osa;
    nsa.sa_handler = idio_job_control_sa_signal;
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

    /*
     * The following is from the "info libc" pages, 28.5.2
     * Initializing the Shell.
     *
     * With some patching of Idio values.
     */
    idio_job_control_pid = getpid ();
    idio_job_control_terminal = STDIN_FILENO;

    IDIO sym;
    sym = idio_symbols_C_intern ("%idio-terminal");
    idio_module_set_symbol_value (sym,
				  idio_C_int (idio_job_control_terminal),
				  idio_job_control_module);
    IDIO v = idio_module_symbol_value (sym,
				       idio_job_control_module,
				       idio_S_nil);
    IDIO_FLAGS (v) |= IDIO_FLAG_CONST;

    /*
     * The Idio-visible %idio-interactive should be read-only.
     * However, we actually play some tricks with it like disabling
     * during {load} so we don't get plagued with job failure
     * messages.  So it should be a (read-only) computed variable.
     */
    IDIO geti;
    geti = IDIO_ADD_PRIMITIVE (interactivep);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("%idio-interactive"),
				     idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)),
				     idio_S_nil,
				     idio_job_control_module);

    /*
     * Not noted in the Job Control docs is that if we are launched
     * non-interactively then we never set
     * idio_job_control_pgid/%idio-pgid with a later complaint about a
     * symbol not being a C_int when the variable is accessed in
     * foreground-job in job_control.idio.
     *
     * More specifically, idio_job_control_pgid is set to 0 (as a C
     * global) but we don't (otherwise) explicitly set the matching
     * Idio variable, %idio-pgid.  I guess the C example wasn't
     * expecting this sort of parallel behaviour...
     *
     * Arguably foreground-job shouldn't be changing pgid if the shell
     * is not interactive -- but there's still a sense of using unset
     * variables which we should avoid.
     */
    idio_job_control_pgid = getpgrp ();
    sym = idio_symbols_C_intern ("%idio-pgid");
    idio_module_set_symbol_value (sym,
				  idio_C_int (idio_job_control_pgid),
				  idio_job_control_module);
    v = idio_module_symbol_value (sym,
				  idio_job_control_module,
				  idio_S_nil);
    IDIO_FLAGS (v) |= IDIO_FLAG_CONST;

    idio_job_control_jobs_sym = idio_symbols_C_intern ("%idio-jobs");
    idio_module_set_symbol_value (idio_job_control_jobs_sym, idio_S_nil, idio_job_control_module);
    idio_job_control_last_job = idio_symbols_C_intern ("%%last-job");
    idio_module_set_symbol_value (idio_job_control_last_job, idio_S_nil, idio_job_control_module);

    sym = idio_symbols_C_intern ("%idio-process");
    idio_job_control_process_type = idio_struct_type (sym,
						      idio_S_nil,
						      idio_pair (idio_symbols_C_intern ("argv"),
						      idio_pair (idio_symbols_C_intern ("pid"),
						      idio_pair (idio_symbols_C_intern ("completed"),
						      idio_pair (idio_symbols_C_intern ("stopped"),
						      idio_pair (idio_symbols_C_intern ("status"),
						      idio_S_nil))))));
    idio_module_set_symbol_value (sym, idio_job_control_process_type, idio_job_control_module);

    sym = idio_symbols_C_intern ("%idio-job");
    idio_job_control_job_type = idio_struct_type (sym,
						  idio_S_nil,
						  idio_pair (idio_symbols_C_intern ("pipeline"),
						  idio_pair (idio_symbols_C_intern ("procs"),
						  idio_pair (idio_symbols_C_intern ("pgid"),
						  idio_pair (idio_symbols_C_intern ("notified"),
						  idio_pair (idio_symbols_C_intern ("raised"),
						  idio_pair (idio_symbols_C_intern ("tcattrs"),
						  idio_pair (idio_symbols_C_intern ("stdin"),
						  idio_pair (idio_symbols_C_intern ("stdout"),
						  idio_pair (idio_symbols_C_intern ("stderr"),
						  idio_S_nil))))))))));
    idio_module_set_symbol_value (sym, idio_job_control_job_type, idio_job_control_module);

    idio_job_control_default_child_handler_sym = idio_symbols_C_intern ("default-child-handler");
}

