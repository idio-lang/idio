apply `f` to the collected n\ :sup:`th` elements of each list in
`lists`.  `f` should accept as many arguments as there are lists and
each list in `lists` should be the same length.

:param f: function to be applied
:type f: function
:param lists: lists, arrays or strings
:type lists: list
:return: list of the results from applying `f`
:rtype: list

``map*`` is a variant of :ref:`map <map>` that handles improper lists.
