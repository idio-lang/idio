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
 * libcurl-module.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <curl/curl.h>

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
#include "libcurl-module.h"
#include "libcurl-system.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"

IDIO idio_libcurl_module;

IDIO idio_condition_rt_libcurl_error_type;

IDIO_C_STRUCT_IDENT_DECL (libcurl_CURL);

IDIO idio_libcurl_error_string (char *format, va_list argp)
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

void idio_libcurl_error_printf (IDIO detail, char *format, ...)
{
    IDIO_ASSERT (detail);
    assert (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg = idio_libcurl_error_string (format, fmt_args);
    va_end (fmt_args);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_libcurl_error_type,
				   IDIO_LIST3 (msg,
					       location,
					       detail));

    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

size_t idio_libcurl_read_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    IDIO h = (IDIO) userdata;

    size_t sz = size * nmemb;
    size_t i;
    for (i = 0; i < sz; i++) {
	char c = idio_getb_handle (h);
	if (idio_eofp_handle (h)) {
	    return i;
	} else {
	    ptr[i] = c;
	}
    }

    return i;
}

size_t idio_libcurl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    return idio_puts_handle ((IDIO) userdata, ptr, size * nmemb);
}

IDIO_DEFINE_PRIMITIVE0_DS ("curl-version", curl_version, (), "", "\
Return the libcurl version	\n\
				\n\
:return: libcurl version	\n\
:rtype: string			\n\
")
{
    return idio_string_C (curl_version ());
}

IDIO_DEFINE_PRIMITIVE0_DS ("curl-easy-init", curl_easy_init, (), "", "\
Return a libcurl easy handle	\n\
				\n\
:return: :ref:`libcurl/CURL <libcurl/CURL>`	\n\
:rtype: C/pointer		\n\
:raises ^rt-libcurl-error:	\n\
")
{
    CURL *curl = curl_easy_init ();

    if (NULL == curl) {
	/*
	 * Test Case: ??
	 */
	idio_libcurl_error_printf (idio_S_nil, "curl_easy_init(): failed\n");

	return idio_S_notreached;
    }

    IDIO C_p = idio_C_pointer_type (idio_CSI_libcurl_CURL, curl);

    /*
     * The finalizer, curl_easy_cleanup(), frees this memory
     */
    IDIO_C_TYPE_POINTER_FREEP (C_p) = 0;

    idio_gc_register_finalizer (C_p, idio_libcurl_CURL_finalizer);
    return C_p;
}

typedef enum {
    IDIO_LIBCURL_SETOPT_READER,
    IDIO_LIBCURL_SETOPT_WRITER,
} idio_libcurl_setopt_enum_t;

/*
 * cf. STklos's set_transfer_port()
 */
