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
 * closure.c
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
#include <stdio.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "error.h"
#include "evaluate.h"
#include "frame.h"
#include "handle.h"
#include "idio-string.h"
#include "keyword.h"
#include "module.h"
#include "primitive.h"
#include "string-handle.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static idio_vtable_t *idio_closure_vtable;

/**
 * idio_array() - closure constructor
 * @code: byte code
 * @frame: ??
 * @env: closure's environment
 * @code: signature string
 * @code: unprocessed doc string
 *
 * Return:
 * Returns the closure.
 */
IDIO idio_closure (size_t const code_pc, size_t const code_len, IDIO frame, IDIO env, IDIO sigstr, IDIO docstr, IDIO srcloc)
{
    IDIO_C_ASSERT (code_pc);
    IDIO_C_ASSERT (code_len);
    IDIO_ASSERT (frame);
    IDIO_ASSERT (env);
    IDIO_ASSERT (sigstr);
    IDIO_ASSERT (docstr);

    if (idio_S_nil != frame) {
	IDIO_TYPE_ASSERT (frame, frame);
    }
    IDIO_TYPE_ASSERT (module, env);

    IDIO c = idio_gc_get (IDIO_TYPE_CLOSURE);
    c->vtable = idio_closure_vtable;

    IDIO_GC_ALLOC (c->u.closure, sizeof (idio_closure_t));

    IDIO_CLOSURE_GREY (c) = NULL;
    IDIO_CLOSURE_CODE_PC (c) = code_pc;
    IDIO_CLOSURE_CODE_LEN (c) = code_len;
    IDIO_CLOSURE_FRAME (c) = frame;
    IDIO_CLOSURE_ENV (c) = env;
#ifdef IDIO_VM_PROF
    IDIO_CLOSURE_CALLED (c) = 0;
    IDIO_CLOSURE_CALL_TIME (c).tv_sec = 0;
    IDIO_CLOSURE_CALL_TIME (c).tv_nsec = 0;
    IDIO_CLOSURE_RU_UTIME (c).tv_sec = 0;
    IDIO_CLOSURE_RU_UTIME (c).tv_usec = 0;
    IDIO_CLOSURE_RU_STIME (c).tv_sec = 0;
    IDIO_CLOSURE_RU_STIME (c).tv_usec = 0;
#endif

    idio_create_properties (c);
    if (idio_S_nil != sigstr) {
	IDIO osh = idio_open_output_string_handle_C ();
	int printed = 0;
	while (idio_S_nil != sigstr) {
	    IDIO pname = IDIO_PAIR_H (sigstr);

	    /*
	     * Check for last (varargs) arg
	     */
	    if (idio_S_nil == IDIO_PAIR_T (sigstr)) {
		if (idio_S_false != pname) {
		    if (printed) {
			idio_display_C (" ", osh);
		    }

		    idio_display_C ("[", osh);
		    idio_display (pname, osh);
		    idio_display_C ("]", osh);
		}
		break;
	    }

	    if (printed) {
		idio_display_C (" ", osh);
	    } else {
		printed = 1;
	    }

	    idio_display (pname, osh);

	    sigstr = IDIO_PAIR_T (sigstr);
	}
	idio_set_property (c, idio_KW_sigstr, idio_get_output_string (osh));
    }

    if (idio_S_nil != docstr) {
	idio_set_property (c, idio_KW_docstr_raw, docstr);
    }

    if (idio_S_nil != srcloc) {
	idio_set_property (c, idio_KW_source, srcloc);
    }

    return c;
}

int idio_isa_closure (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_CLOSURE);
}

int idio_isa_function (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_closure (o) ||
	    idio_isa_primitive (o));
}

void idio_free_closure (IDIO c)
{
    IDIO_ASSERT (c);
    IDIO_TYPE_ASSERT (closure, c);

    IDIO_GC_FREE (c->u.closure, sizeof (idio_closure_t));
}

IDIO_DEFINE_PRIMITIVE1_DS ("function?", functionp, (IDIO o), "o", "\
test if `o` is a procedure			\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a procedure, ``#f`` otherwise	\n\
						\n\
A procedure can be either an Idio (closure) or C (primitive) defined function\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_function (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("setter", setter, (IDIO p), "p", "\
return the setter of `p`			\n\
						\n\
:param p: procedure				\n\
:type p: function				\n\
:return: the setter of `p`			\n\
")
{
    IDIO_ASSERT (p);

    if (!(idio_isa_primitive (p) ||
	  idio_isa_closure (p))) {
	/*
	 * Test case: closure-errors/not-function.idio
	 *
	 * setter #f
	 */
	idio_error_param_type ("function", p, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Test Case: closure-errors/not-found.idio
     *
     * setter (function #n #t)
     *
     * This generates an ^rt-hash-key-not-found-error
     */
    IDIO setter = idio_ref_property (p, idio_KW_setter, idio_S_nil);

    return setter;
}

void idio_closure_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (functionp);
    IDIO_ADD_PRIMITIVE (setter);

    /*
     * NB Can't set setter's setter until it's been added as a
     * primitive
     */
    IDIO setter = idio_module_symbol_value_recurse (idio_S_setter, idio_Idio_module_instance (), idio_S_nil);
    idio_create_properties (setter);
}

void idio_init_closure ()
{
    idio_module_table_register (idio_closure_add_primitives, NULL, NULL);

    idio_closure_vtable = idio_vtable (IDIO_TYPE_CLOSURE);

    idio_vtable_add_method (idio_closure_vtable,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_closure));

    idio_vtable_add_method (idio_closure_vtable,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_util_method_2string));
}
