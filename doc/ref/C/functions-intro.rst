.. idio:currentmodule:: C

C Functions
-----------

.. attention::

   All of the :lname:`C` functions will only operate on arguments of
   **the same** :ref:`C base types <c module types>` (or typedefs
   thereof).

   You cannot mix :lname:`C` data types in an expression.

The :lname:`C`-variant primitives for ``+``, ``-``, ``*`` and ``/``
are called from the binary arithmetic primitives :ref:`binary-+
<binary-+>`, :ref:`binary-- <binary-->`, :ref:`binary-* <binary-*>`
and :ref:`binary-/ <binary-/>`.  They are not primitives exported from
the ``C`` module.
