.. _`constants`:

Constants
=========

There are a lot of constants in :lname:`Idio` and, indeed, several
*kinds* of constants although most users are only going to use a small
number.

The usual constants are:

.. data:: #f

   The boolean value for `false`.

   This is the **only** value that is tested for.  *Any* other value
   is considered to be `true`.

.. data:: #t

   The boolean value for `true`.  In fact, as any non-``#f`` value is
   regarded as `true` (that is, it isn't ``#f``) then ``#t`` is
   entirely redundant.  It is, however, a useful non-``#f`` value to
   return from a *predicate*.

.. data:: #n

   The `null` value.

   The most useful use of this value is for marking the end of a chain
   of :ref:`pairs <pair type>` -- commonly known as a list.

Other Constants
---------------

Most other constants do not have constructors for use in source code,
they exist *in machina*.  Their printed form is always unacceptable to
the reader.

Other constants that may appear include:

.. data:: #<unspec>

   No useful value.

   Most functions that *set* something return ``#<unspec>``.

   Scholars (and laymen) disagree on what the result of the
   *computation* to modify memory should be in which case none shall
   be correct.

   Printing also returns ``#<unspec>`` for similarly arcane reasons.

.. data:: #<undef>

   The undefined value.

   No value a user sees should be undefined!

   It is used internally for concomitantly defined values and
   *shouldn't* escape.

.. data:: #<void>

   The result of no computation!

   There are several instances of no computation:

   #. a conditional statement where the condition is *false* and there
      is no alternate clause:

      .. code-block:: idio-console

	 Idio> if #f #f
	 #<void>

   #. a ``begin`` clause with no expressions:

      .. code-block:: idio-console

	 Idio> (begin)
	 #<void>

   #. a ``cond`` or ``case`` expression where no match exists and
      there is no ``else`` clause

   In all cases, *something* must be returned.

.. data:: #<eof>

   A flag indicating that a read has reached end of file (or string!).

   .. seealso:: :ref:`eof? <eof?>` and :ref:`eof-object?
                <eof-object?>`.

Other Kinds of Constants
^^^^^^^^^^^^^^^^^^^^^^^^

Where possible, :lname:`Idio` uses constants internally for other
purposes, rather than :lname:`C` integers *per se*.

The are groups of constants for:

* reader tokens as source code is being read in and converted to an
  Abstract Syntax Tree

* generalized intermediate code from the evaluator to the code
  generator

* :ref:`unicode <unicode type>` is a distinct set of constants as is
  its (deprecated) predecessor "character" type

