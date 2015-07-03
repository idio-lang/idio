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
 * libc-wrap.c
 * 
 */

#include "idio.h"

char **idio_libc_signal_names = NULL;

IDIO_DEFINE_PRIMITIVE1 ("c/close", C_close, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = close (fd);

    if (-1 == r) {
	idio_error_system_errno ("close", ifd);
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("c/dup2", C_dup2, (IDIO ioldfd, IDIO inewfd))
{
    IDIO_ASSERT (ioldfd);
    IDIO_ASSERT (inewfd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ioldfd);
    IDIO_VERIFY_PARAM_TYPE (C_int, inewfd);

    int oldfd = IDIO_C_TYPE_INT (ioldfd);
    int newfd = IDIO_C_TYPE_INT (inewfd);

    int r = dup2 (oldfd, newfd);

    if (-1 == r) {
	idio_error_system_errno ("dup2", IDIO_LIST2 (ioldfd, inewfd));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("c/exit", C_exit, (IDIO istatus))
{
    IDIO_ASSERT (istatus);

    int status = 0;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else {
	idio_error_param_type ("fixnum", istatus);
    }

    exit (status);

    /* notreached */
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0 ("c/fork", C_fork, ())
{
    pid_t pid = fork ();

    if (-1 == pid) {
	idio_error_system_errno ("fork", idio_S_nil);
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("c/getcwd", C_getcwd, ())
{
    
    char *cwd = getcwd (NULL, PATH_MAX);

    if (NULL == cwd) {
	idio_error_system_errno ("getcwd", idio_S_nil);
    }

    IDIO r = idio_string_C (cwd);
    free (cwd);

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("c/getpgrp", C_getpgrp, ())
{
    pid_t pid = getpgrp ();

    if (-1 == pid) {
	idio_error_system_errno ("getpgrp", idio_S_nil);
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("c/getpid", C_getpid, ())
{
    pid_t pid = getpid ();

    if (-1 == pid) {
	idio_error_system_errno ("getpid", idio_S_nil);
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE1 ("c/isatty", C_isatty, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = isatty (fd);

    if (0 == r) {
	idio_error_system_errno ("isatty", ifd);
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("c/kill", C_kill, (IDIO ipid, IDIO isig))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (isig);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);
    
    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int sig = IDIO_C_TYPE_INT (isig);

    int r = kill (pid, sig);
    
    if (-1 == r) {
	idio_error_system_errno ("kill", IDIO_LIST2 (ipid, isig));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0 ("c/pipe", C_pipe, ())
{
    int *pipefd = idio_alloc (2 * sizeof (int));
    
    int r = pipe (pipefd);

    if (-1 == r) {
	idio_error_system_errno ("pipe", idio_S_nil);
    }

    return idio_C_pointer_free_me (pipefd);
}

IDIO_DEFINE_PRIMITIVE1 ("c/pipe-reader", C_pipe_reader, (IDIO ipipefd))
{
    IDIO_ASSERT (ipipefd);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ipipefd);
    
    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);
    
    return idio_C_int (pipefd[0]);
}

IDIO_DEFINE_PRIMITIVE1 ("c/pipe-writer", C_pipe_writer, (IDIO ipipefd))
{
    IDIO_ASSERT (ipipefd);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ipipefd);
    
    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);
    
    return idio_C_int (pipefd[1]);
}

IDIO_DEFINE_PRIMITIVE1V ("c/read", C_read, (IDIO ifd, IDIO icount))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    size_t count = BUFSIZ;

    if (idio_S_nil != icount) {
	icount = IDIO_PAIR_H (icount);
	
	if (idio_isa_fixnum (icount)) {
	    count = IDIO_FIXNUM_VAL (icount);
	} else if (idio_isa_C_int (icount)) {
	    count = IDIO_C_TYPE_INT (icount);
	} else {
	    idio_error_param_type ("fixnum|C_int", icount);
	}
    }

    char buf[count];

    ssize_t n = read (fd, buf, count);

    IDIO r;
    
    if (n) {
	r = idio_string_C_len (buf, n);
    } else {
	r = idio_S_eof;
    }
    
    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("c/setpgid", C_setpgid, (IDIO ipid, IDIO ipgid))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (ipgid);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipgid);
    
    pid_t pid = IDIO_C_TYPE_INT (ipid);
    pid_t pgid = IDIO_C_TYPE_INT (ipgid);

    int r = setpgid (pid, pgid);
    
    if (-1 == r) {
	if (EACCES == errno) {
	    /* 
	     * The child has already successfully executed exec() =>
	     * EACCES for us.
	     *
	     * Since the child also ran setpgid() on itself before
	     * calling exec() we should be good.
	     */
	    r = 0;
	} else {
	    idio_error_system_errno ("setpgid", IDIO_LIST2 (ipid, ipgid));
	}
    }
    
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("c/signal", C_signal, (IDIO isig, IDIO ifunc))
{
    IDIO_ASSERT (isig);
    IDIO_ASSERT (ifunc);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ifunc);
    
    int sig = IDIO_C_TYPE_INT (isig);
    void (*func) (int) = IDIO_C_TYPE_POINTER_P (ifunc);

    void (*r) (int) = signal (sig, func);
    
    if (SIG_ERR == r) {
	idio_error_system_errno ("signal", IDIO_LIST2 (isig, ifunc));
    }

    return idio_C_pointer (r);
}

IDIO_DEFINE_PRIMITIVE1 ("c/sleep", C_sleep, (IDIO iseconds))
{
    IDIO_ASSERT (iseconds);
    
    unsigned int seconds = 0;
    if (idio_isa_fixnum (iseconds) &&
	IDIO_FIXNUM_VAL (iseconds) >= 0) {
	seconds = IDIO_FIXNUM_VAL (iseconds);
    } else if (idio_isa_C_uint (iseconds)) {
	seconds = IDIO_C_TYPE_UINT (iseconds);
    } else {
	idio_error_param_type ("unsigned fixnum|C_uint", iseconds);
    }

    unsigned int r = sleep (seconds);
    
    return idio_C_uint (r);
}

IDIO_DEFINE_PRIMITIVE1 ("c/strsignal", C_strsignal, (IDIO isignum))
{
    IDIO_ASSERT (isignum);

    int signum = 0;
    if (idio_isa_fixnum (isignum)) {
	signum = IDIO_FIXNUM_VAL (isignum);
    } else if (idio_isa_C_int (isignum)) {
	signum = IDIO_C_TYPE_INT (isignum);
    } else {
	idio_error_param_type ("unsigned fixnum|C_int", isignum);
    }

    char *r = strsignal (signum);
    
    return idio_string_C (r);
}

IDIO_DEFINE_PRIMITIVE1 ("c/tcgetattr", C_tcgetattr, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    struct termios *tcattrs = idio_alloc (sizeof (struct termios));
    int r = tcgetattr (fd, tcattrs);

    if (-1 == r) {
	idio_error_system_errno ("tcgetattr", ifd);
    }

    return idio_C_pointer_free_me (tcattrs);
}

IDIO_DEFINE_PRIMITIVE1 ("c/tcgetpgrp", C_tcgetpgrp, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    pid_t pid = tcgetpgrp (fd);

    if (-1 == pid) {
	idio_error_system_errno ("tcgetpgrp", ifd);
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE3 ("c/tcsetattr", C_tcsetattr, (IDIO ifd, IDIO ioptions, IDIO itcattrs))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (ioptions);
    IDIO_ASSERT (itcattrs);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ioptions);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, itcattrs);

    int fd = IDIO_C_TYPE_INT (ifd);
    int options = IDIO_C_TYPE_INT (ioptions);
    struct termios *tcattrs = IDIO_C_TYPE_POINTER_P (itcattrs);
    
    int r = tcsetattr (fd, options, tcattrs);

    if (-1 == r) {
	idio_error_system_errno ("tcsetattr", IDIO_LIST3 (ifd, ioptions, itcattrs));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("c/tcsetpgrp", C_tcsetpgrp, (IDIO ifd, IDIO ipgrp))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (ipgrp);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipgrp);

    int fd = IDIO_C_TYPE_INT (ifd);
    pid_t pgrp = IDIO_C_TYPE_INT (ipgrp);
    

    int r = tcsetpgrp (fd, pgrp);

    if (-1 == r) {
	idio_error_system_errno ("tcsetpgrp", IDIO_LIST2 (ifd, ipgrp));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("c/waitpid", C_waitpid, (IDIO ipid, IDIO ioptions))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (ioptions);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, ioptions);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int options = IDIO_C_TYPE_INT (ioptions);

    int *statusp = idio_alloc (sizeof (int));

    pid_t r = waitpid (pid, statusp, options);

    if (-1 == r) {
	if (ECHILD == errno) {
	    return IDIO_LIST2 (idio_C_int (0), idio_S_nil);
	}
	idio_error_system_errno ("waitpid", IDIO_LIST2 (ipid, ioptions));
    }

    IDIO istatus = idio_C_pointer_free_me (statusp);

    return IDIO_LIST2 (idio_C_int (r), istatus);
}

IDIO_DEFINE_PRIMITIVE1 ("c/WEXITSTATUS", C_WEXITSTATUS, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WEXITSTATUS (*statusp));
}

IDIO_DEFINE_PRIMITIVE1 ("c/WIFEXITED", C_WIFEXITED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFEXITED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("c/WIFSIGNALED", C_WIFSIGNALED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSIGNALED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("c/WIFSTOPPED", C_WIFSTOPPED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSTOPPED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("c/WTERMSIG", C_WTERMSIG, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WTERMSIG (*statusp));
}

IDIO_DEFINE_PRIMITIVE2 ("c/write", C_write, (IDIO ifd, IDIO istr))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (istr);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (string, istr);

    int fd = IDIO_C_TYPE_INT (ifd);

    size_t blen = idio_string_blen (istr);
    
    ssize_t n = write (fd, idio_string_s (istr), blen);

    if (-1 == n) {
	idio_error_system_errno ("write", IDIO_LIST2 (ifd, istr));
    }

    return idio_integer (n);
}

IDIO_DEFINE_PRIMITIVE1V ("c/|", C_bw_or, (IDIO v1, IDIO args))
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (C_int, v1);

    int r = IDIO_C_TYPE_INT (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_VERIFY_PARAM_TYPE (C_int, arg);
	r = r | IDIO_C_TYPE_INT (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1V ("c/&", C_bw_and, (IDIO v1, IDIO args))
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (C_int, v1);

    int r = IDIO_C_TYPE_INT (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_VERIFY_PARAM_TYPE (C_int, arg);
	r = r & IDIO_C_TYPE_INT (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1V ("c/^", C_bw_xor, (IDIO v1, IDIO args))
{
    IDIO_ASSERT (v1);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (C_int, v1);

    int r = IDIO_C_TYPE_INT (v1);
    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);
	IDIO_VERIFY_PARAM_TYPE (C_int, arg);
	r = r ^ IDIO_C_TYPE_INT (arg);
	args = IDIO_PAIR_T (args);
    }
    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("c/~", C_bw_complement, (IDIO v1))
{
    IDIO_ASSERT (v1);
    IDIO_VERIFY_PARAM_TYPE (C_int, v1);

    int v = IDIO_C_TYPE_INT (v1);

    return idio_C_int (~ v);
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
 * How many signals are there?
 *
 * Linux, OpenSolaris and Mac OS X all seem to define NSIG as the
 * highest signal number.  On FreeBSD, NSIG is the "number of old
 * signals".  SIGRT* are in a range of their own.
 */

#if defined (BSD)
#define IDIO_LIBC_NSIG (SIGRTMAX + 1)
#else
#define IDIO_LIBC_NSIG NSIG
#endif

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
    for (i = 0; i < IDIO_LIBC_NSIG; i++) {
	idio_libc_signal_names[i] = idio_alloc (IDIO_LIBC_SIGNAMELEN);
	*(idio_libc_signal_names[i]) = '\0';
    }
    idio_libc_signal_names[i] = NULL;

#if defined(SIGRTMIN) && defined(SIGRTMAX)    
    int rtmin = SIGRTMIN;
    int rtmax = SIGRTMAX;
    if (rtmax > rtmin &&
	(rtmax - rtmin) > 7) {
        sprintf (idio_libc_signal_names[SIGRTMIN], "SIGRTMIN");
	sprintf (idio_libc_signal_names[SIGRTMAX], "SIGRTMAX");

	int rtmid = (rtmax - rtmin) / 2;
	for (i = 1; i < rtmid ; i++) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	    sprintf (idio_libc_signal_names[rtmax - i], "SIGRTMAX-%d", i);
	}
	
	/*
	 * Can have an extra SIGRTMIN+n if there's an odd number
	 */
	if ((rtmax - rtmin + 1) - (rtmid * 2)) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	}
    }
#endif

#if defined(SIGHUP)
    sprintf (idio_libc_signal_names[SIGHUP], "SIGHUP");
#endif

#if defined(SIGINT)
    sprintf (idio_libc_signal_names[SIGINT], "SIGINT");
#endif

#if defined(SIGQUIT)
    sprintf (idio_libc_signal_names[SIGQUIT], "SIGQUIT");
#endif

#if defined(SIGILL)
    sprintf (idio_libc_signal_names[SIGILL], "SIGILL");
#endif

#if defined(SIGTRAP)
    sprintf (idio_libc_signal_names[SIGTRAP], "SIGTRAP");
#endif

#if defined(SIGIOT)
    sprintf (idio_libc_signal_names[SIGIOT], "SIGIOT");
#endif

#if defined(SIGEMT)
    sprintf (idio_libc_signal_names[SIGEMT], "SIGEMT");
#endif

#if defined(SIGFPE)
    sprintf (idio_libc_signal_names[SIGFPE], "SIGFPE");
#endif

#if defined(SIGKILL)
    sprintf (idio_libc_signal_names[SIGKILL], "SIGKILL");
#endif

#if defined(SIGBUS)
    sprintf (idio_libc_signal_names[SIGBUS], "SIGBUS");
#endif

#if defined(SIGSEGV)
    sprintf (idio_libc_signal_names[SIGSEGV], "SIGSEGV");
#endif

#if defined(SIGSYS)
    sprintf (idio_libc_signal_names[SIGSYS], "SIGSYS");
#endif

#if defined(SIGPIPE)
    sprintf (idio_libc_signal_names[SIGPIPE], "SIGPIPE");
#endif

#if defined(SIGALRM)
    sprintf (idio_libc_signal_names[SIGALRM], "SIGALRM");
#endif

#if defined(SIGTERM)
    sprintf (idio_libc_signal_names[SIGTERM], "SIGTERM");
#endif

#if defined(SIGUSR1)
    sprintf (idio_libc_signal_names[SIGUSR1], "SIGUSR1");
#endif

#if defined(SIGUSR2)
    sprintf (idio_libc_signal_names[SIGUSR2], "SIGUSR2");
#endif

#if defined(SIGCHLD)
    sprintf (idio_libc_signal_names[SIGCHLD], "SIGCHLD");
#endif

#if defined(SIGPWR)
    sprintf (idio_libc_signal_names[SIGPWR], "SIGPWR");
#endif

#if defined(SIGWINCH)
    sprintf (idio_libc_signal_names[SIGWINCH], "SIGWINCH");
#endif

#if defined(SIGURG)
    sprintf (idio_libc_signal_names[SIGURG], "SIGURG");
#endif

#if defined(SIGPOLL)
    sprintf (idio_libc_signal_names[SIGPOLL], "SIGPOLL");
#endif

#if defined(SIGSTOP)
    sprintf (idio_libc_signal_names[SIGSTOP], "SIGSTOP");
#endif

#if defined(SIGTSTP)
    sprintf (idio_libc_signal_names[SIGTSTP], "SIGTSTP");
#endif

#if defined(SIGCONT)
    sprintf (idio_libc_signal_names[SIGCONT], "SIGCONT");
#endif

#if defined(SIGTTIN)
    sprintf (idio_libc_signal_names[SIGTTIN], "SIGTTIN");
#endif

#if defined(SIGTTOU)
    sprintf (idio_libc_signal_names[SIGTTOU], "SIGTTOU");
#endif

#if defined(SIGVTALRM)
    sprintf (idio_libc_signal_names[SIGVTALRM], "SIGVTALRM");
#endif

#if defined(SIGPROF)
    sprintf (idio_libc_signal_names[SIGPROF], "SIGPROF");
#endif

#if defined(SIGXCPU)
    sprintf (idio_libc_signal_names[SIGXCPU], "SIGXCPU");
#endif

#if defined(SIGXFSZ)
    sprintf (idio_libc_signal_names[SIGXFSZ], "SIGXFSZ");
#endif

    /* SunOS */
#if defined(SIGWAITING)
    sprintf (idio_libc_signal_names[SIGWAITING], "SIGWAITING");
#endif

    /* SunOS */
#if defined(SIGLWP)
    sprintf (idio_libc_signal_names[SIGLWP], "SIGLWP");
#endif

    /* SunOS */
#if defined(SIGFREEZE)
    sprintf (idio_libc_signal_names[SIGFREEZE], "SIGFREEZE");
#endif

    /* SunOS */
#if defined(SIGTHAW)
    sprintf (idio_libc_signal_names[SIGTHAW], "SIGTHAW");
#endif

    /* SunOS */
#if defined(SIGCANCEL)
    sprintf (idio_libc_signal_names[SIGCANCEL], "SIGCANCEL");
#endif

    /* SunOS */
#if defined(SIGLOST)
    sprintf (idio_libc_signal_names[SIGLOST], "SIGLOST");
#endif

    /* SunOS */
#if defined(SIGXRES)
    sprintf (idio_libc_signal_names[SIGXRES], "SIGXRES");
#endif

    /* SunOS */
#if defined(SIGJVM1)
    sprintf (idio_libc_signal_names[SIGJVM1], "SIGJVM1");
#endif

    /* SunOS */
#if defined(SIGJVM2)
    sprintf (idio_libc_signal_names[SIGJVM2], "SIGJVM2");
#endif

    /* Linux */
#if defined(SIGSTKFLT)
    sprintf (idio_libc_signal_names[SIGSTKFLT], "SIGSTKFLT");
#endif

    for (i = 0 ; i < IDIO_LIBC_NSIG ; i++) {
	if ('\0' == *(idio_libc_signal_names[i])) {
	    sprintf (idio_libc_signal_names[i], "SIGJUNK%d", i);
	}
    }
}

char *idio_libc_sig_name (int signum)
{
    if (signum < 1 ||
	signum > IDIO_LIBC_NSIG) {
	idio_error_param_type ("int < NSIG (or SIGRTMAX)", idio_C_int (signum));
    }

    char *signame = idio_libc_signal_names[signum];

    if (strncmp (signame, "SIG", 3) == 0) {
	return (signame + 3);
    } else {
	return signame;
    }
}

IDIO_DEFINE_PRIMITIVE1 ("c/sig-name", C_sig_name, (IDIO isignum))
{
    IDIO_ASSERT (isignum);
    IDIO_VERIFY_PARAM_TYPE (C_int, isignum);

    return idio_string_C (idio_libc_sig_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE1 ("c/->integer", C_to_integer, (IDIO inum))
{
    IDIO_ASSERT (inum);

    if (idio_isa_C_uint (inum)) {
	return idio_uinteger (IDIO_C_TYPE_UINT (inum));
    } else if (idio_isa_C_int (inum)) {
	return idio_integer (IDIO_C_TYPE_INT (inum));
    } else {
	idio_error_param_type ("C_int|C_uint", inum);
    }

    /* notreached */
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("c/integer->", C_integer_to, (IDIO inum))
{
    IDIO_ASSERT (inum);

    if (idio_isa_fixnum (inum)) {
	return idio_C_int (IDIO_FIXNUM_VAL (inum));
    } else if (idio_isa_bignum (inum)) {
	return idio_C_int (idio_bignum_intmax_value (inum));
    } else {
	idio_error_param_type ("fixnum|bignum", inum);
    }

    /* notreached */
    return idio_S_unspec;
}

void idio_init_libc_wrap ()
{
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/NULL"), idio_C_pointer (NULL), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/INTMAX_MAX"), idio_C_int (INTMAX_MAX), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/INTMAX_MIN"), idio_C_int (INTMAX_MIN), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/PATH_MAX"), idio_C_int (PATH_MAX), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGHUP"), idio_C_int (SIGHUP), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGINT"), idio_C_int (SIGINT), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGQUIT"), idio_C_int (SIGQUIT), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGPIPE"), idio_C_int (SIGPIPE), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGALRM"), idio_C_int (SIGALRM), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGTERM"), idio_C_int (SIGTERM), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGCHLD"), idio_C_int (SIGCHLD), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGCONT"), idio_C_int (SIGCONT), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGTSTP"), idio_C_int (SIGTSTP), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGTTIN"), idio_C_int (SIGTTIN), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGTTOU"), idio_C_int (SIGTTOU), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIGWINCH"), idio_C_int (SIGWINCH), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIG_DFL"), idio_C_pointer (SIG_DFL), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIG_IGN"), idio_C_pointer (SIG_IGN), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/STDIN_FILENO"), idio_C_int (STDIN_FILENO), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/STDOUT_FILENO"), idio_C_int (STDOUT_FILENO), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/TCSADRAIN"), idio_C_int (TCSADRAIN), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/TCSAFLUSH"), idio_C_int (TCSAFLUSH), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/UINTMAX_MAX"), idio_C_uint (UINTMAX_MAX), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/0U"), idio_C_uint (0U), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WAIT_ANY"), idio_C_int (WAIT_ANY), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WNOHANG"), idio_C_int (WNOHANG), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WUNTRACED"), idio_C_int (WUNTRACED), idio_main_module ());

    idio_libc_set_signal_names ();
}

