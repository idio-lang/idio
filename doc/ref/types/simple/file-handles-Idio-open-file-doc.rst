`mode` can be one of the usual :manpage:`fopen(3)` mode characters but
must start with `r`, `w` or `a`:

.. csv-table::
   :header: flag, :manpage:`open(2)`
   :widths: auto
   :align: left

   ``r``, ``O_RDONLY``
   ``w``, ``O_WRONLY`` | ``O_CREAT`` | ``O_TRUNC``
   ``a``, ``O_WRONLY`` | ``O_CREAT`` | ``O_APPEND``

   ``+``, ``O_RDWR`` in place of ``O_RDONLY`` or ``O_WRONLY``
   ``b``, ignored
   ``e``, ``O_CLOEXEC``
   ``x``, ``O_EXCL``

   
