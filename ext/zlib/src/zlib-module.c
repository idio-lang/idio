/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * zlib-module.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <zlib.h>

#include "gc.h"

#include "bignum.h"
#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "idio.h"
#include "keyword.h"
#include "libc-wrap.h"
#include "zlib-module.h"
#include "zlib-system.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"

IDIO idio_zlib_module;

IDIO idio_condition_rt_zlib_error_type;

IDIO_C_STRUCT_IDENT_DECL (zlib_z_stream);

IDIO idio_zlib_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == idio_vasprintf (&s, format, argp)) {
	idio_error_alloc ("asprintf");
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (s, sh);

    /*
     * idio_vasprintf will not have called idio_gc_alloc to no stats
     * decrement
     */
    idio_free (s);

    return idio_get_output_string (sh);
}

void idio_zlib_error_printf (int ret, IDIO detail, IDIO c_location, char *format, ...)
{
    IDIO_ASSERT (detail);
    IDIO_ASSERT (c_location);
    assert (format);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg2 = idio_zlib_error_string (format, fmt_args);
    va_end (fmt_args);

    idio_display (msg2, msh);
    idio_display_C (": ", msh);

    switch (ret) {
    case Z_ERRNO:
	idio_display_C ("problem with source/dest stream", msh);
	break;
    case Z_STREAM_ERROR:
	idio_display_C ("invalid compression level", msh);
	break;
    case Z_DATA_ERROR:
	idio_display_C ("invalid or incomplete deflate data", msh);
	break;
    case Z_MEM_ERROR:
	idio_display_C ("out of memory", msh);
	break;
    case Z_BUF_ERROR:
	idio_display_C ("no progress possible", msh);
	break;
    case Z_VERSION_ERROR:
	idio_display_C ("zlib version mismatch", msh);
	break;
    default:
	{
	    char em[BUFSIZ];
	    snprintf (em, BUFSIZ, "zlib ret == %d", ret);
	    idio_display_C (em, msh);
	}
    }

    if (idio_S_nil != detail) {
	idio_display (detail, dsh);
    }

    IDIO c = idio_struct_instance (idio_condition_rt_zlib_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       idio_get_output_string (lsh),
					       idio_get_output_string (dsh)));

    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

/*
 * XXX The obvious C nickname, zlib_version, is #define'd to the
 * *actual* zlib version function, zlibVersion()...  We need a
 * better/safer nickname.
 */
IDIO_DEFINE_PRIMITIVE0_DS ("zlib-version", idio_zlib_version, (), "", "\
Return the zlib version	\n\
				\n\
:return: zlib version	\n\
:rtype: string			\n\
")
{
    return idio_string_C (zlibVersion ());
}

#define IDIO_ZLIB_CHUNK 16384

