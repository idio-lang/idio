define a structure instance constructor, predicate and accessors for
structure fields

:param name: structure name
:type name: symbol
:param fields: list of field names, defaults to ``#n``
:type fields: list of symbols, optional

.. note::

   Using this form:

   * the predicate will be :samp:`{name}?`

   * the constructor will be :samp:`make-{name}`

   * the accessors will be

     * :samp:`{name}-{field}`

     * :samp:`set-{name}-{field}!`
