/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * rfc6234.h
 *
 */

#ifndef RFC6234_H
#define RFC6234_H

#include "RFC6234/sha.h"

extern IDIO idio_rfc6234_SHA1_sym;
extern IDIO idio_rfc6234_SHA224_sym;
extern IDIO idio_rfc6234_SHA256_sym;
extern IDIO idio_rfc6234_SHA384_sym;
extern IDIO idio_rfc6234_SHA512_sym;

char *idio_rfc6234_shasum_fd_C (char const *func, int fd, IDIO alg, int *hashsize);
IDIO idio_rfc6234_shasum_fd (char const *func, int fd, IDIO alg);
IDIO idio_rfc6234_shasum_file (char const *func, IDIO file, IDIO alg);
void idio_init_rfc6234 ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
