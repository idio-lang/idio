Operator Constructors
---------------------

.. _`define-infix-operator`:

define-infix-operator
^^^^^^^^^^^^^^^^^^^^^

.. idio:template:: define-infix-operator name priority body

   define `name` as an infix operator with `priority` and template
   body `body`

   To allow like operators to re-use the same code, `body` may be the
   name of a previously defined operator which requires that when the
   operator function is invoked the actual operator used needs to be
   passed as a parameter.

   An actual template function definition is derived something like:

       :samp:`define ({name} op before after) {body}`

   Hence the template function definition is named after the original
   definition but is passed the operator re-using the template as
   `op`.  `before` and `after` are the expressions the reader saw
   before and after the operator although the operator is free to
   re-arrange the entire expression as it sees fit.

.. _`define-postfix-operator`:

define-postfix-operator
^^^^^^^^^^^^^^^^^^^^^^^

.. idio:template:: define-postfix-operator name priority body

   See :ref:`define-infix-operator <define-infix-operator>`.
