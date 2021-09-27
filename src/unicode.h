/*
 * Copyright (c) 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

#define IDIO_UNICODE_PLANE_COUNT	17
#define IDIO_UNICODE_PLANE_SIZE		0x10000
#define IDIO_UNICODE_PLANE_MASK		0xFFFF
#define IDIO_UNICODE_SIZE		IDIO_UNICODE_PLANE_COUNT * IDIO_UNICODE_PLANE_SIZE

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

#define IDIO_UTF8_STRING_NOPREC		0
#define IDIO_UTF8_STRING_USEPREC	1

extern IDIO idio_unicode_module;

int idio_unicode_valid_code_point (idio_unicode_t cp);
int idio_unicode_character_code_point (idio_unicode_t cp);
int idio_isa_unicode (IDIO o);
idio_unicode_t idio_utf8_decode (idio_unicode_t* state, idio_unicode_t* codep, idio_unicode_t byte);
void idio_utf8_code_point (idio_unicode_t c, char *buf, int *sizep);
char *idio_utf8_string (IDIO str, size_t *sizep, int escapes, int quoted, int use_prec);
IDIO idio_unicode_lookup (char const *name);

void idio_init_unicode ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
