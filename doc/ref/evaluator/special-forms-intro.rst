.. _`special forms`:

Special Forms
-------------



Sequence Forms
^^^^^^^^^^^^^^

There are three sequence special forms: ``begin``, ``and`` and ``or``.
They all take a sequence of expressions and start to evaluate them,
one by one.  They differ in how many of the expressions they evaluate,
what they return and what they return if no expressions are passed.

.. code-block:: idio

   begin ...
   and ...
   or ...

.. _`begin`:

begin
"""""

``begin`` evaluates all of the expressions and returns the value of
the last expression.

This is the normal block of code behaviour.

If no expressions are passed, ``begin`` returns ``#<void>``.

and
"""

``and`` evaluates all of the expressions unless an expression returns
``#f`` at which point ``and`` also returns ``#f``.  The remaining
expressions are not evaluated.

If all of the expression return a `true` value then ``and`` returns
the value of the last expression.

If no expressions are passed, ``and`` returns ``#t``.

or
""

``or`` evaluates all of the expressions until an expression returns a
`true` value and ``or`` returns that value.  The remaining expressions
are not evaluated.

If all of the expressions return ``#f`` then ``or`` returns ``#f``.

If no expressions are passed, ``or`` returns ``#f``.

escape
^^^^^^

The ``escape`` special form is used to prevent the reader implementing
operators.  The default reader escape character is ``\``.

.. code-block:: idio

   escape e
   \e

``escape`` continues evaluation with its argument.

quote
^^^^^

The ``quote`` special form is used to prevent the evaluator evaluating
its argument.  The default reader quote character is ``'``.

.. code-block:: idio

   quote e
   'e

``quote`` returns its argument unevaluated.

quasiquote
^^^^^^^^^^

The ``quasiquote`` special form is the basis for expanding
:ref:`templates <templates>`.  

.. code-block:: idio

   #T{ ... }

.. _`function`:

function
^^^^^^^^

The ``function`` special form is used to return a function value,
commonly called a closure.

.. parsed-literal::

   function *formals* *body*

`formals` declares the parameters for the closure and arguments passed
to the function are available through the named parameters within the
`body` of the function.

`formals` takes several forms itself:

* ``#n`` for no arguments

  This is often referred to as a *thunk*.

* a symbol which indicates that all arguments are to be bundled up as
  a list and made accessible within the function through the symbol

* a list of one or more positional parameters and an optional varargs
  parameter separated by an ``&`` character

  It is an error to not pass enough arguments to satisfy the number of
  positional parameters.

  It is an error to pass more arguments than the number of positional
  parameters when there is no varargs parameter.

  * :samp:`(a b)` would suggest two positional parameters and no
    varargs parameter

  * :samp:`(a & b)` would suggest one formal parameter and zero or
    more other parameters bundled up into a list and made available
    through the symbol ``b``.

`body` is a single expression although commonly a block is used as a
synonym for the :ref:`begin <begin>` sequence special form.

function+
^^^^^^^^^

The ``function+`` special form is used to extend the current
function's argument frame.  It appears when a function's body is
rewritten to a normal form.

Users are not expected to use this special form.

.. _`if`:

if
^^

The ``if`` special form is the fundamental test and branch mechanism.

.. parsed-literal::

   if *condition* *consequent* *alternative*

If the expression `condition` evaluates to `true` then evaluate
`consequent` otherwise evaluate `alternative`.

Whichever of `consequent` or `alternative` is evaluated, its result is
the value returned by ``if``.

If `condition` evaluates to ``#f`` and there is no `alternative` then
``if`` returns ``#<void>``.

cond
^^^^

The ``cond`` special form is an abstraction of the :ref:`if <if>`
Special Form.  It is equivalent to many languages' ``if ... elif
... elif ... else ...``.

.. parsed-literal::

   cond *clauses*

`clauses` is a list of clauses where each clause can take the form:

* :samp:`({condition} ...)`

  If the expression `condition` evaluates to `true` then evaluate
  `...` returning its value as the value from ``cond``.

* :samp:`({condition} => {f})`

  If the expression `condition` evaluates to `true` then call the
  function `f` with the result of `condition` and return the result of
  `f` as the value from ``cond``.

  This is the *anaphoric if* expression, roughly equivalent to:

  .. parsed-literal::

     it := *condition*
     if it {
       f it
     }

  where `condition` is evaluated and the result recorded.  If the
  result was `true` then call `f` with the result as an argument.

* :samp:`(else ...)`

  Evaluate `...` returning its value as the value from ``cond``.

  The ``else`` clause can only appear at the end

