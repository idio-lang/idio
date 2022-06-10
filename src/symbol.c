 /*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "keyword.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static IDIO idio_symbols_hash = idio_S_nil;
IDIO idio_properties_hash = idio_S_nil;

/* types */
IDIO_SYMBOL_DECL (fixnum);
IDIO_SYMBOL_DECL (constant_idio);
IDIO_SYMBOL_DECL (constant_token);
IDIO_SYMBOL_DECL (constant_i_code);
IDIO_SYMBOL_DECL (constant_unicode);
IDIO_SYMBOL_DECL (string);
IDIO_SYMBOL_DECL (substring);
IDIO_SYMBOL_DECL (symbol);
IDIO_SYMBOL_DECL (keyword);
IDIO_SYMBOL_DECL (pair);
IDIO_SYMBOL_DECL (array);
IDIO_SYMBOL_DECL (hash);
IDIO_SYMBOL_DECL (closure);
IDIO_SYMBOL_DECL (primitive);
IDIO_SYMBOL_DECL (bignum);
IDIO_SYMBOL_DECL (module);
IDIO_SYMBOL_DECL (frame);
IDIO_SYMBOL_DECL (handle);
IDIO_SYMBOL_DECL (struct_type);
IDIO_SYMBOL_DECL (struct_instance);
IDIO_SYMBOL_DECL (thread);
IDIO_SYMBOL_DECL (continuation);
IDIO_SYMBOL_DECL (bitset);
IDIO_SYMBOL_DECL (c_char);
IDIO_SYMBOL_DECL (c_schar);
IDIO_SYMBOL_DECL (c_uchar);
IDIO_SYMBOL_DECL (c_short);
IDIO_SYMBOL_DECL (c_ushort);
IDIO_SYMBOL_DECL (c_int);
IDIO_SYMBOL_DECL (c_uint);
IDIO_SYMBOL_DECL (c_long);
IDIO_SYMBOL_DECL (c_ulong);
IDIO_SYMBOL_DECL (c_longlong);
IDIO_SYMBOL_DECL (c_ulonglong);
IDIO_SYMBOL_DECL (c_float);
IDIO_SYMBOL_DECL (c_double);
IDIO_SYMBOL_DECL (c_longdouble);
IDIO_SYMBOL_DECL (c_pointer);
IDIO_SYMBOL_DECL (c_void);

