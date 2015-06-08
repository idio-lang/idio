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
 * scm-evaluate.h
 * 
 */

#ifndef SCM_EVALUATE_H
#define SCM_EVALUATE_H

void idio_scm_add_description (IDIO sym, IDIO desc);
IDIO idio_scm_get_description (IDIO sym);
IDIO idio_scm_add_primitive (idio_primitive_t *d);
IDIO idio_scm_add_special_primitive (idio_primitive_t *d);

IDIO idio_scm_predef_ref (idio_ai_t i);
IDIO idio_scm_toplevel_ref (idio_ai_t i);
void idio_scm_toplevel_update (idio_ai_t i, IDIO v);

void idio_install_expander (IDIO id, IDIO proc);

IDIO idio_scm_evaluate (IDIO e);
void idio_init_scm_evaluate ();
void idio_scm_evaluate_add_primitives ();
void idio_final_scm_evaluate ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
