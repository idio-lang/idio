:var:`RANDOM` is a computed value returning a random non-negative
32-bit integer.

Setting :var:`RANDOM` re-seeds the random number generator.

:rtype: integer (irrespective of the type mentioned above)

.. note::

   :var:`RANDOM` is based on :manpage:`random(3)` with attendant risks
   for use in scenarios requiring high quality randomness.
