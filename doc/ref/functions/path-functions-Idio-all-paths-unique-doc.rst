Call :samp:`path-{act}` with `:once` set to `true` for

* :envvar:`PATH` with :file:`{val}/bin`

* :envvar:`LD_LIBRARY_PATH` with :file:`{val}/lib`

* :envvar:`MANPATH` with :file:`{val}/share/man` or :file:`{val}/man`
  if either exists

:param act: an `act` for :ref:`path-modify <path-modify>`
:type act: symbol
:param val: the top of the standard hierarchy tree
:type val: string

See :ref:`path-modify <path-modify>` for the keyword arguments.

:Example:

Append ``"/usr/local/bin"`` to :envvar:`PATH`, ``"/usr/local/lib"`` to
:envvar:`LI_LIBRARY_PATH` and one of ``"/usr/local/share/bin"`` or
``"/usr/local/man"`` (if they exist) to :envvar:`MANPATH`:

.. code-block:: idio

   all-paths 'append "/usr/local"

