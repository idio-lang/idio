By default, if any "async" exits with a non-zero status (from
:manpage:`exit(3)` or by signal) then the failure is reported (via
:ref:`condition-report <condition-report>`).

You can suppress this behaviour by setting
:var:`suppress-async-command-report!` to any non-``#f`` value.

