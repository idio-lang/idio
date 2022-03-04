define a new generic function, `name`, with optional documentation

:param name: name of the generic function
:type name: symbol
:param doc: documentation string, defaults to ``#n``
:type doc: string or ``#n``, optional
:return: generic function
:rtype: instance

Additionally, the symbol `name` will be bound to the new generic function.

``define-generic`` would not normally be called by users as generic
functions are implicitly created through the use of
:ref:`define-method <object/define-method>`.

.. seealso:: :ref:`define-method <object/define-method>`
