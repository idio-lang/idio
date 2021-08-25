/*
 * Copyright (c) 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * json5-api.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <assert.h>
#include <ffi.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"

#include "array.h"
#include "bignum.h"
#include "c-type.h"
#include "error.h"
#include "evaluate.h"
#include "file-handle.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio.h"
#include "module.h"
#include "pair.h"
#include "idio-string.h"
#include "string-handle.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"

#include "json5-module.h"
#include "json5-unicode.h"
#include "json5-token.h"
#include "json5-parser.h"

#include "json5-api.h"

IDIO idio_json5_string_value_to_idio (json5_unicode_string_t *js)
{
    IDIO_C_ASSERT (js);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);
    IDIO_STRING_LEN (so) = js->len;

    size_t reqd_bytes = js->len;
    switch (js->width) {
    case JSON5_UNICODE_STRING_WIDTH_1BYTE:
	IDIO_STRING_FLAGS (so) = IDIO_STRING_FLAG_1BYTE;
	reqd_bytes *= 1;
	break;
    case JSON5_UNICODE_STRING_WIDTH_2BYTE:
	IDIO_STRING_FLAGS (so) = IDIO_STRING_FLAG_2BYTE;
	reqd_bytes *= 2;
	break;
    case JSON5_UNICODE_STRING_WIDTH_4BYTE:
	IDIO_STRING_FLAGS (so) = IDIO_STRING_FLAG_4BYTE;
	reqd_bytes *= 4;
	break;
    default:
	json5_error_printf ("JSON5-string-to-IDIO: unexpected s width: %#x", js->width);

	return idio_S_notreached;
    }

    IDIO_GC_ALLOC (IDIO_STRING_S (so), reqd_bytes);
    IDIO_STRING_BLEN (so) = reqd_bytes;

    memcpy (IDIO_STRING_S (so), js->s, reqd_bytes);

    return so;
}

json5_unicode_string_t *idio_string_to_json5_string_value (IDIO is)
{
    IDIO_ASSERT (is);

    IDIO_TYPE_ASSERT (string, is);

    json5_unicode_string_t *so = (json5_unicode_string_t *) malloc (sizeof (json5_unicode_string_t));

    size_t reqd_bytes = idio_string_len (is);
    so->len = reqd_bytes;

    switch (IDIO_STRING_FLAGS (is)) {
    case IDIO_STRING_FLAG_1BYTE:
	so->width = JSON5_UNICODE_STRING_WIDTH_1BYTE;
	reqd_bytes *= 1;
	break;
    case IDIO_STRING_FLAG_2BYTE:
	so->width = JSON5_UNICODE_STRING_WIDTH_2BYTE;
	reqd_bytes *= 2;
	break;
    case IDIO_STRING_FLAG_4BYTE:
	so->width = JSON5_UNICODE_STRING_WIDTH_4BYTE;
	reqd_bytes *= 4;
	break;
    default:
	json5_error_printf ("JSON5-string-to-IDIO: unexpected s flags: %#x", IDIO_STRING_FLAGS (is));

	return NULL;
    }

    so->s = (char *) malloc (reqd_bytes);

    memcpy (so->s, IDIO_STRING_S (is), reqd_bytes);

    return so;
}

int idio_print_value_as_json (IDIO v, IDIO oh, int json5, int depth);

void idio_print_value_as_json_indent (IDIO oh, int depth)
{
    if (depth < 1) {
	return;
    }

    for (int i = 0; i < depth; i++) {
	idio_display_C ("  ", oh);
    }
}

void idio_print_bignum_as_json (IDIO v, IDIO oh, int json5, int depth)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (bignum, a);

    size_t size = 0;
    char *bs = idio_bignum_as_string (v, &size);

    /*
     * The stock Idio bignum format might have a leading #i
     */
    if ('#' == bs[0]) {
	idio_display_C (bs + 2, oh);
    } else {
	idio_display_C (bs, oh);
    }

    IDIO_GC_FREE (bs);
}

