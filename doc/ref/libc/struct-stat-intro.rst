.. _`libc/struct-stat`:

struct stat
^^^^^^^^^^^

:lname:`Idio` supports tagged references to :lname:`C` ``struct
stat``:

.. code-block:: c

   struct stat
   {
     dev_t                st_dev;
     ino_t                st_ino;
     nlink_t              st_nlink;
     mode_t               st_mode;
     uid_t                st_uid;
     gid_t                st_gid;
     dev_t                st_rdev;
     off_t                st_size;
     blksize_t            st_blksize;
     blkcnt_t             st_blocks;
     struct timespec      st_atim;
     struct timespec      st_mtim;
     struct timespec      st_ctim;
   };

using the structure member names ``st_dev``, ``st_ino`` etc..

In addition the pseudo-member names ``st_atime``, ``st_mtime`` and
``st_ctime`` will access the ``tv_sec`` member of the corresponding
:ref:`struct-timespec <libc/struct-timespec>` members to maintain the
legacy interface.

See also :ref:`stat <libc/stat>` and :ref:`fstat <libc/fstat>`.

