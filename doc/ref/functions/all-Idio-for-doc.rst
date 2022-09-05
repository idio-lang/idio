`for` takes two forms:

    :samp:`for {v} in value {body}`

    :samp:`for ({v1} {v2} ...) in values {body}`

where `body` is a body form that uses the variable(s) :samp:`{v}` or
:samp:`{v1}`, :samp:`{v2}`, etc.,

In the first, singular, form, :samp:`{value}` can be a list, array or
string.

A string will be split into Unicode code points.

In the second, multiple, form, :samp:`{values}` must be a list of
lists, arrays or strings and should be of the same type.

Strings will be split into Unicode code points.

:Example:

Printing the elements of a string, one per line:

.. code-block:: idio-console

   Idio> for x in "hello" {
     printf "%s\n" x
   }
   h
   e
   l
   l
   o
   #<void>

Printing the paired elements of two lists, a pair per line:

.. code-block:: idio-console

   Idio> for (x y) in '((1 2 3) (#\a #\b #\c)) {
     printf "%s - %s\n" x y
   }
   1 - a
   2 - b
   3 - c
   #<void>
