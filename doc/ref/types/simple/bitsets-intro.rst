.. _`bitset type`:

Bitset Type
===========

Bitsets are arrays of bits.

They were originally introduced to provide testable state for
:ref:`SRFI-14 <SRFI-14 module>` char-sets: "is this code point
*Lowercase*?"

Reader Form
-----------

The reader form for bitsets is

    :samp:`#B\\{ {size} [{bits} ...] }`.

:samp:`{size}` indicates the absolute size of the bitset with indexing
starting at 0.  Referencing a bit beyond this range is an error.

All bits are initially cleared.

:samp:`{bits}` defines the bits that are initially set (or clear) with
a sequence of 1s and 0s in blocks of up to 8 bits.  The first 0 or 1
sets the first bit, the second 0 or 1 the second bit etc..  The next
block of eight bits starts at the ninth bit and continues.

Setting the n\ :sup:`th` bit in a large bitset should not involve
explicitly indicating that all the previous bits were 0 so
:samp:`{bits}` can take the form :samp:`{offset}:{bits}` where
:samp:`{offset}` is a hexadecimal number and is a multiple of 8.
Intervening blocks are skipped.  Subsequent :samp:`{bits}` continue
with the next block.

Whole blocks of bits can be set with the form :samp:`{first}-{last}`
where :samp:`{first}` and :samp:`{last}` are hexadecimal numbers (and
multiples of 8) representing the first block of all 1s through to the
last block inclusive.  To set a single block use
:samp:`{first}-{first}`.

By way of example:

* ``#B{ 32 001 }`` is much like hexadecimal 0x0004

* ``#B{ 32 1:1 }`` is much like hexadecimal 0x0010

* ``#B{ 32 1 2-3 }`` is much like hexadecimal 0xFF01

* ``#B{ 32 01 2-2 00000001 }`` is much like hexadecimal 0x8F02

* ``#B{ 65536 30-30 11000000 01111110 60:01111110 }`` is the bitset of
  Unicode's *ASCII_Hex_Digit* Property in Plane 0, this it, bits
  0x30-0x39, 0x41-0x46 and 0x61-0x66.

* try typing ``SRFI-14/char-set:lower-case`` at the :lname:`Idio`
  prompt to see a richer example

