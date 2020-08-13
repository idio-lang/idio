/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * idio_primitives_module is the set of built-ins -- not modifiable!
 *
 * idio_Idio_module is the default toplevel module which imports from
 * idio_primitives_module
 *
 * All new modules implicitly import from idio_Idio_module then
 * idio_primitives_module.
 *
 * There is a little bit of artiface as getting and setting the
 * symbols referred to here is really an indirection to
 * idio_vm_values_(ref|set).
 */

IDIO idio_primitives_module = idio_S_nil;
IDIO idio_Idio_module = idio_S_nil;

void idio_module_error_duplicate_name (IDIO name, IDIO c_location)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("module already exists", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_module_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       name));
    idio_raise_condition (idio_S_true, c);
}

void idio_module_error_set_imports (IDIO module, IDIO c_location)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("cannot set imports", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_module_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       IDIO_MODULE_NAME (module)));
    idio_raise_condition (idio_S_true, c);
}

void idio_module_error_set_exports (IDIO module, IDIO c_location)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("cannot set exports", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_module_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       IDIO_MODULE_NAME (module)));
    idio_raise_condition (idio_S_true, c);
}

void idio_module_error_unbound (IDIO name, IDIO c_location)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("module name unbound", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_module_unbound_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       name));
    idio_raise_condition (idio_S_true, c);
}

void idio_module_error_unbound_name (IDIO symbol, IDIO module, IDIO c_location)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (module);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("symbol unbound in module", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_module_symbol_unbound_error_type,
				   IDIO_LIST5 (idio_get_output_string (sh),
					       location,
					       c_location,
					       module,
					       symbol));
    idio_raise_condition (idio_S_true, c);
}

IDIO idio_module (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO m = idio_hash_get (idio_modules_hash, name);

    if (idio_S_unspec != m) {
	idio_module_error_duplicate_name (name, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }

    IDIO mo = idio_gc_get (IDIO_TYPE_MODULE);

    IDIO_GC_ALLOC (mo->u.module, sizeof (idio_module_t));

    IDIO_MODULE_GREY (mo) = NULL;
    IDIO_MODULE_NAME (mo) = name;
    IDIO_MODULE_EXPORTS (mo) = idio_S_nil;
    /*
     * IDIO_MODULE_IMPORTS (module) will result in semi-gibberish for
     * idio_primitives_module, idio_command_module and idio_Idio_module
     * which we patch up immediately after this call returns
     */
    IDIO_MODULE_IMPORTS (mo) = IDIO_LIST3 (IDIO_LIST1 (idio_command_module),
					   IDIO_LIST1 (idio_Idio_module),
					   IDIO_LIST1 (idio_primitives_module));
    IDIO_MODULE_SYMBOLS (mo) = IDIO_HASH_EQP (1<<7);
    IDIO_MODULE_VCI (mo) = IDIO_HASH_EQP (1<<10);
    IDIO_MODULE_VVI (mo) = IDIO_HASH_EQP (1<<10);

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

IDIO idio_module_get_vci (IDIO module, IDIO mci)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (mci);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (fixnum, mci);

    return idio_hash_get (IDIO_MODULE_VCI (module), mci);
}

IDIO idio_module_set_vci (IDIO module, IDIO mci, IDIO gci)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (mci);
    IDIO_ASSERT (gci);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (fixnum, mci);
    IDIO_TYPE_ASSERT (fixnum, gci);

    return idio_hash_put (IDIO_MODULE_VCI (module), mci, gci);
}

IDIO idio_module_get_or_set_vci (IDIO module, IDIO mci)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (mci);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (fixnum, mci);

    IDIO r = idio_module_get_vci (module, mci);

    if (idio_S_unspec == r) {
	r = idio_module_set_vci (module, mci, mci);
    }

    return r;
}

IDIO idio_module_get_vvi (IDIO module, IDIO mci)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (mci);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (fixnum, mci);

    return idio_hash_get (IDIO_MODULE_VVI (module), mci);
}

