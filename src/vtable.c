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
 * vtable.c
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
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "closure.h"
#include "condition.h"
#include "evaluate.h"
#include "error.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "pair.h"
#include "string-handle.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"

#include "vtable.h"

int idio_vtable_generation = 0;
size_t idio_vtables_size;
idio_vtable_t **idio_vtables;
idio_vtable_t *idio_fixnum_vtable;
idio_vtable_t *idio_constant_idio_vtable;
idio_vtable_t *idio_constant_token_vtable;
idio_vtable_t *idio_constant_i_code_vtable;
idio_vtable_t *idio_constant_unicode_vtable;
idio_vtable_t *idio_placeholder_vtable;
static IDIO idio_vtable_method_names;
static IDIO idio_vtable_method_values;

static void idio_vtable_unbound_error (IDIO v, IDIO c_location)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("vtable is unbound", msh);

    idio_display_C ("value is a ", dsh);
    idio_display_C (idio_type2string (v), dsh);

    idio_error_raise_cont (idio_condition_rt_vtable_unbound_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

void idio_vtable_method_unbound_error (IDIO v, IDIO name, IDIO c_location)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("method '", msh);
    idio_display (name, msh);
    idio_display_C ("' is unbound", msh);

    idio_display_C ("value is a ", dsh);
    idio_display_C (idio_type2string (v), dsh);

    idio_error_raise_cont (idio_condition_rt_vtable_method_unbound_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       name));

    /* notreached */
}

idio_vtable_method_t *idio_vtable_create_method_simple (IDIO (*func) (idio_vtable_method_t *m, IDIO v, ...))
{
    IDIO_C_ASSERT (func);

    idio_vtable_method_t *m = idio_alloc (sizeof (idio_vtable_method_t));
    IDIO_VTABLE_METHOD_FUNC (m) = func;
    IDIO_VTABLE_METHOD_SIZE (m) = 0;
    IDIO_VTABLE_METHOD_DATA (m) = NULL;

    return m;
}

idio_vtable_method_t *idio_vtable_create_method_static_C (IDIO (*func) (idio_vtable_method_t *m, IDIO v, ...), void *data, size_t size)
{
    IDIO_C_ASSERT (func);

    idio_vtable_method_t *m = idio_alloc (sizeof (idio_vtable_method_t));
    IDIO_VTABLE_METHOD_FUNC (m) = func;
    IDIO_VTABLE_METHOD_SIZE (m) = size;
    IDIO_VTABLE_METHOD_DATA (m) = idio_alloc (size);
    memcpy (IDIO_VTABLE_METHOD_DATA (m), data, size);

    return m;
}

/*
 * XXX There is a (massive) risk that {value} could be GC'd under our
 * feet as we don't descend into vtables to verify data (as it could
 * be anything, eg. C static data, see above).
 *
 * {value} MUST be in another collectable table.  We can't guarantee
 * that someone has done that so we'll take a hit and add it to a set
 * of idio_vtable_method_values, a hash.  The reason for the hash is
 * that we don't know if someone has passed us {value} previously,
 * eg. the same function value, so we can't blindly append it to a
 * list (say, idio_gc_protect()) as it will be GC'd more than once.
 */
idio_vtable_method_t *idio_vtable_create_method_value (IDIO (*func) (idio_vtable_method_t *m, IDIO v, ...), IDIO value)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (value);

    if (idio_S_nil != value) {
	idio_hash_set (idio_vtable_method_values, value, idio_S_nil);
    }

    idio_vtable_method_t *m = idio_alloc (sizeof (idio_vtable_method_t));
    IDIO_VTABLE_METHOD_FUNC (m) = func;
    IDIO_VTABLE_METHOD_SIZE (m) = 0;
    IDIO_VTABLE_METHOD_DATA (m) = (void *) value;

    return m;
}

idio_vtable_t *idio_vtable (int type)
{
    idio_vtable_t *vt = NULL;
    IDIO_GC_ALLOC (vt, sizeof (idio_vtable_t));
    vt->vte = NULL;
    /* IDIO_GC_ALLOC (vt->vte, size * sizeof (idio_vtable_entry_t *)); */

    IDIO_VTABLE_FLAGS (vt) = IDIO_VTABLE_FLAG_NONE;
    IDIO_VTABLE_PARENT (vt) = NULL;
    IDIO_VTABLE_GEN (vt) = idio_vtable_generation;
    IDIO_VTABLE_SIZE (vt) = 0;

    idio_vtables = idio_realloc (idio_vtables, (idio_vtables_size + 1) * sizeof (idio_vtable_t));
    idio_vtables[idio_vtables_size++] = vt;

    return vt;
}

