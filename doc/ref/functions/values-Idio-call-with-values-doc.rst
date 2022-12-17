invoke `consumer` with the values returned from invoking `producer`

:param producer: function to return *multiple values*
:type producer: thunk
:param consumer: function to accept as many values as `producer` generates
:type consumer: function
:return: return value of `consumer`
:rtype: any

``call-with-values`` is the fundamental operator in *multiple values*,
all other functions are derivations of it.

:Example:

.. code-block:: idio

   (call-with-values
    (function #n (values 1 2 3 4))
    (function (a b c d) {
      printf "a + d = %s\n" (a + d)	; a + d = 5
    }))

Here, the `consumer` function is called with arguments ``1 2 3 4``,
the *multiple values* returned by the `producer` function.
