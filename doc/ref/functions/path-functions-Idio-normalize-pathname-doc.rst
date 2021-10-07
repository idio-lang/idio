Normalize pathname `val` by:

* non-absolute pathnames are made absolute

* removing ``.`` and ``..`` elements as appropriate

:param val: the pathname to be normalized
:type val: string

keyword arguments:

:param \:sep: the element separator, defaults to ``/``
:type \:sep: string, optional

