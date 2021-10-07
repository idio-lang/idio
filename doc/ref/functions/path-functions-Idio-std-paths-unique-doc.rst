Call :samp:`path-{act}` with `:once` set to `true` for

* :envvar:`PATH` with :file:`{val}/bin`

* :envvar:`MANPATH` with :file:`{val}/share/man` or :file:`{val}/man`
  if either exists

:param act: an `act` for :ref:`path-modify <path-modify>`
:type act: symbol
:param val: the top of the standard hierarchy tree
:type val: string

See :ref:`path-modify <path-modify>` for the keyword arguments.

:Example:

Append ``"/usr/local/bin"`` to :envvar:`PATH` and one of
``"/usr/local/share/bin"`` or ``"/usr/local/man"`` (if they exist) to
:envvar:`MANPATH`:

.. code-block:: idio

   std-paths 'append "/usr/local"

