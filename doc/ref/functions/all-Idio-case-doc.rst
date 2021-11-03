Each clause in `clauses` takes the form :samp:`(({v} ...) {expr} ...)`
except the last clause which can take the form :samp:`(else {expr}
...)`.

`key` is evaluated and then compared with :ref:`memv <memv>` against
the list in the head of each clause in `clauses`.

If the comparison is `true` then the result of ``case`` is the result
of evaluating the expressions in the body of the clause.  No other
clauses are considered.

If no comparison is `true` and there is an ``else`` clause then it is
run otherwise the result is ``#<void>``.

