Invocation Values
^^^^^^^^^^^^^^^^^

When :program:`idio` is invoked it treats its arguments as:

.. parsed-literal::

   .../idio [*Idio-args*] [*script-name* [*script-args*]]

where arguments to :program:`idio` must come before any script name or
arguments to the script as, both in essence and in practice, the first
argument that isn't recognised as an :lname:`Idio` argument will be
treated as the name of a script.  ``--hlep`` beware!

