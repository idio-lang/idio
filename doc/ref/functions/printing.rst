Printing Values
^^^^^^^^^^^^^^^

All :lname:`Idio` values can be printed although not all have a valid
reader input form.  Those without valid reader forms are subject to
change.  The output generally takes the form :samp:`#<{TYPE} ...>`.

If you are unsure, use ``%s`` ("as a string") as a generic catch-all
format specifier.  This "as a string" is used as default behaviour for
unexpected format specifiers (and which may result in a warning).  See
below.

There is a subtle distinction between a value being *printed* --
commonly at the REPL as generally suitable to be read back in -- and
being *displayed* as general output of the program.

This only affects unicode and string types where any escaping is
removed and full UTF-8 encoding is used when displayed.

.. warning::

   Very deep data structures are truncated.

Fixnums
"""""""

:ref:`Fixnums <fixnum type>` support the usual integer formats
``%[Xbdox]`` where ``%b`` is a binary format.

Any other format specifier is treated as ``%d``.

Bignums
"""""""

:ref:`Bignums <bignum type>` have limited printing support.

Integer bignums are printed as, effectively, ``%d``.

Floating point bignums are printed as one of:

* ``%s`` a normalized form

* ``%e`` or ``%f`` in the style of :manpage:`printf(3)`

Any other format specifier is treated as ``%e``.

The output may be preceded by ``#i`` (or ``#e``) indicating the
underlying bignum is *inexact* (or *exact*).

Unicode
"""""""

In general, :ref:`unicode <unicode type>` is *printed* as ``#U+HHHH``
although if the code point is below 128 and satisfies
:manpage:`isgraph(3)` it will be printed as :samp:`#\\c`.

If unicode is being *displayed* :lname:`Idio` generates the UTF-8
encoding for the value.

Strings
"""""""

:ref:`Strings <string type>` are *printed* double-quoted and with the
usual :lname:`C` escape sequences escaped: a newline will be
represented by ``\n``, for example.  Otherwise Unicode code points are
encoded as UTF-8.

String are *displayed* unquoted and with all Unicode code points UTF-8
encoded.

Symbols
"""""""

:ref:`Symbols <symbol type>` are displayed as just their name, without
a leading ``'``.

This creates some anomalies.

Pairs
"""""

:ref:`Pairs <pair type>` are printed as expected.

If the list represents a template any original change to interpolation
characters is lost and standard :lname:`Idio` symbols are used.

Arrays
""""""

:ref:`Arrays <array type>` are printed in the nominal reader form:
``#[ ... ]``.

If the array is less than 40 elements long it is printed in full.

.. warning::

   If it is more than 40 elements long then the first 20 and the last
   20 are printed with in ellipsis and missing index marker displayed.

Hash Tables
"""""""""""

:ref:`Hash tables <hash table type>` are printed in the nominal reader
form: ``#{ ... }``.

Bitsets
"""""""

:ref:`Bitsets <bitset type>` are printed in the nominal reader form:
``#B{ ... }``.

C Types
"""""""

:ref:`C types <c module types>` are printed as:

* ``%c`` for ``char``

* ``%d`` for signed integral types

  Any other format specifier is treated as ``%d``.

  Any format specifier other than ``%s`` may elicit a warning.

* ``%[Xboux]`` for unsigned integral types including the ``%b`` binary format

  Any other format specifier is treated as ``%u``.

  Any format specifier other than ``%s`` may elicit a warning.

* ``%[efg]`` for floating point types

  Any other format specifier is treated as ``%g``.

  Any format specifier other than ``%s`` may elicit a warning.

``C/pointer`` types have the option to be printed by a bespoke
printer.  See :ref:`add-as-string <add-as-string>`.

Struct Instances
""""""""""""""""

:ref:`Struct instances <struct type>` have the option to be printed by
a bespoke printer.  See :ref:`add-as-string <add-as-string>`.
