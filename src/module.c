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
    char *ss = idio_as_string (symbol, 1);
    fprintf (stderr, "%s is unbound in %s\n", ss, IDIO_SYMBOL_S (IDIO_MODULE_NAME (module)));
    free (ss);

    ss = idio_as_string (IDIO_MODULE_SYMBOLS (module), 1);
    fprintf (stderr, "symbols: %s\n", ss);
    free (ss);
    
    char *is = idio_as_string (IDIO_MODULE_IMPORTS (module), 1);
    fprintf (stderr, "%s imports %s\n", IDIO_SYMBOL_S (IDIO_MODULE_NAME (module)), is);
    free (is);
    IDIO i = IDIO_MODULE_IMPORTS (module);
    while (idio_S_nil != i) {
	IDIO m = IDIO_PAIR_H (i);

	char *es = idio_as_string (IDIO_MODULE_EXPORTS (m), 1);
	fprintf (stderr, "  %s: %s\n", IDIO_SYMBOL_S (IDIO_MODULE_NAME (m)), es);
	free (es);

	i = IDIO_PAIR_T (i);
    }
    idio_error_message ("symbol %s unbound in module %s", IDIO_SYMBOL_S (symbol), IDIO_SYMBOL_S (IDIO_MODULE_NAME (module)));
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
    IDIO_MODULE_DEFINED (mo) = idio_S_nil;

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

IDIO_DEFINE_PRIMITIVE1 ("%create-module", create_module, (IDIO name))
{
    IDIO_ASSERT (name);

    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    return idio_find_module (name);
}

IDIO_DEFINE_PRIMITIVE0 ("current-module", current_module, ())
{
    return idio_current_module ();
}

IDIO_DEFINE_PRIMITIVE1 ("%set-current-module!", set_current_module, (IDIO module))
{
    IDIO_ASSERT (module);

    IDIO_VERIFY_PARAM_TYPE (module, module);

    idio_set_current_module (module);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2 ("%set-module-imports!", set_module_imports, (IDIO module, IDIO imports))
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (imports);

    IDIO_VERIFY_PARAM_TYPE (module, module);

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

IDIO_DEFINE_PRIMITIVE2 ("%set-module-exports!", set_module_exports, (IDIO module, IDIO exports))
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (exports);

    IDIO_VERIFY_PARAM_TYPE (module, module);

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

IDIO_DEFINE_PRIMITIVE1 ("module?", modulep, (IDIO module))
{
    IDIO_ASSERT (module);

    IDIO r = idio_S_false;
    if (idio_isa_module (module)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("find-module", find_module, (IDIO name))
{
    IDIO_ASSERT (name);

    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    return idio_find_module (name);
}

IDIO_DEFINE_PRIMITIVE1 ("module-name", module_name, (IDIO module))
{
    IDIO_ASSERT (module);

    IDIO_VERIFY_PARAM_TYPE (module, module);

    return IDIO_MODULE_NAME (module);
}

IDIO_DEFINE_PRIMITIVE1 ("module-imports", module_imports, (IDIO module))
{
    IDIO_ASSERT (module);

    IDIO_VERIFY_PARAM_TYPE (module, module);

    return IDIO_MODULE_IMPORTS (module);
}

IDIO_DEFINE_PRIMITIVE1 ("module-exports", module_exports, (IDIO module))
{
    IDIO_ASSERT (module);

    IDIO_VERIFY_PARAM_TYPE (module, module);

    if (idio_toplevel_module == module) {
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
    return idio_module_symbols (idio_current_module ());
}

IDIO idio_module_primitive_symbols ()
{
    return idio_module_symbols (idio_primitive_module);
}

IDIO_DEFINE_PRIMITIVE1 ("module-symbols", module_symbols, (IDIO module))
{
    IDIO_ASSERT (module);

    IDIO_VERIFY_PARAM_TYPE (module, module);

    return idio_module_symbols (module);
}

IDIO idio_module_defined (IDIO module)
{
    IDIO_ASSERT (module);

    IDIO_TYPE_ASSERT (module, module);

    return IDIO_MODULE_DEFINED (module);
}

IDIO idio_module_current_defined ()
{
    return idio_module_defined (idio_current_module ());
}

IDIO idio_module_primitive_defined ()
{
    return idio_module_defined (idio_primitive_module);
}

void idio_module_extend_defined (IDIO module, IDIO name)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO_MODULE_DEFINED (module) = idio_pair (name, IDIO_MODULE_DEFINED (module));
}

void idio_module_current_extend_defined (IDIO name)
{
    IDIO_ASSERT (name);
    
    idio_module_extend_defined (idio_current_module (), name);
}

void idio_module_primitive_extend_defined (IDIO name)
{
    IDIO_ASSERT (name);
    
    idio_module_extend_defined (idio_primitive_module, name);
}

IDIO_DEFINE_PRIMITIVE0 ("all-modules", all_modules, ())
{
    return idio_hash_keys_to_list (idio_modules_hash);
}

/*
  idio_symbol_lookup will chase down the exports of imported modules
 */
IDIO idio_symbol_lookup_imports (IDIO symbol, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);

    if (idio_S_unspec != sv) {
	if (idio_toplevel_module == module ||
	    idio_primitive_module == module || 
	    idio_S_false != idio_list_memq (symbol, IDIO_MODULE_EXPORTS (module))) {
	    return sv;
	}
    }

    IDIO imports = IDIO_MODULE_IMPORTS (module);
    for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	sv = idio_symbol_lookup_imports (symbol, IDIO_PAIR_H (imports));
	if (idio_S_unspec != sv) {
	    return sv;
	}
    }

    return idio_S_unspec;
}

IDIO idio_symbol_lookup (IDIO symbol, IDIO m_or_n)
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
	IDIO imports = IDIO_MODULE_IMPORTS (module);
	for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	    sv = idio_symbol_lookup_imports (symbol, IDIO_PAIR_H (imports));

	    if (idio_S_unspec != sv) {
		return sv;
	    }
	}

	return idio_S_unspec;
    }

    return sv;    
}

