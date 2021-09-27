.. _`constant types`:

Constant Types
==============

There are a lot of constants in :lname:`Idio` and, indeed, several
*kinds* of constants although most users are only going to use a small
number.

The usual constants are:

.. data:: #t

   The boolean value for `true`.  In fact, as any non-``#f`` value is
   regarded as "true" (that is, it isn't ``#f``) then ``#t`` is
   entirely redundant.  It is, however, a useful non-``#f`` value to
   return from a *predicate* function.

.. data:: #f

   The boolean value for `false`.

   This is the only value that is tested for.  Any other value is
   considered to be `true`.

.. data:: #n

   The `null` value.

   The most useful use of this value is for marking the end of a chain
   of :ref:`pairs <pair type>` -- commonly known as a list.



