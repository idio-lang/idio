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

IDIO idio_struct_type (IDIO name, IDIO parent, IDIO fields)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (parent);
    IDIO_ASSERT (fields);

    IDIO_TYPE_ASSERT (symbol, name);
    if (idio_S_nil != parent) {
	IDIO_TYPE_ASSERT (struct_type, parent);
    }
    IDIO_TYPE_ASSERT (list, fields);
    
    IDIO st = idio_gc_get (IDIO_TYPE_STRUCT_TYPE);

    IDIO_GC_ALLOC (st->u.struct_type, sizeof (idio_struct_type_t));

    IDIO_STRUCT_TYPE_GREY (st) = NULL;
    IDIO_STRUCT_TYPE_NAME (st) = name;
    IDIO_STRUCT_TYPE_PARENT (st) = parent;

    idio_ai_t nfields = 0;
    IDIO field = fields;
    while (idio_S_nil != field) {
	if (! idio_isa_symbol (IDIO_PAIR_H (field))) {
	    fprintf (stderr, "struct-type name parent fields: fields must be symbols");
	    IDIO_C_ASSERT (0);
	}
	nfields++;
	field = IDIO_PAIR_T (field);
    }

    idio_ai_t pfields = 0;
    if (idio_S_nil != parent) {
	pfields = idio_array_size (IDIO_STRUCT_TYPE_FIELDS (parent));
	IDIO_STRUCT_TYPE_FIELDS (st) = idio_array_copy (IDIO_STRUCT_TYPE_FIELDS (parent), nfields);
    } else {    
	IDIO_STRUCT_TYPE_FIELDS (st) = idio_array (nfields);
    }

    field = fields;
    idio_ai_t i;
    for (i = 0; i < nfields; i++) {
	idio_array_insert_index (IDIO_STRUCT_TYPE_FIELDS (st), IDIO_PAIR_H (field), pfields + i);
	field = IDIO_PAIR_T (field);	
    }

    return st;
}

IDIO_DEFINE_PRIMITIVE3 ("make-struct-type", make_struct_type, (IDIO name, IDIO parent, IDIO fields))
{
    IDIO_ASSERT (parent);
    IDIO_ASSERT (fields);

    idio_debug ("make-struct-type: name %s", name);
    idio_debug (" parent %s", parent);
    idio_debug (" fields %s\n", fields);

    IDIO_VERIFY_PARAM_TYPE (symbol, name);
    if (idio_S_nil != parent) {
	IDIO_VERIFY_PARAM_TYPE (struct_type, parent);
    }
    IDIO_VERIFY_PARAM_TYPE (list, fields);

    return idio_struct_type (name, parent, fields);
}

int idio_isa_struct_type (IDIO p)
{
    IDIO_ASSERT (p);

    return idio_isa (p, IDIO_TYPE_STRUCT_TYPE);
}

IDIO_DEFINE_PRIMITIVE1 ("struct-type?", struct_typep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_struct_type (o)) {
	r = idio_S_true;
    }

    return r;
}

void idio_free_struct_type (IDIO p)
{
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (struct_type, p);

    idio_gc_stats_free (sizeof (idio_struct_type_t));

    free (p->u.struct_type);
}

IDIO_DEFINE_PRIMITIVE1 ("struct-type-name", struct_type_name, (IDIO st))
{
    IDIO_ASSERT (st);

    if (idio_isa_struct_instance (st)) {
	st = IDIO_STRUCT_INSTANCE_TYPE (st);
    }
    IDIO_VERIFY_PARAM_TYPE (struct_type, st);

    return IDIO_STRUCT_TYPE_NAME (st);
}

IDIO_DEFINE_PRIMITIVE1 ("struct-type-parent", struct_type_parent, (IDIO st))
{
    IDIO_ASSERT (st);

    if (idio_isa_struct_instance (st)) {
	st = IDIO_STRUCT_INSTANCE_TYPE (st);
    }
    IDIO_VERIFY_PARAM_TYPE (struct_type, st);

    return IDIO_STRUCT_TYPE_PARENT (st);
}

