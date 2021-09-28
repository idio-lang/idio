.. _`pipe handles`:

Pipe Handles
============

Pipe handles are the entities through which we perform command
orchestration pipelines.  They are one underlying implementation of
:ref:`handles <handle type>`.

Pipe handles only really differ from file handles in that you cannot
*seek* on a pipe handle.

You can open pipe handles directly and there are command orchestration
meta-commands like ``pipe-into`` and ``pipe-from`` which will return
pipe handles.

Ordinarily you might simply cease using a handle and have it collected
by the GC but in the case of all forms of fd pipe-handles you will
want to actively close them to avoid running out of pipe descriptors.
:lname:`Idio` has no defence against you using everything up unwisely.


