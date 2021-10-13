.. idio:module:: job-control

.. _`job-control module`:

******************
job-control Module
******************


Job Control in :lname:`Idio` is very closely allied to the description
of `Job Control
<https://www.gnu.org/software/libc/manual/html_node/Job-Control.html#Job-Control>`_
in the GNU libc manual.

Almost all of :lname:`Idio`'s Job Control is written in :lname:`Idio`
meaning it is "easy" to augment.  Some behaviour still uses the
equivalent functionality in the :lname:`C` code base.

Many activities occur in the execution of a job from arranging I/O,
pipelines, fork/exec and determining whether or not the command
actually succeeded.

All of the individual activities can raise conditions however the
primary condition raised by `job-control` is
:ref:`^rt-command-status-error <^rt-command-status-error>`.

In :lname:`Idio` we want to know that processes have failed and not
failed silently.

If, when a job completes, its reason for exiting is not ``exit (0)``
then such a condition is raised with the original Unix *status* value,
indicating either :samp:`exit {n}` or :samp:`killed {signal}`.

The default handler for ``^rt-command-status-error`` will cause
:program:`idio` to exit in the same way.

You can suppress this behaviour by setting
:ref:`suppress-exit-on-error! <suppress-exit-on-error!>` to any `true`
value.

In addition :ref:`suppress-pipefail! <suppress-pipefail!>` controls
whether any "left-hand" processes in a pipeline that do not ``exit
(0)`` cause a condition to be raised.

As a separate category, *Process Substitution* commands, also called
*asynchronous* commands, here, can fail.  We'd like to know about that
although, as an asynchronous command has no bearing on the control
flow, it is handled by raising an :ref:`^rt-async-command-status-error
<^rt-async-command-status-error>`.

The default handler for ``^rt-async-command-status-error`` will invoke
:ref:`condition-report <condition-report>`.

You can suppress this behaviour by setting
:ref:`suppress-async-command-report! <suppress-async-command-report!>`
to any `true` value.



.. toctree::
   :maxdepth: 1

