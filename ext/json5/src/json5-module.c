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
#include "evaluate.h"
#include "handle.h"
#include "fixnum.h"
#include "idio.h"
#include "idio-string.h"
#include "json5-system.h"
#include "malloc.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "vm.h"

#include "json5-api.h"

IDIO idio_json5_module;

IDIO idio_json5_literal_value_Infinity_sym = idio_S_nil;
IDIO idio_json5_literal_value_pos_Infinity_sym = idio_S_nil;
IDIO idio_json5_literal_value_neg_Infinity_sym = idio_S_nil;
IDIO idio_json5_literal_value_NaN_sym = idio_S_nil;
IDIO idio_json5_literal_value_pos_NaN_sym = idio_S_nil;
IDIO idio_json5_literal_value_neg_NaN_sym = idio_S_nil;

IDIO idio_condition_rt_json5_error_type;
IDIO idio_condition_rt_json5_value_error_type;

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

IDIO json5_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == idio_vasprintf (&s, format, argp)) {
	json5_error_alloc ("asprintf");
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (s, sh);

    IDIO_GC_FREE (s);
    /*
     * idio_vasprintf will not have called idio_gc_alloc to no stats
     * decrement
     */

    return idio_get_output_string (sh);
}

void json5_error_printf (char *format, ...)
{
    assert (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg = json5_error_string (format, fmt_args);
    va_end (fmt_args);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_json5_error_type,
				   IDIO_LIST3 (msg,
					       location,
					       idio_S_nil));

    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

void idio_json5_add_primitives ()
{
    idio_json5_api_add_primitives ();
}

void idio_final_json5 ()
{
}

void idio_init_json5 (void *handle)
{
    idio_json5_module = idio_module (IDIO_SYMBOLS_C_INTERN ("json5"));

    idio_module_table_register (idio_json5_add_primitives, idio_final_json5, handle);

    idio_json5_literal_value_Infinity_sym = IDIO_SYMBOLS_C_INTERN ("Infinity");
    idio_json5_literal_value_pos_Infinity_sym = IDIO_SYMBOLS_C_INTERN ("+Infinity");
    idio_json5_literal_value_neg_Infinity_sym = IDIO_SYMBOLS_C_INTERN ("-Infinity");
    idio_json5_literal_value_NaN_sym = IDIO_SYMBOLS_C_INTERN ("NaN");
    idio_json5_literal_value_pos_NaN_sym = IDIO_SYMBOLS_C_INTERN ("+NaN");
    idio_json5_literal_value_neg_NaN_sym = IDIO_SYMBOLS_C_INTERN ("-NaN");

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_json5_error_type, "^rt-json5-error", idio_condition_runtime_error_type);

    IDIO_DEFINE_CONDITION1 (idio_condition_rt_json5_value_error_type, "^rt-json5-value-error", idio_condition_rt_json5_error_type, "value");

    idio_module_export_symbol_value (IDIO_SYMBOLS_C_INTERN ("version"),
				     idio_string_C_len (JSON5_SYSTEM_VERSION, sizeof (JSON5_SYSTEM_VERSION) - 1),
				     idio_json5_module);

}
