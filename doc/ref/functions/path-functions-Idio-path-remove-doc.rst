A wrapper to :ref:`path-modify <path-modify>`'s ``remove`` function.

:param var: the name of the path to be manipulated, eg. PATH, CLASSPATH
:type var: symbol
:param old: the path element to be removed
:type old: string

See :ref:`path-modify <path-modify>` for the keyword arguments.

:Example:

Remove ``"/usr/local/bin"`` from :envvar:`PATH`:

.. code-block:: idio

   path-remove 'PATH "/usr/local/bin"

