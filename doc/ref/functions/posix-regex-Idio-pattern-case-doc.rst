``pattern-case`` works like a simplified :ref:`cond <cond special
form>` where `e` is the string to be matched against and the
"conditions" in each clause are the *pattern matches* to test with.

:param e: the string to be matched against
:type e: string
:param clauses: clauses like :samp:`("{pattern-match}" {expr})`
:return: whatever any matched clause's consequent expression returns.

`e` will be evaluated and should return a string.

Here, *pattern matches* have :ref:`regex-pattern-string
<regex-pattern-string>` applied, are anchored to the entire string and
the code continues like :ref:`regex-case <regex-case>`.  The string
manipulation is like:

    :samp:`sprintf "^%s$" (regex-pattern-string {pattern-match})`

If the pattern matches then the consequent expression is
treated like an implict ``=>`` clause where the supplied parameter is
:var:`r`.

Thus :var:`r.0` represents the whole of the matched string, :var:`r.1`
the first matched sub-expression, :var:`r.2` the second matched
sub-expression, etc..

:Example:

Suppose we want an unreliable method to determine if this is a
BSD-style operating system:

.. code-block:: idio

   (pattern-case (collect-output uname -s)
     ("*BSD" {
       printf "%s is a BSD\n" r.0
     }))

.. note::

   ``pattern-case`` stashes the compiled regular expression for
   literal strings in a global table.  This means that in loops the
   regular expression doesn't need to be recompiled.  It also means
   the compiled regular expressions are not reaped until :lname:`Idio`
   exits.

.. seealso:: :ref:`regex-case <regex-case>`

