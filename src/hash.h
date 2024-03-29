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
 * hash.h
 *
 */

#ifndef HASH_H
#define HASH_H

#define IDIO_HASH(s,e,hf,c,h)	(idio_hash ((s), (e), (hf), (c), (h)))
#define IDIO_HASH_EQP(s)	(IDIO_HASH ((s), idio_eqp, idio_hash_default_hash_C, idio_S_nil, idio_S_nil))
#define IDIO_HASH_EQVP(s)	(IDIO_HASH ((s), idio_eqvp, idio_hash_default_hash_C, idio_S_nil, idio_S_nil))
#define IDIO_HASH_EQUALP(s)	(IDIO_HASH ((s), idio_equalp, idio_hash_default_hash_C, idio_S_nil, idio_S_nil))

IDIO idio_hash (idio_hi_t const size, int (*equal) (void const *k1, void const *k2), idio_hi_t (*hash_C) (IDIO h, void const *k), IDIO comp, IDIO hash);
IDIO idio_copy_hash (IDIO orig, int depth);
IDIO idio_hash_alist_to_hash (IDIO alist, IDIO args);
int idio_isa_hash (IDIO h);
void idio_free_hash (IDIO h);
void idio_hash_resize (IDIO h, int larger);
idio_hi_t idio_hash_default_hash_C_uintmax_t (uintmax_t i);
idio_hi_t idio_hash_default_hash_C_string_C_MurmurOAAT_32 (char const *s_C);
idio_hi_t idio_hash_default_hash_C_string_C (size_t blen, char const *s_C);
idio_hi_t idio_hash_default_hash_C_string (IDIO s);
idio_hi_t idio_hash_default_hash_C_symbol (IDIO s);
idio_hi_t idio_hash_default_hash_C_void (void *p);
idio_hi_t idio_hash_default_hash_C_pair (IDIO h);
idio_hi_t idio_hash_default_hash_C_array (IDIO h);
idio_hi_t idio_hash_default_hash_C_hash (IDIO h);
idio_hi_t idio_hash_default_hash_C (IDIO h, void const *k);
IDIO idio_hash_put (IDIO h, void *k, IDIO v);
int idio_hash_exists_key (IDIO h, void *kv);
IDIO idio_hash_ref (IDIO h, void const *k);
int idio_hash_delete (IDIO h, void *k);
IDIO idio_hash_keys_to_list (IDIO h);
IDIO idio_hash_make_hash (IDIO args);
IDIO idio_hash_reference (IDIO ht, IDIO key, IDIO args);
IDIO idio_hash_set (IDIO ht, IDIO key, IDIO v);

char *idio_hash_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
char *idio_hash_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);

void idio_init_hash ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
