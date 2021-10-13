:Example:

Suppose you want to validate what should be a string argument for a
minimum length:

.. code-block:: idio

   define (frob-string str) {
     (or (string? s)
         (error/type ^rt-parameter-type-error 'frob-string "not a string" s))
     (or ((string-length s) gt 10)
         (error/type ^rt-parameter-value-error 'frob-string "string should be > 10 code points" s))
     ...
   }

   frob-string #t       ; ^rt-parameter-type-error:not a string (#t) at frob-string: detail (#t)
   frob-string "hello"  ; ^rt-parameter-value-error:string should be > 10 code points ("hello") at frob-string: detail ("hello")
