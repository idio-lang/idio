``regex-case`` works like :ref:`case <case>` where `e` is the string
to be matched against and the conditions in each clause are the
regular expressions to test with.

:param e: the string to be matched against
:type e: string
:param clauses: clauses like ``case``
:return: whatever any matched clause's consequent expression returns.

If the regular expression matches then the consequent expression is
treated like an implict ``=>`` clause where the supplied parameter is
:var:`r`.

Thus :var:`r.0` represents the whole of the matched string, :var:`r.1`
the first matched sub-expression, :var:`r.2` the second matched
sub-expression, etc..

:Example:

Suppose we want to match a common :samp:`{var}={value}` assignment:

.. code-block:: idio

   (regex-case (read-line)
     ("^([:alpha:][[:alnum:]_]*)=(.*)" {
       printf "%s is '%s'\n" r.1 r.2
     }))

.. note::

   ``regex-case`` stashes the compiled regular expression in a global
   table.  This means that in loops the regular expression doesn't
   need to be recompiled.  It also means the compiled regular
   expressions are not reaped until :lname:`Idio` exits.

.. seealso:: :ref:`pattern-case <pattern-case>`

