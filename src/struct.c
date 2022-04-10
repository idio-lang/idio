/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static void idio_struct_error (IDIO msg, IDIO st, IDIO args, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (st);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (struct_type, st);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO lsh;
    IDIO dsh;
    idio_error_init (NULL, &lsh, &dsh, c_location);

    idio_display_C ("st=", dsh);
    idio_display (IDIO_STRUCT_TYPE_NAME (st), dsh);
    idio_display_C (" from ", dsh);
    idio_display (args, dsh);

    idio_error_raise_cont (idio_condition_rt_struct_error_type,
			   IDIO_LIST3 (msg,
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_struct_instance_field_not_found_error (IDIO field, IDIO c_location)
{
    IDIO_ASSERT (field);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("field '", msh);
    idio_display (field, msh);
    idio_display_C ("' not found", msh);

    idio_error_raise_cont (idio_condition_rt_struct_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_struct_instance_bounds_error (char const *em, idio_ai_t index, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (em, msh);

    idio_error_raise_cont (idio_condition_rt_struct_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
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

    size_t nfields = 0;
    IDIO fs = fields;
    while (idio_S_nil != fs) {
	IDIO f = IDIO_PAIR_H (fs);

	/*
	 * Test Case: struct-errors/make-struct-type-bad-field-type.idio
	 *
	 * make-struct-type (gensym) #n '(#t)
	 */
	IDIO_USER_TYPE_ASSERT (symbol, f);

	nfields++;
	fs = IDIO_PAIR_T (fs);
    }

    size_t pfields = 0;
    if (idio_S_nil != parent) {
	pfields = IDIO_STRUCT_TYPE_SIZE (parent);
    }

    size_t size = pfields + nfields;

    IDIO st = idio_gc_get (IDIO_TYPE_STRUCT_TYPE);

    /*
     * struct-types need some vtable management:
     *
     * * a per-type vtable for a start
     */
    idio_vtable_t *vt = idio_vtable (0);
    st->vtable = vt;
    if (idio_S_nil == parent) {
	IDIO_VTABLE_PARENT (vt) = idio_vtable (IDIO_TYPE_STRUCT_TYPE);
    } else {
	IDIO_VTABLE_PARENT (vt) = parent->vtable;
    }

    idio_vtable_add_method (vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     name));

    IDIO_GC_ALLOC (st->u.struct_type, sizeof (idio_struct_type_t));

    IDIO_STRUCT_TYPE_GREY (st) = NULL;
    IDIO_STRUCT_TYPE_NAME (st) = name;
    IDIO_STRUCT_TYPE_PARENT (st) = parent;

    IDIO_STRUCT_TYPE_SIZE (st) = size;
    IDIO_GC_ALLOC (st->u.struct_type->fields, size * sizeof (IDIO));

    IDIO all_fields = idio_S_nil;

    size_t i;
    for (i = 0; i < pfields; i++) {
	all_fields = idio_pair (IDIO_STRUCT_TYPE_FIELDS (parent, i), all_fields);
	IDIO_STRUCT_TYPE_FIELDS (st, i) = IDIO_STRUCT_TYPE_FIELDS (parent, i);
    }
    fs = fields;
    while (idio_S_nil != fs) {
	all_fields = idio_pair (IDIO_PAIR_H (fs), all_fields);
	IDIO_STRUCT_TYPE_FIELDS (st, i++) = IDIO_PAIR_H (fs);
	fs = IDIO_PAIR_T (fs);
    }

    idio_vtable_add_method (vt,
			    idio_S_members,
			    idio_vtable_create_method_value (idio_util_method_members,
							     idio_list_nreverse (all_fields)));

    return st;
}

IDIO_DEFINE_PRIMITIVE3_DS ("make-struct-type", make_struct_type, (IDIO name, IDIO parent, IDIO fields), "name parent fields", "\
create a struct type				\n\
						\n\
:param name: struct type name			\n\
:type name: symbol				\n\
:param parent: parent struct type		\n\
:type parent: struct type or ``#n``		\n\
:param fields: field names			\n\
:type fields: list of symbol			\n\
:return: struct type				\n\
:rtype: struct type				\n\
")
{
    IDIO_ASSERT (parent);
    IDIO_ASSERT (fields);

    /*
     * Test Case: struct-errors/make-struct-type-bad-name-type.idio
     *
     * make-struct-type #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, name);

    if (idio_S_nil != parent) {
	/*
	 * Test Case: struct-errors/make-struct-type-bad-parent-type.idio
	 *
	 * make-struct-type (gensym) #t #t
	 */
	IDIO_USER_TYPE_ASSERT (struct_type, parent);
    }

    /*
     * Test Case: struct-errors/make-struct-type-bad-fields-type.idio
     *
     * make-struct-type (gensym) #n #t
     */
    IDIO_USER_TYPE_ASSERT (list, fields);

    return idio_struct_type (name, parent, fields);
}

int idio_isa_struct_type (IDIO p)
{
    IDIO_ASSERT (p);

    return idio_isa (p, IDIO_TYPE_STRUCT_TYPE);
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-type?", struct_typep, (IDIO o), "o", "\
test if `o` is a struct type			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a struct type, ``#f`` otherwise	\n\
")
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

    IDIO_GC_FREE (p->u.struct_type->fields, IDIO_STRUCT_TYPE_SIZE (p) * sizeof (IDIO));
    IDIO_GC_FREE (p->u.struct_type, sizeof (idio_struct_type_t));
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-type-name", struct_type_name, (IDIO st), "st", "\
return the name of struct type `st`		\n\
						\n\
:param st: struct type to query			\n\
:type st: struct type				\n\
:return: struct type name			\n\
:rtype: symbol					\n\
")
{
    IDIO_ASSERT (st);

    /*
     * Test Case: struct-errors/struct-type-name-bad-type.idio
     *
     * struct-type-name #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);

    return IDIO_STRUCT_TYPE_NAME (st);
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-type-parent", struct_type_parent, (IDIO st), "st", "\
return the parent of struct type `st`		\n\
						\n\
:param st: struct type to query			\n\
:type st: struct type				\n\
:return: struct type parent			\n\
:rtype: struct type or ``#n``			\n\
")
{
    IDIO_ASSERT (st);

    /*
     * Test Case: struct-errors/struct-type-parent-bad-type.idio
     *
     * struct-type-parent #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);

    return IDIO_STRUCT_TYPE_PARENT (st);
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-type-fields", struct_type_fields, (IDIO st), "st", "\
return the fields of struct type `st`		\n\
						\n\
:param st: struct type to query			\n\
:type st: struct type				\n\
:return: struct type fields			\n\
:rtype: list of symbols or ``#n``			\n\
")
{
    IDIO_ASSERT (st);

    /*
     * Test Case: struct-errors/struct-type-fields-bad-type.idio
     *
     * struct-type-fields #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);

    IDIO r = idio_S_nil;
    size_t size = IDIO_STRUCT_TYPE_SIZE (st);
    ssize_t i;		/* will go negative */
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
	/*
	 * Code coverage:
	 *
	 * This is protected against from user-land therefore requires
	 * a C unit test.
	 */
	return 0;
    }

    IDIO_TYPE_ASSERT (struct_type, st);

    if (st == type) {
	return 1;
    } else {
	if (idio_S_nil != IDIO_STRUCT_TYPE_PARENT (st)) {
	    return idio_struct_type_isa (IDIO_STRUCT_TYPE_PARENT (st), type);
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE2_DS ("struct-type-isa?", struct_type_isa, (IDIO st, IDIO type), "st type", "\
assert that struct type `st` isa a derivative	\n\
of struct type `type`				\n\
						\n\
:param st: struct type to query			\n\
:type st: struct type				\n\
:param type: struct type to compare		\n\
:type type: struct type				\n\
:return: ``#t``/``#f``					\n\
")
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (type);

    /*
     * Test Case: struct-errors/struct-type-isa-bad-st-type.idio
     *
     * struct-type-isa? #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);
    /*
     * Test Case: struct-errors/struct-type-isa-bad-type-type.idio
     *
     * struct-type-isa? ^error #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, type);

    IDIO r = idio_S_false;

    if (idio_struct_type_isa (st, type)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_allocate_struct_instance_size (IDIO st, size_t size, int fill)
{
    IDIO_ASSERT (st);

    IDIO_TYPE_ASSERT (struct_type, st);

    IDIO si = idio_gc_get (IDIO_TYPE_STRUCT_INSTANCE);
    si->vtable = st->vtable;

    IDIO_GC_ALLOC (si->u.struct_instance, sizeof (idio_struct_instance_t));

    IDIO_STRUCT_INSTANCE_GREY (si) = NULL;
    IDIO_STRUCT_INSTANCE_TYPE (si) = st;
    IDIO_STRUCT_INSTANCE_SIZE (si) = size;

    IDIO_GC_ALLOC (si->u.struct_instance->fields, size * sizeof (struct idio_s));

    if (fill) {
	size_t i = 0;
	for (i = 0; i < size; i++) {
	    IDIO_STRUCT_INSTANCE_FIELDS (si, i) = idio_S_nil;
	}
    }

    return si;
}

IDIO idio_allocate_struct_instance (IDIO st, int fill)
{
    IDIO_ASSERT (st);

    IDIO_TYPE_ASSERT (struct_type, st);

    return idio_allocate_struct_instance_size (st, IDIO_STRUCT_TYPE_SIZE (st), fill);
}

IDIO idio_struct_instance (IDIO st, IDIO values)
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (values);

    IDIO_TYPE_ASSERT (struct_type, st);

    IDIO si = idio_allocate_struct_instance (st, 0);

    size_t i = idio_list_length (values);
    size_t size = IDIO_STRUCT_TYPE_SIZE (st);

    if (i < size) {
	/*
	 * Test Case: struct-errors/make-struct-instance-too-few-values.idio
	 *
	 * define-struct foo x y
	 * make-struct-instance foo 1
	 */
	char em[BUFSIZ];
	size_t eml = idio_snprintf (em, BUFSIZ, "make-struct-instance: not enough values: %" PRIdPTR " < %" PRIdPTR, i, size);

	idio_struct_error (idio_string_C_len (em, eml), st, values, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (i > size) {
	/*
	 * Test Case: struct-errors/make-struct-instance-too-many-values.idio
	 *
	 * define-struct foo x y
	 * make-struct-instance foo 1 2 3
	 */
	char em[BUFSIZ];
	size_t eml = idio_snprintf (em, BUFSIZ, "make-struct-instance: too many values: %" PRIdPTR " > %" PRIdPTR, i, size);

	idio_struct_error (idio_string_C_len (em, eml), st, values, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO value = values;
    i = 0;
    while (idio_S_nil != value) {
	IDIO_STRUCT_INSTANCE_FIELDS (si, i) = IDIO_PAIR_H (value);
	i++;
	value = IDIO_PAIR_T (value);
    }

    return si;
}

IDIO idio_struct_instance_copy (IDIO si)
{
    IDIO_ASSERT (si);

    IDIO_TYPE_ASSERT (struct_instance, si);

    IDIO sic = idio_allocate_struct_instance (IDIO_STRUCT_INSTANCE_TYPE (si), 0);

    size_t size = IDIO_STRUCT_INSTANCE_SIZE (si);
    size_t i;
    for (i = 0; i < size; i++) {
	IDIO_STRUCT_INSTANCE_FIELDS (sic, i) = IDIO_STRUCT_INSTANCE_FIELDS (si, i);
    }

    return sic;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("make-struct-instance", make_struct_instance, (IDIO st, IDIO values), "st values", "\
create an instance of struct type `st` assigning	\n\
values to the struct type's fields			\n\
						\n\
:param st: struct type to create		\n\
:type st: struct type				\n\
:param values: values for fields		\n\
:type type: list				\n\
:return: struct instance			\n\
")
{
    IDIO_ASSERT (st);
    IDIO_ASSERT (values);

    /*
     * Test Case: struct-errors/make-struct-instance-bad-st-type.idio
     *
     * make-struct-instance #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);
    /*
     * Test Case: n/a
     *
     * values is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, values);

    return idio_struct_instance (st, values);
}

int idio_isa_struct_instance (IDIO p)
{
    IDIO_ASSERT (p);

    return idio_isa (p, IDIO_TYPE_STRUCT_INSTANCE);
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-instance?", struct_instancep, (IDIO o), "o", "\
test if `o` is a struct instance		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a struct instance, ``#f`` otherwise	\n\
")
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

    IDIO_GC_FREE (p->u.struct_instance->fields, IDIO_STRUCT_INSTANCE_SIZE (p) * sizeof (IDIO));
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-instance-type", struct_instance_type, (IDIO si), "si", "\
return the struct type of struct instance `si`	\n\
						\n\
:param si: struct instance to query		\n\
:type si: struct instance			\n\
:return: struct type				\n\
:rtype: struct type				\n\
")
{
    IDIO_ASSERT (si);

    /*
     * Test Case: struct-errors/struct-instance-type-bad-type.idio
     *
     * struct-instance-type #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, si);

    return IDIO_STRUCT_INSTANCE_TYPE (si);
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-instance-fields", struct_instance_fields, (IDIO si), "si", "\
return the struct type fields of struct instance `si`	\n\
						\n\
:param si: struct instance to query		\n\
:type si: struct instance			\n\
:return: struct type fields			\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (si);

    /*
     * Test Case: struct-errors/struct-instance-fields-bad-type.idio
     *
     * struct-instance-fields #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, si);

    IDIO r = idio_S_nil;
    size_t size = IDIO_STRUCT_INSTANCE_SIZE (si);
    ssize_t i;		/* will go negative */
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
ssize_t idio_struct_type_find_eqp (IDIO st, IDIO e, size_t index)
{
    IDIO_ASSERT (st);

    IDIO_TYPE_ASSERT (struct_type, st);

    if (index >= IDIO_STRUCT_TYPE_SIZE (st)) {
	/*
	 * Code coverage:
	 *
	 * Coding error
	 */
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
    ssize_t i = idio_struct_type_find_eqp (sit, field, 0);

    if (-1 == i) {
	/*
	 * Test Case: struct-errors/struct-instance-ref-non-existent.idio
	 *
	 * define-struct foo x y
	 * f := make-struct-instance foo 1 2
	 * struct-instance-ref f 'z
	 */
	idio_struct_instance_field_not_found_error (field, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_STRUCT_INSTANCE_FIELDS (si, i);
}

IDIO_DEFINE_PRIMITIVE2_DS ("struct-instance-ref", struct_instance_ref, (IDIO si, IDIO field), "si field", "\
return field `field` of struct instance `si`	\n\
						\n\
:param si: struct instance to query		\n\
:type si: struct instance			\n\
:param field: field name			\n\
:type field: symbol				\n\
:return: value					\n\
")
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);

    /*
     * Test Case: struct-errors/struct-instance-ref-bad-si-type.idio
     *
     * struct-instance-ref #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, si);
    /*
     * Test Case: struct-errors/struct-instance-ref-bad-field-type.idio
     *
     * struct-instance-ref (make-struct-instance ^error) #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, field);

    return idio_struct_instance_ref (si, field);
}

IDIO idio_struct_instance_ref_direct (IDIO si, ssize_t index)
{
    IDIO_ASSERT (si);

    IDIO_TYPE_ASSERT (struct_instance, si);

    if (index < 0) {
	/*
	 * Test Case: struct-errors/struct-instance-ref-direct-negative.idio
	 *
	 * define-struct foo x y
	 * f := make-struct-instance foo 1 2
	 * %struct-instance-ref-direct f foo -1
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "%%struct-instance-ref-direct bounds error: %td < 0", index);

	idio_struct_instance_bounds_error (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (index >= (ssize_t) IDIO_STRUCT_INSTANCE_SIZE (si)) {
	/*
	 * Test Case: struct-errors/struct-instance-ref-direct-non-existent.idio
	 *
	 * define-struct foo x y
	 * f := make-struct-instance foo 1 2
	 * %struct-instance-ref-direct f foo 3
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "%%struct-instance-ref-direct bounds error: %td >= %zu", index, IDIO_STRUCT_INSTANCE_SIZE (si));

	idio_struct_instance_bounds_error (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_STRUCT_INSTANCE_FIELDS (si, index);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%struct-instance-ref-direct", struct_instance_ref_direct, (IDIO si, IDIO st, IDIO index), "si st index", "\
return integer field index `index` of struct	\n\
instance `si`					\n\
						\n\
struct instance `si` is verified as being an	\n\
instance of struct type `st`			\n\
						\n\
:param si: struct instance to query		\n\
:type si: struct instance			\n\
:param st: struct type to verify		\n\
:type st: struct type				\n\
:param index: field index			\n\
:type index: fixnum				\n\
:return: value					\n\
")
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);
    IDIO_ASSERT (index);

    /*
     * Test Case: struct-errors/struct-instance-ref-direct-bad-si-type.idio
     *
     * %struct-instance-ref-direct #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, si);
    /*
     * Test Case: struct-errors/struct-instance-ref-direct-bad-st-type.idio
     *
     * %struct-instance-ref-direct (make-struct-instance ^error) #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);
    /*
     * Test Case: struct-errors/struct-instance-ref-direct-bad-st-type.idio
     *
     * %struct-instance-ref-direct (make-struct-instance ^error) ^error #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, index);

    if (st != IDIO_STRUCT_INSTANCE_TYPE (si)) {
	IDIO msh = idio_open_output_string_handle_C ();
	idio_display_C ("%struct-instance-ref-direct: a '", msh);
	idio_display (IDIO_STRUCT_TYPE_NAME (IDIO_STRUCT_INSTANCE_TYPE (si)), msh);
	idio_display_C ("' is not a '", msh);
	idio_display (IDIO_STRUCT_TYPE_NAME (st), msh);
	idio_display_C ("'", msh);

	IDIO args = IDIO_LIST1 (index);

	if (idio_eqp (IDIO_STRUCT_TYPE_NAME (st), IDIO_STRUCT_TYPE_NAME (IDIO_STRUCT_INSTANCE_TYPE (si)))) {
	    /*
	     * Test Case: struct-errors/struct-type-duplicate-name.idio
	     *
	     * define-struct foo a b
	     * bar := make-foo 1 2
	     * define-struct foo x y
	     * %struct-instance-ref-direct foo bar 0
	     */
	    idio_display_C (" did you redefine it?", msh);
	} else {
	    /*
	     * Test Case: struct-errors/struct-instance-ref-direct-wrong-type.idio
	     *
	     * define-struct foo x y
	     * f := make-struct-instance foo 1 2
	     * %struct-instance-ref-direct f ^error 0
	     */
	    args = idio_pair (si, args);
	}

	idio_struct_error (idio_get_output_string (msh), st, args, IDIO_C_FUNC_LOCATION ());

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
    ssize_t i = idio_struct_type_find_eqp (sit, field, 0);

    if (-1 == i) {
	/*
	 * Test Case: struct-errors/struct-instance-set-non-existent.idio
	 *
	 * define-struct foo x y
	 * f := make-struct-instance foo 1 2
	 * struct-instance-set! f 'z #t
	 */
	idio_struct_instance_field_not_found_error (field, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_STRUCT_INSTANCE_FIELDS (si, i) = v;

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("struct-instance-set!", struct_instance_set, (IDIO si, IDIO field, IDIO v), "si field v", "\
set field `field` of struct instance `si` to `v`\n\
						\n\
:param si: struct instance to modify		\n\
:type si: struct instance			\n\
:param field: field name			\n\
:type field: symbol				\n\
:param v: value					\n\
:type v: value					\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (field);
    IDIO_ASSERT (v);

    /*
     * Test Case: struct-errors/struct-instance-set-bad-si-type.idio
     *
     * struct-instance-set! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, si);
    /*
     * Test Case: struct-errors/struct-instance-set-bad-field-type.idio
     *
     * struct-instance-set! (make-struct-instance ^error) #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, field);

    return idio_struct_instance_set (si, field, v);
}

IDIO idio_struct_instance_set_direct (IDIO si, ssize_t index, IDIO v)
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, si);

    if (index < 0) {
	/*
	 * Test Case: struct-errors/struct-instance-set-direct-negative.idio
	 *
	 * define-struct foo x y
	 * f := make-struct-instance foo 1 2
	 * %struct-instance-set-direct! f foo -1 #t
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "%%struct-instance-set-direct! bounds error: %td < 0", index);

	idio_struct_instance_bounds_error (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (index >= (ssize_t) IDIO_STRUCT_INSTANCE_SIZE (si)) {
	/*
	 * Test Case: struct-errors/struct-instance-set-direct-non-existent.idio
	 *
	 * define-struct foo x y
	 * f := make-struct-instance foo 1 2
	 * %struct-instance-set-direct f foo 3 #t
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "%%struct-instance-set-direct! bounds error: %td >= %zu", index, IDIO_STRUCT_INSTANCE_SIZE (si));

	idio_struct_instance_bounds_error (em, index, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_STRUCT_INSTANCE_FIELDS (si, index) = v;

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE4_DS ("%struct-instance-set-direct!", struct_instance_set_direct, (IDIO si, IDIO st, IDIO index, IDIO v), "si st index v", "\
set integer field index `index` of struct	\n\
instance `si`					\n\
						\n\
struct instance `si` is verified as being an	\n\
instance of struct type `st`			\n\
						\n\
:param si: struct instance to query		\n\
:type si: struct instance			\n\
:param st: struct type to verify		\n\
:type st: struct type				\n\
:param index: field index			\n\
:type index: fixnum				\n\
:param v: value					\n\
:type v: value					\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);
    IDIO_ASSERT (index);
    IDIO_ASSERT (v);

    /*
     * Test Case: struct-errors/struct-instance-set-direct-bad-si-type.idio
     *
     * %struct-instance-set-direct! #t #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, si);
    /*
     * Test Case: struct-errors/struct-instance-set-direct-bad-st-type.idio
     *
     * %struct-instance-set-direct! (make-struct-instance ^error) #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);
    /*
     * Test Case: struct-errors/struct-instance-set-direct-bad-index-type.idio
     *
     * %struct-instance-set-direct! (make-struct-instance ^error) ^error #t #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, index);

    if (st != IDIO_STRUCT_INSTANCE_TYPE (si)) {
	/*
	 * Test Case: struct-errors/struct-instance-set-direct-wrong-type.idio
	 *
	 * define-struct foo x y
	 * f := make-struct-instance foo 1 2
	 * %struct-instance-set-direct! f ^error 0 #t
	 */
	IDIO msh = idio_open_output_string_handle_C ();
	idio_display_C ("%struct-instance-set-direct!: a '", msh);
	idio_display (IDIO_STRUCT_TYPE_NAME (IDIO_STRUCT_INSTANCE_TYPE (si)), msh);
	idio_display_C ("' is not a '", msh);
	idio_display (IDIO_STRUCT_TYPE_NAME (st), msh);
	idio_display_C ("'", msh);
	idio_struct_error (idio_get_output_string (msh), st, IDIO_LIST3 (si, index, v), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
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

IDIO_DEFINE_PRIMITIVE2_DS ("struct-instance-isa?", struct_instance_isa, (IDIO si, IDIO st), "si st", "\
assert that struct instance `si` isa a derivative	\n\
of struct type `st`				\n\
						\n\
:param si: struct instance to query		\n\
:type si: struct instance			\n\
:param st: struct type to verify		\n\
:type st: struct type				\n\
:return: ``#t``/``#f``					\n\
")
{
    IDIO_ASSERT (si);
    IDIO_ASSERT (st);

    /*
     * Test Case: struct-errors/struct-instance-isa-bad-si-type.idio
     *
     * struct-instance-isa? #t #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, si);
    /*
     * Test Case: struct-errors/struct-instance-isa-bad-st-type.idio
     *
     * struct-instance-isa? (make-struct-instance ^error) #t
     */
    IDIO_USER_TYPE_ASSERT (struct_type, st);

    IDIO r = idio_S_false;

    if (idio_struct_instance_isa (si, st)) {
	r = idio_S_true;
    }

    return r;
}

char *idio_struct_instance_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (struct_instance, v);

    char *r = NULL;
    *sizep = idio_asprintf (&r, "#<SI ");

    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (v);

    size_t n_size = 0;
    char *ns = idio_as_string (IDIO_STRUCT_TYPE_NAME (st), &n_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, ns, n_size);

#ifdef IDIO_DEBUG
    size_t size = IDIO_STRUCT_TYPE_SIZE (st);
    size_t i;
    for (i = 0; i < size; i++) {
	IDIO_STRCAT (r, sizep, " ");
	size_t fn_size = 0;
	char *fns = idio_as_string (IDIO_STRUCT_TYPE_FIELDS (st, i), &fn_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, fns, fn_size);
	IDIO_STRCAT (r, sizep, ":");
	size_t fv_size = 0;
	char *fvs = idio_report_string (IDIO_STRUCT_INSTANCE_FIELDS (v, i), &fv_size, depth - 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, fvs, fv_size);
    }
#endif

    IDIO_STRCAT (r, sizep, ">");

    return r;
}

char *idio_struct_instance_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (struct_instance, v);

    /*
     * Prefer a struct-instance->string method in our immediate struct
     * type (ie. don't recurse for a method as it won't be for us).
     */
    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (v);
    idio_vtable_method_t *st_m = idio_vtable_flat_lookup_method (idio_value_vtable (st), st, idio_S_struct_instance_2string, 0);

    if (NULL != st_m &&
	idio_struct_instance_method_2string != IDIO_VTABLE_METHOD_FUNC (st_m)) {
	IDIO s = IDIO_VTABLE_METHOD_FUNC (st_m) (st_m, v, sizep, seen, depth);

	if (idio_isa_string (s)) {
	    return idio_utf8_string (s, sizep, IDIO_UTF8_STRING_VERBATIM, IDIO_UTF8_STRING_UNQUOTED, IDIO_UTF8_STRING_NOPREC);
	} else if (0 == idio_vm_reporting) {
	    /*
	     * Test Case: util-errors/struct-instance-printer-bad-return-type.idio
	     *
	     * ... return #t
	     */
#ifdef IDIO_DEBUG
	    idio_debug ("struct-instance printer => %s (not a STRING)\n", s);
#endif
	    idio_error_param_value_msg ("struct-instance-as-string", "struct-instance printer", s, "should return a string", IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}
    }
    
    /*
     * Otherwise, a basic printer
     */
    char *r = NULL;
    *sizep = idio_asprintf (&r, "#<SI ");

    size_t n_size = 0;
    char *ns = idio_as_string (IDIO_STRUCT_TYPE_NAME (st), &n_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, ns, n_size);

    size_t size = IDIO_STRUCT_TYPE_SIZE (st);
    size_t i;
    for (i = 0; i < size; i++) {
	IDIO_STRCAT (r, sizep, " ");
	size_t fn_size = 0;
	char *fns = idio_as_string (IDIO_STRUCT_TYPE_FIELDS (st, i), &fn_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, fns, fn_size);
	IDIO_STRCAT (r, sizep, ":");
	size_t fv_size = 0;
	char *fvs = idio_as_string (IDIO_STRUCT_INSTANCE_FIELDS (v, i), &fv_size, depth - 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, fvs, fv_size);
    }

    IDIO_STRCAT (r, sizep, ">");

    return r;
}

IDIO idio_struct_instance_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (struct_instance, v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    IDIO seen = va_arg (ap, IDIO);
    int depth = va_arg (ap, int);
    va_end (ap);

    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (v);

    idio_vtable_method_t *st_m = idio_vtable_lookup_method (idio_value_vtable (st), v, idio_S_struct_instance_2string, 0);

    if (NULL != st_m) {
	IDIO r = IDIO_VTABLE_METHOD_FUNC (st_m) (st_m, v, sizep, seen, depth);

	return r;
    }

    char *C_r = idio_struct_instance_as_C_string (v, sizep, 0, seen, depth);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

char *idio_struct_type_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (struct_type, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#<ST ");

    size_t stn_size = 0;
    char *stn = idio_as_string (IDIO_STRUCT_TYPE_NAME (v), &stn_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, stn, stn_size);

    IDIO_STRCAT (r, sizep, ">");

    return r;
}

char *idio_struct_type_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    /*
     * struct-instances are anomalous because they are a true Idio
     * type yet don't have an idio_struct_instance_vtable catering for
     * them (anymore).  That's because the vast majority of uses are
     * actually those of the struct-type.
     *
     * Printing is one where we do want to distinguish and call the
     * struct-instance printer.
     */
    if (idio_isa_struct_instance (v)) {
	return idio_struct_instance_as_C_string (v, sizep, format, seen, depth);
    }
    IDIO_TYPE_ASSERT (struct_type, v);

    char *r = NULL;

#ifdef IDIO_DEBUG
    *sizep = idio_asprintf (&r, "#<ST %10p ", v);
#else
    *sizep = idio_asprintf (&r, "#<ST ");
#endif

    size_t stn_size = 0;
    char *stn = idio_as_string (IDIO_STRUCT_TYPE_NAME (v), &stn_size, 1, seen, 0);
    IDIO_STRCAT_FREE (r, sizep, stn, stn_size);

    IDIO_STRCAT (r, sizep, " [");
    IDIO p = IDIO_STRUCT_TYPE_PARENT (v);
    if (idio_S_nil != p) {
	size_t stp_size = 0;
	char *stp = idio_as_string (IDIO_STRUCT_TYPE_NAME (p), &stp_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, stp, stp_size);
    }
    IDIO_STRCAT (r, sizep, "]");

    size_t size = IDIO_STRUCT_TYPE_SIZE (v);
    size_t i;
    for (i = 0; i < size; i++) {
	IDIO_STRCAT (r, sizep, " ");
	size_t f_size = 0;
	char *fs = idio_as_string (IDIO_STRUCT_TYPE_FIELDS (v, i), &f_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, fs, f_size);
    }

    IDIO_STRCAT (r, sizep, ">");

    return r;
}

IDIO idio_struct_type_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    va_end (ap);

    char *C_r = idio_struct_type_as_C_string (v, sizep, 0, idio_S_nil, 40);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_struct_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (make_struct_type);
    IDIO_ADD_PRIMITIVE (struct_typep);
    IDIO_ADD_PRIMITIVE (struct_type_name);
    IDIO_ADD_PRIMITIVE (struct_type_parent);
    IDIO_ADD_PRIMITIVE (struct_type_fields);
    IDIO_ADD_PRIMITIVE (struct_type_isa);

    IDIO_ADD_PRIMITIVE (make_struct_instance);
    IDIO_ADD_PRIMITIVE (struct_instancep);
    IDIO_ADD_PRIMITIVE (struct_instance_type);
    IDIO_ADD_PRIMITIVE (struct_instance_fields);

    /*
     * XXX set the struct *type's* vtable value-index and
     * set-value-index! methods to be the struct-instance-{ref,set!}
     * functions.
     */
    IDIO ref = IDIO_ADD_PRIMITIVE (struct_instance_ref);
    idio_vtable_t *st_vt = idio_vtable (IDIO_TYPE_STRUCT_TYPE);
    idio_vtable_add_method (st_vt,
			    idio_S_value_index,
			    idio_vtable_create_method_value (idio_util_method_value_index,
							     idio_vm_values_ref (IDIO_FIXNUM_VAL (ref))));

    IDIO_ADD_PRIMITIVE (struct_instance_ref_direct);

    IDIO set = IDIO_ADD_PRIMITIVE (struct_instance_set);
    idio_vtable_add_method (st_vt,
			    idio_S_set_value_index,
			    idio_vtable_create_method_value (idio_util_method_set_value_index,
							     idio_vm_values_ref (IDIO_FIXNUM_VAL (set))));

    IDIO_ADD_PRIMITIVE (struct_instance_set_direct);
    IDIO_ADD_PRIMITIVE (struct_instance_isa);

}

void idio_init_struct ()
{
    idio_module_table_register (idio_struct_add_primitives, NULL, NULL);

    idio_vtable_t *st_vt = idio_vtable (IDIO_TYPE_STRUCT_TYPE);

    idio_vtable_add_method (st_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_struct_type));

    idio_vtable_add_method (st_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_struct_type_method_2string));

    idio_vtable_add_method (st_vt,
			    idio_S_struct_instance_2string,
			    idio_vtable_create_method_simple (idio_struct_instance_method_2string));
}
