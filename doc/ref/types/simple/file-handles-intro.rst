.. _`file handles`:

File Handles
============

File handles are the entities through which we perform input and
output to the file system.  They are one underlying implementation of
:ref:`handles <handle type>`.

Ordinarily you would create input or output file handles with
:samp:`open-input-file {filename}` and :samp:`open-output-file
{filename}` respectively but you can also :samp:`open-file {filename}
{mode}` for some choice of :samp:`{mode}`.

