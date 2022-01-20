:type: :ref:`expect/struct-spawn <expect/struct-spawn>`

``user-spawn-id`` is a spawn-id representing the user, more
particularly ``libc/STDIN_FILENO``.

.. warning::

   Calling :ref:`exp-close <expect/exp-close>` or :ref:`exp-wait
   <expect/exp-wait>` with ``user-spawn-id`` may have undesired
   consequences.