set!
^^^^

The ``set!`` special form modifies memory, hence the ``!`` in the
name.  It has an ``=`` synonym and an ``=`` reader operator.

.. code-block:: idio

   set! var value
   = var value
   var = value

.. note::

   Technically, ``set!`` doesn't modify any value but changes a
   reference to point to a different value.

   From a user perspective, the variable has a different value.

define-template
^^^^^^^^^^^^^^^

The ``define-template`` special form is used to create :ref:`templates
<templates>`.

define-infix-operator
^^^^^^^^^^^^^^^^^^^^^

The ``define-infix-operator`` special form is used to create reader
:ref:`operators <operators>`.

define-postfix-operator
^^^^^^^^^^^^^^^^^^^^^^^

The ``define-postfix-operator`` special form is used to create reader
:ref:`operators <operators>`.

.. _`define`:

define
^^^^^^

The ``define`` special form is used to create lexically scoped
references between symbols, symbolic names, aka identifiers, and
values.  It has an ``:=`` synonym and a ``:=`` reader operator.

.. code-block:: idio

   define var value
   := var value
   var := value

The base form of ``define`` is :samp:`define {var} {value}` and
creates a reference from `var` to `value`.

A second form is for defining functions: :samp:`define ({name}
{formals}) {body}`.

This is rewritten into the base form as: :samp:`define {name}
(function {formals} {body})` thus creating a reference from `name` to
a function value.

:*
^^

The ``:*`` special form is used to create dynamically scoped
references between symbols, symbolic names, aka identifiers, and
values which will subsequently become environment variables when an
external command is executed.  It has a ``:*`` reader operator.

.. code-block:: idio

   :* var value
   var :* value

environ-let
^^^^^^^^^^^

The ``environ-let`` special form is used to evaluate an expression in
the context of a dynamically scoped environment variable.

.. code-block:: idio

   environ-let (var expr) body

environ-unset
^^^^^^^^^^^^^

The ``environ-unset`` special form is used to evaluate an expression
in the context of the absence of a dynamically scoped environment
variable.

.. code-block:: idio

   environ-unset var body

:~
^^

The ``:~`` special form is used to create dynamically scoped
references between symbols, symbolic names, aka identifiers, and
values.  It has a ``:~`` reader operator.

.. code-block:: idio

   :~ var value
   var :~ value

dynamic
^^^^^^^

The ``dynamic`` special form is used to access dynamically scoped
variables.

There is normally no need to use this as the evaluator should figure
our the variable is referencing a dynamic value and create the code
accordingly.

dynamic-let
^^^^^^^^^^^

The ``dynamic-let`` special form is used to evaluate an expression in
the context of a dynamically scoped variable.

.. code-block:: idio

   dynamic-let (var expr) body

dynamic-unset
^^^^^^^^^^^^^

The ``dynamic-unset`` special form is used to evaluate an expression
in the context of the absence of a dynamically scoped variable.

.. code-block:: idio

   dynamic-unset var body

:$
^^

The ``:$`` special form is used to create lexically scoped references
between symbols, symbolic names, aka identifiers, and computed values.
It has a ``:$`` reader operator.

.. code-block:: idio

   :$ var getter
   :$ var getter setter
   :$ var #n setter
   var :$ getter
   var :$ getter setter
   var :$ #n setter

Here, `getter` and `setter` are functions of no args and one arg,
respectively, which retrieve or set some, usually, volatile value.

The variable :ref:`SECONDS <SECONDS>` returns the number of seconds
the program has been running for.  It has no associated `setter` so
trying to give it a value is an error.

block
^^^^^

The ``block`` special form is largely a synonym for the :ref:`begin
<begin>` sequencing special form but it does create a new lexical
context.

Variables created within a block are no longer accessible outside the
block.

Amongst other things this allows for the creation of privately scoped
variables.

trap
^^^^

The ``trap`` special form is used to set in place a handler for a
condition type or types for the evaluation of some body.

.. parsed-literal::

   trap *condition* *handler* *body*
   trap (*conditions*) *handler* *body*

Here, if a condition is raised during the execution of `body` that is
one of the types in `conditions` or a descendent thereof then
`handler` is run.

`handler` can choose to:

* return a value on behalf of the erroring function by simply returning a value

* can raise the condition to a previously established handler

include
^^^^^^^

The ``include`` special form is used by the evaluator to pause,
:ref:`load <load>` another file and then resume processing the current
file.

.. parsed-literal::

   include *filename*
   
