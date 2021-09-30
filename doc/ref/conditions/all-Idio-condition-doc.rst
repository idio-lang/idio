:ref:`allocate-condition <allocate-condition>` an instance of
condition `type` and :ref:`condition-set! <condition-set!>` the
supplied `field-bindings`

:param type: condition type name
:type type: symbol
:param field-bindings: field data, defaults to ``#n``
:type field-bindings: list, optional

Each field in `field-bindings` should be a tuple of the form
:samp:`({field-name} {value})`.

``condition`` is only used in testing.
