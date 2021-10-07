A wrapper to :ref:`path-modify <path-modify>`'s ``replace`` function.

:param var: the name of the path to be manipulated, eg. PATH, CLASSPATH
:type var: symbol
:param old: the path element to be replaced
:type old: string
:param new: the path element(s) to be substituted, `sep` separated
:type new: string

See :ref:`path-modify <path-modify>` for the keyword arguments.

:Example:

Append ``"/usr/local/bin"`` to :envvar:`PATH` if it is a directory:

.. code-block:: idio

   path-append 'PATH "/usr/local/bin" :test d?

