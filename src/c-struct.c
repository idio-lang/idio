/*
 * Copyright (c) 2015, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * c-struct.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <ffi.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "c-struct.h"
#include "c-type.h"
#include "error.h"
#include "hash.h"
#include "idio-string.h"
#include "pair.h"
#include "symbol.h"
#include "util.h"

IDIO idio_CTD_int8 = NULL;
IDIO idio_CTD_uint8 = NULL;
IDIO idio_CTD_int16 = NULL;
IDIO idio_CTD_uint16 = NULL;
IDIO idio_CTD_int32 = NULL;
IDIO idio_CTD_uint32 = NULL;
IDIO idio_CTD_int64 = NULL;
IDIO idio_CTD_uint64 = NULL;
IDIO idio_CTD_float = NULL;
IDIO idio_CTD_double = NULL;
IDIO idio_CTD_asterisk = NULL;
IDIO idio_CTD_string = NULL;

IDIO idio_CTD_short = NULL;
IDIO idio_CTD_ushort = NULL;
IDIO idio_CTD_char = NULL;
IDIO idio_CTD_uchar = NULL;
IDIO idio_CTD_int = NULL;
IDIO idio_CTD_uint = NULL;
IDIO idio_CTD_long = NULL;
IDIO idio_CTD_ulong = NULL;

static IDIO idio_C_typedefs_hash = idio_S_nil;

IDIO idio_C_typedef (IDIO sym)
{
    IDIO_ASSERT (sym);

    IDIO_TYPE_ASSERT (symbol, sym);

    IDIO o = idio_gc_get (IDIO_TYPE_C_TYPEDEF);

    IDIO_GC_ALLOC (o->u.C_typedef, sizeof (idio_C_typedef_t));

    IDIO_C_TYPEDEF_SYM (o) = sym;

    return o;
}

int idio_isa_C_typedef (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_C_TYPEDEF);
}

void idio_free_C_typedef (IDIO o)
{
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (C_typedef, o);

    idio_gc_stats_free (sizeof (idio_C_typedef_t));

    IDIO_GC_FREE (o->u.C_typedef);
}

int idio_C_typedef_type_cmp (IDIO C_typedef, IDIO val)
{
    IDIO_ASSERT (C_typedef);

    int type = idio_type (val);

    switch (type) {
    case IDIO_TYPE_C_INT8_T: return (C_typedef == idio_CTD_int8);
    case IDIO_TYPE_C_UINT8_T: return (C_typedef == idio_CTD_uint8);
    case IDIO_TYPE_C_INT16_T: return (C_typedef == idio_CTD_int16);
    case IDIO_TYPE_C_UINT16_T: return (C_typedef == idio_CTD_uint16);
    case IDIO_TYPE_C_INT32_T: return (C_typedef == idio_CTD_int32);
    case IDIO_TYPE_C_UINT32_T: return (C_typedef == idio_CTD_uint32);
    case IDIO_TYPE_C_INT64_T: return (C_typedef == idio_CTD_int64);
    case IDIO_TYPE_C_UINT64_T: return (C_typedef == idio_CTD_uint64);
    case IDIO_TYPE_C_FLOAT: return (C_typedef == idio_CTD_float);
    case IDIO_TYPE_C_DOUBLE: return (C_typedef == idio_CTD_double);
    case IDIO_TYPE_C_POINTER: return (C_typedef == idio_CTD_asterisk);
    case IDIO_TYPE_STRING: return (C_typedef == idio_CTD_string);
    }

    return 0;
}

IDIO idio_C_typedefs_get (IDIO s)
{
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    return idio_hash_ref (idio_C_typedefs_hash, s);
}

IDIO idio_C_typedefs_exists (IDIO s)
{
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    return idio_hash_exists (idio_C_typedefs_hash, s);
}

IDIO idio_C_typedefs_add_value (IDIO s, IDIO v)
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (symbol, s);

    IDIO r = idio_C_typedefs_get (s);

    if (idio_S_unspec == r) {
	if (idio_S_nil != v) {
	    r = idio_C_typedefs_get (v);
	    if (idio_S_unspec == r) {
		idio_error_C ("target C_typedef does not exist", v, IDIO_C_FUNC_LOCATION ());
	    }
	}
	r = idio_hash_put (idio_C_typedefs_hash, s, v);
    }

    return r;
}

IDIO idio_C_typedefs_add (IDIO s)
{
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    return idio_C_typedefs_add_value (s, idio_S_nil);
}

IDIO idio_resolve_C_typedef (IDIO ctd)
{
    IDIO_ASSERT (ctd);

    /*
      time_t -> slong -> int32/int64
     */

    IDIO ctdv = idio_C_typedefs_get (ctd);
    if (idio_S_nil == ctdv) {
	return ctd;
    } else {
	return idio_resolve_C_typedef (ctdv);
    }
}

