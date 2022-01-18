``C/for`` is a variation on :ref:`do <do>` but adds :ref:`continue
<continue>` and :ref:`break <break>` continuations to alter the flow
of control.

`var-clauses` is a list of `var-clause` which is a tuple,
:samp:`({var} {init} {step})`.  Each :samp:`{var}` is initialised to
:samp:`{init}` and adjusted by :samp:`{step}` each time round the
loop.

.. note::

   Each :samp:`{var}` is local to the `body` loop.

`test` is evaluated each time round the loop, the same as :lname:`C`.
If the test fails ``(break)`` is implicitly called.

`body` is the usual body form.

``C/for`` returns either ``#<void>``, from the implicit ``(break)``,
or the value supplied to any explicit call to ``break``.

:Example:

Loop over :samp:`{i}`, starting from 1 and less than 10.  If
:samp:`{i}` is 3 then return ``99`` otherwise return :samp:`{i} + 13`.

.. code-block:: idio

   C/for ((i 1 (1 + i))) (i lt 10) {
     if (i eq 3) {
       break 99
     }
     i + 13
   }

.. seealso:: :ref:`while <while>` the degenerative form of ``C/for``
             with no `var-clauses`.

