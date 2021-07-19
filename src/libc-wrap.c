/*
 * Copyright (c) 2015, 2017, 2018, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * libc-wrap.c
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
#include <fcntl.h>
#include <ffi.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
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
#include "vm.h"

#include "libc-api.h"

IDIO idio_libc_module = idio_S_nil;
IDIO idio_vm_signal_handler_conditions;
char **idio_libc_signal_names = NULL;
IDIO idio_vm_errno_conditions;
char **idio_libc_errno_names = NULL;
char **idio_libc_rlimit_names = NULL;

static long idio_SC_CLK_TCK = 0;

void idio_libc_format_error (char *msg, IDIO name, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_libc_format_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       name));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

IDIO idio_libc_export_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_export_symbol_value (symbol, value, idio_libc_module);
}

char *idio_libc_string_C (IDIO val, char *func_C, int *free_me_p, IDIO c_location)
{
    IDIO_ASSERT (val);
    IDIO_C_ASSERT (func_C);
    IDIO_C_ASSERT (free_me_p);
    IDIO_ASSERT (c_location);

    *free_me_p = 0;

    if (idio_isa_symbol (val)) {
	return IDIO_SYMBOL_S (val);
    } else if (idio_isa_string (val)) {
	size_t size = 0;
	char *val_C = idio_string_as_C (val, &size);
	size_t C_size = strlen (val_C);
	if (C_size != size) {
	    IDIO_GC_FREE (val_C);

	    char em[BUFSIZ];
	    sprintf (em, "%s: contains an ASCII NUL", func_C);
	    idio_libc_format_error (em, val, c_location);

	    /* notreached */
	    return NULL;
	}
	*free_me_p = 1;

	return val_C;
    } else {
	/*
	 * Code coverage: coding error
	 */
	idio_error_param_type ("symbol|string", val, c_location);

	/* notreached */
	return NULL;
    }
}

/*
 * Code coverage:
 *
 * Added as a system equivalent of error.  I think.
 *
 * Not used.
 */
