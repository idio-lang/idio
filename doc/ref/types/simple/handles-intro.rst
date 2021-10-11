.. _`handle type`:

Handle Type
===========

Handles are the entities through which Idio performs input and output.

A *handle* is opaque about its implementation although there are
several flavours.  There are the obvious :ref:`file handles` and a
similar form, :ref:`pipe handles`.  File and pipe handles can be
abstracted to :ref:`fd handles` (file descriptor handles).

There are also :ref:`string handles` which are useful for transient
I/O.

You cannot open handles directly but must use a file, pipe or string
handle creation method.

Ordinarily you might simply cease using a handle and have it collected
by the GC in due course but in the case of all forms of fd-handles you
may want to actively close them to avoid running out of file
descriptors.  :lname:`Idio` has no defence against you using up
limited resources unwisely.

