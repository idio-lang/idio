*Job Control* calls ``waitpid`` with ``WAIT_ANY`` (-1) and ``WNOHANG``
which means we can get a variety of results.  The value returned is
always a list of two elements.

* If a child process has exited then ``waitpid`` returns
  :samp:`({pid}, {status})`.

  If :samp:`{pid}` is not identified as a process in the list of known
  jobs then the Job Control code enters it into a *stray pids* table
  along with its *status*.

* If no child process has exited then ``waitpid`` returns :samp:`(0,
  {status})`.

* If ``waitpid`` errors then

  * If the error is ``ECHILD`` then either the pid doesn't exist, is
    not a child process or the disposition of the ``SIGCHLD`` signal
    is ``SIG_IGN``.

    However, ``waitpid`` can look at the *stray pids* table and see if
    the queried `pid` exists and return that PID and its *status*.

    Otherwise we return ``(0 #n)``.

  * If the result is ``EINTR`` we can loop, re-invoking ``waitpid``.

  * Otherwise raise a ``^system-error``.


