/*
 * Copyright (c) 2021, 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * empty-module.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"

#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "handle.h"
#include "fixnum.h"
#include "idio.h"
#include "idio-string.h"
#include "empty-system.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "vm.h"

IDIO idio_empty_module;

IDIO_DEFINE_PRIMITIVE1_DS ("hello", empty_hello, (IDIO name), "name", "\
Say hello to ``name``		\n\
				\n\
:param name: name		\n\
:type name: string		\n\
:return: welcome message	\n\
:rtype: string			\n\
")
{
    IDIO_ASSERT (name);

    IDIO_USER_TYPE_ASSERT (string, name);

    size_t size = 0;
    char *name_C = idio_string_as_C (name, &size);

    char const *ss[] = { "Hello, ", name_C, "." };

    size_t lens[] = { 7, size, 1 };

    IDIO r = idio_string_C_array_lens (3, ss, lens);

    IDIO_GC_FREE (name_C, size);

    return r;
}

void idio_empty_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_empty_module, empty_hello);
}

void idio_final_empty ()
{
}

void idio_init_empty (void *handle)
{
    idio_empty_module = idio_module (IDIO_SYMBOL ("empty"));

    idio_module_table_register (idio_empty_add_primitives, idio_final_empty, handle);

    idio_module_export_symbol_value (IDIO_SYMBOL ("version"),
				     idio_string_C_len (EMPTY_SYSTEM_VERSION, sizeof (EMPTY_SYSTEM_VERSION) - 1),
				     idio_empty_module);
}
