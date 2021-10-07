A wrapper to :ref:`path-modify <path-modify>`'s ``append`` function.

:param var: the name of the path to be manipulated, eg. PATH, CLASSPATH
:type var: symbol
:param val: the path element(s) to be added, `sep` separated
:type val: string

See :ref:`path-modify <path-modify>` for the keyword arguments.

:Example:

Append ``"/usr/local/bin"`` to :envvar:`PATH` if it is a directory:

.. code-block:: idio

   path-append 'PATH "/usr/local/bin" :test d?

