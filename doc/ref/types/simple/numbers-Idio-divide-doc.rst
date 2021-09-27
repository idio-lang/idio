divide numbers

:param n1: number
:type n1: number
:param n2: number
:type n2: number
:return: the result of dividing the arguments
:rtype: number

All arguments are converted to bignums before calculating the result.

An attempt is made to convert the result to a fixnum if possible.

``/`` with one argument is equivalent to :samp:`1 / {n1}`.

``/`` with more than one argument is equivalent to :samp:`r = {n1}`
then repeating across the remaining arguments with :samp:`r = {r} /
{n2}`, :samp:`r = {r} / {n3}`, etc..


