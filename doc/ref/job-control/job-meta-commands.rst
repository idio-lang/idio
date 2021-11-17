.. _`job-control/job meta-commands`:

job meta-commands
^^^^^^^^^^^^^^^^^

There are a number of words that can precede a job which generally
affect the environment the job is run in.

Not all the meta-commands work together.

.. index::
   single: collect-output

* ``collect-output`` collects the output from a job

  :return: the *stdout* of the job
  :rtype: string

  This is the equivalent of :lname:`Bash`'s *Command Substitution*,
  ``$(...)``.

.. index::
   single: fg-job

* ``fg-job`` runs the job in the foreground

  :return: the boolean status of the job
  :rtype: boolean

  This is the default and the meta-command exists for completeness.

.. index::
   single: subshell

* ``subshell`` runs the job in the foreground

  :return: the boolean status of the job
  :rtype: boolean

  This is called by the ``{{...}}`` reader form.

.. index::
   single: bg-job

* ``bg-job`` runs the job in the background

  :return: ``#t``

  This is the equivalent of most shell's ``... &``.

.. index::
   single: pipe-into

* ``pipe-into`` directs the *stdin* of the job to the read-end of a
  :manpage:`pipe(2)` and an output pipe handle derived from the
  write-end of the pipe is returned

  :return: output pipe handle from job
  :rtype: handle

  Whatever the caller writes to this pipe handle will be delivered to
  the *stdin* of the job.

  This is similar to :lname:`Perl`'s ``open ("| ...")``.

.. index::
   single: named-pipe-into

* ``named-pipe-into`` directs the *stdin* of the job from a named pipe
  and that pipe name is returned.

  :return: named pipe name
  :rtype: pathname

  The caller is expected to open the returned pipe name for writing.

  On many systems the pipe's name will be :file:`/dev/fd/{n}` but on
  some systems it will be a FIFO in the file system.

  This is the equivalent of :lname:`Bash`'s *Process Substitution*
  form ``>(...)``.

  The job is an *asynchronous* command and if it does not exit with a
  zero status will raise an :ref:`^rt-async-command-status-error
  <^rt-async-command-status-error>`.

.. index::
   single: pipe-from

* ``pipe-from`` directs the *stdout* of the job to the write-end of a
  :manpage:`pipe(2)` and an input pipe handle derived from the
  read-end of the pipe is returned.

  :return: input pipe handle to job
  :rtype: handle

  Whatever the job writes to its *stdout* can be read by the caller
  from this pipe handle.

  This is similar to :lname:`Perl`'s ``open ("... |")``.

.. index::
   single: named-pipe-from

* ``named-pipe-from`` directs the *stdout* of the job to a named pipe
  and that pipe name is returned.

  :return: named pipe name
  :rtype: pathname

  The caller is expected to open the returned pipe name for reading.

  On many systems the pipe's name will be :file:`/dev/fd/{n}` but on
  some systems it will be a FIFO in the file system.

  This is the equivalent of :lname:`Bash`'s *Process Substitution*
  form ``<(...)``.

  The job is an *asynchronous* command and if it does not exit with a
  zero status will raise an :ref:`^rt-async-command-status-error
  <^rt-async-command-status-error>`.

.. index::
   single: time

* ``time`` flags that a report on the accumulated resources of the job
  should be produced when the job completes

  If the job was backgrounded the reported timings will be wholly
  inaccurate.

