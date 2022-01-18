``continue`` stops processing the current :ref:`C/for <C/for>` or
:ref:`while <while>` `body` and starts the next iteration of the loop.
`value` is ignored.

.. warning::
   
   ``continue`` ignores any protection blocks set up by
   :ref:`unwind-protect <unwind-protect>` or :ref:`dynamic-wind
   <dynamic-wind>`.
