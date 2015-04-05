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
 * module.h
 * 
 */

#ifndef MODULE_H
#define MODULE_H

IDIO idio_module (IDIO name);
int idio_isa_module (IDIO o);
void idio_free_module (IDIO fo);
IDIO idio_main_module ();
IDIO idio_current_module ();
IDIO idio_module_symbols (IDIO module);
IDIO idio_module_current_symbols ();
IDIO idio_module_primitive_symbols ();
IDIO idio_module_defined (IDIO module);
IDIO idio_module_current_defined ();
void idio_module_current_extend_defined (IDIO name);
IDIO idio_module_primitive_defined ();
IDIO idio_symbol_lookup (IDIO symbol, IDIO module);
IDIO idio_module_symbol_value (IDIO symbol, IDIO module);
IDIO idio_module_primitive_symbol_value (IDIO symbol);
IDIO idio_module_current_symbol_value (IDIO symbol);
IDIO idio_module_set_symbol_value (IDIO symbol, IDIO value, IDIO module);
IDIO idio_module_primitive_set_symbol_value (IDIO symbol, IDIO value);
IDIO idio_module_current_set_symbol_value (IDIO symbol, IDIO value);

void idio_init_module (void);
void idio_module_add_primitives ();
void idio_final_module (void);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
