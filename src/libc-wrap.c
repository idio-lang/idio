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
    IDIO_VERIFY_PARAM_TYPE (C_int, istatus);

    int status = IDIO_C_TYPE_INT (istatus);

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
	idio_error_system_errno ("setpgid", IDIO_LIST2 (ipid, ipgid));
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
	idio_error_system_errno ("waitpid", IDIO_LIST2 (ipid, ioptions));
    }

    IDIO istatus = idio_C_pointer_free_me (statusp);

    return IDIO_LIST2 (idio_C_int (r), istatus);
}

IDIO_DEFINE_PRIMITIVE1 ("c/WIFSIGNALED", C_WIFSIGNALED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_int, istatus);

    int status = IDIO_C_TYPE_INT (istatus);

    IDIO r = idio_S_false;

    if (WIFSIGNALED (status)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("c/WIFSTOPPED", C_WIFSTOPPED, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_int, istatus);

    int status = IDIO_C_TYPE_INT (istatus);

    IDIO r = idio_S_false;

    if (WIFSTOPPED (status)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("c/WTERMSIG", C_WTERMSIG, (IDIO istatus))
{
    IDIO_ASSERT (istatus);
    IDIO_VERIFY_PARAM_TYPE (C_int, istatus);

    int status = IDIO_C_TYPE_INT (istatus);

    return idio_C_int (WTERMSIG (status));
}

void idio_init_libc_wrap ()
{
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
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WAIT_ANY"), idio_C_int (WAIT_ANY), idio_main_module ());
    idio_module_set_symbol_value (idio_symbols_C_intern ("c/WUNTRACED"), idio_C_int (WUNTRACED), idio_main_module ());
}

void idio_libc_wrap_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (C_close);
    IDIO_ADD_PRIMITIVE (C_dup2);
    IDIO_ADD_PRIMITIVE (C_exit);
    IDIO_ADD_PRIMITIVE (C_fork);
    IDIO_ADD_PRIMITIVE (C_getpgrp);
    IDIO_ADD_PRIMITIVE (C_getpid);
    IDIO_ADD_PRIMITIVE (C_isatty);
    IDIO_ADD_PRIMITIVE (C_kill);
    IDIO_ADD_PRIMITIVE (C_pipe);
    IDIO_ADD_PRIMITIVE (C_pipe_reader);
    IDIO_ADD_PRIMITIVE (C_pipe_writer);
    IDIO_ADD_PRIMITIVE (C_setpgid);
    IDIO_ADD_PRIMITIVE (C_signal);
    IDIO_ADD_PRIMITIVE (C_tcgetattr);
    IDIO_ADD_PRIMITIVE (C_tcgetpgrp);
    IDIO_ADD_PRIMITIVE (C_tcsetattr);
    IDIO_ADD_PRIMITIVE (C_tcsetpgrp);
    IDIO_ADD_PRIMITIVE (C_waitpid);
    IDIO_ADD_PRIMITIVE (C_WIFSIGNALED);
    IDIO_ADD_PRIMITIVE (C_WIFSTOPPED);
    IDIO_ADD_PRIMITIVE (C_WTERMSIG);
}

void idio_final_libc_wrap ()
{
}

