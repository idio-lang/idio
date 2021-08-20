/*
 * Copyright (c) 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * utf8.h
 */

#ifndef UTF8_H
#define UTF8_H

/*
 * The following values are x12 to avoid unnecessary shifts
 */
#define JSON5_UTF8_ACCEPT 0
#define JSON5_UTF8_REJECT 12
/*
 * other > 0 values mean more bytes are required
 */

json5_unicode_string_t *json5_utf8_string_C_len (char *s_C, const size_t slen);

#endif
