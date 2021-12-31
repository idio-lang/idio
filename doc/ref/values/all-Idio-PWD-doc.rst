
:var:`PWD` is the Process Working Directory.

It assumes the value in the environment at startup (or is set to the
result of :ref:`libc/getcwd <libc/getcwd>`).  It is subsequently
updated by calls to :ref:`cd <cd>` and :ref:`pushd <pushd>` /
:ref:`popd <popd>`.

.. warning::

   Calls to :ref:`libc/chdir <libc/chdir>` **do not** change `PWD`.