IDIO_DEFINE_PRIMITIVE0V_DS ("system-error", libc_system_error, (IDIO args), "[name [args]]", "\
raise a ^system-error						\n\
								\n\
:param name: error name						\n\
:type name: string or symbol					\n\
:param args: error args						\n\
:type args: list						\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    char *name = "n/k";

    if (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	if (idio_isa_string (h)) {
	    size_t size = 0;
	    name = idio_string_as_C (h, &size);
	    args = IDIO_PAIR_T (args);
	} else if (idio_isa_symbol (h)) {
	    name = IDIO_SYMBOL_S (h);
	    args = IDIO_PAIR_T (args);
	}
    }

    idio_error_system_errno (name, args, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO idio_libc_struct_timeval_pointer (struct timeval *tvp)
{
    IDIO_C_ASSERT (tvp);

    IDIO r = idio_C_pointer_type (idio_CSI_libc_struct_timeval, tvp);

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("add-struct-timeval", libc_add_struct_timeval, (IDIO tv1, IDIO tv2), "tv1 tv2", "\
A simple function to calculate the sum of two C struct timevals \n\
								\n\
tv1 + tv2							\n\
								\n\
:param tv1: first timeval					\n\
:type tv1: C/pointer						\n\
:param tv2: second timeval					\n\
:type tv2: C/pointer						\n\
:return: C struct timeval or raises ^system-error		\n\
:rtype: C/pointer						\n\
")
{
    IDIO_ASSERT (tv1);
    IDIO_ASSERT (tv2);

    /*
     * Test Case: libc-wrap-errors/add-struct-timeval-bad-first-type.idio
     *
     * add-struct-timeval #t #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, tv1);
    /*
     * Test Case: libc-wrap-errors/add-struct-timeval-bad-second-type.idio
     *
     * add-struct-timeval (gettimeofday) #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, tv2);

    struct timeval *tv1p = IDIO_C_TYPE_POINTER_P (tv1);
    struct timeval *tv2p = IDIO_C_TYPE_POINTER_P (tv2);

    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    tvp->tv_sec = tv1p->tv_sec + tv2p->tv_sec;
    tvp->tv_usec = tv1p->tv_usec + tv2p->tv_usec;

    if (tvp->tv_usec > 1000000) {
	tvp->tv_usec -= 1000000;
	tvp->tv_sec += 1;
    }

    return idio_libc_struct_timeval_pointer (tvp);
}

IDIO_DEFINE_PRIMITIVE2_DS ("subtract-struct-timeval", libc_subtract_struct_timeval, (IDIO tv1, IDIO tv2), "tv1 tv2", "\
A simple function to calculate the difference between two C	\n\
struct timevals							\n\
								\n\
tv1 - tv2							\n\
								\n\
:param tv1: first timeval					\n\
:type tv1: C/pointer						\n\
:param tv2: second timeval					\n\
:type tv2: C/pointer						\n\
:return: C struct timeval or raises ^system-error		\n\
:rtype: C/pointer						\n\
")
{
    IDIO_ASSERT (tv1);
    IDIO_ASSERT (tv2);

    /*
     * Test Case: libc-wrap-errors/subtract-struct-timeval-bad-first-type.idio
     *
     * subtract-struct-timeval #t #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, tv1);
    /*
     * Test Case: libc-wrap-errors/subtract-struct-timeval-bad-second-type.idio
     *
     * subtract-struct-timeval (gettimeofday) #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, tv2);

    struct timeval *tv1p = IDIO_C_TYPE_POINTER_P (tv1);
    struct timeval *tv2p = IDIO_C_TYPE_POINTER_P (tv2);

    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    tvp->tv_sec = tv1p->tv_sec - tv2p->tv_sec;
    tvp->tv_usec = tv1p->tv_usec - tv2p->tv_usec;

    if (tvp->tv_usec < 0) {
	tvp->tv_usec += 1000000;
	tvp->tv_sec -= 1;
    }

    return idio_libc_struct_timeval_pointer (tvp);
}

IDIO_DEFINE_PRIMITIVE1_DS ("exit", libc_exit, (IDIO istatus), "status", "\
in C, close (status)						\n\
a wrapper to libc exit (3)					\n\
								\n\
:param status: exit status					\n\
:type status: fixnum or C/int					\n\
								\n\
DOES NOT RETURN :)						\n\
")
{
    IDIO_ASSERT (istatus);

    int status = 0;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else if (idio_isa_C_int (istatus)) {
	status = IDIO_C_TYPE_int (istatus);
    } else {
	/*
	 * Test Case: libc-wrap-errors/exit-bad-type.idio
	 *
	 * libc/exit #t
	 *
	 * NB watch out for *primitives* /exit (without the SPACE)
	 * which performs a more Idio-ordered exit.
	 */
	idio_error_param_type ("fixnum|C/int", istatus, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    exit (status);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pipe-reader", libc_pipe_reader, (IDIO ipipefd), "pipefd", "\
Return the read end of the pipe array				\n\
								\n\
:param pipefd: pointer to pipe array				\n\
:type pipefd: C/pointer						\n\
:return: read end of the pipe array				\n\
:rtype: C/int							\n\
								\n\
See ``pipe`` for a constructor ofthe pipe array.		\n\
")
{
    IDIO_ASSERT (ipipefd);

    /*
     * Test Case: libc-wrap-errors/pipe-reader-bad-type.idio
     *
     * pipe-reader #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[0]);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pipe-writer", libc_pipe_writer, (IDIO ipipefd), "pipefd", "\
Return the write end of the pipe array				\n\
								\n\
:param pipefd: pointer to pipe array				\n\
:type pipefd: C/pointer						\n\
:return: write end of the pipe array				\n\
:rtype: C/int							\n\
								\n\
See ``pipe`` for a constructor ofthe pipe array.		\n\
")
{
    IDIO_ASSERT (ipipefd);

    /*
     * Test Case: libc-wrap-errors/pipe-writer-bad-type.idio
     *
     * pipe-writer #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[1]);
}

/*
 * Named Pipes (for Process Substitution).
 *
 * Tricky because of platform inconsistency.  We prefer /dev/fd/n but
 * FreeBSD doesn't support it above /dev/fd/2 so we need to utilise a
 * real named pipe.
 *
 * For /dev/fd systems we can call pipe(2) and generate two /dev/fd
 * pathnames.
 *
 * Otherwise we need to wash through mkdtemp(3)[1] and mkfifo(2).  We
 * do NOT open(2) (twice) otherwise we block!  You need two seaprate
 * processes to open each end.  This means we won't be able to return
 * file descriptors as per pipe(2).
 *
 * What should we return?  I suppose the full quartet of:
 *
 * * read fd or #f
 *
 * * write fd or #f
 *
 * * read pathname
 *
 * * write pathname
 *
 * pipe() will have returned two (open) file descriptors so we need to
 * return two /dev/fd pathnames whereas the named pipe's pathname will
 * obviously(?) be the same for both ends.
 *
 * We also want to return the temporary directory for a named pipe (as
 * we're using mkdtemp()) so save a bit of time chopping strings up.
 *
 *
 * [1] We're using mkdtemp(3) rather than Bash's mktemp(3) as modern
 * linkers are prone to whinging about the resultant filename not
 * being safe to use.  Correct and irritating.  It means we need to
 * both unlink(2) the (named) pipe and rmdir(2) the directory when
 * we're done.
 */
IDIO idio_libc_proc_subst_named_pipe ()
{
#if defined (BSD)
    /*
     * I wrote the %get-tmpdir in libc.idio in the style of Bash and
     * now I need it again here...  Poor FreeBSD, we'll need to go the
     * long way round.
     */
    IDIO mtd_cmd = IDIO_LIST2 (idio_module_symbol_value (idio_symbols_C_intern ("make-tmp-dir"),
							idio_libc_module,
							 idio_S_nil),
			       idio_string_C ("idio-np-"));

    IDIO td = idio_vm_invoke_C (idio_thread_current_thread (), mtd_cmd);

    /*
     * make-tmp-dir/mkdtemp should have barfed if it failed to create
     * a temporary directory
     */
    if (idio_isa_pathname (td)) {
	size_t blen = 0;
	char *td_C = idio_string_as_C (td, &blen);

	char np_name_C[PATH_MAX];
	sprintf (np_name_C, "%s/the-pipe", td_C);

	IDIO_GC_FREE (td_C);

	IDIO np_name = idio_pathname_C (np_name_C);

	int mkfifo_r = mkfifo (np_name_C, S_IRUSR | S_IWUSR);

	if (-1 == mkfifo_r) {
	    idio_error_system_errno ("mkfifo", np_name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	return IDIO_LIST5 (idio_S_false, idio_S_false, np_name, np_name, td);
    } else {
	idio_error_param_value_exp ("proc-subst-named-pipe", "tmpdir", td, "C pathname", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
#else  /* BSD */
    int pipefd[2];

    int pipe_r = pipe (pipefd);

    if (-1 == pipe_r) {
	/*
	 * Test Case: ??
	 *
	 * Short of reaching EMFILE/ENFILE there's not much we can do.
	 */
	idio_error_system_errno ("pipe", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    char fd_name_C[PATH_MAX];
    sprintf (fd_name_C, "/dev/fd/%d", pipefd[0]);

    IDIO rfd_name = idio_pathname_C (fd_name_C);

    sprintf (fd_name_C, "/dev/fd/%d", pipefd[1]);

    IDIO wfd_name = idio_pathname_C (fd_name_C);

    return IDIO_LIST4 (idio_C_int (pipefd[0]), idio_C_int (pipefd[1]), rfd_name, wfd_name);
#endif /* BSD */
}

IDIO_DEFINE_PRIMITIVE0_DS ("proc-subst-named-pipe", proc_subst_named_pipe, (void), "", "\
return a (possibly named) pipe with pathnames for each end	\n\
								\n\
:return: see below						\n\
:rtype: list							\n\
								\n\
On /dev/fd supporting systems the return value is:		\n\
(rfd, wfd, rname, wname)					\n\
								\n\
Otherwise the return value is:					\n\
(#f, #f, pipe-name, pipe-name, tmpdir)				\n\
")
{
    return idio_libc_proc_subst_named_pipe ();
}

/*
 * signal-handler isn't a real libc function.  It has been added in
 * to aid spotting if a parent process has kindly sigignored()d
 * SIGPIPE for us:
 *
 * == (libc/signal-handler SIGPIPE) SIG_IGN
 *
 * Hopefully we'll find other uses for it.
 */
IDIO_DEFINE_PRIMITIVE1_DS ("signal-handler", libc_signal_handler, (IDIO isig), "sig", "\
signal-handler (sig)						\n\
								\n\
A helper function to advise of the current disposition for a	\n\
given signal.							\n\
								\n\
== (libc/signal-handler SIGPIPE) SIG_IGN			\n\
								\n\
:param sig: signal						\n\
:type sig: C/int						\n\
:return: current disposition or raises ^system-error		\n\
:rtype: C/pointer						\n\
")
{
    IDIO_ASSERT (isig);

    /*
     * Test Case: libc-wrap-errors/signal-handler-bad-type.idio
     *
     * signal-handler #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, isig);

    int sig = IDIO_C_TYPE_int (isig);

    struct sigaction osa;

    if (sigaction (sig, NULL, &osa) < 0) {
	/*
	 * Test Case: libc-wrap-errors/signal-handler-bad-signal.idio
	 *
	 * signal-handler (C/integer-> -1)
	 */
	idio_error_system_errno ("sigaction", isig, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Our result be be either of:

     void     (*sa_handler)(int);
     void     (*sa_sigaction)(int, siginfo_t *, void *);

     * so, uh, prototype with no args!
     */
    void (*r) ();

    if (osa.sa_flags & SA_SIGINFO) {
	/*
	 * Code coverage:
	 *
	 * Not sure...
	 */
	r = osa.sa_sigaction;
    } else {
	r = osa.sa_handler;
    }

    return idio_C_pointer (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("WEXITSTATUS", libc_WEXITSTATUS, (IDIO istatus), "status", "\
in C, WEXITSTATUS (status)					\n\
a wrapper to libc macro WEXITSTATUS, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C/pointer						\n\
:return: exit status of child					\n\
:rtype: C/int							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WEXITSTATUS-bad-type.idio
     *
     * WEXITSTATUS #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WEXITSTATUS (*statusp));
}

IDIO_DEFINE_PRIMITIVE1_DS ("WIFEXITED", libc_WIFEXITED, (IDIO istatus), "status", "\
in C, WIFEXITED (status)					\n\
a wrapper to libc macro WIFEXITED, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C/pointer						\n\
:return: #t if the child exited normally or #f			\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WIFEXITED-bad-type.idio
     *
     * WIFEXITED #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFEXITED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("WIFSIGNALED", libc_WIFSIGNALED, (IDIO istatus), "status", "\
in C, WIFSIGNALED (status)					\n\
a wrapper to libc macro WIFSIGNALED, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C/pointer						\n\
:return: #t if the child was terminated by a signal or #f	\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WIFSIGNALED-bad-type.idio
     *
     * WIFSIGNALED #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSIGNALED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("WIFSTOPPED", libc_WIFSTOPPED, (IDIO istatus), "status", "\
in C, WIFSTOPPED (status)					\n\
a wrapper to libc macro WIFSTOPPED, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C/pointer						\n\
:return: #t if the child was stopped by a signal or #f		\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WIFSTOPPED-bad-type.idio
     *
     * WIFSTOPPED #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSTOPPED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("WTERMSIG", libc_WTERMSIG, (IDIO istatus), "status", "\
in C, WTERMSIG (status)						\n\
a wrapper to libc macro WTERMSIG, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C/pointer						\n\
:return: signal number that terminated child			\n\
:rtype: C/int							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WTERMSIG-bad-type.idio
     *
     * WTERMSIG #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WTERMSIG (*statusp));
}

/*
 * idio_libc_set_signal_names
 *
 * Surprisingly, despite using the macro value, say, SIGINT in code
 * there is no way to get the descriptive string "SIGINT" back out of
 * the system.  strsignal(3) provides the helpful string "Interrupt".
 *
 * Bash's support/signames.c leads the way
 */

/*
 * How many chars in SIGRTMIN+n ?
 *
 * strlen ("SIGRTMIN+") == 9
 * +1 for NUL == 10 chars
 *
 * IDIO_LIBC_SIGNAMELEN - 10 => max n of 9999
 */
#define IDIO_LIBC_SIGNAMELEN 14

static void idio_libc_set_signal_names ()
{
    idio_libc_signal_names = idio_alloc ((IDIO_LIBC_NSIG + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG; i++) {
	idio_libc_signal_names[i] = idio_alloc (IDIO_LIBC_SIGNAMELEN);
	*(idio_libc_signal_names[i]) = '\0';
    }
    idio_libc_signal_names[i] = NULL;

    /*
     * Linux and SIGRTMIN are a slippery pair.  To be fair, the header
     * file, asm-generic/signal.h, says "These should not be
     * considered constants from userland." [ie. userland, us, should
     * not consider these values as constants] but immediately defines
     * SIGRTMIN as 32.
     *
     * In practice, the code sees SIGRTMIN as 34 and a quick grep has
     * bits/signum.h #define'ing SIGRTMIN as (__libc_current_sigrtmin
     * ()).
     *
     * Which is neither clear nor portable (assuming bits/signum.h
     * speaks the truth).
     */
#if defined(SIGRTMIN) && defined(SIGRTMAX)
    int rtmin = SIGRTMIN;
    int rtmax = SIGRTMAX;
    if (rtmax > rtmin &&
	(rtmax - rtmin) > 7) {
        sprintf (idio_libc_signal_names[SIGRTMIN], "SIGRTMIN");
	sprintf (idio_libc_signal_names[SIGRTMAX], "SIGRTMAX");

	int rtmid = (rtmax - rtmin) / 2;
	int rtdiff = (rtmax - rtmin) - (rtmid * 2);
	if (rtdiff) {
	    rtmid++;
	}

	for (i = 1; i < rtmid ; i++) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	    sprintf (idio_libc_signal_names[rtmax - i], "SIGRTMAX-%d", i);
	}

	/*
	 * Can have an extra SIGRTMIN+n (the plus variant rather than
	 * SIGRTMIN-n) if there's an odd number -- don't forget it is
	 * SIGRTMIN -> SIGRTMAX *inclusive* so there is an off-by-one
	 * error tempting us here...
	 */
	if (0 == rtdiff) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	}
    }
#endif

#if defined(SIGHUP)
    IDIO_LIBC_SIGNAL (SIGHUP)
#endif

#if defined(SIGINT)
    IDIO_LIBC_SIGNAL (SIGINT)
#endif

#if defined(SIGQUIT)
    IDIO_LIBC_SIGNAL (SIGQUIT)
#endif

#if defined(SIGILL)
    IDIO_LIBC_SIGNAL (SIGILL)
#endif

#if defined(SIGTRAP)
    IDIO_LIBC_SIGNAL (SIGTRAP)
#endif

#if defined(SIGIOT)
    IDIO_LIBC_SIGNAL (SIGIOT)
#endif

#if defined(SIGEMT)
    IDIO_LIBC_SIGNAL (SIGEMT)
#endif

#if defined(SIGFPE)
    IDIO_LIBC_SIGNAL (SIGFPE)
#endif

#if defined(SIGKILL)
    IDIO_LIBC_SIGNAL_NAME (SIGKILL)
#endif

#if defined(SIGBUS)
    IDIO_LIBC_SIGNAL (SIGBUS)
#endif

#if defined(SIGSEGV)
    IDIO_LIBC_SIGNAL (SIGSEGV)
#endif

#if defined(SIGSYS)
    IDIO_LIBC_SIGNAL (SIGSYS)
#endif

#if defined(SIGPIPE)
    IDIO_LIBC_SIGNAL (SIGPIPE)
#endif

#if defined(SIGALRM)
    IDIO_LIBC_SIGNAL (SIGALRM)
#endif

#if defined(SIGTERM)
    IDIO_LIBC_SIGNAL (SIGTERM)
#endif

#if defined(SIGUSR1)
    IDIO_LIBC_SIGNAL (SIGUSR1)
#endif

#if defined(SIGUSR2)
    IDIO_LIBC_SIGNAL (SIGUSR2)
#endif

#if defined(SIGCHLD)
    IDIO_LIBC_SIGNAL (SIGCHLD)
#endif

#if defined(SIGPWR)
    IDIO_LIBC_SIGNAL (SIGPWR)
#endif

#if defined(SIGWINCH)
    IDIO_LIBC_SIGNAL (SIGWINCH)
#endif

#if defined(SIGURG)
    IDIO_LIBC_SIGNAL (SIGURG)
#endif

#if defined(SIGPOLL)
    IDIO_LIBC_SIGNAL (SIGPOLL)
#endif

#if defined(SIGSTOP)
    IDIO_LIBC_SIGNAL_NAME (SIGSTOP)
#endif

#if defined(SIGTSTP)
    IDIO_LIBC_SIGNAL (SIGTSTP)
#endif

#if defined(SIGCONT)
    IDIO_LIBC_SIGNAL (SIGCONT)
#endif

#if defined(SIGTTIN)
    IDIO_LIBC_SIGNAL (SIGTTIN)
#endif

#if defined(SIGTTOU)
    IDIO_LIBC_SIGNAL (SIGTTOU)
#endif

#if defined(SIGVTALRM)
    IDIO_LIBC_SIGNAL (SIGVTALRM)
#endif

#if defined(SIGPROF)
    IDIO_LIBC_SIGNAL (SIGPROF)
#endif

#if defined(SIGXCPU)
    IDIO_LIBC_SIGNAL (SIGXCPU)
#endif

#if defined(SIGXFSZ)
    IDIO_LIBC_SIGNAL (SIGXFSZ)
#endif

    /* SunOS */
#if defined(SIGWAITING)
    IDIO_LIBC_SIGNAL (SIGWAITING)
#endif

    /* SunOS */
#if defined(SIGLWP)
    IDIO_LIBC_SIGNAL (SIGLWP)
#endif

    /* SunOS */
#if defined(SIGFREEZE)
    IDIO_LIBC_SIGNAL (SIGFREEZE)
#endif

    /* SunOS */
#if defined(SIGTHAW)
    IDIO_LIBC_SIGNAL (SIGTHAW)
#endif

    /* SunOS */
#if defined(SIGCANCEL)
    IDIO_LIBC_SIGNAL (SIGCANCEL)
#endif

    /* SunOS */
#if defined(SIGLOST)
    IDIO_LIBC_SIGNAL (SIGLOST)
#endif

    /* SunOS */
#if defined(SIGXRES)
    IDIO_LIBC_SIGNAL (SIGXRES)
#endif

    /* SunOS */
#if defined(SIGJVM1)
    IDIO_LIBC_SIGNAL (SIGJVM1)
#endif

    /* SunOS */
#if defined(SIGJVM2)
    IDIO_LIBC_SIGNAL (SIGJVM2)
#endif

    /* OpenIndiana */
#if defined(SIGINFO)
    IDIO_LIBC_SIGNAL (SIGINFO)
#endif

    /* Linux */
#if defined(SIGSTKFLT)
    IDIO_LIBC_SIGNAL (SIGSTKFLT)
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FSIG ; i < IDIO_LIBC_NSIG ; i++) {
	if ('\0' == *(idio_libc_signal_names[i])) {
	    char sig_name[IDIO_LIBC_SIGNAMELEN + 3];
	    sprintf (sig_name, "SIGJUNK%d", i);
	    IDIO_LIBC_SIGNAL_NAME_ONLY (sig_name, i)
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped signal numbers:\n");
		fprintf (stderr, " %3s %-*s %s\n", "id", IDIO_LIBC_SIGNAMELEN, "Idio name", "strsignal()");
	    }
	    /*
	     * strsignal() leaks memory?
	     */
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_SIGNAMELEN, sig_name, strsignal (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

/*
 * (According to Bash's .../builtins/common.c)
 *
 * POSIX.2 says the signal names are displayed without the `SIG'
 * prefix.
 *
 * We'll support both.  Functions like ~signal~ says the full signal
 * name and ~sig~ says the POSIX.2 format (arguably it should be ~nal~
 * as we've stripped off the "sig" part...can't be witty in the API,
 * though!)
 */
char *idio_libc_signal_name (int signum)
{
    if (signum < IDIO_LIBC_FSIG ||
	signum > IDIO_LIBC_NSIG) {
	/*
	 * Test Case:  libc-wrap-errors/signal-name-bad-signum.idio
	 *
	 * signal-name (C/integer-> -1)
	 */
	idio_error_param_value_msg ("sig-name", "signum", idio_fixnum (signum), "should be 0 < int < NSIG (OS dependent)", IDIO_C_FUNC_LOCATION ());

	return NULL;
    }

    return idio_libc_signal_names[signum];
}

char *idio_libc_sig_name (int signum)
{
    char *signame = idio_libc_signal_name (signum);

    if (strncmp (signame, "SIG", 3) == 0) {
	return (signame + 3);
    } else {
	return signame;
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("sig-name", libc_sig_name, (IDIO isignum), "signum", "\
return the short signal name of ``signum``			\n\
								\n\
:param signum: signal number					\n\
:type signum: C/int						\n\
:return: short signal name					\n\
:rtype: string							\n\
								\n\
A short signal name would be \"QUIT\" or \"INT\".		\n\
								\n\
See ``signal-name`` for long versions.				\n\
")
{
    IDIO_ASSERT (isignum);

    /*
     * Test Case: libc-wrap-errors/sig-name-bad-type.idio
     *
     * sig-name #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, isignum);

    return idio_string_C (idio_libc_sig_name (IDIO_C_TYPE_int (isignum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("sig-names", libc_sig_names, (void), "", "\
return a list of (number, short name) pairs of known signals	\n\
								\n\
:return: map of signal pairs					\n\
:rtype: list							\n\
								\n\
A short signal name would be \"QUIT\" or \"INT\".		\n\
								\n\
See ``signal-names`` for long versions.				\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_sig_name (i))), r);
    }

    return idio_list_reverse (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("signal-name", libc_signal_name, (IDIO isignum), "signum", "\
return the long signal name of ``signum``			\n\
								\n\
:param signum: signal number					\n\
:type signum: C/int						\n\
:return: long signal name					\n\
:rtype: string							\n\
								\n\
A long signal name would be \"SIGQUIT\" or \"SIGINT\".		\n\
								\n\
See ``sig-name`` for short versions.				\n\
")
{
    IDIO_ASSERT (isignum);

    /*
     * Test Case: libc-wrap-errors/signal-name-bad-type.idio
     *
     * signal-name #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, isignum);

    return idio_string_C (idio_libc_signal_name (IDIO_C_TYPE_int (isignum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("signal-names", libc_signal_names, (void), "", "\
return a list of (number, long name) pairs of known signals	\n\
								\n\
:return: map of signal pairs					\n\
:rtype: list							\n\
								\n\
A long signal name would be \"SIGQUIT\" or \"SIGINT\".		\n\
								\n\
See ``sig-names`` for short versions.				\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_signal_name (i))), r);
    }

    return idio_list_reverse (r);
}

/*
 * idio_libc_set_errno_names
 *
 * Surprisingly, despite using the macro value, say, ECHILD in code
 * there is no way to get the descriptive string "ECHILD" back out of
 * the system.  strerror(3) provides the helpful string "No child
 * processes".
 *
 * We must follow the path blazed above for signals
 */

/*
 * How many errnos are there?
 *
 * FreeBSD and OS X define ELAST but many others do not.
 *
 * Linux's errno(3) suggests we might be referring to the set
 * including *at least* Linux's own definitions, those of POSIX.1-2001
 * and those of C99.  (Open)Solaris' Intro(2) mentions the Single Unix
 * Specification as the source of all errnos and has gaps in its own
 * definitions for some BSD Networking (100-119) and XENIX (135-142).
 *
 * And there's a stragglers found in anything else that we stumble
 * across.
 *
 * CentOS 6&7	EHWPOISON	133
 * OI 151a8	ESTALE		151
 * OS X 9.8.0	ENOPOLICY	103	aka ELAST
 * OS X 14.4.0	EQFULL		106	aka ELAST
 * FreeBSD 10	EOWNERDEAD	96	aka ELAST
 * Ubuntu 14	EHWPOISON	133
 * Debian 8	EHWPOISON	133
 *
 */

#define IDIO_LIBC_FERRNO 1

#if defined (BSD)
#define IDIO_LIBC_NERRNO (ELAST + 1)
#elif defined (__linux__)
#define IDIO_LIBC_NERRNO (EHWPOISON + 1)
#elif defined (__APPLE__) && defined (__MACH__)
#define IDIO_LIBC_NERRNO (ELAST + 1)
#elif defined (__sun) && defined (__SVR4)
#define IDIO_LIBC_NERRNO (ESTALE + 1)
#else
#error Cannot define IDIO_LIBC_NERRNO
#endif

/*
 * How many chars in E-somename- ?
 *
 * Empirical study suggests ENOTRECOVERABLE, EPROTONOSUPPORT and
 * ESOCKTNOSUPPORT are the longest E-names at 15 chars each.
 */
#define IDIO_LIBC_ERRNAMELEN 20

static void idio_libc_set_errno_names ()
{
    idio_libc_errno_names = idio_alloc ((IDIO_LIBC_NERRNO + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO; i++) {
	idio_libc_errno_names[i] = idio_alloc (IDIO_LIBC_ERRNAMELEN);
	*(idio_libc_errno_names[i]) = '\0';
    }
    idio_libc_errno_names[i] = NULL;

    idio_libc_errno_names[0] = "E0";

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (E2BIG)
    IDIO_LIBC_ERRNO (E2BIG);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EACCES)
    IDIO_LIBC_ERRNO (EACCES);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRINUSE)
    IDIO_LIBC_ERRNO (EADDRINUSE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRNOTAVAIL)
    IDIO_LIBC_ERRNO (EADDRNOTAVAIL);
#endif

    /* Linux, Solaris */
#if defined (EADV)
    IDIO_LIBC_ERRNO (EADV);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAFNOSUPPORT)
    IDIO_LIBC_ERRNO (EAFNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAGAIN)
    IDIO_LIBC_ERRNO (EAGAIN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EALREADY)
    IDIO_LIBC_ERRNO (EALREADY);
#endif

    /* FreeBSD, OSX */
#if defined (EAUTH)
    IDIO_LIBC_ERRNO (EAUTH);
#endif

    /* OSX */
#if defined (EBADARCH)
    IDIO_LIBC_ERRNO (EBADARCH);
#endif

    /* Linux, Solaris */
#if defined (EBADE)
    IDIO_LIBC_ERRNO (EBADE);
#endif

    /* OSX */
#if defined (EBADEXEC)
    IDIO_LIBC_ERRNO (EBADEXEC);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADF)
    IDIO_LIBC_ERRNO (EBADF);
#endif

    /* Linux, Solaris */
#if defined (EBADFD)
    IDIO_LIBC_ERRNO (EBADFD);
#endif

    /* OSX */
#if defined (EBADMACHO)
    IDIO_LIBC_ERRNO (EBADMACHO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADMSG)
    IDIO_LIBC_ERRNO (EBADMSG);
#endif

    /* Linux, Solaris */
#if defined (EBADR)
    IDIO_LIBC_ERRNO (EBADR);
#endif

    /* FreeBSD, OSX */
#if defined (EBADRPC)
    IDIO_LIBC_ERRNO (EBADRPC);
#endif

    /* Linux, Solaris */
#if defined (EBADRQC)
    IDIO_LIBC_ERRNO (EBADRQC);
#endif

    /* Linux, Solaris */
#if defined (EBADSLT)
    IDIO_LIBC_ERRNO (EBADSLT);
#endif

    /* Linux, Solaris */
#if defined (EBFONT)
    IDIO_LIBC_ERRNO (EBFONT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBUSY)
    IDIO_LIBC_ERRNO (EBUSY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECANCELED)
    IDIO_LIBC_ERRNO (ECANCELED);
#endif

    /* FreeBSD */
#if defined (ECAPMODE)
    IDIO_LIBC_ERRNO (ECAPMODE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECHILD)
    IDIO_LIBC_ERRNO (ECHILD);
#endif

    /* Linux, Solaris */
#if defined (ECHRNG)
    IDIO_LIBC_ERRNO (ECHRNG);
#endif

    /* Linux, Solaris */
#if defined (ECOMM)
    IDIO_LIBC_ERRNO (ECOMM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNABORTED)
    IDIO_LIBC_ERRNO (ECONNABORTED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNREFUSED)
    IDIO_LIBC_ERRNO (ECONNREFUSED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNRESET)
    IDIO_LIBC_ERRNO (ECONNRESET);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDEADLK)
    IDIO_LIBC_ERRNO (EDEADLK);
#endif

    /* Linux, Solaris */
#if defined (EDEADLOCK)
    IDIO_LIBC_ERRNO (EDEADLOCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDESTADDRREQ)
    IDIO_LIBC_ERRNO (EDESTADDRREQ);
#endif

    /* OSX */
#if defined (EDEVERR)
    IDIO_LIBC_ERRNO (EDEVERR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDOM)
    IDIO_LIBC_ERRNO (EDOM);
#endif

    /* FreeBSD */
#if defined (EDOOFUS)
    IDIO_LIBC_ERRNO (EDOOFUS);
#endif

    /* Linux */
#if defined (EDOTDOT)
    IDIO_LIBC_ERRNO (EDOTDOT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDQUOT)
    IDIO_LIBC_ERRNO (EDQUOT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EEXIST)
    IDIO_LIBC_ERRNO (EEXIST);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFAULT)
    IDIO_LIBC_ERRNO (EFAULT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFBIG)
    IDIO_LIBC_ERRNO (EFBIG);
#endif

    /* FreeBSD, OSX */
#if defined (EFTYPE)
    IDIO_LIBC_ERRNO (EFTYPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTDOWN)
    IDIO_LIBC_ERRNO (EHOSTDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTUNREACH)
    IDIO_LIBC_ERRNO (EHOSTUNREACH);
#endif

    /* Linux */
#if defined (EHWPOISON)
    IDIO_LIBC_ERRNO (EHWPOISON);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIDRM)
    IDIO_LIBC_ERRNO (EIDRM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EILSEQ)
    IDIO_LIBC_ERRNO (EILSEQ);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINPROGRESS)
    IDIO_LIBC_ERRNO (EINPROGRESS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINTR)
    IDIO_LIBC_ERRNO (EINTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINVAL)
    IDIO_LIBC_ERRNO (EINVAL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIO)
    IDIO_LIBC_ERRNO (EIO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISCONN)
    IDIO_LIBC_ERRNO (EISCONN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISDIR)
    IDIO_LIBC_ERRNO (EISDIR);
#endif

    /* Linux */
#if defined (EISNAM)
    IDIO_LIBC_ERRNO (EISNAM);
#endif

    /* Linux */
#if defined (EKEYEXPIRED)
    IDIO_LIBC_ERRNO (EKEYEXPIRED);
#endif

    /* Linux */
#if defined (EKEYREJECTED)
    IDIO_LIBC_ERRNO (EKEYREJECTED);
#endif

    /* Linux */
#if defined (EKEYREVOKED)
    IDIO_LIBC_ERRNO (EKEYREVOKED);
#endif

    /* Linux, Solaris */
#if defined (EL2HLT)
    IDIO_LIBC_ERRNO (EL2HLT);
#endif

    /* Linux, Solaris */
#if defined (EL2NSYNC)
    IDIO_LIBC_ERRNO (EL2NSYNC);
#endif

    /* Linux, Solaris */
#if defined (EL3HLT)
    IDIO_LIBC_ERRNO (EL3HLT);
#endif

    /* Linux, Solaris */
#if defined (EL3RST)
    IDIO_LIBC_ERRNO (EL3RST);
#endif

    /* Linux, Solaris */
#if defined (ELIBACC)
    IDIO_LIBC_ERRNO (ELIBACC);
#endif

    /* Linux, Solaris */
#if defined (ELIBBAD)
    IDIO_LIBC_ERRNO (ELIBBAD);
#endif

    /* Linux, Solaris */
#if defined (ELIBEXEC)
    IDIO_LIBC_ERRNO (ELIBEXEC);
#endif

    /* Linux, Solaris */
#if defined (ELIBMAX)
    IDIO_LIBC_ERRNO (ELIBMAX);
#endif

    /* Linux, Solaris */
#if defined (ELIBSCN)
    IDIO_LIBC_ERRNO (ELIBSCN);
#endif

    /* Linux, Solaris */
#if defined (ELNRNG)
    IDIO_LIBC_ERRNO (ELNRNG);
#endif

    /* Solaris */
#if defined (ELOCKUNMAPPED)
    IDIO_LIBC_ERRNO (ELOCKUNMAPPED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ELOOP)
    IDIO_LIBC_ERRNO (ELOOP);
#endif

    /* Linux */
#if defined (EMEDIUMTYPE)
    IDIO_LIBC_ERRNO (EMEDIUMTYPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMFILE)
    IDIO_LIBC_ERRNO (EMFILE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMLINK)
    IDIO_LIBC_ERRNO (EMLINK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMSGSIZE)
    IDIO_LIBC_ERRNO (EMSGSIZE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMULTIHOP)
    IDIO_LIBC_ERRNO (EMULTIHOP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENAMETOOLONG)
    IDIO_LIBC_ERRNO (ENAMETOOLONG);
#endif

    /* Linux */
#if defined (ENAVAIL)
    IDIO_LIBC_ERRNO (ENAVAIL);
#endif

    /* FreeBSD, OSX */
#if defined (ENEEDAUTH)
    IDIO_LIBC_ERRNO (ENEEDAUTH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETDOWN)
    IDIO_LIBC_ERRNO (ENETDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETRESET)
    IDIO_LIBC_ERRNO (ENETRESET);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETUNREACH)
    IDIO_LIBC_ERRNO (ENETUNREACH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENFILE)
    IDIO_LIBC_ERRNO (ENFILE);
#endif

    /* Linux, Solaris */
#if defined (ENOANO)
    IDIO_LIBC_ERRNO (ENOANO);
#endif

    /* FreeBSD, OSX */
#if defined (ENOATTR)
    IDIO_LIBC_ERRNO (ENOATTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOBUFS)
    IDIO_LIBC_ERRNO (ENOBUFS);
#endif

    /* Linux, Solaris */
#if defined (ENOCSI)
    IDIO_LIBC_ERRNO (ENOCSI);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENODATA)
    IDIO_LIBC_ERRNO (ENODATA);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENODEV)
    IDIO_LIBC_ERRNO (ENODEV);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOENT)
    IDIO_LIBC_ERRNO (ENOENT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOEXEC)
    IDIO_LIBC_ERRNO (ENOEXEC);
#endif

    /* Linux */
#if defined (ENOKEY)
    IDIO_LIBC_ERRNO (ENOKEY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLCK)
    IDIO_LIBC_ERRNO (ENOLCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLINK)
    IDIO_LIBC_ERRNO (ENOLINK);
#endif

    /* Linux */
#if defined (ENOMEDIUM)
    IDIO_LIBC_ERRNO (ENOMEDIUM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMEM)
    IDIO_LIBC_ERRNO (ENOMEM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMSG)
    IDIO_LIBC_ERRNO (ENOMSG);
#endif

    /* Linux, Solaris */
#if defined (ENONET)
    IDIO_LIBC_ERRNO (ENONET);
#endif

    /* Linux, Solaris */
#if defined (ENOPKG)
    IDIO_LIBC_ERRNO (ENOPKG);
#endif

    /* OSX */
#if defined (ENOPOLICY)
    IDIO_LIBC_ERRNO (ENOPOLICY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOPROTOOPT)
    IDIO_LIBC_ERRNO (ENOPROTOOPT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSPC)
    IDIO_LIBC_ERRNO (ENOSPC);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSR)
    IDIO_LIBC_ERRNO (ENOSR);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSTR)
    IDIO_LIBC_ERRNO (ENOSTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSYS)
    IDIO_LIBC_ERRNO (ENOSYS);
#endif

    /* Solaris */
#if defined (ENOTACTIVE)
    IDIO_LIBC_ERRNO (ENOTACTIVE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTBLK)
    IDIO_LIBC_ERRNO (ENOTBLK);
#endif

    /* FreeBSD */
#if defined (ENOTCAPABLE)
    IDIO_LIBC_ERRNO (ENOTCAPABLE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTCONN)
    IDIO_LIBC_ERRNO (ENOTCONN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTDIR)
    IDIO_LIBC_ERRNO (ENOTDIR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTEMPTY)
    IDIO_LIBC_ERRNO (ENOTEMPTY);
#endif

    /* Linux */
#if defined (ENOTNAM)
    IDIO_LIBC_ERRNO (ENOTNAM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTRECOVERABLE)
    IDIO_LIBC_ERRNO (ENOTRECOVERABLE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTSOCK)
    IDIO_LIBC_ERRNO (ENOTSOCK);
#endif

    /* FreeBSD, OSX, Solaris */
#if defined (ENOTSUP)
    IDIO_LIBC_ERRNO (ENOTSUP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTTY)
    IDIO_LIBC_ERRNO (ENOTTY);
#endif

    /* Linux, Solaris */
#if defined (ENOTUNIQ)
    IDIO_LIBC_ERRNO (ENOTUNIQ);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENXIO)
    IDIO_LIBC_ERRNO (ENXIO);
#endif

    /* FreeBSD, Linux, OSX, OSX, Solaris */
#if defined (EOPNOTSUPP)
    IDIO_LIBC_ERRNO (EOPNOTSUPP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOVERFLOW)
    IDIO_LIBC_ERRNO (EOVERFLOW);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOWNERDEAD)
    IDIO_LIBC_ERRNO (EOWNERDEAD);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPERM)
    IDIO_LIBC_ERRNO (EPERM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPFNOSUPPORT)
    IDIO_LIBC_ERRNO (EPFNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPIPE)
    IDIO_LIBC_ERRNO (EPIPE);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCLIM)
    IDIO_LIBC_ERRNO (EPROCLIM);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCUNAVAIL)
    IDIO_LIBC_ERRNO (EPROCUNAVAIL);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGMISMATCH)
    IDIO_LIBC_ERRNO (EPROGMISMATCH);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGUNAVAIL)
    IDIO_LIBC_ERRNO (EPROGUNAVAIL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTO)
    IDIO_LIBC_ERRNO (EPROTO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTONOSUPPORT)
    IDIO_LIBC_ERRNO (EPROTONOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTOTYPE)
    IDIO_LIBC_ERRNO (EPROTOTYPE);
