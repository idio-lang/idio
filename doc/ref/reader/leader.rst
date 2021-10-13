.. _`reader`:

***********
Idio Reader
***********

The *reader*'s role is to consume UTF-8 source code and construct an
Abstract Syntax Tree suitable for the :ref:`evaluator <evaluator>`.

A complete :lname:`Idio` expression is a whole line (up to a newline)
or however many more lines is required to complete any matching
parentheses, braces, brackets or double-quoted strings.

You cannot have multiple expressions on one line.

The matching part of the reader gives rise to a standard form, say:

.. code-block:: idio
   :linenos:

   define (add a b) "
   add `a` to `b`
   ...
   " {
     ;; calculate a result in a local variable
     r := a + b

     ;; return the result
     r
   }

Here the starting ``"`` on line 1 means the documentation string is
not complete until the matching ``"`` on line 4 where the ``{`` before
the end of line means the body is not complete until the matching
``}`` on line 10.

An :lname:`Idio` expression of multiple words is always a list.  So,
even though the example didn't start and end with a parenthesis, the
AST for that expression will be

    :samp:`(define ({name} {formals}) {docstr} {body})`

Similarly:

.. parsed-literal::

   ls -l
   (ls -l)

are identical.  Multiple words always create a list.

.. rst-class:: center

   \*

Whilst not a problem for the reader, single words are problematic:

.. code-block:: idio

   ls

With our shell hats on we want to run the external program
:program:`ls`.  However, :lname:`Idio` is a programming language and
programming language idioms win out.

Consider line 9 in the above example:

* do you intend to return `r`, the value from the addition of `a` and
  `b`?

* do you want to run the external program that is called whatever the
  *value* of `r` is?

* do you want to run the external program called :program:`r`?

:lname:`Idio` will return the value of `r`, here, the addition of `a`
and `b`.

:lname:`Idio` will similarly return the value of `ls` from our single
word example.  `ls` could be a variable, in which case you'll get its
value but it could be undefined and therefore result in itself, a
symbol, ``ls``.

Neither of which is very helpful in *executing* the program
:program:`ls`.  To force a single word to be executed you must wrap it
in parentheses:

.. code-block:: idio

   (ls)

This forces :lname:`Idio` to interpret the expression as a *list* of a
single symbol.  A list will be evaluated as a "function" call where,
here, the function name, a plain symbol, means the VM will seek an
external program from the current :envvar:`PATH`.

.. rst-class:: center

   \*

The reader will construct simple values as it sees them in the source
code, numbers, strings, symbols, etc. and return those values in the
AST.

The AST itself will be lists of lists of lists.  The printed form of
such lists and simple values looks, by and large, like the original
source code.

.. toctree::
   :maxdepth: 1

