.. _`function Type`:

Function Type
=============

Functions are the cornerstone of any programming language allowing
users to abstract and parameterize parts of their code.

Functions in :lname:`Idio` are first class values and can and, indeed,
commonly are passed as arguments or the value returned by another
function.

There are two basic kinds of functions, *primitives* and *closures*,
and two function-like behaviours called *special forms* and
*templates*.

Primitives
----------

Primitives form the bedrock of the language.  They are written in
:lname:`C` and access to them is exposed by the VM such that they can
be invoked and their results returned like any user-defined function.

Writing functions in :lname:`C` to behave like :lname:`Idio`
processing :lname:`Idio` parameters can be tiresome so generally
primitives are written for actions that user-defined functions cannot
do like alter memory.  Hence all of the value-manipulating functions
are primitives as they have to poke around in the underlying
implementation to return, say, the size of an array -- which you might
reasonably guess is going to be a ``size_t`` in a :lname:`C`
``struct`` -- and return it as an :lname:`Idio` integer.

Otherwise someone might write a primitive:

* where speed is considered important

* as an interface to a library of support functions, as with
  :ref:`libc <libc module>`, for example.

Closures
--------

Closures are user-defined functions and allow the convenient
abstraction of sections of code.

Normally you would define a closure with the special form :ref:`define
<define>`:

.. code-block:: idio

   define (foo a b) {
     a + b
   }

which creates a function called ``foo`` which takes two parameters,
``a`` and ``b``, and performs some addition on them.

Alternatively (and secretly what ``define`` does under the hood) you
can create an anonymous *function value* with the special form
:ref:`function <function>`:

.. code-block:: idio

   function (a b) {
     a + b
   }

which seems to do much as ``foo`` above except that as soon as we have
created it we appear to be throwing it away.

In practice what ``define`` is doing is:

.. code-block:: idio

   define foo (function (a b) {
     a + b
   })

with ``foo`` now available to be used in some way.

Notice the extra set of parentheses around the elements of the
anonymous function.  Much like you might parenthesise a sub-expression
in arithmetic, say, ``5 * (1 + 2)``, the parentheses force the
evaluation of the anonymous function to be a *function value*, hence
the ``define`` statement is more like:

.. code-block:: idio

   define foo {function-value}

``foo``, or, rather, the function value that ``foo`` is referencing,
could be passed around as an argument or invoked:

.. code-block:: idio

   foo 2 3

should return 5.

Special Forms
-------------

Special forms exist only in the evaluator and cannot be extended.

They are also invoked differently.  Rather than "evaluate" each
argument and pass the evaluated values to the special form, the
arguments are passed verbatim: numbers, strings, lists, etc..

The special form can invoke its associated behavioural code.  By and
large that behavioural code is about processing those arguments such
that byte code can be generated and subsequently run.

For more details see :ref:`special forms <special forms>`.

Templates
---------

Templates allow users to "create code."  They are implemented much
like special forms in that no arguments are evaluated but are passed
verbatim.  The result of a template should be something that can be
immediately re-evaluated.

.. attention::

   Using templates is fraught with complications in that they are run
   by the evaluator, in other words, not at the time user code is
   running, and their result is recursively re-evaluated giving their
   operation a meta quality to them.  They are also evaluated in a
   different *environment* (memory space, if you like).

   As if that isn't enough, the entity the user sees when using a
   template is actually a function, called an *expander*, which hides
   the template functionality.
