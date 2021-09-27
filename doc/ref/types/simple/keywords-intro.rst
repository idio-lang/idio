.. _`keyword type`:

Keyword Type
============

Keywords are very similar to symbols but serve a slightly different
purpose.  Like symbols they are a word but must begin with a ``:``,
U+003A (COLON).

They exist as semantic flags -- rather than as symbols' programmatic
flags -- to be used to identify optional arguments to functions.
Think of a function with many possible optional arguments and if you
only want to set the ``foo`` optional parameter you might invoke:

.. parsed-literal::

   func *formals* :foo *arg*

There is a constraint on the possible keywords in that in order to
avoid clashing with the definition operators, ``:=`` etc., then the
characters of a keyword after the ``:`` must not start with a
punctuation character, (through :manpage:`ispunct(3)`).

So ``:foo=bar`` is fine but ``:=bar`` is not -- and will be
interpreted as a symbol.

