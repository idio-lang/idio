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
 * expander.h
 * 
 */

#ifndef EXPANDER_H
#define EXPANDER_H

IDIO idio_evaluate_expander_source (IDIO x, IDIO e);
void idio_install_expander_source (IDIO id, IDIO proc, IDIO code);
IDIO idio_evaluate_expander_code (IDIO m);
IDIO idio_expanderp (IDIO name);
IDIO idio_macro_expand (IDIO e);
IDIO idio_macro_expands (IDIO e);
IDIO idio_evaluate_infix_operator_code (IDIO m);
IDIO idio_evaluate_postfix_operator_code (IDIO m);
void idio_add_infix_operator_primitive (idio_primitive_desc_t *d, int pri);
void idio_add_postfix_operator_primitive (idio_primitive_desc_t *d, int pri);

void idio_init_expander ();
void idio_expander_add_primitives ();
void idio_final_expander ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
