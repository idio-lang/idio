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

IDIO_SYMBOL_DECL (C_struct);
IDIO_SYMBOL_DECL (after);
IDIO_SYMBOL_DECL (ampersand);
IDIO_SYMBOL_DECL (and);
IDIO_SYMBOL_DECL (append);
IDIO_SYMBOL_DECL (apply);
IDIO_SYMBOL_DECL (asterisk);
IDIO_SYMBOL_DECL (before);
IDIO_SYMBOL_DECL (begin);
IDIO_SYMBOL_DECL (block);
IDIO_SYMBOL_DECL (car);
IDIO_SYMBOL_DECL (cdr);
IDIO_SYMBOL_DECL (class);
IDIO_SYMBOL_DECL (colon_caret);
IDIO_SYMBOL_DECL (colon_eq);
IDIO_SYMBOL_DECL (colon_plus);
IDIO_SYMBOL_DECL (colon_star);
IDIO_SYMBOL_DECL (colon_tilde);
IDIO_SYMBOL_DECL (cond);
IDIO_SYMBOL_DECL (cons);
IDIO_SYMBOL_DECL (define);
IDIO_SYMBOL_DECL (define_macro);
IDIO_SYMBOL_DECL (define_operator);
IDIO_SYMBOL_DECL (dloads);
IDIO_SYMBOL_DECL (dynamic);
IDIO_SYMBOL_DECL (dynamic_let);
IDIO_SYMBOL_DECL (dynamic_unset);
IDIO_SYMBOL_DECL (else);
IDIO_SYMBOL_DECL (environ_let);
IDIO_SYMBOL_DECL (environ_unset);
IDIO_SYMBOL_DECL (eq);
IDIO_SYMBOL_DECL (eq_gt);
IDIO_SYMBOL_DECL (error);
IDIO_SYMBOL_DECL (escape);
IDIO_SYMBOL_DECL (ex_star);
IDIO_SYMBOL_DECL (ex_tilde);
IDIO_SYMBOL_DECL (fixed_template);
IDIO_SYMBOL_DECL (function);
IDIO_SYMBOL_DECL (gt);
IDIO_SYMBOL_DECL (if);
IDIO_SYMBOL_DECL (include);
IDIO_SYMBOL_DECL (init);
IDIO_SYMBOL_DECL (lambda);
IDIO_SYMBOL_DECL (let);
IDIO_SYMBOL_DECL (letrec);
IDIO_SYMBOL_DECL (list);
IDIO_SYMBOL_DECL (lt);
IDIO_SYMBOL_DECL (monitor);
IDIO_SYMBOL_DECL (namespace);
IDIO_SYMBOL_DECL (op);
IDIO_SYMBOL_DECL (or);
IDIO_SYMBOL_DECL (pair);
IDIO_SYMBOL_DECL (ph);
IDIO_SYMBOL_DECL (pipe);
IDIO_SYMBOL_DECL (pt);
IDIO_SYMBOL_DECL (profile);
IDIO_SYMBOL_DECL (quasiquote);
IDIO_SYMBOL_DECL (quote);
IDIO_SYMBOL_DECL (root);
IDIO_SYMBOL_DECL (set);
IDIO_SYMBOL_DECL (super);
IDIO_SYMBOL_DECL (template);
IDIO_SYMBOL_DECL (this);
IDIO_SYMBOL_DECL (unquote);
IDIO_SYMBOL_DECL (unquotesplicing);
IDIO_SYMBOL_DECL (unset);

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

    return (0 == strcmp ((const char *) s1, (const char *) s2));
}

idio_hi_t idio_symbol_C_hash (IDIO h, void *s)
{
    size_t hvalue = (uintptr_t) s;

    if (idio_S_nil != s) {
	hvalue = idio_hash_hashval_string_C (strlen ((char *) s), s);
    }
    
    return (hvalue & IDIO_HASH_MASK (h));
}

