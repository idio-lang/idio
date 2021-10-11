Globbing
^^^^^^^^

The variants revolving around an instance of the :ref:`~path <~path>`
struct type were meant to delay the expansion of the
:manpage:`glob(3)` expression until it was needed by an external
command.

That form isn't very well thought through and relies on some
side-effects in the VM.

In the meanwhile, use plain old :ref:`glob <glob>`.

