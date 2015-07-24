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
char **idio_libc_errno_names = NULL;

IDIO_DEFINE_PRIMITIVE1 ("c/close", C_close, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = close (fd);

    if (-1 == r) {
	idio_error_system_errno ("close", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("c/close"));
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
	idio_error_system_errno ("dup2", IDIO_LIST2 (ioldfd, inewfd), IDIO_C_LOCATION ("c/dup2"));
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
	idio_error_param_type ("fixnum", istatus, IDIO_C_LOCATION ("c/exit"));
    }

    exit (status);

    /* notreached */
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0 ("c/fork", C_fork, ())
{
    pid_t pid = fork ();

    if (-1 == pid) {
	idio_error_system_errno ("fork", idio_S_nil, IDIO_C_LOCATION ("c/fork"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("c/getcwd", C_getcwd, ())
{
    
    char *cwd = getcwd (NULL, PATH_MAX);

    if (NULL == cwd) {
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_LOCATION ("c/getcwd"));
    }

    IDIO r = idio_string_C (cwd);
    free (cwd);

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("c/getpgrp", C_getpgrp, ())
{
    pid_t pid = getpgrp ();

    if (-1 == pid) {
	idio_error_system_errno ("getpgrp", idio_S_nil, IDIO_C_LOCATION ("c/getpgrp"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("c/getpid", C_getpid, ())
{
    pid_t pid = getpid ();

    if (-1 == pid) {
	idio_error_system_errno ("getpid", idio_S_nil, IDIO_C_LOCATION ("c/getpid"));
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
	idio_error_system_errno ("isatty", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("c/isatty"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("c/kill", C_kill, (IDIO ipid, IDIO isig))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (isig);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);
    
    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int sig = IDIO_C_TYPE_INT (isig);

    int r = kill (pid, sig);
    
    if (-1 == r) {
	idio_error_system_errno ("kill", IDIO_LIST2 (ipid, isig), IDIO_C_LOCATION ("c/kill"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0 ("c/pipe", C_pipe, ())
{
    int *pipefd = idio_alloc (2 * sizeof (int));
    
    int r = pipe (pipefd);

    if (-1 == r) {
	idio_error_system_errno ("pipe", idio_S_nil, IDIO_C_LOCATION ("c/pipe"));
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
	    idio_error_param_type ("fixnum|C_int", icount, IDIO_C_LOCATION ("c/read"));
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
	    idio_error_system_errno ("setpgid", IDIO_LIST2 (ipid, ipgid), IDIO_C_LOCATION ("c/setpgid"));
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
	idio_error_system_errno ("signal", IDIO_LIST2 (isig, ifunc), IDIO_C_LOCATION ("c/signal"));
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
	idio_error_param_type ("unsigned fixnum|C_uint", iseconds, IDIO_C_LOCATION ("c/sleep"));
    }

    unsigned int r = sleep (seconds);
    
    return idio_C_uint (r);
}

IDIO_DEFINE_PRIMITIVE1 ("c/strerror", C_strerror, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);

    int errnum = 0;
    if (idio_isa_fixnum (ierrnum)) {
	errnum = IDIO_FIXNUM_VAL (ierrnum);
    } else if (idio_isa_C_int (ierrnum)) {
	errnum = IDIO_C_TYPE_INT (ierrnum);
    } else {
	idio_error_param_type ("unsigned fixnum|C_int", ierrnum, IDIO_C_LOCATION ("c/strerror"));
    }

    char *r = strerror (errnum);
    
    return idio_string_C (r);
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
	idio_error_param_type ("unsigned fixnum|C_int", isignum, IDIO_C_LOCATION ("c/strsignal"));
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
	idio_error_system_errno ("tcgetattr", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("c/tcgetattr"));
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
	idio_error_system_errno ("tcgetpgrp", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("c/tcgetpgrp"));
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
	idio_error_system_errno ("tcsetattr", IDIO_LIST3 (ifd, ioptions, itcattrs), IDIO_C_LOCATION ("c/tcsetattr"));
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
	idio_error_system_errno ("tcsetpgrp", IDIO_LIST2 (ifd, ipgrp), IDIO_C_LOCATION ("c/tcsetpgrp"));
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
	idio_error_system_errno ("waitpid", IDIO_LIST2 (ipid, ioptions), IDIO_C_LOCATION ("c/waitpid"));
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
	idio_error_system_errno ("write", IDIO_LIST2 (ifd, istr), IDIO_C_LOCATION ("c/write"));
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

#define IDIO_LIBC_FSIG 1

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
	 * Can have an extra SIGRTMIN+n if there's an odd number --
	 * don't forget it is SIGRTMIN -> SIGRTMAX *inclusive* so
	 * there is an off-by-one error tempting us here...
	 */
	if (0 == rtdiff) {
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

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FSIG ; i < IDIO_LIBC_NSIG ; i++) {
	if ('\0' == *(idio_libc_signal_names[i])) {
	    sprintf (idio_libc_signal_names[i], "SIGJUNK%d", i);
	    if (first) {
		first = 0;
		fprintf (stderr, "Undefined signals:");
	    }
	    fprintf (stderr, " %d", i);
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

char *idio_libc_signal_name (int signum)
{
    if (signum < IDIO_LIBC_FSIG ||
	signum > IDIO_LIBC_NSIG) {
	idio_error_param_type ("int < NSIG (or SIGRTMAX)", idio_C_int (signum), IDIO_C_LOCATION ("idio_libc_signal_name"));
    }

    char *signame = idio_libc_signal_names[signum];

    if (strncmp (signame, "SIG", 3) == 0) {
	return (signame + 3);
    } else {
	return signame;
    }
}

IDIO_DEFINE_PRIMITIVE1 ("c/signal-name", C_signal_name, (IDIO isignum))
{
    IDIO_ASSERT (isignum);
    IDIO_VERIFY_PARAM_TYPE (C_int, isignum);

    return idio_string_C (idio_libc_signal_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0 ("c/signal-names", C_signal_names, ())
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
 * OS 10.10.4	EQFULL		106
 * FreeBSD 10	EOWNERDEAD	96
 * Ubuntu 14	EHWPOISON	133	
 * Debian 8	EHWPOISON	133	
 *
 */

#define IDIO_LIBC_FERRNO 1

#if defined (BSD)
#define IDIO_LIBC_NERRNO (EOWNERDEAD + 1)
#elif defined (__linux__)
#define IDIO_LIBC_NERRNO (EHWPOISON + 1)
#elif defined (__APPLE__) && defined (__MACH__)
#define IDIO_LIBC_NERRNO (EQFULL + 1)
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
    for (i = 0; i < IDIO_LIBC_NERRNO; i++) {
	idio_libc_errno_names[i] = idio_alloc (IDIO_LIBC_ERRNAMELEN);
	*(idio_libc_errno_names[i]) = '\0';
    }
    idio_libc_errno_names[i] = NULL;

    sprintf (idio_libc_errno_names[0], "E0");

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (E2BIG)
    sprintf (idio_libc_errno_names[E2BIG], "E2BIG");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EACCES)
    sprintf (idio_libc_errno_names[EACCES], "EACCES");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRINUSE)
    sprintf (idio_libc_errno_names[EADDRINUSE], "EADDRINUSE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRNOTAVAIL)
    sprintf (idio_libc_errno_names[EADDRNOTAVAIL], "EADDRNOTAVAIL");
#endif

    /* Linux, Solaris */
#if defined (EADV)
    sprintf (idio_libc_errno_names[EADV], "EADV");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAFNOSUPPORT)
    sprintf (idio_libc_errno_names[EAFNOSUPPORT], "EAFNOSUPPORT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAGAIN)
    sprintf (idio_libc_errno_names[EAGAIN], "EAGAIN");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EALREADY)
    sprintf (idio_libc_errno_names[EALREADY], "EALREADY");
#endif

    /* FreeBSD, OSX */
#if defined (EAUTH)
    sprintf (idio_libc_errno_names[EAUTH], "EAUTH");
#endif

    /* OSX */
#if defined (EBADARCH)
    sprintf (idio_libc_errno_names[EBADARCH], "EBADARCH");
#endif

    /* Linux, Solaris */
#if defined (EBADE)
    sprintf (idio_libc_errno_names[EBADE], "EBADE");
#endif

    /* OSX */
#if defined (EBADEXEC)
    sprintf (idio_libc_errno_names[EBADEXEC], "EBADEXEC");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADF)
    sprintf (idio_libc_errno_names[EBADF], "EBADF");
#endif

    /* Linux, Solaris */
#if defined (EBADFD)
    sprintf (idio_libc_errno_names[EBADFD], "EBADFD");
#endif

    /* OSX */
#if defined (EBADMACHO)
    sprintf (idio_libc_errno_names[EBADMACHO], "EBADMACHO");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADMSG)
    sprintf (idio_libc_errno_names[EBADMSG], "EBADMSG");
#endif

    /* Linux, Solaris */
#if defined (EBADR)
    sprintf (idio_libc_errno_names[EBADR], "EBADR");
#endif

    /* FreeBSD, OSX */
#if defined (EBADRPC)
    sprintf (idio_libc_errno_names[EBADRPC], "EBADRPC");
#endif

    /* Linux, Solaris */
#if defined (EBADRQC)
    sprintf (idio_libc_errno_names[EBADRQC], "EBADRQC");
#endif

    /* Linux, Solaris */
#if defined (EBADSLT)
    sprintf (idio_libc_errno_names[EBADSLT], "EBADSLT");
#endif

    /* Linux, Solaris */
#if defined (EBFONT)
    sprintf (idio_libc_errno_names[EBFONT], "EBFONT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBUSY)
    sprintf (idio_libc_errno_names[EBUSY], "EBUSY");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECANCELED)
    sprintf (idio_libc_errno_names[ECANCELED], "ECANCELED");
#endif

    /* FreeBSD */
#if defined (ECAPMODE)
    sprintf (idio_libc_errno_names[ECAPMODE], "ECAPMODE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECHILD)
    sprintf (idio_libc_errno_names[ECHILD], "ECHILD");
#endif

    /* Linux, Solaris */
#if defined (ECHRNG)
    sprintf (idio_libc_errno_names[ECHRNG], "ECHRNG");
#endif

    /* Linux, Solaris */
#if defined (ECOMM)
    sprintf (idio_libc_errno_names[ECOMM], "ECOMM");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNABORTED)
    sprintf (idio_libc_errno_names[ECONNABORTED], "ECONNABORTED");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNREFUSED)
    sprintf (idio_libc_errno_names[ECONNREFUSED], "ECONNREFUSED");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNRESET)
    sprintf (idio_libc_errno_names[ECONNRESET], "ECONNRESET");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDEADLK)
    sprintf (idio_libc_errno_names[EDEADLK], "EDEADLK");
#endif

    /* Linux, Solaris */
#if defined (EDEADLOCK)
    sprintf (idio_libc_errno_names[EDEADLOCK], "EDEADLOCK");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDESTADDRREQ)
    sprintf (idio_libc_errno_names[EDESTADDRREQ], "EDESTADDRREQ");
#endif

    /* OSX */
#if defined (EDEVERR)
    sprintf (idio_libc_errno_names[EDEVERR], "EDEVERR");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDOM)
    sprintf (idio_libc_errno_names[EDOM], "EDOM");
#endif

    /* FreeBSD */
#if defined (EDOOFUS)
    sprintf (idio_libc_errno_names[EDOOFUS], "EDOOFUS");
#endif

    /* Linux */
#if defined (EDOTDOT)
    sprintf (idio_libc_errno_names[EDOTDOT], "EDOTDOT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDQUOT)
    sprintf (idio_libc_errno_names[EDQUOT], "EDQUOT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EEXIST)
    sprintf (idio_libc_errno_names[EEXIST], "EEXIST");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFAULT)
    sprintf (idio_libc_errno_names[EFAULT], "EFAULT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFBIG)
    sprintf (idio_libc_errno_names[EFBIG], "EFBIG");
#endif

    /* FreeBSD, OSX */
#if defined (EFTYPE)
    sprintf (idio_libc_errno_names[EFTYPE], "EFTYPE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTDOWN)
    sprintf (idio_libc_errno_names[EHOSTDOWN], "EHOSTDOWN");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTUNREACH)
    sprintf (idio_libc_errno_names[EHOSTUNREACH], "EHOSTUNREACH");
#endif

    /* Linux */
#if defined (EHWPOISON)
    sprintf (idio_libc_errno_names[EHWPOISON], "EHWPOISON");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIDRM)
    sprintf (idio_libc_errno_names[EIDRM], "EIDRM");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EILSEQ)
    sprintf (idio_libc_errno_names[EILSEQ], "EILSEQ");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINPROGRESS)
    sprintf (idio_libc_errno_names[EINPROGRESS], "EINPROGRESS");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINTR)
    sprintf (idio_libc_errno_names[EINTR], "EINTR");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINVAL)
    sprintf (idio_libc_errno_names[EINVAL], "EINVAL");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIO)
    sprintf (idio_libc_errno_names[EIO], "EIO");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISCONN)
    sprintf (idio_libc_errno_names[EISCONN], "EISCONN");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISDIR)
    sprintf (idio_libc_errno_names[EISDIR], "EISDIR");
#endif

    /* Linux */
#if defined (EISNAM)
    sprintf (idio_libc_errno_names[EISNAM], "EISNAM");
#endif

    /* Linux */
#if defined (EKEYEXPIRED)
    sprintf (idio_libc_errno_names[EKEYEXPIRED], "EKEYEXPIRED");
#endif

    /* Linux */
#if defined (EKEYREJECTED)
    sprintf (idio_libc_errno_names[EKEYREJECTED], "EKEYREJECTED");
#endif

    /* Linux */
#if defined (EKEYREVOKED)
    sprintf (idio_libc_errno_names[EKEYREVOKED], "EKEYREVOKED");
#endif

    /* Linux, Solaris */
#if defined (EL2HLT)
    sprintf (idio_libc_errno_names[EL2HLT], "EL2HLT");
#endif

    /* Linux, Solaris */
#if defined (EL2NSYNC)
    sprintf (idio_libc_errno_names[EL2NSYNC], "EL2NSYNC");
#endif

    /* Linux, Solaris */
#if defined (EL3HLT)
    sprintf (idio_libc_errno_names[EL3HLT], "EL3HLT");
#endif

    /* Linux, Solaris */
#if defined (EL3RST)
    sprintf (idio_libc_errno_names[EL3RST], "EL3RST");
#endif

    /* Linux, Solaris */
#if defined (ELIBACC)
    sprintf (idio_libc_errno_names[ELIBACC], "ELIBACC");
#endif

    /* Linux, Solaris */
#if defined (ELIBBAD)
    sprintf (idio_libc_errno_names[ELIBBAD], "ELIBBAD");
#endif

    /* Linux, Solaris */
#if defined (ELIBEXEC)
    sprintf (idio_libc_errno_names[ELIBEXEC], "ELIBEXEC");
#endif

    /* Linux, Solaris */
#if defined (ELIBMAX)
    sprintf (idio_libc_errno_names[ELIBMAX], "ELIBMAX");
#endif

    /* Linux, Solaris */
#if defined (ELIBSCN)
    sprintf (idio_libc_errno_names[ELIBSCN], "ELIBSCN");
#endif

    /* Linux, Solaris */
#if defined (ELNRNG)
    sprintf (idio_libc_errno_names[ELNRNG], "ELNRNG");
#endif

    /* Solaris */
#if defined (ELOCKUNMAPPED)
    sprintf (idio_libc_errno_names[ELOCKUNMAPPED], "ELOCKUNMAPPED");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ELOOP)
    sprintf (idio_libc_errno_names[ELOOP], "ELOOP");
#endif

    /* Linux */
#if defined (EMEDIUMTYPE)
    sprintf (idio_libc_errno_names[EMEDIUMTYPE], "EMEDIUMTYPE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMFILE)
    sprintf (idio_libc_errno_names[EMFILE], "EMFILE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMLINK)
    sprintf (idio_libc_errno_names[EMLINK], "EMLINK");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMSGSIZE)
    sprintf (idio_libc_errno_names[EMSGSIZE], "EMSGSIZE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMULTIHOP)
    sprintf (idio_libc_errno_names[EMULTIHOP], "EMULTIHOP");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENAMETOOLONG)
    sprintf (idio_libc_errno_names[ENAMETOOLONG], "ENAMETOOLONG");
#endif

    /* Linux */
#if defined (ENAVAIL)
    sprintf (idio_libc_errno_names[ENAVAIL], "ENAVAIL");
#endif

    /* FreeBSD, OSX */
#if defined (ENEEDAUTH)
    sprintf (idio_libc_errno_names[ENEEDAUTH], "ENEEDAUTH");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETDOWN)
    sprintf (idio_libc_errno_names[ENETDOWN], "ENETDOWN");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETRESET)
    sprintf (idio_libc_errno_names[ENETRESET], "ENETRESET");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETUNREACH)
    sprintf (idio_libc_errno_names[ENETUNREACH], "ENETUNREACH");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENFILE)
    sprintf (idio_libc_errno_names[ENFILE], "ENFILE");
