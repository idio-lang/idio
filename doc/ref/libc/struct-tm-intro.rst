struct tm
^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
tm``:

.. code-block:: c

   struct tm
   {
     int                  tm_sec;
     int                  tm_min;
     int                  tm_hour;
     int                  tm_mday;
     int                  tm_mon;
     int                  tm_year;
     int                  tm_wday;
     int                  tm_yday;
     int                  tm_isdst;
     long int             tm_gmtoff;
     char*                tm_zone;
   };

using the structure member names ``tm_sec``, ``tm_min`` etc..

Note that SunOS does not define ``tm_gmtoff`` or ``tm_zone``.

See also :ref:`asctime <libc/asctime>`, :ref:`localtime
<libc/localtime>`, :ref:`gmtime <libc/gmtime>`, :ref:`mktime
<libc/mktime>`, :ref:`strftime <libc/strftime>`, :ref:`strptime
<libc/strptime>`.

