/*
 * Copyright (c) 2017 Ian Fitchet <idf(at)idio-lang.org>
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

typedef uint32_t idio_utf8_t;

#define IDIO_UTF8_ACCEPT 0
#define IDIO_UTF8_REJECT 12

idio_utf8_t idio_utf8_decode (idio_utf8_t* state, idio_utf8_t* codep, idio_utf8_t byte);

void idio_init_unicode ();
void idio_unicode_add_primitives ();
void idio_final_unicode ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