IDIO idio_module_set_vvi (IDIO module, IDIO msi, IDIO gvi)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (msi);
    IDIO_ASSERT (gvi);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (fixnum, msi);
    IDIO_TYPE_ASSERT (fixnum, gvi);

    return idio_hash_put (IDIO_MODULE_VVI (module), msi, gvi);
}

IDIO idio_module_find_module (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    return idio_hash_get (idio_modules_hash, name);
}

IDIO_DEFINE_PRIMITIVE1V ("find-module", find_module, (IDIO name, IDIO args))
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (symbol, name);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO r = idio_module_find_module (name);

    if (idio_S_unspec == r) {
	if (idio_S_nil != args) {
	    r = IDIO_PAIR_H (args);
	} else {
	    idio_module_error_unbound (name, IDIO_C_FUNC_LOCATION ());
	}
    }

    return r;
}

IDIO idio_Idio_module_instance ()
{
    return idio_Idio_module;
}

IDIO idio_primitives_module_instance ()
{
    return idio_primitives_module;
}

IDIO idio_module_find_or_create_module (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    IDIO m = idio_module_find_module (name);

    if (idio_S_unspec == m) {
	m = idio_module (name);
    }

    return m;
}

IDIO_DEFINE_PRIMITIVE1 ("%find-or-create-module", find_or_create_module, (IDIO name))
{
    IDIO_ASSERT (name);
    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    return idio_module_find_or_create_module (name);
}

IDIO_DEFINE_PRIMITIVE0 ("current-module", current_module, ())
{
    return idio_thread_current_module ();
}

IDIO_DEFINE_PRIMITIVE1 ("%set-current-module!", set_current_module, (IDIO module))
{
    IDIO_ASSERT (module);
    IDIO_VERIFY_PARAM_TYPE (module, module);

    idio_thread_set_current_module (module);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2 ("%set-module-imports!", set_module_imports, (IDIO module, IDIO imports))
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (imports);
    IDIO_VERIFY_PARAM_TYPE (module, module);

    if (idio_isa_list (imports)) {
	IDIO_MODULE_IMPORTS (module) = imports;
    } else {
	idio_error_param_type ("list|nil", imports, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }

    return idio_S_unspec;
}

/*
 * Invoke the functionally of %module-import in module.idio --
 * required by the evaluator as a dirty hack...
 */
IDIO idio_module_extend_imports (IDIO module, IDIO syms)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (syms);

    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (list, syms);

    idio_vm_invoke_C (idio_thread_current_thread (),
		      IDIO_LIST3 (idio_module_symbol_value (idio_S_pct_module_import,
							    idio_Idio_module,
							    idio_S_nil),
				  module,
				  syms));

    return idio_S_unspec;
}

/*
 * Functionally the same as %module-export in module.idio but used by
 * the evaluator as a dirty hack...
 */
