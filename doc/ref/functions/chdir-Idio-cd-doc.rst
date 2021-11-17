change the current working directory to `dir`

:param dir: the directory to change to, defaults to :envvar:`HOME`
:type dir: string
:return: current working directory or ``#f``

If `dir` does not exist a message is displayed and ``#f`` is returned.

If `dir` is not supplied or is the symbol ``~``, :envvar:`HOME` is
used.

.. seealso:: ``cd`` calls :ref:`setd <setd>`.