IDIO_SYMBOL_DECL (2string);
IDIO_SYMBOL_DECL (C_struct);
IDIO_SYMBOL_DECL (after);
IDIO_SYMBOL_DECL (ampersand);
IDIO_SYMBOL_DECL (and);
IDIO_SYMBOL_DECL (append);
IDIO_SYMBOL_DECL (append_string);
IDIO_SYMBOL_DECL (apply);
IDIO_SYMBOL_DECL (asterisk);
IDIO_SYMBOL_DECL (before);
IDIO_SYMBOL_DECL (begin);
IDIO_SYMBOL_DECL (block);
IDIO_SYMBOL_DECL (both);
IDIO_SYMBOL_DECL (break);
IDIO_SYMBOL_DECL (class);
IDIO_SYMBOL_DECL (colon_caret);
IDIO_SYMBOL_DECL (colon_dollar);
IDIO_SYMBOL_DECL (colon_eq);
IDIO_SYMBOL_DECL (colon_plus);
IDIO_SYMBOL_DECL (colon_star);
IDIO_SYMBOL_DECL (colon_tilde);
IDIO_SYMBOL_DECL (computed);
IDIO_SYMBOL_DECL (concatenate_string);
IDIO_SYMBOL_DECL (cond);
IDIO_SYMBOL_DECL (continue);
IDIO_SYMBOL_DECL (deep);
IDIO_SYMBOL_DECL (define);
IDIO_SYMBOL_DECL (define_infix_operator);
IDIO_SYMBOL_DECL (define_postfix_operator);
IDIO_SYMBOL_DECL (define_template);
IDIO_SYMBOL_DECL (2display_string);
IDIO_SYMBOL_DECL (dloads);
IDIO_SYMBOL_DECL (dot);
IDIO_SYMBOL_DECL (dynamic);
IDIO_SYMBOL_DECL (dynamic_let);
IDIO_SYMBOL_DECL (dynamic_unset);
IDIO_SYMBOL_DECL (else);
IDIO_SYMBOL_DECL (environ);
IDIO_SYMBOL_DECL (environ_let);
IDIO_SYMBOL_DECL (environ_unset);
IDIO_SYMBOL_DECL (eq);
IDIO_SYMBOL_DECL (eq_gt);
IDIO_SYMBOL_DECL (eqp);
IDIO_SYMBOL_DECL (equalp);
IDIO_SYMBOL_DECL (eqvp);
IDIO_SYMBOL_DECL (error);
IDIO_SYMBOL_DECL (escape);
IDIO_SYMBOL_DECL (escape_block);
IDIO_SYMBOL_DECL (escape_from);
IDIO_SYMBOL_DECL (excl_star);
IDIO_SYMBOL_DECL (excl_tilde);
IDIO_SYMBOL_DECL (exit);
IDIO_SYMBOL_DECL (fixed_template);
IDIO_SYMBOL_DECL (function);
IDIO_SYMBOL_DECL (function_name);
IDIO_SYMBOL_DECL (functionp);
IDIO_SYMBOL_DECL (gt);
IDIO_SYMBOL_DECL (if);
IDIO_SYMBOL_DECL (include);
IDIO_SYMBOL_DECL (init);
IDIO_SYMBOL_DECL (killed);
IDIO_SYMBOL_DECL (left);
IDIO_SYMBOL_DECL (let);
IDIO_SYMBOL_DECL (letrec);
IDIO_SYMBOL_DECL (list);
IDIO_SYMBOL_DECL (load);
IDIO_SYMBOL_DECL (load_handle);
IDIO_SYMBOL_DECL (local);
IDIO_SYMBOL_DECL (lt);
IDIO_SYMBOL_DECL (map);
IDIO_SYMBOL_DECL (members);
IDIO_SYMBOL_DECL (module);
IDIO_SYMBOL_DECL (none);
IDIO_SYMBOL_DECL (not);
IDIO_SYMBOL_DECL (num_eq);
IDIO_SYMBOL_DECL (op);
IDIO_SYMBOL_DECL (or);
IDIO_SYMBOL_DECL (pair);
IDIO_SYMBOL_DECL (pair_separator);
IDIO_SYMBOL_DECL (param);
IDIO_SYMBOL_DECL (pct_module_export);
IDIO_SYMBOL_DECL (pct_module_import);
IDIO_SYMBOL_DECL (ph);
IDIO_SYMBOL_DECL (pipe);
IDIO_SYMBOL_DECL (predef);
IDIO_SYMBOL_DECL (profile);
IDIO_SYMBOL_DECL (prompt_at);
IDIO_SYMBOL_DECL (pt);
IDIO_SYMBOL_DECL (quasiquote);
IDIO_SYMBOL_DECL (quote);
IDIO_SYMBOL_DECL (return);
IDIO_SYMBOL_DECL (right);
IDIO_SYMBOL_DECL (root);
IDIO_SYMBOL_DECL (set);
IDIO_SYMBOL_DECL (set_value_index);
IDIO_SYMBOL_DECL (setter);
IDIO_SYMBOL_DECL (shallow);
IDIO_SYMBOL_DECL (struct_instance_2string);
IDIO_SYMBOL_DECL (subshell);
IDIO_SYMBOL_DECL (super);
IDIO_SYMBOL_DECL (template);
IDIO_SYMBOL_DECL (template_expand);
IDIO_SYMBOL_DECL (this);
IDIO_SYMBOL_DECL (toplevel);
IDIO_SYMBOL_DECL (pct_trap);
IDIO_SYMBOL_DECL (typename);
IDIO_SYMBOL_DECL (unquote);
IDIO_SYMBOL_DECL (unquotesplicing);
IDIO_SYMBOL_DECL (value_index);
IDIO_SYMBOL_DECL (virtualisation_WSL);

IDIO_SYMBOL_DECL (char);
IDIO_SYMBOL_DECL (schar);
IDIO_SYMBOL_DECL (uchar);
IDIO_SYMBOL_DECL (short);
IDIO_SYMBOL_DECL (ushort);
IDIO_SYMBOL_DECL (int);
IDIO_SYMBOL_DECL (uint);
IDIO_SYMBOL_DECL (long);
IDIO_SYMBOL_DECL (ulong);
IDIO_SYMBOL_DECL (longlong);
IDIO_SYMBOL_DECL (ulonglong);
IDIO_SYMBOL_DECL (float);
IDIO_SYMBOL_DECL (double);
IDIO_SYMBOL_DECL (longdouble);

static void idio_symbol_error (char const *msg, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_symbol_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_symbol_format_error (char const *msg, IDIO s, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (s);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_symbol_error (msg, c_location);

    /* notreached */
}

void idio_property_nil_object_error (char const *msg, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_parameter_nil_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_properties_not_found_error (char const *msg, IDIO o, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_hash_key_not_found_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       o));

    /* notreached */
}

static void idio_property_key_not_found_error (IDIO key, IDIO c_location)
{
    IDIO_ASSERT (key);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("key not found", msh);

    idio_error_raise_cont (idio_condition_rt_hash_key_not_found_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       key));

    /* notreached */
}

