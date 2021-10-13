evaluate `pre-thunk` and then evaluate `value-thunk` then evaluate
`post-thunk` irrespective of how `value-thunk` completed

:param pre-thunk: thunk
:type pre-thunk: function
:param value-thunk: thunk
:type value-thunk: function
:param post-thunk: thunk
:type post-thunk: function
:return: the result of evaluating `value-thunk`
:rtype: any

This is derived from ports of :ref-author:`Oleg Kiselyov`'s
`delim-control-n.scm
<http://okmij.org/ftp/Scheme/delim-control-n.scm>`_ and `dynamic-wind
implementation
<http://okmij.org/ftp/continuations/implementations.html#dynamic-wind>`_.

This implementation might not be complete or robust.
