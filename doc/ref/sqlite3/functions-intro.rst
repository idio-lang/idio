.. idio:currentmodule:: sqlite3

sqlite3 Functions
-----------------

The mappings of :lname:`sqlite3` types to/from :lname:`Idio` types is:

.. csv-table:: :lname:`Idio` to :lname:`sqlite3`
   :widths: auto
   :align: left

   fixnum, ``SQLITE_INTEGER``
   integer bignum, ``SQLITE_INTEGER``
   non-integer bignum, ``SQLITE_FLOAT``
   ``C/int``, ``SQLITE_INTEGER``
   ``C/double``, ``SQLITE_FLOAT``
   ``#n``, ``SQLITE_NULL``
   ``libc/int64_t``, ``SQLITE_INTEGER``
   octet-string or pathname, ``SQLITE_BLOB``
   string, ``SQLITE_TEXT``

.. csv-table:: :lname:`sqlite3` to :lname:`Idio`
   :widths: auto
   :align: left

   ``SQLITE_BLOB``, octet-string
   ``SQLITE_FLOAT``, bignum
   ``SQLITE_INTEGER``, integer (fixnum or bignum)
   ``SQLITE_NULL``, ``#n``
   ``SQLITE_TEXT``, string
