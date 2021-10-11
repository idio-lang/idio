generate default predicate and accessor names for
:ref:`define-condition-type/accessors
<define-condition-type/accessors>`

:param name: condition name
:type name: symbol
:param parent: parent type
:type parent: condition type
:param fields: field data, defaults to ``#n``
:type fields: list, optional

Each field in `fields` should be a tuple of the form
:samp:`({field-name} {accessor-name})`.

Normally :samp:`{accessor-name}` would be
:samp:`{condition-name}-{field-name}`.
