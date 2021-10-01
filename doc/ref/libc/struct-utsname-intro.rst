.. _`libc/struct-utsname`:

struct utsname
^^^^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
utsname``:

.. code-block:: c

   struct utsname
   {
     char[]               sysname;
     char[]               nodename;
     char[]               release;
     char[]               version;
     char[]               machine;
     char[]               domainname;
   };

using the structure member names ``sysname``, ``nodename`` etc..

Note that ``domainname`` is only available on Linux.

See also :ref:`uname <libc/uname>` and :ref:`idio-uname
<libc/idio-uname>`.

