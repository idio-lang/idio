/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * c-ffi.c
 *
 */

#include "idio.h"

ffi_type *idio_C_FFI_type (IDIO field_data)
{
    IDIO type = idio_array_ref_index (field_data, IDIO_C_FIELD_DATA_TYPE);

    if (idio_S_nil == type) {
	return &ffi_type_void;
    }

    switch (IDIO_C_TYPE_UINT (type)) {
    case IDIO_TYPE_C_INT8_T:
	return &ffi_type_sint8;
    case IDIO_TYPE_C_UINT8_T:
	return &ffi_type_uint8;
    case IDIO_TYPE_C_INT16_T:
	return &ffi_type_sint16;
    case IDIO_TYPE_C_UINT16_T:
	return &ffi_type_uint16;
    case IDIO_TYPE_C_INT32_T:
	return &ffi_type_sint32;
    case IDIO_TYPE_C_UINT32_T:
	return &ffi_type_uint32;
    case IDIO_TYPE_C_INT64_T:
	return &ffi_type_sint64;
    case IDIO_TYPE_C_UINT64_T:
	return &ffi_type_uint64;
    case IDIO_TYPE_C_FLOAT:
	return &ffi_type_float;
    case IDIO_TYPE_C_DOUBLE:
	return &ffi_type_double;
    case IDIO_TYPE_C_POINTER:
	return &ffi_type_pointer;
    case IDIO_TYPE_STRING:
	return &ffi_type_pointer;
    default:
	{
	    char em[BUFSIZ];
	    sprintf (em, "unexpected C_FFI type %ju: %s", IDIO_C_TYPE_UINT (type), idio_type2string (type));
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), em);

	    /* notreached */
	    return NULL;
	}
	break;
    }

    return NULL;
}

ffi_type **idio_C_FFI_ffi_arg_types (size_t nargs, IDIO args)
{
    if (0 == nargs) {
	return NULL;
    }

    ffi_type **arg_types = idio_alloc (nargs * sizeof (ffi_type *));
    size_t i;
    for (i = 0; i < nargs; i++) {
	arg_types[i] = idio_C_FFI_type (idio_array_ref_index (args, i));
    }

    return arg_types;
}

IDIO idio_C_FFI (IDIO symbol, IDIO arg_types, IDIO result_type)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (arg_types);
    IDIO_ASSERT (result_type);

    IDIO o = idio_gc_get (IDIO_TYPE_C_FFI);

    IDIO_TYPE_ASSERT (opaque, symbol);
    IDIO_TYPE_ASSERT (list, arg_types);

    IDIO_GC_ALLOC (o->u.C_FFI, sizeof (idio_C_FFI_t));

    size_t nargs = 0;
    IDIO a = arg_types;
    while (idio_S_nil != a) {
	nargs++;
	a = idio_list_tail (a);
    }

    IDIO args = idio_array (nargs);
    a = arg_types;
    while (idio_S_nil != a) {
	idio_array_push (args, idio_list_head (a));
	a = idio_list_tail (a);
    }

    IDIO_C_FFI_SYMBOL (o) = symbol;
    IDIO_C_FFI_NAME (o) = idio_C_fields_array (arg_types);
    IDIO_C_FFI_RESULT (o) = idio_C_fields_array (idio_pair (result_type, idio_S_nil));
    IDIO_C_FFI_NAME (o) = symbol->u.opaque->args;

    IDIO_C_FFI_NARGS (o) = nargs;
    IDIO_C_FFI_ARG_TYPES (o) = idio_C_FFI_ffi_arg_types (nargs, IDIO_C_FFI_ARGS (o));

    IDIO result_field_data = idio_array_ref_index (IDIO_C_FFI_RESULT (o), 0);
    IDIO_C_FFI_RTYPE (o) = idio_C_FFI_type (result_field_data);

    IDIO_C_FFI_CIFP (o) = idio_alloc (sizeof (ffi_cif));

    ffi_status s = ffi_prep_cif (IDIO_C_FFI_CIFP (o), FFI_DEFAULT_ABI, nargs, IDIO_C_FFI_RTYPE (o), IDIO_C_FFI_ARG_TYPES (o));
    if (s != FFI_OK) {
	char em[BUFSIZ];
	sprintf (em, "ffi_prep_cif failed");
	idio_error_printf (IDIO_C_FUNC_LOCATION (), em);

	return idio_S_notreached;
    }

    return o;
}

int idio_isa_C_FFI (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_C_FFI);
}

void idio_free_C_FFI (IDIO o)
{
    IDIO_ASSERT (o);
    IDIO_TYPE_ASSERT (C_FFI, o);

    idio_gc_stats_free (sizeof (idio_C_FFI_t));

    IDIO_GC_FREE (IDIO_C_FFI_CIFP (o));
    IDIO_GC_FREE (IDIO_C_FFI_ARG_TYPES (o));
    IDIO_GC_FREE (o->u.C_FFI);
}