void idio_free_vtable (idio_vtable_t *vt)
{
    size_t vt_size = IDIO_VTABLE_SIZE (vt);
    for (size_t i = 0; i < vt_size; i++) {
	idio_vtable_entry_t *vte = IDIO_VTABLE_VTE (vt, i);
	if (0 == IDIO_VTABLE_ENTRY_INHERITED (vte)) {
	    idio_vtable_method_t *m = IDIO_VTABLE_ENTRY_METHOD (vte);
	    size_t m_size = IDIO_VTABLE_METHOD_SIZE (m);
	    if (m_size) {
		IDIO_GC_FREE (IDIO_VTABLE_METHOD_DATA (m), m_size);
	    }
	    IDIO_GC_FREE (m, sizeof (idio_vtable_method_t));
	}
	IDIO_GC_FREE (vte, sizeof (idio_vtable_entry_t));
    }
    IDIO_GC_FREE (vt, sizeof (idio_vtable_t) + vt_size * sizeof (idio_vtable_entry_t));
}

/*
 * idio_value_vtable() exists as the simpler types do not encapsulate
 * a vtable pointer directly.
 */
idio_vtable_t *idio_value_vtable (IDIO o)
{
    IDIO_ASSERT (o);

    idio_vtable_t *vt = NULL;

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	vt = idio_fixnum_vtable;
	break;
    case IDIO_TYPE_CONSTANT_MARK:
	switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	case IDIO_TYPE_CONSTANT_IDIO_MARK:
	    vt = idio_constant_idio_vtable;
	    break;
	case IDIO_TYPE_CONSTANT_TOKEN_MARK:
	    vt = idio_constant_token_vtable;
	    break;
	case IDIO_TYPE_CONSTANT_I_CODE_MARK:
	    vt = idio_constant_i_code_vtable;
	    break;
	case IDIO_TYPE_CONSTANT_UNICODE_MARK:
	    vt = idio_constant_unicode_vtable;
	    break;
	default:
	    /* inconceivable! */
 	    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT"), "type: unexpected object type %#x", o);

	    /* notreached */
	    return NULL;
	}
	break;
    case IDIO_TYPE_PLACEHOLDER_MARK:
	/* inconceivable! */
	vt = idio_placeholder_vtable;
	break;
    case IDIO_TYPE_POINTER_MARK:
	vt = o->vtable;
	break;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "type: unexpected object type %#x", o);

	/* notreached */
	return NULL;
    }

    return vt;
}

int idio_validate_vtable (idio_vtable_t *vt)
{
    if (NULL == vt) {
	return 0;
    }

    int gen = IDIO_VTABLE_GEN (vt);

    idio_vtable_t *parent = IDIO_VTABLE_PARENT (vt);
    if (NULL != parent) {
	int pgen = idio_validate_vtable (parent);
	if (pgen > gen) {
	    gen = pgen;

	    size_t vt_size = IDIO_VTABLE_SIZE (vt);
	    for (size_t i = 0; i < vt_size; i++) {
		idio_vtable_entry_t *vte = IDIO_VTABLE_VTE (vt, i);
		if (IDIO_VTABLE_ENTRY_INHERITED (vte)) {
		    for (size_t j = i + 1; j < vt_size; j++) {
			IDIO_VTABLE_VTE (vt, j - 1) = IDIO_VTABLE_VTE (vt, j);
		    }
		    i++;
		    IDIO_VTABLE_SIZE (vt)--;
		}
	    }
	    
	}
    }

    IDIO_VTABLE_GEN (vt) = idio_vtable_generation;
    return gen;
}

void idio_vtable_add_method_name (IDIO name)
{
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (symbol, name);

    idio_ai_t al = IDIO_ARRAY_USIZE (idio_vtable_method_names);
    for (idio_ai_t i = 0; i < al; i++) {
	if (idio_eqp (name, IDIO_ARRAY_AE (idio_vtable_method_names, i))) {
	    return;
	}
    }

    idio_array_push (idio_vtable_method_names, name);

    return;
}

