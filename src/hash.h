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
 * hash.h
 * 
 */

#ifndef HASH_H
#define HASH_H

#define IDIO_HASH(s,e,h)	(idio_hash ((s), (e), (h)))
#define IDIO_HASH_EQP(s)	(IDIO_HASH ((s), idio_eqp, idio_hash_hashval))
#define IDIO_HASH_EQVP(s)	(IDIO_HASH ((s), idio_eqvp, idio_hash_hashval))
#define IDIO_HASH_EQUALP(s)	(IDIO_HASH ((s), idio_equalp, idio_hash_hashval))

IDIO idio_hash (size_t size, int (*equal) (void *k1, void *k2), size_t (*hashf) (IDIO h, void *k));
int idio_isa_hash (IDIO h);
void idio_free_hash (IDIO h);
size_t idio_hash_hcount (IDIO h);
void idio_hash_resize (IDIO h);
size_t idio_hash_hashval_uintmax_t (uintmax_t i);
size_t idio_hash_hashval_string_C (size_t blen, const char *s_C);
size_t idio_hash_hashval_string (IDIO s);
size_t idio_hash_hashval_symbol (IDIO s);
size_t idio_hash_hashval_void (void *p);
size_t idio_hash_hashval_pair (IDIO h);
size_t idio_hash_hashval_array (IDIO h);
size_t idio_hash_hashval_hash (IDIO h);
size_t idio_hash_hashval (IDIO h, void *k);
void idio_hash_verify_chain (IDIO h, void *k);
size_t idio_hash_find_free_slot (IDIO h);
IDIO idio_hash_put (IDIO h, void *k, IDIO v);
size_t idio_hash_hv_follow_chain (IDIO h, void *k);
IDIO idio_hash_exists (IDIO h, void *k);
IDIO idio_hash_get (IDIO h, void *k);
int idio_hash_delete (IDIO h, void *k);
IDIO idio_hash_keys_to_list (IDIO h);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
