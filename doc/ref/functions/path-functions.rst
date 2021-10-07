Path Manipulation
^^^^^^^^^^^^^^^^^

The following are simple functions to manipulate common PATH-like
strings.

.. warning::

   Throughout these examples, `var` is a *symbol* and so, in all
   probability, you will need to quote the name:

   .. code-block:: idio

      path-append 'PATH "/usr/local/bin"
