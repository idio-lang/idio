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
 * All new modules default to importing from idio_toplevel_module.
 *
 * There is a little bit of artiface as getting and setting the
 * symbols referred to here is really an indirection to
 * idio_vm_values_(ref|set).
 */

static IDIO idio_primitive_module = idio_S_nil;
static IDIO idio_toplevel_module = idio_S_nil;
static IDIO idio_toplevel_scm_module = idio_S_nil;

void idio_module_error_duplicate_name (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("module already exists", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_module_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       name));
    idio_signal_exception (idio_S_true, c);
}

void idio_module_error_set_imports (IDIO module)
{
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("cannot set imports", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_module_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       IDIO_MODULE_NAME (module)));
    idio_signal_exception (idio_S_true, c);
}

void idio_module_error_set_exports (IDIO module)
{
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("cannot set exports", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_module_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       IDIO_MODULE_NAME (module)));
    idio_signal_exception (idio_S_true, c);
}

void idio_module_error_unbound (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("module name unbound", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_module_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       name));
    idio_signal_exception (idio_S_true, c);
}

void idio_module_error_unbound_name (IDIO symbol, IDIO module)
{
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("symbol unbound in module", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_module_symbol_unbound_error_type,
				   IDIO_LIST5 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       IDIO_MODULE_NAME (module),
					       symbol));
    idio_signal_exception (idio_S_true, c);
}

IDIO idio_module (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO m = idio_hash_get (idio_modules_hash, name);

    if (idio_S_unspec != m) {
	idio_module_error_duplicate_name (name);
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

IDIO_DEFINE_PRIMITIVE1 ("find-module", find_module, (IDIO name))
{
    IDIO_ASSERT (name);

    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    return idio_find_module (name);
}

IDIO idio_main_module ()
{
    return idio_toplevel_module;
}

IDIO idio_main_scm_module ()
{
    return idio_toplevel_scm_module;
}

IDIO_DEFINE_PRIMITIVE1 ("%create-module", create_module, (IDIO name))
{
    IDIO_ASSERT (name);

    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    IDIO m = idio_find_module (name);

    if (idio_S_unspec == m) {
	m = idio_module (name);
    }

    return m;
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
    } else if (idio_toplevel_scm_module == module) {
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

IDIO idio_module_symbols_match_type (IDIO module, IDIO symbols, IDIO type)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (symbols);
    IDIO_ASSERT (type);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (list, symbols);
    
    IDIO r = idio_S_nil;

    if (idio_S_nil == type) {
	r = symbols;
    } else {
	while (idio_S_nil != symbols) {
	    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), IDIO_PAIR_H (symbols));
	    if (idio_S_unspec == sv) {
		idio_module_error_unbound_name (module, IDIO_PAIR_H (symbols));
	    } else if (! idio_isa_pair (sv)) {
		idio_error_C ("symbol not a pair", IDIO_LIST1 (sv));
	    } else if (type == IDIO_PAIR_H (sv)) {
		r = idio_pair (IDIO_PAIR_H (symbols), r);
	    }

	    symbols = IDIO_PAIR_T (symbols);
	}
    }

    return r;
}

IDIO idio_module_visible_imported_symbols (IDIO module, IDIO type)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (type);
    IDIO_TYPE_ASSERT (module, module);

    IDIO r = idio_S_nil;

    IDIO symbols = IDIO_MODULE_EXPORTS (module);

    r = idio_module_symbols_match_type (module, symbols, type);

    IDIO imports = IDIO_MODULE_IMPORTS (module);
    for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	r = idio_list_append2 (r, idio_module_visible_imported_symbols (IDIO_PAIR_H (imports), type));
    }

    return r;
}

IDIO idio_module_visible_symbols (IDIO module, IDIO type)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (type);
    IDIO_TYPE_ASSERT (module, module);
    
    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module);
	return idio_S_unspec;
    }

    IDIO r = idio_S_nil;

    IDIO symbols = idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));

    r = idio_module_symbols_match_type (module, symbols, type);

    IDIO imports = IDIO_MODULE_IMPORTS (module);
    for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	r = idio_list_append2 (r, idio_module_visible_imported_symbols (IDIO_PAIR_H (imports), type));
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("all-modules", all_modules, ())
{
    return idio_hash_keys_to_list (idio_modules_hash);
}

