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
 * json5-parser.h
 *
 * The result of wandering through https://spec.json5.org
 *
 * From a portability perspective, this code is limited to C's int64_t
 * integers and double floating point numbers.
 */

#ifndef JSON5_PARSER_H
#define JSON5_PARSER_H

json5_value_t *json5_parse_fd (int fd);

#endif
