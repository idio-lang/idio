Changing Directory
^^^^^^^^^^^^^^^^^^

:lname:`Idio` supports a very simplistic set of directory changing
functions revolving around a *directory stack*.

The shell-like ``cd`` (with no arguments) cannot be directly
implemented in :lname:`Idio` (because it's a programming language and
``cd`` returns the value of the function ``cd``) so you are required
to wrap it in parenthesis, which uses :envvar:`HOME`, or pass ``~``
which is a synonym for :envvar:`HOME`.

