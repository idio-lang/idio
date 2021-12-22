Each clause in `clauses` takes one of the forms

    :samp:`([:str-kw]* {string} {body})`

    :samp:`(:term-kw {body})`

Here, :samp:`:str-kw` is one of the string keywords: ``:re``, ``:gl``,
``:ex`` indicating that :samp:`{string}` should be used as a regular
expression match, glob-style match (the default) or an exact match.

The string keyword ``:icase`` indicates that a case-insensitive match
should be used.

:samp:`:term-kw` is one of the terminal keywords: ``:eof`` for
matching the End of File indicator, ``:timeout`` for matching a time
out.

:samp:`{body}` will be invoked as though the body of a function with
the following arguments:

* for a successful string match the arguments are ``spawn-id``, ``r``,
  the result of the match as per :ref:`regexec <regexec>`, and
  ``prefix``, the contents of the `spawn-id`'s buffer before the match

* for a successful terminal match the argument is ``spawn-id``

:samp:`{body}` can invoke the function ``exp-continue`` to loop around
again otherwise the result of :samp:`{body}` is returned.

.. note::

   All (supported) operating systems can use :manpage:`poll(2)`.
   However, some tested operating systems (Mac OS 10.5.8) return
   ``POLLNVAL`` for (pseudo-terminal) devices.

   In this case, the code reverts to the uses of :manpage:`select(2)`
   with any associated limits (notably, ``FD_SETSIZE``).

:Example:

From the top of the :lname:`Idio` distribution you might try:

.. code-block:: idio

   import expect

   spawn ls -1		;; minus one !

   (expect-case
    (:re "NG[.]" {
      printf ":re '%s' => %s\n" prefix r
      (exp-continue)
    })
    ("doc?" {
      printf ":gl '%s' => %s\n" prefix r
      (exp-continue)
    })
    (:icase "EXT?" {
      printf ":gl '%s' => %s\n" prefix r
      (exp-continue)
    })
    (:ex "NSE." {
      printf ":ex '%s' => %s\n" prefix r
      (exp-continue)
    })
    (:eof {
      printf ":eof\n"
    })
    (:timeout {
      printf ":timeout\n"
    }))

to get:

.. code-block:: console

   :re 'bin
   CONTRIBUTI' => #[ ("NG." 15 18) ]
   :gl 'md
   ' => #[ ("doc\r" 4 8) ]
   :gl '
   ' => #[ ("ext\r" 1 5) ]
   :ex '
   lib
   LICENSE
   LICE' => #[ ("NSE." 19 23) ]
   :eof