#endif

    /* Linux, Solaris */
#if defined (ENOANO)
    sprintf (idio_libc_errno_names[ENOANO], "ENOANO");
#endif

    /* FreeBSD, OSX */
#if defined (ENOATTR)
    sprintf (idio_libc_errno_names[ENOATTR], "ENOATTR");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOBUFS)
    sprintf (idio_libc_errno_names[ENOBUFS], "ENOBUFS");
#endif

    /* Linux, Solaris */
#if defined (ENOCSI)
    sprintf (idio_libc_errno_names[ENOCSI], "ENOCSI");
#endif

    /* Linux, OSX, Solaris */
#if defined (ENODATA)
    sprintf (idio_libc_errno_names[ENODATA], "ENODATA");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENODEV)
    sprintf (idio_libc_errno_names[ENODEV], "ENODEV");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOENT)
    sprintf (idio_libc_errno_names[ENOENT], "ENOENT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOEXEC)
    sprintf (idio_libc_errno_names[ENOEXEC], "ENOEXEC");
#endif

    /* Linux */
#if defined (ENOKEY)
    sprintf (idio_libc_errno_names[ENOKEY], "ENOKEY");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLCK)
    sprintf (idio_libc_errno_names[ENOLCK], "ENOLCK");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLINK)
    sprintf (idio_libc_errno_names[ENOLINK], "ENOLINK");
