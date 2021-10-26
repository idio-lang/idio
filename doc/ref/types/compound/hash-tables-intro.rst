.. _`hash table type`:

Hash Table Type
===============

Hash tables are arbitrary value indexed collections of references to
values.  The only value you cannot use as an index is ``#n``.

Reader Form
-----------

Hash tables of simple key and value types can be constructed with the
reader form

    :samp:`#\\{ [({key} & {value}) ...] }`

degenerating to ``#{}``.

Any :samp:`{key}` or :samp:`{value}` will not be evaluated, only
simple type construction (numbers, strings, symbols etc.) will occur.

This reader form ultimately calls :ref:`alist-\>hash <alist-\>hash>`
meaning the equivalence function will be the :lname:`C` implementation
of :ref:`equal? <equal?>` and the hashing function will be the default
:lname:`C` implementation.

