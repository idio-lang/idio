.. _`sorting`:

Sorting
^^^^^^^

These sorting functions are a port of SRFI-95_ and, generally the
functions take a sequence (a list or an array), a function that can
determine a "less than" comparison between two values and an optional
*accessor* function.

The *accessor* function allows you to indirect through another data
structure and have the comparison function sort the value returned by
the accessor rather than the original datum.  Sorting by proxy, if you
will.

For example, suppose you want to sort a list of file names by the size
of each file:

#. :ref:`stat <libc/stat>` all the files and put the results in a hash
   table indexed by file name

   You could put the entire :ref:`struct-stat <libc/struct-stat>`
   struct in the hash table or just the ``st_size`` member depending
   on what you want your accessor function to do.

#. create a named, or anonymous, accessor function that, given a file
   name, retrieves the corresponding ``st_size`` member from the hash
   table

#. the ``st_size`` member is a :lname:`C` type so we can use
   :ref:`C/\< <C/\<>` as the "less than" comparison function

#. call :ref:`sort <sort>` with the list of file names, ``C/<`` and
   your accessor function

Obviously, you call :ref:`reverse <reverse>` if you want the file
names sorted in the opposite order.

Functions for sorting file names by ``st_atime``, ``st_ctime``,
``st_atime`` and ``st_size`` are below.