IDIO idio_module_extend_exports (IDIO module, IDIO syms)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (syms);

    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (list, syms);

    IDIO exports = IDIO_MODULE_EXPORTS (module);

    while (idio_S_nil != syms) {
	IDIO sym = IDIO_PAIR_H (syms);
	if (idio_S_false == idio_list_memq (sym, exports)) {
	    exports = idio_pair (sym, exports);
	}
	syms = IDIO_PAIR_T (syms);
    }

    IDIO_MODULE_EXPORTS (module) = exports;

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2 ("%set-module-exports!", set_module_exports, (IDIO module, IDIO exports))
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (exports);
    IDIO_VERIFY_PARAM_TYPE (module, module);

    if (idio_isa_list (exports)) {
	IDIO_MODULE_EXPORTS (module) = exports;
    } else {
	idio_error_param_type ("list|nil", exports, IDIO_C_FUNC_LOCATION ());
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

IDIO_DEFINE_PRIMITIVE1 ("module-imports", module_imports, (IDIO m_or_n))
{
    IDIO_ASSERT (m_or_n);

    IDIO module = idio_S_unspec;

    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_get (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    idio_module_error_unbound (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_MODULE_IMPORTS (module);
}

IDIO_DEFINE_PRIMITIVE1 ("module-exports", module_exports, (IDIO m_or_n))
{
    IDIO_ASSERT (m_or_n);

    IDIO module = idio_S_unspec;

    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_get (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    idio_module_error_unbound (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (idio_Idio_module == module) {
	return idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));
    } else {
	return IDIO_MODULE_EXPORTS (module);
    }
}

IDIO idio_module_symbols (IDIO m_or_n)
{
    IDIO_ASSERT (m_or_n);

    IDIO module = idio_S_unspec;

    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_get (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    idio_module_error_unbound (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));
}

IDIO idio_module_current_symbols ()
{
    return idio_module_symbols (idio_thread_current_module ());
}

IDIO idio_module_primitive_symbols ()
{
    return idio_module_symbols (idio_primitives_module);
}

IDIO_DEFINE_PRIMITIVE0V ("module-symbols", module_symbols, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO module = idio_thread_current_module ();

    if (idio_S_nil != args &&
	idio_isa_module (IDIO_PAIR_H (args))) {
	module = IDIO_PAIR_H (args);
    }

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
	    IDIO sym = IDIO_PAIR_H (symbols);
	    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), sym);
	    if (idio_S_unspec == sv) {
		idio_module_error_unbound_name (sym, module, IDIO_C_FUNC_LOCATION ());
	    } else if (! idio_isa_pair (sv)) {
		idio_error_C ("symbol not a pair", sv, IDIO_C_FUNC_LOCATION ());
	    } else if (type == IDIO_PAIR_H (sv)) {
		r = idio_pair (sym, r);
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
    if (idio_Idio_module == module ||
	idio_primitives_module == module) {
	symbols = idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));
    }

    r = idio_module_symbols_match_type (module, symbols, type);

    /*
    IDIO imports = IDIO_MODULE_IMPORTS (module);
    for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	r = idio_list_append2 (r, idio_module_visible_imported_symbols (IDIO_PAIR_H (imports), type));
    }
    */

    return r;
}

