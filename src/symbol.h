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

extern IDIO idio_S_ampersand;
extern IDIO idio_S_and;
extern IDIO idio_S_append;
extern IDIO idio_S_apply;
extern IDIO idio_S_asterisk;
extern IDIO idio_S_begin;
extern IDIO idio_S_block;
extern IDIO idio_S_car;
extern IDIO idio_S_cdr;
extern IDIO idio_S_c_struct;
extern IDIO idio_S_class;
extern IDIO idio_S_cond;
extern IDIO idio_S_cons;
extern IDIO idio_S_define;
extern IDIO idio_S_define_macro;
extern IDIO idio_S_dloads;
extern IDIO idio_S_dynamic;
extern IDIO idio_S_dynamic_let;
extern IDIO idio_S_else;
extern IDIO idio_S_eq_gt;
extern IDIO idio_S_error;
extern IDIO idio_S_fixed_template;
extern IDIO idio_S_if;
extern IDIO idio_S_init;
extern IDIO idio_S_lambda;
extern IDIO idio_S_let;
extern IDIO idio_S_letrec;
extern IDIO idio_S_list;
extern IDIO idio_S_monitor;
extern IDIO idio_S_namespace;
extern IDIO idio_S_or;
extern IDIO idio_S_profile;
extern IDIO idio_S_quasiquote;
extern IDIO idio_S_quote;
extern IDIO idio_S_root;
extern IDIO idio_S_set;
extern IDIO idio_S_super;
extern IDIO idio_S_template;
extern IDIO idio_S_this;
extern IDIO idio_S_unquote;
extern IDIO idio_S_unquotesplicing;

IDIO idio_symbol_C (const char *s_C);
IDIO idio_tag_C (const char *s_C);
void idio_free_symbol (IDIO s);
int idio_isa_symbol (IDIO s);
IDIO idio_symbols_C_intern (char *s);
IDIO idio_symbols_string_intern (IDIO str);

IDIO idio_gensym ();

void idio_init_symbol (void);
void idio_symbol_add_primitives (void);
void idio_final_symbol (void);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