void idio_libcurl_setopt_rw_data (CURL *curl, IDIO h, idio_libcurl_setopt_enum_t dir)
{
    IDIO_C_ASSERT (curl);
    IDIO_ASSERT (h);

    CURLoption data_opt, callback_opt;
    void *callback;

    switch (dir) {
    case IDIO_LIBCURL_SETOPT_READER:
	/*
	 * Test Case: libcurl-errors/curl-easy-setopt-bad-reader-type.idio
	 *
	 * curl-easy-setopt curl :reader #t
	 */
	IDIO_USER_TYPE_ASSERT (input_handle, h);

	data_opt = CURLOPT_READDATA;
	callback_opt = CURLOPT_READFUNCTION;
	callback = idio_libcurl_read_callback;
	break;
    case IDIO_LIBCURL_SETOPT_WRITER:
	/*
	 * Test Case: libcurl-errors/curl-easy-setopt-bad-writer-type.idio
	 *
	 * curl-easy-setopt curl :writer (current-input-handle)
	 */
	IDIO_USER_TYPE_ASSERT (output_handle, h);

	data_opt = CURLOPT_WRITEDATA;
	callback_opt = CURLOPT_WRITEFUNCTION;
	callback = idio_libcurl_write_callback;
	break;
    }

    CURLcode cc = curl_easy_setopt (curl, data_opt, (void *) h);

    if (CURLE_OK != cc) {
	/*
	 * Test Case: ??
	 */
	idio_libcurl_error_printf (idio_S_nil, "curl_easy_setopt(): %s\n", curl_easy_strerror (cc));

	/* notreached */
	return;
    }

    cc = curl_easy_setopt (curl, callback_opt, callback);

    if (CURLE_OK != cc) {
	/*
	 * Test Case: ??
	 */
	idio_libcurl_error_printf (idio_S_nil, "curl_easy_setopt(): %s\n", curl_easy_strerror (cc));

	/* notreached */
	return;
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("curl-easy-setopt", curl_easy_setopt, (IDIO curl, IDIO args), "curl [kw arg]+", "\
set libcurl easy options on `curl`	\n\
					\n\
:param curl: libcurl easy handle	\n\
:type curl: :ref:`libcurl/CURL <libcurl/CURL>`	\n\
:param kw: libcurl option		\n\
:type kw: keyword			\n\
:param arg: libcurl option argument	\n\
:type arg: see below			\n\
:return: ``#<unspec>``			\n\
:raises ^rt-libcurl-error:		\n\
:raises ^rt-libc-format-error: if `arg` contains an ASCII NUL for a string option	\n\
:raises ^rt-bignum-conversion-error: if `arg` exceeds limits for a long option	\n\
					\n\
`kw` and `arg` should be supplied as two arguments and any	\n\
number of `kw`/`arg` tuples can be passed.			\n\
					\n\
`kw` can be either :samp:`:CURLOPT_{name}` or :samp:`:{name}` for	\n\
some libcurl option :samp:`CURLOPT_{name}`.  :samp:`:{name}` is	\n\
case-insensitive.			\n\
					\n\
In addition `kw` can be :samp:`:reader` or :samp:`:writer` to	\n\
use input or output handles as source or sink for libcurl data.	\n\
")
{
    IDIO_ASSERT (curl);

    /*
     * Test Case: libcurl-errors/curl-easy-setopt-bad-curl-type.idio
     *
     * curl-easy-setopt #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, curl);
    if (idio_CSI_libcurl_CURL != IDIO_C_TYPE_POINTER_PTYPE (curl)) {
	/*
	 * Test Case: libcurl-errors/curl-easy-setopt-invalid-curl-type.idio
	 *
	 * curl-easy-setopt libc/NULL
	 */
	idio_error_param_value_exp ("curl-easy-setopt", "curl", curl, "libcurl/CURL", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    CURL *C_curl = IDIO_C_TYPE_POINTER_P (curl);

    while (idio_S_nil != args) {
	if (!(idio_isa_pair (args) &&
	      idio_isa_keyword (IDIO_PAIR_H (args)) &&
	      idio_isa_pair (IDIO_PAIR_T (args)))) {
	    /*
	     * Test Case: libcurl-errors/curl-easy-setopt-invalid-option-tuple.idio
	     *
	     * curl-easy-setopt curl #t
	     */
	    idio_error_param_value_exp ("curl-easy-setopt", "option tuple", args, "kw arg tuple", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	IDIO kw = IDIO_PAIR_H (args);
	IDIO arg = IDIO_PAIR_HT (args);

	/*
	 * keyword -> curl option
	 *
	 * without the leading CURLOPT_
	 */
	char *opt_prefix = "CURLOPT_";
	char *opt_orig = IDIO_KEYWORD_S (kw);
	char *opt_name = opt_orig;

	if (strncasecmp (opt_name, IDIO_STATIC_STR_LEN ("reader")) == 0 &&
	    6 == IDIO_KEYWORD_BLEN (kw)) {
	    idio_libcurl_setopt_rw_data (C_curl, arg, IDIO_LIBCURL_SETOPT_READER);
	} else if (strncasecmp (opt_name, IDIO_STATIC_STR_LEN ("writer")) == 0 &&
	    6 == IDIO_KEYWORD_BLEN (kw)) {
	    idio_libcurl_setopt_rw_data (C_curl, arg, IDIO_LIBCURL_SETOPT_WRITER);
	} else {
	    /*
	     * opt_prefix will used for reporting to truncate it of the
	     * opt_orig already has it
	     */
	    if (strncasecmp (opt_name, opt_prefix, 8) == 0) {
		opt_prefix = "";
		if (8 == IDIO_KEYWORD_BLEN (kw)) {
		    /*
		     * Test Case: libcurl-errors/curl-easy-setopt-short-option-name.idio
		     *
		     * curl-easy-setopt curl :CURLOPT_ #t
		     */
		    idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), "curl_easy_setopt (curl, %s, arg): short option name", opt_orig);

		    return idio_S_notreached;
		} else {
		    opt_name += 8;
		}
	    }

	    const struct curl_easyoption *opt = curl_easy_option_by_name (opt_name);

	    if (NULL == opt) {
		/*
		 * Test Case: libcurl-errors/curl-easy-setopt-invalid-option-name.idio
		 *
		 * curl-easy-setopt curl :foo #t
		 */
		idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), "curl_easy_setopt (curl, %s%s, arg): no such option", opt_prefix, opt_orig);

		return idio_S_notreached;
	    }

	    CURLcode cc;

	    switch (opt->type) {
	    case CURLOT_LONG:
		{
		    long C_arg;
		    char *emsg = "curl_easy_setopt (curl, %s%s, arg): long arg should be a boolean|integer|C/long";
		    switch (idio_type (arg)) {
		    case IDIO_TYPE_CONSTANT_IDIO:
			{
			    if (idio_S_true == arg) {
				C_arg = 1;
			    } else if (idio_S_false == arg) {
				C_arg = 0;
			    } else {
				/*
				 * Test Case: libcurl-errors/curl-easy-setopt-bad-long-constant-type.idio
				 *
				 * curl-easy-setopt curl :SSL_VERIFYPEER #n
				 */
				idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), emsg, opt_prefix, opt_orig);

				return idio_S_notreached;
			    }
			    cc = curl_easy_setopt (C_curl, opt->id, C_arg);
			}
			break;
		    case IDIO_TYPE_FIXNUM:
			{
			    C_arg = IDIO_FIXNUM_VAL (arg);
			    cc = curl_easy_setopt (C_curl, opt->id, C_arg);
			}
			break;
		    case IDIO_TYPE_BIGNUM:
			{
			    if (IDIO_BIGNUM_INTEGER_P (arg)) {
				C_arg = idio_bignum_long_value (arg);
			    } else {
				IDIO arg_i = idio_bignum_real_to_integer (arg);
				if (idio_S_nil == arg_i) {
				    /*
				     * Test Case: libcurl-errors/curl-easy-setopt-long-arg-float.idio
				     *
				     * curl-easy-setopt curl :SSL_VERIFYPEER 1.1
				     */
				    idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), emsg, opt_prefix, opt_orig);

				    return idio_S_notreached;
				} else {
				    /*
				     * Test Case: libcurl-errors/curl-easy-setopt-long-arg-too-large.idio
				     *
				     * 64-bit LONG_MAX + 1
				     * curl-easy-setopt curl :SSL_VERIFYPEER 9223372036854775808
				     */
				    C_arg = idio_bignum_long_value (arg_i);
				}
			    }

			    cc = curl_easy_setopt (C_curl, opt->id, C_arg);
			}
			break;
		    case IDIO_TYPE_C_LONG:
			{
			    C_arg = IDIO_C_TYPE_long (arg);
			    cc = curl_easy_setopt (C_curl, opt->id, C_arg);
			}
			break;
		    default:
			/*
			 * Test Case: libcurl-errors/curl-easy-setopt-bad-long-arg-type.idio
			 *
			 * curl-easy-setopt curl :SSL_VERIFYPEER "no"
			 */
			idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), emsg, opt_prefix, opt_orig);

			return idio_S_notreached;
		    }
		}
		break;
	    case CURLOT_STRING:
		{
		    switch (idio_type (arg)) {
		    case IDIO_TYPE_STRING:
		    case IDIO_TYPE_SUBSTRING:
			{
			    char *C_str_arg;
			    size_t free_C_str_arg = 0;

			    /*
			     * Test Case: libcurl-errors/curl-easy-setopt-bad-string-arg-format.idio
			     *
			     * curl-easy-setopt curl :URL "hello\x0world"
			     */
			    C_str_arg = idio_libc_string_C (arg, "arg", &free_C_str_arg, IDIO_C_FUNC_LOCATION ());
			    cc = curl_easy_setopt (C_curl, opt->id, C_str_arg);

			    if (free_C_str_arg) {
				idio_free (C_str_arg);
			    }
			}
			break;
		    default:
			/*
			 * Test Case: libcurl-errors/curl-easy-setopt-bad-string-arg-type.idio
			 *
			 * curl-easy-setopt curl :URL #t
			 */
			idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), "curl_easy_setopt (curl, %s%s, arg): arg should be a string", opt_prefix, opt_orig);

			return idio_S_notreached;
		    }
		}
		break;
	    default:
		/*
		 * Test Case: libcurl-errors/curl-easy-setopt-unhandled-option-type.idio ??
		 */
		idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), "curl_easy_setopt (curl, %s%s, arg): cannot handle option type %d", opt_prefix, opt_orig, opt->type);

		return idio_S_notreached;
	    }

	    if (CURLE_OK != cc) {
		/*
		 * Test Case: ??
		 */
		idio_libcurl_error_printf (IDIO_LIST2 (kw, arg), "curl_easy_setopt(): %s", curl_easy_strerror (cc));

		return idio_S_notreached;
	    }
	}

	args = IDIO_PAIR_TT (args);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("curl-easy-perform", curl_easy_perform, (IDIO curl), "curl", "\
perform libcurl easy transfer for `curl`	\n\
					\n\
:param curl: libcurl easy handle	\n\
:type curl: :ref:`libcurl/CURL <libcurl/CURL>`	\n\
:return: ``#<unspec>``			\n\
:raises ^rt-libcurl-error:		\n\
")
{
    IDIO_ASSERT (curl);

    /*
     * Test Case: libcurl-errors/curl-easy-perform-bad-curl-type.idio
     *
     * curl-easy-perform #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, curl);
    if (idio_CSI_libcurl_CURL != IDIO_C_TYPE_POINTER_PTYPE (curl)) {
	/*
	 * Test Case: libcurl-errors/curl-easy-perform-invalid-curl-type.idio
	 *
	 * curl-easy-perform libc/NULL
	 */
	idio_error_param_value_exp ("curl-easy-perform", "curl", curl, "libcurl/CURL", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    CURL *C_curl = IDIO_C_TYPE_POINTER_P (curl);

    CURLcode cc = curl_easy_perform (C_curl);

    if (CURLE_OK != cc) {
	/*
	 * Test Case: ??
	 */
	idio_libcurl_error_printf (idio_S_nil, "curl_easy_perform(): %s", curl_easy_strerror (cc));

	return idio_S_notreached;
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("curl-easy-reset", curl_easy_reset, (IDIO curl), "curl", "\
reset libcurl easy transfer for `curl`	\n\
					\n\
:param curl: libcurl easy handle	\n\
:type curl: :ref:`libcurl/CURL <libcurl/CURL>`	\n\
:return: ``#<unspec>``			\n\
:raises ^rt-libcurl-error:		\n\
")
{
    IDIO_ASSERT (curl);

    /*
     * Test Case: libcurl-errors/curl-easy-reset-bad-curl-type.idio
     *
     * curl-easy-reset #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, curl);
    if (idio_CSI_libcurl_CURL != IDIO_C_TYPE_POINTER_PTYPE (curl)) {
	/*
	 * Test Case: libcurl-errors/curl-easy-reset-invalid-curl-type.idio
	 *
	 * curl-easy-reset libc/NULL
	 */
	idio_error_param_value_exp ("curl-easy-reset", "curl", curl, "libcurl/CURL", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    CURL *C_curl = IDIO_C_TYPE_POINTER_P (curl);

    curl_easy_reset (C_curl);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("curl-easy-cleanup", curl_easy_cleanup, (IDIO curl), "curl", "\
cleanup libcurl easy transfer for `curl`	\n\
					\n\
:param curl: libcurl easy handle	\n\
:type curl: :ref:`libcurl/CURL <libcurl/CURL>`	\n\
:return: ``#<unspec>``			\n\
:raises ^rt-libcurl-error:		\n\
					\n\
In normal circumstances, :manpage:`curl_easy_cleanup(3)`	\n\
will be called by the garbage collector however you may		\n\
need to call ``curl-easy-cleanup`` directly.			\n\
					\n\
`curl` will be reset to a NULL pointer	\n\
")
{
    IDIO_ASSERT (curl);

    /*
     * Test Case: libcurl-errors/curl-easy-cleanup-bad-curl-type.idio
     *
     * curl-easy-cleanup #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, curl);
    if (idio_CSI_libcurl_CURL != IDIO_C_TYPE_POINTER_PTYPE (curl)) {
	/*
	 * Test Case: libcurl-errors/curl-easy-cleanup-invalid-curl-type.idio
	 *
	 * curl-easy-cleanup libc/NULL
	 */
	idio_error_param_value_exp ("curl-easy-cleanup", "curl", curl, "libcurl/CURL", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    CURL *C_curl = IDIO_C_TYPE_POINTER_P (curl);

    idio_gc_deregister_finalizer (curl);

    curl_easy_cleanup (C_curl);

    idio_invalidate_C_pointer (curl);

    return idio_S_unspec;
}

void idio_libcurl_CURL_finalizer (IDIO C_p)
{
    IDIO_ASSERT (C_p);
    IDIO_TYPE_ASSERT (C_pointer, C_p);

    CURL *curl = IDIO_C_TYPE_POINTER_P (C_p);

    curl_easy_cleanup (curl);
}

void idio_libcurl_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libcurl_module, curl_version);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libcurl_module, curl_easy_init);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libcurl_module, curl_easy_setopt);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libcurl_module, curl_easy_perform);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libcurl_module, curl_easy_reset);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libcurl_module, curl_easy_cleanup);
}

void idio_final_libcurl ()
{
    curl_global_cleanup ();
}

void idio_init_libcurl (void *handle)
{
    idio_libcurl_module = idio_module (IDIO_SYMBOL ("libcurl"));

    idio_module_table_register (idio_libcurl_add_primitives, idio_final_libcurl, handle);

    idio_module_export_symbol_value (idio_S_version,
				     idio_string_C_len (LIBCURL_SYSTEM_VERSION, sizeof (LIBCURL_SYSTEM_VERSION) - 1),
				     idio_libcurl_module);

    CURLcode cc = curl_global_init (CURL_GLOBAL_ALL);
    if (cc) {
	idio_libcurl_error_printf (idio_S_nil, "FATAL: curl_global_init(): %s", curl_easy_strerror (cc));

	/* notreached */
	exit (1);
    }

    IDIO struct_name = IDIO_SYMBOL ("libcurl/CURL");
    IDIO_C_STRUCT_IDENT_DEF (struct_name, idio_S_nil, libcurl_CURL, idio_fixnum0);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_libcurl_error_type, "^rt-libcurl-error", idio_condition_runtime_error_type);
}
