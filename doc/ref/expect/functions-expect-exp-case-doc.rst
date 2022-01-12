``exp-case`` looks to find the first match from the clauses:

* passed to :ref:`exp-case-before <expect/exp-case-before>`, if any

* passed to ``exp-case`` itself, if any

* passed to :ref:`exp-case-after <expect/exp-case-after>`, if any

``exp-case`` operates on the current value of :ref:`spawn-id
<expect/spawn-id>` (no such value can be passed directly as
``exp-case`` is a template).

Each clause in `clauses` takes one of the forms

    :samp:`([:str-kw]* {string} {body})`

    :samp:`(:term-kw {body})`

Here, :samp:`:str-kw` is one of the string keywords:

* ``:re`` indicating that :samp:`{string}` should be used for a
  regular expression match

* ``:gl`` indicating that :samp:`{string}` should be used for a
  glob-style pattern match (the default) -- see
  :ref:`regex-pattern-string <regex-pattern-string>` for how the
  string is modified
  
* ``:ex`` indicating that :samp:`{string}` should be used for an exact
  match -- see :ref:`regex-exact-string <regex-exact-string>` for how
  the string is modified

* ``:icase`` indicates that a case-insensitive match should be used

:samp:`:term-kw` is one of the terminal keywords:

* ``:eof`` for matching the End of File indicator

  .. note::

     If ``spawn-id`` is a list then :samp:`{body}` will be called for
     each spawn-id for which the condition is true.

     If ``spawn-id`` is a list then it is inadvisable to call
     ``(exp-continue)`` in the :samp:`{body}` of an ``:eof`` clause as
     that will prevent the invocation of any other extant End of File
     notifications.

     See also the End of File note below.

* ``:timeout`` for matching a time out

  .. note::

     ``:timeout`` will be true for all spawn-ids if `spawn-id` is a
     list.  The :samp:`{body}` will be called with the value of
     `spawn-id` (which could be a list or a struct instance).

* ``:all`` to match everything in the buffer, ie. in effect, to empty
  the buffer

  .. note::

     ``:all`` always matches and therefore will not read any more data
     from the spawned process.

     If ``spawn-id`` is a list then for each spawn-id in the list the
     entire buffer will be matched and the :samp:`{body}` called.

:samp:`{body}` will be invoked as though the body of a function with
the following arguments:

* for a successful string match, including ``:all``, the arguments are
  ``spawn-id``, ``r``, the result of the match as per :ref:`regexec
  <regexec>`, and ``prefix``, the contents of the `spawn-id`'s buffer
  before the match

* for a successful terminal match the argument is ``spawn-id``

:samp:`{body}` can invoke the function ``(exp-continue)`` to loop
around again.

Passing no clauses will still attempt to match using any existing
clauses from :ref:`exp-case-before <expect/exp-case-before>` or
:ref:`exp-case-after <expect/exp-case-after>`.

If a clause matches, ``exp-case`` returns the value from
:samp:`{body}`.  If no clauses match or all spawn-ids have indicated
End of File, or a timeout has occurred and there is no ``:timeout``
clause ``exp-case`` returns ``#f``.

.. admonition:: End of File

   If the spawned process indicates End of File then the master file
   descriptor is generally closed although this is not guaranteed.

   Call :ref:`exp-wait <expect/exp-wait>` to clean up both file
   descriptors and spawned processes.

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
