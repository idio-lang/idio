Reader Operators
----------------

Reader :ref:`operators <operators>` operate on the Abstract Syntax
Tree.  Their purpose is to rewrite the AST to normalize expressions.

Having read an expression in, the reader will scan it for operators
and apply the operator to the "before" and "after" parts of the
expression.  The operators job is to return a re-written expression
that has been normalised.

For example, commonly, arithmetic uses infix operators: :samp:`1 + 2`.
However, :lname:`Idio` only accepts functions as the first element of
a form so the original expression needs to be rewritten.

For many infix operators you can imagine that it is easy enough to
rewrite the expression with the symbol for the operator now in first
place :samp:`+ 1 2` as the symbol `+` will evaluate to the general
addition function.

The general :ref:`+ <+>` function handles a number of varargs
possibilities whereas we know that the infix `+` operator is a binary
addition function, that is it always has two arguments.  So, in this
particular case, the `+` infix operator rewrites the expressions as
:samp:`binary-+ 1 2` where :ref:`binary-+ <binary-+>` is expecting
exactly two arguments.

Standard Operators
^^^^^^^^^^^^^^^^^^

There are a number of standard operators and modules can define their
own.  An obvious operator for the :ref:`job-control module` to define
is ``|``.

.. csv-table::
   :widths: auto
   :align: left

   ``+`` ``-`` ``*`` ``/``, infix binary arithmetic
   ``lt`` ``le`` ``eq`` ``ge`` ``gt``, infix binary numeric comparison
   ``and`` ``or``, infix multiple logical sequence :ref:`and <and special form>` and :ref:`or <or special form>`
   ``=+`` ``+=``, infix :ref:`array-push! <array-push!>` and :ref:`array-unshift! <array-unshift!>` operators
   ``=-`` ``-=``, postfix :ref:`array-pop! <array-pop!>` and :ref:`array-shift! <array-shift!>` operators
   ``.``, infix :ref:`value-index <value-index>`
