/*
 * Copyright (c) 2021-2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * usi-wrap.h
 *
 */

#ifndef USI_WRAP_H
#define USI_WRAP_H

int idio_usi_codepoint_is_category (json5_unicode_t cp, idio_USI_Category_t cat);
int idio_usi_codepoint_has_attribute (json5_unicode_t cp, int flag);

#endif
