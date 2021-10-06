:var:`IDIOLIB` is the search path used by :program:`idio` to find
libraries and extension code.

The construction of :var:`IDIOLIB` is complicated but there are two
things to note:

#. :program:`idio` will determine the :file:`lib` directory that
   corresponds with the actual executable being run.  This directory
   will be *prepended* to any user-supplied :envvar:`IDIOLIB`
   environment variable.

#. if you used a *virtualenv*-style setup then :program:`idio` will
   determine the :file:`lib` directory that corresponds with the
   virtualenv being used.  This directory will be *prepended* to any
   user-supplied :envvar:`IDIOLIB` environment variable.
