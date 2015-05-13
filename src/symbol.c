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

IDIO idio_S_C_struct;
IDIO idio_S_ampersand;
IDIO idio_S_and;
IDIO idio_S_append;
IDIO idio_S_apply;
IDIO idio_S_asterisk;
IDIO idio_S_begin;
IDIO idio_S_block;
IDIO idio_S_car;
IDIO idio_S_cdr;
IDIO idio_S_class;
IDIO idio_S_colon_eq;
IDIO idio_S_colon_plus;
IDIO idio_S_cond;
IDIO idio_S_cons;
IDIO idio_S_define;
IDIO idio_S_define_macro;
IDIO idio_S_dloads;
IDIO idio_S_dynamic;
IDIO idio_S_dynamic_let;
IDIO idio_S_else;
IDIO idio_S_eq_gt;
IDIO idio_S_error;
IDIO idio_S_escape;
IDIO idio_S_fixed_template;
IDIO idio_S_function;
IDIO idio_S_if;
IDIO idio_S_include;
IDIO idio_S_init;
IDIO idio_S_lambda;
IDIO idio_S_let;
IDIO idio_S_letrec;
IDIO idio_S_list;
IDIO idio_S_monitor;
IDIO idio_S_namespace;
IDIO idio_S_or;
IDIO idio_S_profile;
IDIO idio_S_quasiquote;
IDIO idio_S_quote;
IDIO idio_S_root;
IDIO idio_S_set;
IDIO idio_S_super;
IDIO idio_S_template;
IDIO idio_S_this;
IDIO idio_S_unquote;
IDIO idio_S_unquotesplicing;

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

size_t idio_symbol_C_hash (IDIO h, void *s)
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

    IDIO_GC_ALLOC (o->u.symbol, sizeof (idio_symbol_t));

    size_t blen = strlen (s_C);
    IDIO_GC_ALLOC (o->u.symbol->s, blen + 1);
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

    free (s->u.symbol);
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

IDIO idio_gensym ()
{
    char *prefix = "g";

    /*
     * strlen ("/") == 1
     * strlen (uintmax_t) == 20
     * NUL
     */
    char buf[strlen (prefix) + 1 + 20 + 1];

    IDIO sym;
    
    for (;idio_gensym_id;idio_gensym_id++) {
	sprintf (buf, "%s/%zd", prefix, idio_gensym_id);

	sym = idio_hash_get (idio_symbols_hash, buf);

	if (idio_S_unspec == sym) {
	    sym = idio_symbols_C_intern (buf);
	    return sym;
	}
    }

    idio_error_message ("gensym: looped!");
    IDIO_C_ASSERT (0);
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

IDIO_DEFINE_PRIMITIVE0 ("gensym", gensym, ())
{
    return idio_gensym ();
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

    idio_S_C_struct = idio_symbols_C_intern ("c_struct");
    idio_S_ampersand = idio_symbols_C_intern ("&");
    idio_S_and = idio_symbols_C_intern ("and");
    idio_S_append = idio_symbols_C_intern ("append");
    idio_S_apply = idio_symbols_C_intern ("apply");
    idio_S_asterisk = idio_symbols_C_intern ("*");
    idio_S_begin = idio_symbols_C_intern ("begin");
    idio_S_block = idio_symbols_C_intern ("block");
    idio_S_car = idio_symbols_C_intern ("car");
    idio_S_cdr = idio_symbols_C_intern ("cdr");
    idio_S_class = idio_symbols_C_intern ("class");
    idio_S_colon_eq = idio_symbols_C_intern (":=");
    idio_S_colon_plus = idio_symbols_C_intern (":+");
    idio_S_cond = idio_symbols_C_intern ("cond");
    idio_S_cons = idio_symbols_C_intern ("cons");
    idio_S_define = idio_symbols_C_intern ("define");
    idio_S_define_macro = idio_symbols_C_intern ("define-macro");
    idio_S_dloads = idio_symbols_C_intern ("dloads");
    idio_S_dynamic = idio_symbols_C_intern ("dynamic");
    idio_S_dynamic_let = idio_symbols_C_intern ("dynamic-let");
    idio_S_else = idio_symbols_C_intern ("else");
    idio_S_eq_gt = idio_symbols_C_intern ("=>");
    idio_S_error = idio_symbols_C_intern ("error");
    idio_S_escape = idio_symbols_C_intern ("escape");
    idio_S_fixed_template = idio_symbols_C_intern ("fixed_template");
    idio_S_function = idio_symbols_C_intern ("function");
    idio_S_if = idio_symbols_C_intern ("if");
    idio_S_include = idio_symbols_C_intern ("include");
    idio_S_init = idio_symbols_C_intern ("init");
    idio_S_lambda = idio_symbols_C_intern ("lambda");
    idio_S_let = idio_symbols_C_intern ("let");
    idio_S_letrec = idio_symbols_C_intern ("letrec");
    idio_S_list = idio_symbols_C_intern ("list");
    idio_S_monitor = idio_symbols_C_intern ("monitor");
    idio_S_namespace = idio_symbols_C_intern ("namespace");
    idio_S_or = idio_symbols_C_intern ("or");
    idio_S_profile = idio_symbols_C_intern ("profile");
    idio_S_quasiquote = idio_symbols_C_intern ("quasiquote");
    idio_S_quote = idio_symbols_C_intern ("quote");
    idio_S_root = idio_symbols_C_intern ("root");
    idio_S_set = idio_symbols_C_intern ("set!");
    idio_S_super = idio_symbols_C_intern ("super");
    idio_S_template = idio_symbols_C_intern ("template");
    idio_S_this = idio_symbols_C_intern ("this");
    idio_S_unquote = idio_symbols_C_intern ("unquote");
    idio_S_unquotesplicing = idio_symbols_C_intern ("unquotesplicing");
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

