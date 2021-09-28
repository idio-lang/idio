.. _`unicode type`:

Unicode Type
============

The *unicode* type is a representation of Unicode_ (arguably
ISO10646_) code points.  In practice they are much like an integer
ranging from 0 to 0x10FFFF although they do not overlap or have any
direct interaction with integers, they are a separate type.


Reader Forms
------------

The canonical reader form is :samp:`#U+HHHH` where the number of hex
digits, :samp:`{H}`, is "enough."  Leading zeroes are not required,
``#U+127`` is the same as ``#U+0127``.

An alternate reader form is :samp:`#\\x` where :samp:`{x}` is the
UTF-8 representation of the code point -- for example, ``#\ħ`` would
be read as U+0127 (LATIN SMALL LETTER H WITH STROKE).

A final reader form is for a limited number of named characters, say,
``#\{newline}``, with the name in braces.

We could, but we don't, use the Unicode Character Database names, say,
``#\{LATIN SMALL LETTER H WITH STROKE}``.

Instead the set of names is limited to:

.. csv-table::
   :header: name, code point, C escape sequence
   :widths: auto
   :align: left

   nul,             U+0000
   soh,             U+0001
   stx,             U+0002
   etx,             U+0003
   eot,             U+0004
   enq,             U+0005
   ack,             U+0006
   bel,             U+0007, ``\a``
   bs,              U+0008, ``\b``
   ht,              U+0009, ``\t``
   lf,              U+000A, ``\n``
   vt,              U+000B, ``\v``
   ff,              U+000C, ``\f``
   cr,              U+000D, ``\r``
   so,              U+000E
   si,              U+000F
   dle,             U+0010
   dcl,             U+0011
   dc2,             U+0012
   dc3,             U+0013
   dc4,             U+0014
   nak,             U+0015
   syn,             U+0016
   etb,             U+0017
   can,             U+0018
   em,              U+0019
   sub,             U+001A
   esc,             U+001B
   fs,              U+001C
   gs,              U+001D
   rs,              U+001E
   us,              U+001F
   sp,              U+0020

   alarm,           U+0007, ``\a``
   backspace,       U+0008, ``\b``
   tab,             U+0009, ``\t``
   linefeed,        U+000A, ``\n``
   newline,         U+000A, ``\n``
   vtab,            U+000B, ``\v``
   page,            U+000C, ``\f``
   return,          U+000D, ``\r``
   carriage-return, U+000D, ``\r``
   esc,             U+001B
   escape,          U+001B
   space,           U+0020
   del,             U+007F
   delete,          U+007F

   lbrace,          U+007B
   {,               U+007B

