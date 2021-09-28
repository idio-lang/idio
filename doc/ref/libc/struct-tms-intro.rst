struct tms
----------

:lname:`Idio` supports tagged references to :lname:`C` ``struct
tms``:

.. code-block:: c

   struct tms
   {
     clock_t              tms_utime;
     clock_t              tms_stime;
     clock_t              tms_cutime;
     clock_t              tms_cstime;
   };

using the structure member names ``tms_utime``, ``tms_stime`` etc..

See also :ref:`times <libc/times>`.

