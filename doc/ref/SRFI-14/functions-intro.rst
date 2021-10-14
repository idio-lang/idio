.. idio:currentmodule:: SRFI-14

char-set Functions
------------------

:lname:`Idio` uses a "sparse char-set" implementation for char-sets
which is essentially an array of bitsets, one per Unicode plane which
can be ``#f`` if the plane is unused.

