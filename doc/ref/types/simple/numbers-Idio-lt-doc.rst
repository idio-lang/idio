apply the less-than comparator to numbers

:param n1: number
:type n1: number
:param n2: number
:type n2: number
:return: the result of comparing the arguments
:rtype: boolean

If any arguments are bignums then every argument is converted to a
bignum before calculating the result.

``lt`` with one argument is ``#t``.

``lt`` with more than one argument has each subsequent argument
compared to the one to its left.  The result is ``#t`` if all
subsequent arguments are less-than the argument to their left
otherwise the result is ``#f``.
