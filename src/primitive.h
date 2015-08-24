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
 * primitive.h
 * 
 */

#ifndef PRIMITIVE_H
#define PRIMITIVE_H

IDIO idio_primitive (IDIO (*func) (IDIO args), const char *name_C, size_t arity, char varargs);
int idio_isa_primitive (IDIO o);
IDIO idio_primitive_data (idio_primitive_desc_t *desc);
void idio_free_primitive (IDIO o);

void idio_init_primitive ();
void idio_primitive_add_primitives ();
void idio_final_primitive ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