int idio_print_array_as_json (IDIO a, IDIO oh, int json5, int depth)
{
    IDIO_ASSERT (a);

    IDIO_TYPE_ASSERT (array, a);

    int printed = 0;
    idio_display_C ("[", oh);

    idio_ai_t al = IDIO_ARRAY_USIZE (a);
    idio_ai_t ai;

    for (ai = 0 ; ai < al; ai++) {
	if (printed) {
	    idio_display_C (",", oh);
	} else {
	    printed = 1;
	}
	idio_display_C ("\n", oh);
	idio_print_value_as_json_indent (oh, depth);

	IDIO e = idio_array_ref_index (a, ai);
	IDIO_ASSERT (e);

	int er = idio_print_value_as_json (e, oh, json5, depth);

	if (0 == er) {
	    return 0;
	}
    }
    if (printed) {
	idio_display_C ("\n", oh);
	idio_print_value_as_json_indent (oh, depth - 1);
    }
    idio_display_C ("]", oh);

    return 1;
}

int idio_print_hash_as_json (IDIO h, IDIO oh, int json5, int depth)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    int printed = 0;
    idio_display_C ("{", oh);

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	idio_hash_entry_t *he = IDIO_HASH_HA (h, i);
	for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	    IDIO k = IDIO_HASH_HE_KEY (he);
	    if (idio_isa_string (k)) {
		if (printed) {
		    idio_display_C (",", oh);
		} else {
		    printed = 1;
		}
		idio_display_C ("\n", oh);
		idio_print_value_as_json_indent (oh, depth);

		int kr = idio_print_value_as_json (k, oh, json5, depth);

		if (0 == kr) {
		    return 0;
		}

		idio_display_C (": ", oh);

		int vr = idio_print_value_as_json (IDIO_HASH_HE_VALUE (he), oh, json5, depth + 1);

		if (0 == vr) {
		    return 0;
		}
	    } else {
		return 0;
	    }
	}
    }
    if (printed) {
	idio_display_C ("\n", oh);
	idio_print_value_as_json_indent (oh, depth - 1);
    }
    idio_display_C ("}", oh);

    return 1;
}

