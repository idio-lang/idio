.. _`symbol type`:

Symbol Type
===========

Symbols are usually what you think of in other languages as
identifiers, the references to values.  In :lname:`Idio` they can also
be used first class values in their own right, often as flags.

If a symbol is bound to a value, ie. it is being used as an
identifier, then the evaluator will evaluate the symbol to the value.
To prevent the evaluator performing that function you need to
:ref:`quote <quote special form>` the symbol.
