.. _`POSIX regex`:

POSIX regex
^^^^^^^^^^^

:lname:`Idio` uses the POSIX :manpage:`regex(7)` regular expression
primitives :ref:`regcomp <regcomp>` and :ref:`regexec <regexec>`.
These are combined in the function :ref:`regex-matches
<regex-matches>`.

Slightly better for use in loops is the template :ref:`regex-case
<regex-case>` which works like a simplified :ref:`cond <cond special
form>` except the clause "conditions" are regular expressions to be
matched.

``regex-case`` then supplies the consequent block with the result of
the call to ``regexec`` as the variable :var:`r`.  As such :var:`r.0`
is the whole of the matched string, :var:`r.1` is the first matched
sub-expression, :var:`r.2` the second matched sub-expression, etc..

Similarly, :ref:`pattern-case <pattern-case>` provides something like
the shell's *Pattern Matching* where ``*`` and ``?`` are really ``.*``
and ``.`` respectively.

