/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "idio-string.h"
#include "keyword.h"
#include "primitive.h"
#include "symbol.h"
#include "vm.h"

/*
 * idio_primitive() exists in case anyone wants to create a primitive
 * dynamically -- as opposed to via the usual C macro methods.
 */
IDIO idio_primitive (IDIO (*func) (IDIO args), char const *name_C, size_t const name_C_len, size_t const arity, char varargs, char const *sigstr_C, size_t const sigstr_C_len, char const *docstr_C, size_t const docstr_C_len)
{
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (name_C);

    IDIO o = idio_gc_get (IDIO_TYPE_PRIMITIVE);

    IDIO_GC_ALLOC (o->u.primitive, sizeof (idio_primitive_t));

    IDIO_PRIMITIVE_GREY (o) = NULL;
    IDIO_PRIMITIVE_F (o) = func;
    IDIO_PRIMITIVE_ARITY (o) = arity;
    IDIO_PRIMITIVE_VARARGS (o) = varargs;

    IDIO_GC_ALLOC (IDIO_PRIMITIVE_NAME (o), name_C_len + 1);

    memcpy (IDIO_PRIMITIVE_NAME (o), name_C, name_C_len);
    IDIO_PRIMITIVE_NAME (o)[name_C_len] = '\0';
    IDIO_PRIMITIVE_NAME_LEN (o) = name_C_len;

#ifdef IDIO_VM_PROF
    IDIO_PRIMITIVE_CALLED (o) = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_nsec = 0;
    IDIO_PRIMITIVE_RU_UTIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_RU_UTIME (o).tv_usec = 0;
    IDIO_PRIMITIVE_RU_STIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_RU_STIME (o).tv_usec = 0;
#endif

    idio_create_properties (o);
    if (NULL != sigstr_C) {
	idio_set_property (o, idio_KW_sigstr, idio_string_C_len (sigstr_C, sigstr_C_len));
    }
    if (NULL != docstr_C) {
	idio_set_property (o, idio_KW_docstr_raw, idio_string_C_len (docstr_C, docstr_C_len));
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

    IDIO_GC_ALLOC (IDIO_PRIMITIVE_NAME (o), desc->name_len + 1);

    memcpy (IDIO_PRIMITIVE_NAME (o), desc->name, desc->name_len);
    IDIO_PRIMITIVE_NAME (o)[desc->name_len] = '\0';
    IDIO_PRIMITIVE_NAME_LEN (o) = desc->name_len;

#ifdef IDIO_VM_PROF
    IDIO_PRIMITIVE_CALLED (o) = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_CALL_TIME (o).tv_nsec = 0;
    IDIO_PRIMITIVE_RU_UTIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_RU_UTIME (o).tv_usec = 0;
    IDIO_PRIMITIVE_RU_STIME (o).tv_sec = 0;
    IDIO_PRIMITIVE_RU_STIME (o).tv_usec = 0;
#endif

    idio_create_properties (o);
    if (NULL != desc->name) {
	idio_set_property (o, idio_KW_name, idio_symbols_C_intern (desc->name, desc->name_len));
    }
    if (NULL != desc->sigstr) {
	idio_set_property (o, idio_KW_sigstr, idio_string_C_len (desc->sigstr, desc->sigstr_len));
    }
    if (NULL != desc->docstr) {
	idio_set_property (o, idio_KW_docstr_raw, idio_string_C_len (desc->docstr, desc->docstr_len));
    }
    if (NULL != desc->source_file) {
	char src[81];
	snprintf (src, 80, "%s:line %d", desc->source_file, desc->source_line);
	src[80] = '\0';

	idio_set_property (o, idio_KW_source, idio_string_C (src));
    }

    return o;
}

void idio_primitive_set_property_C (IDIO p, IDIO kw, char const *str_C, size_t const str_C_len)
{
    IDIO_ASSERT (p);
    IDIO_TYPE_ASSERT (primitive, p);

    IDIO str = idio_S_nil;

    if (NULL == str_C) {
	/*
	 * Code coverage:
	 *
	 * Coding error.
	 */
	return;
    } else {
	if (0 == str_C_len) {
	    /*
	     * Code coverage:
	     *
	     * Coding error.
	     */
	    str = idio_S_nil;
	} else {
	    str = idio_string_C_len (str_C, str_C_len);
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

    IDIO_GC_FREE (IDIO_PRIMITIVE_NAME (o));
    IDIO_GC_FREE (o->u.primitive);
}

IDIO_DEFINE_PRIMITIVE1_DS ("primitive?", primitivep, (IDIO o), "o", "\
test if `o` is a primitive				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: ``#t`` if `o` is a primitive, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_primitive (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("primitive-arity", primitive_arity, (IDIO p), "p", "\
Return the arity of `p`				\n\
						\n\
:param p: primtive				\n\
:type p: primitive				\n\
:return: arity					\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: primitive-errors/primitive-arity-bad-type.idio
     *
     * primitive-arity #t
     */
    IDIO_USER_TYPE_ASSERT (primitive, p);

    return idio_integer (IDIO_PRIMITIVE_ARITY (p));
}

IDIO_DEFINE_PRIMITIVE1_DS ("primitive-name", primitive_name, (IDIO p), "p", "\
Return the name of `p`				\n\
						\n\
:param p: primtive				\n\
:type p: primitive				\n\
:return: name					\n\
:rtype: string					\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: primitive-errors/primitive-name-bad-type.idio
     *
     * primitive-name #t
     */
    IDIO_USER_TYPE_ASSERT (primitive, p);

    return idio_string_C_len (IDIO_PRIMITIVE_NAME (p), IDIO_PRIMITIVE_NAME_LEN (p));
}

IDIO_DEFINE_PRIMITIVE1_DS ("primitive-varargs?", primitive_varargsp, (IDIO p), "p", "\
Return ``#t`` if `p` is varargs			\n\
						\n\
:param p: primtive				\n\
:type p: primitive				\n\
:return: varargs				\n\
:rtype: boolean					\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: primitive-errors/primitive-varargs-bad-type.idio
     *
     * primitive-varargs? #t
     */
    IDIO_USER_TYPE_ASSERT (primitive, p);

    IDIO r = idio_S_false;

    if (IDIO_PRIMITIVE_VARARGS (p)) {
	r = idio_S_true;
    }

    return r;
}

void idio_primitive_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (primitivep);

    /*
     * Export these into *evaluation* for the evaluator to use
     */
    IDIO_ADD_MODULE_PRIMITIVE (idio_evaluate_module, primitive_arity);
    IDIO_ADD_MODULE_PRIMITIVE (idio_evaluate_module, primitive_name);
    IDIO_ADD_MODULE_PRIMITIVE (idio_evaluate_module, primitive_varargsp);
}

void idio_init_primitive ()
{
    idio_module_table_register (idio_primitive_add_primitives, NULL, NULL);
}

