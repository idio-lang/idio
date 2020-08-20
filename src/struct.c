/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

static void idio_struct_error (IDIO msg, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO location = idio_vm_source_location ();

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);

#ifdef IDIO_DEBUG
    idio_display_C (": ", dsh);
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_idio_error_type,
				   IDIO_LIST3 (msg,
					       location,
					       idio_get_output_string (dsh)));
    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

static void idio_struct_instance_error_field_not_found (IDIO field, IDIO c_location)
{
    IDIO_ASSERT (field);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("field '", sh);
    idio_display (field, sh);
    idio_display_C ("' not found", sh);
    IDIO c = idio_struct_instance (idio_condition_runtime_error_type,
				   IDIO_LIST3 (idio_get_output_string (sh),
					       c_location,
					       idio_S_nil));
    idio_raise_condition (idio_S_true, c);
}

static void idio_struct_instance_error_bounds (char *em, idio_ai_t index, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (em, msh);

    IDIO location = idio_vm_source_location ();

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (idio_integer (index), dsh);

#ifdef IDIO_DEBUG
    idio_display_C (": ", dsh);
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_runtime_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh)));

    idio_raise_condition (idio_S_true, c);
}

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

    size_t nfields = 0;
    IDIO fs = fields;
    while (idio_S_nil != fs) {
	IDIO f = IDIO_PAIR_H (fs);
	if (! idio_isa_symbol (f)) {
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "struct-type name parent fs: fs must be symbols");

	    return idio_S_notreached;
	}
	nfields++;
	fs = IDIO_PAIR_T (fs);
    }

    size_t pfields = 0;
    if (idio_S_nil != parent) {
	pfields = IDIO_STRUCT_TYPE_SIZE (parent);
    }

    size_t size = pfields + nfields;
    IDIO_STRUCT_TYPE_SIZE (st) = size;
    IDIO_GC_ALLOC (st->u.struct_type->fields, size * sizeof (IDIO));

    size_t i;
    for (i = 0; i < pfields; i++) {
	IDIO_STRUCT_TYPE_FIELDS (st, i) = IDIO_STRUCT_TYPE_FIELDS (parent, i);
    }
    fs = fields;
    while (idio_S_nil != fs) {
	IDIO_STRUCT_TYPE_FIELDS (st, i++) = IDIO_PAIR_H (fs);
	fs = IDIO_PAIR_T (fs);
    }

    return st;
}

IDIO_DEFINE_PRIMITIVE3 ("make-struct-type", make_struct_type, (IDIO name, IDIO parent, IDIO fields))
{
    IDIO_ASSERT (parent);
    IDIO_ASSERT (fields);

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

    idio_gc_stats_free (IDIO_STRUCT_TYPE_SIZE (p) * sizeof (IDIO));
    idio_gc_stats_free (sizeof (idio_struct_type_t));

    free (p->u.struct_type->fields);
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

    IDIO r = idio_S_nil;
    size_t size = IDIO_STRUCT_TYPE_SIZE (st);
    idio_ai_t i;		/* will go negative */
    for (i = size - 1; i >=0 ; i--) {
	r = idio_pair (IDIO_STRUCT_TYPE_FIELDS (st, i), r);
    }

    return r;
}