int idio_print_value_as_json (IDIO v, IDIO oh, int json5, int depth)
{
    IDIO_ASSERT (v);

    int r = 1;

    switch ((intptr_t) v & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	idio_display (v, oh);
	break;
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) v & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		{
		    intptr_t ci = IDIO_CONSTANT_IDIO_VAL (v);

		    switch (ci) {
		    case IDIO_CONSTANT_NIL:
			idio_display_C ("null", oh);
			break;
		    case IDIO_CONSTANT_TRUE:
			idio_display_C ("true", oh);
			break;
		    case IDIO_CONSTANT_FALSE:
			idio_display_C ("false", oh);
			break;
		    default:
			return 0;
		    }

		}
		break;
	    default:
		return 0;
	    }
	}
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (v->type) {
	    case IDIO_TYPE_STRING:
	    case IDIO_TYPE_SUBSTRING:
		{
		    size_t size = 0;
		    idio_display_C (idio_utf8_string (v, &size, IDIO_UTF8_STRING_ESCAPES, IDIO_UTF8_STRING_QUOTED, IDIO_UTF8_STRING_USEPREC), oh);
		}
		break;
	    case IDIO_TYPE_SYMBOL:
		{
		    if (json5 &&
			(idio_json5_literal_value_Infinity_sym == v ||
			 idio_json5_literal_value_pos_Infinity_sym == v ||
			 idio_json5_literal_value_neg_Infinity_sym == v ||
			 idio_json5_literal_value_NaN_sym == v ||
			 idio_json5_literal_value_pos_NaN_sym == v ||
			 idio_json5_literal_value_neg_NaN_sym == v)) {
			idio_display (v, oh);
		    } else {
			return 0;
		    }
		}
		break;
	    case IDIO_TYPE_ARRAY:
		return idio_print_array_as_json (v, oh, json5, depth + 1);
	    case IDIO_TYPE_HASH:
		return idio_print_hash_as_json (v, oh, json5, depth + 1);
	    case IDIO_TYPE_BIGNUM:
		idio_print_bignum_as_json (v, oh, json5, depth);
		break;
	    default:
		return 0;
	    }
	}
	break;
    default:
	return 0;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("generate", json5_generate, (IDIO v, IDIO args), "v [handle]", "\
generate the JSON5 UTF-8 for ``v``	\n\
					\n\
:param v: value				\n\
:type v: JSON5 compatible value		\n\
:param handle: output handle		\n\
:type handle: handle			\n\
:return: JSON5 representation of ``v``	\n\
:rtype: string / #unspec		\n\
					\n\
See also ``json5/generate-json``	\n\
")
{
    IDIO_ASSERT (v);

    IDIO sh = idio_open_output_string_handle_C ();

    if (idio_print_value_as_json (v, sh, 1, 0)) {
	IDIO s = idio_get_output_string (sh);

	if (idio_isa_pair (args)) {
	    IDIO oh = IDIO_PAIR_H (args);

	    IDIO_USER_TYPE_ASSERT (handle, oh);
	    return idio_display (s, oh);
	} else {
	    return s;
	}
    } else {
	idio_error_param_value_exp ("json5/generate", "v", v, "JSON5-compatible value", IDIO_C_FUNC_LOCATION ());
	
	return idio_S_notreached;
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("generate-json", json5_generate_json, (IDIO v, IDIO args), "v [handle]", "\
generate the JSON UTF-8 for ``v``	\n\
					\n\
:param v: value				\n\
:type v: JSON compatible value		\n\
:param handle: output handle		\n\
:type handle: handle			\n\
:return: JSON representation of ``v``	\n\
:rtype: string / #unspec		\n\
					\n\
See also ``json5/generate``		\n\
")
{
    IDIO_ASSERT (v);

    IDIO sh = idio_open_output_string_handle_C ();

    if (idio_print_value_as_json (v, sh, 0, 0)) {
	IDIO s = idio_get_output_string (sh);

	if (idio_isa_pair (args)) {
	    IDIO oh = IDIO_PAIR_H (args);

	    IDIO_USER_TYPE_ASSERT (handle, oh);
	    return idio_display (s, oh);
	} else {
	    return s;
	}
    } else {
	idio_error_param_value_exp ("json5/generate-json", "v", v, "JSON-compatible value", IDIO_C_FUNC_LOCATION ());
	
	return idio_S_notreached;
    }
}

IDIO idio_json5_value_to_idio (json5_value_t *v);

IDIO idio_json5_array_value_to_idio (json5_array_t *ja)
{
    if (NULL == ja) {
	return idio_array (0);
    }

    size_t n = 0;
    json5_array_t *oja = ja;
    for (; NULL != ja;) {
	n++;
	ja = ja->next;
    }
    
    IDIO ia = idio_array (n);

    n = 0;
    for (ja = oja; NULL != ja;) {
	idio_array_insert_index (ia, idio_json5_value_to_idio (ja->element), n++);
	ja = ja->next;
    }

    return ia;
}

IDIO idio_json5_object_value_to_idio (json5_object_t *o)
{
    if (NULL == o) {
	return IDIO_HASH_EQUALP (0);
    }

    size_t n = 0;
    json5_object_t *oo = o;
    for (; NULL != o;) {
	n++;
	o = o->next;
    }
    
    IDIO ht = IDIO_HASH_EQUALP (n);

    for (o = oo; NULL != o;) {
	IDIO k = idio_S_nil;

	switch (o->type) {
	case JSON5_MEMBER_IDENTIFIER:
	    k = idio_json5_string_value_to_idio (o->name);
	    break;
	case JSON5_MEMBER_STRING:
	    k = idio_json5_string_value_to_idio (o->name);
	    break;
	default:
	    json5_error_printf ("?member %d?", o->type);

	    return idio_S_notreached;
	}

	idio_hash_put (ht, k, idio_json5_value_to_idio (o->value));

	o = o->next;
    }

    return ht;
}

IDIO idio_json5_value_to_idio (json5_value_t *v)
{
    if (NULL == v) {
	json5_error_printf ("_value: NULL?");

	return idio_S_notreached;
    }

    IDIO r = idio_S_unspec;

    switch (v->type) {
    case JSON5_VALUE_NULL:
	r = idio_S_nil;
	break;
    case JSON5_VALUE_BOOLEAN:
	switch (v->u.l) {
	case JSON5_LITERAL_NULL:
	    /* shouldn't get here */
	    r = idio_S_nil;
	    break;
	case JSON5_LITERAL_TRUE:
	    r = idio_S_true;
	    break;
	case JSON5_LITERAL_FALSE:
	    r = idio_S_false;
	    break;
	default:
	    json5_error_printf ("?literal %d?", v->u.l);

	    return idio_S_notreached;
	}
	break;
    case JSON5_VALUE_STRING:
	r = idio_json5_string_value_to_idio (v->u.s);
	break;
    case JSON5_VALUE_NUMBER:
	switch (v->u.n->type) {
	case JSON5_NUMBER_INFINITY:
	    r = idio_json5_literal_value_Infinity_sym;
	    break;
	case JSON5_NUMBER_NEG_INFINITY:
	    r = idio_json5_literal_value_neg_Infinity_sym;
	    break;
	case JSON5_NUMBER_NAN:
	    r = idio_json5_literal_value_NaN_sym;
	    break;
	case JSON5_NUMBER_NEG_NAN:
	    r = idio_json5_literal_value_neg_NaN_sym;
	    break;
	case ECMA_NUMBER_INTEGER:
	    r = idio_integer (v->u.n->u.i);
	    break;
	case ECMA_NUMBER_FLOAT:
	    r = idio_bignum_double (v->u.n->u.f);
	    break;
	default:
	    json5_error_printf ("?number %d? ", v->u.n->type);

	    return idio_S_notreached;
	}
	break;
    case JSON5_VALUE_OBJECT:
	r = idio_json5_object_value_to_idio (v->u.o);
	break;
    case JSON5_VALUE_ARRAY:
	r = idio_json5_array_value_to_idio (v->u.a);
	break;
    default:
	json5_error_printf ("?value %d?", v->type);

	return idio_S_notreached;
    }

    return r;
}

IDIO idio_json5_parse_string (IDIO s)
{
    json5_unicode_string_t *js = idio_string_to_json5_string_value (s);

    json5_value_t *v = json5_parse_string (js);

    IDIO r = idio_json5_value_to_idio (v);

    json5_value_free (v);

    free (js->s);
    free (js);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("parse-string", json5_parse_string, (IDIO string), "string", "\
parse the JSON5 in ``string``		\n\
					\n\
:param string: string			\n\
:type string: string			\n\
:return: value				\n\
:rtype: any				\n\
")
{
    IDIO_ASSERT (string);

    IDIO_USER_TYPE_ASSERT (string, string);

    return idio_json5_parse_string (string);
}

IDIO idio_json5_parse_fd (int fd)
{
    json5_value_t *v = json5_parse_fd (fd);

    IDIO r = idio_json5_value_to_idio (v);

    json5_value_free (v);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("parse-fd", json5_parse_fd, (IDIO fd), "fd", "\
parse the JSON5 in ``fd``		\n\
					\n\
:param fd: fd				\n\
:type fd: file descriptor		\n\
:return: value				\n\
:rtype: any				\n\
")
{
    IDIO_ASSERT (fd);

    IDIO_USER_C_TYPE_ASSERT (int, fd);

    return idio_json5_parse_fd (IDIO_C_TYPE_int (fd));
}

IDIO_DEFINE_PRIMITIVE1_DS ("parse-file", json5_parse_file, (IDIO file), "file", "\
parse the JSON5 in ``file``		\n\
					\n\
:param file: file			\n\
:type file: pathname or string		\n\
:return: value				\n\
:rtype: any				\n\
")
{
    IDIO_ASSERT (file);

    IDIO_USER_TYPE_ASSERT (string, file);

    IDIO fh = idio_file_handle_open_file ("JSON5/parse-file", file, idio_S_nil, IDIO_MODE_RE, sizeof (IDIO_MODE_RE) - 1);

    IDIO r = idio_json5_parse_fd (IDIO_FILE_HANDLE_FD (fh));

    idio_close_file_handle (fh);

    return r;
}

void idio_json5_api_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_json5_module, json5_generate);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_json5_module, json5_generate_json);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_json5_module, json5_parse_string);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_json5_module, json5_parse_fd);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_json5_module, json5_parse_file);
}