int idio_symbol_C_eqp (const void *s1, const void *s2)
{
    /*
     * We should only be here for idio_symbols_hash key comparisons
     * but hash keys default to idio_S_nil
     */
    if (idio_S_nil == s1 ||
	idio_S_nil == s2) {
	return 0;
    }

    /*
     * There are no useful restrictions on the length of a symbol.
     */
    return (0 == strcmp ((char const *) s1, (char const *) s2));
}

idio_hi_t idio_symbol_C_hash (IDIO h, const void *s)
{
    idio_hi_t hvalue = (uintptr_t) s;

    if (idio_S_nil != s) {
	hvalue = idio_hash_default_hash_C_string_C_MurmurOAAT_32 (s);
    }

    return (hvalue & IDIO_HASH_MASK (h));
}

/*
 * WARNING:
 *
 * You have no reason to call idio_symbol_C()!  Almost certainly you
 * will end up with two conflicting symbols and you will have the
 * equivalent of
 *
 *   (eq? 'foo 'foo) => #f
 *
 * Which is hard to debug!
 *
 * You should be calling idio_symbols_C_intern() which will check for
 * an existing symbol of the same name for you.
 */
static IDIO idio_symbol_C (char const *s_C, size_t const blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO o = idio_gc_get (IDIO_TYPE_SYMBOL);
    o->vtable = idio_vtable (IDIO_TYPE_SYMBOL);

    IDIO_GC_ALLOC (IDIO_SYMBOL_S (o), blen + 1);
    memcpy (IDIO_SYMBOL_S (o), s_C, blen);
    IDIO_SYMBOL_S (o)[blen] = '\0';

    IDIO_SYMBOL_BLEN (o) = blen;

    IDIO_SYMBOL_FLAGS (o) = IDIO_SYMBOL_FLAG_NONE;

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

}

IDIO idio_symbols_C_intern (char const *sym_C, size_t const blen)
{
    IDIO_C_ASSERT (sym_C);

    IDIO sym = idio_hash_ref (idio_symbols_hash, sym_C);

    if (idio_S_unspec == sym) {
	sym = idio_symbol_C (sym_C, blen);
	idio_hash_put (idio_symbols_hash, IDIO_SYMBOL_S (sym), sym);
    }

    return sym;
}

