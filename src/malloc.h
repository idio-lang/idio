/*
 * Copyright (c) 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * malloc.h
 *
 */

#ifndef MALLOC_H
#define MALLOC_H

void *idio_malloc_malloc (size_t nbytes);
void *idio_malloc_calloc (size_t num, size_t size);
void idio_malloc_free(void *cp);
void *idio_malloc_realloc(void *cp, size_t nbytes);
int idio_malloc_vasprintf (char **strp, const char *fmt, va_list ap);
int idio_malloc_asprintf(char **strp, const char *fmt, ...);

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
