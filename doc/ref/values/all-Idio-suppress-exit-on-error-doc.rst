By default, if an external command exits with a non-zero status (from
:manpage:`exit(3)` or by signal) then :program:`idio` will exit the
same way.

You can suppress this behaviour by setting
:var:`suppress-exit-on-error!` to any non-``#f`` value.
