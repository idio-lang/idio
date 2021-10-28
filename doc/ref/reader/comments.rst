.. _`comments`:

Comments
--------

There are several comment forms in :lname:`Idio` -- none of which are
shell-like.  That's largely because the shell comment character,
``#``, is used to tell the reader that some interesting construction
is coming its way.

Line
^^^^

You can comment everything to the end of the line with ``;``.

S-exp
^^^^^

You can comment out an entire s-exp with ``#;``, including multi-line
s-exps.

S-exps are usually parenthesised expressions, :samp:`(...)`, which,
noting that :lname:`Idio` doesn't always use them, makes this kind of
comment less useful.

.. code-block:: idio

   a := 10

   #;(and my-debug-flag
	  (set! a 5))

   a			; 10

Multi-line
^^^^^^^^^^

There are two types of nestable multi-line comments:

* ``#* ... *#`` for regular commentary

* ``#| ... |#`` which is reserved for "semi-literate" comments

  These are a nod to :ref-author:`Donald Knuth`'s `literate
  programming`_ but inverted: code interspersed with natural language.

  That said, this form doesn't do anything other than act as another
  form of multi-line comment.  Some thought needs to be put into what
  to do with semi-literate commentary.  Currently, junk it!

The multi-line part of these forms should be self-explanatory, the
*nestable* part is more interesting.

In the first instance it means you can safely comment out any larger
block of code that already contains a multi-line comment:

.. code-block:: idio

   #*

   a := 10

   #*
   if my-debug-flag {
     a = 5
   }
   *#

   a
   *#

These two forms are mutually nestable meaning you can comment out
parts of your semi-literate commentary and put semi-literate
commentary in your comments.

Escaping
""""""""

There are always some corner cases where we might need to escape our
way out:

WARNING: the web page language interpreter doesn't handle these very
well!

.. code-block:: idio

   #*

   ; don't use the comment terminator, \*#, here

   *#

If you want to use something other than ``\`` for the escape
character, put the graphic character (ie., not whitespace) immediately
after the opening ``#*`` or ``#|``:

.. code-block:: idio

   #*%

   ; don't use the comment terminator, %*#, here

   *#

