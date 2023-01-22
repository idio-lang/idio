invoke `body` in the context of the *multiple values* bindings

:param mv-bindings: *multiple values* bindings, see below
:type mv-bindings: list
:param body: body form
:type body: expressions
:return: return value of `body`
:rtype: any

`mv-bindings` takes the form :samp:`(({formals} {init}) ...)` where
:samp:`{init}` is an expression returning *multiple values* and
:samp:`{formals}` is a list of variable names.

:Example:

.. code-block:: idio

   (let-values
    (((a b) (values 1 2))
     ((c d) (values 3 4))) {
      printf "a + d = %s\n" (a + d)	; a + d = 5
    })

Here, the `body` function is called with the variable names ``a``,
``b``, ``c`` and ``d`` bound to values ``1``, ``2``, ``3`` and ``4``
respectively.

:samp:`{formals}` can be an improper list:

.. code-block:: idio

   (let-values
    (((a b & c) (values 1 2 3 4))) {
      printf "c is %s\n" c				; c is (3 4)
      printf "a + (pht c) = %s\n" (a + (pht c))	; a + (pht c) = 5
    })
