/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * closure.h
 *
 */

#ifndef CLOSURE_H
#define CLOSURE_H

IDIO idio_toplevel_closure (size_t code_pc, size_t code_len, IDIO frame, IDIO env, IDIO name, IDIO sigstr, IDIO docstr, IDIO srcloc);
IDIO idio_closure (IDIO cl, IDIO frame);
int idio_isa_closure (IDIO o);
int idio_isa_function (IDIO o);
void idio_free_closure (IDIO c);

char *idio_closure_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
char *idio_closure_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);

void idio_init_closure ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
