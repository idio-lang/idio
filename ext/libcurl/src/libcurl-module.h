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
 * curl-module.h
 *
 */

#ifndef CURL_MODULE_H
#define CURL_MODULE_H

extern IDIO idio_curl_module;

extern IDIO idio_condition_rt_libcurl_error_type;

void idio_libcurl_CURL_finalizer (IDIO C_p);

void idio_init_libcurl ();

#endif
