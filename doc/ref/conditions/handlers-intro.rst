.. _`condition handlers`:

Condition Handlers
------------------

Handling errors in :lname:`Idio` works differently to many programming
languages.  In those, when an exception is raised then the computation
state is *unwound* to where the handler is and the handler is executed
and control continues from after the exception handling block.  Think
``try``/``except`` in :lname:`Python`.

In :lname:`Idio`, when a condition is raised the VM *pauses* the
current computation with all of the extant computation intact and
walks back through the execution state looking to see if any handlers
have been established for this condition type (or an ancestor of this
condition type).

If one is found the associated *handler* is called with the condition
as an argument which can do a number of things:

#. it can return a value in which case that value is used *in place
   of* the errant computation and the original computation continues
   as if the errant computation had returned whatever the handler has
   just returned

   This isn't used by the :lname:`Idio` defined error handlers as the
   conditions are not normally something easily recoverable from.
   There is an example below of unwise intervention.

   It also requires that the handler have some intimate knowledge of
   the state of processing at the time of the error.

   Suppose you wrote an :ref:`^i/o-no-such-file-error
   <^i/o-no-such-file-error>` handler around a call to :ref:`open-file
   <open-file>`.  The expectation of the calling code is to receive an
   open file handle and so, if you want to handle the missing file by
   "quickly creating and opening another file" in its stead to return
   the open file handle, you also need to return the correct type of
   file (input or output) and with the correct mode.

#. it can raise the condition again with a view that a previously
   established handler can take care of the problem

   The state computation is unchanged, the outer handler can still
   return a value.

#. it can raise a totally different condition

   The state computation is unchanged, the outer handler, handling a
   different condition, can still return a value to the original point
   of failure.

#. it can emulate the execution stack unwinding (``try``/``except``)
   by calling :ref:`trap-return <trap-return>` in the handler

----

You can establish handlers in two ways:

#. :ref:`trap <trap>` establishes a handler for a condition type (or
   types) around a block of code

#. :ref:`set-default-handler! <set-default-handler!>` establishes a
   handler for a condition type

   In that sense, ``set-default-handler!`` is much more like the
   shell's ``trap`` builtin.

If you don't do anything you will find there are several handlers
established by default, including:

* an :ref:`^rt-command-status-error <^rt-command-status-error>`
  handler, :ref:`default-rcse-handler <default-rcse-handler>`

  If an external program fails then this handler will normally cause
  :program:`idio` to exit in the same manner.  There are some
  mitigations.

* a :ref:`^condition <^condition>` handler,
  :ref:`default-condition-handler <default-condition-handler>`

  This looks to see if a handler for the condition's type has been
  established with ``set-default-handler!`` and, if so, runs it.

* a :ref:`^condition <^condition>` handler,
  :ref:`restart-condition-handler <restart-condition-handler>`

  This attempts to unwind the current state of execution to the most
  recent top level expression and runs its continuation.

* a :ref:`^condition <^condition>` handler,
  :ref:`restart-condition-handler <restart-condition-handler>`

  This attempts to exit cleanly.

  .. attention::

     This should be straight-forward but somehow isn't.

Example Handler
^^^^^^^^^^^^^^^

Suppose we want to handle :ref:`^rt-divide-by-zero-error
<^rt-divide-by-zero-error>`:

.. code-block:: idio

   trap ^rt-divide-by-zero-error (function (c) {
				    ; we could generate a scathing report with
				    ; condition-report "fool!" c

				    ; return a value indicating the
				    ; user's foolishness
				    'fool
   }) {
     1 / 0
   }

Hmm, nothing.  Well, technically, ``trap`` itself will have returned
the symbol ``fool``.

Suppose the `body` was more complex and went on to use the returned
value:

.. code-block:: idio

   trap ^rt-divide-by-zero-error (function (c) {
				    'fool
   }) {
     t := 1 / 0
     1 + t
   }

This shows our handler as being incredibly naïve as now we get an
:ref:`^rt-parameter-type-error <^rt-parameter-type-error>` in the next
expression as the addition, ``+``, won't accept the symbol as a valid
type.

We can revert to the more common ``try``/``expect`` behaviour by
returning from the ``trap`` itself with :ref:`trap-return
<trap-return>`:

.. code-block:: idio

   trap ^rt-divide-by-zero-error (function (c) {
				    trap-return 'fool
   }) {
     t := 1 / 0
     1 + t
   }

Here, we return the symbol ``fool`` from ``trap`` as soon as the
divide-by-zero error occurs and without stumbling into the problem
with addition.