#endif

    /* Linux */
#if defined (ENOMEDIUM)
    sprintf (idio_libc_errno_names[ENOMEDIUM], "ENOMEDIUM");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMEM)
    sprintf (idio_libc_errno_names[ENOMEM], "ENOMEM");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMSG)
    sprintf (idio_libc_errno_names[ENOMSG], "ENOMSG");
#endif

    /* Linux, Solaris */
#if defined (ENONET)
    sprintf (idio_libc_errno_names[ENONET], "ENONET");
#endif

    /* Linux, Solaris */
#if defined (ENOPKG)
    sprintf (idio_libc_errno_names[ENOPKG], "ENOPKG");
#endif

    /* OSX */
#if defined (ENOPOLICY)
    sprintf (idio_libc_errno_names[ENOPOLICY], "ENOPOLICY");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOPROTOOPT)
    sprintf (idio_libc_errno_names[ENOPROTOOPT], "ENOPROTOOPT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSPC)
    sprintf (idio_libc_errno_names[ENOSPC], "ENOSPC");
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSR)
    sprintf (idio_libc_errno_names[ENOSR], "ENOSR");
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSTR)
    sprintf (idio_libc_errno_names[ENOSTR], "ENOSTR");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSYS)
    sprintf (idio_libc_errno_names[ENOSYS], "ENOSYS");
