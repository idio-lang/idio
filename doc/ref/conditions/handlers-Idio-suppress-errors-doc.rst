Establish a handler (that returns ``#<void>``) for conditions
`conditions` around `body`

:param conditions: a condition type or a list of condition types
:type conditions: a condition type or a list of condition types
:param body: the body expression
:type body: expression

.. attention::

   ``suppress-errors!`` has a very narrow use case.  In general,
   returning a value to failed computations requires intimate
   knowledge of the computation.

   However, ``suppress-errors!`` is used exclusively with
   :ref:`^system-error <^system-error>` in situations where there is a
   race condition between the parent and child after a :ref:`libc/fork
   <libc/fork>` which results in one or the other failing.
