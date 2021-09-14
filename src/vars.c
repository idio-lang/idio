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
 * vars.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ffi.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "idio-string.h"
#include "idio-system.h"
#include "module.h"
#include "pair.h"
#include "symbol.h"
#include "vm.h"

IDIO idio_vars_IFS_sym;
#define IDIO_VARS_IFS_DEFAULT	" \t\n"

IDIO idio_vars_suppress_exit_on_error_sym;
IDIO idio_vars_suppress_pipefail_sym;
IDIO idio_vars_suppress_async_command_report_sym;

static int idio_vars_set_dynamic_default (IDIO name, IDIO val)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (val);

    IDIO_TYPE_ASSERT (symbol, name);

    IDIO VARS = idio_module_current_symbol_value_recurse (name, IDIO_LIST1 (idio_S_false));
    if (idio_S_false == VARS) {
	idio_dynamic_extend (name, name, val, idio_vm_constants);
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE0_DS ("SECONDS/get", SECONDS_get, (void), "", "\
Return the VM's elapsed running time in seconds	\n\
						\n\
Normally accessed as the variable SECONDS	\n\
						\n\
:return: elapsed VM running time		\n\
:rtype: integer					\n\
")
{
    return idio_integer (idio_vm_elapsed ());
}

void idio_vars_add_primitives ()
{
    IDIO geti;
    geti = IDIO_ADD_PRIMITIVE (SECONDS_get);
    idio_module_add_computed_symbol (IDIO_SYMBOLS_C_INTERN ("SECONDS"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_Idio_module);

    idio_vars_set_dynamic_default (idio_vars_IFS_sym, idio_string_C_len (IDIO_STATIC_STR_LEN (IDIO_VARS_IFS_DEFAULT)));
    idio_vars_set_dynamic_default (idio_vars_suppress_exit_on_error_sym, idio_S_false);
    idio_vars_set_dynamic_default (idio_vars_suppress_pipefail_sym, idio_S_false);
    idio_vars_set_dynamic_default (idio_vars_suppress_async_command_report_sym, idio_S_false);
}

void idio_final_vars ()
{
}

void idio_init_vars ()
{
    idio_module_table_register (idio_vars_add_primitives, idio_final_vars, NULL);

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_VERSION"),
				  idio_string_C_len (IDIO_SYSTEM_VERSION, sizeof (IDIO_SYSTEM_VERSION) - 1),
				  idio_Idio_module);

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_MM_VERSION"),
				  idio_string_C_len (IDIO_SYSTEM_VERSION_MM, sizeof (IDIO_SYSTEM_VERSION_MM) - 1),
				  idio_Idio_module);

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_BUILD_DATE"),
				  idio_string_C_len (IDIO_SYSTEM_BUILD_DATE, sizeof (IDIO_SYSTEM_BUILD_DATE) - 1),
				  idio_Idio_module);

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_BUILD_TIMESTAMP"),
				  idio_string_C_len (IDIO_SYSTEM_BUILD_TIMESTAMP, sizeof (IDIO_SYSTEM_BUILD_TIMESTAMP) - 1),
				  idio_Idio_module);

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_SYSTEM_ARCH"),
				  idio_string_C_len (IDIO_SYSTEM_ARCH, sizeof (IDIO_SYSTEM_ARCH) - 1),
				  idio_Idio_module);

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_BUILD_COMMIT"),
				  idio_string_C_len (IDIO_BUILD_COMMIT, sizeof (IDIO_BUILD_COMMIT) - 1),
				  idio_Idio_module);

    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_BUILD_DESCRIBE"),
				  idio_string_C_len (IDIO_BUILD_DESCRIBE, sizeof (IDIO_BUILD_DESCRIBE) - 1),
				  idio_Idio_module);

    idio_vars_IFS_sym                           = IDIO_SYMBOLS_C_INTERN ("IFS");
    idio_vars_suppress_exit_on_error_sym        = IDIO_SYMBOLS_C_INTERN ("suppress-exit-on-error!");
    idio_vars_suppress_pipefail_sym             = IDIO_SYMBOLS_C_INTERN ("suppress-pipefail!");
    idio_vars_suppress_async_command_report_sym = IDIO_SYMBOLS_C_INTERN ("suppress-async-command-report!");
}

