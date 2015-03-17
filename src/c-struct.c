/*
 * Copyright (c) 2015 Ian Fitchet <idf(at)idio-lang.org>
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

#include "idio.h"

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

void idio_init_C_struct ()
{
    idio_C_typedefs_hash = IDIO_HASH_EQP (idio_G_frame, 80);
    idio_collector_protect (idio_G_frame, idio_C_typedefs_hash);
    
    IDIO_C_TYPEDEF_ADD (idio_G_frame, int8);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, uint8);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, int16);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, uint16);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, int32);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, uint32);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, int64);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, uint64);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, float);
    IDIO_C_TYPEDEF_ADD (idio_G_frame, double);
    {
	IDIO sym = idio_symbols_C_intern (idio_G_frame, "*");
	idio_CTD_asterisk = idio_C_typedefs_add (idio_G_frame, sym);
    }
    IDIO_C_TYPEDEF_ADD (idio_G_frame, string);

    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, char, int8);
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, uchar, uint8);
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, short, int16);
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, ushort, uint16);
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, int, int32);
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, uint, uint32);
#ifdef __LP64__
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, long, int64);
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, ulong, uint64);
#else
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, long, int32);
    IDIO_C_TYPEDEF_ADD_VALUE (idio_G_frame, ulong, uint32);
#endif
    
}

IDIO idio_C_typedef (IDIO f, IDIO sym)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (sym);

    IDIO_C_ASSERT (idio_isa_symbol (f, sym));
    
    IDIO o = idio_get (f, IDIO_TYPE_C_TYPEDEF);

    IDIO_ALLOC (f, o->u.C_typedef, sizeof (idio_C_typedef_t));

    IDIO_C_TYPEDEF_SYM (o) = sym;

    return o;
}

int idio_isa_C_typedef (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return idio_isa (f, o, IDIO_TYPE_C_TYPEDEF);
}

void idio_free_C_typedef (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    IDIO_C_ASSERT (idio_isa_C_typedef (f, o));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_C_typedef_t);

    free (o->u.C_typedef);
}

int idio_C_typedef_type_cmp (IDIO f, IDIO C_typedef, IDIO val)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (C_typedef);
    
    switch (val->type) {
    case IDIO_TYPE_C_INT8: return (C_typedef == idio_CTD_int8);
    case IDIO_TYPE_C_UINT8: return (C_typedef == idio_CTD_uint8);
    case IDIO_TYPE_C_INT16: return (C_typedef == idio_CTD_int16);
    case IDIO_TYPE_C_UINT16: return (C_typedef == idio_CTD_uint16);
    case IDIO_TYPE_C_INT32: return (C_typedef == idio_CTD_int32);
    case IDIO_TYPE_C_UINT32: return (C_typedef == idio_CTD_uint32);
    case IDIO_TYPE_C_INT64: return (C_typedef == idio_CTD_int64);
    case IDIO_TYPE_C_UINT64: return (C_typedef == idio_CTD_uint64);
    case IDIO_TYPE_C_FLOAT: return (C_typedef == idio_CTD_float);
    case IDIO_TYPE_C_DOUBLE: return (C_typedef == idio_CTD_double);
    case IDIO_TYPE_C_POINTER: return (C_typedef == idio_CTD_asterisk);
    case IDIO_TYPE_STRING: return (C_typedef == idio_CTD_string);
    }

    return 0;
}

IDIO idio_C_typedefs_get (IDIO f, IDIO s)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    return idio_hash_get (f, idio_C_typedefs_hash, s);
}

IDIO idio_C_typedefs_exists (IDIO f, IDIO s)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    return idio_hash_exists (f, idio_C_typedefs_hash, s);
}

IDIO idio_C_typedefs_add_value (IDIO f, IDIO s, IDIO v)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (s);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (symbol, s);

    IDIO r = idio_C_typedefs_get (f, s);

    if (idio_S_nil == r) {
	if (idio_S_nil != v) {
	    r = idio_C_typedefs_get (f, v);
	    if (idio_S_nil == r) {
		char *vs = idio_display_string (f, v);
		char em[BUFSIZ];
		sprintf (em, "target C_typedef '%s' does not exist", vs);
		free (vs);
		idio_error_add_C (f, em);
	    }
	}
	r = idio_hash_put (f, idio_C_typedefs_hash, s, v);
    }

    return r;
}

IDIO idio_C_typedefs_add (IDIO f, IDIO s)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    return idio_C_typedefs_add_value (f, s, idio_S_nil);
}

IDIO idio_resolve_C_typedef (IDIO f, IDIO ctd)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (ctd);

    /*
      time_t -> slong -> int32/int64
     */

    IDIO ctdv = idio_C_typedefs_get (f, ctd);
    if (idio_S_nil == ctdv) {
	return ctd;
    } else {
	return idio_resolve_C_typedef (f, ctdv);
    }
}

