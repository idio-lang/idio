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

#include <assert.h>
#include <ffi.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "evaluate.h"
#include "idio.h"
#include "module.h"
#include "symbol.h"
#include "vm.h"

#include "json5-module.h"
#include "json5-unicode.h"
#include "json5-token.h"
#include "json5-parser.h"

IDIO idio_json5_module;

#ifdef IDIO_MALLOC
#define JSON5_VASPRINTF idio_malloc_vasprintf
#else
#define JSON5_VASPRINTF vasprintf
#endif

void json5_error_alloc (char *m)
{
    assert (m);

    /*
     * This wants to be a lean'n'mean error "handler" as we've
     * (probably) run out of memory.  The chances are {m} is a static
     * string and has been pushed onto the stack so no allocation
     * there.
     *
     * perror(3) ought to be able to work in this situation and in the
     * end we abort(3).
     */
    perror (m);
    abort ();
}

char *json5_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == JSON5_VASPRINTF (&s, format, argp)) {
	json5_error_alloc ("asprintf");
    }

    return s;
}

void json5_error_printf (char *format, ...)
{
    assert (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    char *msg = json5_error_string (format, fmt_args);
    va_end (fmt_args);

    fprintf (stderr, "ERROR: %s\n", msg);
    free (msg);
    exit (1);
}

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
