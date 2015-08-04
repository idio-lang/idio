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

IDIO_DEFINE_PRIMITIVE2V ("c/fcntl", C_fcntl, (IDIO ifd, IDIO icmd, IDIO args))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (icmd);
    IDIO_ASSERT (args);

    int fd = 0;
    if (idio_isa_fixnum (ifd)) {
	fd = IDIO_FIXNUM_VAL (ifd);
    } else if (idio_isa_C_int (ifd)) {
	fd = IDIO_C_TYPE_INT (ifd);
    } else {
	idio_error_param_type ("fixnum|C_int ifd", ifd, IDIO_C_LOCATION ("c/fcntl"));
    }

    int cmd = 0;
    if (idio_isa_C_int (icmd)) {
	cmd = IDIO_C_TYPE_INT (icmd);
    } else {
	idio_error_param_type ("C_int icmd", icmd, IDIO_C_LOCATION ("c/fcntl"));
    }

    IDIO iarg = idio_list_head (args);
    
    int r;

    switch (cmd) {
    case F_DUPFD:
	{
	    /*
	     * CentOS 6 i386 fcntl(2) says it wants long but accepts
	     * int
	     */
	    int arg;
	    if (idio_isa_fixnum (iarg)) {
		arg = IDIO_FIXNUM_VAL (iarg);
	    } else if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_LOCATION ("c/fcntl"));

		/* notreached */
		return idio_S_unspec;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
#if defined (F_DUPFD_CLOEXEC)
    case F_DUPFD_CLOEXEC:
	{
	    /*
	     * CentOS 6 i386 fcntl(2) says it wants long but accepts
	     * int
	     */
	    int arg;
	    if (idio_isa_fixnum (iarg)) {
		arg = IDIO_FIXNUM_VAL (iarg);
	    } else if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_LOCATION ("c/fcntl"));

		/* notreached */
		return idio_S_unspec;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
#endif
    case F_GETFD:
	{
	    r = fcntl (fd, cmd);
	}
	break;
    case F_SETFD:
	{
	    int arg;
	    if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		idio_error_param_type ("C_int", iarg, IDIO_C_LOCATION ("c/fcntl"));

		/* notreached */
		return idio_S_unspec;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
    default:
	idio_error_C ("unexpected cmd", IDIO_LIST2 (ifd, icmd), IDIO_C_LOCATION ("c/fcntl"));

	/* notreached */
	return idio_S_unspec;
    }

    return idio_C_int (r);
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
    /*
     * See comment in command.c re: getcwd(3) and PATH_MAX
     */
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

    IDIO sig_sym;
    
#if defined(SIGHUP)
    sig_sym = idio_symbols_C_intern ("c/SIGHUP");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGHUP), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGHUP], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGINT)
    sig_sym = idio_symbols_C_intern ("c/SIGINT");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGINT), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGINT], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGQUIT)
    sig_sym = idio_symbols_C_intern ("c/SIGQUIT");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGQUIT), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGQUIT], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGILL)
    sig_sym = idio_symbols_C_intern ("c/SIGILL");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGILL), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGILL], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGTRAP)
    sig_sym = idio_symbols_C_intern ("c/SIGTRAP");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGTRAP), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGTRAP], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGIOT)
    sig_sym = idio_symbols_C_intern ("c/SIGIOT");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGIOT), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGIOT], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGEMT)
    sig_sym = idio_symbols_C_intern ("c/SIGEMT");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGEMT), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGEMT], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGFPE)
    sig_sym = idio_symbols_C_intern ("c/SIGFPE");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGFPE), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGFPE], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGKILL)
    sig_sym = idio_symbols_C_intern ("c/SIGKILL");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGKILL), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGKILL], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGBUS)
    sig_sym = idio_symbols_C_intern ("c/SIGBUS");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGBUS), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGBUS], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGSEGV)
    sig_sym = idio_symbols_C_intern ("c/SIGSEGV");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGSEGV), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGSEGV], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGSYS)
    sig_sym = idio_symbols_C_intern ("c/SIGSYS");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGSYS), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGSYS], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGPIPE)
    sig_sym = idio_symbols_C_intern ("c/SIGPIPE");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGPIPE), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGPIPE], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGALRM)
    sig_sym = idio_symbols_C_intern ("c/SIGALRM");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGALRM), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGALRM], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGTERM)
    sig_sym = idio_symbols_C_intern ("c/SIGTERM");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGTERM), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGTERM], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGUSR1)
    sig_sym = idio_symbols_C_intern ("c/SIGUSR1");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGUSR1), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGUSR1], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGUSR2)
    sig_sym = idio_symbols_C_intern ("c/SIGUSR2");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGUSR2), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGUSR2], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGCHLD)
    sig_sym = idio_symbols_C_intern ("c/SIGCHLD");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGCHLD), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGCHLD], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGPWR)
    sig_sym = idio_symbols_C_intern ("c/SIGPWR");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGPWR), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGPWR], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGWINCH)
    sig_sym = idio_symbols_C_intern ("c/SIGWINCH");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGWINCH), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGWINCH], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGURG)
    sig_sym = idio_symbols_C_intern ("c/SIGURG");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGURG), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGURG], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGPOLL)
    sig_sym = idio_symbols_C_intern ("c/SIGPOLL");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGPOLL), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGPOLL], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGSTOP)
    sig_sym = idio_symbols_C_intern ("c/SIGSTOP");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGSTOP), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGSTOP], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGTSTP)
    sig_sym = idio_symbols_C_intern ("c/SIGTSTP");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGTSTP), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGTSTP], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGCONT)
    sig_sym = idio_symbols_C_intern ("c/SIGCONT");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGCONT), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGCONT], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGTTIN)
    sig_sym = idio_symbols_C_intern ("c/SIGTTIN");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGTTIN), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGTTIN], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGTTOU)
    sig_sym = idio_symbols_C_intern ("c/SIGTTOU");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGTTOU), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGTTOU], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGVTALRM)
    sig_sym = idio_symbols_C_intern ("c/SIGVTALRM");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGVTALRM), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGVTALRM], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGPROF)
    sig_sym = idio_symbols_C_intern ("c/SIGPROF");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGPROF), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGPROF], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGXCPU)
    sig_sym = idio_symbols_C_intern ("c/SIGXCPU");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGXCPU), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGXCPU], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if defined(SIGXFSZ)
    sig_sym = idio_symbols_C_intern ("c/SIGXFSZ");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGXFSZ), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGXFSZ], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGWAITING)
    sig_sym = idio_symbols_C_intern ("c/SIGWAITING");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGWAITING), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGWAITING], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGLWP)
    sig_sym = idio_symbols_C_intern ("c/SIGLWP");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGLWP), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGLWP], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGFREEZE)
    sig_sym = idio_symbols_C_intern ("c/SIGFREEZE");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGFREEZE), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGFREEZE], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGTHAW)
    sig_sym = idio_symbols_C_intern ("c/SIGTHAW");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGTHAW), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGTHAW], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGCANCEL)
    sig_sym = idio_symbols_C_intern ("c/SIGCANCEL");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGCANCEL), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGCANCEL], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGLOST)
    sig_sym = idio_symbols_C_intern ("c/SIGLOST");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGLOST), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGLOST], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGXRES)
    sig_sym = idio_symbols_C_intern ("c/SIGXRES");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGXRES), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGXRES], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGJVM1)
    sig_sym = idio_symbols_C_intern ("c/SIGJVM1");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGJVM1), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGJVM1], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* SunOS */
