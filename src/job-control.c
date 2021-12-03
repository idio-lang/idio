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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "c-type.h"
#include "command.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "libc-wrap.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vars.h"
#include "vm.h"

#include "libc-api.h"

IDIO idio_job_control_module = idio_S_nil;
static pid_t idio_job_control_pid;
static pid_t idio_job_control_pgid;
static IDIO idio_job_control_tcattrs;
int idio_job_control_tty_fd;
int idio_job_control_tty_isatty;
int idio_job_control_interactive;

pid_t idio_job_control_cmd_pid;

IDIO idio_job_control_process_type;
IDIO idio_job_control_job_type;
static IDIO idio_job_control_jobs_sym;
static IDIO idio_job_control_last_job;
IDIO idio_job_control_known_pids_sym;
IDIO idio_job_control_stray_pids_sym;

static IDIO idio_S_background_job;
static IDIO idio_S_foreground_job;
static IDIO idio_S_wait_for_job;
IDIO idio_S_stdin_fileno;
IDIO idio_S_stdout_fileno;
IDIO idio_S_stderr_fileno;

static IDIO idio_job_control_default_child_handler_sym;
static IDIO idio_job_control_djn_sym;

/*
 * Indexes into structures for direct references
 */
#define IDIO_JOB_ST_PIPELINE		0
#define IDIO_JOB_ST_PROCS		1
#define IDIO_JOB_ST_PGID		2
#define IDIO_JOB_ST_NOTIFY_STOPPED	3
#define IDIO_JOB_ST_NOTIFY_COMPLETED	4
#define IDIO_JOB_ST_RAISEP		5
#define IDIO_JOB_ST_RAISED		6
#define IDIO_JOB_ST_TCATTRS		7
#define IDIO_JOB_ST_STDIN		8
#define IDIO_JOB_ST_STDOUT		9
#define IDIO_JOB_ST_STDERR		10
#define IDIO_JOB_ST_REPORT_TIMING	11
#define IDIO_JOB_ST_TIMING_START	12
#define IDIO_JOB_ST_TIMING_END		13
#define IDIO_JOB_ST_ASYNC		14

#define IDIO_PROCESS_ST_ARGV		0
#define IDIO_PROCESS_ST_EXEC		1
#define IDIO_PROCESS_ST_PID		2
#define IDIO_PROCESS_ST_COMPLETED	3
#define IDIO_PROCESS_ST_STOPPED		4
#define IDIO_PROCESS_ST_STATUS		5

/* PSJ is PROCESS_SUBSTITUTION_JOB */
#define IDIO_PSJ_READ			0
#define IDIO_PSJ_FD			1
#define IDIO_PSJ_PATH			2
#define IDIO_PSJ_DIR			3
#define IDIO_PSJ_SUPPRESS		4

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

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_command_exec_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       detail,
					       idio_fixnum ((intptr_t) errno)));
    idio_raise_condition (idio_S_true, c);
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

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	procs = IDIO_PAIR_T (procs);

	if (idio_S_false == idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_COMPLETED) &&
	    idio_S_false == idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_STOPPED)) {
	    return 0;
	}
    }

    return 1;
}

static int idio_job_control_job_is_async (IDIO job)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    if (idio_S_true == idio_struct_instance_ref_direct (job, IDIO_JOB_ST_ASYNC)) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("job-is-stopped", job_is_stopped, (IDIO job), "job", "\
test if job `job` is stopped			\n\
						\n\
:param job: job to test				\n\
:type job: struct-instance			\n\
:return: ``#t`` if job `job` is stopped, ``#f`` otherwise\n\
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

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	if (idio_S_false == idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_COMPLETED)) {
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
:type job: struct-instance			\n\
:return: ``#t`` if job `job` has completed, ``#f`` otherwise\n\
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
	IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
	while (idio_S_nil != procs) {
	    IDIO proc = IDIO_PAIR_H (procs);
	    IDIO istatus = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_STATUS);
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
:type job: struct-instance			\n\
:return: ``#t`` if job `job` has failed, ``#f`` otherwise\n\
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

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	IDIO istatus = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_STATUS);
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
:type job: struct-instance			\n\
:return: ``#f`` if job `job` has a process status, ``#t`` otherwise\n\
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

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);
	IDIO istatus = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_STATUS);
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

    return IDIO_LIST2 (idio_S_exit, idio_C_int (0));
}

