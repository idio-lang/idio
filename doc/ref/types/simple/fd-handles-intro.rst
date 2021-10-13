.. _`fd handles`:

FD Handles
----------

FD handles don't exist *per se* but abstract the idea of file
descriptor handles, ie. both file and pipe handles, giving a set of
commands that work for either.

You cannot open fd handles directly but must use a file or pipe handle
creation method.

