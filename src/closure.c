/*
 * Copyright (c) 2015, 2017 Ian Fitchet <idf(at)idio-lang.org>
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

IDIO idio_closure (size_t code, IDIO frame, IDIO env)
{
    IDIO_C_ASSERT (code);
    IDIO_ASSERT (frame);
    IDIO_ASSERT (env);

    if (idio_S_nil != frame) {
	IDIO_TYPE_ASSERT (frame, frame);
    }
    IDIO_TYPE_ASSERT (module, env);

    IDIO c = idio_gc_get (IDIO_TYPE_CLOSURE);

    IDIO_FPRINTF (stderr, "idio_closure: %10p = (%10p %10p)\n", c, code, env);

    IDIO_GC_ALLOC (c->u.closure, sizeof (idio_closure_t)); 

    IDIO_CLOSURE_GREY (c) = NULL;
    IDIO_CLOSURE_CODE (c) = code;
    IDIO_CLOSURE_FRAME (c) = frame;
    IDIO_CLOSURE_ENV (c) = env;
    IDIO_CLOSURE_PROPERTIES (c) = idio_S_nil;
#ifdef IDIO_VM_PERF
    IDIO_CLOSURE_CALLED (c) = 0;
    IDIO_CLOSURE_CALL_TIME (c).tv_sec = 0;
    IDIO_CLOSURE_CALL_TIME (c).tv_nsec = 0;
#endif

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

IDIO_DEFINE_PRIMITIVE1 ("procedure?", procedurep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_procedure (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("setter", setter, (IDIO p))
{
    IDIO_ASSERT (p);

    IDIO kwt = idio_S_nil;
    
    if (idio_isa_primitive (p)) {
	kwt = IDIO_PRIMITIVE_PROPERTIES (p);
    } else if (idio_isa_closure (p)) {
	kwt = IDIO_CLOSURE_PROPERTIES (p);
    } else {
	idio_error_param_type ("primitive|closure", p, IDIO_C_LOCATION ("setter"));
    }

    IDIO setter = idio_keyword_get (kwt, idio_KW_setter, IDIO_LIST1 (idio_S_false));

    if (idio_S_false == setter) {
	idio_error_C ("no setter defined", IDIO_LIST1 (p), IDIO_C_LOCATION ("setter"));
    }

    return setter;
}

IDIO idio_closure_procedure_properties (IDIO p)
{
    IDIO_ASSERT (p);

    if (idio_isa_primitive (p)) {
	return IDIO_PRIMITIVE_PROPERTIES (p);
    } else if (idio_isa_closure (p)) {
	return IDIO_CLOSURE_PROPERTIES (p);
    } else {
	idio_error_param_type ("primitive|closure", p, IDIO_C_LOCATION ("%procedure-properties"));
    }

    /* notreached */
    return idio_S_unspec;
}    

IDIO_DEFINE_PRIMITIVE1 ("%procedure-properties", procedure_properties, (IDIO p))
{
    IDIO_ASSERT (p);

    return idio_closure_procedure_properties (p);
}

IDIO idio_closure_set_procedure_properties (IDIO p, IDIO v)
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    if (! (idio_S_nil == v ||
	   idio_isa_hash (v))) {
	idio_error_param_type ("hash", v, IDIO_C_LOCATION ("%set-procedure-properties"));
    }

    if (idio_isa_primitive (p)) {
	IDIO_PRIMITIVE_PROPERTIES (p) = v;
    } else if (idio_isa_closure (p)) {
	IDIO_CLOSURE_PROPERTIES (p) = v;
    } else {
	idio_error_param_type ("primitive|closure", p, IDIO_C_LOCATION ("%set-procedure-properties!"));
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2 ("%set-procedure-properties!", set_procedure_properties, (IDIO p, IDIO v))
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    return idio_closure_set_procedure_properties (p, v);
}

void idio_init_closure ()
{
}

void idio_closure_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (procedurep);
    IDIO_ADD_PRIMITIVE (setter);
    IDIO_ADD_PRIMITIVE (procedure_properties);
    IDIO_ADD_PRIMITIVE (set_procedure_properties);

    /*
     * NB Can't set setter's setter until it's been added as a
     * primitive
     */
    IDIO setter = idio_module_symbol_value_recurse (idio_S_setter, idio_Idio_module_instance (), idio_S_nil);
    
    IDIO kwt = idio_closure_procedure_properties (setter);

    if (idio_S_nil == kwt) {
	/*
	 * See closure.idio for why  a keyword-table of size 4.
	 */
	kwt = idio_hash_make_keyword_table (IDIO_LIST1 (idio_fixnum (4)));
    }

    idio_closure_set_procedure_properties (setter, kwt);
}

void idio_final_closure ()
{
}

