
# sqlite3

This is a simple wrapper around some of the sqlite3 functions.

## Build

The extension will not be built unless the sqlite3 headers are
installed.  This might be a

 sqlite-devel
 libsqlite3-dev

package on your system.

The actual configuration test is performed by
utils/bin/gen-idio-system which provides src/Makefile.system with a
IDIO_SQLITE3_DEV flag which is used to conditionally compile this
code.

sqlite3_open_v2() introduced in 3.5.0
SQLITE_OPEN_URI introduced in 3.7.7.1
sqlite3_close_v2() introduced in 3.7.14
