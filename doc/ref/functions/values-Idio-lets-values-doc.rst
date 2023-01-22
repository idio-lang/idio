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

``let*-values`` is similar to :ref:`let-values <let-values>` except
the inner bindings are evaluated in the context of the outer bindings.

:Example:

.. code-block:: idio

   (let*-values
    (((a b) (values 1 2))
     ((c d) (values (a + b) (a - b)))) {
     printf "c=%s d=%s\n" c d			; c=3 d=-1
    })
