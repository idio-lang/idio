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
 * json5-module.h
 *
 */

#ifndef JSON5_MODULE_H
#define JSON5_MODULE_H

extern IDIO idio_json5_module;

extern IDIO idio_json5_literal_value_Infinity_sym;
extern IDIO idio_json5_literal_value_pos_Infinity_sym;
extern IDIO idio_json5_literal_value_neg_Infinity_sym;
extern IDIO idio_json5_literal_value_NaN_sym;
extern IDIO idio_json5_literal_value_pos_NaN_sym;
extern IDIO idio_json5_literal_value_neg_NaN_sym;

extern IDIO idio_condition_rt_json5_error_type;
extern IDIO idio_condition_rt_json5_value_error_type;

void idio_init_json5 ();

#endif
