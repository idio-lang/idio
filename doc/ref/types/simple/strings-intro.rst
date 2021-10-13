.. _`string type`:

String Type
===========

Strings are arrays of Unicode code points efficiently packed into
variable-width arrays.

Substrings are references into sections of :lname:`Idio` strings but
are otherwise handled the same.

Pathnames are a subset of strings where the elements of the string are
not treated as UTF-8.  Any file name value returned from the operating
system will be a pathname.

Consequently, you cannot directly compare a file name from the file
system to a string from your source code.  See :ref:`string->pathname
<string->pathname>` for a conversion function.  There is no reverse
function (pathname to string) as there is no encoding in a file name,
it is just a sequence of bytes.

Reader Form
-----------

The input form for a string is the usual ``"..."``, that is a U+0022
(QUOTATION MARK) delimited value.

The collected bytes are assumed to be part of a valid UTF-8 sequence.
If the byte sequence is invalid UTF-8 you will get the (standard) �,
U+FFFD (REPLACEMENT CHARACTER) and the decoding will resume *with the
next byte*.  This may result in several replacement characters being
generated.

There are a couple of notes:

#. ``\``, U+005C (REVERSE SOLIDUS -- backslash) is the escape
   character.  The obvious character to escape is ``"`` itself
   allowing you to embed a double-quote symbol in a double-quoted
   string: ``"hello\"world"``.

   In the spirit of `C escape sequences
   <https://en.wikipedia.org/wiki/Escape_sequences_in_C>`_
   :lname:`Idio` also allows:

   .. csv-table:: Supported escape sequences in strings
      :header: sequence, (hex) ASCII, description
      :align: left
      :widths: auto

      ``\a``, 07, alert / bell
      ``\b``, 08, backspace
      ``\e``, 1B, escape character
      ``\f``, 0C, form feed
      ``\n``, 0A, newline
      ``\r``, 0D, carriage return
      ``\t``, 09, horizontal tab
      ``\v``, 0B, vertical tab
      ``\\``, 5C, backslash
      ``\x...``, , up to 2 hex digits representing any byte
      ``\u...``, , up to 4 hex digits representing a Unicode code point
      ``\U...``, , up to 8 hex digits representing a Unicode code point

   Any other escaped character results in that character.

   For ``\x``, ``\u`` and ``\U`` the code will stop consuming code
   points if it sees one of the usual delimiters or a code point that
   is not a hex digit: ``"\Ua9 2021"`` silently stops at the SPACE
   character giving ``"© 2021"`` and, correspondingly,
   ``"\u00a92021"`` gives ``"©2021"`` as a maximum of 4 hex digits are
   consumed by ``\u``.

   ``\x`` is unrestricted (other than between 0x0 and 0xff) and ``\u``
   and ``\U`` will have the hex digits converted into UTF-8.

   Adding ``\x`` bytes into a string is an exercise in due diligence.

#. :lname:`Idio` allows multi-line strings:

   .. code-block:: idio

      str1 := "Hello
      World"

      str2 := "Hello\nWorld"

   The string constructors for ``str1`` and ``str2`` are equivalent.

Pathnames
^^^^^^^^^

``%P"..."`` (or ``%P(...)`` or ``%P{...}`` or ``%P[...]``) where the
``...`` is a regular string as above.

That's where the :samp:`\\x{HH}` escape for strings comes into its
own.  If we know that a filename starts with ISO8859-1_'s 0xA9 (the
same "character" as ©, U+00A9 (COPYRIGHT SIGN)), as in a literal byte,
0xA9, and not the UTF-8 sequence 0xC2 0xA9, then we can create such a
string: ``%P"\xa9..."``.

Pathnames, or strings being used as pathnames, with an ASCII NUL
(``\x00``) will result in a format error when they are attempted to be
used.  They are perfectly valid code points for :lname:`Idio` strings
but it is not possible to have an ASCII NUL in a :lname:`C` string,
being passed to the operating system's API.

Interpolated Strings
^^^^^^^^^^^^^^^^^^^^

From time to time it is convenient to want to expand references to
variables inside a string.  There is a special reader form for such
interpolated strings:

    ``#S{...${expr}...}``

Here, everything between the outermost matching ``{`` and ``}`` are
scanned for instances of the *interpolation sigil*, ``$``.  A matching
set of ``{`` and ``}`` is read in and the expression therein is
evaluated, the result being converted to a string (if required) and
replacing the interpolated expression.  The rest of the string is
added in a similar way.

If you want to embed an actual interpolation sigil, ``$``, you can
escape it with the default escape character ``\``:

    ``#S{Your \$PATH will be '${(frob-path)}'!}``

Whatever the call to ``frob-path`` returns will be converted to a
string (if necessary) giving a string equivalent to:

    ``"Your $PATH will be '...'!"``

In this particular case, there's little advantage over using
:ref:`sprintf <sprintf>` etc. but in code generation it is much more
convenient to see (pre-)constructed variable references *in situ* in
the expected output.

There are two options you can pass, between the ``#S`` and opening
brace: an alternative interpolation sigil and an alternative escape
character.

In effect, normal behaviour is:

    ``#S$\{...}``

If you only want to change the escape character, use ``.`` for the
interpolation sigil -- which implies that the interpolation sigil
cannot be ``.``.

If the use of braces, ``{`` and ``}``, means you would need to escape
braces within the interpolated string a lot you can use parenthesis or
brackets as the delimiting pair:

.. code-block:: idio

   ; generate some C code
   printf #S[
   if ($condition) {
       doit(${c-name arg1}, ${c-name arg2});
   }
   ]

although note that you can only use braces for the expression
delimiters.

