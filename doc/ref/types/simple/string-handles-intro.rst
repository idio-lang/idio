.. _`string handles`:

String Handles
==============

String handles use memory as their backing store otherwise they
maintain the same interface as file handles.  You can write to a
string until you run out of memory in the same way you can write to a
file until you run of space in the file system.

You can only have input or output handles, not a mixed form.

An output string handle requires an extra step,
:samp:`get-output-string {string-handle}` to retrieve the string in
the string handle.  Compare this with having to, say, :program:`cat` a
file to retrieve its contents after having written to it.


