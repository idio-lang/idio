* `argv` is the arguments to :ref:`spawn <expect/spawn>`

* `pid` is the Process ID of the spawned process

* `mfd` is master file descriptor of the pseudo-terminal

* `mode` is the :ref:`libc/struct-termios <libc/struct-termios>` of
  the initial state of the standard input device

* `buffer` is current unmatched input from the pseudo-terminal

  If, after matching has been attempted, this may be truncated to
  :ref:`expect-match-max <expect/expect-match-max>` code points.

The following attributes are used internally and are not supported for
use by users:

* `matched` is a flag indicating whether a match occurred

* `eof` is a flag indicating whether End of File was indicated for the
  pseudo-terminal

* `timeout` is a flag indicating if interaction with the
  pseudo-terminal timed out

  The timeout used is :ref:`expect-timeout <expect/expect-timeout>` in
  seconds.

