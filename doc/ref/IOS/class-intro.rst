.. idio:currentmodule:: object

IOS Defined Classes
-------------------

IOS defines a number of classes, several of which are used internally
to define the meta-object protocol (MOP).

MOP Classes
^^^^^^^^^^^

The MOP classes are used to define the implementation of the
(user-level) use of classes and are (necessarily) circular in
definition.

Most classes have a MOP class of ``<class>`` -- including ``<class>``
itself.

MOP classes are distinct from the normal super-class element in a
class definition.  MOP classes do have super-classes as seen in this
table:

.. csv-table::
   :header: class, super-class, description
   :align: left
   :widths: auto

   ``<top>``, (none), the root
   ``<object>``, ``<top>``, the ancestor of all objects
   ``<procedure-class>``, ``<class>``, the ancestor of all invokable classes
   ``<entity-class>``, ``<procedure-class>``, the MOP class of generics
   ``<generic>``, ``<object>``,
   ``<method>``, ``<object>``,

Builtin Classes
^^^^^^^^^^^^^^^

The regular :lname:`Idio` types are represented by classes with a
super-class ``<builtin-class>`` (itself with a super-class of
``<top>``).

The builtin classes include:

``<C/char>`` ``<C/double>`` ``<C/float>`` ``<C/int>`` ``<C/long>``
``<C/longdouble>`` ``<C/longlong>`` ``<C/pointer>`` ``<C/schar>``
``<C/short>`` ``<C/uchar>`` ``<C/uint>`` ``<C/ulong>``
``<C/ulonglong>`` ``<C/ushort>`` ``<array>`` ``<bignum>`` ``<bitset>``
``<constant>`` ``<continuation>`` ``<fixnum>`` ``<handle>`` ``<hash>``
``<keyword>`` ``<module>`` ``<pair>`` ``<string>``
``<struct-instance>`` ``<struct-type>`` ``<symbol>`` ``<unicode>``


``<closure>`` and ``<primitive>`` are invokable classes.