void idio_libc_wrap_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (C_close);
    IDIO_ADD_PRIMITIVE (C_dup2);
    IDIO_ADD_PRIMITIVE (C_exit);
    IDIO_ADD_PRIMITIVE (C_fork);
    IDIO_ADD_PRIMITIVE (C_getcwd);
    IDIO_ADD_PRIMITIVE (C_getpgrp);
    IDIO_ADD_PRIMITIVE (C_getpid);
    IDIO_ADD_PRIMITIVE (C_isatty);
    IDIO_ADD_PRIMITIVE (C_kill);
    IDIO_ADD_PRIMITIVE (C_pipe);
    IDIO_ADD_PRIMITIVE (C_pipe_reader);
    IDIO_ADD_PRIMITIVE (C_pipe_writer);
    IDIO_ADD_PRIMITIVE (C_read);
    IDIO_ADD_PRIMITIVE (C_setpgid);
    IDIO_ADD_PRIMITIVE (C_signal);
    IDIO_ADD_PRIMITIVE (C_sleep);
    IDIO_ADD_PRIMITIVE (C_strsignal);
    IDIO_ADD_PRIMITIVE (C_tcgetattr);
    IDIO_ADD_PRIMITIVE (C_tcgetpgrp);
    IDIO_ADD_PRIMITIVE (C_tcsetattr);
    IDIO_ADD_PRIMITIVE (C_tcsetpgrp);
    IDIO_ADD_PRIMITIVE (C_waitpid);
    IDIO_ADD_PRIMITIVE (C_WEXITSTATUS);
    IDIO_ADD_PRIMITIVE (C_WIFEXITED);
    IDIO_ADD_PRIMITIVE (C_WIFSIGNALED);
    IDIO_ADD_PRIMITIVE (C_WIFSTOPPED);
    IDIO_ADD_PRIMITIVE (C_WTERMSIG);
    IDIO_ADD_PRIMITIVE (C_write);

    IDIO_ADD_PRIMITIVE (C_bw_or);
    IDIO_ADD_PRIMITIVE (C_bw_and);
    IDIO_ADD_PRIMITIVE (C_bw_xor);
    IDIO_ADD_PRIMITIVE (C_bw_complement);

    IDIO_ADD_PRIMITIVE (C_sig_name);
    IDIO_ADD_PRIMITIVE (C_to_integer);
    IDIO_ADD_PRIMITIVE (C_integer_to);
}

void idio_final_libc_wrap ()
{
    int i;
    for (i = 0; NULL != idio_libc_signal_names[i]; i++) {
        free (idio_libc_signal_names[i]);
    }
    free (idio_libc_signal_names);
}

