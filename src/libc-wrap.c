/*
 * Copyright (c) 2015, 2017, 2018, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

IDIO idio_libc_wrap_module = idio_S_nil;
IDIO idio_vm_signal_handler_conditions;
char **idio_libc_signal_names = NULL;
char **idio_libc_errno_names = NULL;
char **idio_libc_rlimit_names = NULL;
static IDIO idio_libc_struct_sigaction = NULL;
static IDIO idio_libc_struct_utsname = NULL;
static IDIO idio_libc_struct_rlimit = NULL;
IDIO idio_libc_struct_stat = NULL;

/*
 * Indexes into structures for direct references
 */
#define IDIO_STRUCT_SIGACTION_SA_HANDLER	0
#define IDIO_STRUCT_SIGACTION_SA_SIGACTION	1
#define IDIO_STRUCT_SIGACTION_SA_MASK		2
#define IDIO_STRUCT_SIGACTION_SA_FLAGS		3

#define IDIO_STRUCT_UTSNAME_SYSNAME		0
#define IDIO_STRUCT_UTSNAME_NODENAME		1
#define IDIO_STRUCT_UTSNAME_RELEASE		2
#define IDIO_STRUCT_UTSNAME_VERSION		3
#define IDIO_STRUCT_UTSNAME_MACHINE		4

#define IDIO_STRUCT_RLIMIT_RLIM_CUR		0
#define IDIO_STRUCT_RLIMIT_RLIM_MAX		1

IDIO idio_libc_export_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_export_symbol_value (symbol, value, idio_libc_wrap_module);
}

