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
 * evaluate.h
 * 
 */

#ifndef EVALUATE_H
#define EVALUATE_H

/*
 * Some flags for scope hints for non-local variables
 */
#define IDIO_UNKNOWN_SCOPE	0
#define IDIO_LEXICAL_SCOPE	1 /* regular lexical globals */
#define IDIO_DYNAMIC_SCOPE	2 /* dynamic globals */
#define IDIO_ENVIRON_SCOPE	3 /* dynamics tagged as environment
				     variables */
#define IDIO_COMPUTED_SCOPE	4 /* computed variables */

void idio_add_description (IDIO sym, IDIO desc);
IDIO idio_get_description (IDIO sym);
IDIO idio_add_primitive (idio_primitive_t *d);
IDIO idio_add_special_primitive (idio_primitive_t *d);
void idio_add_expander_primitive (idio_primitive_t *d);

IDIO idio_toplevel_extend (IDIO name, int variant);

void idio_install_expander (IDIO id, IDIO proc);

void idio_install_infix_operator (IDIO id, IDIO proc, int pri);
IDIO idio_infix_operatorp (IDIO name);
IDIO idio_infix_operator_expand (IDIO e, int depth);

void idio_install_postfix_operator (IDIO id, IDIO proc, int pri);
IDIO idio_postfix_operatorp (IDIO name);
IDIO idio_postfix_operator_expand (IDIO e, int depth);

IDIO idio_operator_expand (IDIO e, int depth);

IDIO idio_evaluate (IDIO e);
void idio_init_evaluate ();
void idio_evaluate_add_primitives ();
void idio_final_evaluate ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
