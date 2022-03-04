define a new class, `name`, with optional super-classes and slots

:param name: name of the class
:type name: symbol
:param supers: super-classes of the new class, defaults to ``#n``
:type supers: list of symbols, a symbol or ``#n``, optional
:param slots: slots of the new class, defaults to ``#n``
:type slots: symbols, optional
:return: class
:rtype: instance

Additionally, the symbol `name` will be bound to the new class.

:Example:

.. code-block:: idio-console

   Idio> define-class A
   #<CLASS A>
   Idio> define-class B A
   #<CLASS B>
   Idio> define-class C (B)
   #<CLASS C>
   Idio> define-class D #n a b c
   #<CLASS D>