IDIO idio_C_fields_array (IDIO C_typedefs)
{
    IDIO_ASSERT (C_typedefs);

    IDIO_TYPE_ASSERT (pair, C_typedefs);

    int sc = 0;
    IDIO sp = C_typedefs;
    while (idio_S_nil != sp) {
	sc++;
	sp = idio_list_tail (sp);
    }

    IDIO fields_array = idio_array (sc);

    size_t offset = 0;
    while (idio_S_nil != C_typedefs) {
	IDIO C_typedef = idio_list_head (C_typedefs);

	IDIO field_data = idio_array (IDIO_C_FIELD_DATA_MAX);
	idio_array_push (field_data, C_typedef);

	size_t alignment;
	size_t size;
	size_t type;

	int nelem = 1;
	if (idio_isa_pair (C_typedef)) {
	    IDIO v1 = idio_list_head (idio_list_tail (C_typedef));
	    IDIO v2 = idio_C_number_cast (v1, IDIO_TYPE_C_UINT);
	    nelem = IDIO_C_TYPE_uint (v2);

	    C_typedef = idio_list_head (C_typedef);
	}

	IDIO base_C_typedef = idio_resolve_C_typedef (C_typedef);

	if (base_C_typedef == idio_CTD_int8) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int8_t);
	    size = nelem * sizeof (int8_t);
	    if (nelem > 1) {
		type = IDIO_TYPE_STRING;
	    } else {
		type = IDIO_TYPE_C_INT8_T;
	    }
	} else if (base_C_typedef == idio_CTD_uint8) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint8_t);
	    size = nelem * sizeof (uint8_t);
	    type = IDIO_TYPE_C_UINT8_T;
	} else if (base_C_typedef == idio_CTD_int16) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int16_t);
	    size = nelem * sizeof (int16_t);
	    type = IDIO_TYPE_C_INT16_T;
	} else if (base_C_typedef == idio_CTD_uint16) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint16_t);
	    size = nelem * sizeof (int16_t);
	    type = IDIO_TYPE_C_UINT16_T;
	} else if (base_C_typedef == idio_CTD_int32) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int32_t);
	    size = nelem * sizeof (int32_t);
	    type = IDIO_TYPE_C_INT32_T;
	} else if (base_C_typedef == idio_CTD_uint32) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint32_t);
	    size = nelem * sizeof (int32_t);
	    type = IDIO_TYPE_C_UINT32_T;
	} else if (base_C_typedef == idio_CTD_int64) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int64_t);
	    size = nelem * sizeof (int64_t);
	    type = IDIO_TYPE_C_INT64_T;
	} else if (base_C_typedef == idio_CTD_uint64) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint64_t);
	    size = nelem * sizeof (int64_t);
	    type = IDIO_TYPE_C_UINT64_T;
	} else if (base_C_typedef == idio_CTD_float) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (float);
	    size = nelem * sizeof (float);
	    type = IDIO_TYPE_C_FLOAT;
	} else if (base_C_typedef == idio_CTD_double) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (double);
	    size = nelem * sizeof (double);
	    type = IDIO_TYPE_C_DOUBLE;
	} else if (base_C_typedef == idio_CTD_asterisk) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (void*);
	    size = nelem * sizeof (void*);
	    type = IDIO_TYPE_C_POINTER;
	} else if (base_C_typedef == idio_CTD_string) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (char*);
	    size = nelem * sizeof (char*);
	    type = IDIO_TYPE_STRING;
	} else {
	    idio_error_C ("unexpected C typedef", IDIO_LIST2 (C_typedef, base_C_typedef), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/* add any alignment required */
	offset += offset % alignment;

	idio_array_push (field_data, idio_C_uint (alignment));
	idio_array_push (field_data, idio_C_uint (type));
	idio_array_push (field_data, idio_C_uint (offset));
	idio_array_push (field_data, idio_C_uint (size));
	idio_array_push (field_data, idio_C_uint (nelem));

	idio_array_push (fields_array, field_data);

	offset += size;

	C_typedefs = idio_list_tail (C_typedefs);
    }

    return fields_array;
}

size_t idio_sizeof_C_struct (IDIO fields_array)
{
    IDIO_ASSERT (fields_array);

    size_t r = 0;

    size_t sas = idio_array_size (fields_array);

    IDIO field_data = idio_array_ref_index (fields_array, sas - 1);
    IDIO offset = idio_array_ref_index (field_data, IDIO_C_FIELD_DATA_OFFSET);
    IDIO size = idio_array_ref_index (field_data, IDIO_C_FIELD_DATA_SIZE);

    r = IDIO_C_TYPE_int (offset) + IDIO_C_TYPE_uint (size);

    return r;
}

