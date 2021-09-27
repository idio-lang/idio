apply the greater-than-or-equal comparator to strings

:param s1: string
:type s1: string
:param s2: string
:type s2: string
:return: the result of comparing the arguments
:rtype: boolean

``string>=?`` with more than one argument (a minimum of two) has each
subsequent argument compared to the one to its left.  The result is
``#t`` if all subsequent arguments are greater-than-or-equal to the
argument to their left otherwise the result is ``#f``.

``string>=?`` converts the :lname:`Idio` string to a UTF-8
representation in a :lname:`C` string then uses :manpage:`strncmp(3)`
to compare using the shorter length string.

If the strings are considered equal then the shorter string is
considered less than the longer string.
