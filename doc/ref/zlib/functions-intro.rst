.. idio:currentmodule:: zlib

zlib Functions
--------------

In the following functions `windowBits` is tri-state and multi-valued.
For the ``deflate`` and ``inflate`` functions you can use the full
variance:

* if `windowBits` is between 9 and 15, inclusive, a *zlib* header and
  trailer will be added/expected

  * further, if `windowBits` has 16 added to it, then add/expect a
    *gzip* header and trailer

* if `windowBits` is between -15 and -9, inclusive, a *raw* deflate
  stream will be created/expected for programs that want to use the
  raw stream (eg. :program:`zip`)

* for ``inflate``, if `windowBits` is 0 (zero), then the window size
  in the header will be used

The ``gzip-*`` and ``zlib-*`` functions enforce *gzip* and *zlib*
header and trailer respectively meaning:

* for ``gzip-compress`` and ``gzip-decompress``, `windowBits` defaults
  to 15 and is range limited from 9 to 15 inclusive and the 16 for a
  *gzip* header is added implicitly

  For ``gzip-decompress`` a `windowBits` of 0 (use the window size in
  the header) is still possible.

* for ``zlib-compress`` and ``zlib-decompress``, `windowBits` defaults
  to 15 and is range limited from 9 to 15 inclusive

  For ``zlib-decompress`` a `windowBits` of 0 (use the window size in
  the header) is still possible.