IDIO_DEFINE_PRIMITIVE1_DS ("job-detail", job_detail, (IDIO job), "job", "\
return the process status of job `job`		\n\
						\n\
:param job: job					\n\
:type job: struct-instance			\n\
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

	    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
	    while (idio_S_nil != procs) {
		IDIO proc = IDIO_PAIR_H (procs);

		if (! idio_struct_instance_isa (proc, idio_job_control_process_type)) {
		    idio_error_param_type ("%idio-process", proc, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return -4;
		}

		int proc_pid = IDIO_C_TYPE_libc_pid_t (idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_PID));

		if (proc_pid == pid) {
		    IDIO proc_status = idio_struct_instance_ref_direct (proc, IDIO_PROCESS_ST_STATUS);
		    if (idio_S_nil == proc_status) {
			int *statusp = idio_alloc (sizeof (int));
			*statusp = status;
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_ST_STATUS, idio_C_pointer_free_me (statusp));
		    } else {
			int *statusp = IDIO_C_TYPE_POINTER_P (proc_status);
			*statusp = status;
		    }

		    if (WIFSTOPPED (status)) {
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_ST_STOPPED, idio_S_true);
		    } else {
			idio_struct_instance_set_direct (proc, IDIO_PROCESS_ST_COMPLETED, idio_S_true);
			if (WIFSIGNALED (status)) {
			    fprintf (stderr, "Job Terminated: kill -%s %ld: ", idio_libc_signal_name (WTERMSIG (status)), (long) pid);
			    idio_debug ("%s\n", idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PIPELINE));
			}
		    }

		    return 0;
		}

		procs = IDIO_PAIR_T (procs);
	    }

	    jobs = IDIO_PAIR_T (jobs);
	}

	if (idio_job_control_interactive) {
	    fprintf (stderr, "No child process %d.\n", (int) pid);
	}
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
:param pid: Process ID				\n\
:type pid: libc/pid_t				\n\
:param status: Unix process status		\n\
:type status: C/pointer				\n\
:return: ``#t`` if the update was successfull, ``#f`` otherwise\n\
")
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (istatus);

    IDIO_USER_libc_TYPE_ASSERT (pid_t, ipid);
    IDIO_USER_C_TYPE_ASSERT (pointer, istatus);

    int pid = IDIO_C_TYPE_libc_pid_t (ipid);
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
:return: ``#<unspec>``				\n\
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
	IDIO raised = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_RAISED);
	if (idio_S_false == raised) {
	    IDIO c = idio_struct_instance (idio_condition_rt_command_status_error_type,
					   IDIO_LIST4 (idio_string_C_len (IDIO_STATIC_STR_LEN ("C/job failed")),
						       IDIO_C_FUNC_LOCATION (),
						       job,
						       idio_job_control_job_status (job)));

	    idio_struct_instance_set_direct (job, IDIO_JOB_ST_RAISED, idio_S_true);
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
:type job: struct-instance			\n\
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

static void idio_job_control_format_job_info (IDIO job, char const *msg)
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

    pid_t job_pgid = IDIO_C_TYPE_libc_pid_t (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PGID));

    fprintf (stderr, "job %5ld (%s)", (long) job_pgid, msg);
    idio_debug (": %s\n", idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PIPELINE));
}

IDIO_DEFINE_PRIMITIVE2_DS ("format-job-info", format_job_info, (IDIO job, IDIO msg), "job msg", "\
display to stderr `msg` alongside job `job` details\n\
						\n\
:param job: job					\n\
:type job: struct-instance			\n\
:param msg: string				\n\
:type msg: string				\n\
:return: ``#<unspec>``				\n\
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

    IDIO_GC_FREE (msgs, size);

    return idio_S_unspec;
}

/*
 * idio_job_control_do_job_notification() is called:
 *
 * * from the primitive do-job-notification which should be
     overwritten when job-control.idio is loaded
 *
 * * during shutdown
 *
 * Notice we don't do any fancy asynchronous process handling.  We're
 * shutting down!
 */

