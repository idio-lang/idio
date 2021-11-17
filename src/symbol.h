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
 * symbol.h
 *
 */

#ifndef SYMBOL_H
#define SYMBOL_H

#define IDIO_SYMBOL_DECL(n)		IDIO idio_S_ ## n
#define IDIO_SYMBOL_DEF(iname,cname)	idio_S_ ## cname = idio_symbols_C_intern (iname, sizeof (iname) - 1);

extern IDIO_SYMBOL_DECL (2string);
extern IDIO_SYMBOL_DECL (C_struct);
extern IDIO_SYMBOL_DECL (after);
extern IDIO_SYMBOL_DECL (ampersand);
extern IDIO_SYMBOL_DECL (and);
extern IDIO_SYMBOL_DECL (append);
extern IDIO_SYMBOL_DECL (append_string);
extern IDIO_SYMBOL_DECL (apply);
extern IDIO_SYMBOL_DECL (asterisk);
extern IDIO_SYMBOL_DECL (before);
extern IDIO_SYMBOL_DECL (begin);
extern IDIO_SYMBOL_DECL (block);
extern IDIO_SYMBOL_DECL (both);
extern IDIO_SYMBOL_DECL (break);
extern IDIO_SYMBOL_DECL (class);
extern IDIO_SYMBOL_DECL (colon_caret);
extern IDIO_SYMBOL_DECL (colon_dollar);
extern IDIO_SYMBOL_DECL (colon_eq);
extern IDIO_SYMBOL_DECL (colon_plus);
extern IDIO_SYMBOL_DECL (colon_star);
extern IDIO_SYMBOL_DECL (colon_tilde);
extern IDIO_SYMBOL_DECL (computed);
extern IDIO_SYMBOL_DECL (concatenate_string);
extern IDIO_SYMBOL_DECL (cond);
extern IDIO_SYMBOL_DECL (continue);
extern IDIO_SYMBOL_DECL (deep);
extern IDIO_SYMBOL_DECL (define);
extern IDIO_SYMBOL_DECL (define_infix_operator);
extern IDIO_SYMBOL_DECL (define_postfix_operator);
extern IDIO_SYMBOL_DECL (define_template);
extern IDIO_SYMBOL_DECL (dloads);
extern IDIO_SYMBOL_DECL (dot);
extern IDIO_SYMBOL_DECL (dynamic);
extern IDIO_SYMBOL_DECL (dynamic_let);
extern IDIO_SYMBOL_DECL (dynamic_unset);
extern IDIO_SYMBOL_DECL (else);
extern IDIO_SYMBOL_DECL (environ);
extern IDIO_SYMBOL_DECL (environ_let);
extern IDIO_SYMBOL_DECL (environ_unset);
extern IDIO_SYMBOL_DECL (eq);
extern IDIO_SYMBOL_DECL (eq_gt);
extern IDIO_SYMBOL_DECL (eqp);
extern IDIO_SYMBOL_DECL (equalp);
extern IDIO_SYMBOL_DECL (eqvp);
extern IDIO_SYMBOL_DECL (error);
extern IDIO_SYMBOL_DECL (escape);
extern IDIO_SYMBOL_DECL (escape_block);
extern IDIO_SYMBOL_DECL (escape_from);
extern IDIO_SYMBOL_DECL (excl_star);
extern IDIO_SYMBOL_DECL (excl_tilde);
extern IDIO_SYMBOL_DECL (exit);
extern IDIO_SYMBOL_DECL (fixed_template);
extern IDIO_SYMBOL_DECL (function);
extern IDIO_SYMBOL_DECL (functionp);
extern IDIO_SYMBOL_DECL (gt);
extern IDIO_SYMBOL_DECL (if);
extern IDIO_SYMBOL_DECL (include);
extern IDIO_SYMBOL_DECL (killed);
extern IDIO_SYMBOL_DECL (init);
extern IDIO_SYMBOL_DECL (left);
extern IDIO_SYMBOL_DECL (let);
extern IDIO_SYMBOL_DECL (letrec);
extern IDIO_SYMBOL_DECL (list);
extern IDIO_SYMBOL_DECL (load);
extern IDIO_SYMBOL_DECL (load_handle);
extern IDIO_SYMBOL_DECL (local);
extern IDIO_SYMBOL_DECL (lt);
extern IDIO_SYMBOL_DECL (map);
extern IDIO_SYMBOL_DECL (module);
extern IDIO_SYMBOL_DECL (none);
extern IDIO_SYMBOL_DECL (not);
extern IDIO_SYMBOL_DECL (num_eq);
extern IDIO_SYMBOL_DECL (op);
extern IDIO_SYMBOL_DECL (or);
extern IDIO_SYMBOL_DECL (pair);
extern IDIO_SYMBOL_DECL (pair_separator);
extern IDIO_SYMBOL_DECL (param);
extern IDIO_SYMBOL_DECL (pct_module_export);
extern IDIO_SYMBOL_DECL (pct_module_import);
extern IDIO_SYMBOL_DECL (ph);
extern IDIO_SYMBOL_DECL (pipe);
extern IDIO_SYMBOL_DECL (predef);
extern IDIO_SYMBOL_DECL (profile);
extern IDIO_SYMBOL_DECL (prompt_at);
extern IDIO_SYMBOL_DECL (pt);
extern IDIO_SYMBOL_DECL (quasiquote);
extern IDIO_SYMBOL_DECL (quote);
extern IDIO_SYMBOL_DECL (return);
extern IDIO_SYMBOL_DECL (right);
extern IDIO_SYMBOL_DECL (root);
extern IDIO_SYMBOL_DECL (set);
extern IDIO_SYMBOL_DECL (setter);
extern IDIO_SYMBOL_DECL (shallow);
extern IDIO_SYMBOL_DECL (subshell);
extern IDIO_SYMBOL_DECL (super);
extern IDIO_SYMBOL_DECL (template);
extern IDIO_SYMBOL_DECL (template_expand);
extern IDIO_SYMBOL_DECL (this);
extern IDIO_SYMBOL_DECL (toplevel);
extern IDIO_SYMBOL_DECL (trap);
extern IDIO_SYMBOL_DECL (unquote);
extern IDIO_SYMBOL_DECL (unquotesplicing);
extern IDIO_SYMBOL_DECL (virtualisation_WSL);