int idio_struct_type_isa (IDIO st, IDIO type)
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (type);

    if (idio_S_nil == st) {
	return 0;
    }

    IDIO_VERIFY_PARAM_TYPE (struct_type, st);

    if (st == type) {
	return 1;
    } else {
	if (idio_S_nil != IDIO_STRUCT_TYPE_PARENT (st)) {
	    return idio_struct_type_isa (IDIO_STRUCT_TYPE_PARENT (st), type);
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE2 ("struct-type-isa", struct_type_isa, (IDIO st, IDIO type))
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (type);

    IDIO_VERIFY_PARAM_TYPE (struct_type, st);
    IDIO_VERIFY_PARAM_TYPE (struct_type, type);

    IDIO r = idio_S_false;

    if (idio_struct_type_isa (st, type)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_allocate_struct_instance (IDIO st, int fill)
{
    IDIO_ASSERT (st);

    IDIO_TYPE_ASSERT (struct_type, st);

    IDIO si = idio_gc_get (IDIO_TYPE_STRUCT_INSTANCE);

    IDIO_STRUCT_INSTANCE_GREY (si) = NULL;
    IDIO_STRUCT_INSTANCE_TYPE (si) = st;

    idio_ai_t size = IDIO_STRUCT_TYPE_SIZE (st);
    IDIO_GC_ALLOC (si->u.struct_instance.fields, size * sizeof (struct idio_s));

    if (fill) {
	idio_ai_t i = 0;
	for (i = 0; i < size; i++) {
	    IDIO_STRUCT_INSTANCE_FIELDS (si, i) = idio_S_nil;
	}
    }

    return si;
}

IDIO_DEFINE_PRIMITIVE1 ("allocate-struct-instance", allocate_struct_instance, (IDIO st))
{
    IDIO_ASSERT (st);

    IDIO_VERIFY_PARAM_TYPE (struct_type, st);

    return idio_allocate_struct_instance (st, 1);
}

IDIO idio_struct_instance (IDIO st, IDIO values)
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (values);

    IDIO_TYPE_ASSERT (struct_type, st);

    IDIO si = idio_allocate_struct_instance (st, 0);

    idio_ai_t i = 0;
    IDIO value = values;
    while (idio_S_nil != value) {
	IDIO_STRUCT_INSTANCE_FIELDS (si, i) = IDIO_PAIR_H (value);
	i++;
	value = IDIO_PAIR_T (value);
    }

    idio_ai_t size = IDIO_STRUCT_TYPE_SIZE (st);

    if (i < size) {
	idio_debug ("fields: %s\n", st);
	idio_debug ("values: %s\n", values);
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "make-struct-instance: not enough values: %" PRIdPTR " < %" PRIdPTR, i, size);

	return idio_S_notreached;
    }

    if (idio_S_nil != value) {
	idio_error_C ("make-struct-instance: too many values: the following are left over:", values, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return si;
}

IDIO idio_struct_instance_copy (IDIO si)
{
    IDIO_ASSERT (si);

    IDIO_TYPE_ASSERT (struct_instance, si);

    IDIO sic = idio_allocate_struct_instance (IDIO_STRUCT_INSTANCE_TYPE (si), 0);

    idio_ai_t size = IDIO_STRUCT_INSTANCE_SIZE (si);
    idio_ai_t i;
    for (i = 0; i < size; i++) {
	IDIO_STRUCT_INSTANCE_FIELDS (sic, i) = IDIO_STRUCT_INSTANCE_FIELDS (si, i);
    }

    return sic;
}

IDIO_DEFINE_PRIMITIVE1V ("make-struct-instance", make_struct_instance, (IDIO st, IDIO values))
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (values);

    IDIO_VERIFY_PARAM_TYPE (struct_type, st);
    IDIO_VERIFY_PARAM_TYPE (list, values);

    return idio_struct_instance (st, values);
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

    idio_gc_stats_free (IDIO_STRUCT_INSTANCE_SIZE (p) * sizeof (IDIO));

    free (p->u.struct_instance.fields);
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

    IDIO r = idio_S_nil;
    size_t size = IDIO_STRUCT_INSTANCE_SIZE (si);
    idio_ai_t i;		/* will go negative */
    for (i = size - 1; i >=0 ; i--) {
	r = idio_pair (IDIO_STRUCT_INSTANCE_FIELDS (si, i), r);
    }

    return r;
}

/**
 * idio_struct_type_find_eqp() - return the index of the first element eqp to e
 * @a: struct-type
 * @e: ``IDIO`` value to match
 * @index: starting index
 *
 * Return:
 * The index of the first matching element or -1.
 */
idio_ai_t idio_struct_type_find_eqp (IDIO st, IDIO e, idio_ai_t index)
{
    IDIO_ASSERT (st);

    IDIO_TYPE_ASSERT (struct_type, st);

    if (index < 0) {
	return -1;
    }

    if (index >= IDIO_STRUCT_TYPE_SIZE (st)) {
	return -1;
    }

    for (; index < IDIO_STRUCT_TYPE_SIZE (st); index++) {
	if (idio_eqp (IDIO_STRUCT_TYPE_FIELDS (st, index), e)) {
	    return index;
	}
    }

    return -1;
}

IDIO idio_struct_instance_ref (IDIO si, IDIO field)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);

    IDIO_TYPE_ASSERT (struct_instance, si);
    IDIO_TYPE_ASSERT (symbol, field);

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (si);
    idio_ai_t i = idio_struct_type_find_eqp (sit, field, 0);

    if (-1 == i) {
	fprintf (stderr, "\nERROR: struct-instance-ref: field not found\n");
	idio_debug ("si=%s\n", si);
	idio_debug ("fi=%s\n", field);
	idio_debug ("sit=%s\n", sit);
	idio_struct_instance_error_field_not_found (field, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_STRUCT_INSTANCE_FIELDS (si, i);
}

IDIO_DEFINE_PRIMITIVE2 ("struct-instance-ref", struct_instance_ref, (IDIO si, IDIO field))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    return idio_struct_instance_ref (si, field);
}

IDIO idio_struct_instance_ref_direct (IDIO si, idio_ai_t index)
{
    IDIO_ASSERT (si);

    IDIO_TYPE_ASSERT (struct_instance, si);

    if (index < 0) {
	char em[BUFSIZ];
	sprintf (em, "%%struct-instance-ref-direct bounds error: %td < 0", index);
	idio_struct_instance_error_bounds (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (index >= IDIO_STRUCT_INSTANCE_SIZE (si)) {
	char em[BUFSIZ];
	sprintf (em, "%%struct-instance-ref-direct bounds error: %td >= %zu", index, IDIO_STRUCT_INSTANCE_SIZE (si));
	idio_struct_instance_error_bounds (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_STRUCT_INSTANCE_FIELDS (si, index);
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
	IDIO msh = idio_open_output_string_handle_C ();
	idio_display_C ("%struct-instance-ref-direct: a '", msh);
	idio_display (IDIO_STRUCT_TYPE_NAME (IDIO_STRUCT_INSTANCE_TYPE (si)), msh);
	idio_display_C ("' is not a '", msh);
	idio_display (IDIO_STRUCT_TYPE_NAME (st), msh);
	idio_display_C ("'", msh);
	idio_struct_error (idio_get_output_string (msh), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_struct_instance_ref_direct (si, IDIO_FIXNUM_VAL (index));
}

IDIO idio_struct_instance_set (IDIO si, IDIO field, IDIO v)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, si);
    IDIO_TYPE_ASSERT (symbol, field);

    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (si);
    idio_ai_t i = idio_struct_type_find_eqp (sit, field, 0);

    if (-1 == i) {
	fprintf (stderr, "\nERROR: struct-instance-set!: field not found\n");
	idio_debug ("struct-instance-set!: sit=%s\n", sit);
	idio_debug ("si=%s\n", si);
	idio_debug ("fi=%s\n", field);
	idio_debug ("sit=%s\n", sit);
	idio_struct_instance_error_field_not_found (field, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_STRUCT_INSTANCE_FIELDS (si, i) = v;

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3 ("struct-instance-set!", struct_instance_set, (IDIO si, IDIO field, IDIO v))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);
    IDIO_ASSERT (v);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (symbol, field);

    return idio_struct_instance_set (si, field, v);
}

IDIO idio_struct_instance_set_direct (IDIO si, idio_ai_t index, IDIO v)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, si);

    if (index < 0) {
	char em[BUFSIZ];
	sprintf (em, "%%struct-instance-set-direct! bounds error: %td < 0", index);
	idio_struct_instance_error_bounds (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (index >= IDIO_STRUCT_INSTANCE_SIZE (si)) {
	char em[BUFSIZ];
	sprintf (em, "%%struct-instance-set-direct! bounds error: %td >= %zu", index, IDIO_STRUCT_INSTANCE_SIZE (si));
	idio_struct_instance_error_bounds (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_STRUCT_INSTANCE_FIELDS (si, index) = v;

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE5 ("%struct-instance-set-direct!", struct_instance_set_direct, (IDIO si, IDIO st, IDIO fname, IDIO index, IDIO v))
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
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "bad structure set");
    }

    return idio_struct_instance_set_direct (si, IDIO_FIXNUM_VAL (index), v);
}

int idio_struct_instance_isa (IDIO si, IDIO st)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);
    IDIO_TYPE_ASSERT (struct_instance, si);
    IDIO_TYPE_ASSERT (struct_type, st);

    return idio_struct_type_isa (IDIO_STRUCT_INSTANCE_TYPE (si), st);
}

IDIO_DEFINE_PRIMITIVE2 ("struct-instance-isa", struct_instance_isa, (IDIO si, IDIO st))
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);

    IDIO_VERIFY_PARAM_TYPE (struct_instance, si);
    IDIO_VERIFY_PARAM_TYPE (struct_type, st);

    IDIO r = idio_S_false;

    if (idio_struct_instance_isa (si, st)) {
	r = idio_S_true;
    }

    return r;
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
    IDIO_ADD_PRIMITIVE (struct_type_isa);

    IDIO_ADD_PRIMITIVE (allocate_struct_instance);
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