IDIO idio_module_visible_symbols (IDIO module, IDIO type)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (type);
    IDIO_TYPE_ASSERT (module, module);

    if (! idio_isa_module (module)) {
	idio_error_param_type ("module", module, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }

    int seen_Idio = 0;

    IDIO r = idio_S_nil;

    IDIO symbols = idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));
    if (idio_eqp (module, idio_Idio_module)) {
	seen_Idio = 1;
    }

    r = idio_module_symbols_match_type (module, symbols, type);

    IDIO imports = IDIO_MODULE_IMPORTS (module);
    for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	IDIO import = IDIO_PAIR_H (imports);
	IDIO im = IDIO_PAIR_H (import);
	r = idio_list_append2 (r, idio_module_visible_imported_symbols (im, type));
	if (0 == seen_Idio &&
	    idio_eqp (im, idio_Idio_module)) {
	    seen_Idio = 1;
	}
    }

    if (0 == seen_Idio) {
	r = idio_list_append2 (r, idio_module_visible_imported_symbols (idio_Idio_module, type));
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("all-modules", all_modules, ())
{
    return idio_hash_keys_to_list (idio_modules_hash);
}

/*
 * if name is like M/S -- where S does not contain a / -- and M is a
 * module and S is exported by that module then it's good.
 *
 * Avoid /S and S/, ie. a symbol beginning or ending in /
 */
IDIO idio_module_direct_reference (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO r = idio_S_unspec;

    char *name_C = IDIO_SYMBOL_S (name);
    char *slash = strrchr (name_C, '/');
    if (NULL != slash &&
	'\0' != slash[1] &&
	slash != name_C) {
	/*
	 * Interning a symbol requires a NUL-terminated C string.
	 * {name}, being a symbol, already is NUL-terminated however
	 * we need symbols for both {M} and {S} which requires
	 * over-writing the / with a NUL.
	 *
	 * Just to be safe we'll use a copy of name.
	 */
	char *copy_C = idio_alloc (strlen (name_C) + 1);
	strcpy (copy_C, name_C);
	slash = strrchr (copy_C, '/');

	if (NULL == slash) {
	    idio_error_C ("failed to strcpy a string", name, IDIO_C_FUNC_LOCATION ());
	}
	*slash = '\0';


	IDIO m_sym = idio_symbols_C_intern (copy_C);
	IDIO s_sym = idio_symbols_C_intern (slash + 1);

	free (copy_C);

	IDIO mod = idio_module_find_module (m_sym);

	if (idio_S_unspec != mod) {
	    IDIO f = idio_list_memq (s_sym, IDIO_MODULE_EXPORTS (mod));

	    if (idio_S_false != f) {
		IDIO sk = idio_module_find_symbol (s_sym, mod);

		if (idio_S_unspec != sk) {
		    /*
		     * Our result sk should look similar to the result
		     * we've just found except that the mci cannot be
		     * the same: the symbols M/S is not the same as
		     * the symbol S.
		     */
		    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
		    r = IDIO_LIST3 (m_sym,
				    s_sym,
				    IDIO_LIST5 (IDIO_PAIR_H (sk), idio_fixnum (mci), IDIO_PAIR_HTT (sk), mod, idio_string_C ("idio_module_direct_reference")));
		}
	    }
	}
    }

    return r;
}

/*
 * idio_module_find_symbol_recurse will chase down the exports of imported
 * modules
 */
IDIO idio_module_find_symbol_recurse_imports (IDIO symbol, IDIO imported_module, IDIO break_module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (imported_module);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, imported_module);

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (imported_module), symbol);

    /*
     * If it was a symbol in the given module then we still need to
     * check it was in the list of symbols exported from that module.
     *
     * That said, the main modules export everything.
     */
    if (idio_S_unspec != sv) {
	if (idio_Idio_module == imported_module ||
	    idio_primitives_module == imported_module ||
	    idio_S_false != idio_list_memq (symbol, IDIO_MODULE_EXPORTS (imported_module))) {
	    return sv;
	}
    }

    return idio_S_unspec;
}

/*
 * {recurse} is a tri-state variable:
 *
 * 0 - look in this module only -- see idio_module_find_symbol()
 * 1 - look in this module and imports
 * 2 - look in imports only -- see idio_vm_get_or_create_vvi()
 */
