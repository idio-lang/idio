struct rlimit
^^^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
rlimit``:

.. code-block:: c

   struct rlimit
   {
     rlim_t               rlim_cur;
     rlim_t               rlim_max;
   };

using the structure member names ``rlim_cur`` and ``rlim_max``.

See also :ref:`getrlimit <libc/getrlimit>` and :ref:`setrlimit
<libc/setrlimit>`.

