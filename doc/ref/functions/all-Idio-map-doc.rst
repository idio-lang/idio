apply `f` to the collected n\ :sup:`th` elements of each list in
`lists`.  `f` should accept as many arguments as there are lists and
each list in `lists` should be the same length.

:param f: function to be applied
:type f: function
:param lists: lists, arrays or strings
:type lists: list
:return: list of the results from applying `f`
:rtype: list

:Example:

Multiply a list of numbers by 10:

.. code-block:: idio

   map (function (n) n * 10) '(1 2 3)                   ; '(10 20 30)

Add two lists of numbers:

.. code-block:: idio

   map (function (n1 n2) n1 + n2) '(1 2 3) '(4 5 6)     ; '(5 7 9)

Combine the elements of arrays (:ref:`list <ref:list>`, of course,
takes any number of arguments):

.. code-block:: idio

   map list #[1 2 3] #[4 5 6] #[7 8 9]	; '((1 4 7) (2 5 8) (3 6 9))

.. seealso:: :ref:`for-each <for-each>` which does the same but
             doesn't return a list