/*
 * idio_module_symbol_value will only look in the current module
 */
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

    return idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);
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

IDIO_DEFINE_PRIMITIVE2 ("symbol-value", symbol_value, (IDIO symbol, IDIO module))
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (module);

    IDIO_VERIFY_PARAM_TYPE (symbol, symbol);
    IDIO_VERIFY_PARAM_TYPE (module, module);

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

IDIO idio_module_primitive_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_primitive_module);
}

IDIO idio_module_current_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_current_module ());
}

IDIO_DEFINE_PRIMITIVE3 ("set-symbol-value", set_symbol_value, (IDIO symbol, IDIO value, IDIO module))
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);

    IDIO_VERIFY_PARAM_TYPE (symbol, symbol);
    IDIO_VERIFY_PARAM_TYPE (module, module);

    return idio_module_set_symbol_value (symbol, value, module);
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

void idio_module_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (create_module);
    IDIO_ADD_PRIMITIVE (current_module);
    IDIO_ADD_PRIMITIVE (set_current_module);
    IDIO_ADD_PRIMITIVE (set_module_imports);
    IDIO_ADD_PRIMITIVE (set_module_exports);
    IDIO_ADD_PRIMITIVE (modulep);
    IDIO_ADD_PRIMITIVE (find_module);
    IDIO_ADD_PRIMITIVE (module_name);
    IDIO_ADD_PRIMITIVE (module_imports);
    IDIO_ADD_PRIMITIVE (module_exports);
    IDIO_ADD_PRIMITIVE (module_symbols);
    IDIO_ADD_PRIMITIVE (all_modules);
    IDIO_ADD_PRIMITIVE (symbol_value);
    IDIO_ADD_PRIMITIVE (set_symbol_value);
}

void idio_final_module ()
{
    idio_gc_expose (idio_modules_hash);
}