#if defined(SIGJVM2)
    sig_sym = idio_symbols_C_intern ("c/SIGJVM2");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGJVM2), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGJVM2], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

    /* Linux */
#if defined(SIGSTKFLT)
    sig_sym = idio_symbols_C_intern ("c/SIGSTKFLT");
    idio_module_set_symbol_value (sig_sym, idio_C_int (SIGSTKFLT), idio_main_module ());
    sprintf (idio_libc_signal_names[SIGSTKFLT], "%s", IDIO_SYMBOL_S (sig_sym) + 2);
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FSIG ; i < IDIO_LIBC_NSIG ; i++) {
	if ('\0' == *(idio_libc_signal_names[i])) {
	    char sig_name[IDIO_LIBC_SIGNAMELEN + 2];
	    sprintf (sig_name, "c/SIGJUNK%d", i);
	    sig_sym = idio_symbols_C_intern (sig_name);
	    idio_module_set_symbol_value (sig_sym, idio_C_int (i), idio_main_module ());
	    sprintf (idio_libc_signal_names[i], "%s", sig_name + 2);
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

/*
 * (According to Bash's .../builtins/common.c)
 *
 * POSIX.2 says the signal names are displayed without the `SIG'
 * prefix.
 *
 * We'll support both.  ~signal~ says the full signal name and ~sig~
 * says the POSIX.2 format (arguably it should be ~nal~ as we've
 * stripped off the "sig" part...can't be witty in the API!)
 */
char *idio_libc_sig_name (int signum)
{
    if (signum < IDIO_LIBC_FSIG ||
	signum > IDIO_LIBC_NSIG) {
	idio_error_param_type ("int < NSIG (or SIGRTMAX)", idio_C_int (signum), IDIO_C_LOCATION ("idio_libc_sig_name"));
    }

    char *signame = idio_libc_signal_names[signum];

    if (strncmp (signame, "SIG", 3) == 0) {
	return (signame + 3);
    } else {
	return signame;
    }
}

char *idio_libc_signal_name (int signum)
{
    if (signum < IDIO_LIBC_FSIG ||
	signum > IDIO_LIBC_NSIG) {
	idio_error_param_type ("int < NSIG (or SIGRTMAX)", idio_C_int (signum), IDIO_C_LOCATION ("idio_libc_signal_name"));
    }

    return idio_libc_signal_names[signum];
}

IDIO_DEFINE_PRIMITIVE1 ("c/sig-name", C_sig_name, (IDIO isignum))
{
    IDIO_ASSERT (isignum);
    IDIO_VERIFY_PARAM_TYPE (C_int, isignum);

    return idio_string_C (idio_libc_sig_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0 ("c/sig-names", C_sig_names, ())
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_sig_name (i))), r);
    }

    return idio_list_reverse (r);
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
    for (i = 0; i < IDIO_LIBC_NERRNO; i++) {
	idio_libc_errno_names[i] = idio_alloc (IDIO_LIBC_ERRNAMELEN);
	*(idio_libc_errno_names[i]) = '\0';
    }
    idio_libc_errno_names[i] = NULL;

    sprintf (idio_libc_errno_names[0], "E0");

    IDIO err_sym;
    
    /* FreeBSD, Linux, OSX, Solaris */
