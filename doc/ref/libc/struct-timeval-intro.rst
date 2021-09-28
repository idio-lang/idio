struct timeval
--------------

:lname:`Idio` supports tagged references to :lname:`C` ``struct
timeval``:

.. code-block:: c

   struct timeval
   {
     time_t             tv_sec;
     suseconds_t        tv_usec;
   };

using the structure member names ``tv_sec`` and ``tv_usec``.

Note that on some systems ``suseconds_t`` may be typedef'd to a
``long``.

See also :ref:`gettimeofday <libc/gettimeofday>`.