IDIO_DEFINE_PRIMITIVE0V ("system-error", libc_system_error, (IDIO args))
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    char *name = "n/k";

    if (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	if (idio_isa_string (h)) {
	    name = idio_string_as_C (h);
	    args = IDIO_PAIR_T (args);
	} else if (idio_isa_symbol (h)) {
	    name = IDIO_SYMBOL_S (h);
	    args = IDIO_PAIR_T (args);
	}
    }

    idio_error_system_errno (name, args, IDIO_C_LOCATION ("system-error"));

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2 ("access", libc_access, (IDIO ipath, IDIO imode))
{
    IDIO_ASSERT (ipath);
    IDIO_ASSERT (imode);
    IDIO_VERIFY_PARAM_TYPE (string, ipath);
    IDIO_VERIFY_PARAM_TYPE (C_int, imode);

    char *path = idio_string_as_C (ipath);
    int mode = IDIO_C_TYPE_INT (imode);

    IDIO r = idio_S_false;

    if (0 == access (path, mode)) {
	r = idio_S_true;
    }

    free (path);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("close", libc_close, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = close (fd);

    if (-1 == r) {
	idio_error_system_errno ("close", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("close"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("dup", libc_dup, (IDIO ioldfd))
{
    IDIO_ASSERT (ioldfd);

    int oldfd = -1;
    if (idio_isa_fixnum (ioldfd)) {
	oldfd = IDIO_FIXNUM_VAL (ioldfd);
    } else if (idio_isa_C_int (ioldfd)) {
	oldfd = IDIO_C_TYPE_INT (ioldfd);
    } else {
	idio_error_param_type ("fixnum|C_int ioldfd", ioldfd, IDIO_C_LOCATION ("dup"));
    }

    int r = dup (oldfd);

    if (-1 == r) {
	idio_error_system_errno ("dup", IDIO_LIST1 (ioldfd), IDIO_C_LOCATION ("dup"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("dup2", libc_dup2, (IDIO ioldfd, IDIO inewfd))
{
    IDIO_ASSERT (ioldfd);
    IDIO_ASSERT (inewfd);

    int oldfd = -1;
    if (idio_isa_fixnum (ioldfd)) {
	oldfd = IDIO_FIXNUM_VAL (ioldfd);
    } else if (idio_isa_C_int (ioldfd)) {
	oldfd = IDIO_C_TYPE_INT (ioldfd);
    } else {
	idio_error_param_type ("fixnum|C_int ioldfd", ioldfd, IDIO_C_LOCATION ("dup"));
    }

    int newfd = -1;
    if (idio_isa_fixnum (inewfd)) {
	newfd = IDIO_FIXNUM_VAL (inewfd);
    } else if (idio_isa_C_int (inewfd)) {
	newfd = IDIO_C_TYPE_INT (inewfd);
    } else {
	idio_error_param_type ("fixnum|C_int inewfd", inewfd, IDIO_C_LOCATION ("dup"));
    }


    int r = dup2 (oldfd, newfd);

    if (-1 == r) {
	idio_error_system_errno ("dup2", IDIO_LIST2 (ioldfd, inewfd), IDIO_C_LOCATION ("dup2"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("exit", libc_exit, (IDIO istatus))
{
    IDIO_ASSERT (istatus);

    int status = 0;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else {
	idio_error_param_type ("fixnum", istatus, IDIO_C_LOCATION ("exit"));
    }

    exit (status);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2V ("fcntl", libc_fcntl, (IDIO ifd, IDIO icmd, IDIO args))
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
	idio_error_param_type ("fixnum|C_int ifd", ifd, IDIO_C_LOCATION ("fcntl"));
    }

    int cmd = 0;
    if (idio_isa_C_int (icmd)) {
	cmd = IDIO_C_TYPE_INT (icmd);
    } else {
	idio_error_param_type ("C_int icmd", icmd, IDIO_C_LOCATION ("fcntl"));
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
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_LOCATION ("fcntl"));

		return idio_S_notreached;
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
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_LOCATION ("fcntl"));

		return idio_S_notreached;
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
		idio_error_param_type ("C_int", iarg, IDIO_C_LOCATION ("fcntl"));

		return idio_S_notreached;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
    default:
	idio_error_C ("unexpected cmd", IDIO_LIST2 (ifd, icmd), IDIO_C_LOCATION ("fcntl"));

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("fileno", libc_fileno, (IDIO ifilep))
{
    IDIO_ASSERT (ifilep);

    int fd = 0;
    if (idio_isa_file_handle (ifilep)) {
	fd = idio_file_handle_fd (ifilep);
    } else {
	idio_error_param_type ("file-handle", ifilep, IDIO_C_LOCATION ("fileno"));

	return idio_S_notreached;
    }

    return idio_C_int (fd);
}

IDIO_DEFINE_PRIMITIVE0 ("fork", libc_fork, ())
{
    pid_t pid = fork ();

    if (-1 == pid) {
	idio_error_system_errno ("fork", idio_S_nil, IDIO_C_LOCATION ("fork"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("getcwd", libc_getcwd, ())
{
    /*
     * getcwd(3) and its arguments
     *
     * A sensible {size}?
     *
     * PATH_MAX varies: POSIX is 256, CentOS 7 is 4096
     *
     * The Linux man page for realpath(3) suggests that calling
     * pathconf(3) for _PC_PATH_MAX doesn't improve matters a whole
     * bunch as it can return a value that is infeasible to allocate
     * in memory.
     *
     * Some systems (OS X, FreeBSD) suggest getcwd(3) should accept
     * MAXPATHLEN (which is #define'd as PATH_MAX in <sys/param.h>).
     *
     * A NULL {buf}?
     *
     * Some systems (older OS X, FreeBSD) do not support a zero {size}
     * parameter.  If passed a NULL {buf}, those systems seem to
     * allocate as much memory as is required to contain the result,
     * regardless of {size}.
     *
     * On systems that do support a zero {size} parameter then they
     * will limit themselves to allocating a maximum of {size} bytes
     * if passed a NULL {buf} and a non-zero {size}.
     *
     * Given that we can't set {size} to zero on some systems then
     * always set {size} to PATH_MAX which should be be enough.
     *
     * If getcwd(3) returns a value that consumes all of PATH_MAX (or
     * more) then we're doomed to hit other problems in the near
     * future anyway as other parts of the system try to use the
     * result.
     */
    char *cwd = getcwd (NULL, PATH_MAX);

    if (NULL == cwd) {
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_LOCATION ("getcwd"));
    }

    IDIO r = idio_string_C (cwd);
    free (cwd);

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("getpgrp", libc_getpgrp, ())
{
    pid_t pid = getpgrp ();

    if (-1 == pid) {
	idio_error_system_errno ("getpgrp", idio_S_nil, IDIO_C_LOCATION ("getpgrp"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0 ("getpid", libc_getpid, ())
{
    pid_t pid = getpid ();

    if (-1 == pid) {
	idio_error_system_errno ("getpid", idio_S_nil, IDIO_C_LOCATION ("getpid"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE1 ("isatty", libc_isatty, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = isatty (fd);

    if (0 == r) {
	idio_error_system_errno ("isatty", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("isatty"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("kill", libc_kill, (IDIO ipid, IDIO isig))
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (isig);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipid);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int sig = IDIO_C_TYPE_INT (isig);

    int r = kill (pid, sig);

    if (-1 == r) {
	idio_error_system_errno ("kill", IDIO_LIST2 (ipid, isig), IDIO_C_LOCATION ("kill"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1 ("mkdtemp", libc_mkdtemp, (IDIO idirname))
{
    IDIO_ASSERT (idirname);
    IDIO_VERIFY_PARAM_TYPE (string, idirname);

    /*
     * XXX mkdtemp() requires a NUL-terminated C string and it will
     * modify the template part.
     *
     * If we are passed a SUBSTRING then we must substitute a
     * NUL-terminated C string and copy the result back.
     */
    char *dirname = idio_string_s (idirname);

    int isa_substring = 0;
    if (idio_isa_substring (idirname)) {
	isa_substring = 1;
	dirname = idio_string_as_C (idirname);
    }

    char *d = mkdtemp (dirname);

    if (NULL == d) {
	idio_error_system_errno ("mkdtemp", IDIO_LIST1 (idirname), IDIO_C_LOCATION ("mkdtemp"));
    }

    if (isa_substring) {
	memcpy (idio_string_s (idirname), dirname, idio_string_blen (idirname));
	free (dirname);
    }

    return idio_string_C (d);
}

IDIO_DEFINE_PRIMITIVE1 ("mkstemp", libc_mkstemp, (IDIO ifilename))
{
    IDIO_ASSERT (ifilename);
    IDIO_VERIFY_PARAM_TYPE (string, ifilename);

    /*
     * XXX mkstemp() requires a NUL-terminated C string and it will
     * modify the template part.
     *
     * If we are passed a SUBSTRING then we must substitute a
     * NUL-terminated C string and copy the result back.
     */
    char *filename = idio_string_s (ifilename);

    int isa_substring = 0;
    if (idio_isa_substring (ifilename)) {
	isa_substring = 1;
	filename = idio_string_as_C (ifilename);
    }

    int r = mkstemp (filename);

    if (-1 == r) {
	idio_error_system_errno ("mkstemp", IDIO_LIST1 (ifilename), IDIO_C_LOCATION ("mkstemp"));
    }

    if (isa_substring) {
	memcpy (idio_string_s (ifilename), filename, idio_string_blen (ifilename));
	free (filename);
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0 ("pipe", libc_pipe, ())
{
    int *pipefd = idio_alloc (2 * sizeof (int));

    int r = pipe (pipefd);

    if (-1 == r) {
	idio_error_system_errno ("pipe", idio_S_nil, IDIO_C_LOCATION ("pipe"));
    }

    return idio_C_pointer_free_me (pipefd);
}

IDIO_DEFINE_PRIMITIVE1 ("pipe-reader", libc_pipe_reader, (IDIO ipipefd))
{
    IDIO_ASSERT (ipipefd);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[0]);
}

IDIO_DEFINE_PRIMITIVE1 ("pipe-writer", libc_pipe_writer, (IDIO ipipefd))
{
    IDIO_ASSERT (ipipefd);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[1]);
}

IDIO_DEFINE_PRIMITIVE1V ("read", libc_read, (IDIO ifd, IDIO icount))
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
	    idio_error_param_type ("fixnum|C_int", icount, IDIO_C_LOCATION ("read"));
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

IDIO_DEFINE_PRIMITIVE2 ("setpgid", libc_setpgid, (IDIO ipid, IDIO ipgid))
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
	    idio_error_system_errno ("setpgid", IDIO_LIST2 (ipid, ipgid), IDIO_C_LOCATION ("setpgid"));
	}
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("signal", libc_signal, (IDIO isig, IDIO ifunc))
{
    IDIO_ASSERT (isig);
    IDIO_ASSERT (ifunc);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, ifunc);

    int sig = IDIO_C_TYPE_INT (isig);
    void (*func) (int) = IDIO_C_TYPE_POINTER_P (ifunc);

    void (*r) (int) = signal (sig, func);

    if (SIG_ERR == r) {
	idio_error_system_errno ("signal", IDIO_LIST2 (isig, ifunc), IDIO_C_LOCATION ("signal"));
    }

    return idio_C_pointer (r);
}

/*
 * signal-handler isn't a real libc function.  It has been added in
 * to aid spotting if a parent process has kindly sigignored()d
 * SIGPIPE for us:
 *
 * == (c/signal-handler SIGPIPE) SIG_IGN
 *
 * Hopefully we'll find other uses for it.
 */
IDIO_DEFINE_PRIMITIVE1 ("signal-handler", libc_signal_handler, (IDIO isig))
{
    IDIO_ASSERT (isig);
    IDIO_VERIFY_PARAM_TYPE (C_int, isig);

    int sig = IDIO_C_TYPE_INT (isig);

    struct sigaction osa;

    if (sigaction (sig, NULL, &osa) < 0) {
	idio_error_system_errno ("sigaction", idio_S_nil, IDIO_C_LOCATION ("signal-handler"));
    }

    /*
     * Our result be be either of:

     void     (*sa_handler)(int);
     void     (*sa_sigaction)(int, siginfo_t *, void *);

     * so, uh, prototype with no args!
     */
    void (*r) ();

    if (osa.sa_flags & SA_SIGINFO) {
	r = osa.sa_sigaction;
    } else {
	r = osa.sa_handler;
    }

    return idio_C_pointer (r);
}

IDIO_DEFINE_PRIMITIVE1 ("sleep", libc_sleep, (IDIO iseconds))
{
    IDIO_ASSERT (iseconds);

    unsigned int seconds = 0;
    if (idio_isa_fixnum (iseconds) &&
	IDIO_FIXNUM_VAL (iseconds) >= 0) {
	seconds = IDIO_FIXNUM_VAL (iseconds);
    } else if (idio_isa_C_uint (iseconds)) {
	seconds = IDIO_C_TYPE_UINT (iseconds);
    } else {
	idio_error_param_type ("unsigned fixnum|C_uint", iseconds, IDIO_C_LOCATION ("sleep"));
    }

    unsigned int r = sleep (seconds);

    return idio_C_uint (r);
}

IDIO idio_libc_stat (IDIO p)
{
    IDIO_ASSERT (p);
    IDIO_TYPE_ASSERT (string, p);

    char *p_C = idio_string_as_C (p);

    struct stat sb;

    if (stat (p_C, &sb) == -1) {
	idio_error_system_errno ("stat", IDIO_LIST1 (p), IDIO_C_LOCATION ("idio_libc_stat"));
    }

    /*
     * XXX idio_C_uint for everything?  We should know more.
     */
    IDIO r = idio_struct_instance (idio_libc_struct_stat,
				   idio_pair (idio_C_uint (sb.st_dev),
				   idio_pair (idio_C_uint (sb.st_ino),
				   idio_pair (idio_C_uint (sb.st_mode),
				   idio_pair (idio_C_uint (sb.st_nlink),
				   idio_pair (idio_C_uint (sb.st_uid),
				   idio_pair (idio_C_uint (sb.st_gid),
				   idio_pair (idio_C_uint (sb.st_rdev),
				   idio_pair (idio_C_uint (sb.st_size),
				   idio_pair (idio_C_uint (sb.st_blksize),
				   idio_pair (idio_C_uint (sb.st_blocks),
				   idio_pair (idio_C_uint (sb.st_atime),
				   idio_pair (idio_C_uint (sb.st_mtime),
				   idio_pair (idio_C_uint (sb.st_ctime),
				   idio_S_nil))))))))))))));
    free (p_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("stat", libc_stat, (IDIO p))
{
    IDIO_ASSERT (p);
    IDIO_VERIFY_PARAM_TYPE (string, p);

    return idio_libc_stat (p);
}

IDIO_DEFINE_PRIMITIVE1 ("strerror", libc_strerror, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);

    int errnum = 0;
    if (idio_isa_fixnum (ierrnum)) {
	errnum = IDIO_FIXNUM_VAL (ierrnum);
    } else if (idio_isa_C_int (ierrnum)) {
	errnum = IDIO_C_TYPE_INT (ierrnum);
    } else {
	idio_error_param_type ("unsigned fixnum|C_int", ierrnum, IDIO_C_LOCATION ("strerror"));
    }

    char *r = strerror (errnum);

    return idio_string_C (r);
}

IDIO_DEFINE_PRIMITIVE1 ("strsignal", libc_strsignal, (IDIO isignum))
{
    IDIO_ASSERT (isignum);

    int signum = 0;
    if (idio_isa_fixnum (isignum)) {
	signum = IDIO_FIXNUM_VAL (isignum);
    } else if (idio_isa_C_int (isignum)) {
	signum = IDIO_C_TYPE_INT (isignum);
    } else {
	idio_error_param_type ("unsigned fixnum|C_int", isignum, IDIO_C_LOCATION ("strsignal"));
    }

    char *r = strsignal (signum);

    return idio_string_C (r);
}

IDIO_DEFINE_PRIMITIVE1 ("tcgetattr", libc_tcgetattr, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    struct termios *tcattrs = idio_alloc (sizeof (struct termios));
    int r = tcgetattr (fd, tcattrs);

    if (-1 == r) {
	idio_error_system_errno ("tcgetattr", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("tcgetattr"));
    }

    return idio_C_pointer_free_me (tcattrs);
}

IDIO_DEFINE_PRIMITIVE1 ("tcgetpgrp", libc_tcgetpgrp, (IDIO ifd))
{
    IDIO_ASSERT (ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    pid_t pid = tcgetpgrp (fd);

    if (-1 == pid) {
	idio_error_system_errno ("tcgetpgrp", IDIO_LIST1 (ifd), IDIO_C_LOCATION ("tcgetpgrp"));
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE3 ("tcsetattr", libc_tcsetattr, (IDIO ifd, IDIO ioptions, IDIO itcattrs))
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
	idio_error_system_errno ("tcsetattr", IDIO_LIST3 (ifd, ioptions, itcattrs), IDIO_C_LOCATION ("tcsetattr"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("tcsetpgrp", libc_tcsetpgrp, (IDIO ifd, IDIO ipgrp))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (ipgrp);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (C_int, ipgrp);

    int fd = IDIO_C_TYPE_INT (ifd);
    pid_t pgrp = IDIO_C_TYPE_INT (ipgrp);


    int r = tcsetpgrp (fd, pgrp);

    if (-1 == r) {
	idio_error_system_errno ("tcsetpgrp", IDIO_LIST2 (ifd, ipgrp), IDIO_C_LOCATION ("tcsetpgrp"));
    }

    return idio_C_int (r);
}

IDIO idio_libc_uname ()
{
    struct utsname u;

    if (uname (&u) == -1) {
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_LOCATION ("idio_libc_uname"));
    }

    return idio_struct_instance (idio_libc_struct_utsname, IDIO_LIST5 (idio_string_C (u.sysname),
								       idio_string_C (u.nodename),
								       idio_string_C (u.release),
								       idio_string_C (u.version),
								       idio_string_C (u.machine)));
}

IDIO_DEFINE_PRIMITIVE0 ("uname", libc_uname, ())
{
    struct utsname *up;
    up = idio_alloc (sizeof (struct utsname));

    if (uname (up) == -1) {
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_LOCATION ("uname"));
    }

    return idio_C_pointer_free_me (up);
}

IDIO_DEFINE_PRIMITIVE1 ("unlink", libc_unlink, (IDIO ipath))
{
    IDIO_ASSERT (ipath);
    IDIO_VERIFY_PARAM_TYPE (string, ipath);

    char *path = idio_string_as_C (ipath);

    int r = unlink (path);

    free (path);

    if (-1 == r) {
	idio_error_system_errno ("unlink", IDIO_LIST1 (ipath), IDIO_C_LOCATION ("unlink"));
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2 ("waitpid", libc_waitpid, (IDIO ipid, IDIO ioptions))
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
	idio_error_system_errno ("waitpid", IDIO_LIST2 (ipid, ioptions), IDIO_C_LOCATION ("waitpid"));
    }

    IDIO istatus = idio_C_pointer_free_me (statusp);

    return IDIO_LIST2 (idio_C_int (r), istatus);
}

IDIO_DEFINE_PRIMITIVE1 ("WEXITSTATUS", libc_WEXITSTATUS, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WEXITSTATUS (*statusp));
}

IDIO_DEFINE_PRIMITIVE1 ("WIFEXITED", libc_WIFEXITED, (IDIO istatus))
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

IDIO_DEFINE_PRIMITIVE1 ("WIFSIGNALED", libc_WIFSIGNALED, (IDIO istatus))
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

IDIO_DEFINE_PRIMITIVE1 ("WIFSTOPPED", libc_WIFSTOPPED, (IDIO istatus))
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

IDIO_DEFINE_PRIMITIVE1 ("WTERMSIG", libc_WTERMSIG, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WTERMSIG (*statusp));
}

IDIO_DEFINE_PRIMITIVE2 ("write", libc_write, (IDIO ifd, IDIO istr))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (istr);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (string, istr);

    int fd = IDIO_C_TYPE_INT (ifd);

    size_t blen = idio_string_blen (istr);

    /*
     * A rare occasion we can use idio_string_s() as we are also using
     * blen.
     */
    ssize_t n = write (fd, idio_string_s (istr), blen);

    if (-1 == n) {
	idio_error_system_errno ("write", IDIO_LIST2 (ifd, istr), IDIO_C_LOCATION ("write"));
    }

    return idio_integer (n);
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
    IDIO sig_cond;

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
    IDIO_LIBC_SIGNAL (SIGKILL)
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
    IDIO_LIBC_SIGNAL (SIGSTOP)
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
		fprintf (stderr, "Unmapped signal numbers:");
	    }
	    fprintf (stderr, " %d (%s) -> %s;", i, strsignal (i), sig_name);
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

IDIO_DEFINE_PRIMITIVE1 ("sig-name", libc_sig_name, (IDIO isignum))
{
    IDIO_ASSERT (isignum);
    IDIO_VERIFY_PARAM_TYPE (C_int, isignum);

    return idio_string_C (idio_libc_sig_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0 ("sig-names", libc_sig_names, ())
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_sig_name (i))), r);
    }

    return idio_list_reverse (r);
}

IDIO_DEFINE_PRIMITIVE1 ("signal-name", libc_signal_name, (IDIO isignum))
{
    IDIO_ASSERT (isignum);
    IDIO_VERIFY_PARAM_TYPE (C_int, isignum);

    return idio_string_C (idio_libc_signal_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0 ("signal-names", libc_signal_names, ())
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

    IDIO err_sym;

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (E2BIG)
    err_sym = idio_symbols_C_intern ("E2BIG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (E2BIG));
    sprintf (idio_libc_errno_names[E2BIG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EACCES)
    err_sym = idio_symbols_C_intern ("EACCES");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EACCES));
    sprintf (idio_libc_errno_names[EACCES], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRINUSE)
    err_sym = idio_symbols_C_intern ("EADDRINUSE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EADDRINUSE));
    sprintf (idio_libc_errno_names[EADDRINUSE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRNOTAVAIL)
    err_sym = idio_symbols_C_intern ("EADDRNOTAVAIL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EADDRNOTAVAIL));
    sprintf (idio_libc_errno_names[EADDRNOTAVAIL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EADV)
    err_sym = idio_symbols_C_intern ("EADV");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EADV));
    sprintf (idio_libc_errno_names[EADV], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAFNOSUPPORT)
    err_sym = idio_symbols_C_intern ("EAFNOSUPPORT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EAFNOSUPPORT));
    sprintf (idio_libc_errno_names[EAFNOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAGAIN)
    err_sym = idio_symbols_C_intern ("EAGAIN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EAGAIN));
    sprintf (idio_libc_errno_names[EAGAIN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EALREADY)
    err_sym = idio_symbols_C_intern ("EALREADY");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EALREADY));
    sprintf (idio_libc_errno_names[EALREADY], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (EAUTH)
    err_sym = idio_symbols_C_intern ("EAUTH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EAUTH));
    sprintf (idio_libc_errno_names[EAUTH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (EBADARCH)
    err_sym = idio_symbols_C_intern ("EBADARCH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADARCH));
    sprintf (idio_libc_errno_names[EBADARCH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EBADE)
    err_sym = idio_symbols_C_intern ("EBADE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADE));
    sprintf (idio_libc_errno_names[EBADE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (EBADEXEC)
    err_sym = idio_symbols_C_intern ("EBADEXEC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADEXEC));
    sprintf (idio_libc_errno_names[EBADEXEC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADF)
    err_sym = idio_symbols_C_intern ("EBADF");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADF));
    sprintf (idio_libc_errno_names[EBADF], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EBADFD)
    err_sym = idio_symbols_C_intern ("EBADFD");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADFD));
    sprintf (idio_libc_errno_names[EBADFD], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (EBADMACHO)
    err_sym = idio_symbols_C_intern ("EBADMACHO");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADMACHO));
    sprintf (idio_libc_errno_names[EBADMACHO], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADMSG)
    err_sym = idio_symbols_C_intern ("EBADMSG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADMSG));
    sprintf (idio_libc_errno_names[EBADMSG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EBADR)
    err_sym = idio_symbols_C_intern ("EBADR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADR));
    sprintf (idio_libc_errno_names[EBADR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (EBADRPC)
    err_sym = idio_symbols_C_intern ("EBADRPC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADRPC));
    sprintf (idio_libc_errno_names[EBADRPC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EBADRQC)
    err_sym = idio_symbols_C_intern ("EBADRQC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADRQC));
    sprintf (idio_libc_errno_names[EBADRQC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EBADSLT)
    err_sym = idio_symbols_C_intern ("EBADSLT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBADSLT));
    sprintf (idio_libc_errno_names[EBADSLT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EBFONT)
    err_sym = idio_symbols_C_intern ("EBFONT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBFONT));
    sprintf (idio_libc_errno_names[EBFONT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBUSY)
    err_sym = idio_symbols_C_intern ("EBUSY");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EBUSY));
    sprintf (idio_libc_errno_names[EBUSY], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECANCELED)
    err_sym = idio_symbols_C_intern ("ECANCELED");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECANCELED));
    sprintf (idio_libc_errno_names[ECANCELED], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD */
#if defined (ECAPMODE)
    err_sym = idio_symbols_C_intern ("ECAPMODE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECAPMODE));
    sprintf (idio_libc_errno_names[ECAPMODE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECHILD)
    err_sym = idio_symbols_C_intern ("ECHILD");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECHILD));
    sprintf (idio_libc_errno_names[ECHILD], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ECHRNG)
    err_sym = idio_symbols_C_intern ("ECHRNG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECHRNG));
    sprintf (idio_libc_errno_names[ECHRNG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ECOMM)
    err_sym = idio_symbols_C_intern ("ECOMM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECOMM));
    sprintf (idio_libc_errno_names[ECOMM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNABORTED)
    err_sym = idio_symbols_C_intern ("ECONNABORTED");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECONNABORTED));
    sprintf (idio_libc_errno_names[ECONNABORTED], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNREFUSED)
    err_sym = idio_symbols_C_intern ("ECONNREFUSED");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECONNREFUSED));
    sprintf (idio_libc_errno_names[ECONNREFUSED], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNRESET)
    err_sym = idio_symbols_C_intern ("ECONNRESET");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ECONNRESET));
    sprintf (idio_libc_errno_names[ECONNRESET], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDEADLK)
    err_sym = idio_symbols_C_intern ("EDEADLK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDEADLK));
    sprintf (idio_libc_errno_names[EDEADLK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EDEADLOCK)
    err_sym = idio_symbols_C_intern ("EDEADLOCK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDEADLOCK));
    sprintf (idio_libc_errno_names[EDEADLOCK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDESTADDRREQ)
    err_sym = idio_symbols_C_intern ("EDESTADDRREQ");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDESTADDRREQ));
    sprintf (idio_libc_errno_names[EDESTADDRREQ], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (EDEVERR)
    err_sym = idio_symbols_C_intern ("EDEVERR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDEVERR));
    sprintf (idio_libc_errno_names[EDEVERR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDOM)
    err_sym = idio_symbols_C_intern ("EDOM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDOM));
    sprintf (idio_libc_errno_names[EDOM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD */
#if defined (EDOOFUS)
    err_sym = idio_symbols_C_intern ("EDOOFUS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDOOFUS));
    sprintf (idio_libc_errno_names[EDOOFUS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EDOTDOT)
    err_sym = idio_symbols_C_intern ("EDOTDOT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDOTDOT));
    sprintf (idio_libc_errno_names[EDOTDOT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDQUOT)
    err_sym = idio_symbols_C_intern ("EDQUOT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EDQUOT));
    sprintf (idio_libc_errno_names[EDQUOT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EEXIST)
    err_sym = idio_symbols_C_intern ("EEXIST");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EEXIST));
    sprintf (idio_libc_errno_names[EEXIST], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFAULT)
    err_sym = idio_symbols_C_intern ("EFAULT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EFAULT));
    sprintf (idio_libc_errno_names[EFAULT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFBIG)
    err_sym = idio_symbols_C_intern ("EFBIG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EFBIG));
    sprintf (idio_libc_errno_names[EFBIG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (EFTYPE)
    err_sym = idio_symbols_C_intern ("EFTYPE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EFTYPE));
    sprintf (idio_libc_errno_names[EFTYPE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTDOWN)
    err_sym = idio_symbols_C_intern ("EHOSTDOWN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EHOSTDOWN));
    sprintf (idio_libc_errno_names[EHOSTDOWN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTUNREACH)
    err_sym = idio_symbols_C_intern ("EHOSTUNREACH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EHOSTUNREACH));
    sprintf (idio_libc_errno_names[EHOSTUNREACH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EHWPOISON)
    err_sym = idio_symbols_C_intern ("EHWPOISON");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EHWPOISON));
    sprintf (idio_libc_errno_names[EHWPOISON], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIDRM)
    err_sym = idio_symbols_C_intern ("EIDRM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EIDRM));
    sprintf (idio_libc_errno_names[EIDRM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EILSEQ)
    err_sym = idio_symbols_C_intern ("EILSEQ");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EILSEQ));
    sprintf (idio_libc_errno_names[EILSEQ], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINPROGRESS)
    err_sym = idio_symbols_C_intern ("EINPROGRESS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EINPROGRESS));
    sprintf (idio_libc_errno_names[EINPROGRESS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINTR)
    err_sym = idio_symbols_C_intern ("EINTR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EINTR));
    sprintf (idio_libc_errno_names[EINTR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINVAL)
    err_sym = idio_symbols_C_intern ("EINVAL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EINVAL));
    sprintf (idio_libc_errno_names[EINVAL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIO)
    err_sym = idio_symbols_C_intern ("EIO");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EIO));
    sprintf (idio_libc_errno_names[EIO], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISCONN)
    err_sym = idio_symbols_C_intern ("EISCONN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EISCONN));
    sprintf (idio_libc_errno_names[EISCONN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISDIR)
    err_sym = idio_symbols_C_intern ("EISDIR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EISDIR));
    sprintf (idio_libc_errno_names[EISDIR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EISNAM)
    err_sym = idio_symbols_C_intern ("EISNAM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EISNAM));
    sprintf (idio_libc_errno_names[EISNAM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EKEYEXPIRED)
    err_sym = idio_symbols_C_intern ("EKEYEXPIRED");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EKEYEXPIRED));
    sprintf (idio_libc_errno_names[EKEYEXPIRED], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EKEYREJECTED)
    err_sym = idio_symbols_C_intern ("EKEYREJECTED");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EKEYREJECTED));
    sprintf (idio_libc_errno_names[EKEYREJECTED], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EKEYREVOKED)
    err_sym = idio_symbols_C_intern ("EKEYREVOKED");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EKEYREVOKED));
    sprintf (idio_libc_errno_names[EKEYREVOKED], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EL2HLT)
    err_sym = idio_symbols_C_intern ("EL2HLT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EL2HLT));
    sprintf (idio_libc_errno_names[EL2HLT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EL2NSYNC)
    err_sym = idio_symbols_C_intern ("EL2NSYNC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EL2NSYNC));
    sprintf (idio_libc_errno_names[EL2NSYNC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EL3HLT)
    err_sym = idio_symbols_C_intern ("EL3HLT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EL3HLT));
    sprintf (idio_libc_errno_names[EL3HLT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EL3RST)
    err_sym = idio_symbols_C_intern ("EL3RST");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EL3RST));
    sprintf (idio_libc_errno_names[EL3RST], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ELIBACC)
    err_sym = idio_symbols_C_intern ("ELIBACC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELIBACC));
    sprintf (idio_libc_errno_names[ELIBACC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ELIBBAD)
    err_sym = idio_symbols_C_intern ("ELIBBAD");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELIBBAD));
    sprintf (idio_libc_errno_names[ELIBBAD], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ELIBEXEC)
    err_sym = idio_symbols_C_intern ("ELIBEXEC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELIBEXEC));
    sprintf (idio_libc_errno_names[ELIBEXEC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ELIBMAX)
    err_sym = idio_symbols_C_intern ("ELIBMAX");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELIBMAX));
    sprintf (idio_libc_errno_names[ELIBMAX], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ELIBSCN)
    err_sym = idio_symbols_C_intern ("ELIBSCN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELIBSCN));
    sprintf (idio_libc_errno_names[ELIBSCN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ELNRNG)
    err_sym = idio_symbols_C_intern ("ELNRNG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELNRNG));
    sprintf (idio_libc_errno_names[ELNRNG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Solaris */
#if defined (ELOCKUNMAPPED)
    err_sym = idio_symbols_C_intern ("ELOCKUNMAPPED");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELOCKUNMAPPED));
    sprintf (idio_libc_errno_names[ELOCKUNMAPPED], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ELOOP)
    err_sym = idio_symbols_C_intern ("ELOOP");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ELOOP));
    sprintf (idio_libc_errno_names[ELOOP], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EMEDIUMTYPE)
    err_sym = idio_symbols_C_intern ("EMEDIUMTYPE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EMEDIUMTYPE));
    sprintf (idio_libc_errno_names[EMEDIUMTYPE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMFILE)
    err_sym = idio_symbols_C_intern ("EMFILE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EMFILE));
    sprintf (idio_libc_errno_names[EMFILE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMLINK)
    err_sym = idio_symbols_C_intern ("EMLINK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EMLINK));
    sprintf (idio_libc_errno_names[EMLINK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMSGSIZE)
    err_sym = idio_symbols_C_intern ("EMSGSIZE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EMSGSIZE));
    sprintf (idio_libc_errno_names[EMSGSIZE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMULTIHOP)
    err_sym = idio_symbols_C_intern ("EMULTIHOP");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EMULTIHOP));
    sprintf (idio_libc_errno_names[EMULTIHOP], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENAMETOOLONG)
    err_sym = idio_symbols_C_intern ("ENAMETOOLONG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENAMETOOLONG));
    sprintf (idio_libc_errno_names[ENAMETOOLONG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (ENAVAIL)
    err_sym = idio_symbols_C_intern ("ENAVAIL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENAVAIL));
    sprintf (idio_libc_errno_names[ENAVAIL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (ENEEDAUTH)
    err_sym = idio_symbols_C_intern ("ENEEDAUTH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENEEDAUTH));
    sprintf (idio_libc_errno_names[ENEEDAUTH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETDOWN)
    err_sym = idio_symbols_C_intern ("ENETDOWN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENETDOWN));
    sprintf (idio_libc_errno_names[ENETDOWN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETRESET)
    err_sym = idio_symbols_C_intern ("ENETRESET");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENETRESET));
    sprintf (idio_libc_errno_names[ENETRESET], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETUNREACH)
    err_sym = idio_symbols_C_intern ("ENETUNREACH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENETUNREACH));
    sprintf (idio_libc_errno_names[ENETUNREACH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENFILE)
    err_sym = idio_symbols_C_intern ("ENFILE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENFILE));
    sprintf (idio_libc_errno_names[ENFILE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ENOANO)
    err_sym = idio_symbols_C_intern ("ENOANO");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOANO));
    sprintf (idio_libc_errno_names[ENOANO], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (ENOATTR)
    err_sym = idio_symbols_C_intern ("ENOATTR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOATTR));
    sprintf (idio_libc_errno_names[ENOATTR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOBUFS)
    err_sym = idio_symbols_C_intern ("ENOBUFS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOBUFS));
    sprintf (idio_libc_errno_names[ENOBUFS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ENOCSI)
    err_sym = idio_symbols_C_intern ("ENOCSI");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOCSI));
    sprintf (idio_libc_errno_names[ENOCSI], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, OSX, Solaris */
#if defined (ENODATA)
    err_sym = idio_symbols_C_intern ("ENODATA");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENODATA));
    sprintf (idio_libc_errno_names[ENODATA], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENODEV)
    err_sym = idio_symbols_C_intern ("ENODEV");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENODEV));
    sprintf (idio_libc_errno_names[ENODEV], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOENT)
    err_sym = idio_symbols_C_intern ("ENOENT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOENT));
    sprintf (idio_libc_errno_names[ENOENT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOEXEC)
    err_sym = idio_symbols_C_intern ("ENOEXEC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOEXEC));
    sprintf (idio_libc_errno_names[ENOEXEC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (ENOKEY)
    err_sym = idio_symbols_C_intern ("ENOKEY");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOKEY));
    sprintf (idio_libc_errno_names[ENOKEY], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLCK)
    err_sym = idio_symbols_C_intern ("ENOLCK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOLCK));
    sprintf (idio_libc_errno_names[ENOLCK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLINK)
    err_sym = idio_symbols_C_intern ("ENOLINK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOLINK));
    sprintf (idio_libc_errno_names[ENOLINK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (ENOMEDIUM)
    err_sym = idio_symbols_C_intern ("ENOMEDIUM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOMEDIUM));
    sprintf (idio_libc_errno_names[ENOMEDIUM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMEM)
    err_sym = idio_symbols_C_intern ("ENOMEM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOMEM));
    sprintf (idio_libc_errno_names[ENOMEM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMSG)
    err_sym = idio_symbols_C_intern ("ENOMSG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOMSG));
    sprintf (idio_libc_errno_names[ENOMSG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ENONET)
    err_sym = idio_symbols_C_intern ("ENONET");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENONET));
    sprintf (idio_libc_errno_names[ENONET], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ENOPKG)
    err_sym = idio_symbols_C_intern ("ENOPKG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOPKG));
    sprintf (idio_libc_errno_names[ENOPKG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (ENOPOLICY)
    err_sym = idio_symbols_C_intern ("ENOPOLICY");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOPOLICY));
    sprintf (idio_libc_errno_names[ENOPOLICY], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOPROTOOPT)
    err_sym = idio_symbols_C_intern ("ENOPROTOOPT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOPROTOOPT));
    sprintf (idio_libc_errno_names[ENOPROTOOPT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSPC)
    err_sym = idio_symbols_C_intern ("ENOSPC");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOSPC));
    sprintf (idio_libc_errno_names[ENOSPC], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSR)
    err_sym = idio_symbols_C_intern ("ENOSR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOSR));
    sprintf (idio_libc_errno_names[ENOSR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSTR)
    err_sym = idio_symbols_C_intern ("ENOSTR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOSTR));
    sprintf (idio_libc_errno_names[ENOSTR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSYS)
    err_sym = idio_symbols_C_intern ("ENOSYS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOSYS));
    sprintf (idio_libc_errno_names[ENOSYS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Solaris */
#if defined (ENOTACTIVE)
    err_sym = idio_symbols_C_intern ("ENOTACTIVE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTACTIVE));
    sprintf (idio_libc_errno_names[ENOTACTIVE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTBLK)
    err_sym = idio_symbols_C_intern ("ENOTBLK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTBLK));
    sprintf (idio_libc_errno_names[ENOTBLK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD */
#if defined (ENOTCAPABLE)
    err_sym = idio_symbols_C_intern ("ENOTCAPABLE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTCAPABLE));
    sprintf (idio_libc_errno_names[ENOTCAPABLE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTCONN)
    err_sym = idio_symbols_C_intern ("ENOTCONN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTCONN));
    sprintf (idio_libc_errno_names[ENOTCONN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTDIR)
    err_sym = idio_symbols_C_intern ("ENOTDIR");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTDIR));
    sprintf (idio_libc_errno_names[ENOTDIR], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTEMPTY)
    err_sym = idio_symbols_C_intern ("ENOTEMPTY");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTEMPTY));
    sprintf (idio_libc_errno_names[ENOTEMPTY], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (ENOTNAM)
    err_sym = idio_symbols_C_intern ("ENOTNAM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTNAM));
    sprintf (idio_libc_errno_names[ENOTNAM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTRECOVERABLE)
    err_sym = idio_symbols_C_intern ("ENOTRECOVERABLE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTRECOVERABLE));
    sprintf (idio_libc_errno_names[ENOTRECOVERABLE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTSOCK)
    err_sym = idio_symbols_C_intern ("ENOTSOCK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTSOCK));
    sprintf (idio_libc_errno_names[ENOTSOCK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX, Solaris */
#if defined (ENOTSUP)
    err_sym = idio_symbols_C_intern ("ENOTSUP");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTSUP));
    sprintf (idio_libc_errno_names[ENOTSUP], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTTY)
    err_sym = idio_symbols_C_intern ("ENOTTY");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTTY));
    sprintf (idio_libc_errno_names[ENOTTY], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ENOTUNIQ)
    err_sym = idio_symbols_C_intern ("ENOTUNIQ");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENOTUNIQ));
    sprintf (idio_libc_errno_names[ENOTUNIQ], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENXIO)
    err_sym = idio_symbols_C_intern ("ENXIO");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ENXIO));
    sprintf (idio_libc_errno_names[ENXIO], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, OSX, Solaris */
#if defined (EOPNOTSUPP)
    err_sym = idio_symbols_C_intern ("EOPNOTSUPP");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EOPNOTSUPP));
    sprintf (idio_libc_errno_names[EOPNOTSUPP], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOVERFLOW)
    err_sym = idio_symbols_C_intern ("EOVERFLOW");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EOVERFLOW));
    sprintf (idio_libc_errno_names[EOVERFLOW], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOWNERDEAD)
    err_sym = idio_symbols_C_intern ("EOWNERDEAD");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EOWNERDEAD));
    sprintf (idio_libc_errno_names[EOWNERDEAD], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPERM)
    err_sym = idio_symbols_C_intern ("EPERM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPERM));
    sprintf (idio_libc_errno_names[EPERM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPFNOSUPPORT)
    err_sym = idio_symbols_C_intern ("EPFNOSUPPORT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPFNOSUPPORT));
    sprintf (idio_libc_errno_names[EPFNOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPIPE)
    err_sym = idio_symbols_C_intern ("EPIPE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPIPE));
    sprintf (idio_libc_errno_names[EPIPE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (EPROCLIM)
    err_sym = idio_symbols_C_intern ("EPROCLIM");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPROCLIM));
    sprintf (idio_libc_errno_names[EPROCLIM], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (EPROCUNAVAIL)
    err_sym = idio_symbols_C_intern ("EPROCUNAVAIL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPROCUNAVAIL));
    sprintf (idio_libc_errno_names[EPROCUNAVAIL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (EPROGMISMATCH)
    err_sym = idio_symbols_C_intern ("EPROGMISMATCH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPROGMISMATCH));
    sprintf (idio_libc_errno_names[EPROGMISMATCH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (EPROGUNAVAIL)
    err_sym = idio_symbols_C_intern ("EPROGUNAVAIL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPROGUNAVAIL));
    sprintf (idio_libc_errno_names[EPROGUNAVAIL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTO)
    err_sym = idio_symbols_C_intern ("EPROTO");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPROTO));
    sprintf (idio_libc_errno_names[EPROTO], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTONOSUPPORT)
    err_sym = idio_symbols_C_intern ("EPROTONOSUPPORT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPROTONOSUPPORT));
    sprintf (idio_libc_errno_names[EPROTONOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTOTYPE)
    err_sym = idio_symbols_C_intern ("EPROTOTYPE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPROTOTYPE));
    sprintf (idio_libc_errno_names[EPROTOTYPE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (EPWROFF)
    err_sym = idio_symbols_C_intern ("EPWROFF");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EPWROFF));
    sprintf (idio_libc_errno_names[EPWROFF], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (EQFULL)
    err_sym = idio_symbols_C_intern ("EQFULL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EQFULL));
    sprintf (idio_libc_errno_names[EQFULL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ERANGE)
    err_sym = idio_symbols_C_intern ("ERANGE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ERANGE));
    sprintf (idio_libc_errno_names[ERANGE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EREMCHG)
    err_sym = idio_symbols_C_intern ("EREMCHG");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EREMCHG));
    sprintf (idio_libc_errno_names[EREMCHG], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EREMOTE)
    err_sym = idio_symbols_C_intern ("EREMOTE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EREMOTE));
    sprintf (idio_libc_errno_names[EREMOTE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EREMOTEIO)
    err_sym = idio_symbols_C_intern ("EREMOTEIO");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EREMOTEIO));
    sprintf (idio_libc_errno_names[EREMOTEIO], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ERESTART)
    err_sym = idio_symbols_C_intern ("ERESTART");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ERESTART));
    sprintf (idio_libc_errno_names[ERESTART], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (ERFKILL)
    err_sym = idio_symbols_C_intern ("ERFKILL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ERFKILL));
    sprintf (idio_libc_errno_names[ERFKILL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EROFS)
    err_sym = idio_symbols_C_intern ("EROFS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EROFS));
    sprintf (idio_libc_errno_names[EROFS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, OSX */
#if defined (ERPCMISMATCH)
    err_sym = idio_symbols_C_intern ("ERPCMISMATCH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ERPCMISMATCH));
    sprintf (idio_libc_errno_names[ERPCMISMATCH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* OSX */
#if defined (ESHLIBVERS)
    err_sym = idio_symbols_C_intern ("ESHLIBVERS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESHLIBVERS));
    sprintf (idio_libc_errno_names[ESHLIBVERS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESHUTDOWN)
    err_sym = idio_symbols_C_intern ("ESHUTDOWN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESHUTDOWN));
    sprintf (idio_libc_errno_names[ESHUTDOWN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESOCKTNOSUPPORT)
    err_sym = idio_symbols_C_intern ("ESOCKTNOSUPPORT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESOCKTNOSUPPORT));
    sprintf (idio_libc_errno_names[ESOCKTNOSUPPORT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESPIPE)
    err_sym = idio_symbols_C_intern ("ESPIPE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESPIPE));
    sprintf (idio_libc_errno_names[ESPIPE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESRCH)
    err_sym = idio_symbols_C_intern ("ESRCH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESRCH));
    sprintf (idio_libc_errno_names[ESRCH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ESRMNT)
    err_sym = idio_symbols_C_intern ("ESRMNT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESRMNT));
    sprintf (idio_libc_errno_names[ESRMNT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESTALE)
    err_sym = idio_symbols_C_intern ("ESTALE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESTALE));
    sprintf (idio_libc_errno_names[ESTALE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (ESTRPIPE)
    err_sym = idio_symbols_C_intern ("ESTRPIPE");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ESTRPIPE));
    sprintf (idio_libc_errno_names[ESTRPIPE], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, OSX, Solaris */
#if defined (ETIME)
    err_sym = idio_symbols_C_intern ("ETIME");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ETIME));
    sprintf (idio_libc_errno_names[ETIME], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETIMEDOUT)
    err_sym = idio_symbols_C_intern ("ETIMEDOUT");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ETIMEDOUT));
    sprintf (idio_libc_errno_names[ETIMEDOUT], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETOOMANYREFS)
    err_sym = idio_symbols_C_intern ("ETOOMANYREFS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ETOOMANYREFS));
    sprintf (idio_libc_errno_names[ETOOMANYREFS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETXTBSY)
    err_sym = idio_symbols_C_intern ("ETXTBSY");
    idio_libc_export_symbol_value (err_sym, idio_C_int (ETXTBSY));
    sprintf (idio_libc_errno_names[ETXTBSY], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux */
#if defined (EUCLEAN)
    err_sym = idio_symbols_C_intern ("EUCLEAN");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EUCLEAN));
    sprintf (idio_libc_errno_names[EUCLEAN], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EUNATCH)
    err_sym = idio_symbols_C_intern ("EUNATCH");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EUNATCH));
    sprintf (idio_libc_errno_names[EUNATCH], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EUSERS)
    err_sym = idio_symbols_C_intern ("EUSERS");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EUSERS));
    sprintf (idio_libc_errno_names[EUSERS], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EWOULDBLOCK)
    err_sym = idio_symbols_C_intern ("EWOULDBLOCK");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EWOULDBLOCK));
    sprintf (idio_libc_errno_names[EWOULDBLOCK], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EXDEV)
    err_sym = idio_symbols_C_intern ("EXDEV");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EXDEV));
    sprintf (idio_libc_errno_names[EXDEV], "%s", IDIO_SYMBOL_S (err_sym));
#endif

    /* Linux, Solaris */
#if defined (EXFULL)
    err_sym = idio_symbols_C_intern ("EXFULL");
    idio_libc_export_symbol_value (err_sym, idio_C_int (EXFULL));
    sprintf (idio_libc_errno_names[EXFULL], "%s", IDIO_SYMBOL_S (err_sym));
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FERRNO ; i < IDIO_LIBC_NERRNO ; i++) {
	if ('\0' == *(idio_libc_errno_names[i])) {
	    char err_name[IDIO_LIBC_ERRNAMELEN + 2];
	    sprintf (err_name, "ERRUNKNOWN%d", i);
	    err_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (err_sym, idio_C_int (i));
	    sprintf (idio_libc_errno_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped errno numbers:");
	    }
	    fprintf (stderr, " %d (%s) -> %s;", i, strerror (i), err_name);
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

IDIO_DEFINE_PRIMITIVE1 ("errno-name", libc_errno_name, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);
    IDIO_VERIFY_PARAM_TYPE (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0 ("errno-names", libc_errno_names, ())
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
IDIO_DEFINE_PRIMITIVE1 ("strerrno", libc_strerrno, (IDIO ierrnum))
{
    IDIO_ASSERT (ierrnum);
    IDIO_VERIFY_PARAM_TYPE (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0 ("errno/get", libc_errno_get, (void))
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

    IDIO rlimit_sym;

    /* Linux, Solaris */
#if defined (RLIMIT_CPU)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_CPU");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_CPU));
    sprintf (idio_libc_rlimit_names[RLIMIT_CPU], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_FSIZE)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_FSIZE");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_FSIZE));
    sprintf (idio_libc_rlimit_names[RLIMIT_FSIZE], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_DATA)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_DATA");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_DATA));
    sprintf (idio_libc_rlimit_names[RLIMIT_DATA], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_STACK)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_STACK");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_STACK));
    sprintf (idio_libc_rlimit_names[RLIMIT_STACK], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_CORE)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_CORE");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_CORE));
    sprintf (idio_libc_rlimit_names[RLIMIT_CORE], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_RSS)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_RSS");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_RSS));
    sprintf (idio_libc_rlimit_names[RLIMIT_RSS], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_NOFILE)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_NOFILE");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_NOFILE));
    sprintf (idio_libc_rlimit_names[RLIMIT_NOFILE], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Solaris */
#if defined (RLIMIT_VMEM)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_VMEM");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_VMEM));
    sprintf (idio_libc_rlimit_names[RLIMIT_VMEM], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux, Solaris */
#if defined (RLIMIT_AS)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_AS");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_AS));
    sprintf (idio_libc_rlimit_names[RLIMIT_AS], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_NPROC)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_NPROC");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_NPROC));
    sprintf (idio_libc_rlimit_names[RLIMIT_NPROC], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_MEMLOCK)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_MEMLOCK");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_MEMLOCK));
    sprintf (idio_libc_rlimit_names[RLIMIT_MEMLOCK], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_LOCKS)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_LOCKS");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_LOCKS));
    sprintf (idio_libc_rlimit_names[RLIMIT_LOCKS], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_SIGPENDING)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_SIGPENDING");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_SIGPENDING));
    sprintf (idio_libc_rlimit_names[RLIMIT_SIGPENDING], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_MSGQUEUE)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_MSGQUEUE");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_MSGQUEUE));
    sprintf (idio_libc_rlimit_names[RLIMIT_MSGQUEUE], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_NICE)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_NICE");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_NICE));
    sprintf (idio_libc_rlimit_names[RLIMIT_NICE], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_RTPRIO)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_RTPRIO");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_RTPRIO));
    sprintf (idio_libc_rlimit_names[RLIMIT_RTPRIO], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

    /* Linux */
#if defined (RLIMIT_RTTIME)
    rlimit_sym = idio_symbols_C_intern ("RLIMIT_RTTIME");
    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (RLIMIT_RTTIME));
    sprintf (idio_libc_rlimit_names[RLIMIT_RTTIME], "%s", IDIO_SYMBOL_S (rlimit_sym));
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FRLIMIT ; i < IDIO_LIBC_NRLIMIT ; i++) {
	if ('\0' == *(idio_libc_rlimit_names[i])) {
	    char err_name[IDIO_LIBC_RLIMITNAMELEN + 2];
	    sprintf (err_name, "RLIMIT_UNKNOWN%d", i);
	    rlimit_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (i));
	    sprintf (idio_libc_rlimit_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped rlimit numbers:");
	    }
	    fprintf (stderr, " %d (%s) -> %s;", i, strerror (i), err_name);
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
	idio_error_param_type ("int < FRLIMIT (or > NRLIMIT)", idio_C_int (rlim), IDIO_C_LOCATION ("idio_libc_rlimit_name"));
    }

    return idio_libc_rlimit_names[rlim];
}

IDIO_DEFINE_PRIMITIVE1_DS ("rlimit-name", libc_rlimit_name, (IDIO irlim), "irlim", "\
return the string name of the getrlimit(2)      \n\
C macro						\n\
						\n\
:param irlim: the C-int value of the macro	\n\
						\n\
:return: a string				\n\
						\n\
:raises: ^rt-parameter-type-error		\n\
")
{
    IDIO_ASSERT (irlim);
    IDIO_VERIFY_PARAM_TYPE (C_int, irlim);

    return idio_string_C (idio_libc_rlimit_name (IDIO_C_TYPE_INT (irlim)));
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

IDIO idio_libc_getrlimit (int resource)
{
    struct rlimit rlim;

    if (getrlimit (resource, &rlim) == -1) {
	idio_error_system_errno ("getrlimit", idio_S_nil, IDIO_C_LOCATION ("idio_libc_rlimit"));
    }

    return idio_struct_instance (idio_libc_struct_rlimit, IDIO_LIST2 (idio_C_int (rlim.rlim_cur),
								      idio_C_int (rlim.rlim_max)));
}

IDIO_DEFINE_PRIMITIVE1 ("getrlimit", libc_getrlimit, (IDIO iresource))
{
    IDIO_ASSERT (iresource);
    IDIO_VERIFY_PARAM_TYPE (C_int, iresource);

    return idio_libc_getrlimit (IDIO_C_TYPE_INT (iresource));
}

void idio_libc_setrlimit (int resource, struct rlimit *rlimp)
{
    if (setrlimit (resource, rlimp) == -1) {
	idio_error_system_errno ("setrlimit", idio_S_nil, IDIO_C_LOCATION ("idio_libc_rlimit"));
    }
}

IDIO_DEFINE_PRIMITIVE2 ("setrlimit", libc_setrlimit, (IDIO iresource, IDIO irlim))
{
    IDIO_ASSERT (iresource);
    IDIO_ASSERT (irlim);
    IDIO_VERIFY_PARAM_TYPE (C_int, iresource);
    IDIO_VERIFY_PARAM_TYPE (struct_instance, irlim);

    IDIO cur = idio_struct_instance_ref_direct (irlim, IDIO_STRUCT_RLIMIT_RLIM_CUR);
    IDIO max = idio_struct_instance_ref_direct (irlim, IDIO_STRUCT_RLIMIT_RLIM_MAX);

    struct rlimit rlim;
    rlim.rlim_cur = (rlim_t) idio_C_int_get (cur);
    rlim.rlim_max = (rlim_t) idio_C_int_get (max);

    idio_libc_setrlimit (IDIO_C_TYPE_INT (iresource), &rlim);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0 ("EGID/get", EGID_get, (void))
{
    return idio_integer (getegid ());
}

IDIO_DEFINE_PRIMITIVE1 ("EGID/set", EGID_set, (IDIO iegid))
{
    IDIO_ASSERT (iegid);

    gid_t egid = -1;

    if (idio_isa_fixnum (iegid)) {
	egid = IDIO_FIXNUM_VAL (iegid);
    } else if (idio_isa_bignum (iegid)) {
	egid = idio_bignum_intmax_value (iegid);
    } else if (idio_isa_C_int (iegid)) {
	egid = IDIO_C_TYPE_INT (iegid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", iegid, IDIO_C_LOCATION ("EGID/set"));
    }

    int r = setegid (egid);

    if (-1 == r) {
	idio_error_system_errno ("setegid", iegid, IDIO_C_LOCATION ("EGID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("EUID/get", EUID_get, (void))
{
    return idio_integer (geteuid ());
}

IDIO_DEFINE_PRIMITIVE1 ("EUID/set", EUID_set, (IDIO ieuid))
{
    IDIO_ASSERT (ieuid);

    uid_t euid = -1;

    if (idio_isa_fixnum (ieuid)) {
	euid = IDIO_FIXNUM_VAL (ieuid);
    } else if (idio_isa_bignum (ieuid)) {
	euid = idio_bignum_intmax_value (ieuid);
    } else if (idio_isa_C_int (ieuid)) {
	euid = IDIO_C_TYPE_INT (ieuid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", ieuid, IDIO_C_LOCATION ("EUID/set"));
    }

    int r = seteuid (euid);

    if (-1 == r) {
	idio_error_system_errno ("seteuid", ieuid, IDIO_C_LOCATION ("EUID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("GID/get", GID_get, (void))
{
    return idio_integer (getgid ());
}

IDIO_DEFINE_PRIMITIVE1 ("GID/set", GID_set, (IDIO igid))
{
    IDIO_ASSERT (igid);

    gid_t gid = -1;

    if (idio_isa_fixnum (igid)) {
	gid = IDIO_FIXNUM_VAL (igid);
    } else if (idio_isa_bignum (igid)) {
	gid = idio_bignum_intmax_value (igid);
    } else if (idio_isa_C_int (igid)) {
	gid = IDIO_C_TYPE_INT (igid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", igid, IDIO_C_LOCATION ("GID/set"));
    }

    int r = setgid (gid);

    if (-1 == r) {
	idio_error_system_errno ("setgid", igid, IDIO_C_LOCATION ("GID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("UID/get", UID_get, (void))
{
    return idio_integer (getuid ());
}

IDIO_DEFINE_PRIMITIVE1 ("UID/set", UID_set, (IDIO iuid))
{
    IDIO_ASSERT (iuid);

    uid_t uid = -1;

    if (idio_isa_fixnum (iuid)) {
	uid = IDIO_FIXNUM_VAL (iuid);
    } else if (idio_isa_bignum (iuid)) {
	uid = idio_bignum_intmax_value (iuid);
    } else if (idio_isa_C_int (iuid)) {
	uid = IDIO_C_TYPE_INT (iuid);
    } else {
	idio_error_param_type ("fixnum|bignum|C_int", iuid, IDIO_C_LOCATION ("UID/set"));
    }

    int r = setuid (uid);

    if (-1 == r) {
	idio_error_system_errno ("setuid", iuid, IDIO_C_LOCATION ("UID/set"));
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0 ("STDIN/get", libc_STDIN_get, (void))
{
    return idio_thread_current_input_handle ();
}

IDIO_DEFINE_PRIMITIVE0 ("STDOUT/get", libc_STDOUT_get, (void))
{
    return idio_thread_current_output_handle ();
}

IDIO_DEFINE_PRIMITIVE0 ("STDERR/get", libc_STDERR_get, (void))
{
    return idio_thread_current_error_handle ();
}

void idio_init_libc_wrap ()
{
    idio_libc_wrap_module = idio_module (idio_symbols_C_intern ("libc"));

    idio_module_export_symbol_value (idio_symbols_C_intern ("0U"), idio_C_uint (0U), idio_libc_wrap_module);

    /* fcntl.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD"), idio_C_int (F_DUPFD), idio_libc_wrap_module);
#if defined (F_DUPFD_CLOEXEC)
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD_CLOEXEC"), idio_C_int (F_DUPFD_CLOEXEC), idio_libc_wrap_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("FD_CLOEXEC"), idio_C_int (FD_CLOEXEC), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_GETFD"), idio_C_int (F_GETFD), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_SETFD"), idio_C_int (F_SETFD), idio_libc_wrap_module);

    /* signal.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_DFL"), idio_C_pointer (SIG_DFL), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_IGN"), idio_C_pointer (SIG_IGN), idio_libc_wrap_module);

    /* stdio.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("NULL"), idio_C_pointer (NULL), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("EOF"), idio_C_int (EOF), idio_libc_wrap_module);

    /* stdint.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MAX"), idio_C_int (INTMAX_MAX), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MIN"), idio_C_int (INTMAX_MIN), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("UINTMAX_MAX"), idio_C_uint (UINTMAX_MAX), idio_libc_wrap_module);

    /* sys/resource.h */
    /*
     * NB RLIM_SAVED_* not defined in FreeBSD (10)
     */
#ifdef RLIM_SAVED_MAX
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_MAX"), idio_C_int (RLIM_SAVED_MAX), idio_libc_wrap_module);
#endif
#ifdef RLIM_SAVED_CUR
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_CUR"), idio_C_int (RLIM_SAVED_CUR), idio_libc_wrap_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_INFINITY"), idio_C_int (RLIM_INFINITY), idio_libc_wrap_module);

    /* sys/wait.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("WAIT_ANY"), idio_C_int (WAIT_ANY), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WNOHANG"), idio_C_int (WNOHANG), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WUNTRACED"), idio_C_int (WUNTRACED), idio_libc_wrap_module);

    /* termios.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSADRAIN"), idio_C_int (TCSADRAIN), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSAFLUSH"), idio_C_int (TCSAFLUSH), idio_libc_wrap_module);

    /* unistd.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("PATH_MAX"), idio_C_int (PATH_MAX), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDIN_FILENO"), idio_C_int (STDIN_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDOUT_FILENO"), idio_C_int (STDOUT_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDERR_FILENO"), idio_C_int (STDERR_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("R_OK"), idio_C_int (R_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("W_OK"), idio_C_int (W_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("X_OK"), idio_C_int (X_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_OK"), idio_C_int (F_OK), idio_libc_wrap_module);

    IDIO geti;
    IDIO seti;
    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("errno"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDIN_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDIN"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDOUT_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDOUT"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDERR_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDERR"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    IDIO name;
    name = idio_symbols_C_intern ("struct-sigaction");
    idio_libc_struct_sigaction = idio_struct_type (name,
						   idio_S_nil,
						   idio_pair (idio_symbols_C_intern ("sa_handler"),
						   idio_pair (idio_symbols_C_intern ("sa_sigaction"),
						   idio_pair (idio_symbols_C_intern ("sa_mask"),
						   idio_pair (idio_symbols_C_intern ("sa_flags"),
						   idio_S_nil)))));
    idio_module_export_symbol_value (name, idio_libc_struct_sigaction, idio_libc_wrap_module);

    name = idio_symbols_C_intern ("struct-utsname");
    idio_libc_struct_utsname = idio_struct_type (name,
						 idio_S_nil,
						 idio_pair (idio_symbols_C_intern ("sysname"),
						 idio_pair (idio_symbols_C_intern ("nodename"),
						 idio_pair (idio_symbols_C_intern ("release"),
						 idio_pair (idio_symbols_C_intern ("version"),
						 idio_pair (idio_symbols_C_intern ("machine"),
						 idio_S_nil))))));
    idio_module_export_symbol_value (name, idio_libc_struct_utsname, idio_libc_wrap_module);

    {
	char *field_names[] = { "sb_dev", "sb_ino", "sb_mode", "sb_nlink", "sb_uid", "sb_gid", "sb_rdev", "sb_size", "sb_blksize", "sb_blocks", "sb_atime", "sb_mtime", "sb_ctime", NULL };
	IDIO_DEFINE_MODULE_STRUCTn (idio_libc_wrap_module, idio_libc_struct_stat, "struct-stat", idio_S_nil);
    }

    name = idio_symbols_C_intern ("Idio/uname");
    idio_module_export_symbol_value (name, idio_libc_uname (), idio_libc_wrap_module);

    name = idio_symbols_C_intern ("struct-rlimit");
    idio_libc_struct_rlimit = idio_struct_type (name,
						idio_S_nil,
						idio_pair (idio_symbols_C_intern ("rlim_cur"),
						idio_pair (idio_symbols_C_intern ("rlim_max"),
						idio_S_nil)));
    idio_module_export_symbol_value (name, idio_libc_struct_rlimit, idio_libc_wrap_module);

    idio_vm_signal_handler_conditions = idio_array (IDIO_LIBC_NSIG + 1);
    idio_gc_protect (idio_vm_signal_handler_conditions);
    /*
     * idio_vm_run1() will be indexing anywhere into this array when
     * it gets a signal so make sure that the "used" size is up there
     * by poking something at at NSIG.
     */
    idio_array_insert_index (idio_vm_signal_handler_conditions, idio_S_nil, (idio_ai_t) IDIO_LIBC_NSIG);

    idio_libc_set_signal_names ();
    idio_libc_set_errno_names ();
    idio_libc_set_rlimit_names ();

    /*
     * Define some host/user/process variables
     */
    IDIO main_module = idio_Idio_module_instance ();

    if (getenv ("HOSTNAME") == NULL) {
	struct utsname u;
	if (uname (&u) == -1) {
	    idio_error_system_errno ("uname", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
	}
	idio_module_set_symbol_value (idio_symbols_C_intern ("HOSTNAME"), idio_string_C (u.nodename), main_module);
    }

    /*
     * From getpwuid(3) on CentOS
     */

    /*
    struct passwd pwd;
    struct passwd *pwd_result;
    char *pwd_buf;
    size_t pwd_bufsize;
    int pwd_s;

    pwd_bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
    if (pwd_bufsize == -1)
	pwd_bufsize = 16384;

    pwd_buf = idio_alloc (pwd_bufsize);

    int pwd_exists = 1;
    pwd_s = getpwuid_r (getuid (), &pwd, pwd_buf, pwd_bufsize, &pwd_result);
    if (pwd_result == NULL) {
	if (pwd_s) {
	    errno = pwd_s;
	    idio_error_system_errno ("getpwnam_r", idio_integer (getuid ()), IDIO_C_LOCATION ("idio_init_libc_wrap"));
	}
	pwd_exists = 0;
    }

    IDIO blank = idio_string_C ("");
    IDIO HOME = blank;
    IDIO SHELL = blank;
    if (pwd_exists) {
	HOME = idio_string_C (pwd.pw_dir);
	SHELL = idio_string_C (pwd.pw_shell);
    }

    if (getenv ("HOME") == NULL) {
	IDIO name = idio_symbols_C_intern ("HOME");
	idio_toplevel_extend (name, IDIO_MEANING_ENVIRON_SCOPE (0));
	idio_module_export_symbol_value (name, HOME, idio_libc_wrap_module);
    }

    if (getenv ("SHELL") == NULL) {
	IDIO name = idio_symbols_C_intern ("SHELL");
	idio_toplevel_extend (name, IDIO_MEANING_ENVIRON_SCOPE (0));
	idio_module_export_symbol_value (name, SHELL, idio_libc_wrap_module);
    }

    free (pwd_buf);
    */

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, UID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, UID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("UID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EUID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EUID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EUID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, GID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, GID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("GID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EGID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EGID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EGID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    int ngroups = getgroups (0, (gid_t *) NULL);

    if (-1 == ngroups) {
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
    }

    gid_t grp_list[ngroups];

    int ng = getgroups (ngroups, grp_list);
    if (-1 == ng) {
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
    }

    /*
     * Could this ever happen?
     */
    if (ngroups != ng) {
	idio_error_C ("getgroups", idio_S_nil, IDIO_C_LOCATION ("idio_init_libc_wrap"));
    }

    IDIO GROUPS = idio_array (ngroups);

    for (ng = 0; ng < ngroups ; ng++) {
	idio_array_insert_index (GROUPS, idio_integer (grp_list[ng]), ng);
    }
    idio_module_set_symbol_value (idio_symbols_C_intern ("GROUPS"), GROUPS, main_module);

    idio_module_set_symbol_value (idio_symbols_C_intern ("PID"), idio_integer (getpid ()), main_module);
    idio_module_set_symbol_value (idio_symbols_C_intern ("PPID"), idio_integer (getppid ()), main_module);

}

void idio_libc_wrap_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_system_error);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_access);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_close);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_dup);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_dup2);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_exit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fcntl);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fileno);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fork);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getcwd);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getpid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_isatty);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_kill);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_mkdtemp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_mkstemp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe_reader);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe_writer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_read);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_setpgid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_handler);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sleep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_stat);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strerror);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strsignal);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcgetattr);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcgetpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcsetattr);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcsetpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_uname);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_unlink);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_waitpid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WEXITSTATUS);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFEXITED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFSIGNALED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFSTOPPED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WTERMSIG);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_write);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sig_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sig_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strerrno);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_rlimit_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_rlimit_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getrlimit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_setrlimit);
}

void idio_final_libc_wrap ()
{
    int i;
    idio_gc_expose (idio_vm_signal_handler_conditions);
    for (i = IDIO_LIBC_FSIG; NULL != idio_libc_signal_names[i]; i++) {
        free (idio_libc_signal_names[i]);
    }
    free (idio_libc_signal_names);
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO; i++) {
        free (idio_libc_errno_names[i]);
    }
    free (idio_libc_errno_names);
    idio_gc_expose (idio_libc_struct_stat);
}