void idio_job_control_do_job_notification ()
{
    /*
     * Get up to date info
     */
    idio_job_control_update_status ();

    IDIO ps_jobs = idio_module_symbol_value (IDIO_SYMBOLS_C_INTERN ("%%process-substitution-jobs"), idio_job_control_module, idio_S_nil);

    IDIO jobs = idio_module_symbol_value (idio_job_control_jobs_sym, idio_job_control_module, idio_S_nil);
    IDIO njobs = idio_S_nil;

    while (idio_S_nil != jobs) {
	IDIO job = IDIO_PAIR_H (jobs);

	if (idio_job_control_job_is_completed (job)) {
	    idio_job_control_format_job_info (job, "completed");
	} else if (idio_job_control_job_is_stopped (job)) {
	    IDIO ntfy = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_NOTIFY_STOPPED);
	    if (idio_S_false == ntfy) {
		idio_job_control_format_job_info (job, "stopped");
		idio_struct_instance_set_direct (job, IDIO_JOB_ST_NOTIFY_STOPPED, idio_S_true);
	    }
	    njobs = idio_pair (job, njobs);
	} else {
	    njobs = idio_pair (job, njobs);
	}

	/*
	 * else: no need to say anything about running jobs
	 *
	 * However, take the opportunity during shutdown to clean up
	 * any extant true named pipes.
	 *
	 * Remember, we're on the way out, don't care so much about
	 * errors
	 *
	 */
	if (IDIO_STATE_SHUTDOWN == idio_state &&
	    idio_S_nil != ps_jobs) {
	    IDIO psj = idio_hash_ref (ps_jobs, job);

	    if (idio_S_unspec != psj) {
		IDIO psj_path = idio_struct_instance_ref_direct (psj, IDIO_PSJ_PATH);
		if (idio_S_false != psj_path) {
#ifdef IDIO_DEBUG
		    fprintf (stderr, "%6d: SHUTDOWN: ", getpid ());
		    idio_debug ("unlink/rm %s\n", psj);
#endif
		    size_t size = 0;
		    char *path_C = idio_string_as_C (psj_path, &size);

		    /*
		     * Use size + 1 to avoid a truncation warning --
		     * we're just seeing if path_C includes a NUL
		     */
		    size_t C_size = idio_strnlen (path_C, size + 1);
		    if (C_size != size) {
			fprintf (stderr, "ERROR: named-pipe: path contains an ASCII NUL: %s\n", path_C);
		    } else {
			if (unlink (path_C) < 0) {
			    char em[81];
			    snprintf (em, 80, "unlink (%s)", path_C);
			    em[80] = '\0';

			    perror (em);
			} else {
			    IDIO psj_dir = idio_struct_instance_ref_direct (psj, IDIO_PSJ_DIR);
			    size = 0;
			    char *dir_C = idio_string_as_C (psj_dir, &size);

			    /*
			     * Use size + 1 to avoid a truncation
			     * warning -- we're just seeing if dir_C
			     * includes a NUL
			     */
			    size_t C_size = idio_strnlen (dir_C, size);
			    if (C_size != size) {
				fprintf (stderr, "ERROR: named-pipe: dir: contains an ASCII NUL: %s\n", dir_C);
			    } else {
				if (rmdir (dir_C) < 0) {
				    char em[81];
				    snprintf (em, 80, "rmdir (%s)", dir_C);
				    em[80] = '\0';

				    perror (em);
				}
			    }

			    IDIO_GC_FREE (dir_C, size);
			}
		    }

		    IDIO_GC_FREE (path_C, size);
		}
	    }
	}

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

void idio_job_control_restore_terminal ()
{
    if (idio_job_control_interactive) {
	struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (idio_job_control_tcattrs);
	if (tcsetattr (idio_job_control_tty_fd, TCSADRAIN, tcattrsp) < 0) {
	    /*
	     * If the interactive user has typed ^D then read(2) gets EOL
	     * and closes the file descriptor.
	     *
	     * If we're running from a tty then calling tcsetattr(0, ...)
	     * as we shutdown *after* that gets EBADF.
	     */
	    if (! (IDIO_STATE_SHUTDOWN == idio_state &&
		   EBADF == errno)) {
		idio_error_system_errno ("tcsetattr", idio_C_int (idio_job_control_tty_fd), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
    }
}

static IDIO idio_job_control_foreground_job (IDIO job, int cont)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    pid_t job_pgid = IDIO_C_TYPE_libc_pid_t (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PGID));

    if (idio_job_control_interactive) {
	/*
	 * Put the job in the foreground
	 */
	if (tcsetpgrp (idio_job_control_tty_fd, job_pgid) < 0) {
	    idio_error_system_errno ("tcsetpgrp",
				     IDIO_LIST3 (idio_C_int (idio_job_control_tty_fd),
						 idio_libc_pid_t (job_pgid),
						 job),
				     IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (cont) {
	if (idio_job_control_interactive) {
	    IDIO job_tcattrs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_TCATTRS);
	    IDIO_TYPE_ASSERT (C_pointer, job_tcattrs);
	    struct termios *tcattrsp = IDIO_C_TYPE_POINTER_P (job_tcattrs);

	    if (tcsetattr (idio_job_control_tty_fd, TCSADRAIN, tcattrsp) < 0) {
		idio_error_system_errno ("tcsetattr", idio_C_int (idio_job_control_tty_fd), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	if (killpg (job_pgid, SIGCONT) < 0) {
	    idio_error_system_errno_msg ("kill", "SIGCONT", idio_libc_pid_t (-job_pgid), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO r = idio_vm_invoke_C (idio_thread_current_thread (),
			       IDIO_LIST2 (idio_module_symbol_value (idio_S_wait_for_job,
								     idio_job_control_module,
								     idio_S_nil),
					   job));

    if (idio_job_control_interactive) {
	/*
	 * Put the shell back in the foreground.
	 */
	if (tcsetpgrp (idio_job_control_tty_fd, idio_job_control_pgid) < 0) {
	    idio_error_system_errno ("tcsetpgrp",
				     IDIO_LIST3 (idio_C_int (idio_job_control_tty_fd),
						 idio_libc_pid_t (idio_job_control_pgid),
						 job),
				     IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * Save the job's current terminal state -- creating a struct
	 * termios if necessary
	 */
	IDIO job_tcattrs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_TCATTRS);
	struct termios *tcattrsp = NULL;
	if (idio_S_nil == job_tcattrs) {
	    tcattrsp = idio_alloc (sizeof (struct termios));
	    job_tcattrs = idio_C_pointer_free_me (tcattrsp);
	    idio_struct_instance_set_direct (job, IDIO_JOB_ST_TCATTRS, job_tcattrs);
	}

	if (tcgetattr (idio_job_control_tty_fd, tcattrsp) < 0) {
	    idio_error_system_errno ("tcgetattr", idio_C_int (idio_job_control_tty_fd), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * Restore the shell's terminal state
	 */
	idio_job_control_restore_terminal ();
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("foreground-job", foreground_job, (IDIO job, IDIO icont), "job cont", "\
place job `job` in the foreground\n\
						\n\
:param job: job					\n\
:type job: struct-instance			\n\
:param cont: boolean				\n\
:type cont: boolean				\n\
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
	int job_pgid = IDIO_C_TYPE_libc_pid_t (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PGID));

	if (job_pgid > 0) {
	    if (killpg (job_pgid, SIGCONT) < 0) {
		idio_error_system_errno_msg ("kill", "SIGCONT", idio_libc_pid_t (-job_pgid), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    fprintf (stderr, "SIGCONT -> pgid %d ??\n", job_pgid);
	    idio_debug ("job %s\n", job);
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
:type job: struct-instance			\n\
:param cont: boolean				\n\
:type cont: boolean				\n\
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

    idio_debug ("ijc-HUP: %s\n", job);
    idio_job_control_format_job_info (job, "SIGHUP'ed");

    IDIO ipgid = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PGID);
    pid_t job_pgid = -1;
    if (idio_isa_libc_pid_t (ipgid)) {
	job_pgid = IDIO_C_TYPE_libc_pid_t (ipgid);
    } else {
	idio_error_param_type ("libc/pid_t", ipgid, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (job_pgid > 0) {
	if (killpg (job_pgid, SIGCONT) < 0) {
	    if (ESRCH != errno) {
		idio_error_system_errno_msg ("kill", "SIGCONT", idio_libc_pid_t (-job_pgid), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}

	if (killpg (job_pgid, SIGHUP) < 0) {
	    if (ESRCH != errno) {
		idio_error_system_errno_msg ("kill", "SIGHUP", idio_libc_pid_t (-job_pgid), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
    } else {
	fprintf (stderr, "SIGHUP -> pgid %d ??\n", job_pgid);
	idio_debug ("job %s\n", job);
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("hangup-job", hangup_job, (IDIO job), "job", "\
hangup job `job`				\n\
						\n\
:param job: job					\n\
:type job: struct-instance			\n\
:return: ``#<unspec>``				\n\
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
	if (idio_job_control_interactive) {
	    fprintf (stderr, "HUP: outstanding jobs: ");
	    idio_debug ("%s\n", jobs);
	}

	/*
	 * NB
	 *
	 * Take a copy of the jobs list as the list may be perturbed
	 * by jobs finishing (naturally or by our hand, here).
	 *
	 * Under the highly transient error conditions that we get
	 * here I've found the processes have gone away even as I walk
	 * the (copied) list.
	 *
	 * YMMV
	 */
	jobs = idio_copy (jobs, IDIO_COPY_SHALLOW);
	while (idio_S_nil != jobs) {
	    IDIO job = IDIO_PAIR_H (jobs);
	    idio_job_control_hangup_job (job);
	    jobs = IDIO_PAIR_T (jobs);
	}
    }

    return idio_S_unspec;
}

IDIO idio_job_control_SIGTERM_stopped_jobs ()
{
    IDIO jobs = idio_module_symbol_value (idio_job_control_jobs_sym, idio_job_control_module, idio_S_nil);
    if (idio_S_nil != jobs) {
	if (idio_job_control_interactive) {
	    fprintf (stderr, "%6d: ijc SIGTERM: outstanding jobs: ", getpid());
	    idio_debug ("%s\n", jobs);
	}

	/*
	 * NB
	 *
	 * Take a copy of the jobs list as the list may be perturbed
	 * by jobs finishing (naturally or by our hand, here).
	 *
	 * Under the highly transient error conditions that we get
	 * here I've found the processes have gone away even as I walk
	 * the (copied) list.
	 *
	 * YMMV
	 */
	jobs = idio_copy (jobs, IDIO_COPY_SHALLOW);

	/*
	 * In the time it takes us to shutdown jobs we may get
	 * ^rt-async-command-status-errors
	 */
	idio_module_set_symbol_value (idio_vars_suppress_async_command_report_sym, idio_S_true, idio_Idio_module);

	while (idio_S_nil != jobs) {
	    IDIO job = IDIO_PAIR_H (jobs);
	    if (idio_job_control_job_is_stopped (job) ||
		idio_job_control_job_is_async (job)) {
		pid_t job_pgid = IDIO_C_TYPE_libc_pid_t (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PGID));

		if (job_pgid > 0) {
#ifdef IDIO_DEBUG
		    if (idio_job_control_interactive) {
			fprintf (stderr, "%6d: ijc SIGTERM -> pgid %d\n", getpid (), job_pgid);
			idio_debug ("job %s\n", job);
		    }
#endif
		    /*
		     * Following in the style of Bash's
		     * terminate_stopped_jobs(), issue the SIGTERM before
		     * the SIGCONT.
		     *
		     * Ignore errors, we're shutting down.
		     */
		    killpg (job_pgid, SIGTERM);
		    killpg (job_pgid, SIGCONT);
		} else {
		    /*
		     * Hmm.  The PGID is 0 but all all the cases I've
		     * seen have the job being neither stopped nor
		     * async.  Always completed.  Transient timing
		     * issue?
		     */
#ifdef IDIO_DEBUG
		    if (idio_job_control_interactive) {
			fprintf (stderr, "%6d: ijc SIGTERM -> pgid %d ??\n", getpid(), job_pgid);
			idio_debug ("job %s\n", job);
		    }
#endif
		}
	    }
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
			       idio_module_symbol_value (idio_job_control_djn_sym,
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

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
    while (idio_S_nil != procs) {
	IDIO proc = IDIO_PAIR_H (procs);

	if (! idio_struct_instance_isa (proc, idio_job_control_process_type)) {
	    idio_error_param_type ("%idio-process", proc, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	idio_struct_instance_set_direct (proc, IDIO_PROCESS_ST_STOPPED, idio_S_false);

	procs = IDIO_PAIR_T (procs);
    }

    idio_struct_instance_set_direct (job, IDIO_JOB_ST_NOTIFY_STOPPED, idio_S_false);
}

IDIO_DEFINE_PRIMITIVE1_DS ("mark-job-as-running", mark_job_as_running, (IDIO job), "job", "\
mark job `job` as running			\n\
						\n\
:param job: job					\n\
:type job: struct-instance			\n\
:return: ``#<unspec>``				\n\
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
:type job: struct-instance			\n\
:param foreground: boolean			\n\
:type foreground: boolean			\n\
:return: ``#<unspec>``				\n\
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
    /*
     * Use the supplied stdin/stdout/stderr
     */
    if (infile != STDIN_FILENO) {
	if (dup2 (infile, STDIN_FILENO) < 0) {
	    idio_error_system_errno ("dup2",
				     IDIO_LIST2 (idio_C_int (infile),
						 idio_C_int (STDIN_FILENO)),
				     IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	if (infile > STDERR_FILENO  &&
	    (infile != outfile &&
	     infile != errfile)) {
	    if (close (infile) < 0) {
		idio_error_system_errno ("close", IDIO_LIST1 (idio_C_int (infile)), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
    }

    if (outfile != STDOUT_FILENO) {
	if (dup2 (outfile, STDOUT_FILENO) < 0) {
	    idio_error_system_errno ("dup2",
				     IDIO_LIST2 (idio_C_int (outfile),
						 idio_C_int (STDOUT_FILENO)),
				     IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	if (outfile > STDERR_FILENO &&
	    outfile != errfile) {
	    if (close (outfile) < 0) {
		idio_error_system_errno ("close", IDIO_LIST1 (idio_C_int (outfile)), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
    }

    if (errfile != STDERR_FILENO) {
	if (dup2 (errfile, STDERR_FILENO) < 0) {
	    idio_error_system_errno ("dup2",
				     IDIO_LIST2 (idio_C_int (errfile),
						 idio_C_int (STDERR_FILENO)),
				     IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}

	if (errfile > STDERR_FILENO) {
	    if (close (errfile) < 0) {
		idio_error_system_errno ("close", IDIO_LIST1 (idio_C_int (errfile)), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}
    }
}

static void idio_job_control_prep_process (pid_t job_pgid, int infile, int outfile, int errfile, int foreground, int async)
{
    pid_t pid;

    if (idio_job_control_interactive ||
	async) {
	pid = getpid ();
	if (0 == job_pgid) {
	    job_pgid = pid;
	}

	/*
	 * Put the process in the process group.  Dupe of parent to
	 * avoid race conditions.
	 */
	if (setpgid (pid, job_pgid) < 0) {
	    idio_error_system_errno ("setpgid",
				     IDIO_LIST2 (idio_libc_pid_t (pid),
						 idio_libc_pid_t (job_pgid)),
				     IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    if (idio_job_control_interactive) {
	if (foreground) {
	    /*
	     * Give the terminal to the process group.  Dupe of parent
	     * to avoid race conditions.
	     */
	    if (tcsetpgrp (idio_job_control_tty_fd, job_pgid) < 0) {
		idio_error_system_errno ("ijc-pp tcsetpgrp",
					 IDIO_LIST2 (idio_C_int (idio_job_control_tty_fd),
						     idio_libc_pid_t (job_pgid)),
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

IDIO_DEFINE_PRIMITIVE5_DS ("%prep-process", prep_process, (IDIO pgid, IDIO infile, IDIO outfile, IDIO errfile, IDIO foreground, IDIO async), "pgid infile outfile errfile foreground async", "\
prepare the current process			\n\
						\n\
:param pgid: process group id			\n\
:param infile: file descriptor for stdin	\n\
:param outfile: file descriptor for stdout	\n\
:param errfile: file descriptor for stderr	\n\
:param foreground: boolean			\n\
:param async: boolean				\n\
:return: ``#<unspec>``				\n\
						\n\
Place the current process in `pgid` and dup() stdin, stdout and stderr.\n\
Place the current process in the foreground if requested.\n\
Mark the job as asynchronous if requested.	\n\
						\n\
File descriptors are C integers.		\n\
")
{
    IDIO_ASSERT (pgid);
    IDIO_ASSERT (infile);
    IDIO_ASSERT (outfile);
    IDIO_ASSERT (errfile);
    IDIO_ASSERT (foreground);

    IDIO_USER_C_TYPE_ASSERT (int, infile);
    IDIO_USER_C_TYPE_ASSERT (int, outfile);
    IDIO_USER_C_TYPE_ASSERT (int, errfile);
    IDIO_USER_TYPE_ASSERT (boolean, foreground);

    pid_t C_pgid = 0;
    if (idio_isa_libc_pid_t (pgid)) {
	C_pgid = IDIO_C_TYPE_libc_pid_t (pgid);
    } else {
	idio_error_param_type ("libc/pid_t", pgid, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int C_infile = IDIO_C_TYPE_int (infile);
    int C_outfile = IDIO_C_TYPE_int (outfile);
    int C_errfile = IDIO_C_TYPE_int (errfile);

    int C_foreground = 0;

    if (idio_S_true == foreground) {
	C_foreground = 1;
    }

    int C_async = 0;

    if (idio_S_true == async) {
	C_async = 1;
    }

    idio_job_control_prep_process (C_pgid, C_infile, C_outfile, C_errfile, C_foreground, C_async);

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

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
    pid_t job_pgid = IDIO_C_TYPE_libc_pid_t (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PGID));
    int job_stdin = IDIO_C_TYPE_int (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_STDIN));
    int job_stdout = IDIO_C_TYPE_int (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_STDOUT));
    int job_stderr = IDIO_C_TYPE_int (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_STDERR));
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
	    outfile = IDIO_C_TYPE_int (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_STDOUT));
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
					   foreground,
					   0);
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
	    idio_struct_instance_set_direct (proc, IDIO_PROCESS_ST_PID, idio_libc_pid_t (pid));
	    if (idio_job_control_interactive) {
		if (0 == job_pgid) {
		    job_pgid = pid;
		    idio_struct_instance_set_direct (job, IDIO_JOB_ST_PGID, idio_libc_pid_t (job_pgid));
		}
		if (setpgid (pid, job_pgid) < 0) {
		    idio_error_system_errno ("setpgid",
					     IDIO_LIST4 (idio_libc_pid_t (pid),
							 idio_libc_pid_t (job_pgid),
							 proc,
							 job),
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

    /*
    if (! idio_job_control_interactive) {
	idio_job_control_wait_for_job (job);
    } else
    */
    if (foreground) {
	idio_job_control_foreground_job (job, 0);
    } else {
	idio_job_control_background_job (job, 0);
    }
}

IDIO idio_job_control_launch_1proc_job (IDIO job, int foreground, char const *pathname, char **argv, IDIO args)
{
    IDIO_ASSERT (job);
    IDIO_TYPE_ASSERT (struct_instance, job);

    if (! idio_struct_instance_isa (job, idio_job_control_job_type)) {
	idio_error_param_type ("%idio-job", job, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO procs = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PROCS);
    IDIO proc = IDIO_PAIR_H (procs);

    if (! idio_struct_instance_isa (proc, idio_job_control_process_type)) {
	idio_error_param_type ("%idio-process", proc, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    pid_t job_pgid = IDIO_C_TYPE_libc_pid_t (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_PGID));
    int job_stdin = IDIO_C_TYPE_int (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_STDIN));
    int job_stdout = IDIO_C_TYPE_int (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_STDOUT));
    int job_stderr = IDIO_C_TYPE_int (idio_struct_instance_ref_direct (job, IDIO_JOB_ST_STDERR));
    IDIO job_async = idio_struct_instance_ref_direct (job, IDIO_JOB_ST_ASYNC);

    size_t envp_size = 0;
    char **envp = idio_command_get_envp (&envp_size);

    /*
     * We're here because the VM saw a symbol in functional position
     * -- which we have found is an external job_control on PATH -- but we
     * don't know whether we're in a pipeline or the job_control was
     * inline.
     *
     * If we're in a pipeline then our pid will be different to the
     * original Idio's pid.
     */

    if (idio_S_false == job_async) {
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
	    idio_command_free_argv1 (argv);
	    IDIO_GC_FREE (envp, envp_size);

	    idio_error_system_errno ("pipe", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	pid_t pid = fork ();
	if (pid < 0) {
	    idio_command_free_argv1 (argv);
	    IDIO_GC_FREE (envp, envp_size);

	    /*
	     * was idio_alloc()'ed no no stat decrement
	     */

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
					   foreground,
					   0);

	    if (close (pgrp_pipe[1]) < 0) {
		idio_command_free_argv1 (argv);
		IDIO_GC_FREE (envp, envp_size);

		idio_error_system_errno ("close", idio_fixnum (pgrp_pipe[1]), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    /*
	     * Block reading the pgrp_pipe
	     */
	    char buf[1];
	    read (pgrp_pipe[0], buf, 1);

	    if (close (pgrp_pipe[0]) < 0) {
		idio_command_free_argv1 (argv);
		IDIO_GC_FREE (envp, envp_size);

		idio_error_system_errno ("close", idio_fixnum (pgrp_pipe[0]), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    execve (pathname, argv, envp);
	    perror ("execv");
	    char **av;
	    fprintf (stderr, "exec: [%s] ", pathname);
	    for (av = argv; *av; av++) {
		fprintf (stderr, "%s ", *av);
	    }
	    fprintf (stderr, "\n");
	    idio_job_control_error_exec (argv, envp, IDIO_C_FUNC_LOCATION ());

	    exit (33);
	    return idio_S_notreached;
	} else {
	    IDIO_GC_FREE (envp, envp_size);

	    idio_struct_instance_set_direct (proc, IDIO_PROCESS_ST_PID, idio_libc_pid_t (pid));
	    if (idio_job_control_interactive) {
		if (0 == job_pgid) {
		    job_pgid = pid;
		    idio_struct_instance_set_direct (job, IDIO_JOB_ST_PGID, idio_libc_pid_t (job_pgid));
		}
		if (setpgid (pid, job_pgid) < 0) {
		    /*
		     * Duplicate check as per c/setpgid in libc-wrap.c
		     */
		    if (EACCES != errno) {
			idio_error_system_errno ("setpgid",
						 IDIO_LIST4 (idio_libc_pid_t (pid),
							     idio_libc_pid_t (job_pgid),
							     proc,
							     job),
						 IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }
		}
	    }

	    /*
	     * synchronise!
	     */
	    if (close (pgrp_pipe[0]) < 0) {
		idio_error_system_errno ("close", idio_fixnum (pgrp_pipe[0]), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    if (close (pgrp_pipe[1]) < 0) {
		idio_error_system_errno ("close", idio_fixnum (pgrp_pipe[1]), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    char **av = argv + 1;
	    IDIO as = args;
	    while (idio_S_nil != as) {
		IDIO arg = IDIO_PAIR_H (as);
		if (idio_isa_fd_pathname (arg)) {
		    /*
		     * "/dev/fd/" is 8 chars and the following number
		     * is in base 10
		     */
		    IDIO fd = idio_fixnum_C (*av + 8, 10);
		    if (close (IDIO_FIXNUM_VAL (fd)) == -1) {
			if (EBADF != errno) {
			    perror ("close named-pipe FD");
			}
		    }
		}
		as = IDIO_PAIR_T (as);
		av++;
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
	    /*
	    if (! idio_job_control_interactive) {
		IDIO wfj = idio_module_symbol_value (idio_S_wait_for_job, idio_job_control_module, idio_S_nil);
		cmd = IDIO_LIST2 (wfj, job);
	    } else
	    */
	    if (foreground) {
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
	    IDIO r = idio_vm_invoke_C (idio_thread_current_thread (), cmd);

	    return r;
	}
    } else {
	/*
	 * In a pipeline, just exec -- the %prep-process has been done
	 */
	idio_job_control_prep_io (job_stdin,
				  job_stdout,
				  job_stderr);

	execve (pathname, argv, envp);
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
    idio_coding_error_C ("post-launch: cannot be here", idio_S_nil, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%launch-job", launch_job, (IDIO job), "job", "\
launch job `job`				\n\
						\n\
:param job: job					\n\
:type job: struct-instance			\n\
:return: ``#<unspec>``				\n\
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
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (job_controls);

    IDIO_USER_TYPE_ASSERT (list, job_controls);

    idio_debug ("%launch-pipeline: %s\n", job_controls);

    IDIO procs = idio_S_nil;

    IDIO cmds = job_controls;
    while (idio_S_nil != cmds) {
	IDIO proc = idio_struct_instance (idio_job_control_process_type,
					  idio_pair (IDIO_PAIR_H (cmds),
						     IDIO_LIST5 (idio_S_nil,
								 idio_libc_pid_t (-1),
								 idio_S_false,
								 idio_S_false,
								 idio_S_nil)));

	procs = idio_pair (proc, procs);

	cmds = IDIO_PAIR_T (cmds);
    }

    procs = idio_list_reverse (procs);

    IDIO job_stdin = idio_C_int (STDIN_FILENO);
    IDIO job_stdout = idio_C_int (STDOUT_FILENO);
    IDIO job_stderr = idio_C_int (STDERR_FILENO);

    /*
     * some job accounting
     */
    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    if (-1 == gettimeofday (tvp, NULL)) {
	idio_error_system_errno ("gettimeofday", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    struct rusage *rusage_selfp = (struct rusage *) idio_alloc (sizeof (struct rusage));

    if (-1 == getrusage (RUSAGE_SELF, rusage_selfp)) {
	idio_error_system_errno ("getrusage", idio_C_int (RUSAGE_SELF), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    struct rusage *rusage_childrenp = (struct rusage *) idio_alloc (sizeof (struct rusage));

    if (-1 == getrusage (RUSAGE_CHILDREN, rusage_childrenp)) {
	idio_error_system_errno ("getrusage", idio_C_int (RUSAGE_CHILDREN), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO timing_start = IDIO_LIST3 (idio_C_pointer_type (idio_CSI_libc_struct_timeval, tvp),
				    idio_C_pointer_type (idio_CSI_libc_struct_rusage, rusage_selfp),
				    idio_C_pointer_type (idio_CSI_libc_struct_rusage, rusage_childrenp));

    IDIO job = idio_struct_instance (idio_job_control_job_type,
				     idio_pair (job_controls,
				     idio_pair (procs,
				     idio_pair (idio_libc_pid_t (0),
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_nil,
				     idio_pair (job_stdin,
				     idio_pair (job_stdout,
				     idio_pair (job_stderr,
				     idio_pair (idio_S_false,
				     idio_pair (timing_start,
				     idio_pair (idio_S_false,
				     idio_pair (idio_S_false,
				     idio_S_nil))))))))))))))));

    idio_job_control_launch_job (job, 1);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0_DS ("%interactive?", interactivep, (void), "", "\
get the current interactiveness			\n\
						\n\
:return: ``#t`` or ``#f``			\n\
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
	while (tcgetpgrp (idio_job_control_tty_fd) != (idio_job_control_pgid = getpgrp ())) {
	    fprintf (stderr, "%2d: tcgetpgrp(%d)=%d getpgrp()=%d\n", c, tcgetpgrp (idio_job_control_tty_fd), idio_job_control_tty_fd, getpgrp ());
	    c++;
	    if (c > 2) {
		exit (128 + 15);
	    }
	    if (killpg (idio_job_control_pgid, SIGTTIN) < 0) {
		idio_error_system_errno_msg ("kill", "SIGTTIN", idio_libc_pid_t (-idio_job_control_pgid), IDIO_C_FUNC_LOCATION ());

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
		idio_error_system_errno ("setpgid", idio_libc_pid_t (idio_job_control_pgid), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }
	}

	idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("%idio-pgid"),
				      idio_libc_pid_t (idio_job_control_pgid),
				      idio_job_control_module);

	/*
	 * Grab control of the terminal.
	 */
	if (tcsetpgrp (idio_job_control_tty_fd, idio_job_control_pgid) < 0) {
	    idio_error_system_errno ("tcsetpgrp",
				     IDIO_LIST2 (idio_C_int (idio_job_control_tty_fd),
						 idio_libc_pid_t (idio_job_control_pgid)),
				     IDIO_C_FUNC_LOCATION ());

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
    idio_job_control_restore_terminal ();

    /*
     * Be a good citizen and tidy up.  This will report completed
     * jobs, though.  Maybe we should suppress the reports.
     */
    idio_job_control_set_interactive (0);

    /*
     * This deliberately uses the C versions of these functions as
     * other modules have been shutting down -- we don't want to be
     * running any more Idio code here!
     */
    idio_job_control_do_job_notification ();

    idio_job_control_SIGTERM_stopped_jobs ();
}

void idio_init_job_control ()
{
    idio_module_table_register (idio_job_control_add_primitives, idio_final_job_control, NULL);

    idio_job_control_module = idio_module (IDIO_SYMBOLS_C_INTERN ("job-control"));

    idio_S_background_job = IDIO_SYMBOLS_C_INTERN ("background-job");
    idio_S_exit = IDIO_SYMBOLS_C_INTERN ("exit");
    idio_S_foreground_job = IDIO_SYMBOLS_C_INTERN ("foreground-job");
    idio_S_killed = IDIO_SYMBOLS_C_INTERN ("killed");
    idio_S_wait_for_job = IDIO_SYMBOLS_C_INTERN ("wait-for-job");
    idio_S_stdin_fileno = IDIO_SYMBOLS_C_INTERN ("stdin-fileno");
    idio_S_stdout_fileno = IDIO_SYMBOLS_C_INTERN ("stdout-fileno");
    idio_S_stderr_fileno = IDIO_SYMBOLS_C_INTERN ("stderr-fileno");

    int signum;
    for (signum = IDIO_LIBC_FSIG; signum <= IDIO_LIBC_NSIG; signum++) {
	idio_vm_signal_record[signum] = 0;
    }

    struct sigaction nsa, osa;
    nsa.sa_handler = idio_vm_sa_signal;
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
    idio_job_control_tty_fd = STDIN_FILENO;
    idio_job_control_tty_isatty = isatty (idio_job_control_tty_fd);

    IDIO sym;
    sym = IDIO_SYMBOLS_C_INTERN ("%idio-terminal");
    idio_module_set_symbol_value (sym,
				  idio_C_int (idio_job_control_tty_fd),
				  idio_job_control_module);
    IDIO v = idio_module_symbol_value (sym,
				       idio_job_control_module,
				       idio_S_nil);
    IDIO_FLAGS (v) |= IDIO_FLAG_CONST;

    struct termios *tcattrsp = idio_alloc (sizeof (struct termios));
    idio_job_control_tcattrs = idio_C_pointer_free_me (tcattrsp);

    /*
     * The info pages only set shell_attrs when the shell is
     * interactive.
     */
    if (idio_job_control_tty_isatty) {
	if (tcgetattr (idio_job_control_tty_fd, tcattrsp) < 0) {
	    idio_error_system_errno ("tcgetattr", idio_C_int (idio_job_control_tty_fd), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	}
    }

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("%idio-tcattrs"),
				  idio_job_control_tcattrs,
				  idio_job_control_module);

    idio_job_control_cmd_pid = idio_job_control_pid;

    /*
     * The Idio-visible %idio-interactive should be read-only.
     * However, we actually play some tricks with it like disabling
     * during {load} so we don't get plagued with job failure
     * messages.  So it should be a (read-only) computed variable.
     */
    IDIO geti;
    geti = IDIO_ADD_PRIMITIVE (interactivep);
    idio_module_add_computed_symbol (IDIO_SYMBOLS_C_INTERN ("%idio-interactive"),
				     idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)),
				     idio_S_nil,
				     idio_job_control_module);

    /*
     * Not noted in the Job Control docs is that if we are launched
     * non-interactively then we never set
     * idio_job_control_pgid/%idio-pgid with a later complaint about a
     * symbol not being a C/int when the variable is accessed in
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
    sym = IDIO_SYMBOLS_C_INTERN ("%idio-pgid");
    idio_module_set_symbol_value (sym,
				  idio_libc_pid_t (idio_job_control_pgid),
				  idio_job_control_module);
    v = idio_module_symbol_value (sym,
				  idio_job_control_module,
				  idio_S_nil);
    IDIO_FLAGS (v) |= IDIO_FLAG_CONST;

    idio_job_control_jobs_sym = IDIO_SYMBOLS_C_INTERN ("%idio-jobs");
    idio_module_set_symbol_value (idio_job_control_jobs_sym, idio_S_nil, idio_job_control_module);
    idio_job_control_last_job = IDIO_SYMBOLS_C_INTERN ("%%last-job");
    idio_module_set_symbol_value (idio_job_control_last_job, idio_S_nil, idio_job_control_module);

    /*
     * Job Control is not the only mechanism that will fork&exec child
     * processes but Job Control (through waitpid) *is* the only
     * handler for SIGCHLD events.
     *
     * So we need a mechanism to handle these other known processes.
     */
    idio_job_control_known_pids_sym = IDIO_SYMBOLS_C_INTERN ("%idio-known-pids");
    idio_module_set_symbol_value (idio_job_control_known_pids_sym, IDIO_HASH_EQP (4), idio_job_control_module);

    idio_job_control_stray_pids_sym = IDIO_SYMBOLS_C_INTERN ("%idio-stray-pids");
    idio_module_set_symbol_value (idio_job_control_stray_pids_sym, IDIO_HASH_EQP (4), idio_job_control_module);

    sym = IDIO_SYMBOLS_C_INTERN ("%idio-process");
    idio_job_control_process_type = idio_struct_type (sym,
						      idio_S_nil,
						      idio_pair (IDIO_SYMBOLS_C_INTERN ("argv"),
						      idio_pair (IDIO_SYMBOLS_C_INTERN ("exec"),
						      idio_pair (IDIO_SYMBOLS_C_INTERN ("pid"),
						      idio_pair (IDIO_SYMBOLS_C_INTERN ("completed"),
						      idio_pair (IDIO_SYMBOLS_C_INTERN ("stopped"),
						      idio_pair (IDIO_SYMBOLS_C_INTERN ("status"),
						      idio_S_nil)))))));
    idio_module_set_symbol_value (sym, idio_job_control_process_type, idio_job_control_module);

    sym = IDIO_SYMBOLS_C_INTERN ("%idio-job");
    idio_job_control_job_type = idio_struct_type (sym,
						  idio_S_nil,
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("pipeline"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("procs"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("pgid"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("notify-stopped"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("notify-completed"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("raise?"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("raised"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("tcattrs"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("stdin"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("stdout"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("stderr"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("report-timing"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("timing-start"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("timing-end"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("async"),
						  idio_S_nil))))))))))))))));
    idio_module_set_symbol_value (sym, idio_job_control_job_type, idio_job_control_module);

    idio_job_control_default_child_handler_sym = IDIO_SYMBOLS_C_INTERN ("default-child-handler");
    idio_job_control_djn_sym = IDIO_SYMBOLS_C_INTERN ("do-job-notification");
}

