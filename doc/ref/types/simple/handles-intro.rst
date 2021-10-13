.. _`handle type`:

Handle Type
===========

Handles are the entities through which Idio performs input and output.

A *handle* is opaque about its implementation although there are
several flavours.  There are the obvious :ref:`file handles` and a
similar form, :ref:`pipe handles`.  File and pipe handles can be
abstracted to :ref:`fd handles` (file descriptor handles).

There are also :ref:`string handles` which are useful for pseudo I/O,
capturing output or creating strings.

You cannot open handles directly but must use a file, pipe or string
handle creation method.

Ordinarily you might simply cease using a handle and have it collected
by the GC in due course but in the case of all forms of FD handles you
may want to be more careful in ensuring you do not retain a reference
to them as otherwise the GC cannot collect the handle and close the
file descriptor.

:lname:`Idio` has no defence against you using up limited resources
unwisely.

