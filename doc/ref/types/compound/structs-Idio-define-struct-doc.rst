define a structure type called `name` then call
:ref:`define-struct-accessors-only <define-struct-accessors-only>`
with `name` and `fields`

:param name: structure name
:type name: symbol
:param fields: list of field names, defaults to ``#n``
:type fields: list of symbols, optional

.. note::

   Using this form the structure type has no parent type.
