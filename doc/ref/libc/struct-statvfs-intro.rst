.. _`libc/struct-statvfs`:

struct statvfs
^^^^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
statvfs``:

.. code-block:: c

   struct statvfs
   {
       unsigned long  f_bsize;
       unsigned long  f_frsize;
       fsblkcnt_t     f_blocks;
       fsblkcnt_t     f_bfree;
       fsblkcnt_t     f_bavail;
       fsfilcnt_t     f_files;
       fsfilcnt_t     f_ffree;
       fsfilcnt_t     f_favail;
       unsigned long  f_fsid;
       unsigned long  f_flag;
       unsigned long  f_namemax;
   };

using the structure member names ``f_bsize``, ``f_frsize`` etc..

See also :ref:`statvfs <libc/statvfs>` and :ref:`fstatvfs
<libc/fstatvfs>`.
