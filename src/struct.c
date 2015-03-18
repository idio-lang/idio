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
 * struct.c
 * 
 */

#include "idio.h"

IDIO idio_struct_type (IDIO f, IDIO name, IDIO parent, IDIO slots)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (name);
    IDIO_ASSERT (parent);
    IDIO_ASSERT (slots);

    char *usage = NULL;
    
    if (! idio_isa_symbol (f, name)) {
	usage = "name must be a symbol";
    } else if (! (idio_S_nil == parent ||
		  idio_isa_struct_type (f, parent))) {
	usage = "parent must be nil or a struct-type";
    } else if (! idio_isa_pair (f, slots)) {
	usage = "slots must be a list";
    }

    if (usage) {
	fprintf (stderr, "struct-type name parent slots: %s", usage);
	IDIO_C_ASSERT (0);
    }
    
    IDIO st = idio_get (f, IDIO_TYPE_STRUCT_TYPE);

    IDIO_ALLOC (f, st->u.struct_type, sizeof (idio_struct_type_t));

    IDIO_STRUCT_TYPE_GREY (st) = NULL;
    IDIO_STRUCT_TYPE_NAME (st) = name;
    IDIO_STRUCT_TYPE_PARENT (st) = parent;

    idio_index_t nslots = 0;
    IDIO slot = slots;
    while (idio_S_nil != slot) {
	if (! idio_isa_symbol (f, IDIO_PAIR_H (slot))) {
	    fprintf (stderr, "struct-type name parent slots: slots must be symbols");
	    IDIO_C_ASSERT (0);
	}
	nslots++;
	slot = IDIO_PAIR_T (slot);
    }

    idio_index_t pslots = 0;
    if (idio_S_nil != parent) {
	pslots = idio_array_size (f, IDIO_STRUCT_TYPE_SLOTS (parent));
	IDIO_STRUCT_TYPE_SLOTS (st) = idio_array_copy (f, IDIO_STRUCT_TYPE_SLOTS (parent), nslots);
    } else {    
	IDIO_STRUCT_TYPE_SLOTS (st) = idio_array (f, nslots);
    }

    slot = slots;
    idio_index_t i;
    for (i = 0; i < nslots; i++) {
	idio_array_insert_index (f, IDIO_STRUCT_TYPE_SLOTS (st), IDIO_PAIR_H (slot), pslots + i);
	slot = IDIO_PAIR_T (slot);	
    }

    return st;
}

int idio_isa_struct_type (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);

    return idio_isa (f, p, IDIO_TYPE_STRUCT_TYPE);
}

void idio_free_struct_type (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (struct_type, p);

    idio_gc_t *gc = IDIO_GC (f);

    gc->stats.nbytes -= sizeof (idio_struct_type_t);

    free (p->u.struct_type);
}

IDIO idio_defprimitive_make_struct_type (IDIO f, IDIO name, IDIO parent, IDIO slots)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (name);
    IDIO_ASSERT (parent);
    IDIO_ASSERT (slots);

    return idio_struct_type (f, name, parent, slots);
}

IDIO idio_defprimitive_struct_typep (IDIO f, IDIO st)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (st);

    IDIO r = idio_S_false;

    if (idio_isa_struct_type (f, st)) {
	r = idio_S_true;
    }
    
    return r;
}

IDIO idio_defprimitive_struct_type_name (IDIO f, IDIO st)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (st);

    if (! idio_isa_struct_type (f, st)) {
	idio_error_type (f, "not a struct-type", st);
    }

    return IDIO_STRUCT_TYPE_NAME (st);
}

IDIO idio_defprimitive_struct_type_parent (IDIO f, IDIO st)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (st);

    if (! idio_isa_struct_type (f, st)) {
	idio_error_type (f, "not a struct-type", st);
    }

    return IDIO_STRUCT_TYPE_PARENT (st);
}

IDIO idio_defprimitive_struct_type_slots (IDIO f, IDIO st)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (st);

    if (! idio_isa_struct_type (f, st)) {
	idio_error_type (f, "not a struct-type", st);
    }

    return idio_array_to_list (f, IDIO_STRUCT_TYPE_SLOTS (st));
}

