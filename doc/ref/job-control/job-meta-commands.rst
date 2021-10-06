job meta-commands
-----------------

There are a number of words that can precede a job which generally
affect the environment the job is run in.

Not all the meta-commands work together.

* ``collect-output`` collects the output from a job

  :return: the *stdout* of the job
  :rtype: string

  This is the equivalent of :lname:`Bash`'s ``$(...)``.

* ``fg-job`` runs the job in the foreground

  :return: the boolean status of the job
  :rtype: boolean

  This is the default and the meta-command exists for completeness.

* ``bg-job`` runs the job in the background

  :return: ``#t``

  This is the equivalent of most shell's ``... &``.

* ``pipe-into`` directs the *stdin* of the job to the read-end of a
  :manpage:`pipe(2)` and an output pipe handle derived from the
  write-end of the pipe is returned

  :return: output pipe handle from job
  :rtype: handle

  Whatever the caller writes to this pipe handle will be delivered to
  the *stdin* of the job.

  This is similar to :lname:`Perl`'s ``open ("| ...")``.

* ``named-pipe-into`` directs the *stdin* of the job from a named pipe
  and that pipe name is returned.

  :return: named pipe name
  :rtype: pathname

  The caller is expected to open the returned pipe name for writing.

  On many systems the pipe's name will be :file:`/dev/fd/{n}` but on
  some systems it will be a FIFO in the file system.

  This is the equivalent of :lname:`Bash`'s ``>(...)``.

* ``pipe-from`` directs the *stdout* of the job to the write-end of a
  :manpage:`pipe(2)` and an input pipe handle derived from the
  read-end of the pipe is returned.

  :return: input pipe handle to job
  :rtype: handle

  Whatever the job writes to its *stdout* can be read by the caller
  from this pipe handle.

  This is similar to :lname:`Perl`'s ``open ("... |")``.

* ``named-pipe-from`` directs the *stdout* of the job to a named pipe
  and that pipe name is returned.

  :return: named pipe name
  :rtype: pathname

  The caller is expected to open the returned pipe name for reading.

  On many systems the pipe's name will be :file:`/dev/fd/{n}` but on
  some systems it will be a FIFO in the file system.

  This is the equivalent of :lname:`Bash`'s ``<(...)``.

* ``time`` flags that a report on the accumulated resources of the job
  should be produced when the job completes

  If possible, ``time`` will return the boolean status of the job.
  Other meta-commands may intervene.
