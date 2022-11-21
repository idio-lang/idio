define a new class, `name`, with optional super-classes and slots

:param name: name of the class
:type name: symbol
:param supers: super-classes of the new class, defaults to ``#n``
:type supers: list of symbols, a symbol or ``#n``, optional
:param slots: slots of the new class, defaults to ``#n``
:type slots: see below, optional
:return: class
:rtype: instance

Additionally, the symbol `name` will be bound to the new class.

`slots` can each be a simple symbol, the slot's name, or a list of a
symbol, the slot's name, and some slot options.

If a slot is declared as:

* a simple symbol then it has no slot options

* a list then, in addition to any slot options given, a slot option of
  ``:initarg`` with a keyword value of :samp:`:{slot-name}` is added
  if no ``:initarg`` is otherwise given

Slot options include:

* :samp:`:initarg {keyword}`

  Subsequently, :samp:`{keyword} {arg}` can be passed to
  :ref:`make-instance <object/make-instance>` to set the slot's value
  to :samp:`{arg}`

* :samp:`:initform {func}`

  Here, if no ``:initarg`` overrides then :samp:`{func}` is applied to
  the `initargs` passed to :ref:`make-instance <object/make-instance>`
  to get the default slot value instead of a "default slot value"
  function which returns ``#f``.

  :samp:`{func}` should expect to be passed any number of arguments.

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
