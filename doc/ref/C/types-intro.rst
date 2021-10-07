.. idio:currentmodule:: C

.. _`c module types`:

C Types
-------

:lname:`Idio` supports the use of the fourteen :lname:`C` base types
and typedefs thereof and a pointer type:

.. csv-table::
   :header: :lname:`Idio`, :lname:`C`
   :widths: auto

   ``C/char``, ``char``
   ``C/schar``, ``signed char``
   ``C/uchar``, ``unsigned char``
   ``C/short``, ``short``
   ``C/ushort``, ``unsigned short``
   ``C/int``, ``int``
   ``C/uint``, ``unsigned int``
   ``C/long``, ``long``
   ``C/ulong``, ``unsigned long``
   ``C/longlong``, ``long long``
   ``C/ulonglong``, ``unsigned long long``
   ``C/float``, ``float``
   ``C/double``, ``double``
   ``C/longdouble``, ``long double``
   ``C/pointer``, ``void *``

Equality tests of ``C/longdouble`` are, at best, untested.

The ``C/pointer`` type is used to carry references to :lname:`C`
allocated memory.

There is a special form of ``C/pointer`` with a *C Structure
Identifier* tag which allows for the implicit use of accessors and
printers of that value.  Most of the allocated :lname:`C` ``struct``\
s in :ref:`libc <libc module>` are so tagged.