#endif

    /* OSX */
#if defined (EPWROFF)
    IDIO_LIBC_ERRNO (EPWROFF);
#endif

    /* OSX */
#if defined (EQFULL)
    IDIO_LIBC_ERRNO (EQFULL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ERANGE)
    IDIO_LIBC_ERRNO (ERANGE);
#endif

    /* Linux, Solaris */
#if defined (EREMCHG)
    IDIO_LIBC_ERRNO (EREMCHG);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EREMOTE)
    IDIO_LIBC_ERRNO (EREMOTE);
#endif

    /* Linux */
#if defined (EREMOTEIO)
    IDIO_LIBC_ERRNO (EREMOTEIO);
#endif

    /* Linux, Solaris */
#if defined (ERESTART)
    IDIO_LIBC_ERRNO (ERESTART);
#endif

    /* Linux */
#if defined (ERFKILL)
    IDIO_LIBC_ERRNO (ERFKILL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EROFS)
    IDIO_LIBC_ERRNO (EROFS);
#endif

    /* FreeBSD, OSX */
#if defined (ERPCMISMATCH)
    IDIO_LIBC_ERRNO (ERPCMISMATCH);
#endif

    /* OSX */
#if defined (ESHLIBVERS)
    IDIO_LIBC_ERRNO (ESHLIBVERS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESHUTDOWN)
    IDIO_LIBC_ERRNO (ESHUTDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESOCKTNOSUPPORT)
    IDIO_LIBC_ERRNO (ESOCKTNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESPIPE)
    IDIO_LIBC_ERRNO (ESPIPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESRCH)
    IDIO_LIBC_ERRNO (ESRCH);
