.. idio:module:: expect

.. _`expect module`:

*************
expect Module
*************

Expect_ is :ref-author:`Don Libes`' tool for managing interactive
applications.  It was written as an extension to :ref-author:`John
Ousterhout`'s Tcl_.  As a mark of its utility, `alternative ports
<https://en.wikipedia.org/wiki/Expect#Alternatives>`_ have appeared in
many languages.  :lname:`Idio` is no exception.

The ``expect`` module handles simple :manpage:`expect(1)`-like
behaviour.

Normally, :ref:`spawn-id <expect/spawn-id>` is a :ref:`struct-spawn
<expect/struct-spawn>` however it may be more convenient to operate on
multiple spawned processes simultaneously by treating ``spawn-id`` as
a list.  This is allowed but you need a much deeper understanding of
what your processes are doing particularly when matching ``:eof``.

.. toctree::
   :maxdepth: 2

