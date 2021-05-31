/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

void idio_keyword_key_not_found_error (IDIO key, IDIO c_location)
{
    IDIO_ASSERT (key);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("keyword not found", msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_keyword_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       key));

    idio_raise_condition (idio_S_true, c);
}

void idio_keyword_format_error (char *msg, IDIO kw, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_keyword_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       kw));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
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
	hvalue = idio_hash_default_hash_C_string_C (strlen ((char *) s), s);
    }

    return (hvalue & IDIO_HASH_MASK (h));
}

IDIO idio_keyword_C_len (const char *s_C, size_t blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO o = idio_gc_get (IDIO_TYPE_KEYWORD);

    IDIO_GC_ALLOC (IDIO_KEYWORD_S (o), blen + 1);

    strncpy (IDIO_KEYWORD_S (o), s_C, blen + 1);
    IDIO_KEYWORD_S (o)[blen] = '\0';

    return o;
}

IDIO idio_keyword_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_keyword_C_len (s_C, strlen (s_C));
}

IDIO_DEFINE_PRIMITIVE1_DS ("make-keyword", make_keyword, (IDIO s), "s", "\
create a keyword from `s`			\n\
						\n\
:param s: keyword				\n\
:type size: symbol or string			\n\
:return: keyword				\n\
:rtype: keyword					\n\
")
{
    if (idio_isa_string (s)) {
	size_t size = 0;
	char *sC = idio_string_as_C (s, &size);

	size_t C_size = strlen (sC);
	if (C_size != size) {
	    /*
	     * Test Case: keyword-errors/make-keyword-bad-format.idio
	     *
	     * make-keyword (join-string (make-string 1 #U+0) '("hello" "world"))
	     */
	    IDIO_GC_FREE (sC);

	    idio_keyword_format_error ("keyword contains an ASCII NUL", s, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	IDIO r = idio_keywords_C_intern (sC);

	IDIO_GC_FREE (sC);

	return r;
    } else if (idio_isa_symbol (s)) {
	return idio_keywords_C_intern (IDIO_SYMBOL_S (s));
    } else {
	/*
	 * Test Case: keyword-errors/make-keyword-bad-type.idio
	 *
	 * make-keyword #t
	 */
	idio_error_param_type ("string|symbol", s, IDIO_C_FUNC_LOCATION ());
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

    /* IDIO_GC_FREE (s->u.keyword); */
}

IDIO idio_keywords_C_intern (char *s)
{
    IDIO_C_ASSERT (s);

    IDIO sym = idio_hash_ref (idio_keywords_hash, s);

    if (idio_S_unspec == sym) {
	sym = idio_keyword_C (s);
	idio_hash_put (idio_keywords_hash, IDIO_KEYWORD_S (sym), sym);
    }

    return sym;
}

IDIO_DEFINE_PRIMITIVE1_DS ("keyword?", keyword_p, (IDIO o), "o", "\
test if `o` is an keyword			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an keyword, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_keyword (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("keyword->string", keyword2string, (IDIO kw), "kw", "\
convert keyword `kw` to a string		\n\
						\n\
:param kw: keyword to convert			\n\
:type kw: keyword				\n\
						\n\
:return: string					\n\
")
{
    IDIO_ASSERT (kw);

    /*
     * Test Case: keyword-errors/keyword2string-bad-type.idio
     *
     * keyword->string #t
     */
    IDIO_USER_TYPE_ASSERT (keyword, kw);

    return idio_string_C (IDIO_KEYWORD_S (kw));
}

IDIO_DEFINE_PRIMITIVE0_DS ("keywords", keywords, (), "", "\
return a list of all keywords			\n\
						\n\
:return: list					\n\
")
{
    return idio_hash_keys_to_list (idio_keywords_hash);
}

IDIO idio_hash_make_keyword_table (IDIO args)
{
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (list, args);

    return idio_hash_make_hash (idio_list_append2 (IDIO_LIST2 (idio_S_eqp, idio_S_nil), args));
}

IDIO_DEFINE_PRIMITIVE0V_DS ("make-keyword-table", make_keyword_table, (IDIO args), "[size]", "\
used for constructing property tables		\n\
						\n\
:param size: size of underlying hash table	\n\
:type size: integer				\n\
						\n\
:return: keyword table				\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    return idio_hash_make_keyword_table (args);
}

IDIO idio_keyword_ref (IDIO ht, IDIO kw, IDIO args)
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (hash, ht);
    IDIO_TYPE_ASSERT (keyword, kw);
    IDIO_TYPE_ASSERT (list, args);

    IDIO v = idio_hash_ref (ht, kw);

    if (idio_S_unspec != v) {
	return v;
    }

    if (idio_S_nil == args) {
	/*
	 * Test Case: keyword-errors/keyword-ref-not-found.idio
	 *
	 * kwt := (make-keyword-table)
	 * keyword-ref kwt :foo
	 */
	idio_keyword_key_not_found_error (kw, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    } else {
	return IDIO_PAIR_H (args);
    }
}

IDIO_DEFINE_PRIMITIVE2V_DS ("keyword-ref", keyword_ref, (IDIO ht, IDIO kw, IDIO args), "kt kw [default]", "\
return the value indexed by keyword ``kw` in keyword	\n\
table ``ht``						\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
:param kw: keyword index				\n\
:type kw: keyword					\n\
:param default: a default value if ``kw`` not found	\n\
:type default: value					\n\
							\n\
:return: value (#unspec if ``key`` not found and no	\n\
	 ``default`` supplied)				\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (args);

    /*
     * Test Case: keyword-errors/keyword-ref-bad-type.idio
     *
     * keyword-ref #t #t
     */
    IDIO_USER_TYPE_ASSERT (hash, ht);
    /*
     * Test Case: keyword-errors/keyword-ref-bad-keyword-type.idio
     *
     * keyword-ref (make-keyword-table) #t
     */
    IDIO_USER_TYPE_ASSERT (keyword, kw);
    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    return idio_keyword_ref (ht, kw, args);
}

IDIO idio_keyword_set (IDIO ht, IDIO kw, IDIO v)
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (v);

    IDIO_TYPE_ASSERT (hash, ht);
    IDIO_TYPE_ASSERT (keyword, kw);

    idio_hash_put (ht, kw, v);

    return idio_S_unspec;
}
IDIO_DEFINE_PRIMITIVE3_DS ("keyword-set!", keyword_set, (IDIO ht, IDIO kw, IDIO v), "ht kw v", "\
set the index of ``kw` in hash table ``ht`` to ``v``	\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
:param kw: non-#n value					\n\
:type kw: any non-#n					\n\
:param v: value						\n\
:type v: a value					\n\
							\n\
:return: #unspec					\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (kw);
    IDIO_ASSERT (v);

    /*
     * Test Case: keyword-errors/keyword-set-bad-type.idio
     *
     * keyword-set! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (hash, ht);
    /*
     * Test Case: keyword-errors/keyword-set-bad-keyword-type.idio
     *
     * keyword-set! (make-keyword-table) #t #t
     */
    IDIO_USER_TYPE_ASSERT (keyword, kw);

    return idio_keyword_set (ht, kw, v);
}

void idio_keyword_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (make_keyword);
    IDIO_ADD_PRIMITIVE (keyword_p);
    IDIO_ADD_PRIMITIVE (keyword2string);
    IDIO_ADD_PRIMITIVE (keywords);
    IDIO_ADD_PRIMITIVE (make_keyword_table);
    IDIO_ADD_PRIMITIVE (keyword_ref);
    IDIO_ADD_PRIMITIVE (keyword_set);
}

void idio_init_keyword ()
{
    idio_module_table_register (idio_keyword_add_primitives, NULL);

    idio_keywords_hash = idio_hash (1<<7, idio_keyword_C_eqp, idio_keyword_C_hash, idio_S_nil, idio_S_nil);
    idio_gc_protect_auto (idio_keywords_hash);
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

