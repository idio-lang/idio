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
 * keyword.c
 *
 */

#include "idio.h"

static IDIO idio_keywords_hash = idio_S_nil;

IDIO_KEYWORD_DECL (docstr);
IDIO_KEYWORD_DECL (docstr_raw);
IDIO_KEYWORD_DECL (handle);
IDIO_KEYWORD_DECL (line);
IDIO_KEYWORD_DECL (name);
IDIO_KEYWORD_DECL (setter);
IDIO_KEYWORD_DECL (sigstr);
IDIO_KEYWORD_DECL (source);

void idio_keyword_error_key_not_found (IDIO key, IDIO loc)
{
    IDIO_ASSERT (key);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("key not found", msh);

    IDIO c = idio_struct_instance (idio_condition_rt_hash_key_not_found_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_S_nil,
					       key));

    idio_raise_condition (idio_S_true, c);
}

int idio_keyword_C_eqp (void *s1, void *s2)
{
    /*
     * We should only be here for idio_keywords_hash key comparisons
     * but hash keys default to idio_S_nil
     */
    if (idio_S_nil == s1 ||
	idio_S_nil == s2) {
	return 0;
    }

    return (0 == strcmp ((const char *) s1, (const char *) s2));
}

idio_hi_t idio_keyword_C_hash (IDIO h, void *s)
{
    size_t hvalue = (uintptr_t) s;

    if (idio_S_nil != s) {
	hvalue = idio_hash_hashval_string_C (strlen ((char *) s), s);
    }

    return (hvalue & IDIO_HASH_MASK (h));
}

IDIO idio_keyword_C_len (const char *s_C, size_t blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO o = idio_gc_get (IDIO_TYPE_KEYWORD);

    /* IDIO_GC_ALLOC (o->u.keyword, sizeof (idio_keyword_t)); */

    IDIO_GC_ALLOC (IDIO_KEYWORD_S (o), blen + 1);

    strncpy (IDIO_KEYWORD_S (o), s_C, blen);
    *(IDIO_KEYWORD_S (o) + blen) = '\0';

    return o;
}

IDIO idio_keyword_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_keyword_C_len (s_C, strlen (s_C));
}

IDIO_DEFINE_PRIMITIVE1 ("make-keyword", make_keyword, (IDIO s))
{
    if (idio_isa_string (s)) {
	return idio_keywords_C_intern (idio_string_s (s));
    } else if (idio_isa_symbol (s)) {
	return idio_keyword_C (IDIO_SYMBOL_S (s));
    } else {
	idio_error_param_type ("string|symbol", s, IDIO_C_LOCATION ("make-keyword"));
    }

    return idio_S_notreached;
}

int idio_isa_keyword (IDIO s)
{
    IDIO_ASSERT (s);

    return idio_isa (s, IDIO_TYPE_KEYWORD);
}

void idio_free_keyword (IDIO s)
{
    IDIO_ASSERT (s);
    IDIO_TYPE_ASSERT (keyword, s);

    /* free (s->u.keyword); */
}

IDIO idio_keywords_C_intern (char *s)
{
    IDIO_C_ASSERT (s);

    IDIO sym = idio_hash_get (idio_keywords_hash, s);

    if (idio_S_unspec == sym) {
	sym = idio_keyword_C (s);
	idio_hash_put (idio_keywords_hash, IDIO_KEYWORD_S (sym), sym);
    }

    return sym;
}

IDIO idio_keywords_string_intern (IDIO str)
{
    IDIO_ASSERT (str);
    IDIO_TYPE_ASSERT (string, str);

    char *s_C = idio_string_as_C (str);

    IDIO r = idio_keywords_C_intern (s_C);

    free (s_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("keyword?", keyword_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_keyword (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("keyword->string", keyword2string, (IDIO kw))
{
    IDIO_ASSERT (kw);
    IDIO_VERIFY_PARAM_TYPE (keyword, kw);

    return idio_string_C (IDIO_KEYWORD_S (kw));
}

IDIO_DEFINE_PRIMITIVE0 ("keywords", keywords, ())
{
    return idio_hash_keys_to_list (idio_keywords_hash);
}

IDIO idio_hash_make_keyword_table (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_hash_make_hash (idio_list_append2 (IDIO_LIST2 (idio_S_eqp, idio_S_nil), args));
}

IDIO_DEFINE_PRIMITIVE0V ("make-keyword-table", make_keyword_table, (IDIO args))
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_hash_make_keyword_table (args);
}

IDIO idio_keyword_get (IDIO ht, IDIO kw, IDIO args)
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (keyword, kw);
    IDIO_TYPE_ASSERT (list, args);

    if (idio_S_nil == ht) {
	idio_error_param_nil ("keyword table", IDIO_C_LOCATION ("keyword-get"));

	return idio_S_notreached;
    }

    IDIO_TYPE_ASSERT (hash, ht);

    IDIO v = idio_hash_get (ht, kw);

    if (idio_S_unspec != v) {
	return v;
    }

    if (idio_S_nil == args) {
	idio_keyword_error_key_not_found (kw, IDIO_C_LOCATION ("keyword-get"));

	return idio_S_notreached;
    } else {
	return IDIO_PAIR_H (args);
    }
}

IDIO_DEFINE_PRIMITIVE2V ("keyword-get", keyword_get, (IDIO ht, IDIO kw, IDIO args))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (keyword, kw);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_keyword_get (ht, kw, args);
}

IDIO idio_keyword_set (IDIO ht, IDIO kw, IDIO v)
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (keyword, kw);

    if (idio_S_nil == ht) {
	idio_error_param_nil ("keyword table", IDIO_C_LOCATION ("keyword-set!"));

	return idio_S_notreached;
    }

    IDIO_TYPE_ASSERT (hash, ht);

    idio_hash_put (ht, kw, v);

    return idio_S_unspec;
}
IDIO_DEFINE_PRIMITIVE3 ("keyword-set!", keyword_set, (IDIO ht, IDIO kw, IDIO v))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (v);
    IDIO_VERIFY_PARAM_TYPE (keyword, kw);

    return idio_keyword_set (ht, kw, v);
}

void idio_init_keyword ()
{
    idio_keywords_hash = idio_hash (1<<7, idio_keyword_C_eqp, idio_keyword_C_hash, idio_S_nil, idio_S_nil);
    idio_gc_protect (idio_keywords_hash);
    IDIO_HASH_FLAGS (idio_keywords_hash) |= IDIO_HASH_FLAG_STRING_KEYS;

    IDIO_KEYWORD_DEF ("docstr", docstr);
    IDIO_KEYWORD_DEF ("docstr-raw", docstr_raw);
    IDIO_KEYWORD_DEF ("handle", handle);
    IDIO_KEYWORD_DEF ("line", line);
    IDIO_KEYWORD_DEF ("name", name);
    IDIO_KEYWORD_DEF ("setter", setter);
    IDIO_KEYWORD_DEF ("sigstr", sigstr);
    IDIO_KEYWORD_DEF ("source", source);
}

void idio_keyword_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (make_keyword);
    IDIO_ADD_PRIMITIVE (keyword_p);
    IDIO_ADD_PRIMITIVE (keyword2string);
    IDIO_ADD_PRIMITIVE (keywords);
    IDIO_ADD_PRIMITIVE (make_keyword_table);
    IDIO_ADD_PRIMITIVE (keyword_get);
    IDIO_ADD_PRIMITIVE (keyword_set);
}

void idio_final_keyword ()
{
    idio_gc_expose (idio_keywords_hash);
}

