.. _`pair type`:

Pair Type
=========

Pairs are the primary compound type.  A pair is a value with
references to two other values called the *head* and *tail*.

A pair is printed as :samp:`({head} & {tail})`.

If the tail references another pair then you can envisage a chain
forming, more commonly called a *list*.

Lists
-----

Lists are an emergent property of chained pairs and are commonly
written and printed without superfluous chaining punctuation so:

    :samp:`({e1} & ({e2} & ({e3} & #n)))`

is the more convenient:

    :samp:`({e1} {e2} {e3})`

If the tail of the last element of the list is not ``#n`` then the
list is an *improper* list and is printed as:

    :samp:`({e1} {e2} & {e3})`

This form is commonly used in the *formals* of a function definition
to denote a varargs parameter, that is, bundle any remaining arguments
up into a list.

When the :ref:`evaluator <evaluator>` sees a list it interprets it as
a function call.  However, if you simply want to create a list
variable you need to *quote* the list to prevent the evaluator
processing it as a function call:

.. code-block:: idio

   my-list := '(1 2 3)

Superficially, with "only the *head* to play with", lists seem
relatively limited.  However, the structure pointed to by the head can
be arbitrarily complex.

Association Lists
^^^^^^^^^^^^^^^^^

One simple form of that complexity is if the head is itself a list.

This is called an association list where there are a number of
functions that will walk over the (top-level) list looking for a key
matching the first element of one of the sub-lists which it will
return:

.. code-block:: idio

   al := '((#\a "apple" 'fruit)
           (#\b "banana" 'fruit)
           (#\c "carrot" 'vegetable))

   assq #\b al		; '(#\b "banana" 'fruit)