IDIO idio_C_slots_array (IDIO f, IDIO C_typedefs)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (C_typedefs);
    
    IDIO_TYPE_ASSERT (pair, C_typedefs);

    int sc = 0;
    IDIO sp = C_typedefs;
    while (idio_S_nil != sp) {
	sc++;
	sp = idio_pair_tail (f, sp);
    }

    IDIO slots_array = idio_array (f, sc);

    size_t offset = 0;
    while (idio_S_nil != C_typedefs) {
	IDIO C_typedef = idio_pair_head (f, C_typedefs);

	IDIO slot_data = idio_array (f, IDIO_C_SLOT_DATA_MAX);
	idio_array_push (f, slot_data, C_typedef);

	size_t alignment;
	size_t size;
	size_t type;

	int nelem = 1;
	if (idio_isa_pair (f, C_typedef)) {
	    IDIO v1 = idio_pair_head (f, idio_pair_tail (f, C_typedef));
	    IDIO v2 = idio_C_number_cast (f, v1, IDIO_TYPE_C_UINT64);
	    nelem = IDIO_C_TYPE_UINT64 (v2);

	    C_typedef = idio_pair_head (f, C_typedef);
	}

	IDIO base_C_typedef = idio_resolve_C_typedef (f, C_typedef);
	
	if (base_C_typedef == idio_CTD_int8) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int8_t);
	    size = nelem * sizeof (int8_t);
	    if (nelem > 1) {
		type = IDIO_TYPE_STRING;
	    } else {
		type = IDIO_TYPE_C_INT8;
	    }
	} else if (base_C_typedef == idio_CTD_uint8) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint8_t);
	    size = nelem * sizeof (uint8_t);
	    type = IDIO_TYPE_C_UINT8;
	} else if (base_C_typedef == idio_CTD_int16) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int16_t);
	    size = nelem * sizeof (int16_t);
	    type = IDIO_TYPE_C_INT16;
	} else if (base_C_typedef == idio_CTD_uint16) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint16_t);
	    size = nelem * sizeof (int16_t);
	    type = IDIO_TYPE_C_UINT16;
	} else if (base_C_typedef == idio_CTD_int32) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int32_t);
	    size = nelem * sizeof (int32_t);
	    type = IDIO_TYPE_C_INT32;
	} else if (base_C_typedef == idio_CTD_uint32) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint32_t);
	    size = nelem * sizeof (int32_t);
	    type = IDIO_TYPE_C_UINT32;
	} else if (base_C_typedef == idio_CTD_int64) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (int64_t);
	    size = nelem * sizeof (int64_t);
	    type = IDIO_TYPE_C_INT64;
	} else if (base_C_typedef == idio_CTD_uint64) {
	    alignment = IDIO_C_STRUCT_ALIGNMENT (uint64_t);
	    size = nelem * sizeof (int64_t);
	    type = IDIO_TYPE_C_UINT64;
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
	    idio_error_add_C (f, "unexpected type\n");
	    return slots_array;
	}

	/* add any alignment required */
	offset += offset % alignment;

	idio_array_push (f, slot_data, idio_C_uint64 (f, alignment));
	idio_array_push (f, slot_data, idio_C_uint64 (f, type));
	idio_array_push (f, slot_data, idio_C_uint64 (f, offset));
	idio_array_push (f, slot_data, idio_C_uint64 (f, size));
	idio_array_push (f, slot_data, idio_C_uint64 (f, nelem));

	idio_array_push (f, slots_array, slot_data);

	offset += size;
	
	C_typedefs = idio_pair_tail (f, C_typedefs);
    }

    return slots_array;
}