IDIO idio_symbol_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    IDIO o = idio_gc_get (IDIO_TYPE_SYMBOL);

    /* IDIO_GC_ALLOC (o->u.symbol, sizeof (idio_symbol_t)); */

    size_t blen = strlen (s_C);
    IDIO_GC_ALLOC (IDIO_SYMBOL_S (o), blen + 1);
    strcpy (IDIO_SYMBOL_S (o), s_C);
    
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

    /* free (s->u.symbol); */
}

IDIO idio_symbols_C_intern (char *s)
{
    IDIO_C_ASSERT (s);

    IDIO sym = idio_hash_get (idio_symbols_hash, s);

    if (idio_S_unspec == sym) {
	sym = idio_symbol_C (s);
	idio_hash_put (idio_symbols_hash, IDIO_SYMBOL_S (sym), sym);
    }

    return sym;
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

static uintmax_t idio_gensym_id = 1;

IDIO idio_gensym (char *pref_prefix)
{
    char *prefix = "g";

    if (NULL != pref_prefix) {
	prefix = pref_prefix;
    }

    /*
     * strlen ("/") == 1
     * strlen (uintmax_t) == 20
     * NUL
     */
    char buf[strlen (prefix) + 1 + 20 + 1];

    IDIO sym;
    
    for (;idio_gensym_id;idio_gensym_id++) {
	sprintf (buf, "%s/%" PRIuMAX, prefix, idio_gensym_id);

	sym = idio_hash_get (idio_symbols_hash, buf);

	if (idio_S_unspec == sym) {
	    sym = idio_symbols_C_intern (buf);
	    return sym;
	}
    }

    idio_error_printf (IDIO_C_LOCATION ("gensym"), "looped!");

    /* notreached */
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V ("gensym", gensym, (IDIO args))
{
    IDIO_ASSERT (args);

    char *prefix = NULL;
    int free_me = 0;
    
    if (idio_isa_pair (args)) {
	IDIO iprefix = IDIO_PAIR_H (args);

	if (idio_isa_string (iprefix)) {
	    prefix = idio_string_as_C (iprefix);
	    free_me = 1;
	} else if (idio_isa_symbol (iprefix)) {
	    prefix = IDIO_SYMBOL_S (iprefix);
	} else {
	    idio_error_param_type ("string|symbol", iprefix, IDIO_C_LOCATION ("gensym"));

	    /* notreached */
	    return idio_S_unspec;
	}
    }
    
    IDIO sym = idio_gensym (prefix);

    if (free_me) {
	free (prefix);
    }

    return sym;
}

IDIO_DEFINE_PRIMITIVE1 ("symbol?", symbol_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	r = idio_S_true;
    }
    
    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("symbol->string", symbol2string, (IDIO s))
{
    IDIO_ASSERT (s);

    IDIO_VERIFY_PARAM_TYPE (symbol, s);

    return idio_string_C (IDIO_SYMBOL_S (s));
}

IDIO_DEFINE_PRIMITIVE0 ("symbols", symbols, ())
{
    return idio_hash_keys_to_list (idio_symbols_hash);
}

void idio_init_symbol ()
{
    idio_symbols_hash = idio_hash (1<<7, idio_symbol_C_eqp, idio_symbol_C_hash);
    idio_gc_protect (idio_symbols_hash);
    IDIO_HASH_FLAGS (idio_symbols_hash) |= IDIO_HASH_FLAG_STRING_KEYS;

    IDIO_SYMBOL_DEF ("c_struct", C_struct);
    IDIO_SYMBOL_DEF ("&", ampersand);
    IDIO_SYMBOL_DEF ("after", after);
    IDIO_SYMBOL_DEF ("and", and);
    IDIO_SYMBOL_DEF ("append", append);
    IDIO_SYMBOL_DEF ("apply", apply);
    IDIO_SYMBOL_DEF ("*", asterisk);
    IDIO_SYMBOL_DEF ("before", before);
    IDIO_SYMBOL_DEF ("begin", begin);
    IDIO_SYMBOL_DEF ("block", block);
    IDIO_SYMBOL_DEF ("car", car);
    IDIO_SYMBOL_DEF ("cdr", cdr);
    IDIO_SYMBOL_DEF ("class", class);
    IDIO_SYMBOL_DEF (":^", colon_caret);
    IDIO_SYMBOL_DEF (":=", colon_eq);
    IDIO_SYMBOL_DEF (":+", colon_plus);
    IDIO_SYMBOL_DEF (":*", colon_star);
    IDIO_SYMBOL_DEF (":~", colon_tilde);
    IDIO_SYMBOL_DEF ("cond", cond);
    IDIO_SYMBOL_DEF ("cons", cons);
    IDIO_SYMBOL_DEF ("define", define);
    IDIO_SYMBOL_DEF ("define-macro", define_macro);
    IDIO_SYMBOL_DEF ("define-operator", define_operator);
    IDIO_SYMBOL_DEF ("dloads", dloads);
    IDIO_SYMBOL_DEF ("dynamic", dynamic);
    IDIO_SYMBOL_DEF ("dynamic-let", dynamic_let);
    IDIO_SYMBOL_DEF ("dynamic-unset", dynamic_unset);
    IDIO_SYMBOL_DEF ("else", else);
    IDIO_SYMBOL_DEF ("environ-let", environ_let);
    IDIO_SYMBOL_DEF ("environ-unset", environ_unset);
    IDIO_SYMBOL_DEF ("=", eq);
    IDIO_SYMBOL_DEF ("=>", eq_gt);
    IDIO_SYMBOL_DEF ("error", error);
    IDIO_SYMBOL_DEF ("escape", escape);
    IDIO_SYMBOL_DEF ("!*", ex_star);
    IDIO_SYMBOL_DEF ("!~", ex_tilde);
    IDIO_SYMBOL_DEF ("fixed_template", fixed_template);
    IDIO_SYMBOL_DEF ("function", function);
    IDIO_SYMBOL_DEF (">", gt);
    IDIO_SYMBOL_DEF ("if", if);
    IDIO_SYMBOL_DEF ("include", include);
    IDIO_SYMBOL_DEF ("init", init);
    IDIO_SYMBOL_DEF ("lambda", lambda);
    IDIO_SYMBOL_DEF ("let", let);
    IDIO_SYMBOL_DEF ("letrec", letrec);
    IDIO_SYMBOL_DEF ("list", list);
    IDIO_SYMBOL_DEF ("<", lt);
    IDIO_SYMBOL_DEF ("monitor", monitor);
    IDIO_SYMBOL_DEF ("namespace", namespace);
    IDIO_SYMBOL_DEF ("op", op);
    IDIO_SYMBOL_DEF ("or", or);
    IDIO_SYMBOL_DEF ("pair", pair);
    IDIO_SYMBOL_DEF ("ph", ph);
    IDIO_SYMBOL_DEF ("|", pipe);
    IDIO_SYMBOL_DEF ("pt", pt);
    IDIO_SYMBOL_DEF ("profile", profile);
    IDIO_SYMBOL_DEF ("quasiquote", quasiquote);
    IDIO_SYMBOL_DEF ("quote", quote);
    IDIO_SYMBOL_DEF ("root", root);
    IDIO_SYMBOL_DEF ("set!", set);
    IDIO_SYMBOL_DEF ("super", super);
    IDIO_SYMBOL_DEF ("template", template);
    IDIO_SYMBOL_DEF ("this", this);
    IDIO_SYMBOL_DEF ("unquote", unquote);
    IDIO_SYMBOL_DEF ("unquotesplicing", unquotesplicing);
    IDIO_SYMBOL_DEF ("unset", unset);
}

void idio_symbol_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (symbol_p);
    IDIO_ADD_PRIMITIVE (gensym);
    IDIO_ADD_PRIMITIVE (symbol2string);
    IDIO_ADD_PRIMITIVE (symbols);
}

void idio_final_symbol ()
{
    idio_gc_expose (idio_symbols_hash);
}

