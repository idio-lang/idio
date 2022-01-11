* `argv` is the arguments to :ref:`spawn <expect/spawn>`

* `pid` is the Process ID of the spawned process

* `mfd` is master file descriptor of the pseudo-terminal

* `mode` is the :ref:`libc/struct-termios <libc/struct-termios>` of
  the initial state of the standard input device

* `buffer` is current unmatched input from the pseudo-terminal

  If new data was required to be read and after matching has been
  attempted but no match made, `buffer` may be truncated to the most
  recent :ref:`exp-match-max <expect/exp-match-max>` code points.

The following attributes are used internally and are not supported for
use by users:

* `matched` is a flag indicating whether a match occurred

* `eof` is a flag indicating whether End of File was indicated for the
  pseudo-terminal

* `timeout` is a flag indicating if interaction with the
  pseudo-terminal timed out

  The timeout used is :ref:`exp-timeout <expect/exp-timeout>` in
  seconds.

* `log-file` is the `file` argument passed to :ref:`exp-log-file
  <expect/exp-log-file>`

* `lfh` is the file handle derived from `log-file`

* `status` is the status as reported by :ref:`exp-wait
  <expect/exp-wait>` plus a decoding of that status, eg. ``(exit 0)``

