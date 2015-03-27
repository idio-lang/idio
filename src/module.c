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
 * module.c
 * 
 */

#include "idio.h"

static IDIO idio_modules_hash = idio_S_nil;

/*
 * idio_primitive_module is the set of built-ins -- not modifiable!
 *
 * idio_toplevel_module is the default toplevel module which imports from
 * idio_primitive_module
 *
 * All new modules default to importing from idio_toplevel_module
 */
static IDIO idio_primitive_module = idio_S_nil;
static IDIO idio_toplevel_module = idio_S_nil;

void idio_error_module_duplicate_name (IDIO name)
{
    idio_error_message ("module: %s already exists", IDIO_SYMBOL_S (name));
}

void idio_error_module_set_imports (IDIO module)
{
    idio_error_message ("module %s: cannot set imports", IDIO_SYMBOL_S (IDIO_MODULE_NAME (module)));
}

void idio_error_module_set_exports (IDIO module)
{
    idio_error_message ("module %s: cannot set exports", IDIO_SYMBOL_S (IDIO_MODULE_NAME (module)));
}

void idio_error_module_unbound (IDIO module)
{
    fprintf (stderr, "all-modules: %s\n", idio_as_string (idio_modules_hash, 3));
    idio_error_message ("module %s unbound in all-modules?", IDIO_SYMBOL_S (IDIO_MODULE_NAME (module)));
}

void idio_error_module_unbound_name (IDIO symbol, IDIO module)
{
    idio_error_message ("symbol %s unbound in module %s", IDIO_SYMBOL_S (symbol), IDIO_SYMBOL_S (IDIO_MODULE_NAME (module)));
}

void idio_init_module ()
{
    idio_modules_hash = IDIO_HASH_EQP (1<<4);
    idio_gc_protect (idio_modules_hash);

    idio_primitive_module = idio_module (idio_symbols_C_intern ("Idio.primitives"));
    IDIO_MODULE_IMPORTS (idio_primitive_module) = idio_S_nil;

    idio_toplevel_module = idio_module (idio_symbols_C_intern ("Idio"));
    IDIO_MODULE_IMPORTS (idio_toplevel_module) = IDIO_LIST1 (idio_primitive_module);
}

void idio_final_module ()
{
    idio_gc_expose (idio_modules_hash);
}

IDIO idio_module (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO m = idio_hash_get (idio_modules_hash, name);

    if (idio_S_unspec != m) {
	idio_error_module_duplicate_name (name);
	return idio_S_unspec;
    }

    IDIO mo = idio_gc_get (IDIO_TYPE_MODULE);

    IDIO_GC_ALLOC (mo->u.module, sizeof (idio_module_t));
    
    IDIO_MODULE_GREY (mo) = NULL;
    IDIO_MODULE_NAME (mo) = name;
    IDIO_MODULE_EXPORTS (mo) = idio_S_nil;
    IDIO_MODULE_IMPORTS (mo) = IDIO_LIST1 (idio_toplevel_module);
    IDIO_MODULE_SYMBOLS (mo) = IDIO_HASH_EQP (1<<7);

    idio_hash_put (idio_modules_hash, name, mo);
    
    return mo;
}

int idio_isa_module (IDIO mo)
{
    IDIO_ASSERT (mo);

    return idio_isa (mo, IDIO_TYPE_MODULE);
}

void idio_free_module (IDIO mo)
{
    IDIO_ASSERT (mo);

    IDIO_TYPE_ASSERT (module, mo);

    idio_gc_stats_free (sizeof (idio_module_t));

    free (mo->u.module);
}

IDIO idio_find_module (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    return idio_hash_get (idio_modules_hash, name);
}

IDIO idio_main_module ()
{
    return idio_toplevel_module;
}

IDIO idio_current_module ()
{
    return idio_main_module ();
}

void idio_set_current_module (IDIO module)
{
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);

    fprintf (stderr, "set-current-module: always sets main\n");
}

IDIO idio_defprimitive_create_module (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
	idio_error_param_type ("symbol", name);
	return idio_S_unspec;
    }

    return idio_find_module (name);
}

IDIO idio_defprimitive_current_module ()
{
    return idio_current_module ();
}

IDIO idio_defprimitive_set_current_module (IDIO module)
{
    IDIO_ASSERT (module);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    idio_set_current_module (module);

    return idio_S_unspec;
}

IDIO idio_defprimitive_set_module_imports (IDIO module, IDIO imports)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (imports);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    if (idio_toplevel_module == module ||
	idio_primitive_module == module) {
	idio_error_module_set_imports (module);
	return idio_S_unspec;
    }
    
    if (idio_isa_pair (imports)) {
	IDIO_MODULE_IMPORTS (module) = IDIO_LIST2 (imports, idio_toplevel_module);
    } else if (idio_S_nil == imports) {
	IDIO_MODULE_IMPORTS (module) = idio_S_nil;
    } else {
	idio_error_param_type ("list|nil", imports);
	return idio_S_unspec;
    }

    return idio_S_unspec;
}