#endif

    /* Solaris */
#if defined (ENOTACTIVE)
    sprintf (idio_libc_errno_names[ENOTACTIVE], "ENOTACTIVE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTBLK)
    sprintf (idio_libc_errno_names[ENOTBLK], "ENOTBLK");
#endif

    /* FreeBSD */
#if defined (ENOTCAPABLE)
    sprintf (idio_libc_errno_names[ENOTCAPABLE], "ENOTCAPABLE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTCONN)
    sprintf (idio_libc_errno_names[ENOTCONN], "ENOTCONN");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTDIR)
    sprintf (idio_libc_errno_names[ENOTDIR], "ENOTDIR");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTEMPTY)
    sprintf (idio_libc_errno_names[ENOTEMPTY], "ENOTEMPTY");
#endif

    /* Linux */
#if defined (ENOTNAM)
    sprintf (idio_libc_errno_names[ENOTNAM], "ENOTNAM");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTRECOVERABLE)
    sprintf (idio_libc_errno_names[ENOTRECOVERABLE], "ENOTRECOVERABLE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTSOCK)
    sprintf (idio_libc_errno_names[ENOTSOCK], "ENOTSOCK");
#endif

    /* FreeBSD, OSX, Solaris */
#if defined (ENOTSUP)
    sprintf (idio_libc_errno_names[ENOTSUP], "ENOTSUP");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTTY)
    sprintf (idio_libc_errno_names[ENOTTY], "ENOTTY");
