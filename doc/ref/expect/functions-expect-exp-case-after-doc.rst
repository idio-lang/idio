``exp-case-after`` establishes match clauses identically to
:ref:`exp-case <expect/exp-case>` except the `clauses` are tested
*after* those passed in ``exp-case``.

Invoking ``exp-case-after`` doesn't actually perform any matching,
you must still call ``exp-case``.

Passing no clauses effectively unsets this behaviour.

:Example:

.. code-block:: idio

   import expect

   spawn echo foo

   (exp-case-after
    ("foo" {
      'after
    }))

   (exp-case
    ("bar" {
      'bar
    }))

to have ``exp-case`` return ``after``.

