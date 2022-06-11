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
 * pair.h
 *
 */

#ifndef PAIR_H
#define PAIR_H

IDIO idio_pair (IDIO h, IDIO t);
int idio_isa_pair (IDIO p);
int idio_isa_list (IDIO p);
void idio_free_pair (IDIO p);
IDIO idio_list_head (IDIO p);
IDIO idio_list_tail (IDIO p);

IDIO idio_listv (size_t nargs, ...);
IDIO idio_improper_list_reverse (IDIO l, IDIO last);
IDIO idio_list_reverse (IDIO l);
IDIO idio_list_nreverse (IDIO l);
IDIO idio_list_to_array (IDIO l);
size_t idio_list_length (IDIO l);
IDIO idio_copy_pair (IDIO p, int depth);
IDIO idio_list_append2 (IDIO l1, IDIO l2);
IDIO idio_list_ph_of (IDIO l);
IDIO idio_list_pt_of (IDIO l);
IDIO idio_list_memq (IDIO k, IDIO l);
IDIO idio_list_memv (IDIO k, IDIO l);
IDIO idio_list_member (IDIO k, IDIO l);
IDIO idio_list_assq (IDIO k, IDIO l);
IDIO idio_list_assv (IDIO k, IDIO l);
IDIO idio_list_assoc (IDIO k, IDIO l);
IDIO idio_list_nth (IDIO l, IDIO I_n, IDIO args);

char *idio_pair_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
char *idio_pair_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);

void idio_init_pair ();

#define IDIO_LIST1(...)		idio_listv (1, __VA_ARGS__)
#define IDIO_LIST2(...)		idio_listv (2, __VA_ARGS__)
#define IDIO_LIST3(...)		idio_listv (3, __VA_ARGS__)
#define IDIO_LIST4(...)		idio_listv (4, __VA_ARGS__)
#define IDIO_LIST5(...)		idio_listv (5, __VA_ARGS__)
#define IDIO_LIST6(...)		idio_listv (6, __VA_ARGS__)
#define IDIO_LIST7(...)		idio_listv (7, __VA_ARGS__)

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
