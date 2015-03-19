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
 * pair.h
 * 
 */

#ifndef PAIR_H
#define PAIR_H

IDIO idio_pair (IDIO h, IDIO t);
int idio_isa_pair (IDIO p);
int idio_isa_list (IDIO p);
void idio_free_pair (IDIO p);
IDIO idio_pair_head (IDIO p);
IDIO idio_pair_tail (IDIO p);

IDIO idio_list_reverse (IDIO l);
IDIO idio_list_to_array (IDIO l);
size_t idio_list_length (IDIO l);
IDIO idio_list_copy (IDIO l);
IDIO idio_list_append (IDIO l1, IDIO l2);

#define IDIO_LIST1(e1)		idio_pair (e1, idio_S_nil)
#define IDIO_LIST2(e1,e2)	idio_pair (e1, idio_pair (e2, idio_S_nil))
#define IDIO_LIST3(e1,e2,e3)	idio_pair (e1, idio_pair (e2, idio_pair (e3, idio_S_nil)))

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