IDIO idio_zlib_deflate (IDIO handle, int level, int method, int windowBits, int memLevel, int strategy)
{
    IDIO_ASSERT (handle);

    IDIO_TYPE_ASSERT (input_handle, handle);

    z_stream *zs = idio_alloc (sizeof (z_stream));
    IDIO C_p = idio_C_pointer_type (idio_CSI_zlib_z_stream, zs);

    zs->zalloc = Z_NULL;
    zs->zfree  = Z_NULL;
    zs->opaque = Z_NULL;

    int ret = deflateInit2 (zs, level, method, windowBits, memLevel, strategy);

    if (Z_OK != ret) {
	/*
	 * Test Case: ??
	 */
	idio_zlib_error_printf (ret, handle, IDIO_C_FUNC_LOCATION (), "deflateInit()");

	return idio_S_notreached;
    }

    idio_gc_register_finalizer (C_p, idio_zlib_deflate_finalizer);

    IDIO osh = idio_open_output_string_handle_C ();

    int flush;
    do {
	unsigned char in[IDIO_ZLIB_CHUNK];
	unsigned char *inp = in;

	size_t avail_in = 0;
	while (avail_in < IDIO_ZLIB_CHUNK) {
	    idio_unicode_t c = idio_getc_handle (handle);

	    if (idio_eofp_handle (handle)) {
		break;
	    }

	    int size = 0;
	    char buf[4];
	    idio_utf8_code_point (c, buf, &size);

	    for (int n = 0; n < size; n++) {
		*inp++ = buf[n];
	    }

	    avail_in += size;
	}

	zs->next_in = in;
	zs->avail_in = avail_in;

	flush = idio_eofp_handle (handle) ? Z_FINISH : Z_NO_FLUSH;

	do {
	    unsigned char out[IDIO_ZLIB_CHUNK];
	    zs->avail_out = IDIO_ZLIB_CHUNK;
	    zs->next_out = out;
	    ret = deflate (zs, flush);

	    if (Z_STREAM_ERROR == ret) {
		/*
		 * Test Case: ??
		 */
		idio_zlib_error_printf (ret, handle, IDIO_C_FUNC_LOCATION (), "deflate()");

		return idio_S_notreached;
	    }

	    unsigned have = IDIO_ZLIB_CHUNK - zs->avail_out;

	    unsigned i = 0;
	    while (i < have) {
		idio_putb_handle (osh, (uint8_t) out[i]);
		i++;
	    }
	} while (zs->avail_out == 0);

	if (0 != zs->avail_in) {
	    /*
	     * Test Case: ??
	     */
	    idio_zlib_error_printf (0, handle, IDIO_C_FUNC_LOCATION (), "deflate(): 0 != strm.avail_in %zu", zs->avail_in);

	    return idio_S_notreached;
	}

    } while (flush != Z_FINISH);

    if (Z_STREAM_END != ret) {
	/*
	 * Test Case: ??
	 */
	idio_zlib_error_printf (ret, handle, IDIO_C_FUNC_LOCATION (), "deflate(): Z_STREAM_END != ret");

	return idio_S_notreached;
    }

    /*
     * cleanup
     *
     * C_p, the C/pointer, will be garbage collected in due course
     */
    idio_gc_deregister_finalizer (C_p);
    deflateEnd (zs);

    return idio_get_output_octet_string (osh);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("deflate", zlib_deflate, (IDIO handle, IDIO args), "handle [level [windowBits [strategy]]]", "\
Return zlib compression of the UTF-8 encoded	\n\
data stream in `handle`.			\n\
				\n\
:param handle: handle		\n\
:type handle: input handle	\n\
:param level: compression level, defaults to ``Z_DEFAULT_COMPRESSION``	\n\
:type level: C/int|fixnum, optional	\n\
:param windowBits: base two logarithm of the window size, defaults to 15	\n\
:type windowBits: C/int|fixnum, optional	\n\
:param strategy: strategy, defaults to ``Z_DEFAULT_STRATEGY``	\n\
:type strategy: C/int|fixnum, optional	\n\
:return: compressed data	\n\
:rtype: octet string		\n\
")
{
    IDIO_ASSERT (handle);

    /*
     * Test Case: zlib-errors/deflate-bad-handle-type.idio
     *
     * deflate #t
     */
    IDIO_USER_TYPE_ASSERT (input_handle, handle);

    int C_level      = Z_DEFAULT_COMPRESSION;
    int C_method     = Z_DEFLATED;	/* only available value */
    int C_windowBits = 15;		/* default for deflateInit() */
    int C_memLevel   = 8;		/* default */
    int C_strategy   = Z_DEFAULT_STRATEGY;

    if (idio_isa_pair (args)) {
	IDIO level = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (level)) {
	    C_level = IDIO_FIXNUM_VAL (level);
	} else if (idio_isa_C_int (level)) {
	    C_level = IDIO_C_TYPE_int (level);
	} else {
	    /*
	     * Test Case: zlib-errors/deflate-bad-level-type.idio
	     *
	     * deflate (current-input-handle) #t
	     */
	    idio_error_param_type ("C/int|fixnum", level, IDIO_C_FUNC_LOCATION ());
	}

	if (Z_DEFAULT_COMPRESSION != C_level &&
	    (C_level < 0 ||
	     C_level > 9)) {
	    /*
	     * Test Case: zlib-errors/deflate-bad-level-value.idio
	     *
	     * deflate (current-input-handle) 99
	     */
	    idio_error_param_value_msg ("deflate", "level", idio_fixnum (C_level), "should be 0 <= int <= 9", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	args = IDIO_PAIR_T (args);

	if (idio_isa_pair (args)) {
	    IDIO windowBits = IDIO_PAIR_H (args);
	    if (idio_isa_fixnum (windowBits)) {
		C_windowBits = IDIO_FIXNUM_VAL (windowBits);
	    } else if (idio_isa_C_int (windowBits)) {
		C_windowBits = IDIO_C_TYPE_int (windowBits);
	    } else {
		/*
		 * Test Case: zlib-errors/deflate-bad-windowBits-type.idio
		 *
		 * deflate (current-input-handle) Z_DEFAULT_COMPRESSION #t
		 */
		idio_error_param_type ("C/int|fixnum", windowBits, IDIO_C_FUNC_LOCATION ());
	    }

	    if (Z_DEFAULT_COMPRESSION != C_windowBits) {
		if (C_windowBits > 0) {
		    if (!((C_windowBits >= 9 &&
			   C_windowBits <= 15) ||
			  (C_windowBits >= (16 + 9) &&
			   C_windowBits <= (16 + 15)))) {
			/*
			 * Test Case: zlib-errors/deflate-bad-windowBits-positive-value.idio
			 *
			 * deflate (current-input-handle) Z_DEFAULT_COMPRESSION 99
			 */
			idio_error_param_value_msg ("deflate", "windowBits", idio_fixnum (C_windowBits), "should be [16+]9 <= int <= [16+]15", IDIO_C_FUNC_LOCATION ());

			return idio_S_notreached;
		    }
		} else if (C_windowBits > -9 ||
			   C_windowBits < -15) {
		    /*
		     * Test Case: zlib-errors/deflate-bad-windowBits-negative-value.idio
		     *
		     * deflate (current-input-handle) Z_DEFAULT_COMPRESSION -99
		     */
		    idio_error_param_value_msg ("deflate", "windowBits", idio_fixnum (C_windowBits), "should be -15 <= int <= -9", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }

	    args = IDIO_PAIR_T (args);

	    if (idio_isa_pair (args)) {
		IDIO strategy = IDIO_PAIR_H (args);
		if (idio_isa_fixnum (strategy)) {
		    C_strategy = IDIO_FIXNUM_VAL (strategy);
		} else if (idio_isa_C_int (strategy)) {
		    C_strategy = IDIO_C_TYPE_int (strategy);
		} else {
		    /*
		     * Test Case: zlib-errors/deflate-bad-strategy-type.idio
		     *
		     * deflate (current-input-handle) Z_DEFAULT_COMPRESSION 15 #t
		     */
		    idio_error_param_type ("C/int|fixnum", strategy, IDIO_C_FUNC_LOCATION ());
		}

		args = IDIO_PAIR_T (args);
	    }
	}
    }

    return idio_zlib_deflate (handle, C_level, C_method, C_windowBits, C_memLevel, C_strategy);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("zlib-compress", zlib_compress, (IDIO handle, IDIO args), "handle [level [windowBits [strategy]]]", "\
Return zlib compression of the UTF-8 encoded	\n\
data stream in `handle`.			\n\
				\n\
:param handle: handle		\n\
:type handle: input handle	\n\
:param level: compression level, defaults to ``Z_DEFAULT_COMPRESSION``	\n\
:type level: C/int|fixnum, optional	\n\
:param windowBits: base two logarithm of the window size, defaults to 15	\n\
:type windowBits: C/int|fixnum, optional	\n\
:param strategy: strategy, defaults to ``Z_DEFAULT_STRATEGY``	\n\
:type strategy: C/int|fixnum, optional	\n\
:return: compressed data	\n\
:rtype: octet string		\n\
")
{
    IDIO_ASSERT (handle);

    /*
     * Test Case: zlib-errors/zlib-compress-bad-handle-type.idio
     *
     * zlib-compress #t
     */
    IDIO_USER_TYPE_ASSERT (input_handle, handle);

    int C_level      = Z_DEFAULT_COMPRESSION;
    int C_method     = Z_DEFLATED;	/* only available value */
    int C_windowBits = 15;		/* default for deflateInit() */
    int C_memLevel   = 8;		/* default */
    int C_strategy   = Z_DEFAULT_STRATEGY;

    if (idio_isa_pair (args)) {
	IDIO level = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (level)) {
	    C_level = IDIO_FIXNUM_VAL (level);
	} else if (idio_isa_C_int (level)) {
	    C_level = IDIO_C_TYPE_int (level);
	} else {
	    /*
	     * Test Case: zlib-errors/zlib-compress-bad-level-type.idio
	     *
	     * zlib-compress (current-input-handle) #t
	     */
	    idio_error_param_type ("C/int|fixnum", level, IDIO_C_FUNC_LOCATION ());
	}

	if (Z_DEFAULT_COMPRESSION != C_level &&
	    (C_level < 0 ||
	     C_level > 9)) {
	    /*
	     * Test Case: zlib-errors/zlib-compress-bad-level-value.idio
	     *
	     * zlib-compress (current-input-handle) 99
	     */
	    idio_error_param_value_msg ("zlib-compress", "level", idio_fixnum (C_level), "should be 0 <= int <= 9", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	args = IDIO_PAIR_T (args);

	if (idio_isa_pair (args)) {
	    IDIO windowBits = IDIO_PAIR_H (args);
	    if (idio_isa_fixnum (windowBits)) {
		C_windowBits = IDIO_FIXNUM_VAL (windowBits);
	    } else if (idio_isa_C_int (windowBits)) {
		C_windowBits = IDIO_C_TYPE_int (windowBits);
	    } else {
		/*
		 * Test Case: zlib-errors/zlib-compress-bad-windowBits-type.idio
		 *
		 * zlib-compress (current-input-handle) Z_DEFAULT_COMPRESSION #t
		 */
		idio_error_param_type ("C/int|fixnum", windowBits, IDIO_C_FUNC_LOCATION ());
	    }

	    if (Z_DEFAULT_COMPRESSION != C_windowBits) {
		if (C_windowBits < 9 ||
		    C_windowBits > 15) {
		    /*
		     * Test Cases:
		     *
		     *   zlib-errors/zlib-compress-bad-windowBits-negative-value.idio
		     *   zlib-errors/zlib-compress-bad-windowBits-positive-value.idio
		     *
		     * zlib-compress (current-input-handle) Z_DEFAULT_COMPRESSION 99
		     * zlib-compress (current-input-handle) Z_DEFAULT_COMPRESSION -99
		     */
		    idio_error_param_value_msg ("zlib-compress", "windowBits", idio_fixnum (C_windowBits), "should be 9 <= int <= 15", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }

	    args = IDIO_PAIR_T (args);

	    if (idio_isa_pair (args)) {
		IDIO strategy = IDIO_PAIR_H (args);
		if (idio_isa_fixnum (strategy)) {
		    C_strategy = IDIO_FIXNUM_VAL (strategy);
		} else if (idio_isa_C_int (strategy)) {
		    C_strategy = IDIO_C_TYPE_int (strategy);
		} else {
		    /*
		     * Test Case: zlib-errors/zlib-compress-bad-strategy-type.idio
		     *
		     * zlib-compress (current-input-handle) Z_DEFAULT_COMPRESSION 15 #t
		     */
		    idio_error_param_type ("C/int|fixnum", strategy, IDIO_C_FUNC_LOCATION ());
		}

		args = IDIO_PAIR_T (args);
	    }
	}
    }

    return idio_zlib_deflate (handle, C_level, C_method, C_windowBits, C_memLevel, C_strategy);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("gzip-compress", gzip_compress, (IDIO handle, IDIO args), "handle [level [windowBits [strategy]]]", "\
Return gzip compression of the UTF-8 encoded	\n\
data stream in `handle`.			\n\
				\n\
:param handle: handle		\n\
:type handle: input handle	\n\
:param level: compression level, defaults to ``Z_DEFAULT_COMPRESSION``	\n\
:type level: C/int|fixnum, optional	\n\
:param windowBits: base two logarithm of the window size, defaults to 15	\n\
:type windowBits: C/int|fixnum, optional	\n\
:param strategy: strategy, defaults to ``Z_DEFAULT_STRATEGY``	\n\
:type strategy: C/int|fixnum, optional	\n\
:return: compressed data	\n\
:rtype: octet string		\n\
")
{
    IDIO_ASSERT (handle);

    /*
     * Test Case: zlib-errors/gzip-compress-bad-handle-type.idio
     *
     * gzip-compress #t
     */
    IDIO_USER_TYPE_ASSERT (input_handle, handle);

    int C_level      = Z_DEFAULT_COMPRESSION;
    int C_method     = Z_DEFLATED;	/* only available value */
    int C_windowBits = 15;		/* default for deflateInit() */
    int C_memLevel   = 8;		/* default */
    int C_strategy   = Z_DEFAULT_STRATEGY;

    if (idio_isa_pair (args)) {
	IDIO level = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (level)) {
	    C_level = IDIO_FIXNUM_VAL (level);
	} else if (idio_isa_C_int (level)) {
	    C_level = IDIO_C_TYPE_int (level);
	} else {
	    /*
	     * Test Case: zlib-errors/gzip-compress-bad-level-type.idio
	     *
	     * gzip-compress (current-input-handle) #t
	     */
	    idio_error_param_type ("C/int|fixnum", level, IDIO_C_FUNC_LOCATION ());
	}

	if (Z_DEFAULT_COMPRESSION != C_level &&
	    (C_level < 0 ||
	     C_level > 9)) {
	    /*
	     * Test Case: zlib-errors/gzip-compress-bad-level-value.idio
	     *
	     * gzip-compress (current-input-handle) Z_DEFAULT_COMPRESSION 99
	     */
	    idio_error_param_value_msg ("gzip-compress", "level", idio_fixnum (C_level), "should be 0 <= int <= 9", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	args = IDIO_PAIR_T (args);

	if (idio_isa_pair (args)) {
	    IDIO windowBits = IDIO_PAIR_H (args);
	    if (idio_isa_fixnum (windowBits)) {
		C_windowBits = IDIO_FIXNUM_VAL (windowBits);
	    } else if (idio_isa_C_int (windowBits)) {
		C_windowBits = IDIO_C_TYPE_int (windowBits);
	    } else {
		/*
		 * Test Case: zlib-errors/gzip-compress-bad-windowBits-type.idio
		 *
		 * gzip-compress (current-input-handle) Z_DEFAULT_COMPRESSION #t
		 */
		idio_error_param_type ("C/int|fixnum", windowBits, IDIO_C_FUNC_LOCATION ());
	    }

	    if (Z_DEFAULT_COMPRESSION != C_windowBits) {
		if (C_windowBits < 9 ||
		    C_windowBits > 15) {
		    /*
		     * Test Cases:
		     *
		     *   zlib-errors/gzip-compress-bad-windowBits-positive-value.idio
		     *   zlib-errors/gzip-compress-bad-windowBits-negative-value.idio
		     *
		     * gzip-compress (current-input-handle) Z_DEFAULT_COMPRESSION 99
		     * gzip-compress (current-input-handle) Z_DEFAULT_COMPRESSION -99
		     */
		    idio_error_param_value_msg ("gzip-compress", "windowBits", idio_fixnum (C_windowBits), "should be 9 <= int <= 15", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }

	    args = IDIO_PAIR_T (args);

	    if (idio_isa_pair (args)) {
		IDIO strategy = IDIO_PAIR_H (args);
		if (idio_isa_fixnum (strategy)) {
		    C_strategy = IDIO_FIXNUM_VAL (strategy);
		} else if (idio_isa_C_int (strategy)) {
		    C_strategy = IDIO_C_TYPE_int (strategy);
		} else {
		    /*
		     * Test Case: zlib-errors/gzip-compress-bad-strategy-type.idio
		     *
		     * gzip-compress (current-input-handle) Z_DEFAULT_COMPRESSION 15 #t
		     */
		    idio_error_param_type ("C/int|fixnum", strategy, IDIO_C_FUNC_LOCATION ());
		}

		args = IDIO_PAIR_T (args);
	    }
	}
    }

    C_windowBits += 16;

    return idio_zlib_deflate (handle, C_level, C_method, C_windowBits, C_memLevel, C_strategy);
}

IDIO idio_zlib_inflate (IDIO handle, int windowBits)
{
    IDIO_ASSERT (handle);

    IDIO_TYPE_ASSERT (input_handle, handle);

    z_stream *zs = idio_alloc (sizeof (z_stream));
    IDIO C_p = idio_C_pointer_type (idio_CSI_zlib_z_stream, zs);

    zs->zalloc   = Z_NULL;
    zs->zfree    = Z_NULL;
    zs->opaque   = Z_NULL;
    zs->avail_in = 0;
    zs->next_in  = Z_NULL;

    int ret = inflateInit2 (zs, windowBits);

    if (Z_OK != ret) {
	/*
	 * Test case: ??
	 */
	idio_zlib_error_printf (ret, handle, IDIO_C_FUNC_LOCATION (), "inflateInit()");

	return idio_S_notreached;
    }

    idio_gc_register_finalizer (C_p, idio_zlib_inflate_finalizer);

    IDIO osh = idio_open_output_string_handle_C ();

    do {
	unsigned char in[IDIO_ZLIB_CHUNK];
	unsigned char *inp = in;

	size_t avail_in = 0;
	while (avail_in < IDIO_ZLIB_CHUNK) {
	    int c = idio_getb_handle (handle);

	    if (idio_eofp_handle (handle)) {
		break;
	    }

	    *inp++ = (unsigned char) c;

	    avail_in++;
	}

	if (0 == avail_in) {
	    break;
	}

	zs->next_in = in;
	zs->avail_in = avail_in;

	do {
	    unsigned char out[IDIO_ZLIB_CHUNK];
	    zs->avail_out = IDIO_ZLIB_CHUNK;
	    zs->next_out = out;
	    ret = inflate (zs, Z_NO_FLUSH);

	    if (Z_STREAM_ERROR == ret) {
		/*
		 * Test case: ??
		 */
		idio_zlib_error_printf (ret, handle, IDIO_C_FUNC_LOCATION (), "inflate()");

		return idio_S_notreached;
	    }

	    switch (ret) {
	    case Z_NEED_DICT:
		ret = Z_DATA_ERROR;
		/* fall through */
	    case Z_DATA_ERROR:
	    case Z_MEM_ERROR:
		/*
		 * Test case: zlib-errors/inflate-windowBits-smaller.idio
		 *
		 * ;; windowBits for inflate should be at least as big
		 * ;; as windowBits for deflate
		 *
		 * inflate (open-input-string (deflate ish :windowBits 15)) :windowBits 9
		 */
		idio_zlib_error_printf (ret, handle, IDIO_C_FUNC_LOCATION (), "inflate(): Z_STREAM_END != ret");

		return idio_S_notreached;
	    }

	    unsigned have = IDIO_ZLIB_CHUNK - zs->avail_out;

	    unsigned i = 0;
	    while (i < have) {
		idio_putb_handle (osh, (uint8_t) out[i]);
		i++;
	    }
	} while (zs->avail_out == 0);

    } while (ret != Z_STREAM_END);

    /*
     * cleanup
     *
     * C_p, the C/pointer, will be garbage collected in due course
     */
    idio_gc_deregister_finalizer (C_p);
    inflateEnd (zs);

    return idio_get_output_octet_string (osh);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("inflate", zlib_inflate, (IDIO handle, IDIO args), "handle [windowBits]", "\
Return zlib decompression of the bytes in `handle`.	\n\
				\n\
:param handle: handle		\n\
:type handle: input handle	\n\
:param windowBits: base two logarithm of the window size, defaults to 15	\n\
:type windowBits: C/int|fixnum, optional	\n\
:return: decompressed data	\n\
:rtype: octet string		\n\
")
{
    IDIO_ASSERT (handle);

    /*
     * Test Case: zlib-errors/inflate-bad-handle-type.idio
     *
     * inflate #t
     */
    IDIO_USER_TYPE_ASSERT (input_handle, handle);

    int C_windowBits = 15;	/* default for inflateInit() */

    if (idio_isa_pair (args)) {
	IDIO windowBits = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (windowBits)) {
	    C_windowBits = IDIO_FIXNUM_VAL (windowBits);
	} else if (idio_isa_C_int (windowBits)) {
	    C_windowBits = IDIO_C_TYPE_int (windowBits);
	} else {
	    /*
	     * Test Case: zlib-errors/inflate-bad-windowBits-type.idio
	     *
	     * inflate (current-input-handle) #t
	     */
	    idio_error_param_type ("C/int|fixnum", windowBits, IDIO_C_FUNC_LOCATION ());
	}

	if (0 != C_windowBits) {
	    if (C_windowBits > 0) {
		if (!((C_windowBits >= 9 &&
		       C_windowBits <= 15) ||
		      (C_windowBits >= (16 + 9) &&
		       C_windowBits <= (16 + 15)))) {
		    /*
		     * Test Case: zlib-errors/inflate-bad-windowBits-positive-value.idio
		     *
		     * inflate (current-input-handle) 99
		     */
		    idio_error_param_value_msg ("inflate", "windowBits", idio_fixnum (C_windowBits), "should be [16+]9 <= int <= [16+]15", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    } else if (C_windowBits > -9 ||
		       C_windowBits < -15) {
		/*
		 * Test Case: zlib-errors/inflate-bad-windowBits-negative-value.idio
		 *
		 * inflate (current-input-handle) -99
		 */
		idio_error_param_value_msg ("inflate", "windowBits", idio_fixnum (C_windowBits), "should be -15 <= int <= -9", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return idio_zlib_inflate (handle, C_windowBits);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("zlib-decompress", zlib_decompress, (IDIO handle, IDIO args), "handle [windowBits]", "\
Return zlib decompression of the bytes in `handle`.	\n\
				\n\
:param handle: handle		\n\
:type handle: input handle	\n\
:param windowBits: base two logarithm of the window size, defaults to 15	\n\
:type windowBits: C/int|fixnum, optional	\n\
:return: decompressed data	\n\
:rtype: octet string		\n\
")
{
    IDIO_ASSERT (handle);

    /*
     * Test Case: zlib-errors/zlib-decompress-bad-handle-type.idio
     *
     * zlib-decompress #t
     */
    IDIO_USER_TYPE_ASSERT (input_handle, handle);

    int C_windowBits = 15;	/* default for inflateInit() */

    if (idio_isa_pair (args)) {
	IDIO windowBits = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (windowBits)) {
	    C_windowBits = IDIO_FIXNUM_VAL (windowBits);
	} else if (idio_isa_C_int (windowBits)) {
	    C_windowBits = IDIO_C_TYPE_int (windowBits);
	} else {
	    /*
	     * Test Case: zlib-errors/zlib-decompress-bad-windowBits-type.idio
	     *
	     * zlib-decompress (current-input-handle) #t
	     */
	    idio_error_param_type ("C/int|fixnum", windowBits, IDIO_C_FUNC_LOCATION ());
	}

	if (0 != C_windowBits) {
	    if (C_windowBits < 9 ||
		C_windowBits > 15) {
		/*
		 * Test Cases:
		 *
		 *   zlib-errors/zlib-decompress-bad-windowBits-positive-value.idio
		 *   zlib-errors/zlib-decompress-bad-windowBits-negative-value.idio
		 *
		 * zlib-decompress (current-input-handle) 99
		 * zlib-decompress (current-input-handle) -99
		 */
		idio_error_param_value_msg ("zlib-decompress", "windowBits", idio_fixnum (C_windowBits), "should be 9 <= int <= 15", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    return idio_zlib_inflate (handle, C_windowBits);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("gzip-decompress", gzip_decompress, (IDIO handle, IDIO args), "handle [windowBits]", "\
Return gzip decompression of the bytes in `handle`.	\n\
				\n\
:param handle: handle		\n\
:type handle: input handle	\n\
:param windowBits: base two logarithm of the window size, defaults to 15	\n\
:type windowBits: C/int|fixnum, optional	\n\
:return: decompressed data	\n\
:rtype: octet string		\n\
")
{
    IDIO_ASSERT (handle);

    /*
     * Test Case: zlib-errors/gzip-decompress-bad-handle-type.idio
     *
     * gzip-decompress #t
     */
    IDIO_USER_TYPE_ASSERT (input_handle, handle);

    int C_windowBits = 15;	/* default for inflateInit() */

    if (idio_isa_pair (args)) {
	IDIO windowBits = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (windowBits)) {
	    C_windowBits = IDIO_FIXNUM_VAL (windowBits);
	} else if (idio_isa_C_int (windowBits)) {
	    C_windowBits = IDIO_C_TYPE_int (windowBits);
	} else {
	    /*
	     * Test Case: zlib-errors/gzip-decompress-bad-windowBits-type.idio
	     *
	     * gzip-decompress (current-input-handle) Z_DEFAULT_COMPRESSION #t
	     */
	    idio_error_param_type ("C/int|fixnum", windowBits, IDIO_C_FUNC_LOCATION ());
	}

	if (0 != C_windowBits) {
	    if (C_windowBits < 9 ||
		C_windowBits > 15) {
		/*
		 * Test Cases:
		 *
		 *   zlib-errors/gzip-decompress-bad-windowBits-positive-value.idio
		 *   zlib-errors/gzip-decompress-bad-windowBits-negative-value.idio
		 *
		 * gzip-decompress (current-input-handle) 99
		 * gzip-decompress (current-input-handle) -99
		 */
		idio_error_param_value_msg ("gzip-decompress", "windowBits", idio_fixnum (C_windowBits), "should be 9 <= int <= 15", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    C_windowBits += 16;

    return idio_zlib_inflate (handle, C_windowBits);
}

void idio_zlib_deflate_finalizer (IDIO C_p)
{
    IDIO_ASSERT (C_p);
    IDIO_TYPE_ASSERT (C_pointer, C_p);

    z_stream *zs = IDIO_C_TYPE_POINTER_P (C_p);

    deflateEnd (zs);
}

void idio_zlib_inflate_finalizer (IDIO C_p)
{
    IDIO_ASSERT (C_p);
    IDIO_TYPE_ASSERT (C_pointer, C_p);

    z_stream *zs = IDIO_C_TYPE_POINTER_P (C_p);

    inflateEnd (zs);
}

void idio_zlib_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_zlib_module, idio_zlib_version);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_zlib_module, zlib_deflate);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_zlib_module, zlib_compress);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_zlib_module, gzip_compress);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_zlib_module, zlib_inflate);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_zlib_module, zlib_decompress);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_zlib_module, gzip_decompress);
}

void idio_final_zlib ()
{
}

void idio_init_zlib (void *handle)
{
    idio_zlib_module = idio_module (IDIO_SYMBOL ("zlib"));

    idio_module_table_register (idio_zlib_add_primitives, idio_final_zlib, handle);

    idio_module_export_symbol_value (idio_S_version,
				     idio_string_C_len (ZLIB_SYSTEM_VERSION, sizeof (ZLIB_SYSTEM_VERSION) - 1),
				     idio_zlib_module);

    IDIO struct_name = IDIO_SYMBOL ("zlib/z_stream");
    IDIO_C_STRUCT_IDENT_DEF (struct_name, idio_S_nil, zlib_z_stream, idio_fixnum0);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_zlib_error_type, "^rt-zlib-error", idio_condition_runtime_error_type);

    /* compression levels */
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_NO_COMPRESSION"), idio_C_int (Z_NO_COMPRESSION), idio_zlib_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_BEST_SPEED"), idio_C_int (Z_BEST_SPEED), idio_zlib_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_BEST_COMPRESSION"), idio_C_int (Z_BEST_COMPRESSION), idio_zlib_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_DEFAULT_COMPRESSION"), idio_C_int (Z_DEFAULT_COMPRESSION), idio_zlib_module);

    /* compression strategies */
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_FILTERED"), idio_C_int (Z_FILTERED), idio_zlib_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_HUFFMAN_ONLY"), idio_C_int (Z_HUFFMAN_ONLY), idio_zlib_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_RLE"), idio_C_int (Z_RLE), idio_zlib_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_FIXED"), idio_C_int (Z_FIXED), idio_zlib_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("Z_DEFAULT_STRATEGY"), idio_C_int (Z_DEFAULT_STRATEGY), idio_zlib_module);
}
