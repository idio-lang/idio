Despite the visual differences, ``do`` performs much the same task as
:lname:`C`'s :samp:`for ({init}; {test}; {step}) {body}` statement.

`var-clauses` is a list of `var-clause` which is a tuple,
:samp:`({var} {init} {step})`.  Each :samp:`{var}` is initialised to
:samp:`{init}` and adjusted by :samp:`{step}` each time round the
loop.

`test-result` offers a little more flexibility than :lname:`C` in that
`test-result` is the tuple :samp:`({test} {expr})` where
:samp:`{test}` is evaluated each time round the loop, the same as
:lname:`C`, but ``do`` allows you to run some arbitrary :samp:`{expr}`
for the result to be returned from ``do``.

`body` is the usual body form.

:Example:

Increment :samp:`{i}` starting from 1.  If :samp:`{i}` is 10 then
return :samp:`{i} + 13`.  The body, ``i``, is a placeholder in this
instance.

.. code-block:: idio

   do ((i 1 (1 + i))) ((eq i 10) i + 13) i      ; 23
