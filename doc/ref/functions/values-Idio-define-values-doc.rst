define multiple new variables from the *multiple values* returned from
`expr`

:param formals: variable names
:type formals: list
:param expr: expression returning *multiple values*
:type expr: expression
:rtype: any

:Example:

.. code-block:: idio

   define-values (a b & c) (values 1 2 3 4)

   printf "a + (pht c) = %s\n" (a + (pht c)) ; a + (pht c) = 5
