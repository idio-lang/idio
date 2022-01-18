``break`` returns `value` from the enclosing :ref:`C/for <C/for>` or
:ref:`while <while>` loop or ``#<void>`` if no `value` is supplied.

.. warning::

   ``break`` ignores any protection blocks set up by
   :ref:`unwind-protect <unwind-protect>` or :ref:`dynamic-wind
   <dynamic-wind>`.