IDIO idio_struct_instance (IDIO f, IDIO st, IDIO slots)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (st);
    IDIO_ASSERT (slots);

    if (! idio_isa_struct_type (f, st)) {
	fprintf (stderr, "make-struct type slots: type must be a struct-type");
    }

    IDIO si = idio_get (f, IDIO_TYPE_STRUCT_INSTANCE);

    IDIO_ALLOC (f, si->u.struct_instance, sizeof (idio_struct_instance_t));

    IDIO_STRUCT_INSTANCE_GREY (si) = NULL;
    IDIO_STRUCT_INSTANCE_TYPE (si) = st;

    idio_index_t size = idio_array_size (f, IDIO_STRUCT_TYPE_SLOTS (st));
    IDIO_STRUCT_INSTANCE_SLOTS (si) = idio_array (f, size);

    idio_index_t i = 0;
    IDIO slot = slots;
    while (idio_S_nil != slot) {
	idio_array_insert_index (f, IDIO_STRUCT_INSTANCE_SLOTS (si), IDIO_PAIR_H (slot), i);
	i++;
	slot = IDIO_PAIR_T (slot);
    }

    if (i < size) {
	fprintf (stderr, "make-struct: not enough slots");
	IDIO_C_ASSERT (0);
    }

    if (idio_S_nil != slot) {
	fprintf (stderr, "make-struct: too many slots");
	IDIO_C_ASSERT (0);
    }

    return si;
}

int idio_isa_struct_instance (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);

    return idio_isa (f, p, IDIO_TYPE_STRUCT_INSTANCE);
}

void idio_free_struct_instance (IDIO f, IDIO p)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (struct_instance, p);

    idio_gc_t *gc = IDIO_GC (f);

    gc->stats.nbytes -= sizeof (idio_struct_instance_t);

    free (p->u.struct_instance);
}

IDIO idio_defprimitive_make_struct (IDIO f, IDIO st, IDIO slots)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (st);
    IDIO_ASSERT (slots);

    return (idio_struct_instance (f, st, slots));
}

IDIO idio_defprimitive_struct_instancep (IDIO f, IDIO si)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (si);

    IDIO r = idio_S_false;

    if (idio_isa_struct_instance (f, si)) {
	r = idio_S_true;
    }
    
    return r;
}

IDIO idio_defprimitive_struct_instance_type (IDIO f, IDIO si)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (si);

    if (! idio_isa_struct_instance (f, si)) {
	idio_error_type (f, "not a struct-instance", si);
    }

    return IDIO_STRUCT_INSTANCE_TYPE (si);
}

IDIO idio_defprimitive_struct_instance_slots (IDIO f, IDIO si)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (si);

    if (! idio_isa_struct_instance (f, si)) {
	idio_error_type (f, "not a struct-instance", si);
    }

    return idio_array_to_list (f, IDIO_STRUCT_INSTANCE_SLOTS (si));
}

IDIO idio_defprimitive_struct_instance_ref (IDIO f, IDIO si, IDIO slot)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (si);
    IDIO_ASSERT (slot);

    if (! idio_isa_struct_instance (f, si)) {
	idio_error_type (f, "not a struct-instance", si);
    }

    if (! idio_isa_symbol (f, slot)) {
	idio_error_type (f, "not a symbol", slot);
    }

    idio_index_t i = idio_array_find_eqp (f, IDIO_STRUCT_INSTANCE_SLOTS (si), slot, 0);

    if (-1 == i) {
	fprintf (stderr, "struct-ref: slot not found");
	IDIO_C_ASSERT (0);
    }

    return idio_array_get_index (f, IDIO_STRUCT_INSTANCE_SLOTS (si), i);
}

IDIO idio_defprimitive_struct_instance_set (IDIO f, IDIO si, IDIO slot, IDIO v)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (si);
    IDIO_ASSERT (slot);
    IDIO_ASSERT (v);

    if (! idio_isa_struct_instance (f, si)) {
	idio_error_type (f, "not a struct-instance", si);
    }

    if (! idio_isa_symbol (f, slot)) {
	idio_error_type (f, "not a symbol", slot);
    }

    idio_index_t i = idio_array_find_eqp (f, IDIO_STRUCT_INSTANCE_SLOTS (si), slot, 0);

    if (-1 == i) {
	fprintf (stderr, "struct-set!: slot not found");
	IDIO_C_ASSERT (0);
    }

    idio_array_insert_index (f, IDIO_STRUCT_INSTANCE_SLOTS (si), v, i);

    return idio_S_unspec;
}

static IDIO idio_struct_instance_isa (IDIO f, IDIO si, IDIO st)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);

    if (IDIO_STRUCT_INSTANCE_TYPE (si) == st) {
	return idio_S_true;
    } else {
	if (idio_S_nil != IDIO_STRUCT_TYPE_PARENT (st)) {
	    return idio_defprimitive_struct_instance_isa (f, si, IDIO_STRUCT_TYPE_PARENT (st));
	}
    }
    
    return idio_S_false;
}

IDIO idio_defprimitive_struct_instance_isa (IDIO f, IDIO si, IDIO st)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);

    if (! idio_isa_struct_instance (f, si)) {
	idio_error_type (f, "not a struct-instance", si);
    }

    if (! idio_isa_struct_type (f, st)) {
	idio_error_type (f, "not a struct-type", st);
    }

    return idio_struct_instance_isa (f, si, st);
}

