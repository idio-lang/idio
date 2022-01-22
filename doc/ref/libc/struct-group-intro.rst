.. _`libc/struct-group`:

struct group
^^^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
group``:

.. code-block:: c

   struct group
   {
       char   *gr_name;
       char   *gr_passwd;
       gid_t   gr_gid;
       char  **gr_mem;
   };

See also :ref:`getgrgid <libc/getgrgid>` and :ref:`getgrnam
<libc/getgrnam>`.

