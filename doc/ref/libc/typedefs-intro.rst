.. _`libc typedefs`:

.. idio:currentmodule:: libc

libc typedefs
-------------

There are many :lname:`C` *typedef*\ s in use in :lname:`Idio` which
are *essential* for correct interaction with the :lname:`C` API.

There will be a type definition, say, ``pid_t``, which maps directly
or through a chain of other typedefs to a :lname:`C` base type
definition, a symbol, say, ``'int``.

In parallel, there is a type predicate, say, ``pid_t?``, which maps
directly or through a chain of other predicates to a :ref:`C <C
module>` predicate, say, ``C/int?``.

.. rst-class:: center

   \*

Even though the :lname:`C` API defines a portable typedef, say,
``pid_t``, your operating system might use some intermediate typedefs.
Fedora uses ``__pid_t``, for example, in the function interfaces and
``pid_t`` is a typedef of ``__pid_t``.

The chain of mappings for both the typedef and predicate to the
underlying :lname:`C` base type and predicate will reflect those
intermediate mappings.

----

The following mappings are for the system this documentation was built
on and are representative of that system, not yours.

The complete set of typedefs and predicates are defined for your
system in :file:`lib/libc-api.idio`.

