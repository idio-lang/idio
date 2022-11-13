:var:`IDIO_BUILD_ASM_COMMIT` is a reference to the *git* commit of the
:file:`src/compile.c` file at the time of build.

For developers, the commit identifier might be suffixed with:

* ``*`` if there were unstaged changes in the working tree at the time
  of the build

* ``+`` if there were uncommitted changes in the index at the time of
  the build
