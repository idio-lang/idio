:default: ``" \t\n"`` (SPACE TAB NEWLINE)

:var:`IFS` is the Input [#]_ Field Separator and is the value used by
:ref:`fields <fields>` or the default value used by :ref:`split-string
<split-string>` to split a string into substrings.

.. [#] In :lname:`Bash` it is the *Internal* Field Separator and is
       used for both input and output field separation processing and
       unlike :program:`awk`'s :var:`RS` and :var:`ORS`, say.