#if defined (E2BIG)
    err_sym = idio_symbols_C_intern ("c/E2BIG");
    idio_module_set_symbol_value (err_sym, idio_C_int (E2BIG), idio_main_module ());
    sprintf (idio_libc_errno_names[E2BIG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EACCES)
    err_sym = idio_symbols_C_intern ("c/EACCES");
    idio_module_set_symbol_value (err_sym, idio_C_int (EACCES), idio_main_module ());
    sprintf (idio_libc_errno_names[EACCES], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRINUSE)
    err_sym = idio_symbols_C_intern ("c/EADDRINUSE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EADDRINUSE), idio_main_module ());
    sprintf (idio_libc_errno_names[EADDRINUSE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRNOTAVAIL)
    err_sym = idio_symbols_C_intern ("c/EADDRNOTAVAIL");
    idio_module_set_symbol_value (err_sym, idio_C_int (EADDRNOTAVAIL), idio_main_module ());
    sprintf (idio_libc_errno_names[EADDRNOTAVAIL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EADV)
    err_sym = idio_symbols_C_intern ("c/EADV");
    idio_module_set_symbol_value (err_sym, idio_C_int (EADV), idio_main_module ());
    sprintf (idio_libc_errno_names[EADV], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAFNOSUPPORT)
    err_sym = idio_symbols_C_intern ("c/EAFNOSUPPORT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EAFNOSUPPORT), idio_main_module ());
    sprintf (idio_libc_errno_names[EAFNOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAGAIN)
    err_sym = idio_symbols_C_intern ("c/EAGAIN");
    idio_module_set_symbol_value (err_sym, idio_C_int (EAGAIN), idio_main_module ());
    sprintf (idio_libc_errno_names[EAGAIN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EALREADY)
    err_sym = idio_symbols_C_intern ("c/EALREADY");
    idio_module_set_symbol_value (err_sym, idio_C_int (EALREADY), idio_main_module ());
    sprintf (idio_libc_errno_names[EALREADY], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (EAUTH)
    err_sym = idio_symbols_C_intern ("c/EAUTH");
    idio_module_set_symbol_value (err_sym, idio_C_int (EAUTH), idio_main_module ());
    sprintf (idio_libc_errno_names[EAUTH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (EBADARCH)
    err_sym = idio_symbols_C_intern ("c/EBADARCH");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADARCH), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADARCH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EBADE)
    err_sym = idio_symbols_C_intern ("c/EBADE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADE), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (EBADEXEC)
    err_sym = idio_symbols_C_intern ("c/EBADEXEC");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADEXEC), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADEXEC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADF)
    err_sym = idio_symbols_C_intern ("c/EBADF");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADF), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADF], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EBADFD)
    err_sym = idio_symbols_C_intern ("c/EBADFD");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADFD), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADFD], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (EBADMACHO)
    err_sym = idio_symbols_C_intern ("c/EBADMACHO");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADMACHO), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADMACHO], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADMSG)
    err_sym = idio_symbols_C_intern ("c/EBADMSG");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADMSG), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADMSG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EBADR)
    err_sym = idio_symbols_C_intern ("c/EBADR");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADR), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (EBADRPC)
    err_sym = idio_symbols_C_intern ("c/EBADRPC");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADRPC), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADRPC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EBADRQC)
    err_sym = idio_symbols_C_intern ("c/EBADRQC");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADRQC), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADRQC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EBADSLT)
    err_sym = idio_symbols_C_intern ("c/EBADSLT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBADSLT), idio_main_module ());
    sprintf (idio_libc_errno_names[EBADSLT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EBFONT)
    err_sym = idio_symbols_C_intern ("c/EBFONT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBFONT), idio_main_module ());
    sprintf (idio_libc_errno_names[EBFONT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBUSY)
    err_sym = idio_symbols_C_intern ("c/EBUSY");
    idio_module_set_symbol_value (err_sym, idio_C_int (EBUSY), idio_main_module ());
    sprintf (idio_libc_errno_names[EBUSY], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECANCELED)
    err_sym = idio_symbols_C_intern ("c/ECANCELED");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECANCELED), idio_main_module ());
    sprintf (idio_libc_errno_names[ECANCELED], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD */
#if defined (ECAPMODE)
    err_sym = idio_symbols_C_intern ("c/ECAPMODE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECAPMODE), idio_main_module ());
    sprintf (idio_libc_errno_names[ECAPMODE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECHILD)
    err_sym = idio_symbols_C_intern ("c/ECHILD");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECHILD), idio_main_module ());
    sprintf (idio_libc_errno_names[ECHILD], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ECHRNG)
    err_sym = idio_symbols_C_intern ("c/ECHRNG");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECHRNG), idio_main_module ());
    sprintf (idio_libc_errno_names[ECHRNG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ECOMM)
    err_sym = idio_symbols_C_intern ("c/ECOMM");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECOMM), idio_main_module ());
    sprintf (idio_libc_errno_names[ECOMM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNABORTED)
    err_sym = idio_symbols_C_intern ("c/ECONNABORTED");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECONNABORTED), idio_main_module ());
    sprintf (idio_libc_errno_names[ECONNABORTED], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNREFUSED)
    err_sym = idio_symbols_C_intern ("c/ECONNREFUSED");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECONNREFUSED), idio_main_module ());
    sprintf (idio_libc_errno_names[ECONNREFUSED], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNRESET)
    err_sym = idio_symbols_C_intern ("c/ECONNRESET");
    idio_module_set_symbol_value (err_sym, idio_C_int (ECONNRESET), idio_main_module ());
    sprintf (idio_libc_errno_names[ECONNRESET], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDEADLK)
    err_sym = idio_symbols_C_intern ("c/EDEADLK");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDEADLK), idio_main_module ());
    sprintf (idio_libc_errno_names[EDEADLK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EDEADLOCK)
    err_sym = idio_symbols_C_intern ("c/EDEADLOCK");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDEADLOCK), idio_main_module ());
    sprintf (idio_libc_errno_names[EDEADLOCK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDESTADDRREQ)
    err_sym = idio_symbols_C_intern ("c/EDESTADDRREQ");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDESTADDRREQ), idio_main_module ());
    sprintf (idio_libc_errno_names[EDESTADDRREQ], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (EDEVERR)
    err_sym = idio_symbols_C_intern ("c/EDEVERR");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDEVERR), idio_main_module ());
    sprintf (idio_libc_errno_names[EDEVERR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDOM)
    err_sym = idio_symbols_C_intern ("c/EDOM");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDOM), idio_main_module ());
    sprintf (idio_libc_errno_names[EDOM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD */
#if defined (EDOOFUS)
    err_sym = idio_symbols_C_intern ("c/EDOOFUS");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDOOFUS), idio_main_module ());
    sprintf (idio_libc_errno_names[EDOOFUS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EDOTDOT)
    err_sym = idio_symbols_C_intern ("c/EDOTDOT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDOTDOT), idio_main_module ());
    sprintf (idio_libc_errno_names[EDOTDOT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDQUOT)
    err_sym = idio_symbols_C_intern ("c/EDQUOT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EDQUOT), idio_main_module ());
    sprintf (idio_libc_errno_names[EDQUOT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EEXIST)
    err_sym = idio_symbols_C_intern ("c/EEXIST");
    idio_module_set_symbol_value (err_sym, idio_C_int (EEXIST), idio_main_module ());
    sprintf (idio_libc_errno_names[EEXIST], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFAULT)
    err_sym = idio_symbols_C_intern ("c/EFAULT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EFAULT), idio_main_module ());
    sprintf (idio_libc_errno_names[EFAULT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFBIG)
    err_sym = idio_symbols_C_intern ("c/EFBIG");
    idio_module_set_symbol_value (err_sym, idio_C_int (EFBIG), idio_main_module ());
    sprintf (idio_libc_errno_names[EFBIG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (EFTYPE)
    err_sym = idio_symbols_C_intern ("c/EFTYPE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EFTYPE), idio_main_module ());
    sprintf (idio_libc_errno_names[EFTYPE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTDOWN)
    err_sym = idio_symbols_C_intern ("c/EHOSTDOWN");
    idio_module_set_symbol_value (err_sym, idio_C_int (EHOSTDOWN), idio_main_module ());
    sprintf (idio_libc_errno_names[EHOSTDOWN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTUNREACH)
    err_sym = idio_symbols_C_intern ("c/EHOSTUNREACH");
    idio_module_set_symbol_value (err_sym, idio_C_int (EHOSTUNREACH), idio_main_module ());
    sprintf (idio_libc_errno_names[EHOSTUNREACH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EHWPOISON)
    err_sym = idio_symbols_C_intern ("c/EHWPOISON");
    idio_module_set_symbol_value (err_sym, idio_C_int (EHWPOISON), idio_main_module ());
    sprintf (idio_libc_errno_names[EHWPOISON], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIDRM)
    err_sym = idio_symbols_C_intern ("c/EIDRM");
    idio_module_set_symbol_value (err_sym, idio_C_int (EIDRM), idio_main_module ());
    sprintf (idio_libc_errno_names[EIDRM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EILSEQ)
    err_sym = idio_symbols_C_intern ("c/EILSEQ");
    idio_module_set_symbol_value (err_sym, idio_C_int (EILSEQ), idio_main_module ());
    sprintf (idio_libc_errno_names[EILSEQ], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINPROGRESS)
    err_sym = idio_symbols_C_intern ("c/EINPROGRESS");
    idio_module_set_symbol_value (err_sym, idio_C_int (EINPROGRESS), idio_main_module ());
    sprintf (idio_libc_errno_names[EINPROGRESS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINTR)
    err_sym = idio_symbols_C_intern ("c/EINTR");
    idio_module_set_symbol_value (err_sym, idio_C_int (EINTR), idio_main_module ());
    sprintf (idio_libc_errno_names[EINTR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINVAL)
    err_sym = idio_symbols_C_intern ("c/EINVAL");
    idio_module_set_symbol_value (err_sym, idio_C_int (EINVAL), idio_main_module ());
    sprintf (idio_libc_errno_names[EINVAL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIO)
    err_sym = idio_symbols_C_intern ("c/EIO");
    idio_module_set_symbol_value (err_sym, idio_C_int (EIO), idio_main_module ());
    sprintf (idio_libc_errno_names[EIO], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISCONN)
    err_sym = idio_symbols_C_intern ("c/EISCONN");
    idio_module_set_symbol_value (err_sym, idio_C_int (EISCONN), idio_main_module ());
    sprintf (idio_libc_errno_names[EISCONN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISDIR)
    err_sym = idio_symbols_C_intern ("c/EISDIR");
    idio_module_set_symbol_value (err_sym, idio_C_int (EISDIR), idio_main_module ());
    sprintf (idio_libc_errno_names[EISDIR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EISNAM)
    err_sym = idio_symbols_C_intern ("c/EISNAM");
    idio_module_set_symbol_value (err_sym, idio_C_int (EISNAM), idio_main_module ());
    sprintf (idio_libc_errno_names[EISNAM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EKEYEXPIRED)
    err_sym = idio_symbols_C_intern ("c/EKEYEXPIRED");
    idio_module_set_symbol_value (err_sym, idio_C_int (EKEYEXPIRED), idio_main_module ());
    sprintf (idio_libc_errno_names[EKEYEXPIRED], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EKEYREJECTED)
    err_sym = idio_symbols_C_intern ("c/EKEYREJECTED");
    idio_module_set_symbol_value (err_sym, idio_C_int (EKEYREJECTED), idio_main_module ());
    sprintf (idio_libc_errno_names[EKEYREJECTED], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EKEYREVOKED)
    err_sym = idio_symbols_C_intern ("c/EKEYREVOKED");
    idio_module_set_symbol_value (err_sym, idio_C_int (EKEYREVOKED), idio_main_module ());
    sprintf (idio_libc_errno_names[EKEYREVOKED], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EL2HLT)
    err_sym = idio_symbols_C_intern ("c/EL2HLT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EL2HLT), idio_main_module ());
    sprintf (idio_libc_errno_names[EL2HLT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EL2NSYNC)
    err_sym = idio_symbols_C_intern ("c/EL2NSYNC");
    idio_module_set_symbol_value (err_sym, idio_C_int (EL2NSYNC), idio_main_module ());
    sprintf (idio_libc_errno_names[EL2NSYNC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EL3HLT)
    err_sym = idio_symbols_C_intern ("c/EL3HLT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EL3HLT), idio_main_module ());
    sprintf (idio_libc_errno_names[EL3HLT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EL3RST)
    err_sym = idio_symbols_C_intern ("c/EL3RST");
    idio_module_set_symbol_value (err_sym, idio_C_int (EL3RST), idio_main_module ());
    sprintf (idio_libc_errno_names[EL3RST], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ELIBACC)
    err_sym = idio_symbols_C_intern ("c/ELIBACC");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELIBACC), idio_main_module ());
    sprintf (idio_libc_errno_names[ELIBACC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ELIBBAD)
    err_sym = idio_symbols_C_intern ("c/ELIBBAD");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELIBBAD), idio_main_module ());
    sprintf (idio_libc_errno_names[ELIBBAD], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ELIBEXEC)
    err_sym = idio_symbols_C_intern ("c/ELIBEXEC");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELIBEXEC), idio_main_module ());
    sprintf (idio_libc_errno_names[ELIBEXEC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ELIBMAX)
    err_sym = idio_symbols_C_intern ("c/ELIBMAX");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELIBMAX), idio_main_module ());
    sprintf (idio_libc_errno_names[ELIBMAX], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ELIBSCN)
    err_sym = idio_symbols_C_intern ("c/ELIBSCN");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELIBSCN), idio_main_module ());
    sprintf (idio_libc_errno_names[ELIBSCN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ELNRNG)
    err_sym = idio_symbols_C_intern ("c/ELNRNG");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELNRNG), idio_main_module ());
    sprintf (idio_libc_errno_names[ELNRNG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Solaris */
#if defined (ELOCKUNMAPPED)
    err_sym = idio_symbols_C_intern ("c/ELOCKUNMAPPED");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELOCKUNMAPPED), idio_main_module ());
    sprintf (idio_libc_errno_names[ELOCKUNMAPPED], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ELOOP)
    err_sym = idio_symbols_C_intern ("c/ELOOP");
    idio_module_set_symbol_value (err_sym, idio_C_int (ELOOP), idio_main_module ());
    sprintf (idio_libc_errno_names[ELOOP], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EMEDIUMTYPE)
    err_sym = idio_symbols_C_intern ("c/EMEDIUMTYPE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EMEDIUMTYPE), idio_main_module ());
    sprintf (idio_libc_errno_names[EMEDIUMTYPE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMFILE)
    err_sym = idio_symbols_C_intern ("c/EMFILE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EMFILE), idio_main_module ());
    sprintf (idio_libc_errno_names[EMFILE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMLINK)
    err_sym = idio_symbols_C_intern ("c/EMLINK");
    idio_module_set_symbol_value (err_sym, idio_C_int (EMLINK), idio_main_module ());
    sprintf (idio_libc_errno_names[EMLINK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMSGSIZE)
    err_sym = idio_symbols_C_intern ("c/EMSGSIZE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EMSGSIZE), idio_main_module ());
    sprintf (idio_libc_errno_names[EMSGSIZE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMULTIHOP)
    err_sym = idio_symbols_C_intern ("c/EMULTIHOP");
    idio_module_set_symbol_value (err_sym, idio_C_int (EMULTIHOP), idio_main_module ());
    sprintf (idio_libc_errno_names[EMULTIHOP], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENAMETOOLONG)
    err_sym = idio_symbols_C_intern ("c/ENAMETOOLONG");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENAMETOOLONG), idio_main_module ());
    sprintf (idio_libc_errno_names[ENAMETOOLONG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (ENAVAIL)
    err_sym = idio_symbols_C_intern ("c/ENAVAIL");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENAVAIL), idio_main_module ());
    sprintf (idio_libc_errno_names[ENAVAIL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (ENEEDAUTH)
    err_sym = idio_symbols_C_intern ("c/ENEEDAUTH");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENEEDAUTH), idio_main_module ());
    sprintf (idio_libc_errno_names[ENEEDAUTH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETDOWN)
    err_sym = idio_symbols_C_intern ("c/ENETDOWN");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENETDOWN), idio_main_module ());
    sprintf (idio_libc_errno_names[ENETDOWN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETRESET)
    err_sym = idio_symbols_C_intern ("c/ENETRESET");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENETRESET), idio_main_module ());
    sprintf (idio_libc_errno_names[ENETRESET], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETUNREACH)
    err_sym = idio_symbols_C_intern ("c/ENETUNREACH");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENETUNREACH), idio_main_module ());
    sprintf (idio_libc_errno_names[ENETUNREACH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENFILE)
    err_sym = idio_symbols_C_intern ("c/ENFILE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENFILE), idio_main_module ());
    sprintf (idio_libc_errno_names[ENFILE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ENOANO)
    err_sym = idio_symbols_C_intern ("c/ENOANO");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOANO), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOANO], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (ENOATTR)
    err_sym = idio_symbols_C_intern ("c/ENOATTR");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOATTR), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOATTR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOBUFS)
    err_sym = idio_symbols_C_intern ("c/ENOBUFS");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOBUFS), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOBUFS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ENOCSI)
    err_sym = idio_symbols_C_intern ("c/ENOCSI");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOCSI), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOCSI], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENODATA)
    err_sym = idio_symbols_C_intern ("c/ENODATA");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENODATA), idio_main_module ());
    sprintf (idio_libc_errno_names[ENODATA], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENODEV)
    err_sym = idio_symbols_C_intern ("c/ENODEV");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENODEV), idio_main_module ());
    sprintf (idio_libc_errno_names[ENODEV], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOENT)
    err_sym = idio_symbols_C_intern ("c/ENOENT");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOENT), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOENT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOEXEC)
    err_sym = idio_symbols_C_intern ("c/ENOEXEC");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOEXEC), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOEXEC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (ENOKEY)
    err_sym = idio_symbols_C_intern ("c/ENOKEY");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOKEY), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOKEY], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLCK)
    err_sym = idio_symbols_C_intern ("c/ENOLCK");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOLCK), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOLCK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLINK)
    err_sym = idio_symbols_C_intern ("c/ENOLINK");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOLINK), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOLINK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (ENOMEDIUM)
    err_sym = idio_symbols_C_intern ("c/ENOMEDIUM");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOMEDIUM), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOMEDIUM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMEM)
    err_sym = idio_symbols_C_intern ("c/ENOMEM");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOMEM), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOMEM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMSG)
    err_sym = idio_symbols_C_intern ("c/ENOMSG");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOMSG), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOMSG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ENONET)
    err_sym = idio_symbols_C_intern ("c/ENONET");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENONET), idio_main_module ());
    sprintf (idio_libc_errno_names[ENONET], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ENOPKG)
    err_sym = idio_symbols_C_intern ("c/ENOPKG");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOPKG), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOPKG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (ENOPOLICY)
    err_sym = idio_symbols_C_intern ("c/ENOPOLICY");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOPOLICY), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOPOLICY], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOPROTOOPT)
    err_sym = idio_symbols_C_intern ("c/ENOPROTOOPT");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOPROTOOPT), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOPROTOOPT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSPC)
    err_sym = idio_symbols_C_intern ("c/ENOSPC");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOSPC), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOSPC], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSR)
    err_sym = idio_symbols_C_intern ("c/ENOSR");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOSR), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOSR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSTR)
    err_sym = idio_symbols_C_intern ("c/ENOSTR");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOSTR), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOSTR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSYS)
    err_sym = idio_symbols_C_intern ("c/ENOSYS");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOSYS), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOSYS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Solaris */
