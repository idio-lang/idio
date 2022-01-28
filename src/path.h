/*
 * Copyright (c) 2015-2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * path.h
 *
 */

#ifndef PATH_H
#define PATH_H

/*
 * Indexes into structures for direct references
 */
#define IDIO_PATH_PATTERN		0

extern IDIO idio_path_type;

IDIO idio_pathname_C_len (char const *s_C, size_t blen);
IDIO idio_pathname_C (char const *s_C);
int idio_isa_pathname (IDIO o);
IDIO idio_fd_pathname_C_len (char const *s_C, size_t blen);
IDIO idio_fd_pathname_C (char const *s_C);
int idio_isa_fd_pathname (IDIO o);
IDIO idio_fifo_pathname_C_len (char const *s_C, size_t blen);
IDIO idio_fifo_pathname_C (char const *s_C);
int idio_isa_fifo_pathname (IDIO o);
IDIO idio_path_expand (IDIO p);

void idio_path_add_primitives ();
void idio_init_path ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
