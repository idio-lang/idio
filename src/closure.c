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

IDIO idio_closure (IDIO f, IDIO args, IDIO body, IDIO frame)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (args);
    IDIO_ASSERT (body);
    IDIO_ASSERT (frame);

    IDIO c = idio_get (f, IDIO_TYPE_CLOSURE);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_closure: %10p = (%10p %10p %10p)\n", c, args, body, frame);

    IDIO_ALLOC (f, c->u.closure, sizeof (idio_closure_t));

    IDIO_CLOSURE_GREY (c) = NULL;
    IDIO_CLOSURE_ARGS (c) = args;
    IDIO_CLOSURE_BODY (c) = body;
    IDIO_CLOSURE_FRAME (c) = frame;

    return c;
}

int idio_isa_closure (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return idio_isa (f, o, IDIO_TYPE_CLOSURE);
}

void idio_free_closure (IDIO f, IDIO c)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (c);

    IDIO_C_ASSERT (idio_isa_closure (f, c));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_closure_t);

    free (c->u.closure);
}

