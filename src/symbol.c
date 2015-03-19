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

int idio_symbol_C_eqp (void *s1, void *s2)
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
    idio_symbols_hash = idio_hash (1<<7, idio_symbol_C_eqp, idio_symbol_C_hash);
    idio_gc_protect (idio_symbols_hash);

    IDIO_HASH_FLAGS (idio_symbols_hash) |= IDIO_HASH_FLAG_STRING_KEYS;
}

void idio_final_symbol ()
{
    idio_gc_expose (idio_symbols_hash);
}

IDIO idio_symbol_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    IDIO_FPRINTF (stderr, "idio_symbol: '%s'\n", s_C);

    IDIO o = idio_gc_get (IDIO_TYPE_SYMBOL);

    IDIO_GC_ALLOC (o->u.symbol, sizeof (idio_symbol_t));
    IDIO_SYMBOL_STRING (o) = idio_string_C (s_C);
    
    return o;
}

int idio_isa_symbol (IDIO s)
{
    IDIO_ASSERT (s);

    return idio_isa (s, IDIO_TYPE_SYMBOL);
}

void idio_free_symbol (IDIO s)
{
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (symbol, s);

    free (s->u.symbol);
}

IDIO idio_symbols_C_intern (char *s)
{
    IDIO_C_ASSERT (s);

    IDIO r = idio_hash_get (idio_symbols_hash, s);

    if (idio_S_nil == r) {
	r = idio_symbol_C (s);
	idio_hash_put (idio_symbols_hash, s, r);
    }

    idio_dump (idio_symbols_hash, 4);

    return r;
}

IDIO idio_symbols_string_intern (IDIO str)
{
    IDIO_ASSERT (str);

    IDIO_TYPE_ASSERT (string, str);
    
    char *s_C = idio_string_as_C (str);
    
    IDIO r = idio_symbols_C_intern (s_C);

    free (s_C);
	
    return r;
}