#endif

    /* Linux, Solaris */
#if defined (ESRMNT)
    IDIO_LIBC_ERRNO (ESRMNT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESTALE)
    IDIO_LIBC_ERRNO (ESTALE);
#endif

    /* Linux, Solaris */
#if defined (ESTRPIPE)
    IDIO_LIBC_ERRNO (ESTRPIPE);
#endif

    /* Linux, OSX, Solaris */
#if defined (ETIME)
    IDIO_LIBC_ERRNO (ETIME);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETIMEDOUT)
    IDIO_LIBC_ERRNO (ETIMEDOUT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETOOMANYREFS)
    IDIO_LIBC_ERRNO (ETOOMANYREFS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETXTBSY)
    IDIO_LIBC_ERRNO (ETXTBSY);
#endif

    /* Linux */
#if defined (EUCLEAN)
    IDIO_LIBC_ERRNO (EUCLEAN);
#endif

    /* Linux, Solaris */
#if defined (EUNATCH)
    IDIO_LIBC_ERRNO (EUNATCH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EUSERS)
    IDIO_LIBC_ERRNO (EUSERS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EWOULDBLOCK)
    IDIO_LIBC_ERRNO (EWOULDBLOCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EXDEV)
    IDIO_LIBC_ERRNO (EXDEV);