/*
  idio_module_symbol_recurse will chase down the exports of imported modules
 */
IDIO idio_module_symbol_recurse_imports (IDIO symbol, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);

    /*
     * If it was a symbol in the given module then we still need to
     * check it was in the list of symbols exported from that module.
     *
     * That said, the main modules export everything.
     */
    if (idio_S_unspec != sv) {
	if (idio_toplevel_module == module ||
	    idio_toplevel_scm_module == module ||
	    idio_primitive_module == module || 
	    idio_S_false != idio_list_memq (symbol, IDIO_MODULE_EXPORTS (module))) {
	    return sv;
	}
    }

    /*
     * Otherwise try each of this module's imports.
     *
     * Note this search is depth first.
     */
    IDIO imports = IDIO_MODULE_IMPORTS (module);
    for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	sv = idio_module_symbol_recurse_imports (symbol, IDIO_PAIR_H (imports));
	if (idio_S_unspec != sv) {
	    return sv;
	}
    }

    return idio_S_unspec;
}

IDIO idio_module_symbol_recurse (IDIO symbol, IDIO m_or_n, int recurse)
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
	    idio_module_error_unbound (m_or_n);
	    return idio_S_unspec;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n);
	return idio_S_unspec;
    }

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);

    if (idio_S_unspec == sv &&
	recurse) {
	IDIO imports = IDIO_MODULE_IMPORTS (module);
	for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	    sv = idio_module_symbol_recurse_imports (symbol, IDIO_PAIR_H (imports));

	    if (idio_S_unspec != sv) {
		break;
		return sv;
	    }
	}
    }

    return sv;    
}

IDIO idio_module_primitive_symbol_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_recurse (symbol, idio_primitive_module, 1);
}

IDIO idio_module_current_symbol_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_recurse (symbol, idio_current_module (), 1);
}

IDIO idio_module_symbol (IDIO symbol, IDIO m_or_n)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_recurse (symbol, m_or_n, 0);
}

IDIO idio_module_primitive_symbol (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol (symbol, idio_primitive_module);
}

IDIO idio_module_current_symbol (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol (symbol, idio_current_module ());
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
	    idio_module_error_unbound (m_or_n);
	    return idio_S_unspec;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n);
	return idio_S_unspec;
    }

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);

    if (idio_S_unspec != sv) {
	IDIO kind = IDIO_PAIR_H (sv);
	IDIO fvi = IDIO_PAIR_H (IDIO_PAIR_T (sv));

	if (idio_S_toplevel == kind) {
	    sv = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
	} else if (idio_S_predef == kind) {
	    sv = idio_vm_primitives_ref (IDIO_FIXNUM_VAL (fvi));
	} else if (idio_S_dynamic == kind) {
	    sv = idio_vm_dynamic_ref (IDIO_FIXNUM_VAL (fvi), idio_current_thread ());
	} else if (idio_S_environ == kind) {
	    sv = idio_vm_environ_ref (IDIO_FIXNUM_VAL (fvi), idio_current_thread ());
	} else {
	    idio_error_C ("unexpected symbol kind", IDIO_LIST1 (sv));
	}
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

IDIO idio_module_toplevel_symbol_value (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_value (symbol, idio_main_module ());    
}

/*
 * Only look in the given module (or the current if none given)
 */
IDIO_DEFINE_PRIMITIVE1V ("symbol-value", symbol_value, (IDIO symbol, IDIO args))
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);

    IDIO_VERIFY_PARAM_TYPE (symbol, symbol);

    IDIO module;
    if (idio_S_nil == args) {
	module = idio_current_module ();
    } else {
	module = IDIO_PAIR_H (args);
	IDIO_VERIFY_PARAM_TYPE (module, module);
    }

    return idio_module_symbol_value (symbol, module);
}