#if defined (ENOTACTIVE)
    err_sym = idio_symbols_C_intern ("c/ENOTACTIVE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTACTIVE), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTACTIVE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTBLK)
    err_sym = idio_symbols_C_intern ("c/ENOTBLK");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTBLK), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTBLK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD */
#if defined (ENOTCAPABLE)
    err_sym = idio_symbols_C_intern ("c/ENOTCAPABLE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTCAPABLE), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTCAPABLE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTCONN)
    err_sym = idio_symbols_C_intern ("c/ENOTCONN");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTCONN), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTCONN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTDIR)
    err_sym = idio_symbols_C_intern ("c/ENOTDIR");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTDIR), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTDIR], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTEMPTY)
    err_sym = idio_symbols_C_intern ("c/ENOTEMPTY");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTEMPTY), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTEMPTY], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (ENOTNAM)
    err_sym = idio_symbols_C_intern ("c/ENOTNAM");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTNAM), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTNAM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTRECOVERABLE)
    err_sym = idio_symbols_C_intern ("c/ENOTRECOVERABLE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTRECOVERABLE), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTRECOVERABLE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTSOCK)
    err_sym = idio_symbols_C_intern ("c/ENOTSOCK");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTSOCK), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTSOCK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX, Solaris */
#if defined (ENOTSUP)
    err_sym = idio_symbols_C_intern ("c/ENOTSUP");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTSUP), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTSUP], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTTY)
    err_sym = idio_symbols_C_intern ("c/ENOTTY");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTTY), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTTY], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ENOTUNIQ)
    err_sym = idio_symbols_C_intern ("c/ENOTUNIQ");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENOTUNIQ), idio_main_module ());
    sprintf (idio_libc_errno_names[ENOTUNIQ], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENXIO)
    err_sym = idio_symbols_C_intern ("c/ENXIO");
    idio_module_set_symbol_value (err_sym, idio_C_int (ENXIO), idio_main_module ());
    sprintf (idio_libc_errno_names[ENXIO], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, OSX, Solaris */
#if defined (EOPNOTSUPP)
    err_sym = idio_symbols_C_intern ("c/EOPNOTSUPP");
    idio_module_set_symbol_value (err_sym, idio_C_int (EOPNOTSUPP), idio_main_module ());
    sprintf (idio_libc_errno_names[EOPNOTSUPP], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOVERFLOW)
    err_sym = idio_symbols_C_intern ("c/EOVERFLOW");
    idio_module_set_symbol_value (err_sym, idio_C_int (EOVERFLOW), idio_main_module ());
    sprintf (idio_libc_errno_names[EOVERFLOW], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOWNERDEAD)
    err_sym = idio_symbols_C_intern ("c/EOWNERDEAD");
    idio_module_set_symbol_value (err_sym, idio_C_int (EOWNERDEAD), idio_main_module ());
    sprintf (idio_libc_errno_names[EOWNERDEAD], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPERM)
    err_sym = idio_symbols_C_intern ("c/EPERM");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPERM), idio_main_module ());
    sprintf (idio_libc_errno_names[EPERM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPFNOSUPPORT)
    err_sym = idio_symbols_C_intern ("c/EPFNOSUPPORT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPFNOSUPPORT), idio_main_module ());
    sprintf (idio_libc_errno_names[EPFNOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPIPE)
    err_sym = idio_symbols_C_intern ("c/EPIPE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPIPE), idio_main_module ());
    sprintf (idio_libc_errno_names[EPIPE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCLIM)
    err_sym = idio_symbols_C_intern ("c/EPROCLIM");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPROCLIM), idio_main_module ());
    sprintf (idio_libc_errno_names[EPROCLIM], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCUNAVAIL)
    err_sym = idio_symbols_C_intern ("c/EPROCUNAVAIL");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPROCUNAVAIL), idio_main_module ());
    sprintf (idio_libc_errno_names[EPROCUNAVAIL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGMISMATCH)
    err_sym = idio_symbols_C_intern ("c/EPROGMISMATCH");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPROGMISMATCH), idio_main_module ());
    sprintf (idio_libc_errno_names[EPROGMISMATCH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGUNAVAIL)
    err_sym = idio_symbols_C_intern ("c/EPROGUNAVAIL");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPROGUNAVAIL), idio_main_module ());
    sprintf (idio_libc_errno_names[EPROGUNAVAIL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTO)
    err_sym = idio_symbols_C_intern ("c/EPROTO");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPROTO), idio_main_module ());
    sprintf (idio_libc_errno_names[EPROTO], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTONOSUPPORT)
    err_sym = idio_symbols_C_intern ("c/EPROTONOSUPPORT");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPROTONOSUPPORT), idio_main_module ());
    sprintf (idio_libc_errno_names[EPROTONOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTOTYPE)
    err_sym = idio_symbols_C_intern ("c/EPROTOTYPE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPROTOTYPE), idio_main_module ());
    sprintf (idio_libc_errno_names[EPROTOTYPE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (EPWROFF)
    err_sym = idio_symbols_C_intern ("c/EPWROFF");
    idio_module_set_symbol_value (err_sym, idio_C_int (EPWROFF), idio_main_module ());
    sprintf (idio_libc_errno_names[EPWROFF], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (EQFULL)
    err_sym = idio_symbols_C_intern ("c/EQFULL");
    idio_module_set_symbol_value (err_sym, idio_C_int (EQFULL), idio_main_module ());
    sprintf (idio_libc_errno_names[EQFULL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ERANGE)
    err_sym = idio_symbols_C_intern ("c/ERANGE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ERANGE), idio_main_module ());
    sprintf (idio_libc_errno_names[ERANGE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EREMCHG)
    err_sym = idio_symbols_C_intern ("c/EREMCHG");
    idio_module_set_symbol_value (err_sym, idio_C_int (EREMCHG), idio_main_module ());
    sprintf (idio_libc_errno_names[EREMCHG], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EREMOTE)
    err_sym = idio_symbols_C_intern ("c/EREMOTE");
    idio_module_set_symbol_value (err_sym, idio_C_int (EREMOTE), idio_main_module ());
    sprintf (idio_libc_errno_names[EREMOTE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EREMOTEIO)
    err_sym = idio_symbols_C_intern ("c/EREMOTEIO");
    idio_module_set_symbol_value (err_sym, idio_C_int (EREMOTEIO), idio_main_module ());
    sprintf (idio_libc_errno_names[EREMOTEIO], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ERESTART)
    err_sym = idio_symbols_C_intern ("c/ERESTART");
    idio_module_set_symbol_value (err_sym, idio_C_int (ERESTART), idio_main_module ());
    sprintf (idio_libc_errno_names[ERESTART], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (ERFKILL)
    err_sym = idio_symbols_C_intern ("c/ERFKILL");
    idio_module_set_symbol_value (err_sym, idio_C_int (ERFKILL), idio_main_module ());
    sprintf (idio_libc_errno_names[ERFKILL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EROFS)
    err_sym = idio_symbols_C_intern ("c/EROFS");
    idio_module_set_symbol_value (err_sym, idio_C_int (EROFS), idio_main_module ());
    sprintf (idio_libc_errno_names[EROFS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, OSX */
#if defined (ERPCMISMATCH)
    err_sym = idio_symbols_C_intern ("c/ERPCMISMATCH");
    idio_module_set_symbol_value (err_sym, idio_C_int (ERPCMISMATCH), idio_main_module ());
    sprintf (idio_libc_errno_names[ERPCMISMATCH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* OSX */
#if defined (ESHLIBVERS)
    err_sym = idio_symbols_C_intern ("c/ESHLIBVERS");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESHLIBVERS), idio_main_module ());
    sprintf (idio_libc_errno_names[ESHLIBVERS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESHUTDOWN)
    err_sym = idio_symbols_C_intern ("c/ESHUTDOWN");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESHUTDOWN), idio_main_module ());
    sprintf (idio_libc_errno_names[ESHUTDOWN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESOCKTNOSUPPORT)
    err_sym = idio_symbols_C_intern ("c/ESOCKTNOSUPPORT");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESOCKTNOSUPPORT), idio_main_module ());
    sprintf (idio_libc_errno_names[ESOCKTNOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESPIPE)
    err_sym = idio_symbols_C_intern ("c/ESPIPE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESPIPE), idio_main_module ());
    sprintf (idio_libc_errno_names[ESPIPE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESRCH)
    err_sym = idio_symbols_C_intern ("c/ESRCH");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESRCH), idio_main_module ());
    sprintf (idio_libc_errno_names[ESRCH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ESRMNT)
    err_sym = idio_symbols_C_intern ("c/ESRMNT");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESRMNT), idio_main_module ());
    sprintf (idio_libc_errno_names[ESRMNT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESTALE)
    err_sym = idio_symbols_C_intern ("c/ESTALE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESTALE), idio_main_module ());
    sprintf (idio_libc_errno_names[ESTALE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (ESTRPIPE)
    err_sym = idio_symbols_C_intern ("c/ESTRPIPE");
    idio_module_set_symbol_value (err_sym, idio_C_int (ESTRPIPE), idio_main_module ());
    sprintf (idio_libc_errno_names[ESTRPIPE], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, OSX, Solaris */
#if defined (ETIME)
    err_sym = idio_symbols_C_intern ("c/ETIME");
    idio_module_set_symbol_value (err_sym, idio_C_int (ETIME), idio_main_module ());
    sprintf (idio_libc_errno_names[ETIME], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETIMEDOUT)
    err_sym = idio_symbols_C_intern ("c/ETIMEDOUT");
    idio_module_set_symbol_value (err_sym, idio_C_int (ETIMEDOUT), idio_main_module ());
    sprintf (idio_libc_errno_names[ETIMEDOUT], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETOOMANYREFS)
    err_sym = idio_symbols_C_intern ("c/ETOOMANYREFS");
    idio_module_set_symbol_value (err_sym, idio_C_int (ETOOMANYREFS), idio_main_module ());
    sprintf (idio_libc_errno_names[ETOOMANYREFS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETXTBSY)
    err_sym = idio_symbols_C_intern ("c/ETXTBSY");
    idio_module_set_symbol_value (err_sym, idio_C_int (ETXTBSY), idio_main_module ());
    sprintf (idio_libc_errno_names[ETXTBSY], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux */
#if defined (EUCLEAN)
    err_sym = idio_symbols_C_intern ("c/EUCLEAN");
    idio_module_set_symbol_value (err_sym, idio_C_int (EUCLEAN), idio_main_module ());
    sprintf (idio_libc_errno_names[EUCLEAN], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EUNATCH)
    err_sym = idio_symbols_C_intern ("c/EUNATCH");
    idio_module_set_symbol_value (err_sym, idio_C_int (EUNATCH), idio_main_module ());
    sprintf (idio_libc_errno_names[EUNATCH], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EUSERS)
    err_sym = idio_symbols_C_intern ("c/EUSERS");
    idio_module_set_symbol_value (err_sym, idio_C_int (EUSERS), idio_main_module ());
    sprintf (idio_libc_errno_names[EUSERS], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EWOULDBLOCK)
    err_sym = idio_symbols_C_intern ("c/EWOULDBLOCK");
    idio_module_set_symbol_value (err_sym, idio_C_int (EWOULDBLOCK), idio_main_module ());
    sprintf (idio_libc_errno_names[EWOULDBLOCK], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EXDEV)
    err_sym = idio_symbols_C_intern ("c/EXDEV");
    idio_module_set_symbol_value (err_sym, idio_C_int (EXDEV), idio_main_module ());
    sprintf (idio_libc_errno_names[EXDEV], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

    /* Linux, Solaris */
#if defined (EXFULL)
    err_sym = idio_symbols_C_intern ("c/EXFULL");
    idio_module_set_symbol_value (err_sym, idio_C_int (EXFULL), idio_main_module ());
    sprintf (idio_libc_errno_names[EXFULL], "%s", IDIO_SYMBOL_S (err_sym) + 2);
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FERRNO ; i < IDIO_LIBC_NERRNO ; i++) {
	if ('\0' == *(idio_libc_errno_names[i])) {
	    char err_name[IDIO_LIBC_ERRNAMELEN + 2];
	    sprintf (err_name, "c/ERRNO%d", i);
	    err_sym = idio_symbols_C_intern (err_name);
	    idio_module_set_symbol_value (err_sym, idio_C_int (i), idio_main_module ());
	    sprintf (idio_libc_errno_names[i], "%s", err_name + 2);
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

    return idio_libc_errno_names[errnum];
}

IDIO_DEFINE_PRIMITIVE1 ("c/errno-name", C_errno_name, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);
    IDIO_VERIFY_PARAM_TYPE (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0 ("c/errno-names", C_errno_names, ())
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
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/0U"), idio_C_uint (0U), idio_main_module ());

    /* fcntl.h */
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/F_DUPFD"), idio_C_int (F_DUPFD), idio_main_module ());
#if defined (F_DUPFD_CLOEXEC)
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/F_DUPFD_CLOEXEC"), idio_C_int (F_DUPFD_CLOEXEC), idio_main_module ());
#endif
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/FD_CLOEXEC"), idio_C_int (FD_CLOEXEC), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/F_GETFD"), idio_C_int (F_GETFD), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/F_SETFD"), idio_C_int (F_SETFD), idio_main_module ());

    /* signal.h */
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIG_DFL"), idio_C_pointer (SIG_DFL), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/SIG_IGN"), idio_C_pointer (SIG_IGN), idio_main_module ());
    
    /* stdio.h */
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/NULL"), idio_C_pointer (NULL), idio_main_module ());

    /* stdint.h */
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/INTMAX_MAX"), idio_C_int (INTMAX_MAX), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/INTMAX_MIN"), idio_C_int (INTMAX_MIN), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/UINTMAX_MAX"), idio_C_uint (UINTMAX_MAX), idio_main_module ());

    /* sys/wait.h */
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WAIT_ANY"), idio_C_int (WAIT_ANY), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WNOHANG"), idio_C_int (WNOHANG), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WUNTRACED"), idio_C_int (WUNTRACED), idio_main_module ());

    /* termios.h */
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/TCSADRAIN"), idio_C_int (TCSADRAIN), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/TCSAFLUSH"), idio_C_int (TCSAFLUSH), idio_main_module ());
    
    /* unistd.h */
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/PATH_MAX"), idio_C_int (PATH_MAX), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/STDIN_FILENO"), idio_C_int (STDIN_FILENO), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/STDOUT_FILENO"), idio_C_int (STDOUT_FILENO), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/STDERR_FILENO"), idio_C_int (STDERR_FILENO), idio_main_module ());
    
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
    IDIO_ADD_PRIMITIVE (C_fcntl);
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

    IDIO_ADD_PRIMITIVE (C_sig_name);
    IDIO_ADD_PRIMITIVE (C_sig_names);
    IDIO_ADD_PRIMITIVE (C_signal_name);
    IDIO_ADD_PRIMITIVE (C_signal_names);
    IDIO_ADD_PRIMITIVE (C_errno_name);
    IDIO_ADD_PRIMITIVE (C_errno_names);
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

