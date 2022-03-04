.. idio:module:: Idio

.. _`IOS`:

******************
Idio Object System
******************

The Idio Object System is derived from :ref-author:`Gregor Kiczales`'s
`Tiny CLOS <http://community.schemewiki.org/?Tiny-CLOS>`_ and
provides:

* classes with instance slots

* multiple-inheritance

* generic functions with multi-methods

* primary methods and next-method

IOS differs from many familiar object-oriented systems in that classes
and the functions that operate on them are separated.

On the one hand you define classes with named fields and optional
super-classes and on the other hand you define generic functions and
associate with those generic functions methods with particular
argument specializers (classes).

When a generic function is invoked with some arguments, the most
specific method for those arguments is invoked which can call the next
most specific method should it need to.

The computation of the most specific method (and the increasingly less
specific methods in turn) uses all the arguments to the generic
function not just the one (usually the first) as in other systems.

.. toctree::
   :maxdepth: 2
