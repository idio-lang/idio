Reader Operators
----------------

Reader Operators are :ref:`templates <templates>` that operate on the
Abstract Syntax Tree.  Their purpose is to rewrite the AST to
normalize expressions.

Having read an expression in, the reader will scan it for operators
and apply the operator to the "before" and "after" parts of the
expression.  The operators job is to return a re-written expression
that has been normalised.

For example, commonly, arithmetic uses infix operators: :samp:`1 + 2`.
However, :lname:`Idio` only accepts functions as the first element of
a form so the original expression needs to be rewritten.

For many infix operators you can imagine that it is easy enough to
rewrite the expression with the symbol for the operator now in first
place: samp:`+ 1 2` as `+` is the general addition function.

The general :ref:`+ <+>` function handles a number of varargs
possibilities whereas we know that the infix `+` operator is a binary
addition function, that is it always has two arguments.  So, in this
particular case, the `+` infix operator rewrites the expressions as
:samp:`binary-+ 1 2` where :ref:`binary-+ <binary-+>` is expecting
exactly two arguments.
