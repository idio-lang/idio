This is the number of arguments to the *script*.

Clearly, if no arguments are passed to the script then :var:`ARGC`
is 0.

What if no script is being run, ie. we are in an interactive shell?
Arguably, :var:`ARGC` should be 0 again but currently it is -1 to
distinguish that case.
