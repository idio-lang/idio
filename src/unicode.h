/*
 * Copyright (c) 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * unicode.h
 *
 */

#ifndef UNICODE_H
#define UNICODE_H

typedef uint32_t idio_unicode_t;

/*
 * The following values are x12 to avoid unnecessary shifts
 */
#define IDIO_UTF8_ACCEPT 0
#define IDIO_UTF8_REJECT 12
/*
 * other > 0 values mean more bytes are required
 */

#define IDIO_UTF8_STRING_VERBATIM	0
#define IDIO_UTF8_STRING_ESCAPES	1

#define IDIO_UTF8_STRING_UNQUOTED	0
#define IDIO_UTF8_STRING_QUOTED		1

int idio_isa_unicode (IDIO o);
idio_unicode_t idio_utf8_decode (idio_unicode_t* state, idio_unicode_t* codep, idio_unicode_t byte);
char *idio_utf8_string (IDIO str, size_t *sizep, int escapes, int quoted);
IDIO idio_unicode_lookup (char *name);

void idio_init_unicode ();
void idio_unicode_add_primitives ();
void idio_final_unicode ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
