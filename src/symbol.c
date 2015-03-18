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
 * symbol.c
 * 
 */

#include "idio.h"

static IDIO idio_symbols_hash = idio_S_nil;

int idio_symbol_C_eqp (IDIO f, void *s1, void *s2)
{
    /*
     * We should only be here for idio_symbols_hash key comparisons
     * but hash keys default to idio_S_nil
     */
    if (idio_S_nil == s1 ||
	idio_S_nil == s2) {
	return 0;
    }

    return strcmp ((char *) s1, (char *) s2);
}

size_t idio_symbol_C_hash (IDIO h, void *s)
{
    size_t hvalue = idio_hash_hashval_string_C (strlen ((char *) s), s);
    
    return (hvalue & IDIO_HASH_MASK (h));
}

void idio_init_symbol ()
{
    idio_symbols_hash = idio_hash (idio_G_frame, 1<<7, idio_symbol_C_eqp, idio_symbol_C_hash);
    idio_gc_protect (idio_G_frame, idio_symbols_hash);

    IDIO_HASH_FLAGS (idio_symbols_hash) |= IDIO_HASH_FLAG_STRING_KEYS;
}

void idio_final_symbol ()
{
    idio_symbols_hash = idio_S_nil;
}

IDIO idio_symbol_C (IDIO f, const char *s_C)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (s_C);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_symbol: '%s'\n", s_C);

    IDIO o = idio_get (f, IDIO_TYPE_SYMBOL);

    IDIO_ALLOC (f, o->u.symbol, sizeof (idio_symbol_t));
    IDIO_SYMBOL_STRING (o) = idio_string_C (f, s_C);
    
    return o;
}

int idio_isa_symbol (IDIO f, IDIO s)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (s);

    return idio_isa (f, s, IDIO_TYPE_SYMBOL);
}

void idio_free_symbol (IDIO f, IDIO s)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    free (s->u.symbol);
}

IDIO idio_symbols_C_intern (IDIO f, char *s)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (s);

    IDIO r = idio_hash_get (f, idio_symbols_hash, s);

    if (idio_S_nil == r) {
	r = idio_symbol_C (f, s);
	idio_hash_put (f, idio_symbols_hash, s, r);
    }

    idio_dump (f, idio_symbols_hash, 4);

    return r;
}

IDIO idio_symbols_string_intern (IDIO f, IDIO str)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (str);

    IDIO_TYPE_ASSERT (string, str);
    
    char *s_C = idio_string_as_C (f, str);
    
    IDIO r = idio_symbols_C_intern (f, s_C);

    free (s_C);
	
    return r;
}