size_t idio_sizeof_C_struct (IDIO f, IDIO slots_array)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (slots_array);

    size_t r = 0;

    size_t sas = idio_array_size (f, slots_array);

    IDIO slot_data = idio_array_get_index (f, slots_array, sas - 1);
    IDIO offset = idio_array_get_index (f, slot_data, IDIO_C_SLOT_DATA_OFFSET);
    IDIO size = idio_array_get_index (f, slot_data, IDIO_C_SLOT_DATA_SIZE);

    r = IDIO_C_TYPE_INT64 (offset) + IDIO_C_TYPE_UINT64 (size);

    return r;
}

IDIO idio_C_struct (IDIO f, IDIO slots_array, IDIO methods, IDIO frame)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (slots_array);
    IDIO_ASSERT (methods);
    IDIO_ASSERT (frame);

    IDIO cs = idio_get (f, IDIO_TYPE_C_STRUCT);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_C_struct: %10p = (%10p %10p)\n", cs, slots_array, methods);

    IDIO_ALLOC (f, cs->u.C_struct, sizeof (idio_C_struct_t));

    IDIO_C_STRUCT_GREY (cs) = NULL;
    IDIO_C_STRUCT_SLOTS (cs) = slots_array;
    IDIO_C_STRUCT_METHODS (cs) = methods;
    IDIO_C_STRUCT_FRAME (cs) = frame;
    IDIO_C_STRUCT_SIZE (cs) = idio_sizeof_C_struct (f, slots_array);

    return cs;
}

int idio_isa_C_struct (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return idio_isa (f, o, IDIO_TYPE_C_STRUCT);
}

void idio_free_C_struct (IDIO f, IDIO cs)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (cs);
    IDIO_C_ASSERT (idio_isa_C_struct (f, cs));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_C_struct_t);

    free (cs->u.C_struct);
}

IDIO idio_C_instance (IDIO f, IDIO cs, IDIO frame)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (cs);
    IDIO_C_ASSERT (idio_isa_C_struct (f, cs));
    IDIO_ASSERT (frame);

    IDIO ci = idio_get (f, IDIO_TYPE_C_INSTANCE);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_C_instance: %10p = (%10p)\n", ci, cs);

    IDIO_ALLOC (f, ci->u.C_instance, sizeof (idio_C_instance_t));
    IDIO_ALLOC (f, IDIO_C_INSTANCE_P (ci), IDIO_C_STRUCT_SIZE (cs));

    IDIO_C_INSTANCE_GREY (ci) = NULL;
    IDIO_C_INSTANCE_C_STRUCT (ci) = cs;
    IDIO_C_INSTANCE_FRAME (ci) = frame;

    return ci;
}

int idio_isa_C_instance (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return idio_isa (f, o, IDIO_TYPE_C_INSTANCE);
}

void idio_free_C_instance (IDIO f, IDIO ci)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (ci);
    IDIO_C_ASSERT (idio_isa_C_instance (f, ci));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_C_instance_t);

    free (ci->u.C_instance->p);
    free (ci->u.C_instance);
}

IDIO idio_opaque (IDIO f, void *p)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (p);
    
    return idio_opaque_final (f, p, NULL, idio_S_nil);
}

IDIO idio_opaque_args (IDIO f, void *p, IDIO args)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (p);
    IDIO_ASSERT (args);
    
    return idio_opaque_final (f, p, NULL, args);
}

IDIO idio_opaque_final (IDIO f, void *p, void (*func) (IDIO o), IDIO args)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (p);

    IDIO o = idio_get (f, IDIO_TYPE_OPAQUE);

    IDIO_ALLOC (f, o->u.opaque, sizeof (idio_opaque_t));

    IDIO_OPAQUE_P (o) = p;
    IDIO_OPAQUE_ARGS (o) = args;

    idio_register_finalizer (f, o, func);

    return o;
}

int idio_isa_opaque (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return idio_isa (f, o, IDIO_TYPE_OPAQUE);
}

void idio_free_opaque (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    IDIO_C_ASSERT (idio_isa_opaque (f, o));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_opaque_t);

    free (o->u.opaque);
}

