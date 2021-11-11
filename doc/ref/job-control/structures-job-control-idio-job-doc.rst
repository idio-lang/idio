* `pipeline` is a list of the original commands in the pipeline

* `procs` is a list of the :ref:`%idio-process
  <job-control/%idio-process>` structures, one per element of
  `pipeline`

* `pgid` is the Process Group ID of the pipeline

* `notify-stopped` is a flag to say whether the job being stopped has
  been reported

* `notify-completed` is a flag to say whether any failure on job
  completion has been reported

* `raise?` is a flag to say whether any failure on job completion
  should have an ``^rt-command-status-error`` condition raised

  Notably, this is disabled in logical expressions thus allowing
  external commands that fail to be used.

* `raised` is a flag to say whether any failure on job completion
  has had a condition raised

* `tcattrs` is a :ref:`struct-termios <libc/struct-termios>`

* `stdin` indicates the overall job *stdin*

* `stdout` indicates the overall job *stdout*

* `stderr` indicates the overall job *stderr*

* `report-timing` is a flag to indicate that a timing report should be
  generated on job completion

* `timing-start` is a list of

  #. :ref:`struct-timeval <libc/struct-timeval>`

  #. :ref:`struct-rusage <libc/struct-rusage>` for ``libc/RUSAGE_SELF``

  #. :ref:`struct-rusage <libc/struct-rusage>` for ``libc/RUSAGE_CHILDREN``

* `timing-end` will be a list of

  #. :ref:`struct-timeval <libc/struct-timeval>`

  #. :ref:`struct-rusage <libc/struct-rusage>` for ``libc/RUSAGE_SELF``

  #. :ref:`struct-rusage <libc/struct-rusage>` for ``libc/RUSAGE_CHILDREN``

* `async` is a flag indicating the job is asynchronous and normally
  set for *Process Substitution* jobs
