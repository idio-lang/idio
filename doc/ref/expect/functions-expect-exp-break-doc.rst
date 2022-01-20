``exp-break`` returns `value` from the enclosing :ref:`exp-case
<expect/exp-case>` or ``#<void>`` if no `value` is supplied.

.. warning::

   ``exp-break`` ignores any protection blocks set up by
   :ref:`unwind-protect <unwind-protect>` or :ref:`dynamic-wind
   <dynamic-wind>`.
