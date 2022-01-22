.. _`libc/struct-passwd`:

struct passwd
^^^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
passwd``:

.. code-block:: c

   struct passwd
   {
       char   *pw_name;
       char   *pw_passwd;
       uid_t   pw_uid;
       gid_t   pw_gid;
       char   *pw_gecos;
       char   *pw_dir;
       char   *pw_shell;
   };

See also :ref:`getpwuid <libc/getpwuid>` and :ref:`getpwnam
<libc/getpwnam>`.

