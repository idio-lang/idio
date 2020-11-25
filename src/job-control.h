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
 * job_control.h
 *
 */

#ifndef JOB_CONTROL_H
#define JOB_CONTROL_H

extern IDIO idio_job_control_module;
extern int idio_job_control_interactive;
extern IDIO idio_job_control_process_type;
extern IDIO idio_job_control_job_type;
extern IDIO idio_S_stdin_fileno;
extern IDIO idio_S_stdout_fileno;
extern IDIO idio_S_stderr_fileno;

IDIO idio_job_control_SIGHUP_signal_handler ();
IDIO idio_job_control_SIGCHLD_signal_handler ();
IDIO idio_job_control_rcse_handler (IDIO c);
void idio_job_control_set_interactive (void);
IDIO idio_job_control_launch_1proc_job (IDIO job, int foreground, char **argv);

extern volatile sig_atomic_t idio_job_control_signal_record[IDIO_LIBC_NSIG+1];

void idio_init_job_control ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
