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
 * env.h
 *
 */

#ifndef ENV_H
#define ENV_H

extern char *idio_env_PATH_default;
extern char *idio_env_IDIOLIB_default;
extern IDIO idio_S_PATH;
extern IDIO idio_S_IDIOLIB;

void idio_env_format_error (char const *circumstance, char const *msg, IDIO name, IDIO val, IDIO c_location);

void idio_env_init_idiolib (char const *argv0, size_t argv0_len);

void idio_init_env ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