IDIO idio_module_find_symbol_recurse (IDIO symbol, IDIO m_or_n, int recurse)
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
	    idio_module_error_unbound (m_or_n, IDIO_C_FUNC_LOCATION ());
	    return idio_S_unspec;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }

    /* idio_debug ("im_fsr %s", IDIO_MODULE_NAME (module)); */
    /* idio_debug ("/%s", symbol); */
    /* fprintf (stderr, " recurse=%d\n", recurse); */

    IDIO cm_sk = idio_S_unspec;
    if (recurse < 2) {
	cm_sk = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);
    } else {
	/* idio_debug ("im_fsr %s", IDIO_MODULE_NAME (module));  */
	/* idio_debug ("/%s", symbol);  */
	/* fprintf (stderr, " recurse > 1\n");  */
    }

    if (idio_S_unspec != cm_sk) {
	/*
	 * NB a vi of 0 indicates an unresolved value index to be
	 * resolved (based on the current set of imports) during
	 * runtime.
	 *
	 * Here, reset cm_sk to #unspec (as though we had failed to find
	 * the symbol in our own module) and carry on.
	 */
	IDIO kind = IDIO_PAIR_H (cm_sk);
	IDIO fgvi = IDIO_PAIR_HTT (cm_sk);
	idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);
	if (idio_S_toplevel == kind) {
	    if (0 == gvi) {
		if (recurse) {
		    cm_sk = idio_S_unspec;
		}
	    }
	}
    }

    if (idio_S_unspec == cm_sk &&
	recurse) {
	IDIO imports = IDIO_MODULE_IMPORTS (module);
	for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	    IDIO im = IDIO_PAIR_HH (imports);

	    if (! idio_isa_module (im)) {
		idio_debug ("module import error: not a module: %s imports", module);
		idio_debug (" %s\n", IDIO_MODULE_IMPORTS (module));
		continue;
	    }
	    IDIO mi_sk = idio_module_find_symbol_recurse_imports (symbol, im, module);

	    if (idio_S_unspec != mi_sk) {
		/*
		 * Careful!  Same trick as above.
		 */
		IDIO kind = IDIO_PAIR_H (mi_sk);
		IDIO fgvi = IDIO_PAIR_HTT (mi_sk);
		idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

		if (gvi > 0) {
		    cm_sk = mi_sk;
		    /*
		     * Lookup resolved, copy it for next time.
		     */
		    idio_hash_put (IDIO_MODULE_SYMBOLS (module), symbol, cm_sk);
		    break;
		}
	    }
	}
    }

    /* idio_debug ("im_fsr %s", IDIO_MODULE_NAME (module));  */
    /* idio_debug ("/%s", symbol);  */
    /* idio_debug (" => cm_sk=%s\n", cm_sk);  */
    return cm_sk;
}

IDIO idio_module_primitive_symbol_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_find_symbol_recurse (symbol, idio_primitives_module, 1);
}

IDIO idio_module_current_symbol_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_find_symbol_recurse (symbol, idio_thread_current_module (), 1);
}

IDIO idio_module_env_symbol_recurse (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_find_symbol_recurse (symbol, idio_thread_current_env (), 1);
}

IDIO idio_module_find_symbol (IDIO symbol, IDIO m_or_n)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_find_symbol_recurse (symbol, m_or_n, 0);
}

IDIO idio_module_primitive_symbol (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_find_symbol (symbol, idio_primitives_module);
}

IDIO idio_module_current_symbol (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_find_symbol (symbol, idio_thread_current_module ());
}

IDIO idio_module_env_symbol (IDIO symbol)
{
    IDIO_ASSERT (symbol);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_find_symbol (symbol, idio_thread_current_env ());
}

IDIO_DEFINE_PRIMITIVE1V ("find-symbol", find_symbol, (IDIO symbol, IDIO args))
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (symbol, symbol);

    IDIO m_or_n = idio_thread_current_env ();

    if (idio_isa_pair (args)) {
	IDIO m = IDIO_PAIR_H (args);
	if (idio_isa_module (m) ||
	    idio_isa_symbol (m)) {
	    m_or_n = m;
	}
    }

    return idio_module_find_symbol_recurse (symbol, m_or_n, 1);
}

/*
 * idio_module_symbol_value will only look in the current module
 */
IDIO idio_module_symbol_value (IDIO symbol, IDIO m_or_n, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    IDIO module = idio_S_unspec;

    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_get (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    idio_module_error_unbound (m_or_n, IDIO_C_FUNC_LOCATION ());
	    return idio_S_unspec;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());
	return idio_S_unspec;
    }

    IDIO sk = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);

    IDIO r = idio_S_unspec;
    if (idio_S_nil != args) {
	r = IDIO_PAIR_H (args);
    }

    if (idio_S_unspec != sk) {
	IDIO kind = IDIO_PAIR_H (sk);
	IDIO fmci = IDIO_PAIR_HT (sk);
	IDIO fgvi = IDIO_PAIR_HTT (sk);

	if (idio_S_toplevel == kind) {
	    r = idio_vm_values_ref (IDIO_FIXNUM_VAL (fgvi));
	} else if (idio_S_predef == kind) {
	    r = idio_vm_values_ref (IDIO_FIXNUM_VAL (fgvi));
	} else if (idio_S_dynamic == kind) {
	    r = idio_vm_dynamic_ref (IDIO_FIXNUM_VAL (fmci), IDIO_FIXNUM_VAL (fgvi), idio_thread_current_thread (), args);
	} else if (idio_S_environ == kind) {
	    r = idio_vm_environ_ref (IDIO_FIXNUM_VAL (fmci), IDIO_FIXNUM_VAL (fgvi), idio_thread_current_thread (), args);
	} else if (idio_S_computed == kind) {
	    r = idio_vm_computed_ref (IDIO_FIXNUM_VAL (fmci), IDIO_FIXNUM_VAL (fgvi), idio_thread_current_thread ());
	} else {
	    idio_error_C ("unexpected symbol kind", IDIO_LIST3 (symbol, module, sk), IDIO_C_FUNC_LOCATION ());
	}
    }

    return r;
}

