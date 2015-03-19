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
 * util.h
 * 
 */

#ifndef UTIL_H
#define UTIL_H

const char *idio_type2string (IDIO o);
const char *idio_type_enum2string (idio_type_e type);
IDIO idio_type (IDIO o);
int idio_eqp (void *o1, void *o2);
int idio_eqvp (void *o1, void *o2);
int idio_equalp (void *o1, void *o2);
int idio_equal (IDIO o1, IDIO o2, int eqp);
IDIO idio_value (IDIO o);
char *idio_as_string (IDIO o, int depth);
char *idio_display_string (IDIO o);
IDIO idio_apply (IDIO func, IDIO args);
IDIO idio_apply1 (IDIO func, IDIO arg);
IDIO idio_apply2 (IDIO func, IDIO arg1, IDIO arg2);

void idio_dump (IDIO o, int detail);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