#endif

    /* Linux, Solaris */
#if defined (ENOTUNIQ)
    sprintf (idio_libc_errno_names[ENOTUNIQ], "ENOTUNIQ");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENXIO)
    sprintf (idio_libc_errno_names[ENXIO], "ENXIO");
#endif

    /* FreeBSD, Linux, OSX, OSX, Solaris */
#if defined (EOPNOTSUPP)
    sprintf (idio_libc_errno_names[EOPNOTSUPP], "EOPNOTSUPP");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOVERFLOW)
    sprintf (idio_libc_errno_names[EOVERFLOW], "EOVERFLOW");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOWNERDEAD)
    sprintf (idio_libc_errno_names[EOWNERDEAD], "EOWNERDEAD");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPERM)
    sprintf (idio_libc_errno_names[EPERM], "EPERM");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPFNOSUPPORT)
    sprintf (idio_libc_errno_names[EPFNOSUPPORT], "EPFNOSUPPORT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPIPE)
    sprintf (idio_libc_errno_names[EPIPE], "EPIPE");
#endif

    /* FreeBSD, OSX */
#if defined (EPROCLIM)
    sprintf (idio_libc_errno_names[EPROCLIM], "EPROCLIM");
#endif

    /* FreeBSD, OSX */
#if defined (EPROCUNAVAIL)
    sprintf (idio_libc_errno_names[EPROCUNAVAIL], "EPROCUNAVAIL");
#endif

    /* FreeBSD, OSX */
#if defined (EPROGMISMATCH)
    sprintf (idio_libc_errno_names[EPROGMISMATCH], "EPROGMISMATCH");
#endif

    /* FreeBSD, OSX */
#if defined (EPROGUNAVAIL)
    sprintf (idio_libc_errno_names[EPROGUNAVAIL], "EPROGUNAVAIL");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTO)
    sprintf (idio_libc_errno_names[EPROTO], "EPROTO");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTONOSUPPORT)
    sprintf (idio_libc_errno_names[EPROTONOSUPPORT], "EPROTONOSUPPORT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTOTYPE)
    sprintf (idio_libc_errno_names[EPROTOTYPE], "EPROTOTYPE");
#endif

    /* OSX */
#if defined (EPWROFF)
    sprintf (idio_libc_errno_names[EPWROFF], "EPWROFF");
#endif

    /* OSX */
#if defined (EQFULL)
    sprintf (idio_libc_errno_names[EQFULL], "EQFULL");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ERANGE)
    sprintf (idio_libc_errno_names[ERANGE], "ERANGE");
