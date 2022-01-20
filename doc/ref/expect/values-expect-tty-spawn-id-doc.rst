:type: :ref:`expect/struct-spawn <expect/struct-spawn>`

``tty-spawn-id`` is a spawn-id representing :file:`/dev/tty`.

If :file:`/dev/tty` is not available, ``tty-spawn-id`` is ``#f``.

.. warning::

   Calling :ref:`exp-close <expect/exp-close>` or :ref:`exp-wait
   <expect/exp-wait>` with ``tty-spawn-id`` may have undesired
   consequences.