IDIO idio_module_primitive_symbol_value (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value (symbol, idio_primitives_module, args);
}

IDIO idio_module_current_symbol_value (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value (symbol, idio_thread_current_module (), args);
}

IDIO idio_module_env_symbol_value (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value (symbol, idio_thread_current_env (), args);
}

IDIO idio_module_toplevel_symbol_value (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value (symbol, idio_Idio_module, args);
}

/*
 * Only look in the given module.  If not present return the optional
 * default or error.
 */
IDIO_DEFINE_PRIMITIVE2V ("symbol-value", symbol_value, (IDIO symbol, IDIO m_or_n, IDIO args))
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value (symbol, m_or_n, args);
}

IDIO idio_module_symbol_value_recurse (IDIO symbol, IDIO m_or_n, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    IDIO module = idio_S_unspec;

    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_get (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    idio_module_error_unbound (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO sk = idio_module_find_symbol_recurse (symbol, module, 1);

    IDIO r = idio_S_unspec;

    if (idio_isa_pair (args)) {
	r = IDIO_PAIR_H (args);
    }

    if (idio_S_unspec != sk) {
	IDIO kind = IDIO_PAIR_H (sk);
	IDIO fmci = IDIO_PAIR_HT (sk);
	IDIO fgvi = IDIO_PAIR_HTT (sk);
	idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

	if (gvi) {
	    if (idio_S_toplevel == kind) {
		r = idio_vm_values_ref (gvi);
	    } else if (idio_S_predef == kind) {
		r = idio_vm_values_ref (gvi);
	    } else if (idio_S_dynamic == kind) {
		r = idio_vm_dynamic_ref (IDIO_FIXNUM_VAL (fmci), gvi, idio_thread_current_thread (), args);
	    } else if (idio_S_environ == kind) {
		r = idio_vm_environ_ref (IDIO_FIXNUM_VAL (fmci), gvi, idio_thread_current_thread (), args);
	    } else if (idio_S_computed == kind) {
		idio_error_C ("cannot get a computed variable from C", IDIO_LIST3 (symbol, module, sk), IDIO_C_FUNC_LOCATION ());
	    } else {
		idio_error_C ("unexpected symbol kind", IDIO_LIST3 (symbol, module, sk), IDIO_C_FUNC_LOCATION ());
	    }
	}
    }

    return r;
}

IDIO idio_module_primitive_symbol_value_recurse (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_recurse (symbol, idio_primitives_module, args);
}

IDIO idio_module_current_symbol_value_recurse (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_recurse (symbol, idio_thread_current_module (), args);
}

IDIO idio_module_env_symbol_value_recurse (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_recurse (symbol, idio_thread_current_env (), args);
}

IDIO idio_module_toplevel_symbol_value_recurse (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_recurse (symbol, idio_Idio_module, args);
}

/*
 * Recursively find starting at the given module (or the current
 * module if none given)
 */
IDIO_DEFINE_PRIMITIVE1V_DS ("symbol-value-recurse", symbol_value_recurse, (IDIO symbol, IDIO args), "symbol [mod [default]]", "\
recursively find ``symbol`` starting in ``mod`` and return its		\n\
value or ``default`` if no value found					\n\
									\n\
:param symbol: symbol to find						\n\
:type symbol: symbol							\n\
:param mod: module to search from					\n\
:type mod: module or module name					\n\
:param default: value to return if ``symbol`` is not found		\n\
:return: value of ``symbol``						\n\
")
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    IDIO m_or_n = idio_thread_current_env ();

    if (idio_isa_pair (args)) {
	m_or_n = IDIO_PAIR_H (args);
	args = IDIO_PAIR_T (args);
    }

    return idio_module_symbol_value_recurse (symbol, m_or_n, args);
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

    return idio_module_set_symbol (symbol, value, idio_primitives_module);
}

IDIO idio_module_current_set_symbol (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol (symbol, value, idio_thread_current_module ());
}

IDIO idio_module_env_set_symbol (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol (symbol, value, idio_thread_current_env ());
}

IDIO idio_module_toplevel_set_symbol (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol (symbol, value, idio_Idio_module);
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
    IDIO fmci;
    IDIO fgvi;

    if (idio_S_unspec == sv) {
	/*
	 * C init code will get us here without having been through
	 * the evaluator.  These will be implicitly idio_S_toplevel
	 * symbols in this module.
	 */
	kind = idio_S_toplevel;

	idio_ai_t mci = idio_vm_constants_lookup_or_extend (symbol);
	fmci = idio_fixnum (mci);

	idio_ai_t gvi = idio_vm_extend_values ();
	fgvi = idio_fixnum (gvi);

	idio_hash_put (IDIO_MODULE_SYMBOLS (module), symbol, IDIO_LIST5 (kind, fmci, fgvi, module, idio_string_C ("idio_module_set_symbol_value")));
    } else {
	kind = IDIO_PAIR_H (sv);
	fmci = IDIO_PAIR_HT (sv);
	fgvi = IDIO_PAIR_HTT (sv);
    }

    idio_ai_t mci = IDIO_FIXNUM_VAL (fmci);
    idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);
    if (0 == gvi) {
	/*
	 * Again, C init code will have called idio_toplevel_extend()
	 * and then immediately set_symbol_value() which means the gvi
	 * remains 0.
	 */
	gvi = idio_vm_extend_values ();
	fgvi = idio_fixnum (gvi);
	IDIO_PAIR_HTT (sv) = idio_fixnum (gvi);
    }

    idio_module_set_vci (module, fmci, fmci);
    idio_module_set_vvi (module, fmci, fgvi);

    if (idio_S_toplevel == kind) {
	idio_vm_values_set (gvi, value);
    } else if (idio_S_dynamic == kind) {
	idio_vm_dynamic_set (mci, gvi, value, idio_thread_current_thread ());
    } else if (idio_S_environ == kind) {
	idio_vm_environ_set (mci, gvi, value, idio_thread_current_thread ());
    } else if (idio_S_computed == kind) {
	idio_vm_computed_set (mci, gvi, value, idio_thread_current_thread ());
    } else if (idio_S_predef == kind) {
	if (module == idio_primitives_module) {
	    idio_error_C ("cannot set a primitive", IDIO_LIST2 (symbol, module), IDIO_C_FUNC_LOCATION ());
	} else {
	    idio_vm_values_set (gvi, value);
	}
    } else {
	idio_error_C ("unexpected symbol kind", IDIO_LIST3 (symbol, module, sv), IDIO_C_FUNC_LOCATION ());
    }

    return value;
}