IDIO idio_C_struct (IDIO fields_array, IDIO methods, IDIO frame)
{
    IDIO_ASSERT (fields_array);
    IDIO_ASSERT (methods);
    IDIO_ASSERT (frame);

    IDIO cs = idio_gc_get (IDIO_TYPE_C_STRUCT);

    IDIO_GC_ALLOC (cs->u.C_struct, sizeof (idio_C_struct_t));

    IDIO_C_STRUCT_GREY (cs) = NULL;
    IDIO_C_STRUCT_FIELDS (cs) = fields_array;
    IDIO_C_STRUCT_METHODS (cs) = methods;
    IDIO_C_STRUCT_FRAME (cs) = frame;
    IDIO_C_STRUCT_SIZE (cs) = idio_sizeof_C_struct (fields_array);

    return cs;
}

int idio_isa_C_struct (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_C_STRUCT);
}

void idio_free_C_struct (IDIO cs)
{
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (C_struct, cs);

    idio_gc_stats_free (sizeof (idio_C_struct_t));

    IDIO_GC_FREE (cs->u.C_struct);
}

IDIO idio_C_instance (IDIO cs, IDIO frame)
{
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (C_struct, cs);
    IDIO_ASSERT (frame);

    IDIO ci = idio_gc_get (IDIO_TYPE_C_INSTANCE);

    IDIO_GC_ALLOC (ci->u.C_instance, sizeof (idio_C_instance_t));
    IDIO_GC_ALLOC (IDIO_C_INSTANCE_P (ci), IDIO_C_STRUCT_SIZE (cs));

    IDIO_C_INSTANCE_GREY (ci) = NULL;
    IDIO_C_INSTANCE_C_STRUCT (ci) = cs;
    IDIO_C_INSTANCE_FRAME (ci) = frame;

    return ci;
}

int idio_isa_C_instance (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_C_INSTANCE);
}

void idio_free_C_instance (IDIO ci)
{
    IDIO_ASSERT (ci);
    IDIO_TYPE_ASSERT (C_instance, ci);

    idio_gc_stats_free (sizeof (idio_C_instance_t));

    IDIO_GC_FREE (ci->u.C_instance->p);
    IDIO_GC_FREE (ci->u.C_instance);
}

IDIO idio_opaque (void *p)
{
    IDIO_C_ASSERT (p);

    return idio_opaque_final (p, NULL, idio_S_nil);
}

IDIO idio_opaque_args (void *p, IDIO args)
{
    IDIO_C_ASSERT (p);
    IDIO_ASSERT (args);

    return idio_opaque_final (p, NULL, args);
}

IDIO idio_opaque_final (void *p, void (*func) (IDIO o), IDIO args)
{
    IDIO_C_ASSERT (p);

    IDIO o = idio_gc_get (IDIO_TYPE_OPAQUE);

    IDIO_GC_ALLOC (o->u.opaque, sizeof (idio_opaque_t));

    IDIO_OPAQUE_P (o) = p;
    IDIO_OPAQUE_ARGS (o) = args;

    idio_gc_register_finalizer (o, func);

    return o;
}

int idio_isa_opaque (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_OPAQUE);
}

void idio_free_opaque (IDIO o)
{
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (opaque, o);

    idio_gc_stats_free (sizeof (idio_opaque_t));

    IDIO_GC_FREE (o->u.opaque);
}

void idio_init_C_struct ()
{
    /*
     * XXX nothing to do here...
     */
    idio_module_table_register (NULL, NULL);

    idio_C_typedefs_hash = IDIO_HASH_EQP (1<<6);
    idio_gc_protect_auto (idio_C_typedefs_hash);

    IDIO_C_TYPEDEF_ADD (int8);
    IDIO_C_TYPEDEF_ADD (uint8);
    IDIO_C_TYPEDEF_ADD (int16);
    IDIO_C_TYPEDEF_ADD (uint16);
    IDIO_C_TYPEDEF_ADD (int32);
    IDIO_C_TYPEDEF_ADD (uint32);
    IDIO_C_TYPEDEF_ADD (int64);
    IDIO_C_TYPEDEF_ADD (uint64);
    IDIO_C_TYPEDEF_ADD (float);
    IDIO_C_TYPEDEF_ADD (double);
    {
	IDIO sym = idio_symbols_C_intern ("*");
	idio_CTD_asterisk = idio_C_typedefs_add (sym);
    }
    IDIO_C_TYPEDEF_ADD (string);

    IDIO_C_TYPEDEF_ADD_VALUE (char, int8);
    IDIO_C_TYPEDEF_ADD_VALUE (uchar, uint8);
    IDIO_C_TYPEDEF_ADD_VALUE (short, int16);
    IDIO_C_TYPEDEF_ADD_VALUE (ushort, uint16);
    IDIO_C_TYPEDEF_ADD_VALUE (int, int32);
    IDIO_C_TYPEDEF_ADD_VALUE (uint, uint32);
#ifdef __LP64__
    IDIO_C_TYPEDEF_ADD_VALUE (long, int64);
    IDIO_C_TYPEDEF_ADD_VALUE (ulong, uint64);
#else
    IDIO_C_TYPEDEF_ADD_VALUE (long, int32);
    IDIO_C_TYPEDEF_ADD_VALUE (ulong, uint32);
#endif

}

