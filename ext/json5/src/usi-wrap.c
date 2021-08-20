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
 * usi-wrap.c
 *
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "json5-unicode.h"
#include "usi.h"
#include "usi-wrap.h"

int idio_usi_codepoint_is_category (json5_unicode_t cp, int cat)
{
    int r = 0;

    const idio_USI_t *var = idio_USI_codepoint (cp);
    if (cat == var->category) {
	r = 1;
    }

    return r;
}

int idio_usi_codepoint_has_attribute (json5_unicode_t cp, int flag)
{
    int r = 0;

    const idio_USI_t *var = idio_USI_codepoint (cp);
    if (var->flags & flag) {
	r = 1;
    }

    return r;
}

