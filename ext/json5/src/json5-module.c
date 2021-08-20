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
 * json5-module.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <ffi.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "gc.h"
#include "evaluate.h"
#include "idio.h"
#include "module.h"
#include "symbol.h"
#include "vm.h"

#include "json5-module.h"
#include "json5-parser.h"

IDIO idio_json5_module;

IDIO_DEFINE_PRIMITIVE0_DS ("hello", json5_hello, (), "", "\
say hello				\n\
")
{
    fprintf (stdout, "hello\n");

    return idio_S_unspec;
}

void idio_json5_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_json5_module, json5_hello);
}

void idio_final_json5 ()
{
}

void idio_init_json5 ()
{
    idio_json5_module = idio_module (idio_symbols_C_intern ("json5"));

    idio_module_table_register (idio_json5_add_primitives, idio_final_json5);
}