IDIO idio_module_symbol_value_recurse (IDIO symbol, IDIO m_or_n)
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
	    idio_module_error_unbound (m_or_n);
	    return idio_S_unspec;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n);
	return idio_S_unspec;
    }

    IDIO sv = idio_module_symbol_recurse (symbol, module, 1);

    if (idio_S_unspec != sv) {
	IDIO kind = IDIO_PAIR_H (sv);
	IDIO fvi = IDIO_PAIR_H (IDIO_PAIR_T (sv));

	if (idio_S_toplevel == kind) {
	    sv = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
	} else if (idio_S_predef == kind) {
	    sv = idio_vm_primitives_ref (IDIO_FIXNUM_VAL (fvi));
	} else if (idio_S_dynamic == kind) {
	    sv = idio_vm_dynamic_ref (IDIO_FIXNUM_VAL (fvi), idio_current_thread ());
	} else if (idio_S_environ == kind) {
	    sv = idio_vm_environ_ref (IDIO_FIXNUM_VAL (fvi), idio_current_thread ());
	} else {
	    idio_error_C ("unexpected symbol kind", IDIO_LIST1 (sv));
	}
    }

    return sv;
}

IDIO idio_module_primitive_symbol_value_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_value_recurse (symbol, idio_primitive_module);    
}

IDIO idio_module_current_symbol_value_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_value_recurse (symbol, idio_current_module ());    
}

IDIO idio_module_toplevel_symbol_value_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_symbol_value_recurse (symbol, idio_main_module ());    
}

/*
 * Recursively find starting at the given module (or the current
 * module if none given)
 */
IDIO_DEFINE_PRIMITIVE1V ("symbol-value-recurse", symbol_value_recurse, (IDIO symbol, IDIO args))
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);

    IDIO_VERIFY_PARAM_TYPE (symbol, symbol);

    IDIO module;
    if (idio_S_nil == args) {
	module = idio_current_module ();
    } else {
	module = IDIO_PAIR_H (args);
	IDIO_VERIFY_PARAM_TYPE (module, module);
    }

    return idio_module_symbol_value_recurse (symbol, module);
}

IDIO idio_module_set_symbol (IDIO symbol, IDIO value, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    return idio_hash_put (IDIO_MODULE_SYMBOLS (module), symbol, value);
}

IDIO idio_module_primitive_set_symbol (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol (symbol, value, idio_primitive_module);
}

IDIO idio_module_current_set_symbol (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol (symbol, value, idio_current_module ());
}

IDIO idio_module_toplevel_set_symbol (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol (symbol, value, idio_main_module ());
}

IDIO idio_module_set_symbol_value (IDIO symbol, IDIO value, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);
    IDIO kind;
    IDIO fvi;
    
    if (idio_S_unspec == sv) {
	idio_ai_t vi = idio_vm_extend_symbols (symbol);
	kind = idio_S_toplevel;
	fvi = IDIO_FIXNUM (vi);
	
	idio_hash_put (IDIO_MODULE_SYMBOLS (module), symbol, IDIO_LIST2 (idio_S_toplevel, fvi));
    } else {
	kind = IDIO_PAIR_H (sv);
	fvi = IDIO_PAIR_H (IDIO_PAIR_T (sv));
    }

    if (idio_S_toplevel == kind) {
	idio_vm_values_set (IDIO_FIXNUM_VAL (fvi), value);
    } else if (idio_S_dynamic == kind) {
	idio_vm_dynamic_set (IDIO_FIXNUM_VAL (fvi), value, idio_current_thread ());
    } else if (idio_S_environ == kind) {
	idio_vm_environ_set (IDIO_FIXNUM_VAL (fvi), value, idio_current_thread ());
    } else if (idio_S_predef == kind) {
	idio_error_C ("cannot set a primitive", IDIO_LIST1 (symbol));
    } else {
	idio_error_C ("unexpected symbol kind", IDIO_LIST1 (sv));
    }

    return value;
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

IDIO idio_module_toplevel_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_main_module ());
}

IDIO_DEFINE_PRIMITIVE3 ("set-symbol-value!", set_symbol_value, (IDIO symbol, IDIO value, IDIO module))
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

    idio_toplevel_scm_module = idio_module (idio_symbols_C_intern ("SCM"));
    IDIO_MODULE_IMPORTS (idio_toplevel_scm_module) = IDIO_LIST1 (idio_primitive_module);
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
    IDIO_ADD_PRIMITIVE (symbol_value_recurse);
    IDIO_ADD_PRIMITIVE (set_symbol_value);
}

void idio_final_module ()
{
    idio_gc_expose (idio_modules_hash);
}

