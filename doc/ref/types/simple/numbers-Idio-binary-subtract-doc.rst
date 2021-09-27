subtract two numbers

:param n1: number
:type n1: number
:param n2: number
:type n2: number
:return: the result of subtracting the arguments
:rtype: number

If any arguments are bignums then every argument is converted to a
bignum before calculating the result.

An attempt is made to convert the result to a fixnum if possible.

The use of the function ``binary--`` is normally the result of the
``-`` infix operator.
