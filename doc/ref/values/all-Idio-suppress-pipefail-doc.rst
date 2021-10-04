By default, if any process in a job pipeline exits with a non-zero
status (from :manpage:`exit(3)` or by signal) then the whole pipeline
is determined to have failed.

You can suppress this behaviour by setting :var:`suppress-pipefail!` to
any non-``#f`` value.