IDIO idio_defprimitive_set_module_exports (IDIO module, IDIO exports)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (exports);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    if (idio_toplevel_module == module ||
	idio_primitive_module == module) {
	idio_error_module_set_exports (module);
	return idio_S_unspec;
    }
    
    if (idio_isa_pair (exports)) {
	IDIO_MODULE_EXPORTS (module) = exports;
    } else if (idio_S_nil == exports) {
	IDIO_MODULE_EXPORTS (module) = exports;
    } else {
	idio_error_param_type ("list|nil", exports);
	return idio_S_unspec;
    }

    return idio_S_unspec;
}

IDIO idio_defprimitive_modulep (IDIO module)
{
    IDIO_ASSERT (module);

    IDIO r = idio_S_false;
    if (idio_isa_module (module)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_defprimitive_find_module (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
	idio_error_param_type ("symbol", name);
	return idio_S_unspec;
    }

    return idio_find_module (name);
}

IDIO idio_defprimitive_module_name (IDIO module)
{
    IDIO_ASSERT (module);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    return IDIO_MODULE_NAME (module);
}

IDIO idio_defprimitive_module_imports (IDIO module)
{
    IDIO_ASSERT (module);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    return IDIO_MODULE_IMPORTS (module);
}

IDIO idio_defprimitive_module_exports (IDIO module)
{
    IDIO_ASSERT (module);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    if (idio_toplevel_module == module ||
	idio_primitive_module == module) {
	return idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));
    } else {
	return IDIO_MODULE_EXPORTS (module);
    }
}

IDIO idio_module_symbols (IDIO module)
{
    IDIO_ASSERT (module);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    return idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));
}

IDIO idio_module_current_symbols ()
{
    return idio_module_symbols (idio_toplevel_module);
}

IDIO idio_module_primitive_symbols ()
{
    return idio_module_symbols (idio_primitive_module);
}

IDIO idio_defprimitive_module_symbols (IDIO module)
{
    IDIO_ASSERT (module);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    return idio_module_symbols (module);
}

IDIO idio_defprimitive_all_modules ()
{
    return idio_hash_keys_to_list (idio_modules_hash);
}

IDIO idio_module_symbol_value (IDIO symbol, IDIO m_or_n)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_TYPE_ASSERT (symbol, symbol);

    IDIO module = idio_S_unspec;
    
    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_get (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    idio_error_module_unbound (m_or_n);
	    return idio_S_unspec;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n);
	return idio_S_unspec;
    }

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);

    if (idio_S_unspec == sv) {
	/*
	 * Try the list of exports of the modules we have imported
	 */
	IDIO imports = IDIO_MODULE_IMPORTS (module);
	for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	    IDIO m = IDIO_PAIR_H (imports);

	    if (! idio_isa_module (m)) {
		idio_error_module_unbound (m);
		return idio_S_unspec;
	    }

	    sv = idio_hash_get (IDIO_MODULE_SYMBOLS (m), symbol);

	    if (idio_S_unspec != sv) {
		if (idio_toplevel_module == m ||
		    idio_primitive_module == m ||
		    idio_S_false != idio_list_memq (symbol, IDIO_MODULE_EXPORTS (m))) {
		    return sv;
		}
	    }
	}

	idio_error_module_unbound_name (symbol, module);
	return idio_S_unspec;
    }
    
    return sv;    
}

IDIO idio_module_primitive_symbol_value (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_value (symbol, idio_primitive_module);    
}

IDIO idio_module_current_symbol_value (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_value (symbol, idio_current_module ());    
}

IDIO idio_defprimitive_symbol_value (IDIO symbol, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (module);

    if (! idio_isa_symbol (symbol)) {
	idio_error_param_type ("symbol", symbol);
	return idio_S_unspec;
    }

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    return idio_module_symbol_value (symbol, module);
}

IDIO idio_module_set_symbol_value (IDIO symbol, IDIO value, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    return idio_hash_put (IDIO_MODULE_SYMBOLS (module), symbol, value);
}

IDIO idio_module_set_primitive_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_primitive_module);
}

IDIO idio_module_set_current_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_current_module ());
}

IDIO idio_defprimitive_set_symbol_value (IDIO symbol, IDIO value, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);

    if (! idio_isa_symbol (symbol)) {
	idio_error_param_type ("symbol", symbol);
	return idio_S_unspec;
    }

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    return idio_module_set_symbol_value (symbol, value, module);
}

