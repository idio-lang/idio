
# libcurl

This is a simple wrapper around the curl-easy-* functionality of
libcurl.

libcurl v7.73.0 and later only.

(We use the curl_easy_option_by_name() function and structure.)

## Build

The extension will not be built unless the libcurl headers are
installed.  This might be a

 libcurl-devel
 libcurl4-openssl-dev
 debug-curl

package on your system.

The actual configuration test is performed by
utils/bin/gen-idio-system which provides src/Makefile.system with a
IDIO_LIBCURL_DEV flag which is used to conditionally compile this
code.
