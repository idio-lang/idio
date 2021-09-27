.. _`file handles`:

File Handles
============

File handles are the entities through which we perform input and
output to the file system.  They are one underlying implementation of
handles.

Ordinarily you would create input or output file handles with
:samp:`open-input-file {filename}` and :samp:`open-output-file
{filename}` respectively but you can also :samp:`open-file {filename}
{mode}` for some choice of :samp:`{mode}`.

Ordinarily you might simply cease using a handle and have it collected
by the GC but in the case of all forms fd file-handles you will want
to actively close them to avoid running out of file descriptors.
:lname:`Idio` has no defence against you using everything up unwisely.