#endif

    /* Linux, Solaris */
#if defined (EXFULL)
    IDIO_LIBC_ERRNO (EXFULL);
#endif

    /*
     * OpenIndiana anomalies -- strerror returns a non "Error X"
     * string - sys/errno.h says "XENIX has 135 - 142"
     *
     * num	errno?		strerror
     *
     * 135	EUCLEAN		Structure needs cleaning
     * 137	ENOTNAM		Not a name file
     * 138	?		Not available
     * 139	EISNAM		Is a name file
     * 140	EREMOTEIO	Remote I/O error
     * 141	?		Reserved for future use
     */

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FERRNO ; i < IDIO_LIBC_NERRNO ; i++) {
	if ('\0' == *(idio_libc_errno_names[i])) {
	    char err_name[IDIO_LIBC_ERRNAMELEN + 2];
	    sprintf (err_name, "ERRUNKNOWN%d", i);
	    IDIO err_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (err_sym, idio_C_int (i));
	    sprintf (idio_libc_errno_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped errno numbers:\n");
		fprintf (stderr, " %3s %-*s %s\n", "id", IDIO_LIBC_ERRNAMELEN, "Idio name", "strerror ()");
	    }
	    /*
	     * strerror() leaks memory?
	     */
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_ERRNAMELEN, err_name, strerror (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

char *idio_libc_errno_name (int errnum)
{
    if (errnum < 0 ||
	errnum > IDIO_LIBC_NERRNO) {
	/*
	 * Test Case:  libc-wrap-errors/errno-name-bad-errnum.idio
	 *
	 * errno-name (C/integer-> -1)
	 */
	idio_error_param_value_msg ("errno-name", "errnum", idio_fixnum (errnum), "should be 0 < int < NERRNO (OS dependent)", IDIO_C_FUNC_LOCATION ());

	return NULL;
    }

    return idio_libc_errno_names[errnum];
}

IDIO_DEFINE_PRIMITIVE1_DS ("errno-name", libc_errno_name, (IDIO ierrnum), "errnum", "\
return the error name of ``errnum``				\n\
								\n\
:param errnum: error number					\n\
:type errnum: C/int						\n\
:return: error name						\n\
:rtype: string							\n\
")
{
    IDIO_ASSERT (ierrnum);

    /*
     * Test Case: libc-wrap-errors/errno-name-bad-type.idio
     *
     * errno-name #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_int (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("errno-names", libc_errno_names, (void), "", "\
return a list of (number, name) pairs of known errno numbers	\n\
								\n\
:return: map of errno pairs					\n\
:rtype: list							\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_errno_name (i))), r);
    }

    return idio_list_reverse (r);
}

/*
 * Moral equivalent of strsignal(3) -- identical to errno-name, above.
 */
IDIO_DEFINE_PRIMITIVE1_DS ("strerrno", libc_strerrno, (IDIO ierrnum), "errnum", "\
return the error name of ``errnum``				\n\
								\n\
:param errnum: error number					\n\
:type errnum: C/int						\n\
:return: error name						\n\
:rtype: string							\n\
								\n\
Identical to ``errno-name``.					\n\
")
{
    IDIO_ASSERT (ierrnum);

    /*
     * Test Case: libc-wrap-errors/strerrno-bad-type.idio
     *
     * strerrno #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_int (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("errno/get", libc_errno_get, (void), "", "\
return ``errno``						\n\
								\n\
:return: errno							\n\
:rtype: C/int							\n\
")
{
    return idio_C_int (errno);
}

/*
 * How many rlimits are there?
 *
 * RLIMIT_NLIMITS
 */

#define IDIO_LIBC_FRLIMIT 0
#define IDIO_LIBC_NRLIMIT RLIMIT_NLIMITS

/*
 * How many chars in RLIMIT_-somename- ?
 *
 * Empirical study suggests RLIMIT_SIGPENDING is the longest
 * RLIMIT_-name at 17 chars.
 */
#define IDIO_LIBC_RLIMITNAMELEN 20

static void idio_libc_set_rlimit_names ()
{
    idio_libc_rlimit_names = idio_alloc ((IDIO_LIBC_NRLIMIT + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT; i++) {
	idio_libc_rlimit_names[i] = idio_alloc (IDIO_LIBC_RLIMITNAMELEN);
	*(idio_libc_rlimit_names[i]) = '\0';
    }
    idio_libc_rlimit_names[i] = NULL;

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_CPU)
    IDIO_LIBC_RLIMIT (RLIMIT_CPU);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_FSIZE)
    IDIO_LIBC_RLIMIT (RLIMIT_FSIZE);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_DATA)
    IDIO_LIBC_RLIMIT (RLIMIT_DATA);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_STACK)
    IDIO_LIBC_RLIMIT (RLIMIT_STACK);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_CORE)
    IDIO_LIBC_RLIMIT (RLIMIT_CORE);
#endif

    /* Linux, FreeBSD */
#if defined (RLIMIT_RSS)
    IDIO_LIBC_RLIMIT (RLIMIT_RSS);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_NOFILE)
    IDIO_LIBC_RLIMIT (RLIMIT_NOFILE);
#endif

    /* Solaris, FreeBSD */
#if defined (RLIMIT_VMEM)
    IDIO_LIBC_RLIMIT (RLIMIT_VMEM);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_AS)
    IDIO_LIBC_RLIMIT (RLIMIT_AS);
#endif

    /* Linux, FreeBSD */
#if defined (RLIMIT_NPROC)
    IDIO_LIBC_RLIMIT (RLIMIT_NPROC);
#endif

    /* Linux, FreeBSD */
#if defined (RLIMIT_MEMLOCK)
    IDIO_LIBC_RLIMIT (RLIMIT_MEMLOCK);
#endif

    /* Linux */
#if defined (RLIMIT_LOCKS)
    IDIO_LIBC_RLIMIT (RLIMIT_LOCKS);
#endif

    /* Linux */
#if defined (RLIMIT_SIGPENDING)
    IDIO_LIBC_RLIMIT (RLIMIT_SIGPENDING);
#endif

    /* Linux */
#if defined (RLIMIT_MSGQUEUE)
    IDIO_LIBC_RLIMIT (RLIMIT_MSGQUEUE);
#endif

    /* Linux */
#if defined (RLIMIT_NICE)
    IDIO_LIBC_RLIMIT (RLIMIT_NICE);
#endif

    /* Linux */
#if defined (RLIMIT_RTPRIO)
    IDIO_LIBC_RLIMIT (RLIMIT_RTPRIO);
#endif

    /* Linux */
#if defined (RLIMIT_RTTIME)
    IDIO_LIBC_RLIMIT (RLIMIT_RTTIME);
#endif

    /* FreeBSD */
#if defined (RLIMIT_SBSIZE)
    IDIO_LIBC_RLIMIT (RLIMIT_SBSIZE);
#endif

    /* FreeBSD */
#if defined (RLIMIT_NPTS)
    IDIO_LIBC_RLIMIT (RLIMIT_NPTS);
#endif

    /* FreeBSD */
