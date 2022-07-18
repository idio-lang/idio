:default: any existing environment variable

If :var:`PATH` is not currently set in the environment then it is set
to the result of passing ``_CS_PATH`` to :manpage:`confstr(3)` or,
failing that, ``"/bin:/usr/bin"``.
