.. _`libc/struct-rusage`:

struct rusage
^^^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
rusage``:

.. code-block:: c

   struct rusage
   {
     struct timeval       ru_utime;
     struct timeval       ru_stime;
   };

using the structure member names ``ru_utime`` and ``ru_stime`` for
:ref:`struct-timeval <libc/struct-timeval>` structures.  The other
member names are not accessible due to portability concerns.

See also :ref:`getrusage <libc/getrusage>`.

