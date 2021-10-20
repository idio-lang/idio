.. _`function Type`:

Function Type
=============

Functions are the cornerstone of any programming language allowing
users to abstract and parameterize parts of their code.

Functions in :lname:`Idio` are first class values and can and, indeed,
are commonly passed as arguments or the value returned by another
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
<define special form>`:

.. code-block:: idio

   define (foo a b) {
     a + b
   }

which creates a function called ``foo`` which takes two parameters,
``a`` and ``b``, and performs some addition on them.

Alternatively (and secretly what ``define`` does under the hood) you
can create an anonymous *function value* with the special form
:ref:`function <function special form>`:

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

.. _`closure parameters`:

Closure Parameters
^^^^^^^^^^^^^^^^^^

The formal parameters of a closure (named or anonymous) can take a
confusing set of forms primarily because of support for a varargs
parameter but also because of the two ways closures are created.

In general, if you specify `n` formal ("positional") parameters then
the caller must supply `n` arguments when the closure is invoked.  Any
more or less is an error.

If you specify a varargs parameters in addition to `n` formal
parameters then the user must supply *at least* `n` arguments and any
remaining arguments are bundled up into a list.  If there were no
extra arguments, the list is ``#n`` (the empty list) otherwise the
list will be the extra arguments.

If we assume there is a `body` of some kind following these
stub-expressions:

.. csv-table::
   :header: define, function, notes
   :widths: 40, 40, 50
   :align: left

   :samp:`define ({foo})`, :samp:`function #n`, no parameters at all
   :samp:`define ({foo} {a} {b})`, :samp:`function ({a} {b})`, two positional parameters
   :samp:`define ({foo} {a} {b} & {c})`, :samp:`function ({a} {b} & {c})`, two positional parameters and a varargs parameter `c`
   :samp:`define ({foo} & {c})`, :samp:`function {c}`, no positional parameters and only a varargs parameter `c`
   
Note that:

* in the first example, ``function`` is given ``#n``, the empty list,
  directly.  ``define`` always uses :samp:`pt {formals}` as the list
  of parameters and the :ref:`pt <pt>` of a list of one element is
  ``#n``.

* in the final example, ``function`` is given a single symbol instead
  of a list of parameters to indicate there is only a varargs
  parameter.

By way of example, epitomising elegance (or legitimising laziness),
the function :ref:`list <list>` is defined as:

.. code-block:: idio

   define (list & x) x

Here, ``list`` only takes a varargs parameter, that is all of its
arguments are bundled up into a list by the evaluator.  As ``list``'s
job is to return a list from its arguments and the evaluator has done
all the heavy lifting then ``list``'s `body` is simply to return the
list it was given as its varargs parameter `x`.

Closure Environment
^^^^^^^^^^^^^^^^^^^

When a closure is *created* it is associated with the current *frame of
execution* and the current module.  Together these describe both the
locally defined (through containing functions' parameters) local
variables as well as the set of *free variables* defined in this and
any imported modules.

When a closure is *run* all it has from the calling environment are
the (evaluated) arguments passed to it.  When the VM starts executing
the instructions of the closure it will be using the stored execution
frame and module associated with the closure's definition.

Special Forms
-------------

Special forms exist only in the evaluator and cannot be extended or
altered.

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
