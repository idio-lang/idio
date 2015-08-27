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
 * symbol.h
 * 
 */

#ifndef SYMBOL_H
#define SYMBOL_H

#define IDIO_SYMBOL_DECL(n)		IDIO idio_S_ ## n
#define IDIO_SYMBOL_DEF(iname,cname)	idio_S_ ## cname = idio_symbols_C_intern (iname);

extern IDIO_SYMBOL_DECL (after);
extern IDIO_SYMBOL_DECL (ampersand);
extern IDIO_SYMBOL_DECL (and);
extern IDIO_SYMBOL_DECL (append);
extern IDIO_SYMBOL_DECL (apply);
extern IDIO_SYMBOL_DECL (asterisk);
extern IDIO_SYMBOL_DECL (before);
extern IDIO_SYMBOL_DECL (begin);
extern IDIO_SYMBOL_DECL (block);
extern IDIO_SYMBOL_DECL (car);
extern IDIO_SYMBOL_DECL (cdr);
extern IDIO_SYMBOL_DECL (c_struct);
extern IDIO_SYMBOL_DECL (class);
extern IDIO_SYMBOL_DECL (colon_caret);
extern IDIO_SYMBOL_DECL (colon_dollar);
extern IDIO_SYMBOL_DECL (colon_eq);
extern IDIO_SYMBOL_DECL (colon_plus);
extern IDIO_SYMBOL_DECL (colon_star);
extern IDIO_SYMBOL_DECL (colon_tilde);
extern IDIO_SYMBOL_DECL (cond);
extern IDIO_SYMBOL_DECL (cons);
extern IDIO_SYMBOL_DECL (define);
extern IDIO_SYMBOL_DECL (define_macro);
extern IDIO_SYMBOL_DECL (define_infix_operator);
extern IDIO_SYMBOL_DECL (define_postfix_operator);
extern IDIO_SYMBOL_DECL (dloads);
extern IDIO_SYMBOL_DECL (dot);
extern IDIO_SYMBOL_DECL (dynamic);
extern IDIO_SYMBOL_DECL (dynamic_let);
extern IDIO_SYMBOL_DECL (dynamic_unset);
extern IDIO_SYMBOL_DECL (else);
extern IDIO_SYMBOL_DECL (environ_let);
extern IDIO_SYMBOL_DECL (environ_unset);
extern IDIO_SYMBOL_DECL (eq);
extern IDIO_SYMBOL_DECL (eqp);
extern IDIO_SYMBOL_DECL (eqvp);
extern IDIO_SYMBOL_DECL (equalp);
extern IDIO_SYMBOL_DECL (eq_gt);
extern IDIO_SYMBOL_DECL (error);
extern IDIO_SYMBOL_DECL (escape);
extern IDIO_SYMBOL_DECL (ex_star);
extern IDIO_SYMBOL_DECL (ex_tilde);
extern IDIO_SYMBOL_DECL (fixed_template);
extern IDIO_SYMBOL_DECL (function);
extern IDIO_SYMBOL_DECL (gt);
extern IDIO_SYMBOL_DECL (if);
extern IDIO_SYMBOL_DECL (include);
extern IDIO_SYMBOL_DECL (init);
extern IDIO_SYMBOL_DECL (lambda);
extern IDIO_SYMBOL_DECL (let);
extern IDIO_SYMBOL_DECL (letrec);
extern IDIO_SYMBOL_DECL (list);
extern IDIO_SYMBOL_DECL (lt);
extern IDIO_SYMBOL_DECL (monitor);
extern IDIO_SYMBOL_DECL (namespace);
extern IDIO_SYMBOL_DECL (op);
extern IDIO_SYMBOL_DECL (or);
extern IDIO_SYMBOL_DECL (pair);
extern IDIO_SYMBOL_DECL (pair_separator);
extern IDIO_SYMBOL_DECL (ph);
extern IDIO_SYMBOL_DECL (pipe);
extern IDIO_SYMBOL_DECL (pt);
extern IDIO_SYMBOL_DECL (profile);
extern IDIO_SYMBOL_DECL (quasiquote);
extern IDIO_SYMBOL_DECL (quote);
extern IDIO_SYMBOL_DECL (root);
extern IDIO_SYMBOL_DECL (set);
extern IDIO_SYMBOL_DECL (setter);
extern IDIO_SYMBOL_DECL (super);
extern IDIO_SYMBOL_DECL (template);
extern IDIO_SYMBOL_DECL (this);
extern IDIO_SYMBOL_DECL (unquote);
extern IDIO_SYMBOL_DECL (unquotesplicing);
extern IDIO_SYMBOL_DECL (unset);

IDIO idio_symbol_C (const char *s_C);
IDIO idio_tag_C (const char *s_C);
void idio_free_symbol (IDIO s);
int idio_isa_symbol (IDIO s);
IDIO idio_symbols_C_intern (char *s);
IDIO idio_symbols_string_intern (IDIO str);

IDIO idio_gensym (char *pref_prefix);

void idio_init_symbol (void);
void idio_symbol_add_primitives (void);
void idio_final_symbol (void);

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
