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

IDIO idio_primitive_C (IDIO (*func) (IDIO args), const char *name_C, size_t arity, char varargs);
IDIO idio_primitive_C_desc (idio_primitive_C_t *desc);
void idio_free_primitive_C (IDIO o);


#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