IDIO_DEFINE_PRIMITIVE1 ("struct-type-fields", struct_type_fields, (IDIO st))
{
    IDIO_ASSERT (st);

    if (idio_isa_struct_instance (st)) {
	st = IDIO_STRUCT_INSTANCE_TYPE (st);
    }
    IDIO_VERIFY_PARAM_TYPE (struct_type, st);

    return idio_array_to_list (IDIO_STRUCT_TYPE_FIELDS (st));
}

IDIO idio_struct_instance (IDIO st, IDIO fields)
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (fields);

    if (! idio_isa_struct_type (st)) {
	fprintf (stderr, "make-struct-instance type fields: type must be a struct-type");
    }

    IDIO si = idio_gc_get (IDIO_TYPE_STRUCT_INSTANCE);

    IDIO_GC_ALLOC (si->u.struct_instance, sizeof (idio_struct_instance_t));

    IDIO_STRUCT_INSTANCE_GREY (si) = NULL;
    IDIO_STRUCT_INSTANCE_TYPE (si) = st;

    idio_ai_t size = idio_array_size (IDIO_STRUCT_TYPE_FIELDS (st));
    IDIO_STRUCT_INSTANCE_FIELDS (si) = idio_array (size);

    idio_ai_t i = 0;
    IDIO field = fields;
    while (idio_S_nil != field) {
	idio_array_insert_index (IDIO_STRUCT_INSTANCE_FIELDS (si), IDIO_PAIR_H (field), i);
	i++;
	field = IDIO_PAIR_T (field);
    }

    if (i < size) {
	fprintf (stderr, "make-struct-instance: not enough fields: %" PRIdPTR " < %" PRIdPTR, i, size);
	IDIO_C_ASSERT (0);
    }

    if (idio_S_nil != field) {
	fprintf (stderr, "make-struct-instance: too many fields");
	IDIO_C_ASSERT (0);
    }

    return si;
}

IDIO_DEFINE_PRIMITIVE1V ("make-struct-instance", make_struct_instance, (IDIO st, IDIO fields))
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (fields);

    IDIO_VERIFY_PARAM_TYPE (struct_type, st);
    IDIO_VERIFY_PARAM_TYPE (list, fields);

    return idio_struct_instance (st, fields);
}

int idio_isa_struct_instance (IDIO p)
{
    IDIO_ASSERT (p);

    return idio_isa (p, IDIO_TYPE_STRUCT_INSTANCE);
}

IDIO_DEFINE_PRIMITIVE1 ("struct-instance?", struct_instancep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_struct_instance (o)) {
	r = idio_S_true;
    }

    return r;
}

void idio_free_struct_instance (IDIO p)
{
    IDIO_ASSERT (p);
    
    IDIO_TYPE_ASSERT (struct_instance, p);

    idio_gc_stats_free (sizeof (idio_struct_instance_t));

    free (p->u.struct_instance);
}

IDIO_DEFINE_PRIMITIVE1 ("struct-instance-type", struct_instance_type, (IDIO si))
{
    IDIO_ASSERT (si);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);

    return IDIO_STRUCT_INSTANCE_TYPE (si);
}

IDIO_DEFINE_PRIMITIVE1 ("struct-instance-fields", struct_instance_fields, (IDIO si))
{
    IDIO_ASSERT (si);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);

    return idio_array_to_list (IDIO_STRUCT_INSTANCE_FIELDS (si));
}

IDIO_DEFINE_PRIMITIVE2 ("struct-instance-ref", struct_instance_ref, (IDIO si, IDIO field))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (si);
    idio_ai_t i = idio_array_find_eqp (IDIO_STRUCT_TYPE_FIELDS (st), field, 0);

    if (-1 == i) {
	fprintf (stderr, "struct-instance-ref: field not found");
	IDIO_C_ASSERT (0);
    }

    return idio_array_get_index (IDIO_STRUCT_INSTANCE_FIELDS (si), i);
}

