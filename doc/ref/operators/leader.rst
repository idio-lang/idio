.. _`operators`:

****************
Reader Operators
****************

Reader operators allow the :ref:`reader <reader>` to re-arrange
expressions before passing them onto the :ref:`evaluator <evaluator>`.

The obvious use of operators is for *infix* expressions like regular
arithmetic, 1 + 2.  :lname:`Idio` expects the function to be the first
element of any form so without any re-arrangement it would try to
invoke the function 1.

You can define *postfix* operators as well although there are very few
defined by default.

Arithmetic is usually a binary operation in that it expects a number
either side of the operator.  Other infix operators like
:ref:`job-control <job-control module>`'s ``|`` operator can expect
any number of arguments either side of the operator and potentially
more than one instance of itself in the same expression.

Like :lname:`C` arithmetic precedence, operators are given a
precedence which allows them to gather elements of expressions into
groups before recursing the operator expansion code on those groups.

Consider:

    ``find . -name foo 2> "/dev/null" | xargs wc -l``

Here we would expect ``|`` to have a high precedence to construct two
sub-expressions:

* ``find . -name foo 2> "/dev/null"``

* ``xargs wc -l``

Recursing the operator expansion into the first sub-expression we
would find the I/O redirection operator, ``2>``, also from
``job-control``, which would distinguish ``find . -name foo`` from the
I/O redirection target ``"/dev/null"``

Operators are another spin on :ref:`templates <templates>` and like
them are regular functions which return an expression to be evaluated.

The actual expansion of the ``|`` operator is pages of code, see
:file:`lib/job-control.idio` for more details.

.. toctree::
   :maxdepth: 1

