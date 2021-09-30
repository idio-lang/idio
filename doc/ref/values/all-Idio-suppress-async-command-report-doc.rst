By default, if any "async" exits with a non-zero status (from
:manpage:`exit(3)` or by signal) then the failure goes unreported.

You can suppress this behaviour by setting
``suppress-async-command-report!`` to ``#f`` to enable a
:ref:`condition-report <condition-report>`.

