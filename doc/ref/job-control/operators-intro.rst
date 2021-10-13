.. idio:currentmodule:: job-control

job-control Operators
---------------------

job-control defines a numbers of standard shell-like operators.

Pipeline
^^^^^^^^

``|`` is the quintessential shell operator.  It will identify any
other ``|`` symbols in this expression and (effectively) rewrite it
from, say, ``a ... | b ... | c ...`` to ``(| (a ...) (b ...)  (c
...))`` and then combine the arguments to ``|`` into a pipeline.

I/O Redirection
^^^^^^^^^^^^^^^

The set of :lname:`Idio` I/O redirection operators is more limited
than, say, :lname:`Bash`'s partly because :lname:`Idio` doesn't
require such arbitrary expressions.  Generally, file descriptors can
be managed like regular programming languages.

:lname:`Idio` is looking to cover the convenient redirection of the
standard input, output and error file descriptors when orchestrating
commands.

However, the :lname:`Idio` I/O redirection operators go a little
further and allow you to redirect to or from string and file
(descriptor) handles.

They are also handling this with respect to the general context of
*current* input output and error handles.

The broad thrust of :lname:`Idio` I/O redirection is :samp:`cmd args
{op} {src/dst}`.

In particular note that the I/O operator operates on the :samp:`cmd
args` to its left.  You cannot do I/O redirection "in advance" of the
command you are operating on.

Basic Redirection
"""""""""""""""""

That is the ``<`` ``>`` ``>>`` ``2>`` ``2>>`` operators.

Here, the `src/dst` of `op` can be:

* a file descriptor or string handle in which case it used directly

* a string in which case the named file is opened for input or output

* ``#n`` which is a synonym for the string "/dev/null"

Duplication Redirection
"""""""""""""""""""""""

That is the ``<&`` ``>&`` ``2>&`` operators.

Here, the `src/dst` of `op` can be:

* a file descriptor or string handle in which case it used directly

* a C/int or fixnum to be used as a reference to the corresponding
  file descriptor

  Only file descriptors 0, 1 and 2 are supported