/*
 * A handy all-in-one for C init code
 */
IDIO idio_module_export_symbol_value (IDIO symbol, IDIO value, IDIO module)
{
    IDIO_MODULE_EXPORTS (module) = idio_pair (symbol, IDIO_MODULE_EXPORTS (module));
    return idio_module_set_symbol_value (symbol, value, module);
}

IDIO idio_module_primitive_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_primitives_module);
}

IDIO idio_module_current_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_thread_current_module ());
}

IDIO idio_module_env_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_thread_current_env ());
}

IDIO idio_module_toplevel_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_Idio_module);
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

IDIO idio_module_add_computed_symbol (IDIO symbol, IDIO get, IDIO set, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (get);
    IDIO_ASSERT (set);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    IDIO sv = idio_hash_get (IDIO_MODULE_SYMBOLS (module), symbol);
    IDIO kind;
    IDIO fgvi;

    if (idio_S_unspec == sv) {
	idio_ai_t mci = idio_vm_constants_lookup_or_extend (symbol);
	kind = idio_S_computed;
	idio_ai_t gvi = idio_vm_extend_values ();
	fgvi = idio_fixnum (gvi);

	idio_hash_put (IDIO_MODULE_SYMBOLS (module), symbol, IDIO_LIST5 (kind, idio_fixnum (mci), fgvi, module, idio_string_C ("idio_module_add_computed_symbol")));
    } else {
	kind = IDIO_PAIR_H (sv);
	if (idio_S_computed != kind) {
	    idio_error_C ("computed variable already defined", IDIO_LIST2 (symbol, kind), IDIO_C_FUNC_LOCATION ());
	}
	fgvi = IDIO_PAIR_HTT (kind);
    }

    idio_vm_values_set (IDIO_FIXNUM_VAL (fgvi), idio_pair (get, set));

    return idio_S_unspec;
}

