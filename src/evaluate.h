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

void idio_add_description (IDIO sym, IDIO desc);
IDIO idio_get_description (IDIO sym);
IDIO idio_add_primitive (idio_primitive_t *d);
IDIO idio_add_special_primitive (idio_primitive_t *d);

IDIO idio_predef_ref (idio_ai_t i);
IDIO idio_toplevel_ref (idio_ai_t i);
void idio_toplevel_update (idio_ai_t i, IDIO v);

void idio_install_expander (IDIO id, IDIO proc);
void idio_install_operator (IDIO id, IDIO proc);

IDIO idio_meaning_operators (IDIO e, int depth);
IDIO idio_evaluate (IDIO e);
void idio_init_evaluate ();
void idio_evaluate_add_primitives ();
void idio_final_evaluate ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
