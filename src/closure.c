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
 * closure.c
 * 
 */

#include "idio.h"

IDIO idio_closure (size_t code, IDIO env)
{
    IDIO_C_ASSERT (code);
    IDIO_ASSERT (env);

    IDIO c = idio_gc_get (IDIO_TYPE_CLOSURE);

    IDIO_FPRINTF (stderr, "idio_closure: %10p = (%10p %10p)\n", c, code, env);

    IDIO_GC_ALLOC (c->u.closure, sizeof (idio_closure_t));

    IDIO_CLOSURE_GREY (c) = NULL;
    IDIO_CLOSURE_CODE (c) = code;
    IDIO_CLOSURE_ENV (c) = env;

    return c;
}

int idio_isa_closure (IDIO o)
{
    IDIO_ASSERT (o);

    return idio_isa (o, IDIO_TYPE_CLOSURE);
}

void idio_free_closure (IDIO c)
{
    IDIO_ASSERT (c);

    IDIO_TYPE_ASSERT (closure, c);

    idio_gc_stats_free (sizeof (idio_closure_t));

    free (c->u.closure);
}

IDIO_DEFINE_PRIMITIVE1 ("procedure?", closurep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_closure (o)) {
	r = idio_S_true;
    }

    return r;
}

void idio_init_closure ()
{
}

void idio_closure_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (closurep)
}

void idio_final_closure ()
{
}

