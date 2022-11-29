
# zlib

This is a simple wrapper around the zlib functions.

## Build

The extension will not be built unless the zlib headers are
installed.  This might be a

 zlib-devel
 zlib1g-dev
 debug-zlib

package on your system.

The actual configuration test is performed by
utils/bin/gen-idio-system which provides src/Makefile.system with a
IDIO_ZLIB_DEV flag which is used to conditionally compile this code.
