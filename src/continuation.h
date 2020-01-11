/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * continuation.h
 *
 */

#ifndef CONTINUATION_H
#define CONTINUATION_H

IDIO idio_continuation (IDIO thr);
int idio_isa_continuation (IDIO o);
void idio_free_continuation (IDIO k);

void idio_init_continuation ();
void idio_continuation_add_primitives ();
void idio_final_continuation ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
