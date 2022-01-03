C Pointer Reflection
^^^^^^^^^^^^^^^^^^^^

:lname:`C` pointers can be tagged with some C Structure Identification
(CSI) information.  The purpose is multi-fold where, using
:ref:`libc/struct-stat <libc/struct-stat>` as an example:

* we can store a nominal structure name, eg. ``libc/struct-stat``

  The name is descriptive only.

* we can store a list of the structure members,
  eg. ``libc/struct-stat``'s:

  ``(st_dev st_ino st_nlink st_mode st_uid st_gid st_rdev st_size st_blksize st_blocks st_atim st_mtim st_ctim st_atime st_mtime st_ctime)``

  .. note::

     The modern :lname:`C` ``struct stat`` does not have ``st_atime``
     etc. members *per se* but those are ``#define``'d to the ``time_t
     tv_sec`` components of the corresponding ``st_atim``'s ``struct
     timespec`` thus maintaining backwards compatibility.

     :lname:`Idio` supports accessing both forms.

* we can associate a getter to manipulate the ``C/pointer`` given a
  member name, here, :ref:`libc/struct-stat-ref
  <libc/struct-stat-ref>`

  We can subsequently associate a setter with the getter -- albeit
  there is (deliberately) no setter for a ``libc/struct-stat``.

* we can associate a function to print the ``C/pointer`` in a more
  aesthetically pleasing manner.  For example, the ``struct timespec``
  is stylised as a floating point number.

  Here, we associate :ref:`libc/struct-stat-as-string
  <libc/struct-stat-as-string>`.