void idio_vtable_add_method_base (idio_vtable_t *vt, IDIO name, idio_vtable_method_t *m, int inherit)
{
    IDIO_C_ASSERT (vt);
    IDIO_ASSERT (name);

    if (NULL == m) {
	return;
    }

    IDIO_TYPE_ASSERT (symbol, name);

    if (0 == inherit) {
	idio_vtable_add_method_name (name);
    }

    int done = 0;
    size_t vt_size = IDIO_VTABLE_SIZE (vt);
    for (size_t i = 0; i < vt_size; i++) {
	idio_vtable_entry_t *vte = IDIO_VTABLE_VTE (vt, i);
	if (idio_eqp (name, IDIO_VTABLE_ENTRY_NAME (vte))) {
	    done = 1;
	    IDIO_VTABLE_ENTRY_INHERITED (vte) = inherit;
	    IDIO_VTABLE_ENTRY_COUNT (vte) = inherit ? 1 : 0;
	    IDIO_VTABLE_ENTRY_METHOD (vte) = m;
	}
    }

    if (0 == done) {
	vt->vte = idio_realloc (vt->vte, (vt_size + 1) * sizeof (idio_vtable_entry_t *));
	IDIO_VTABLE_VTE (vt, vt_size) = idio_alloc (sizeof (idio_vtable_entry_t));
	idio_vtable_entry_t *vte = IDIO_VTABLE_VTE (vt, vt_size++);
	IDIO_VTABLE_SIZE (vt) = vt_size;
	IDIO_VTABLE_ENTRY_NAME (vte) = name;
	IDIO_VTABLE_ENTRY_INHERITED (vte) = inherit;
	IDIO_VTABLE_ENTRY_COUNT (vte) = inherit ? 1 : 0;
	IDIO_VTABLE_ENTRY_METHOD (vte) = m;
    }

    if (0 == inherit) {
	idio_vtable_generation++;
    }
}

void idio_vtable_add_method (idio_vtable_t *vt, IDIO name, idio_vtable_method_t *m)
{
    idio_vtable_add_method_base (vt, name, m, 0);
}

void idio_vtable_inherit_method (idio_vtable_t *vt, IDIO name, idio_vtable_method_t *m)
{
    idio_vtable_add_method_base (vt, name, m, 1);
}

IDIO_DEFINE_PRIMITIVE3_DS ("vtable-add-method", vtable_add_method, (IDIO o, IDIO name, IDIO func), "o name func", "\
add `func` as the `name` method to `o`		\n\
						\n\
:param o: object to update			\n\
:param name: method name			\n\
:type name: symbol				\n\
:param func: method function			\n\
:type func: function				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (name);
    IDIO_ASSERT (func);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (function, func);

    idio_vtable_t *vt = idio_value_vtable (o);

    idio_vtable_add_method (vt,
			    name,
			    idio_vtable_create_method_value (idio_util_method_run0,
							     func));

    return idio_S_unspec;
}

