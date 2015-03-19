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
 * string.c
 * 
 */

#include "idio.h"

IDIO idio_string_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);
    
    IDIO_FPRINTF (stderr, "idio_string_C: %10p = '%s'\n", so, s_C);

    size_t blen = strlen (s_C);

    IDIO_GC_ALLOC (so->u.string, sizeof (idio_string_t));
    IDIO_GC_ALLOC (IDIO_STRING_S (so), blen + 1);
    
    memcpy (IDIO_STRING_S (so), s_C, blen);
    IDIO_STRING_S (so)[blen] = '\0';
    IDIO_STRING_BLEN (so) = blen;

    return so;
}

IDIO idio_string_C_array (size_t ns, char *a_C[])
{
    IDIO_C_ASSERT (a_C);

    IDIO so;

    so = idio_gc_get (IDIO_TYPE_STRING);
    
    size_t blen = 0;
    size_t ai;

    for (ai = 0; ai < ns; ai++) {
	blen += strlen (a_C[ai]);
    }

    IDIO_GC_ALLOC (so->u.string, sizeof (idio_string_t));
    IDIO_GC_ALLOC (IDIO_STRING_S (so), blen + 1);
    
    size_t ao = 0;
    for (ai = 0; ai < ns; ai++) {
	size_t sl = strlen (a_C[ai]);
	memcpy (IDIO_STRING_S (so) + ao, a_C[ai], sl);
	ao += sl;
    }
    
    IDIO_STRING_S (so)[blen] = '\0';
    IDIO_STRING_BLEN (so) = blen;

    return so;
}

IDIO idio_string_copy (IDIO string)
{
    IDIO_ASSERT (string);

    IDIO_TYPE_ASSERT (string, string);

    IDIO copy;

    char *s = idio_string_s (string);

    switch (string->type) {
    case IDIO_TYPE_STRING:
	{
	    copy = idio_string_C (s);
	    break;
	}
    case IDIO_TYPE_SUBSTRING:
	{
	    copy = idio_substring_offset (IDIO_SUBSTRING_PARENT (string),
					  IDIO_SUBSTRING_S (string) - IDIO_STRING_S (IDIO_SUBSTRING_PARENT (string)),
					  IDIO_SUBSTRING_BLEN (string));
	    break;
	}
    }

    return copy;
}

int idio_isa_string (IDIO so)
{
    IDIO_ASSERT (so);

    return (idio_isa (so, IDIO_TYPE_STRING) ||
	    idio_isa (so, IDIO_TYPE_SUBSTRING));
}

void idio_free_string (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    idio_gc_stats_free (sizeof (idio_string_t) + IDIO_STRING_BLEN (so));

    free (IDIO_STRING_S (so));
    free (so->u.string);
}

IDIO idio_substring_offset (IDIO p, size_t offset, size_t blen)
{
    IDIO_ASSERT (p);
    IDIO_C_ASSERT (blen);
            
    IDIO so = idio_gc_get (IDIO_TYPE_SUBSTRING);

    IDIO_FPRINTF (stderr, "idio_substring_offset: %10p = '%.*s'\n", so, blen, IDIO_STRING_S (p) + offset);

    IDIO_GC_ALLOC (so->u.substring, sizeof (idio_substring_t));

    IDIO_FPRINTF (stderr, "idio_substring_offset: %d@%d in '%.*s' -> '%.*s'\n", blen, offset, IDIO_STRING_BLEN (p), IDIO_STRING_S (p), blen, IDIO_STRING_S (p) + offset);

    IDIO_SUBSTRING_BLEN (so) = blen;
    IDIO_SUBSTRING_S (so) = IDIO_STRING_S (p) + offset;
    IDIO_SUBSTRING_PARENT (so) = p;

    return so;
}

int idio_isa_substring (IDIO so)
{
    IDIO_ASSERT (so);

    return idio_isa (so, IDIO_TYPE_SUBSTRING);
}

void idio_free_substring (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (substring, so);

    idio_gc_stats_free (sizeof (idio_substring_t));

    free (so->u.substring);
}

int idio_string_cmp_C (IDIO so, char *s_C)
{
    IDIO_ASSERT (so);
    IDIO_C_ASSERT (s_C);

    IDIO_TYPE_ASSERT (string, so);

    if (IDIO_IS_FREE (so)) {
	return 0;
    }

    int blen = strlen (s_C);
    IDIO_C_ASSERT (blen);
    
    if (IDIO_STRING_BLEN (so) < blen) {
	blen = IDIO_STRING_BLEN (so);
    }
    
    return strncmp (IDIO_STRING_S (so), s_C, blen);
}

size_t idio_string_blen (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    size_t blen = 0;

    switch (so->type) {
    case IDIO_TYPE_STRING:
	blen = IDIO_STRING_BLEN (so);
	break;
    case IDIO_TYPE_SUBSTRING:
	blen = IDIO_SUBSTRING_BLEN (so);
	break;
    default:
	{
	    char em[BUFSIZ];
	    sprintf (em, "idio_type_string: unexpected string type %d", so->type);
	    idio_error_add_C (em);
	    break;
	}
    }

    return blen;
}

char *idio_string_s (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    char *s = NULL;

    switch (so->type) {
    case IDIO_TYPE_STRING:
	s = IDIO_STRING_S (so);
	break;
    case IDIO_TYPE_SUBSTRING:
	s = IDIO_SUBSTRING_S (so);
	break;
    default:
	{
	    char em[BUFSIZ];
	    sprintf (em, "idio_type_string: unexpected string type %d", so->type);
	    idio_error_add_C (em);
	    break;
	}
    }

    return s;
}

/*
 * caller must free(3) this string
 */
char *idio_string_as_C (IDIO so)
{
    IDIO_ASSERT (so);

    IDIO_TYPE_ASSERT (string, so);

    char *s = NULL;

    switch (so->type) {
    case IDIO_TYPE_STRING:
	s = IDIO_STRING_S (so);
	break;
    case IDIO_TYPE_SUBSTRING:
	s = IDIO_SUBSTRING_S (so);
	break;
    default:
	{
	    char em[BUFSIZ];
	    sprintf (em, "idio_type_string: unexpected string type %d", so->type);
	    idio_error_add_C (em);
	    break;
	}
    }

    size_t blen = idio_string_blen (so);
    char *s_C = idio_alloc (blen + 1);

    memcpy (s_C, s, blen);
    s_C[blen] = '\0';

    return s_C;
}