extern IDIO_SYMBOL_DECL (char);
extern IDIO_SYMBOL_DECL (schar);
extern IDIO_SYMBOL_DECL (uchar);
extern IDIO_SYMBOL_DECL (short);
extern IDIO_SYMBOL_DECL (ushort);
extern IDIO_SYMBOL_DECL (int);
extern IDIO_SYMBOL_DECL (uint);
extern IDIO_SYMBOL_DECL (long);
extern IDIO_SYMBOL_DECL (ulong);
extern IDIO_SYMBOL_DECL (longlong);
extern IDIO_SYMBOL_DECL (ulonglong);
extern IDIO_SYMBOL_DECL (float);
extern IDIO_SYMBOL_DECL (double);
extern IDIO_SYMBOL_DECL (longdouble);

extern IDIO idio_properties_hash;

void idio_property_error_nil_object (char const *msg, IDIO c_location);
void idio_properties_error_not_found (char const *msg, IDIO o, IDIO c_location);
void idio_property_error_no_properties (char const *msg, IDIO c_location);
void idio_property_error_key_not_found (IDIO key, IDIO c_location);

void idio_free_symbol (IDIO s);
int idio_isa_symbol (IDIO s);
IDIO idio_symbols_C_intern (char const *s, size_t blen);
IDIO idio_symbols_string_intern (IDIO str);

#define IDIO_SYMBOLS_C_INTERN(s)	idio_symbols_C_intern (s, sizeof (s) - 1)

IDIO idio_gensym (char const *pref_prefix, size_t blen);

IDIO idio_ref_properties (IDIO o, IDIO args);
void idio_set_properties (IDIO o, IDIO properties);
void idio_create_properties (IDIO o);
void idio_delete_properties (IDIO o);
IDIO idio_ref_property (IDIO o, IDIO property, IDIO args);
void idio_set_property (IDIO o, IDIO property, IDIO value);

void idio_init_symbol (void);

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
