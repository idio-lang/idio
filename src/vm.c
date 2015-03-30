/*
 * Copyright (c) 2015 Ian Fitchet <idf(at)idio-lang.org>
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
 * vm.c
 * 
 */

#include "idio.h"

void idio_vm_codegen (IDIO code)
{
    IDIO_ASSERT (code);
    IDIO_TYPE_ASSERT (pair, code);

    IDIO_C_ASSERT (0);
}

IDIO idio_current_input_handle ()
{
    return idio_module_current_symbol_value (idio_symbols_C_intern ("*current-input-handle*"));
}

void idio_set_current_input_handle (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    idio_module_set_current_symbol_value (idio_symbols_C_intern ("*current-input-handle*"), h);
}

IDIO idio_current_output_handle ()
{
    return idio_module_current_symbol_value (idio_symbols_C_intern ("*current-output-handle*"));
}

void idio_set_current_output_handle (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    idio_module_set_current_symbol_value (idio_symbols_C_intern ("*current-output-handle*"), h);
}

