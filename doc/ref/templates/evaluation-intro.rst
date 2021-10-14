Template Evaluation
-------------------

If the evaluator determines that the element is functional position in
a form is a template (technically, an *expander*) then the arguments
to the template are not evaluated but passed verbatim to the expander.

The job of the expander/template is to return some expression that
can, in turn, be evaluated.  (Hence the idea of generating code.)
Note that the expression returned must be something that can be
evaluated and so cannot be a regular value.

Of course, returned expression might be the invocation of a template
in which case its expander will be invoked.

Circular loops should be avoided.

The template is a regular function and can do whatever it likes.  The
only constraint is that it must return something that can be
evaluated.  You can create an evaluable return value by hand but it is
more convenient to express the evaluable expression as the code you
want to have produced: it should look like this but with these
elements replaced, ie. a template.

Quasiquoting
^^^^^^^^^^^^

*quasiquoting* is like :ref:`quote <quote special form>` but allows
for sections of the quoted expression to be evaluated, like the
substitution of a variable name, although any expression can be
evaluated.

Quasiquoting can be nested which adds visual complexity.

:lname:`Idio` goes a step further and allows you to change the
quasiquotation *sigils* within a block if it is more convenient.

A quasiquotation block in :lname:`Idio` is delimited by ``#T{ ... }``,
denoting a template, and the default quasiquotation sigils are:

* ``$`` for *unquoting*, that is, have the next expression evaluated,
  the exact opposite of ``quote``

* ``@``, following the unquote sigil, meaning *unquote splicing* where
  the expectation is that the expression being evaluated will return a
  list and we want all the *elements* of the list inserted (and not a
  *list* of the elements)

* ``'`` for *quoting*, ie. like ``quote``

* ``\`` for *escaping* the behaviour of a sigil

You can change the sigils by passing the preferred sigils between
``#T`` and the opening ``{``.  The default sigils are effectively:

    ``#T$@'\{ ... }``

If you only want to change the quoting sigil, say, use ``.`` for the
preceding two -- meaning that ``.`` cannot be a sigil:

    ``#T..!{ ... }``

Here, the quoting sigil is now ``!`` and the others remain the same.

The expression inside the template is reproduced verbatim except any
unquoted elements are evaluated.

Hygiene
^^^^^^^

Casual creation of variables in templates are at high risk of clashing
with or occluding variables outside of the template.

Consider:

.. code-block:: idio

   define-template (foo a) {
     #T{
       b := frob $a

       printf "b = %s\n" b
     }
   }

which will expand into:
   
.. parsed-literal::

   b := frob *a*

   printf "b = %s\\n" b

where :samp:`{a}` is whatever was passed to ``foo`` when it was
invoked.

Here, if ``b`` already existed in the environment then we've just
clobbered it.

The use of :ref:`gensym <gensym>` is strongly advised.  Create a
*gensymed* variable name as a regular variable in the expander
function and have the quasiquotation expand the name out to the value:

.. code-block:: idio

   define-template (foo a) {
     b-var := gensym 'b

     #T{
       $b-var := frob $a

       printf "b = %s\n" $b-var
     }
   }

which will now expand into something like:
   
.. parsed-literal::

   b/*n* := frob *a*

   printf "b = %s\\n" b/*n*

where :samp:`b/{n}` is a unique symbol.

