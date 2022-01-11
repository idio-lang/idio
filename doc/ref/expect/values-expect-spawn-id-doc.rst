:type: :ref:`expect/struct-spawn <expect/struct-spawn>`

``spawn-id`` is set by and returned from :ref:`expect/spawn
<expect/spawn>`.

.. tip::

   ``spawn-id`` defaults to ``#f`` however if you set it to ``#n`` or
   a list of spawn-id(s) then ``spawn`` will prepend any new value to
   the list.

   If you want to revert this behaviour, set ``spawn-id`` back to a
   single ``struct-spawn`` value or to ``#f``.
