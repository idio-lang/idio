/*
 * Copyright (c) 2020-2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * bitset.h
 *
 */

#ifndef BITSET_H
#define BITSET_H

#define IDIO_BITSET_BITS_PER_WORD (CHAR_BIT * sizeof (idio_bitset_word_t))

IDIO idio_bitset (size_t size);
int idio_isa_bitset (IDIO o);
void idio_free_bitset (IDIO bs);

IDIO idio_bitset_set (IDIO bs, size_t bit);
IDIO idio_bitset_clear (IDIO bs, size_t bit);
IDIO idio_bitset_ref (IDIO bs, size_t bit);
IDIO idio_copy_bitset (IDIO obs);
IDIO idio_not_bitset (IDIO bs);
int idio_equal_bitsetp (IDIO args);

char *idio_bitset_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
char *idio_bitset_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);

void idio_init_bitset ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
