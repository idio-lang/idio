Associate `name` with a syntax transformer using `pattern` and
`expansion`.

This is a one-shot syntax transformer.

In effect,

    :samp:`(define-syntax-rule ({name} & {pattern}) {expansion})`

is re-written as

    :samp:`(define-syntax {name} (syntax-rules () (({name} & {pattern}) {expansion})))`

:Example:

The definitions of :ref:`when <when>` and :ref:`unless <unless>`
demonstrate the use of ellipses where the expression is saying that
`body` could be multiple expressions, all of which are to be gathered
up and put inside a :ref:`begin <begin special form>` form.

.. code-block:: idio

   define-syntax-rule (when test body ...) (
     if test (begin body ...)
   )

   define-syntax-rule (unless test body ...) (
     if (not test) (begin body ...)
   )
