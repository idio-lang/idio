struct rusage
-------------

:lname:`Idio` supports tagged references to :lname:`C` ``struct
rusage``:

.. code-block:: c

   struct rusage
   {
     struct timeval       ru_utime;
     struct timeval       ru_stime;
   };

using the structure member names ``ru_utime`` and ``ru_stime``.  The
other member names are not accessible due to portability issues.

See also :ref:`getrusage <libc/getrusage>`.

