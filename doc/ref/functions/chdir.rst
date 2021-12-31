Changing Directory
^^^^^^^^^^^^^^^^^^

:lname:`Idio` supports a very simplistic set of directory changing
functions revolving around a *directory stack*.

The shell-like ``cd`` (with no arguments) cannot be directly
implemented in :lname:`Idio` (because it's a programming language and
``cd`` returns the value of the function :ref:`cd <cd>`) so you are
required to wrap it in parenthesis, which uses :envvar:`HOME`, or pass
``~`` which is a synonym for :envvar:`HOME`.

.. note::

   These functions work with respect to :ref:`PWD <PWD>` in the sense
   that *relative* directory movements modify `PWD` and then
   :ref:`libc/chdir <libc/chdir>` is called on `PWD`.

   For example, on this system :file:`/sbin` is a symlink to
   :file:`/usr/sbin`:

   .. code-block:: idio-console

      Idio> cd "/sbin"
      0
      Idio> (libc/getcwd)
      %P"/usr/sbin"
      Idio> cd ".."
      0
      Idio> (libc/getcwd)
      %P"/"

   Notice that the ``cd ".."`` returns us to :file:`/` and not
   :file:`/usr`, the parent of the directory we were actually in.

   An *absolute* directory movement simply sets `PWD` (and calls
   `libc/chdir`).
