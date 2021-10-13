apply `f` to the collected n\ :sup:`th` elements of each list in
`lists` together with a cumulative result value which is initialised
to `init`.

`f` should accept one more argument than there are lists and each list
in `lists` should be the same length.

The first time `f` is called the first argument is `init`.  The
subsequent times `f` is called the first argument is the return value
from the previous call to `f`.

:param f: function to be applied
:type f: function
:param init: initial value of the cumulative result
:type init: any
:param lists: list of lists
:type lists: list
:return: cumulative result
:rtype: any

.. seealso:: :ref:`fold-right <fold-right>` which does the same but
             processes `lists` right to left

.. warning::

   There is a :lname:`Scheme` function, ``foldl`` which accepts the
   cumulative result as the last argument.
