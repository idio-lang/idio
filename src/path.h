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

char **idio_path_env_split (const char *path_env);
char *idio_find_file (const char *file);
IDIO idio_path_expand (IDIO p);

void idio_init_path ();
void idio_path_add_primitives ();
void idio_final_path ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
