.. _`fd handles`:

FD Handles
==========

FD handles don't exist *per se* but abstract the idea of file
descriptor handles, ie. both file and pipe handles, giving a set of
commands that work for either.

You cannot open fd handles directly but must use a file or pipe handle
creation method.

Ordinarily you might simply cease using a handle and have it collected
by the GC in due course but in the case of all forms of fd-handles you
may want to actively close them to avoid running out of file
descriptors.  :lname:`Idio` has no defence against you using up
limited resources unwisely.


