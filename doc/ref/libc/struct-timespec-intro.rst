.. _`libc/struct-timespec`:

struct timespec
^^^^^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
timespec``:

.. code-block:: c

   struct timespec
   {
     time_t               tv_sec;
     long		  tv_nsec;
   };

using the structure member names ``tv_sec`` and ``tv_nsec``.

Note that ``tv_nsec`` is defined as a ``long`` due to portability
issues.


