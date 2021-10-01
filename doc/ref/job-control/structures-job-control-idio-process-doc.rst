* `argv` is a list of the original arguments in the process

* `exec` is unused

* `pid` is the Process ID of the process

* `completed` is a flag indicating if the process has completed

* `stopped` is a flag indicating if the process has been stopped (with
  ``SIGSTOP``)

* `status` if set, is a C/pointer to the process status as returned by
  :ref:`waitpid <libc/waitpid>`

