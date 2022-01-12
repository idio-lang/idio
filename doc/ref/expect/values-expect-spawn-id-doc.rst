:type: :ref:`expect/struct-spawn <expect/struct-spawn>`

``spawn-id`` is set by and returned from :ref:`expect/spawn
<expect/spawn>`.

.. tip::

   ``spawn-id`` defaults to ``#f`` however if you set it to ``#n`` or
   a list of spawn-id(s) then ``spawn`` will prepend any new value to
   the list.

   If you want to revert this behaviour, set ``spawn-id`` back to a
   single ``struct-spawn`` value or to ``#f``.

   You must manage ``spawn-id`` under these circumstances.  For
   example, :ref:`exp-wait <expect/exp-wait>` does not modify a
   list-variant ``spawn-id`` and it will continue as a list of
   (expired) spawned processes.
