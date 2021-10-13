Establish a handler `handler` for conditions `conditions` around `body`

:param conditions: a condition type or a list of condition types
:type conditions: symbol or a list of symbols
:param handler: the handler
:type handler: 1-ary function
:param body: the body
:type body: expression

``trap`` may not return.

Normally, ``trap`` will return whatever `body` returns -- usually
`body` is a :ref:`block <block special form>` and so the value of the
last expression evaluated.

If a condition is raised during the processing of `body` then ``trap``
may (eventually) continue processing `body` if a handler returns a
value in place of the condition-raising expression.  ``trap`` will
therefore return the value of the last expression evaluated, as
normal.

The condition handler may be :ref:`restart-condition-handler
<restart-condition-handler>` in which case the current top-level
expression is discarded and its continuation is run.

The condition handler may be :ref:`reset-condition-handler
<reset-condition-handler>` in which case the :program:`idio` process
will attempt to exit.
