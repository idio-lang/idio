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
 * primitive.c
 *
 */

#include "idio.h"

IDIO idio_primitive (IDIO (*func) (IDIO args), const char *name_C, size_t arity, char varargs, const char *sigstr_C, const char *docstr_C)
{
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (name_C);

    IDIO o = idio_gc_get (IDIO_TYPE_PRIMITIVE);

    IDIO_FPRINTF (stderr, "idio_primitive: %10p = (%10p)\n", o, func);

    IDIO_GC_ALLOC (o->u.primitive, sizeof (idio_primitive_t));

    IDIO_PRIMITIVE_GREY (o) = NULL;
    IDIO_PRIMITIVE_F (o) = func;
    IDIO_PRIMITIVE_ARITY (o) = arity;
    IDIO_PRIMITIVE_VARARGS (o) = varargs;

    size_t l = strlen (name_C);
    IDIO_C_ASSERT (l);

    IDIO_GC_ALLOC (IDIO_PRIMITIVE_NAME (o), l + 1);

    /*
      No point in using strncpy as we have just relied on name_C being
      NUL terminated with strlen...
     */
    strcpy (IDIO_PRIMITIVE_NAME (o), name_C);

#ifdef IDIO_VM_PERF
    IDIO_PRIMITIVE_CALLED (o) = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_nsec = 0;
#endif

    idio_properties_create (o);
    if (NULL != sigstr_C) {
	idio_set_property (o, idio_KW_sigstr, idio_string_C (sigstr_C));
    }
    if (NULL != docstr_C) {
	idio_set_property (o, idio_KW_docstr_raw, idio_string_C (docstr_C));
    }

    return o;
}

IDIO idio_primitive_data (idio_primitive_desc_t *desc)
{
    IDIO_C_ASSERT (desc);

    IDIO o = idio_gc_get (IDIO_TYPE_PRIMITIVE);

    IDIO_GC_ALLOC (o->u.primitive, sizeof (idio_primitive_t));

    IDIO_PRIMITIVE_GREY (o) = NULL;
    IDIO_PRIMITIVE_F (o) = desc->f;
    IDIO_PRIMITIVE_ARITY (o) = desc->arity;
    IDIO_PRIMITIVE_VARARGS (o) = desc->varargs;

    size_t l = strlen (desc->name);
    IDIO_C_ASSERT (l);

    IDIO_GC_ALLOC (IDIO_PRIMITIVE_NAME (o), l + 1);

    strcpy (IDIO_PRIMITIVE_NAME (o), desc->name);

#ifdef IDIO_VM_PERF
    IDIO_PRIMITIVE_CALLED (o) = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_nsec = 0;
#endif

    idio_properties_create (o);
    if (NULL != desc->name) {
	idio_set_property (o, idio_KW_name, idio_symbols_C_intern (desc->name));
    }
    if (NULL != desc->sigstr) {
	idio_set_property (o, idio_KW_sigstr, idio_string_C (desc->sigstr));
    }
    if (NULL != desc->docstr) {
	idio_set_property (o, idio_KW_docstr_raw, idio_string_C (desc->docstr));
    }

    return o;
}

void idio_primitive_set_property_C (IDIO p, IDIO kw, const char *str_C)
{
    IDIO_ASSERT (p);
    IDIO_TYPE_ASSERT (primitive, p);

    IDIO str = idio_S_nil;

    if (NULL == str_C) {
	return;
    } else {
	size_t l = strlen (str_C);
	if (0 == l) {
	    str = idio_S_nil;
	} else {
	    str = idio_string_C (str_C);
	}
    }

    idio_set_property (p, kw, str);
}

int idio_isa_primitive (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_PRIMITIVE);
}

void idio_free_primitive (IDIO o)
{
    IDIO_ASSERT (o);
    IDIO_TYPE_ASSERT (primitive, o);

    idio_gc_stats_free (sizeof (idio_primitive_t));

    free (IDIO_PRIMITIVE_NAME (o));
    free (o->u.primitive);
}

void idio_init_primitive ()
{
}

void idio_primitive_add_primitives ()
{
}

void idio_final_primitive ()
{
}