#endif

    /* Linux, Solaris */
#if defined (EREMCHG)
    sprintf (idio_libc_errno_names[EREMCHG], "EREMCHG");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EREMOTE)
    sprintf (idio_libc_errno_names[EREMOTE], "EREMOTE");
#endif

    /* Linux */
#if defined (EREMOTEIO)
    sprintf (idio_libc_errno_names[EREMOTEIO], "EREMOTEIO");
#endif

    /* Linux, Solaris */
#if defined (ERESTART)
    sprintf (idio_libc_errno_names[ERESTART], "ERESTART");
#endif

    /* Linux */
#if defined (ERFKILL)
    sprintf (idio_libc_errno_names[ERFKILL], "ERFKILL");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EROFS)
    sprintf (idio_libc_errno_names[EROFS], "EROFS");
#endif

    /* FreeBSD, OSX */
#if defined (ERPCMISMATCH)
    sprintf (idio_libc_errno_names[ERPCMISMATCH], "ERPCMISMATCH");
#endif

    /* OSX */
#if defined (ESHLIBVERS)
    sprintf (idio_libc_errno_names[ESHLIBVERS], "ESHLIBVERS");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESHUTDOWN)
    sprintf (idio_libc_errno_names[ESHUTDOWN], "ESHUTDOWN");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESOCKTNOSUPPORT)
    sprintf (idio_libc_errno_names[ESOCKTNOSUPPORT], "ESOCKTNOSUPPORT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESPIPE)
    sprintf (idio_libc_errno_names[ESPIPE], "ESPIPE");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESRCH)
    sprintf (idio_libc_errno_names[ESRCH], "ESRCH");
#endif

    /* Linux, Solaris */
