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
 * primitive.c
 * 
 */

#include "idio.h"

IDIO idio_primitive_C (IDIO f, IDIO (*func) (IDIO frame, IDIO args), const char *name_C)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (name_C);

    IDIO o = idio_get (f, IDIO_TYPE_PRIMITIVE_C);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_primitive_C: %10p = (%10p)\n", o, func);

    IDIO_ALLOC (f, o->u.primitive_C, sizeof (idio_primitive_C_t));
    
    IDIO_PRIMITIVE_C_F (o) = func;

    size_t l = strlen (name_C);
    IDIO_C_ASSERT (l);

    IDIO_ALLOC (f, IDIO_PRIMITIVE_C_NAME (o), l + 1);

    /*
      No point in using strncpy as we have just relied on name_C being
      NUL terminated with strlen...
     */
    strcpy (IDIO_PRIMITIVE_C_NAME (o), name_C);

    return o;
}

int idio_isa_primitive_C (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return idio_isa (f, o, IDIO_TYPE_PRIMITIVE_C);
}

void idio_free_primitive_C (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (idio_isa_primitive_C (f, o));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_primitive_C_t);

    free (o->u.primitive_C->name);
    free (o->u.primitive_C);
}