IDIO idio_symbols_string_intern (IDIO str)
{
    IDIO_ASSERT (str);
    IDIO_TYPE_ASSERT (string, str);

    size_t size = 0;
    char *sC = idio_string_as_C (str, &size);

    /*
     * Use size + 1 to avoid a truncation warning -- we're just seeing
     * if sC includes a NUL
     */
    size_t C_size = idio_strnlen (sC, size + 1);
    if (C_size != size) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  idio_symbols_string_intern() is called from
	 * the primitive {symbols} and uses already existing symbols.
	 */
	IDIO_GC_FREE (sC, size);

	idio_symbol_format_error ("symbol: contains an ASCII NUL", str, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_symbols_C_intern (sC, size);

    IDIO_GC_FREE (sC, size);

    return r;
}

static uintmax_t idio_gensym_id = 1;

IDIO idio_gensym (char const *pref_prefix, size_t const blen)
{
    char *prefix = "g";
    size_t prefix_len = 1;

    if (NULL != pref_prefix) {
	prefix = (char *) pref_prefix;
	prefix_len = blen;
    }

    /*
     * strlen ("/") == 1
     * strlen (uintmax_t) == 20
     * NUL
     */
    size_t sym_len = prefix_len + 1 + 20;
    char buf[sym_len + 1];

    IDIO sym;

    for (;idio_gensym_id;idio_gensym_id++) {
	size_t blen = idio_snprintf (buf, sym_len, "%s/%" PRIuMAX, prefix, idio_gensym_id);

	sym = idio_hash_ref (idio_symbols_hash, buf);

	if (idio_S_unspec == sym) {
	    sym = idio_symbols_C_intern (buf, blen);
	    IDIO_SYMBOL_FLAGS (sym) |= IDIO_SYMBOL_FLAG_GENSYM;
	    return sym;
	}
    }

    /*
     * Test Case: ??
     *
     * We've looped over a uintmax_t and not found a free symbol which
     * is an impressive combination of size and that to have found all
     * those symbols you've used any number of times the amount of
     * addressable memory on the system.
     *
     * There should be a prize, I think.  Well done.
     */
    idio_error_printf (IDIO_C_FUNC_LOCATION (), "You've used all the symbols!");

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("gensym", gensym, (IDIO args), "[prefix]", "\
generate a new *unique* symbol using `prefix` followed by ``/``	\n\
							\n\
Such *gensyms* are not guaranteed to be unique if saved.\n\
							\n\
:param prefix: prefix string, defaults to ``\"g\"``	\n\
:type prefix: string or symbol, optional	\n\
:return: unique symbol				\n\
:rtype: symbol					\n\
")
{
    IDIO_ASSERT (args);

    char *prefix_C = NULL;
    size_t prefix_C_len = 0;
    int free_me = 0;

    if (idio_isa_pair (args)) {
	IDIO prefix = IDIO_PAIR_H (args);

	if (idio_isa_string (prefix)) {
	    prefix_C = idio_string_as_C (prefix, &prefix_C_len);

	    /*
	     * Use prefix_C_len + 1 to avoid a truncation warning --
	     * we're just seeing if prefix_C includes a NUL
	     */
	    size_t C_size = idio_strnlen (prefix_C, prefix_C_len + 1);
	    if (C_size != prefix_C_len) {
		/*
		 * Test Case: symbol-errors/gensym-prefix-bad-format.idio
		 *
		 * gensym (join-string (make-string 1 U+0) '("hello" "world"))
		 */
		IDIO_GC_FREE (prefix_C, prefix_C_len);

		idio_symbol_format_error ("gensym: prefix contains an ASCII NUL", prefix, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    free_me = 1;
	} else if (idio_isa_symbol (prefix)) {
	    prefix_C = IDIO_SYMBOL_S (prefix);
	    prefix_C_len = IDIO_SYMBOL_BLEN (prefix);
	} else {
	    /*
	     * Test Case: symbol-errors/gensym-prefix-bad-type.idio
	     *
	     * gensym #t
	     */
	    idio_error_param_type ("string|symbol", prefix, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    IDIO sym = idio_gensym (prefix_C, prefix_C_len);

    if (free_me) {
	IDIO_GC_FREE (prefix_C, prefix_C_len);
    }

    return sym;
}

IDIO_DEFINE_PRIMITIVE1_DS ("symbol?", symbol_p, (IDIO o), "o", "\
test if `o` is a symbol				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a symbol, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_symbol_gensymp (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_isa_symbol (o) &&
	IDIO_SYMBOL_FLAGS (o) & IDIO_SYMBOL_FLAG_GENSYM) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("gensym?", gensym_p, (IDIO o), "o", "\
test if `o` is a gensym				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a gensym, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_symbol_gensymp (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("symbol->string", symbol2string, (IDIO s), "s", "\
convert symbol `s` into a string		\n\
						\n\
:param s: symbol to convert			\n\
:type s: symbol					\n\
:return: string					\n\
:rtype: string					\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: symbol-errors/symbol2string-bad-type.idio
     *
     * symbol->string #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, s);

    return idio_string_C_len (IDIO_SYMBOL_S (s), IDIO_SYMBOL_BLEN (s));
}

IDIO_DEFINE_PRIMITIVE0_DS ("symbols", symbols, (), "", "\
return all known symbols			\n\
						\n\
:return: all known symbols			\n\
:rtype: list					\n\
")
{
    /* the hash keys are strings */
    IDIO strings = idio_hash_keys_to_list (idio_symbols_hash);

    IDIO r = idio_S_nil;

    while (idio_S_nil != strings) {
	r = idio_pair (idio_symbols_string_intern (IDIO_PAIR_H (strings)), r);
	strings = IDIO_PAIR_T (strings);
    }

    return r;
}

IDIO idio_ref_properties (IDIO o, IDIO args)
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (args);

    if (idio_S_nil == o) {
	/*
	 * Test Case: symbol-errors/properties-nil.idio
	 *
	 * %properties #n
	 */
	idio_property_nil_object_error ("value is #n", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO properties = idio_hash_ref (idio_properties_hash, o);

    if (idio_S_unspec == properties) {
	if (idio_isa_pair (args)) {
	    return IDIO_PAIR_H (args);
	} else {
	    /*
	     * Test Case: symbol-errors/properties-non-existent.idio
	     *
	     * %properties (gensym)
	     */
	    idio_debug ("o=%s\n", o);
	    idio_properties_not_found_error ("no properties exist", o, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return properties;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%properties", properties_ref, (IDIO o, IDIO args), "o [default]", "\
return the properties table for `o`		\n\
						\n\
:param o: value to get properties for		\n\
:type o: any non-``#n``				\n\
:param default: value to return if no properties exist	\n\
:type default: any, optional			\n\
:return: properties table			\n\
:rtype: keyword table				\n\
:raise ^rt-parameter-nil-error:			\n\
:raise ^rt-hash-key-not-found-error:		\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (args);

    return idio_ref_properties (o, args);
}

void idio_set_properties (IDIO o, IDIO properties)
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (properties);
    IDIO_TYPE_ASSERT (hash, properties);

    if (idio_S_nil == o) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  The user interface is protected.
	 */
	idio_property_nil_object_error ("value is #n", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_hash_set (idio_properties_hash, o, properties);
}

IDIO_DEFINE_PRIMITIVE2_DS ("%set-properties!", properties_set, (IDIO o, IDIO properties), "o properties", "\
set the properties table for `o` to `properties`	\n\
						\n\
:param o: value to set properties for		\n\
:type o: any non-``#n``				\n\
:param properties: properties table		\n\
:type properties: keyword table			\n\
:return: ``#<unspec>``				\n\
:raise ^rt-parameter-nil-error:			\n\
						\n\
.. seealso:: :ref:`make-keyword-table <make-keyword-table>`	\n\
		for a means to create a proprty table	\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (properties);

    /*
     * Test Case: symbol-errors/set-properties-bad-properties-type.idio
     *
     * %set-properties! #t #t
     */
    IDIO_USER_TYPE_ASSERT (hash, properties);

    idio_set_properties (o, properties);

    return idio_S_unspec;
}

void idio_create_properties (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_S_nil == o) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There is no user interface to this.
	 */
	idio_property_nil_object_error ("object is #n", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_hash_set (idio_properties_hash, o, idio_hash_make_keyword_table (IDIO_LIST1 (idio_fixnum (8))));
}

void idio_share_properties (IDIO o1, IDIO o2)
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    if (idio_S_nil == o1) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There is no user interface to this.
	 */
	idio_property_nil_object_error ("o1 is #n", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (idio_S_nil == o2) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There is no user interface to this.
	 */
	idio_property_nil_object_error ("o2 is #n", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO p = idio_hash_ref (idio_properties_hash, o1);
    if (idio_S_unspec != p) {
	idio_hash_set (idio_properties_hash, o2, p);
    }
}

void idio_delete_properties (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_S_nil == o) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  There is no user interface to this.
	 */
	idio_property_nil_object_error ("object is #n", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_hash_delete (idio_properties_hash, o);
}

IDIO idio_ref_property (IDIO o, IDIO property, IDIO args)
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (property);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (keyword, property);

    if (idio_S_nil == o) {
	/*
	 * Test Case: symbol-errors/property-nil.idio
	 *
	 * %property #n :name
	 */
	idio_property_nil_object_error ("value is #n", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO properties = idio_hash_ref (idio_properties_hash, o);

    if (idio_S_unspec == properties) {
	if (idio_isa_pair (args)) {
	    return IDIO_PAIR_H (args);
	} else {
	    /*
	     * Test Case: symbol-errors/property-properties-non-existent.idio
	     *
	     * %property (gensym) :name
	     */
	    idio_properties_not_found_error ("no properties exist", o, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (idio_S_nil == properties) {
	/*
	 * Test Case: ??
	 *
	 * Coding error?
	 */
	idio_properties_not_found_error ("properties is #n", o, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO value = idio_hash_ref (properties, property);

    if (idio_S_unspec == value) {
	if (idio_isa_pair (args)) {
	    return IDIO_PAIR_H (args);
	} else {
	    /*
	     * Test Case: symbol-errors/property-non-existent.idio
	     *
	     * s := (gensym)
	     * %set-properties! s (make-keyword-table)
	     * %property s :name
	     */
	    idio_property_key_not_found_error (property, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return value;
}

IDIO_DEFINE_PRIMITIVE2V_DS ("%property", get_property, (IDIO o, IDIO property, IDIO args), "o kw [default]", "\
return the property `kw` for `o`		\n\
						\n\
:param o: value to get properties for		\n\
:type o: any non-``#n``				\n\
:param kw: property				\n\
:type kw: keyword				\n\
:param default: value to return if no such property exists	\n\
:type default: any, optional			\n\
:return: property value				\n\
:rtype: any					\n\
:raise ^rt-parameter-nil-error:			\n\
:raise ^rt-hash-key-not-found-error:		\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (property);
    IDIO_ASSERT (args);

    /*
     * Test Case: symbol-errors/property-bad-keyword-type.idio
     *
     * %property #n #t
     */
    IDIO_USER_TYPE_ASSERT (keyword, property);

    return idio_ref_property (o, property, args);
}

void idio_set_property (IDIO o, IDIO property, IDIO value)
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (property);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (keyword, property);

    if (idio_S_nil == o) {
	/*
	 * Test Case: symbol-errors/set-property-nil.idio
	 *
	 * %set-property! #n :name #t
	 */
	idio_property_nil_object_error ("value is #n", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO properties = idio_hash_ref (idio_properties_hash, o);

    if (idio_S_nil == properties) {
	/*
	 * Test Case: ??
	 *
	 * Coding error?
	 */
	idio_properties_not_found_error ("properties is #n", o, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    /*
     * Auto-vivify properties when setting a property
     */
    if (idio_S_unspec == properties) {
	properties = idio_hash_make_keyword_table (IDIO_LIST1 (idio_fixnum (4)));
	idio_hash_put (idio_properties_hash, o, properties);
    }

    idio_hash_set (properties, property, value);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%set-property!", set_property, (IDIO o, IDIO property, IDIO value), "o kw v", "\
set the property `kw` for `o` to `v`		\n\
						\n\
:param o: value to set properties for		\n\
:type o: non-``#n``				\n\
:param kw: property				\n\
:type kw: keyword				\n\
:param v: value					\n\
:type v: any					\n\
:return: ``#<unspec>``				\n\
:raise ^rt-parameter-nil-error:			\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (property);
    IDIO_ASSERT (value);

    /*
     * Test Case: symbol-errors/set-property-bad-keyword-type.idio
     *
     * %set-property! #n #t #t
     */
    IDIO_USER_TYPE_ASSERT (keyword, property);

    idio_set_property (o, property, value);

    return idio_S_unspec;
}

char *idio_symbol_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (symbol, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "%s", IDIO_SYMBOL_S (v));

    return r;
}

IDIO idio_symbol_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    size_t size = 0;
    char *C_r = idio_symbol_as_C_string (v, &size, 0, idio_S_nil, 0);

    IDIO r = idio_string_C_len (C_r, size);

    IDIO_GC_FREE (C_r, size);

    return r;
}

void idio_symbol_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (gensym);
    IDIO_ADD_PRIMITIVE (symbol_p);
    IDIO_ADD_PRIMITIVE (gensym_p);
    IDIO_ADD_PRIMITIVE (symbol2string);
    IDIO_ADD_PRIMITIVE (symbols);
    IDIO_ADD_PRIMITIVE (properties_ref);
    IDIO_ADD_PRIMITIVE (properties_set);
    IDIO_ADD_PRIMITIVE (get_property);
    IDIO_ADD_PRIMITIVE (set_property);
}

void idio_final_symbol ()
{
    idio_gc_remove_weak_object (idio_properties_hash);
}

void idio_init_symbol ()
{
    idio_module_table_register (idio_symbol_add_primitives, idio_final_symbol, NULL);

    /*
     * IDIO_HASH_COUNT (idio_symbols_hash)
     * empty	=> 3k
     * test	=> 8k
     */
    idio_symbols_hash = idio_hash (8 * 1024, idio_symbol_C_eqp, idio_symbol_C_hash, idio_S_nil, idio_S_nil);
    idio_gc_protect_auto (idio_symbols_hash);
    IDIO_HASH_FLAGS (idio_symbols_hash) |= IDIO_HASH_FLAG_STRING_KEYS;

    /* types */
    IDIO_SYMBOL_DEF ("fixnum", fixnum);
    IDIO_SYMBOL_DEF ("constant", constant_idio);
    IDIO_SYMBOL_DEF ("constant_TOKEN", constant_token);
    IDIO_SYMBOL_DEF ("constant_I_CODE", constant_i_code);
    IDIO_SYMBOL_DEF ("unicode", constant_unicode);
    IDIO_SYMBOL_DEF ("string", string);
    IDIO_SYMBOL_DEF ("substring", substring);
    IDIO_SYMBOL_DEF ("symbol", symbol);
    IDIO_SYMBOL_DEF ("keyword", keyword);
    IDIO_SYMBOL_DEF ("pair", pair);
    IDIO_SYMBOL_DEF ("array", array);
    IDIO_SYMBOL_DEF ("hash", hash);
    IDIO_SYMBOL_DEF ("closure", closure);
    IDIO_SYMBOL_DEF ("primitive", primitive);
    IDIO_SYMBOL_DEF ("bignum", bignum);
    IDIO_SYMBOL_DEF ("module", module);
    IDIO_SYMBOL_DEF ("frame", frame);
    IDIO_SYMBOL_DEF ("handle", handle);
    IDIO_SYMBOL_DEF ("struct-type", struct_type);
    IDIO_SYMBOL_DEF ("struct-instance", struct_instance);
    IDIO_SYMBOL_DEF ("thread", thread);
    IDIO_SYMBOL_DEF ("continuation", continuation);
    IDIO_SYMBOL_DEF ("bitset", bitset);
    IDIO_SYMBOL_DEF ("C/char", c_char);
    IDIO_SYMBOL_DEF ("C/schar", c_schar);
    IDIO_SYMBOL_DEF ("C/uchar", c_uchar);
    IDIO_SYMBOL_DEF ("C/short", c_short);
    IDIO_SYMBOL_DEF ("C/ushort", c_ushort);
    IDIO_SYMBOL_DEF ("C/int", c_int);
    IDIO_SYMBOL_DEF ("C/uint", c_uint);
    IDIO_SYMBOL_DEF ("C/long", c_long);
    IDIO_SYMBOL_DEF ("C/ulong", c_ulong);
    IDIO_SYMBOL_DEF ("C/longlong", c_longlong);
    IDIO_SYMBOL_DEF ("C/ulonglong", c_ulonglong);
    IDIO_SYMBOL_DEF ("C/float", c_float);
    IDIO_SYMBOL_DEF ("C/double", c_double);
    IDIO_SYMBOL_DEF ("C/longdouble", c_longdouble);
    IDIO_SYMBOL_DEF ("C/pointer", c_pointer);
    IDIO_SYMBOL_DEF ("C/void", c_void);

    IDIO_SYMBOL_DEF ("->string", 2string);
    IDIO_SYMBOL_DEF ("c_struct", C_struct);
    IDIO_SYMBOL_DEF ("after", after);
    IDIO_SYMBOL_DEF ("&", ampersand);
    IDIO_SYMBOL_DEF ("and", and);
    IDIO_SYMBOL_DEF ("append", append);
    IDIO_SYMBOL_DEF ("append-string", append_string);
    IDIO_SYMBOL_DEF ("apply", apply);
    IDIO_SYMBOL_DEF ("*", asterisk);
    IDIO_SYMBOL_DEF ("before", before);
    IDIO_SYMBOL_DEF ("begin", begin);
    IDIO_SYMBOL_DEF ("block", block);
    IDIO_SYMBOL_DEF ("both", both);
    IDIO_SYMBOL_DEF ("break", break);
    IDIO_SYMBOL_DEF ("class", class);
    IDIO_SYMBOL_DEF (":^", colon_caret);
    IDIO_SYMBOL_DEF (":$", colon_dollar);
    IDIO_SYMBOL_DEF (":=", colon_eq);
    IDIO_SYMBOL_DEF (":+", colon_plus);
    IDIO_SYMBOL_DEF (":*", colon_star);
    IDIO_SYMBOL_DEF (":~", colon_tilde);
    IDIO_SYMBOL_DEF ("computed", computed);
    IDIO_SYMBOL_DEF ("concatenate-string", concatenate_string);
    IDIO_SYMBOL_DEF ("cond", cond);
    IDIO_SYMBOL_DEF ("continue", continue);
    IDIO_SYMBOL_DEF ("deep", deep);
    IDIO_SYMBOL_DEF ("define", define);
    IDIO_SYMBOL_DEF ("define-infix-operator", define_infix_operator);
    IDIO_SYMBOL_DEF ("define-postfix-operator", define_postfix_operator);
    IDIO_SYMBOL_DEF ("define-template", define_template);
    IDIO_SYMBOL_DEF ("->display-string", 2display_string);
    IDIO_SYMBOL_DEF ("dloads", dloads);
    IDIO_SYMBOL_DEF (".", dot);
    IDIO_SYMBOL_DEF ("dynamic", dynamic);
    IDIO_SYMBOL_DEF ("dynamic-let", dynamic_let);
    IDIO_SYMBOL_DEF ("dynamic-unset", dynamic_unset);
    IDIO_SYMBOL_DEF ("else", else);
    IDIO_SYMBOL_DEF ("environ", environ);
    IDIO_SYMBOL_DEF ("environ-let", environ_let);
    IDIO_SYMBOL_DEF ("environ-unset", environ_unset);
    IDIO_SYMBOL_DEF ("=", eq);
    IDIO_SYMBOL_DEF ("=>", eq_gt);
    IDIO_SYMBOL_DEF ("eq?", eqp);
    IDIO_SYMBOL_DEF ("equal?", equalp);
    IDIO_SYMBOL_DEF ("eqv?", eqvp);
    IDIO_SYMBOL_DEF ("error", error);
    IDIO_SYMBOL_DEF ("escape", escape);
    IDIO_SYMBOL_DEF ("escape-block", escape_block);
    IDIO_SYMBOL_DEF ("escape-from", escape_from);
    IDIO_SYMBOL_DEF ("!*", excl_star);
    IDIO_SYMBOL_DEF ("!~", excl_tilde);
    IDIO_SYMBOL_DEF ("exit", exit);
    IDIO_SYMBOL_DEF ("fixed_template", fixed_template);
    IDIO_SYMBOL_DEF ("function", function);
    IDIO_SYMBOL_DEF ("function/name", function_name);
    IDIO_SYMBOL_DEF ("function+", functionp);
    IDIO_SYMBOL_DEF (">", gt);
    IDIO_SYMBOL_DEF ("if", if);
    IDIO_SYMBOL_DEF ("include", include);
    IDIO_SYMBOL_DEF ("init", init);
    IDIO_SYMBOL_DEF ("killed", killed);
    IDIO_SYMBOL_DEF ("left", left);
    IDIO_SYMBOL_DEF ("let", let);
    IDIO_SYMBOL_DEF ("letrec", letrec);
    IDIO_SYMBOL_DEF ("list", list);
    IDIO_SYMBOL_DEF ("load", load);
    IDIO_SYMBOL_DEF ("load-handle", load_handle);
    IDIO_SYMBOL_DEF ("local", local);
    IDIO_SYMBOL_DEF ("<", lt);
    IDIO_SYMBOL_DEF ("map", map);
    IDIO_SYMBOL_DEF ("members", members);
    IDIO_SYMBOL_DEF ("module", module);
    IDIO_SYMBOL_DEF ("none", none);
    IDIO_SYMBOL_DEF ("not", not);
    IDIO_SYMBOL_DEF ("eq", num_eq);
    IDIO_SYMBOL_DEF ("op", op);
    IDIO_SYMBOL_DEF ("or", or);
    IDIO_SYMBOL_DEF ("pair", pair);

    char buf[2];
    idio_snprintf (buf, 2, "%c", IDIO_PAIR_SEPARATOR);
    IDIO_SYMBOL_DEF (buf, pair_separator);

    IDIO_SYMBOL_DEF ("param", param);
    IDIO_SYMBOL_DEF ("%module-export", pct_module_export);
    IDIO_SYMBOL_DEF ("%module-import", pct_module_import);
    IDIO_SYMBOL_DEF ("ph", ph);
    IDIO_SYMBOL_DEF ("|", pipe);
    IDIO_SYMBOL_DEF ("predef", predef);
    IDIO_SYMBOL_DEF ("profile", profile);
    IDIO_SYMBOL_DEF ("prompt-at", prompt_at);
    IDIO_SYMBOL_DEF ("pt", pt);
    IDIO_SYMBOL_DEF ("quasiquote", quasiquote);
    IDIO_SYMBOL_DEF ("quote", quote);
    IDIO_SYMBOL_DEF ("return", return);
    IDIO_SYMBOL_DEF ("right", right);
    IDIO_SYMBOL_DEF ("root", root);
    IDIO_SYMBOL_DEF ("set!", set);
    IDIO_SYMBOL_DEF ("set-value-index!", set_value_index);
    IDIO_SYMBOL_DEF ("setter", setter);
    IDIO_SYMBOL_DEF ("shallow", shallow);
    IDIO_SYMBOL_DEF ("struct-instance->string", struct_instance_2string);
    IDIO_SYMBOL_DEF ("subshell", subshell);
    IDIO_SYMBOL_DEF ("super", super);
    IDIO_SYMBOL_DEF ("template", template);
    IDIO_SYMBOL_DEF ("template-expand", template_expand);
    IDIO_SYMBOL_DEF ("this", this);
    IDIO_SYMBOL_DEF ("toplevel", toplevel);
    IDIO_SYMBOL_DEF ("%trap", pct_trap);
    IDIO_SYMBOL_DEF ("typename", typename);
    IDIO_SYMBOL_DEF ("unquote", unquote);
    IDIO_SYMBOL_DEF ("unquotesplicing", unquotesplicing);
    IDIO_SYMBOL_DEF ("value-index", value_index);
    IDIO_SYMBOL_DEF ("virtualisation/WSL", virtualisation_WSL);

    IDIO_SYMBOL_DEF ("char", char);
    IDIO_SYMBOL_DEF ("schar", schar);
    IDIO_SYMBOL_DEF ("uchar", uchar);
    IDIO_SYMBOL_DEF ("short", short);
    IDIO_SYMBOL_DEF ("ushort", ushort);
    IDIO_SYMBOL_DEF ("int", int);
    IDIO_SYMBOL_DEF ("uint", uint);
    IDIO_SYMBOL_DEF ("long", long);
    IDIO_SYMBOL_DEF ("ulong", ulong);
    IDIO_SYMBOL_DEF ("longlong", longlong);
    IDIO_SYMBOL_DEF ("ulonglong", ulonglong);
    IDIO_SYMBOL_DEF ("float", float);
    IDIO_SYMBOL_DEF ("double", double);
    IDIO_SYMBOL_DEF ("longdouble", longdouble);

    /*
     * idio_properties_hash doesn't really live in symbol.c but we
     * need it up and running before primitives and closures get a
     * look in.
     *
     * weak keys otherwise the existence of any object in this hash
     * prevents it being freed!
     *
     * IDIO_HASH_COUNT (idio_properties_hash)
     * empty	=> 2k
     * test	=> 35k
     */
    idio_properties_hash = IDIO_HASH_EQP (16 * 1024);
    idio_gc_add_weak_object (idio_properties_hash);
    idio_gc_protect_auto (idio_properties_hash);

    idio_vtable_t *s_vt = idio_vtable (IDIO_TYPE_SYMBOL);

    idio_vtable_add_method (s_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_symbol));

    idio_vtable_add_method (s_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_symbol_method_2string));
}