#if defined (ESRMNT)
    sprintf (idio_libc_errno_names[ESRMNT], "ESRMNT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESTALE)
    sprintf (idio_libc_errno_names[ESTALE], "ESTALE");
#endif

    /* Linux, Solaris */
#if defined (ESTRPIPE)
    sprintf (idio_libc_errno_names[ESTRPIPE], "ESTRPIPE");
#endif

    /* Linux, OSX, Solaris */
#if defined (ETIME)
    sprintf (idio_libc_errno_names[ETIME], "ETIME");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETIMEDOUT)
    sprintf (idio_libc_errno_names[ETIMEDOUT], "ETIMEDOUT");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETOOMANYREFS)
    sprintf (idio_libc_errno_names[ETOOMANYREFS], "ETOOMANYREFS");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETXTBSY)
    sprintf (idio_libc_errno_names[ETXTBSY], "ETXTBSY");
#endif

    /* Linux */
#if defined (EUCLEAN)
    sprintf (idio_libc_errno_names[EUCLEAN], "EUCLEAN");
#endif

    /* Linux, Solaris */
#if defined (EUNATCH)
    sprintf (idio_libc_errno_names[EUNATCH], "EUNATCH");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EUSERS)
    sprintf (idio_libc_errno_names[EUSERS], "EUSERS");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EWOULDBLOCK)
    sprintf (idio_libc_errno_names[EWOULDBLOCK], "EWOULDBLOCK");
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EXDEV)
    sprintf (idio_libc_errno_names[EXDEV], "EXDEV");
#endif

    /* Linux, Solaris */
#if defined (EXFULL)
    sprintf (idio_libc_errno_names[EXFULL], "EXFULL");
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FERRNO ; i < IDIO_LIBC_NERRNO ; i++) {
	if ('\0' == *(idio_libc_errno_names[i])) {
	    sprintf (idio_libc_errno_names[i], "ERRNO%d", i);
	    if (first) {
		first = 0;
		fprintf (stderr, "Undefined errno:");
	    }
	    fprintf (stderr, " %d", i);
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
	idio_error_param_type ("int < 0 (or > NERRNO)", idio_C_int (errnum), IDIO_C_LOCATION ("idio_libc_errno_name"));
    }

    char *errname = idio_libc_errno_names[errnum];

    if (strncmp (errname, "ERR", 3) == 0) {
	return (errname + 3);
    } else {
	return errname;
    }
}

IDIO_DEFINE_PRIMITIVE1 ("c/strerrno", C_strerrno, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);
    IDIO_VERIFY_PARAM_TYPE (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE1 ("c/->integer", C_to_integer, (IDIO inum))
{
    IDIO_ASSERT (inum);

    if (idio_isa_C_uint (inum)) {
	return idio_uinteger (IDIO_C_TYPE_UINT (inum));
    } else if (idio_isa_C_int (inum)) {
	return idio_integer (IDIO_C_TYPE_INT (inum));
    } else {
	idio_error_param_type ("C_int|C_uint", inum, IDIO_C_LOCATION ("c/->integer"));
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
	idio_error_param_type ("fixnum|bignum", inum, IDIO_C_LOCATION ("c/integer->"));
    }

    /* notreached */
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0 ("c/errno/get", C_errno_get, (void))
{
    return idio_C_int (errno);
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

    IDIO index = IDIO_ADD_SPECIAL_PRIMITIVE (C_errno_get);

    idio_module_add_computed_symbol (idio_symbols_C_intern ("c/errno"), idio_vm_primitives_ref (IDIO_FIXNUM_VAL (index)), idio_S_nil, idio_main_module ());

    idio_libc_set_signal_names ();
    idio_libc_set_errno_names ();
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
    IDIO_ADD_PRIMITIVE (C_strerror);
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

    IDIO_ADD_PRIMITIVE (C_signal_name);
    IDIO_ADD_PRIMITIVE (C_signal_names);
    IDIO_ADD_PRIMITIVE (C_strerrno);
    IDIO_ADD_PRIMITIVE (C_to_integer);
    IDIO_ADD_PRIMITIVE (C_integer_to);
}

void idio_final_libc_wrap ()
{
    int i;
    for (i = IDIO_LIBC_FSIG; NULL != idio_libc_signal_names[i]; i++) {
        free (idio_libc_signal_names[i]);
    }
    free (idio_libc_signal_names);
    for (i = IDIO_LIBC_FERRNO; NULL != idio_libc_errno_names[i]; i++) {
        free (idio_libc_errno_names[i]);
    }
    free (idio_libc_errno_names);
}