idio_vtable_method_t *idio_vtable_lookup_method_base (idio_vtable_t *vt, IDIO v, IDIO name, int recurse, int throw)
{
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (symbol, name);

    if (NULL == vt) {
	if (throw) {
	    idio_vtable_unbound_error (v, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	return NULL;
    }

    if (IDIO_VTABLE_GEN (vt) != idio_vtable_generation) {
	idio_validate_vtable (vt);
    }

    size_t vt_size = IDIO_VTABLE_SIZE (vt);
    idio_vtable_entry_t *vte_prev = NULL;
    for (size_t i = 0; i < vt_size; i++) {
	idio_vtable_entry_t *vte = IDIO_VTABLE_VTE (vt, i);
	if (idio_eqp (name, IDIO_VTABLE_ENTRY_NAME (vte))) {
	    IDIO_VTABLE_ENTRY_COUNT (vte)++;

	    /*
	     * Bump the more popular methods up the table to be found
	     * faster next time
	     */
	    if (i &&
		IDIO_VTABLE_ENTRY_COUNT (vte) > IDIO_VTABLE_ENTRY_COUNT (vte_prev)) {
		IDIO_VTABLE_VTE (vt, i - 1) = vte;
		IDIO_VTABLE_VTE (vt, i) = vte_prev;
	    }
	    return IDIO_VTABLE_ENTRY_METHOD (vte);
	}
	vte_prev = vte;
    }

    idio_vtable_t *pvt = IDIO_VTABLE_PARENT (vt);
    if (recurse &&
	NULL != pvt) {
	idio_vtable_method_t *m = idio_vtable_lookup_method (pvt, v, name, throw);

	idio_vtable_inherit_method (vt, name, m);

	return m;
    }

    if (throw) {
	idio_vtable_method_unbound_error (v, name, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    return NULL;
}

idio_vtable_method_t *idio_vtable_lookup_method (idio_vtable_t *vt, IDIO v, IDIO name, int throw)
{
    return idio_vtable_lookup_method_base (vt, v, name, 1, throw);
}

idio_vtable_method_t *idio_vtable_flat_lookup_method (idio_vtable_t *vt, IDIO v, IDIO name, int throw)
{
    return idio_vtable_lookup_method_base (vt, v, name, 0, throw);
}

void idio_dump_vtable (idio_vtable_t *vt)
{
    if (NULL == vt) {
	return;
    }

    fprintf (stderr, "Gen %3d:\n", IDIO_VTABLE_GEN (vt));

    size_t vt_size = IDIO_VTABLE_SIZE (vt);
    for (size_t i = 0; i < vt_size; i++) {
	idio_vtable_entry_t *vte = IDIO_VTABLE_VTE (vt, i);
	fprintf (stderr, "%3zd: ", i);
	idio_debug ("%-25s ", IDIO_VTABLE_ENTRY_NAME (vte));
	fprintf (stderr, "%s %4d lookups: ", IDIO_VTABLE_ENTRY_INHERITED (vte) ? "i" : "-", IDIO_VTABLE_ENTRY_COUNT (vte));
	idio_vtable_method_t *m = IDIO_VTABLE_ENTRY_METHOD (vte);
	fprintf (stderr, "%p ", IDIO_VTABLE_METHOD_FUNC (m));
	fprintf (stderr, "uses %zdB", IDIO_VTABLE_METHOD_SIZE (m));
	if (idio_eqp (IDIO_VTABLE_ENTRY_NAME (vte), idio_S_typename)) {
	    idio_debug (" %s", IDIO_VTABLE_METHOD_FUNC (m) (m, idio_S_nil));
	}
	fprintf (stderr, "\n");
    }

    idio_vtable_t *parent = IDIO_VTABLE_PARENT (vt);
    if (NULL != parent) {
	fprintf (stderr, "\n");

	idio_dump_vtable (parent);
    }
    
}

IDIO_DEFINE_PRIMITIVE1_DS ("dump-vtable", dump_vtable, (IDIO o), "o", "\
dump the vtable of `o`				\n\
						\n\
:param o: object to query			\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (o);

    idio_vtable_t *vt = idio_value_vtable (o);

    int type = idio_type (o);
    fprintf (stderr, "The vtable for this %s", idio_type_enum2string (type));
    fprintf (stderr, " is:\n");
    idio_dump_vtable (vt);

    return idio_S_unspec;
}

void idio_final_vtable ()
{
    for (ssize_t i = idio_vtables_size; i > 0; i--) {
	idio_vtable_t *vt = idio_vtables[i-1];
	idio_free_vtable (vt);
    }
    if (idio_vtables_size) {
	IDIO_GC_FREE (idio_vtables, idio_vtables_size * sizeof (idio_vtable_t *));
    }
}

void idio_vtable_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (dump_vtable);
    IDIO_ADD_PRIMITIVE (vtable_add_method);
}

void idio_init_vtable ()
{
    idio_module_table_register (idio_vtable_add_primitives, idio_final_vtable, NULL);

    idio_vtables_size = 0;
    idio_vtables = NULL;

    idio_vtable_method_names = idio_array (20);
    idio_gc_protect_auto (idio_vtable_method_names);

    idio_vtable_method_values = IDIO_HASH_EQP (20);
    idio_gc_protect_auto (idio_vtable_method_values);

    idio_placeholder_vtable      = idio_vtable (IDIO_TYPE_PLACEHOLDER);
}