IDIO_DEFINE_PRIMITIVE4 ("%struct-instance-ref-direct", struct_instance_ref_direct, (IDIO si, IDIO st, IDIO fname, IDIO index))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);
    IDIO_ASSERT (fname);
    IDIO_ASSERT (index);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (struct_type, st);
    IDIO_VERIFY_PARAM_TYPE (symbol, fname);
    IDIO_VERIFY_PARAM_TYPE (fixnum, index);

    if (st != IDIO_STRUCT_INSTANCE_TYPE (si)) {
	idio_error_message ("bad structure ref");
    }
    
    return idio_array_get_index (IDIO_STRUCT_INSTANCE_FIELDS (si), IDIO_FIXNUM_VAL (index));
}

IDIO_DEFINE_PRIMITIVE3 ("struct-instance-set", struct_instance_set, (IDIO si, IDIO field, IDIO v))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (si);
    idio_ai_t i = idio_array_find_eqp (IDIO_STRUCT_TYPE_FIELDS (st), field, 0);

    if (-1 == i) {
	fprintf (stderr, "struct-instance-set: field not found");
	IDIO_C_ASSERT (0);
    }

    idio_array_insert_index (IDIO_STRUCT_INSTANCE_FIELDS (si), v, IDIO_FIXNUM_VAL (index));

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE5 ("%struct-instance-set-direct", struct_instance_set_direct, (IDIO si, IDIO st, IDIO fname, IDIO index, IDIO v))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);
    IDIO_ASSERT (fname);
    IDIO_ASSERT (index);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (struct_type, st);
    IDIO_VERIFY_PARAM_TYPE (symbol, fname);
    IDIO_VERIFY_PARAM_TYPE (fixnum, index);

    if (st != IDIO_STRUCT_INSTANCE_TYPE (si)) {
	idio_error_message ("bad structure set");
    }
    
    idio_array_insert_index (IDIO_STRUCT_INSTANCE_FIELDS (si), v, IDIO_FIXNUM_VAL (index));

    return idio_S_unspec;
}

static IDIO idio_struct_instance_isa (IDIO si, IDIO st)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);

    if (IDIO_STRUCT_INSTANCE_TYPE (si) == st) {
	return idio_S_true;
    } else {
	if (idio_S_nil != IDIO_STRUCT_TYPE_PARENT (st)) {
	    return idio_struct_instance_isa (si, IDIO_STRUCT_TYPE_PARENT (st));
	}
    }
    
    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2 ("struct-instance-isa", struct_instance_isa, (IDIO si, IDIO st))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (struct_type, st);

    return idio_struct_instance_isa (si, st);
}

void idio_init_struct ()
{
}

void idio_struct_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (make_struct_type);
    IDIO_ADD_PRIMITIVE (struct_typep);
    IDIO_ADD_PRIMITIVE (struct_type_name);
    IDIO_ADD_PRIMITIVE (struct_type_parent);
    IDIO_ADD_PRIMITIVE (struct_type_fields);

    IDIO_ADD_PRIMITIVE (make_struct_instance);
    IDIO_ADD_PRIMITIVE (struct_instancep);
    IDIO_ADD_PRIMITIVE (struct_instance_type);
    IDIO_ADD_PRIMITIVE (struct_instance_fields);
    IDIO_ADD_PRIMITIVE (struct_instance_ref);
    IDIO_ADD_PRIMITIVE (struct_instance_ref_direct);
    IDIO_ADD_PRIMITIVE (struct_instance_set);
    IDIO_ADD_PRIMITIVE (struct_instance_set_direct);
    IDIO_ADD_PRIMITIVE (struct_instance_isa);
}

void idio_final_struct ()
{
}