#if defined (RLIMIT_SWAP)
    IDIO_LIBC_RLIMIT (RLIMIT_SWAP);
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FRLIMIT ; i < IDIO_LIBC_NRLIMIT ; i++) {
	if ('\0' == *(idio_libc_rlimit_names[i])) {
	    char err_name[IDIO_LIBC_RLIMITNAMELEN + 2];
	    sprintf (err_name, "RLIMIT_UNKNOWN%d", i);
	    IDIO rlimit_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (i));
	    sprintf (idio_libc_rlimit_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped rlimit numbers:\n");
		fprintf (stderr, " %3s %-*s %s\n", "id", IDIO_LIBC_RLIMITNAMELEN, "Idio name", "strerror ()");
	    }
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_RLIMITNAMELEN, err_name, strerror (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

char *idio_libc_rlimit_name (int rlim)
{
    if (rlim < IDIO_LIBC_FRLIMIT ||
	rlim > IDIO_LIBC_NRLIMIT) {
	/*
	 * Test Case:  libc-wrap-errors/rlimit-name-bad-rlim.idio
	 *
	 * rlimit-name (C/integer-> -1)
	 */
	idio_error_param_value_msg ("rlimit-name", "rlim", idio_fixnum (rlim), "should be an 0 <= int < RLIM_NLIMITS", IDIO_C_FUNC_LOCATION ());

	return NULL;
    }

    return idio_libc_rlimit_names[rlim];
}

IDIO_DEFINE_PRIMITIVE1_DS ("rlimit-name", libc_rlimit_name, (IDIO irlim), "irlim", "\
return the string name of the getrlimit(2)      \n\
C macro						\n\
						\n\
:param irlim: the C/int value of the macro	\n\
						\n\
:return: a string				\n\
						\n\
:raises: ^rt-parameter-type-error		\n\
")
{
    IDIO_ASSERT (irlim);

    /*
     * Test Case: libc-wrap-errors/rlimit-name-bad-type.idio
     *
     * rlimit-name #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, irlim);

    return idio_string_C (idio_libc_rlimit_name (IDIO_C_TYPE_int (irlim)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("rlimit-names", libc_rlimit_names, (), "", "\
return a list of pairs of the getrlimit(2)      \n\
C macros					\n\
						\n\
each pair is the C value and string name	\n\
of the macro					\n\
						\n\
:return: a list of pairs			\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_rlimit_name (i))), r);
    }

    return idio_list_reverse (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("EGID/get", EGID_get, (void), "", "\
getter for the computed value ``EGID`` which is a call to	\n\
getegid (2).							\n\
								\n\
:return: effective group ID					\n\
:rtype: libc/gid_t						\n\
")
{
    return idio_libc_gid_t (getegid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("EGID/set", EGID_set, (IDIO iegid), "egid", "\
setter for the computed value ``EGID`` which is a call to	\n\
setegid (2).							\n\
								\n\
:param egid: effective group ID					\n\
:type egid: libc/gid_t						\n\
:return: 0 or raises ^system-error				\n\
:rtype: C/int							\n\
")
{
    IDIO_ASSERT (iegid);

    gid_t egid = -1;

    if (idio_isa_libc_gid_t (iegid)) {
	egid = IDIO_C_TYPE_libc_gid_t (iegid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/EGID-set-bad-type.idio
	 *
	 * EGID = #t
	 */
	idio_error_param_type ("libc/gid_t", iegid, IDIO_C_LOCATION ("EGID/set"));

	return idio_S_notreached;
    }

    int r = setegid (egid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/EGID-set-invalid-gid.idio
	 *
	 * ;; probably a fail...
	 * EGID = (C/integer-> -1 libc/gid_t)
	 */
	idio_error_system_errno ("setegid", iegid, IDIO_C_LOCATION ("EGID/set"));

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("EUID/get", EUID_get, (void), "", "\
getter for the computed value ``EUID`` which is a call to	\n\
geteuid (2).							\n\
								\n\
:return: effective user ID					\n\
:rtype: libc/uid_t						\n\
")
{
    return idio_libc_uid_t (geteuid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("EUID/set", EUID_set, (IDIO ieuid), "euid", "\
setter for the computed value ``EUID`` which is a call to	\n\
seteuid (2).							\n\
								\n\
:param euid: effective user ID					\n\
:type euid: libc/uid_t						\n\
:return: 0 or raises ^system-error				\n\
:rtype: C/int							\n\
")
{
    IDIO_ASSERT (ieuid);

    uid_t euid = -1;

    if (idio_isa_libc_uid_t (ieuid)) {
	euid = IDIO_C_TYPE_libc_uid_t (ieuid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/EUID-set-bad-type.idio
	 *
	 * EUID = #t
	 */
	idio_error_param_type ("libc/uid_t", ieuid, IDIO_C_LOCATION ("EUID/set"));

	return idio_S_notreached;
    }

    int r = seteuid (euid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/EUID-set-invalid-gid.idio
	 *
	 * ;; probably a fail...
	 * EUID = (C/integer-> -1 libc/uid_t)
	 */
	idio_error_system_errno ("seteuid", ieuid, IDIO_C_LOCATION ("EUID/set"));

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("GID/get", GID_get, (void), "", "\
getter for the computed value ``GID`` which is a call to	\n\
getgid (2).							\n\
								\n\
:return: real group ID						\n\
:rtype: libc/gid_t						\n\
")
{
    return idio_libc_gid_t (getgid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("GID/set", GID_set, (IDIO igid), "gid", "\
setter for the computed value ``GID`` which is a call to	\n\
setgid (2).							\n\
								\n\
:param gid: real group ID					\n\
:type gid: libc/gid_t						\n\
:return: 0 or raises ^system-error				\n\
:rtype: C/int							\n\
")
{
    IDIO_ASSERT (igid);

    gid_t gid = -1;

    if (idio_isa_libc_gid_t (igid)) {
	gid = IDIO_C_TYPE_libc_gid_t (igid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/GID-set-bad-type.idio
	 *
	 * GID = #t
	 */
	idio_error_param_type ("libc/gid_t", igid, IDIO_C_LOCATION ("GID/set"));

	return idio_S_notreached;
    }

    int r = setgid (gid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/GID-set-invalid-gid.idio
	 *
	 * ;; probably a fail...
	 * GID = (C/integer-> -1 libc/gid_t)
	 */
	idio_error_system_errno ("setgid", igid, IDIO_C_LOCATION ("GID/set"));

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("UID/get", UID_get, (void), "", "\
getter for the computed value ``UID`` which is a call to	\n\
getuid (2).							\n\
								\n\
:return: real user ID						\n\
:rtype: libc/uid_t						\n\
")
{
    return idio_libc_uid_t (getuid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("UID/set", UID_set, (IDIO iuid), "uid", "\
setter for the computed value ``UID`` which is a call to	\n\
setuid (2).							\n\
								\n\
:param uid: real user ID					\n\
:type uid: libc/uid_t						\n\
:return: 0 or raises ^system-error				\n\
:rtype: C/int							\n\
")
{
    IDIO_ASSERT (iuid);

    uid_t uid = -1;

    if (idio_isa_libc_uid_t (iuid)) {
	uid = IDIO_C_TYPE_libc_uid_t (iuid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/UID-set-bad-type.idio
	 *
	 * UID = #t
	 */
	idio_error_param_type ("libc/uid_t", iuid, IDIO_C_LOCATION ("UID/set"));

	return idio_S_notreached;
    }

    int r = setuid (uid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/UID-set-invalid-uid.idio
	 *
	 * ;; probably a fail...
	 * UID = (C/integer-> -1 libc/uid_t)
	 */
	idio_error_system_errno ("setuid", iuid, IDIO_C_LOCATION ("UID/set"));

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("STDIN/get", libc_STDIN_get, (void), "", "\
return the current input handle					\n\
								\n\
:return: current input handle					\n\
:rtype: handle							\n\
")
{
    return idio_thread_current_input_handle ();
}

IDIO_DEFINE_PRIMITIVE0_DS ("STDOUT/get", libc_STDOUT_get, (void), "", "\
return the current output handle				\n\
								\n\
:return: current output handle					\n\
:rtype: handle							\n\
")
{
    return idio_thread_current_output_handle ();
}

IDIO_DEFINE_PRIMITIVE0_DS ("STDERR/get", libc_STDERR_get, (void), "", "\
return the current error handle					\n\
								\n\
:return: current error handle					\n\
:rtype: handle							\n\
")
{
    return idio_thread_current_error_handle ();
}

IDIO idio_libc_wrap_access_predicate (IDIO pathname, char *pred_C, int mode)
{
    IDIO_ASSERT (pathname);
    IDIO_C_ASSERT (pred_C);

    int free_pathname_C = 0;
    char *pathname_C = idio_libc_string_C (pathname, pred_C, &free_pathname_C, IDIO_C_FUNC_LOCATION ());

    IDIO r = idio_S_false;

    if (access (pathname_C, mode) == 0) {
	r = idio_S_true;
    }

    if (free_pathname_C) {
	IDIO_GC_FREE (pathname_C);
    }

    return r;
}

IDIO idio_libc_wrap_stat_predicate (IDIO pathname, char *pred_C, int mask)
{
    IDIO_ASSERT (pathname);
    IDIO_C_ASSERT (pred_C);

    int free_pathname_C = 0;
    char *pathname_C = idio_libc_string_C (pathname, pred_C, &free_pathname_C, IDIO_C_FUNC_LOCATION ());

    struct stat sb;

    IDIO r = idio_S_false;

    if (stat (pathname_C, &sb) == 0 &&
	(sb.st_mode & S_IFMT) == mask) {
	r = idio_S_true;
    }

    if (free_pathname_C) {
	IDIO_GC_FREE (pathname_C);
    }

    return r;
}

IDIO idio_libc_wrap_mode_predicate (IDIO mode, int mask)
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISBLK-bad-type.idio
     *
     * S_ISBLK #t
     */
    IDIO_USER_libc_TYPE_ASSERT (mode_t, mode);
    int C_mode = IDIO_C_TYPE_libc_mode_t (mode);

    IDIO r = idio_S_false;

    if ((C_mode & S_IFMT) == mask) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("b?", libc_bp, (IDIO pathname), "pathname", "\
is `pathname` a block special device?	\n\
					\n\
:param pathname: pathname to stat	\n\
:type pathname: string			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/b-bad-type.idio
     *   libc-wrap-errors/b-bad-format.idio
     *
     * b? #t
     * b? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_stat_predicate (pathname, "b?", S_IFBLK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("S_ISBLK", libc_S_ISBLK, (IDIO mode), "mode", "\
does `mode` represent a block device?	\n\
					\n\
:param mode: mode from stat(2)		\n\
:type mode: libc/mode_t			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISBLK-bad-type.idio
     *
     * S_ISBLK #t
     */
    return idio_libc_wrap_mode_predicate (mode, S_IFBLK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("c?", libc_cp, (IDIO pathname), "pathname", "\
is `pathname` a character special device?	\n\
						\n\
:param pathname: pathname to stat		\n\
:type pathname: string				\n\
:return: #t or #f				\n\
:rtype: boolean					\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/c-bad-type.idio
     *   libc-wrap-errors/c-bad-format.idio
     *
     * c? #t
     * c? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_stat_predicate (pathname, "c?", S_IFCHR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("S_ISCHR", libc_S_ISCHR, (IDIO mode), "mode", "\
does `mode` represent a character device?	\n\
					\n\
:param mode: mode from stat(2)		\n\
:type mode: libc/mode_t			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISCHR-bad-type.idio
     *
     * S_ISCHR #t
     */
    return idio_libc_wrap_mode_predicate (mode, S_IFCHR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("d?", libc_dp, (IDIO pathname), "pathname", "\
is `pathname` a directory?		\n\
					\n\
:param pathname: pathname to stat	\n\
:type pathname: string			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/d-bad-type.idio
     *   libc-wrap-errors/d-bad-format.idio
     *
     * d? #t
     * d? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_stat_predicate (pathname, "d?", S_IFDIR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("S_ISDIR", libc_S_ISDIR, (IDIO mode), "mode", "\
does `mode` represent a directory?	\n\
					\n\
:param mode: mode from stat(2)		\n\
:type mode: libc/mode_t			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISDIR-bad-type.idio
     *
     * S_ISDIR #t
     */
    return idio_libc_wrap_mode_predicate (mode, S_IFDIR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("e?", libc_ep, (IDIO pathname), "pathname", "\
does `pathname` exist?			\n\
					\n\
:param pathname: pathname to stat	\n\
:type pathname: string			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/e-bad-type.idio
     *   libc-wrap-errors/e-bad-format.idio
     *
     * e? #t
     * e? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_access_predicate (pathname, "e?", F_OK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("f?", libc_fp, (IDIO pathname), "pathname", "\
is `pathname` a regular file?		\n\
					\n\
:param pathname: pathname to stat	\n\
:type pathname: string			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/f-bad-type.idio
     *   libc-wrap-errors/f-bad-format.idio
     *
     * f? #t
     * f? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_stat_predicate (pathname, "f?", S_IFREG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("S_ISREG", libc_S_ISREG, (IDIO mode), "mode", "\
does `mode` represent a regular file?	\n\
					\n\
:param mode: mode from stat(2)		\n\
:type mode: libc/mode_t			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISREG-bad-type.idio
     *
     * S_ISREG #t
     */
    return idio_libc_wrap_mode_predicate (mode, S_IFREG);
}

IDIO_DEFINE_PRIMITIVE1_DS ("l?", libc_lp, (IDIO pathname), "pathname", "\
is `pathname` a symlink?		\n\
					\n\
:param pathname: pathname to stat	\n\
:type pathname: string			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (pathname);

    int free_pathname_C = 0;

    /*
     * Test Cases:
     *   libc-wrap-errors/l-bad-type.idio
     *   libc-wrap-errors/l-bad-format.idio
     *
     * l? #t
     * l? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    char *pathname_C = idio_libc_string_C (pathname, "l?", &free_pathname_C, IDIO_C_FUNC_LOCATION ());

    struct stat sb;

    IDIO r = idio_S_false;

    if (lstat (pathname_C, &sb) == 0 &&
	S_ISLNK (sb.st_mode)) {
	r = idio_S_true;
    }

    if (free_pathname_C) {
	IDIO_GC_FREE (pathname_C);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("S_ISLNK", libc_S_ISLNK, (IDIO mode), "mode", "\
does `mode` represent a symbolic link?	\n\
					\n\
:param mode: mode from stat(2)		\n\
:type mode: libc/mode_t			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISLNK-bad-type.idio
     *
     * S_ISLNK #t
     */
    return idio_libc_wrap_mode_predicate (mode, S_IFLNK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("p?", libc_pp, (IDIO pathname), "pathname", "\
is `pathname` a FIFO?			\n\
					\n\
:param pathname: pathname to stat	\n\
:type pathname: string			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/p-bad-type.idio
     *   libc-wrap-errors/p-bad-format.idio
     *
     * p? #t
     * p? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_stat_predicate (pathname, "p?", S_IFIFO);
}

IDIO_DEFINE_PRIMITIVE1_DS ("S_ISFIFO", libc_S_ISFIFO, (IDIO mode), "mode", "\
does `mode` represent a FIFO?	\n\
					\n\
:param mode: mode from stat(2)		\n\
:type mode: libc/mode_t			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISFIFO-bad-type.idio
     *
     * S_ISFIFO #t
     */
    return idio_libc_wrap_mode_predicate (mode, S_IFIFO);
}

IDIO_DEFINE_PRIMITIVE1_DS ("r?", libc_rp, (IDIO pathname), "pathname", "\
does `pathname` pass `access (pathname, R_OK)`?	\n\
						\n\
:param pathname: file to test			\n\
:type pathname: string				\n\
						\n\
:return: #t or #f				\n\
:rtype: boolean					\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/r-bad-type.idio
     *   libc-wrap-errors/r-bad-format.idio
     *
     * r? #t
     * r? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_access_predicate (pathname, "r?", R_OK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("s?", libc_sp, (IDIO pathname), "pathname", "\
is `pathname` a socket?			\n\
					\n\
:param pathname: pathname to stat	\n\
:type pathname: string			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/s-bad-type.idio
     *   libc-wrap-errors/s-bad-format.idio
     *
     * s? #t
     * s? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_stat_predicate (pathname, "s?", S_IFSOCK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("S_ISSOCK", libc_S_ISSOCK, (IDIO mode), "mode", "\
does `mode` represent a socket?		\n\
					\n\
:param mode: mode from stat(2)		\n\
:type mode: libc/mode_t			\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (mode);

    /*
     * Test Case: libc-wrap-errors/S_ISSOCK-bad-type.idio
     *
     * S_ISSOCK #t
     */
    return idio_libc_wrap_mode_predicate (mode, S_IFSOCK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("T?", libc_Tp, (IDIO fd), "fd", "\
is `fd` a terminal?			\n\
					\n\
:param fd: file descriptor to stat	\n\
:type fd: C/int				\n\
:return: #t or #f			\n\
:rtype: boolean				\n\
")
{
    IDIO_ASSERT (fd);

    /*
     * Test Case: libc-wrap-errors/t-bad-type.idio
     *
     * T? #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, fd);

    IDIO r = idio_S_false;

    if (isatty (IDIO_C_TYPE_int (fd)) == 0) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("w?", libc_wp, (IDIO pathname), "pathname", "\
does `pathname` pass `access (pathname, W_OK)`?	\n\
						\n\
:param pathname: file to test			\n\
:type pathname: string				\n\
						\n\
:return: #t or #f				\n\
:rtype: boolean					\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/w-bad-type.idio
     *   libc-wrap-errors/w-bad-format.idio
     *
     * w? #t
     * w? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_access_predicate (pathname, "w?", W_OK);
}

IDIO_DEFINE_PRIMITIVE1_DS ("x?", libc_xp, (IDIO pathname), "pathname", "\
does `pathname` pass `access (pathname, X_OK)`?	\n\
						\n\
:param pathname: file to test			\n\
:type pathname: string				\n\
						\n\
:return: #t or #f				\n\
:rtype: boolean					\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Cases:
     *   libc-wrap-errors/x-bad-type.idio
     *   libc-wrap-errors/x-bad-format.idio
     *
     * x? #t
     * x? (join-string (make-string 1 #U+0) '("hello" "world"))
     */
    return idio_libc_wrap_access_predicate (pathname, "x?", X_OK);
}

void idio_libc_wrap_add_primitives ()
{
    idio_libc_api_add_primitives ();

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_add_struct_timeval);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_subtract_struct_timeval);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_system_error);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_exit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_pipe_reader);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_pipe_writer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, proc_subst_named_pipe);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_signal_handler);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_WEXITSTATUS);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_WIFEXITED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_WIFSIGNALED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_WIFSTOPPED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_WTERMSIG);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_sig_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_sig_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_signal_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_signal_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_errno_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_errno_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_strerrno);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_rlimit_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_rlimit_names);

    IDIO_ADD_PRIMITIVE (libc_bp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_S_ISBLK);
    IDIO_ADD_PRIMITIVE (libc_cp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_S_ISCHR);
    IDIO_ADD_PRIMITIVE (libc_dp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_S_ISDIR);
    IDIO_ADD_PRIMITIVE (libc_ep);
    IDIO_ADD_PRIMITIVE (libc_fp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_S_ISREG);
    IDIO_ADD_PRIMITIVE (libc_lp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_S_ISLNK);
    IDIO_ADD_PRIMITIVE (libc_pp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_S_ISFIFO);
    IDIO_ADD_PRIMITIVE (libc_rp);
    IDIO_ADD_PRIMITIVE (libc_sp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_S_ISSOCK);
    IDIO_ADD_PRIMITIVE (libc_Tp);
    IDIO_ADD_PRIMITIVE (libc_wp);
    IDIO_ADD_PRIMITIVE (libc_xp);
}

void idio_final_libc_wrap ()
{
    int i;

    for (i = IDIO_LIBC_FSIG; NULL != idio_libc_signal_names[i]; i++) {
        IDIO_GC_FREE (idio_libc_signal_names[i]);
    }
    IDIO_GC_FREE (idio_libc_signal_names);

    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO; i++) {
        IDIO_GC_FREE (idio_libc_errno_names[i]);
    }
    IDIO_GC_FREE (idio_libc_errno_names);

    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT; i++) {
        IDIO_GC_FREE (idio_libc_rlimit_names[i]);
    }
    IDIO_GC_FREE (idio_libc_rlimit_names);
}

void idio_init_libc_wrap ()
{
    idio_module_table_register (idio_libc_wrap_add_primitives, idio_final_libc_wrap);

    idio_libc_module = idio_module (idio_symbols_C_intern ("libc"));

    /*
     * XXX *after* creating the module!
     */
    idio_init_libc_api ();

    idio_module_export_symbol_value (idio_symbols_C_intern ("0pid_t"), idio_libc_pid_t (0), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("0gid_t"), idio_libc_gid_t (0), idio_libc_module);

    /* fcntl.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("FD_CLOEXEC"), idio_C_int (FD_CLOEXEC), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD"), idio_C_int (F_DUPFD), idio_libc_module);
#if defined (F_DUPFD_CLOEXEC)
    idio_add_feature (idio_symbols_C_intern ("F_DUPFD_CLOEXEC"));
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD_CLOEXEC"), idio_C_int (F_DUPFD_CLOEXEC), idio_libc_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_GETFD"), idio_C_int (F_GETFD), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_SETFD"), idio_C_int (F_SETFD), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_GETFL"), idio_C_int (F_GETFL), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_SETFL"), idio_C_int (F_SETFL), idio_libc_module);

    idio_module_export_symbol_value (idio_symbols_C_intern ("O_RDONLY"), idio_C_int (O_RDONLY), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_WRONLY"), idio_C_int (O_WRONLY), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_RDWR"), idio_C_int (O_RDWR), idio_libc_module);

    /* available on all platforms */
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_APPEND"), idio_C_int (O_APPEND), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_CLOEXEC"), idio_C_int (O_CLOEXEC), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_CREAT"), idio_C_int (O_CREAT), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_EXCL"), idio_C_int (O_EXCL), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_NDELAY"), idio_C_int (O_NDELAY), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_NOFOLLOW"), idio_C_int (O_NOFOLLOW), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_NONBLOCK"), idio_C_int (O_NONBLOCK), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_TRUNC"), idio_C_int (O_TRUNC), idio_libc_module);

    idio_module_export_symbol_value (idio_symbols_C_intern ("FD_CLOEXEC"), idio_C_int (FD_CLOEXEC), idio_libc_module);

    /* available on some platforms */
#if defined(O_ASYNC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_ASYNC"), idio_C_int (O_ASYNC), idio_libc_module);
#endif
#if defined(O_DIRECT)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_DIRECT"), idio_C_int (O_DIRECT), idio_libc_module);
#endif
#if defined(O_DIRECTORY)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_DIRECTORY"), idio_C_int (O_DIRECTORY), idio_libc_module);
#endif
#if defined(O_DSYNC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_DSYNC"), idio_C_int (O_DSYNC), idio_libc_module);
#endif
#if defined(O_EXEC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_EXEC"), idio_C_int (O_EXEC), idio_libc_module);
#endif
#if defined(O_EXLOCK)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_EXLOCK"), idio_C_int (O_EXLOCK), idio_libc_module);
#endif
#if defined(O_FSYNC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_FSYNC"), idio_C_int (O_FSYNC), idio_libc_module);
#endif
#if defined(O_LARGEFILE)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_LARGEFILE"), idio_C_int (O_LARGEFILE), idio_libc_module);
#endif
#if defined(O_NOATIME)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_NOATIME"), idio_C_int (O_NOATIME), idio_libc_module);
#endif
#if defined(O_NOCTTY)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_NOCTTY"), idio_C_int (O_NOCTTY), idio_libc_module);
#endif
#if defined(O_PATH)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_PATH"), idio_C_int (O_PATH), idio_libc_module);
#endif
#if defined(O_SEARCH)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_SEARCH"), idio_C_int (O_SEARCH), idio_libc_module);
#endif
#if defined(O_SHLOCK)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_SHLOCK"), idio_C_int (O_SHLOCK), idio_libc_module);
#endif
#if defined(O_SYNC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_SYNC"), idio_C_int (O_SYNC), idio_libc_module);
#endif
#if defined(O_TMPFILE)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_TMPFILE"), idio_C_int (O_TMPFILE), idio_libc_module);
#endif

    /*
     * The following are single platform only:
     *
     * FreeBSD: O_VERIFY O_RESOLVE_BENEATH
     *
     * SunOS: O_NOLINKS O_RSYNC O_XATTR
     *
     * Mac OS: O_EVTONLY
     */
#if defined(O_VERIFY)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_VERIFY"), idio_C_int (O_VERIFY), idio_libc_module);
#endif
#if defined(O_RESOLVE_BENEATH)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_RESOLVE_BENEATH"), idio_C_int (O_RESOLVE_BENEATH), idio_libc_module);
#endif
#if defined(O_NOLINNKS)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_NOLINNKS"), idio_C_int (O_NOLINNKS), idio_libc_module);
#endif
#if defined(O_RSYNC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_RSYNC"), idio_C_int (O_RSYNC), idio_libc_module);
#endif
#if defined(O_XATTR)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_XATTR"), idio_C_int (O_XATTR), idio_libc_module);
#endif
#if defined(O_EVTONLY)
    idio_module_export_symbol_value (idio_symbols_C_intern ("O_EVTONLY"), idio_C_int (O_EVTONLY), idio_libc_module);
#endif

    /* limits.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("CHAR_MAX"), idio_C_char (CHAR_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SCHAR_MIN"), idio_C_schar (SCHAR_MIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SCHAR_MAX"), idio_C_schar (SCHAR_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("UCHAR_MAX"), idio_C_uchar (UCHAR_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SHRT_MIN"), idio_C_short (SHRT_MIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SHRT_MAX"), idio_C_short (SHRT_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("USHRT_MAX"), idio_C_ushort (USHRT_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INT_MIN"), idio_C_int (INT_MIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INT_MAX"), idio_C_int (INT_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("UINT_MAX"), idio_C_uint (UINT_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("LONG_MIN"), idio_C_long (LONG_MIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("LONG_MAX"), idio_C_long (LONG_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("ULONG_MAX"), idio_C_ulong (ULONG_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("LLONG_MIN"), idio_C_longlong (LLONG_MIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("LLONG_MAX"), idio_C_longlong (LLONG_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("ULLONG_MAX"), idio_C_ulonglong (ULLONG_MAX), idio_libc_module);


    /* signal.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_DFL"), idio_C_pointer (SIG_DFL), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_IGN"), idio_C_pointer (SIG_IGN), idio_libc_module);

    /* stdio.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("BUFSIZ"), idio_C_int (BUFSIZ), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("EOF"), idio_C_int (EOF), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("NULL"), idio_C_pointer (NULL), idio_libc_module);

    /* stdint.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTPTR_MIN"), idio_libc_intptr_t (INTPTR_MIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTPTR_MAX"), idio_libc_intptr_t (INTPTR_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MIN"), idio_libc_intmax_t (INTMAX_MIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MAX"), idio_libc_intmax_t (INTMAX_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("UINTMAX_MAX"), idio_libc_uintmax_t (UINTMAX_MAX), idio_libc_module);

    /* sys/resource.h */
    /*
     * NB RLIM_SAVED_* not defined in FreeBSD (10)
     */
#ifdef RLIM_SAVED_MAX
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_MAX"), idio_libc_rlim_t (RLIM_SAVED_MAX), idio_libc_module);
#endif
#ifdef RLIM_SAVED_CUR
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_CUR"), idio_libc_rlim_t (RLIM_SAVED_CUR), idio_libc_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_INFINITY"), idio_libc_rlim_t (RLIM_INFINITY), idio_libc_module);

    /* sys/wait.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("WAIT_ANY"), idio_libc_pid_t (WAIT_ANY), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WNOHANG"), idio_C_int (WNOHANG), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WUNTRACED"), idio_C_int (WUNTRACED), idio_libc_module);

    /* termios.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSADRAIN"), idio_C_int (TCSADRAIN), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSAFLUSH"), idio_C_int (TCSAFLUSH), idio_libc_module);

    /* unistd.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("PATH_MAX"), idio_C_int (PATH_MAX), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDIN_FILENO"), idio_C_int (STDIN_FILENO), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDOUT_FILENO"), idio_C_int (STDOUT_FILENO), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDERR_FILENO"), idio_C_int (STDERR_FILENO), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("R_OK"), idio_C_int (R_OK), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("W_OK"), idio_C_int (W_OK), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("X_OK"), idio_C_int (X_OK), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_OK"), idio_C_int (F_OK), idio_libc_module);

    IDIO geti;
    IDIO seti;
    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, libc_errno_get);
    idio_module_export_computed_symbol (idio_symbols_C_intern ("errno"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, libc_STDIN_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDIN"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, libc_STDOUT_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDOUT"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, libc_STDERR_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDERR"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_module);

    idio_vm_signal_handler_conditions = idio_array (IDIO_LIBC_NSIG + 1);
    idio_gc_protect_auto (idio_vm_signal_handler_conditions);
    /*
     * idio_vm_run1() will be indexing anywhere into this array when
     * it gets a signal so make sure that the "used" size is up there
     * by poking something at at NSIG.
     */
    idio_array_insert_index (idio_vm_signal_handler_conditions, idio_S_nil, (idio_ai_t) IDIO_LIBC_NSIG);
    idio_libc_set_signal_names ();

    idio_vm_errno_conditions = idio_array (IDIO_LIBC_NERRNO + 1);
    idio_gc_protect_auto (idio_vm_errno_conditions);
    idio_array_insert_index (idio_vm_errno_conditions, idio_S_nil, (idio_ai_t) IDIO_LIBC_NERRNO);
    idio_libc_set_errno_names ();

    idio_libc_set_rlimit_names ();

    /*
     * Define some host/user/process variables
     */
    IDIO main_module = idio_Idio_module_instance ();

    struct utsname *up = idio_alloc (sizeof (struct utsname));
    if (uname (up) == -1) {
	/*
	 * Test Case: ??
	 *
	 * EFAULT buf is not valid.
	 */
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    idio_module_export_symbol_value (idio_symbols_C_intern ("idio-uname"), idio_C_pointer_type (idio_CSI_libc_struct_utsname, up), idio_libc_module);


    if (getenv ("HOSTNAME") == NULL) {
	idio_module_set_symbol_value (idio_symbols_C_intern ("HOSTNAME"), idio_string_C (up->nodename), main_module);
    }

    idio_add_feature_ps ("uname/sysname/", up->sysname);
    idio_add_feature_ps ("uname/nodename/", up->nodename);
    idio_add_feature_ps ("uname/release/", up->release);
    /* idio_add_feature (idio_string_C (up->version)); */
    idio_add_feature_ps ("uname/machine/", up->machine);
    idio_add_feature_pi ("sizeof/pointer/", sizeof (void *) * CHAR_BIT);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, UID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, UID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("UID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, EUID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, EUID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EUID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, GID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, GID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("GID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, EGID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_module, EGID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EGID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    int ngroups = getgroups (0, (gid_t *) NULL);

    if (-1 == ngroups) {
	/*
	 * Test Case: ??
	 *
	 * Can getgroups(2) fail with 0/NULL args?
	 */
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    gid_t *grp_list = idio_alloc (ngroups * sizeof (gid_t));

    int ng = getgroups (ngroups, grp_list);
    if (-1 == ng) {
	/*
	 * Test Case: ??
	 *
	 * Can getgroups(2) fail with 0/NULL args?
	 */
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (ngroups != ng) {
	/*
	 * Test Case: ??
	 *
	 * Could this ever happen?
	 */
	idio_error_C ("getgroups", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO GROUPS = idio_array (ngroups);

    for (ng = 0; ng < ngroups ; ng++) {
	idio_array_insert_index (GROUPS, idio_libc_gid_t (grp_list[ng]), ng);
    }
    IDIO_GC_FREE (grp_list);

    idio_module_set_symbol_value (idio_symbols_C_intern ("GROUPS"), GROUPS, main_module);

    idio_module_set_symbol_value (idio_symbols_C_intern ("IDIO_PID"), idio_libc_pid_t (getpid ()), main_module);
    idio_module_set_symbol_value (idio_symbols_C_intern ("PID"), idio_libc_pid_t (getpid ()), main_module);
    idio_module_set_symbol_value (idio_symbols_C_intern ("PPID"), idio_libc_pid_t (getppid ()), main_module);

    /*
     * POSIX times(3) uses clock_t which is measured in terms of the
     * number of clock ticks used.
     */
    idio_SC_CLK_TCK = sysconf (_SC_CLK_TCK);
    if (-1 == idio_SC_CLK_TCK){
	/*
	 * Test Case: ??
	 *
	 * _SC_CLK_TCK is wrong?
	 */
	idio_error_system_errno_msg ("sysconf", "(_SC_CLK_TCK)", idio_C_long (idio_SC_CLK_TCK), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    idio_module_export_symbol_value (idio_symbols_C_intern ("CLK_TCK"), idio_C_long (idio_SC_CLK_TCK), idio_libc_module);

    idio_module_export_symbol_value (idio_symbols_C_intern ("RUSAGE_SELF"), idio_C_int (RUSAGE_SELF), idio_libc_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("RUSAGE_CHILDREN"), idio_C_int (RUSAGE_CHILDREN), idio_libc_module);
    /*
     * It's not clear how portable the RUSAGE_THREAD parameter is.  In
     * Linux it requires the _GNU_SOURCE feature test macro.  In
     * FreeBSD 10 you might need to define it yourself (as 1).  It is
     * not in OpenIndiana.
     */
}

