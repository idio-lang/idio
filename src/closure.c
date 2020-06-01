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
 * closure.c
 *
 */

#include "idio.h"

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
IDIO idio_closure (size_t code_pc, size_t code_len, IDIO frame, IDIO env, IDIO sigstr, IDIO docstr)
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

    IDIO_FPRINTF (stderr, "idio_closure: %10p = (%10p %10p)\n", c, code_pc, env);

    IDIO_GC_ALLOC (c->u.closure, sizeof (idio_closure_t));

    IDIO_CLOSURE_GREY (c) = NULL;
    IDIO_CLOSURE_CODE_PC (c) = code_pc;
    IDIO_CLOSURE_CODE_LEN (c) = code_len;
    IDIO_CLOSURE_FRAME (c) = frame;
    IDIO_CLOSURE_ENV (c) = env;
#ifdef IDIO_VM_PERF
    IDIO_CLOSURE_CALLED (c) = 0;
    IDIO_CLOSURE_CALL_TIME (c).tv_sec = 0;
    IDIO_CLOSURE_CALL_TIME (c).tv_nsec = 0;
#endif

    idio_properties_create (c);
    if (idio_S_nil != sigstr) {
	idio_property_set (c, idio_KW_sigstr, sigstr);
    }
    if (idio_S_nil != docstr) {
	idio_property_set (c, idio_KW_docstr_raw, docstr);
    }

    return c;
}

int idio_isa_closure (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_CLOSURE);
}

int idio_isa_procedure (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_closure (o) ||
	    idio_isa_primitive (o));
}

void idio_free_closure (IDIO c)
{
    IDIO_ASSERT (c);
    IDIO_TYPE_ASSERT (closure, c);

    idio_gc_stats_free (sizeof (idio_closure_t));

    free (c->u.closure);
}

IDIO_DEFINE_PRIMITIVE1_DS ("function?", functionp, (IDIO o), "o", "\
test if `o` is a procedure			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a procedure, #f otherwise	\n\
						\n\
A procedure can be either an Idio (closure) or C (primitive) defined function\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_procedure (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("setter", setter, (IDIO p), "p", "\
return the setter of `p`			\n\
						\n\
:param p: procedure				\n\
						\n\
:return: the setter of `p`			\n\
")
{
    IDIO_ASSERT (p);

    if (!(idio_isa_primitive (p) ||
	  idio_isa_closure (p))) {
	idio_error_param_type ("primitive|closure", p, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO setter = idio_property_get (p, idio_KW_setter, IDIO_LIST1 (idio_S_false));

    if (idio_S_false == setter) {
	idio_error_C ("no setter defined", p, IDIO_C_FUNC_LOCATION ());
    }

    return setter;
}

void idio_init_closure ()
{
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
    idio_properties_create (setter);
}

void idio_final_closure ()
{
}

