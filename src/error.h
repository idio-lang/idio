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
 * error.h
 * 
 */

#ifndef ERROR_H
#define ERROR_H

extern IDIO idio_io_read_exception;

void idio_raise_exception (IDIO f, IDIO e);
IDIO idio_make_exception (IDIO f, IDIO e);

void idio_error_add_C (IDIO f, const char *s);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