IDIO idio_module_export_computed_symbol (IDIO symbol, IDIO get, IDIO set, IDIO module)
{
    IDIO_MODULE_EXPORTS (module) = idio_pair (symbol, IDIO_MODULE_EXPORTS (module));
    return idio_module_add_computed_symbol (symbol, get, set, module);
}

void idio_init_module ()
{
    idio_modules_hash = IDIO_HASH_EQP (1<<4);
    idio_gc_protect (idio_modules_hash);

    /*
     * We need to pre-seed the Idio module with the names of these two
     * modules.
     *
     * So *primitives* is a symbol (and value) in the Idio module.  As
     * is Idio itself -- as a bootstrap for other people to use.
     *
     * The implied usage is that in idio_module() we set the default
     * imports list for all other modules to be '(Idio *primitives*)
     */
    IDIO name;

    name = idio_symbols_C_intern ("*primitives*");
    idio_primitives_module = idio_module (name);
    IDIO_MODULE_IMPORTS (idio_primitives_module) = idio_S_nil;
    idio_ai_t pm_gci = idio_vm_constants_lookup_or_extend (name);
    idio_ai_t pm_gvi = idio_vm_extend_values ();

    name = idio_symbols_C_intern ("Idio");
    idio_Idio_module = idio_module (name);
    IDIO_MODULE_IMPORTS (idio_Idio_module) = IDIO_LIST1 (IDIO_LIST1 (idio_primitives_module));
    idio_ai_t Im_gci = idio_vm_constants_lookup_or_extend (name);
    idio_ai_t Im_gvi = idio_vm_extend_values ();

    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_toplevel, idio_fixnum (pm_gci), idio_fixnum (pm_gvi), idio_Idio_module, idio_string_C ("idio_init_module")), idio_Idio_module);
    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_toplevel, idio_fixnum (Im_gci), idio_fixnum (Im_gvi), idio_Idio_module, idio_string_C ("idio_init_module")), idio_Idio_module);
}

void idio_module_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (find_or_create_module);
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
    IDIO_ADD_PRIMITIVE (find_symbol);
    IDIO_ADD_PRIMITIVE (symbol_value);
    IDIO_ADD_PRIMITIVE (symbol_value_recurse);
    IDIO_ADD_PRIMITIVE (set_symbol_value);
}

void idio_final_module ()
{
    idio_gc_expose (idio_modules_hash);
}

