define a named function with optional and keyword arguments

The `formals` for ``function*`` are interpreted as the (SRFI-89_)
grammar :token:`~SRFI-89:extended-formals`:

.. productionlist:: SRFI-89
   extended_formals : variable | ( `extended_def_formals` )
   extended_def_formals :   `positional_section` `named_section`? `rest_section`
			: | `named_section`? `positional_section` `rest_section`
   positional_section : `required_positional`* `optional_positional`*
   required_positional : variable
   optional_positional : "(" variable expression ")"
   named_section : `named`+
   named :   `required_named`
         : | `optional_named`
   required_named : "(" keyword variable ")"
   optional_named : "(" keyword variable expression ")"
   rest_section : "&" variable
                : | empty



:Example:

Define a function `f` which takes one normal positional parameter,
`a`, and an optional positional parameter, `b`, which defaults to
``#f``:

.. code-block:: idio

   define* (f a (b #f)) {
     list a b
   }

   f 1                  ; '(1 #f)
   f 1 2                ; '(1 2)

A :token:`keyword` does not need to be the same word as the
:token:`variable` it sets so here we define a function `g` which takes
a normal positional parameter, `a`, and an optional keyword parameter,
``:mykey``, which is accessed in the function as `b` and defaults to
``#f``:

:Example:

.. code-block:: idio

   define* (g a (:mykey b #f)) {
     list a b
   }

   f 1                  ; '(1 #f)
   f 1 :mykey 2         ; '(1 2)

Unlike regular ``function`` definitions,
:token:`~SRFI-89:optional_positional` and :token:`~SRFI-89:named`
parameters can default to expressions involving previous parameters.

Here, the optional positional parameter `b` defaults to :samp:`{a} *
2` and the optional keyword parameter, ``:mykey``, accessed in the
function as `c` defaults to :samp:`{a} * {b}`:

:Example:

.. code-block:: idio

   define* (h a (b (a * 2)) (:mykey c (a * b))) {
     list a b c
   }

   h 2                  ; '(2 4 8)
   h 2 3                ; '(2 3 6)
   h 2 3 :mykey 4       ; '(2 3 4)
