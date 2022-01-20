``exp-continue`` stops processing the current :ref:`exp-case
<expect/exp-case>` `clauses` and starts the next iteration of the
loop.  `value` is ignored and set to ``#<void>`` if not supplied.

.. warning::
   
   ``exp-continue`` ignores any protection blocks set up by
   :ref:`unwind-protect <unwind-protect>` or :ref:`dynamic-wind
   <dynamic-wind>`.
