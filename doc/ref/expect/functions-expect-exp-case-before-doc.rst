``exp-case-before`` establishes match clauses identically to
:ref:`exp-case <expect/exp-case>` except the `clauses` are tested
*before* those passed in ``exp-case``.

Invoking ``exp-case-before`` doesn't actually perform any matching,
you must still call ``exp-case``.

Passing no clauses effectively unsets this behaviour.

:Example:

.. code-block:: idio

   import expect

   spawn echo foo

   (exp-case-before
    ("foo" {
      'before
    }))

   (exp-case
    ("foo" {
      'normally
    }))

to have ``exp-case`` return ``before``.

