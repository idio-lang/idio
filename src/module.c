/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "job-control.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static IDIO idio_modules_hash = idio_S_nil;

/*
 * idio_Idio_module is the default toplevel module
 *
 * All new modules implicitly import from idio_Idio_module.
 *
 * There is a little bit of artiface as getting and setting the
 * symbols referred to here is really an indirection to
 * idio_vm_values_(ref|set).
 *
 * idio_debugger_module doesn't really have a home but defining it in
 * condition.c, where it is used, seems wrong.
 */

IDIO idio_Idio_module = idio_S_nil;
IDIO idio_debugger_module = idio_S_nil;

static IDIO idio_module_direct_reference_string = idio_S_nil;
static IDIO idio_module_set_symbol_value_string = idio_S_nil;
static IDIO idio_module_add_computed_symbol_string = idio_S_nil;
static IDIO idio_module_init_string = idio_S_nil;

static void idio_module_base_name_error (IDIO msg, IDIO name, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO lsh;
    IDIO dsh;
    idio_error_init (NULL, &lsh, &dsh, c_location);

    idio_error_raise_cont (idio_condition_rt_module_error_type,
			   IDIO_LIST4 (msg,
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       name));

    /* notreached */
}

/*
 * Code coverage:
 *
 * Requires a coding error to trigger.
 */
static void idio_module_duplicate_name_error (IDIO name, IDIO c_location)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("module already exists", msh);

    idio_module_base_name_error (idio_get_output_string (msh), name, c_location);

    /* notreached */
}

static void idio_module_unbound_error (IDIO name, IDIO c_location)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("module name unbound", msh);

    idio_module_base_name_error (idio_get_output_string (msh), name, c_location);

    /* notreached */
}

/*
 * Code coverage:
 *
 * Requires a coding error to trigger.
 */
static void idio_module_unbound_name_error (IDIO symbol, IDIO module, IDIO c_location)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (module);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("symbol '", msh);
    idio_display (symbol, msh);
    idio_display_C ("' unbound in module ", msh);
    idio_display (IDIO_MODULE_NAME (module), msh);

    idio_error_raise_cont (idio_condition_rt_module_symbol_unbound_error_type,
			   IDIO_LIST5 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       module,
				       symbol));

    /* notreached */
}

IDIO idio_module (IDIO name)
{
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (symbol, name);

    IDIO m = idio_hash_ref (idio_modules_hash, name);

    if (idio_S_unspec != m) {
	/*
	 * Test Case:
	 *
	 * loading a shared library which creates its own module
	 * without a defensive check will trigger this
	 *
	 * How to translate that into a test case, though?
	 *
	 * load "empty"
	 * load "empty-dupe"
	 *
	 * ??
	 */
	idio_module_duplicate_name_error (name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO mo = idio_gc_get (IDIO_TYPE_MODULE);
    mo->vtable = idio_vtable (IDIO_TYPE_MODULE);

    IDIO_GC_ALLOC (mo->u.module, sizeof (idio_module_t));

    IDIO_MODULE_GREY (mo) = NULL;
    IDIO_MODULE_NAME (mo) = name;
    IDIO_MODULE_EXPORTS (mo) = idio_S_nil;
    /*
     * IDIO_MODULE_IMPORTS (module) will result in semi-gibberish for
     * any modules created before idio_job_control_module:
     *
     *   idio_libc_module because idio_job_control_module isn't
     *   defined at that point
     *
     *   idio_command_module because idio_job_control_module isn't
     *   defined at that point
     *
     *   idio_job_control_module because it imports itself?
     *
     * which we patch up immediately after this call returns.
     *
     * Which modules dun goofed depends on the order of module
     * initialisation in idio_init().
     */
    IDIO imports = IDIO_LIST1 (IDIO_LIST1 (idio_Idio_module));
    if (idio_S_nil != idio_job_control_module) {
	imports = idio_pair (IDIO_LIST1 (idio_job_control_module),
			     imports);
    }
    IDIO_MODULE_IMPORTS (mo) = imports;
    IDIO_MODULE_SYMBOLS (mo) = IDIO_HASH_EQP (1<<4);

    idio_hash_put (idio_modules_hash, name, mo);

    IDIO_MODULE_IDENTITY (mo) = idio_S_nil;

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

    IDIO_GC_FREE (mo->u.module, sizeof (idio_module_t));
}

IDIO idio_module_find_module (IDIO name)
{
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (symbol, name);

    return idio_hash_ref (idio_modules_hash, name);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("find-module", find_module, (IDIO name, IDIO args), "name [default]", "\
Find the module called `name`					\n\
								\n\
:param name: module name to look for				\n\
:type name: symbol						\n\
:param default: return value if `name` is not found		\n\
:type default: any, optional					\n\
:return: module called `name` or `default` (or ``#<unspec>``)	\n\
:rtype: module							\n\
:raises ^rt-module-error:					\n\
")
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (args);

    /*
     * Test Cases: module-errors/find-module-bad-name-type.idio
     *
     * find-module #t
     */
    if (! idio_isa_symbol (name)) {
	if (idio_isa_pair (args)) {
	    return IDIO_PAIR_H (args);
	} else {
	    idio_error_param_type ("symbol", name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_module_find_module (name);

    if (idio_S_unspec == r) {
	if (idio_isa_pair (args)) {
	    r = IDIO_PAIR_H (args);
	} else {
	    /*
	     * Test Case: module-errors/find-module-unbound.idio
	     *
	     * find-module (gensym)
	     */
	    idio_module_unbound_error (name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return r;
}

IDIO idio_module_alias (IDIO name, IDIO identity)
{
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (symbol, name);

    IDIO m = idio_module_find_module (name);

    if (idio_S_unspec != m) {
	/*
	 * Test Case: module-errors/module-alias-name-duplication.idio
	 *
	 * module-alias 'Idio
	 */
	idio_module_duplicate_name_error (name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    m = idio_module (name);

    IDIO_MODULE_EXPORTS (m) = IDIO_MODULE_EXPORTS (identity);
    IDIO_MODULE_IMPORTS (m) = IDIO_MODULE_IMPORTS (identity);
    IDIO_MODULE_SYMBOLS (m) = IDIO_MODULE_SYMBOLS (identity);
    IDIO_MODULE_IDENTITY (m) = identity;

    return m;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("module-alias", module_alias, (IDIO name, IDIO args), "name [identity]", "\
Create `name` as an alias for `identity` or the current module	\n\
if `identity` is not supplied.			\n\
						\n\
:param name: module alias' name			\n\
:type name: symbol				\n\
:param identity: target module			\n\
:type identity: module or symbol		\n\
:return: ``#<unspec>``				\n\
						\n\
.. warning::					\n\
						\n\
   If the `identity` module's symbols, exports or imports	\n\
   are unset when ``module-alias`` is called then changes	\n\
   will not be seen.				\n\
						\n\
   If you are aliasing yourself, use ``module-alias`` after	\n\
   the :ref:`provide <provide>` expression.			\n\
						\n\
.. seealso:: :ref:`module-identity <module-identity>`	\n\
")
{
    IDIO_ASSERT (name);

    /*
     * Test Case: module-errors/module-alias-bad-name-type.idio
     *
     * module-alias #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, name);

    IDIO identity = idio_thread_current_module ();
    if (idio_isa_pair (args)) {
	IDIO m_or_n = IDIO_PAIR_H (args);

	if (idio_isa_module (m_or_n)) {
	    identity = m_or_n;
	} else if (idio_isa_symbol (m_or_n)) {
	    identity = idio_hash_ref (idio_modules_hash, m_or_n);

	    if (idio_S_unspec == identity) {
		/*
		 * Test Case: module-errors/module-alias-identity-unbound.idio
		 *
		 * module-alias 'foo (gensym)
		 */
		idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: module-errors/module-alias-bad-identity-type.idio
	     *
	     * module-alias (gensym) #t
	     */
	    idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return idio_module_alias (name, identity);
}

IDIO_DEFINE_PRIMITIVE1_DS ("module-identity", module_identity, (IDIO m_or_n), "mod", "\
Return the identity of module `mod`		\n\
						\n\
:param mod: module				\n\
:type mod: module or symbol			\n\
:return: module's identity			\n\
:rtype: module or ``#n``			\n\
						\n\
If `mod` is an alias of another module then the identity	\n\
of `mod` is the module it is aliasing -- which could be		\n\
another alias.					\n\
						\n\
.. seealso:: :ref:`module-alias <module-alias>`	\n\
")
{
    IDIO_ASSERT (m_or_n);

    IDIO mod = idio_S_nil;

    if (idio_isa_module (m_or_n)) {
	mod = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	mod = idio_hash_ref (idio_modules_hash, m_or_n);

	if (idio_S_unspec == mod) {
	    /*
	     * Test Case: module-errors/module-identity-mod-unbound.idio
	     *
	     * module-identity (gensym)
	     */
	    idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: module-errors/module-identity-bad-mod-type.idio
	 *
	 * module-identity #t
	 */
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_MODULE_IDENTITY (mod);
}

IDIO idio_module_find_or_create_module (IDIO name)
{
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (symbol, name);

    IDIO m = idio_module_find_module (name);

    if (idio_S_unspec == m) {
	m = idio_module (name);
    }

    return m;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%find-or-create-module", find_or_create_module, (IDIO name), "name", "\
Find the module called `name` or create one	\n\
						\n\
:param name: module name to look for		\n\
:type name: symbol				\n\
:return: module called `name`			\n\
:rtype: module					\n\
")
{
    IDIO_ASSERT (name);

    /*
     * Test Case: module-errors/find-or-create-module-bad-name-type.idio
     *
     * %find-or-create-module #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, name);

    return idio_module_find_or_create_module (name);
}

IDIO_DEFINE_PRIMITIVE0_DS ("current-env", current_env, (), "", "\
Return the current env				\n\
						\n\
:return: current env				\n\
:rtype: module					\n\
")
{
    return idio_thread_current_env ();
}

IDIO_DEFINE_PRIMITIVE0_DS ("current-module", current_module, (), "", "\
Return the current module			\n\
						\n\
:return: current module				\n\
:rtype: module					\n\
")
{
    return idio_thread_current_module ();
}

IDIO_DEFINE_PRIMITIVE1_DS ("%set-current-module!", set_current_module, (IDIO module), "module", "\
Set the current module to `module`		\n\
						\n\
:param module: module to use			\n\
:type module: module				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (module);

    /*
     * Test Case: module-errors/set-current-module-bad-module-type.idio
     *
     * %set-current-module! #t
     */
    IDIO_USER_TYPE_ASSERT (module, module);

    idio_thread_set_current_module (module);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%set-module-imports!", set_module_imports, (IDIO module, IDIO imports), "module imports", "\
Set the imports of `module` to `imports`	\n\
						\n\
:param name: module to use			\n\
:type module: module				\n\
:param imports: import list to use		\n\
:type module: list				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (imports);

    /*
     * Test Case: module-errors/set-module-imports-bad-module-type.idio
     *
     * %set-module-imports! #t #t
     */
    IDIO_USER_TYPE_ASSERT (module, module);

    if (idio_isa_list (imports)) {
	IDIO_MODULE_IMPORTS (module) = imports;
    } else {
	/*
	 * Test Case: module-errors/set-module-imports-bad-imports-type.idio
	 *
	 * %set-module-imports! (find-module 'Idio) #t
	 */
	idio_error_param_type ("list|nil", imports, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%set-module-exports!", set_module_exports, (IDIO module, IDIO exports), "module exports", "\
Set the exports of `module` to `exports`	\n\
						\n\
:param name: module to use			\n\
:type module: module				\n\
:param exports: export list to use		\n\
:type module: list				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (exports);

    /*
     * Test Case: module-errors/set-module-exports-bad-module-type.idio
     *
     * %set-module-exports! #t #t
     */
    IDIO_USER_TYPE_ASSERT (module, module);

    if (idio_isa_list (exports)) {
	IDIO el = exports;
	while (idio_S_nil != el) {
	    IDIO e = IDIO_PAIR_H (el);
	    /*
	     * Test Case: module-errors/set-module-exports-bad-exports-arg-type.idio
	     *
	     * %set-module-exports! (find-module 'Idio) '(#t)
	     */
	    IDIO_USER_TYPE_ASSERT (symbol, e);
	    el = IDIO_PAIR_T (el);
	}
	IDIO_MODULE_EXPORTS (module) = exports;
    } else {
	/*
	 * Test Case: module-errors/set-module-exports-bad-exports-type.idio
	 *
	 * %set-module-exports! (find-module 'Idio) #t
	 */
	idio_error_param_type ("list|nil", exports, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("module?", modulep, (IDIO o), "o", "\
Is `o` a module?				\n\
						\n\
:param o: value to test				\n\
:type o: any					\n\
:return: ``#t`` if `o` is a module, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;
    if (idio_isa_module (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("module-name", module_name, (IDIO module), "module", "\
Return the name of `module`			\n\
						\n\
:param module: module to query			\n\
:type module: module				\n\
:return: module name				\n\
:rtype: symbol					\n\
")
{
    IDIO_ASSERT (module);

    /*
     * Test Case: module-errors/module-name-bad-module-type.idio
     *
     * module-name #t
     */
    IDIO_USER_TYPE_ASSERT (module, module);

    return IDIO_MODULE_NAME (module);
}

IDIO_DEFINE_PRIMITIVE1_DS ("module-imports", module_imports, (IDIO m_or_n), "mod", "\
Return the modules imported by `mod`			\n\
						\n\
:param mod: module to query			\n\
:type mod: module or name			\n\
:return: imported modules			\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (m_or_n);

    IDIO module = idio_S_unspec;

    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_ref (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    /*
	     * Test Case: module-errors/module-imports-unbound.idio
	     *
	     * module-imports (gensym)
	     */
	    idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: module-errors/module-imports-bad-type.idio
	 *
	 * module-imports #t
	 */
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return IDIO_MODULE_IMPORTS (module);
}

IDIO_DEFINE_PRIMITIVE1_DS ("module-exports", module_exports, (IDIO m_or_n), "mod", "\
Return the exported names of `mod`			\n\
						\n\
:param mod: module to query			\n\
:type mod: module or name			\n\
:return: exported names				\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (m_or_n);

    IDIO module = idio_S_unspec;

    if (idio_isa_module (m_or_n)) {
	module = m_or_n;
    } else if (idio_isa_symbol (m_or_n)) {
	module = idio_hash_ref (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    /*
	     * Test Case: module-errors/module-exports-unbound.idio
	     *
	     * module-exports (gensym)
	     */
	    idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: module-errors/module-exports-bad-type.idio
	 *
	 * module-exports #t
	 */
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
	/*
	 * Code coverage:
	 *
	 * The user interface handles this case.
	 */
	module = idio_hash_ref (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    /*
	     * Test Case: ??
	     *
	     * The user interface does this test leaving a coding
	     * error.
	     */
	    idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * The user interface does this test leaving a coding error.
	 */
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));
}

IDIO_DEFINE_PRIMITIVE0V_DS ("module-symbols", module_symbols, (IDIO args), "[module]", "\
return the symbols in `module`				\n\
							\n\
:param module: module to use, defaults to the current module	\n\
:type module: module or symbol, optional		\n\
:return: list of symbols				\n\
:rtype: list						\n\
")
{
    IDIO_ASSERT (args);

    IDIO module = idio_thread_current_module ();

    if (idio_isa_pair (args)) {
	IDIO m_or_n = IDIO_PAIR_H (args);

	if (idio_isa_module (m_or_n)) {
	    module = m_or_n;
	} else if (idio_isa_symbol (m_or_n)) {
	    module = idio_hash_ref (idio_modules_hash, m_or_n);

	    if (idio_S_unspec == module) {
		/*
		 * Test Case: module-errors/module-symbols-unbound.idio
		 *
		 * module-symbols (gensym)
		 */
		idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: module-errors/module-symbols-bad-type.idio
	     *
	     * module-symbols #t
	     */
	    idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
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
	    IDIO si = idio_hash_ref (IDIO_MODULE_SYMBOLS (module), sym);
	    if (idio_S_unspec == si) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_module_unbound_name_error (sym, module, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else if (! idio_isa_pair (si)) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_error_param_type ("pair", si, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else if (type == IDIO_SI_SCOPE (si)) {
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
    if (idio_Idio_module == module) {
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

/*
 * This is called from idio_command_get_envp() in command.c
 */
IDIO idio_module_visible_symbols (IDIO module, IDIO type)
{
    IDIO_ASSERT (module);
    IDIO_ASSERT (type);

    IDIO_TYPE_ASSERT (module, module);

    if (! idio_isa_module (module)) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_error_param_type ("module", module, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
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
	    /*
	     * Code coverage
	     *
	     * Requires a C unit test.
	     */
	    idio_eqp (im, idio_Idio_module)) {
	    seen_Idio = 1;
	}
    }

    if (0 == seen_Idio) {
	/*
	 * Code coverage
	 *
	 * Requires a C unit test.
	 */
	r = idio_list_append2 (r, idio_module_visible_imported_symbols (idio_Idio_module, type));
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0_DS ("all-modules", all_modules, (), "", "\
Return a list of all modules			\n\
						\n\
:return: all modules				\n\
:rtype: list					\n\
")
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

    IDIO r = idio_S_false;

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
	size_t name_len = IDIO_SYMBOL_BLEN (name);
	char *copy_C = idio_alloc (name_len + 1);
	memcpy (copy_C, name_C, name_len);
	copy_C[name_len] = '\0';

	slash = strrchr (copy_C, '/');

	if (NULL == slash) {
	    /*
	     * Test Case: ??
	     *
	     * Can we get here?  idio_alloc() failed?  strrchr()
	     * failed?
	     */
	    idio_coding_error_C ("failed to memcpy a string for strrchr", name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	*slash = '\0';


	size_t mlen = slash - copy_C;
	IDIO m_sym = idio_symbols_C_intern (copy_C, mlen);
	IDIO s_sym = idio_symbols_C_intern (slash + 1, name_len - mlen - 1);

	idio_free (copy_C);

	IDIO mod = idio_module_find_module (m_sym);

	if (idio_S_unspec != mod) {
	    IDIO f = idio_list_memq (s_sym, IDIO_MODULE_EXPORTS (mod));

	    if (idio_S_false != f) {
		IDIO si = idio_module_find_symbol (s_sym, mod);

		if (idio_S_false != si) {
		    /*
		     * Our result si should look similar to the result
		     * we've just found except we'll remark here that
		     * the xi, si and ci cannot be the same: the
		     * symbol M/S (in the caller) is not the same as
		     * the symbol S in the module this was found in.
		     */
		    r = IDIO_LIST3 (m_sym,
				    s_sym,
				    IDIO_LIST7 (IDIO_SI_SCOPE (si),
						IDIO_SI_XI (si),
						IDIO_SI_SI (si),
						IDIO_SI_CI (si),
						IDIO_SI_VI (si),
						mod,
						idio_module_direct_reference_string));
		}
	    }
	}
    }

    return r;
}

/*
 * Code coverage:
 *
 * We need to use evaluate.idio more!
 */
IDIO_DEFINE_PRIMITIVE1_DS ("symbol-direct-reference", symbol_direct_reference, (IDIO sym), "sym", "\
find evaluator details for symbol `sym`				\n\
								\n\
`sym` is of the form ``M/S`` (module/symbol)			\n\
								\n\
If `sym` is of the form ``M/S-M/S`` the result will be for	\n\
symbol ``S`` in the module ``M/S-M``.				\n\
								\n\
:param sym: symbol to find					\n\
:type sym: symbol						\n\
:return: evaluator details for `sym` or ``#f``			\n\
")
{
    IDIO_ASSERT (sym);

    /*
     * Test Case: module-errors/symbol-direct-reference-bad-type.idio
     *
     * symbol-direct-reference #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, sym);

    return idio_module_direct_reference (sym);
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

    IDIO si = idio_hash_ref (IDIO_MODULE_SYMBOLS (imported_module), symbol);

    /*
     * If it was a symbol in the given module then we still need to
     * check it was in the list of symbols exported from that module.
     *
     * That said, the main modules export everything.
     */
    if (idio_S_unspec != si) {
	if (idio_Idio_module == imported_module ||
	    idio_S_false != idio_list_memq (symbol, IDIO_MODULE_EXPORTS (imported_module))) {
	    return si;
	}
    }

    return idio_S_false;
}

/*
 * {recurse} is a tri-state variable:
 *
 * 0 - look in this module only -- see idio_module_find_symbol()
 * 1 - look in this module and imports
 * 2 - look in imports only
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
	module = idio_hash_ref (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    /*
	     * Test Case: module-errors/find-symbol-module-unbound.idio
	     *
	     * find-symbol 'read (gensym)
	     */
	    idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * find-symbol only passes a symbol|module as m_or_n meaning
	 * this clause is only fired by a coding error.
	 */
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO si_cm = idio_S_false;
    if (recurse < 2) {
	si_cm = idio_hash_ref (IDIO_MODULE_SYMBOLS (module), symbol);
	if (idio_S_unspec == si_cm) {
	    si_cm = idio_S_false;
	}
    }

    int found = 0;
    if (idio_S_false != si_cm) {
	found = 1;
	/*
	 * NB a vi of 0 indicates an unresolved value index to be
	 * resolved (based on the current set of imports) during
	 * runtime.
	 *
	 * Here, reset si_cm to #f (as though we had failed to find
	 * the symbol in our own module) and carry on.
	 */
	IDIO scope = IDIO_SI_SCOPE (si_cm);
	IDIO fgvi = IDIO_SI_VI (si_cm);
	idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);
	if (idio_S_toplevel == scope ||
	    idio_S_environ == scope ||
	    idio_S_dynamic == scope) {
	    if (0 == gvi) {
		if (recurse) {
		    found = 0;
		    /*
		    idio_debug ("imfsr skip %s ", symbol);
		    idio_debug ("%s\n", si_cm);
		    */
		}
	    }
	}
    }

    if (0 == found &&
	recurse) {
	IDIO imports = IDIO_MODULE_IMPORTS (module);
	for (; idio_S_nil != imports; imports = IDIO_PAIR_T (imports)) {
	    IDIO im = IDIO_PAIR_HH (imports);

	    if (! idio_isa_module (im)) {
		/*
		 * Code coverage
		 *
		 * I was bitten by this a couple of times (due to an
		 * ordering change in the code leading to a C value
		 * being unset).
		 *
		 * Given that the user can set a module's exports then
		 * this is a perfectly valid check.
		 *
		 * What does it mean to fail, though?
		 */
		idio_debug ("module import error: not a module: %s imports", module);
		idio_debug (" %s\n", IDIO_MODULE_IMPORTS (module));
		continue;
	    }
	    IDIO si_mi = idio_module_find_symbol_recurse_imports (symbol, im, module);

	    if (idio_S_false != si_mi) {
		/*
		 * Careful!  Same trick as above.
		 */
		IDIO fgvi = IDIO_SI_VI (si_mi);
		idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

		if (gvi > 0) {
		    found = 1;
		    if (idio_S_false == si_cm) {
			si_cm = si_mi;
			idio_module_set_symbol (symbol, si_cm, module);
			/*
			idio_debug ("imfsr copy %-10s ", IDIO_MODULE_NAME (module));
			idio_debug ("%-20s ", symbol);
			idio_debug ("== %s\n", si_cm);
			*/
		    } else {
			IDIO_SI_VI (si_cm) = IDIO_SI_VI (si_mi);
			/*
			idio_debug ("imfsr gvi  %s ", symbol);
			idio_debug ("%s\n", si_cm);
			*/
		    }
		    break;
		}
	    }
	}
    }

    if (0 == found) {
	IDIO mdr = idio_module_direct_reference (symbol);

	if (idio_S_false != mdr) {
	    /* (mod sym si) */
	    IDIO si = IDIO_PAIR_HTT (mdr);

	    IDIO fgvi = IDIO_SI_VI (si);
	    idio_as_t gvi = IDIO_FIXNUM_VAL (fgvi);

	    if (gvi) {
		si_cm = si;
		idio_module_set_symbol (symbol, si_cm, module);
		/*
		idio_debug ("imfsr mdr  %-10s ", IDIO_MODULE_NAME (module));
		idio_debug ("%-20s ", symbol);
		idio_debug ("== %s\n", si_cm);
		*/
	    }
	}
    }

    return si_cm;
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

IDIO_DEFINE_PRIMITIVE1V_DS ("find-symbol", find_symbol, (IDIO sym, IDIO args), "sym [mod [recurse]]", "\
find evaluator details for symbol `sym` in module `mod`		\n\
or the current environment if no `mod` supplied			\n\
								\n\
:param sym: symbol to find					\n\
:type sym: symbol						\n\
:param mod: module to search from, defaults to current module	\n\
:type mod: module or module name, optional			\n\
:param recurse: recurse into imported modules, defaults to ``#t``	\n\
:type recurse: recurse, optional				\n\
:return: evaluator details for `sym` or ``#f``			\n\
")
{
    IDIO_ASSERT (sym);
    IDIO_ASSERT (args);

    /*
     * Test Case: module-errors/find-symbol-bad-type.idio
     *
     * find-symbol #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, sym);

    IDIO m_or_n = idio_thread_current_module ();

    int recurse = 1;

    if (idio_isa_pair (args)) {
	IDIO m = IDIO_PAIR_H (args);
	if (idio_isa_module (m) ||
	    idio_isa_symbol (m)) {
	    m_or_n = m;
	}

	args = IDIO_PAIR_T (args);
	if (idio_isa_pair (args)) {
	    if (idio_S_false == IDIO_PAIR_H (args)) {
		recurse = 0;
	    }
	}
    }

    return idio_module_find_symbol_recurse (sym, m_or_n, recurse);
}

/*
 * idio_module_symbol_value_xi() will only look in the current module
 */
IDIO idio_module_symbol_value_xi (idio_xi_t xi, IDIO symbol, IDIO m_or_n, IDIO args)
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
	module = idio_hash_ref (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    /*
	     * Test Case: module-errors/symbol-value-module-unbound.idio
	     *
	     * symbol-value 'read (gensym)
	     */
	    idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: module-errors/symbol-value-bad-module-type.idio
	 *
	 * symbol-value 'read #t
	 */
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO si = idio_hash_ref (IDIO_MODULE_SYMBOLS (module), symbol);

    IDIO r = idio_S_unspec;
    if (idio_isa_pair (args)) {
	r = IDIO_PAIR_H (args);
    }

    if (idio_isa_pair (si)) {
	IDIO scope = IDIO_SI_SCOPE (si);
	IDIO fci = IDIO_SI_CI (si);
	IDIO fgvi = IDIO_SI_VI (si);
	idio_as_t gvi = IDIO_FIXNUM_VAL (fgvi);

	if (0 == gvi) {
	    /*
	     * Test case: ??
	     *
	     */
	    idio_module_unbound_name_error (symbol, module, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	if (idio_S_toplevel == scope) {
	    r = idio_vm_values_gref (0, gvi, "symbol-value/toplevel");
	} else if (idio_S_predef == scope) {
	    r = idio_vm_values_gref (0, gvi, "symbol-value/predef");
	} else if (idio_S_dynamic == scope) {
	    r = idio_vm_dynamic_ref (idio_thread_current_thread (), IDIO_FIXNUM_VAL (fci), gvi, args);
	} else if (idio_S_environ == scope) {
	    r = idio_vm_environ_ref (idio_thread_current_thread (), IDIO_FIXNUM_VAL (fci), gvi, args);
	} else if (idio_S_computed == scope) {
	    /*
	     * Code coverage
	     *
	     * Nobody looks one up, I guess.  I'm not sweating it.
	     */
	    r = idio_vm_computed_ref (xi, IDIO_FIXNUM_VAL (fci), gvi);
	} else {
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    idio_vm_error ("unexpected symbol scope", IDIO_LIST3 (symbol, module, si), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return r;
}

IDIO idio_module_symbol_value (IDIO symbol, IDIO m_or_n, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_xi (0, symbol, m_or_n, args);
}

IDIO idio_module_env_symbol_value (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value (symbol, idio_thread_current_env (), args);
}

/*
 * Only look in the given module.  If not present return the optional
 * default or error.
 */
IDIO_DEFINE_PRIMITIVE2V_DS ("symbol-value", symbol_value, (IDIO symbol, IDIO m_or_n, IDIO args), "sym mod [default]", "\
Return the value of `sym` in `mod` or `default` if supplied	\n\
						\n\
:param sym: symbol to query			\n\
:type sym: symbol				\n\
:param mod: module to query			\n\
:type mod: module or name			\n\
:param default: (optional) value to return if `sym` is not defined in `mod`	\n\
:type defined: any				\n\
:return: value or `default`			\n\
")
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (m_or_n);
    IDIO_ASSERT (args);

    /*
     * Test Case: module-errors/symbol-value-bad-symbol-type.idio
     *
     * symbol-value #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, symbol);
    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_xi (IDIO_THREAD_XI (idio_thread_current_thread ()), symbol, m_or_n, args);
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
	module = idio_hash_ref (idio_modules_hash, m_or_n);

	if (idio_S_unspec == module) {
	    /*
	     * Test Case: module-errors/symbol-value-recurse-module-unbound.idio
	     *
	     * symbol-value-recurse 'read (gensym)
	     */
	    idio_module_unbound_error (m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: module-errors/symbol-value-recurse-bad-module-type.idio
	 *
	 * symbol-value-recurse 'read #t
	 */
	idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO si = idio_module_find_symbol_recurse (symbol, module, 1);

    IDIO r = idio_S_unspec;

    if (idio_isa_pair (args)) {
	r = IDIO_PAIR_H (args);
    }

    if (idio_S_false != si) {
	IDIO scope = IDIO_SI_SCOPE (si);
	IDIO fci = IDIO_SI_CI (si);
	IDIO fgvi = IDIO_SI_VI (si);
	idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);

	IDIO thr = idio_thread_current_thread ();
	idio_xi_t xi = IDIO_THREAD_XI (thr);

	if (gvi) {
	    if (idio_S_toplevel == scope) {
		/*
		 * gvi is a global as we've just looked it up in si
		 */
		r = idio_vm_values_ref (0, gvi);
	    } else if (idio_S_predef == scope) {
		r = idio_vm_values_gref (xi, gvi, "symbol-value-recurse/predef");
	    } else if (idio_S_dynamic == scope) {
		/*
		 * Code coverage
		 *
		 * Nobody looks one up, I guess.  I'm not sweating it.
		 */
		r = idio_vm_dynamic_ref (thr, IDIO_FIXNUM_VAL (fci), gvi, args);
	    } else if (idio_S_environ == scope) {
		r = idio_vm_environ_ref (thr, IDIO_FIXNUM_VAL (fci), gvi, args);
	    } else if (idio_S_computed == scope) {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_vm_error ("cannot get a computed variable from C", IDIO_LIST3 (symbol, module, si), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		/*
		 * Test Case: ??
		 *
		 * Coding error.
		 */
		idio_vm_error ("unexpected symbol scope", IDIO_LIST3 (symbol, module, si), IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}
    }

    return r;
}

IDIO idio_module_current_symbol_value_recurse (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_recurse (symbol, idio_thread_current_module (), args);
}

IDIO idio_module_current_symbol_value_recurse_defined (IDIO symbol)
{
    IDIO_ASSERT (symbol);

    IDIO_TYPE_ASSERT (symbol, symbol);

    IDIO val = idio_module_symbol_value_recurse (symbol, idio_thread_current_module (), IDIO_LIST1 (idio_S_unspec));

    if (idio_S_unspec == val) {
	idio_error_param_undefined (symbol, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return val;
}

IDIO idio_module_env_symbol_value_recurse (IDIO symbol, IDIO args)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (list, args);

    return idio_module_symbol_value_recurse (symbol, idio_thread_current_env (), args);
}

/*
 * Recursively find starting at the given module (or the current
 * module if none given)
 */
IDIO_DEFINE_PRIMITIVE1V_DS ("symbol-value-recurse", symbol_value_recurse, (IDIO sym, IDIO args), "sym [mod [default]]", "\
recursively find symbol `sym` starting in module `mod` and return its		\n\
value or `default` if no value found					\n\
									\n\
:param sym: symbol to find						\n\
:type sym: symbol							\n\
:param mod: module to search from					\n\
:type mod: module or module name					\n\
:param default: value to return if `sym` is not found		\n\
:return: value of `sym`						\n\
")
{
    IDIO_ASSERT (sym);
    IDIO_ASSERT (args);

    /*
     * Test Case: module-errors/symbol-value-recurse-bad-symbol-type.idio
     *
     * symbol-value-recurse #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, sym);
    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO m_or_n = idio_thread_current_env ();

    if (idio_isa_pair (args)) {
	m_or_n = IDIO_PAIR_H (args);
	args = IDIO_PAIR_T (args);
    }

    return idio_module_symbol_value_recurse (sym, m_or_n, args);
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

/*
 * Code coverage:
 *
 * We need to use evaluate.idio more!
 */
IDIO_DEFINE_PRIMITIVE3_DS ("set-symbol!", set_symbol, (IDIO sym, IDIO v, IDIO mod), "sym v mod", "\
set the information associated with symbol `sym` in module `mod` to `v`		\n\
									\n\
:param sym: symbol to use						\n\
:type sym: symbol							\n\
:param v: tuple of value information					\n\
:type v: list								\n\
:param mod: module to search from					\n\
:type mod: module or module name					\n\
:return: `sym`							\n\
")
{
    IDIO_ASSERT (sym);
    IDIO_ASSERT (v);
    IDIO_ASSERT (mod);

    /*
     * Test Case: module-errors/set-symbol-bad-symbol-type.idio
     *
     * set-symbol! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, sym);
    /*
     * Test Case: module-errors/set-symbol-bad-v-type.idio
     *
     * set-symbol! 'read #t #t
     */
    IDIO_USER_TYPE_ASSERT (list, v);

    return idio_module_set_symbol (sym, v, mod);
}

IDIO idio_module_set_symbol_value_xi (idio_xi_t xi, IDIO symbol, IDIO value, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    IDIO si = idio_hash_ref (IDIO_MODULE_SYMBOLS (module), symbol);
    IDIO scope;
    IDIO fci;
    IDIO fgvi;

    if (idio_S_unspec == si) {
	/*
	 * C init code will get us here without having been through
	 * the evaluator.  These will be implicitly idio_S_toplevel
	 * symbols in this module.
	 */
	scope = idio_S_toplevel;
	IDIO si = idio_vm_extend_tables (xi, symbol, scope, module, idio_module_set_symbol_value_string);

	idio_hash_put (IDIO_MODULE_SYMBOLS (module),
		       symbol,
		       si);
	fci = IDIO_SI_CI (si);
	fgvi = IDIO_SI_VI (si);
    } else {
	scope = IDIO_SI_SCOPE (si);
	fci = IDIO_SI_CI (si);
	fgvi = IDIO_SI_VI (si);
    }

    idio_ai_t ci = IDIO_FIXNUM_VAL (fci);
    idio_ai_t gvi = IDIO_FIXNUM_VAL (fgvi);
    if (0 == gvi) {
	/*
	 * Again, C init code will have called idio_toplevel_extend()
	 * and then immediately set_symbol_value() which means the gvi
	 * remains 0.
	 */
	IDIO sym_idx = IDIO_SI_SI (si);
	
	gvi = idio_vm_extend_values (0);
	fgvi = idio_fixnum (gvi);
	if (xi) {
	    idio_vm_values_set (xi, IDIO_FIXNUM_VAL (sym_idx), fgvi);
	}
	IDIO_SI_VI (si) = fgvi;
    }

    if (idio_S_toplevel == scope) {
	/*
	 * gvi is a global as we've just looked it up in si
	 */
	idio_vm_values_set (0, gvi, value);
    } else if (idio_S_dynamic == scope) {
	/*
	 * Code coverage
	 *
	 * Nobody has set one, I guess.  I'm not sweating it.
	 */
	idio_vm_dynamic_set (idio_thread_current_thread (), ci, gvi, value);
    } else if (idio_S_environ == scope) {
	idio_vm_environ_set (idio_thread_current_thread (), ci, gvi, value);
    } else if (idio_S_computed == scope) {
	/*
	 * Code coverage
	 *
	 * Nobody has set one, I guess.  I'm not sweating it.
	 */
	idio_vm_computed_set (xi, ci, gvi, value);
    } else if (idio_S_predef == scope) {
	/*
	 * XXX
	 *
	 * We shouldn't be stamping over a primitive.  It's bad form.
	 *
	 * However the following operators do just that:
	 *
	 * = := :+ :* :~ :$
	 *
	 * The remaining operators are all closures.
	 *
	 * The path is:
	 *
	 *   idio_expander_add_primitives
	 *     IDIO_ADD_INFIX_OPERATOR
	 *       idio_add_infix_operator_primitive
	 *         idio_add_evaluation_primitive
	 *           idio_evaluator_extend
	 *             idio_module_set_symbol (predef, ..., "idio_evaluator_extend")
	 *         idio_install_infix_operator
	 *           idio_install_operator
	 *             -- here --
	 *
	 * In other words, idio_add_infix_operator_primitive first
	 * sets this up as a predef them immediately calls something
	 * that tries to overwrite it.
	 *
	 * This could do with being tagged more deliberately but we
	 * can live with it for now.
	 */
	if (module != idio_operator_module) {
	    IDIO cv = idio_vm_values_gref (xi,  gvi, "set-symbol-value");
	    idio_debug ("PRIM %s/", IDIO_MODULE_NAME (module));
	    idio_debug ("%s", symbol);
	    idio_debug (" => %s", value);
	    idio_debug (" was %s", cv);
	    idio_debug (" from %s\n", si);
	    idio_dump (cv, 2);
	    idio_dump (value, 2);
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    idio_vm_error ("cannot set a primitive", IDIO_LIST2 (symbol, module), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_vm_error ("unexpected symbol scope", IDIO_LIST3 (symbol, module, si), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return value;
}

IDIO idio_module_set_symbol_value (IDIO symbol, IDIO value, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    return idio_module_set_symbol_value_xi (IDIO_THREAD_XI (idio_thread_current_thread ()), symbol, value, module);
}

/*
 * A handy all-in-one for C init code
 */
IDIO idio_module_export_symbol_value (IDIO symbol, IDIO value, IDIO module)
{
    IDIO_MODULE_EXPORTS (module) = idio_pair (symbol, IDIO_MODULE_EXPORTS (module));
    return idio_module_set_symbol_value (symbol, value, module);
}

IDIO idio_module_env_set_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);

    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_set_symbol_value (symbol, value, idio_thread_current_env ());
}

/*
 * Code coverage:
 *
 * We need to use evaluate.idio more!
 */
IDIO_DEFINE_PRIMITIVE3_DS ("set-symbol-value!", set_symbol_value, (IDIO sym, IDIO value, IDIO module), "sym val mod", "\
set the value associated with `sym` in `mod` to `val`			\n\
									\n\
:param sym: symbol to use						\n\
:type sym: symbol							\n\
:param val: value							\n\
:type val: any								\n\
:param mod: module to locate `sym` in					\n\
:type mod: module							\n\
:return: `val`								\n\
")
{
    IDIO_ASSERT (sym);
    IDIO_ASSERT (value);
    IDIO_ASSERT (module);

    /*
     * Test Case: module-errors/set-symbol-value-bad-symbol-type.idio
     *
     * set-symbol-value! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (symbol, sym);
    /*
     * Test Case: module-errors/set-symbol-value-bad-module-type.idio
     *
     * set-symbol-value! 'read #t #t
     */
    IDIO_USER_TYPE_ASSERT (module, module);

    return idio_module_set_symbol_value (sym, value, module);
}

IDIO idio_module_add_computed_symbol (IDIO symbol, IDIO get, IDIO set, IDIO module)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (get);
    IDIO_ASSERT (set);
    IDIO_ASSERT (module);

    IDIO_TYPE_ASSERT (symbol, symbol);
    IDIO_TYPE_ASSERT (module, module);

    IDIO si = idio_hash_ref (IDIO_MODULE_SYMBOLS (module), symbol);
    IDIO scope;
    IDIO fgvi;

    IDIO thr = idio_thread_current_thread ();
    size_t xi = IDIO_THREAD_XI (thr);

    if (idio_S_unspec == si) {
	scope = idio_S_computed;
	IDIO sym_si = idio_vm_extend_tables (xi, symbol, scope, module, idio_module_add_computed_symbol_string);
	fgvi = IDIO_SI_VI (sym_si);

	idio_hash_put (IDIO_MODULE_SYMBOLS (module), symbol, sym_si);
    } else {
	scope = IDIO_SI_SCOPE (si);
	if (idio_S_computed != scope) {
	    /*
	     * Test Case: ??
	     *
	     * This function has no user interface so this will be a
	     * coding error.
	     *
	     * A handy one as redefining an existing symbol as a
	     * computed value doesn't work.
	     */
	    idio_vm_error ("computed variable already defined", IDIO_LIST2 (symbol, scope), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	fgvi = IDIO_PAIR_HTT (scope);
    }

    idio_vm_values_set (IDIO_THREAD_XI (thr), IDIO_FIXNUM_VAL (fgvi), idio_pair (get, set));

    return idio_S_unspec;
}

IDIO idio_module_export_computed_symbol (IDIO symbol, IDIO get, IDIO set, IDIO module)
{
    IDIO_MODULE_EXPORTS (module) = idio_pair (symbol, IDIO_MODULE_EXPORTS (module));
    return idio_module_add_computed_symbol (symbol, get, set, module);
}

#ifdef IDIO_DEBUG
IDIO_DEFINE_PRIMITIVE1_DS ("%dump-module", dump_module, (IDIO mo), "module", "\
print the internal details of `module`		\n\
						\n\
:param module: the module to dump		\n\
:type module: module				\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (mo);

    IDIO_USER_TYPE_ASSERT (module, mo);

    idio_debug ("Module %s\n", IDIO_MODULE_NAME (mo));
    idio_debug ("  exports: %s\n", IDIO_MODULE_EXPORTS (mo));
    idio_debug ("  imports: %s\n", IDIO_MODULE_IMPORTS (mo));
    idio_debug ("  symbols: %s\n", IDIO_MODULE_SYMBOLS (mo));
    if (idio_S_nil != IDIO_MODULE_IDENTITY (mo)) {
	idio_debug ("  identity: %s\n", IDIO_MODULE_NAME (IDIO_MODULE_IDENTITY (mo)));
    }

    return idio_S_unspec;
}
#endif

char *idio_module_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (module, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#<MOD ");

    if (idio_S_nil == IDIO_MODULE_NAME (v)) {
	/*
	 * Code coverage:
	 *
	 * Coding error?
	 */
	IDIO_STRCAT (r, sizep, "(nil)");
    } else {
	size_t mn_size = 0;
	char *mn = idio_as_string (IDIO_MODULE_NAME (v), &mn_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, mn, mn_size);
    }
    IDIO_STRCAT (r, sizep, ">");

    return r;
}

char *idio_module_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (module, v);

    char *r = NULL;

    *sizep = idio_asprintf (&r, "#<MOD ");

    if (idio_S_nil == IDIO_MODULE_NAME (v)) {
	/*
	 * Code coverage:
	 *
	 * Coding error?
	 */
	IDIO_STRCAT (r, sizep, "(nil)");
    } else {
	size_t mn_size = 0;
	char *mn = idio_as_string (IDIO_MODULE_NAME (v), &mn_size, 1, seen, 0);
	IDIO_STRCAT_FREE (r, sizep, mn, mn_size);
    }
    if (0 && depth > 0) {
	IDIO_STRCAT (r, sizep, " exports=");
	if (idio_S_nil == IDIO_MODULE_EXPORTS (v)) {
	    IDIO_STRCAT (r, sizep, "(nil)");
	} else {
	    size_t e_size = 0;
	    char *es = idio_as_string (IDIO_MODULE_EXPORTS (v), &e_size, depth - 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, es, e_size);
	}
	IDIO_STRCAT (r, sizep, " imports=");
	if (idio_S_nil == IDIO_MODULE_IMPORTS (v)) {
	    IDIO_STRCAT (r, sizep, "(nil)");
	} else {
	    size_t i_size = 0;
	    char *is = idio_as_string (IDIO_MODULE_IMPORTS (v), &i_size, 0, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, is, i_size);
	}
	IDIO_STRCAT (r, sizep, " symbols=");
	if (idio_S_nil == IDIO_MODULE_SYMBOLS (v)) {
	    IDIO_STRCAT (r, sizep, "(nil)");
	} else {
	    size_t s_size = 0;
	    char *ss = idio_as_string (IDIO_MODULE_SYMBOLS (v), &s_size, depth - 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, ss, s_size);
	}
	if (idio_S_nil != IDIO_MODULE_IDENTITY (v)) {
	    IDIO_STRCAT (r, sizep, " identity=");
	    size_t s_size = 0;
	    char *ss = idio_as_string (IDIO_MODULE_NAME (IDIO_MODULE_IDENTITY (v)), &s_size, depth - 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, ss, s_size);
	}
    }
    IDIO_STRCAT (r, sizep, ">");

    return r;
}

IDIO idio_module_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    IDIO seen = va_arg (ap, IDIO);
    int depth = va_arg (ap, int);
    va_end (ap);

    char *C_r = idio_module_as_C_string (v, sizep, 0, seen, depth);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_module_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (current_env);
    IDIO_ADD_PRIMITIVE (current_module);
    IDIO_ADD_PRIMITIVE (set_current_module);
    IDIO_ADD_PRIMITIVE (set_module_imports);
    IDIO_ADD_PRIMITIVE (set_module_exports);
    IDIO_ADD_PRIMITIVE (modulep);
    IDIO_ADD_PRIMITIVE (find_module);
    IDIO_ADD_PRIMITIVE (module_alias);
    IDIO_ADD_PRIMITIVE (module_identity);
    IDIO_ADD_PRIMITIVE (find_or_create_module);
    IDIO_ADD_PRIMITIVE (module_name);
    IDIO_ADD_PRIMITIVE (module_imports);
    IDIO_ADD_PRIMITIVE (module_exports);
    IDIO_ADD_PRIMITIVE (module_symbols);
    IDIO_ADD_PRIMITIVE (all_modules);

    IDIO_ADD_MODULE_PRIMITIVE (idio_evaluate_module, symbol_direct_reference);
    /*
     * find-symbol is used in doc.idio which can access it through
     * evaluate/find-symbol
     */
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_evaluate_module, find_symbol);
    IDIO_ADD_PRIMITIVE (symbol_value);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_evaluate_module, symbol_value_recurse);
    IDIO_ADD_MODULE_PRIMITIVE (idio_evaluate_module, set_symbol);

    /*
     * set-symbol-value! is used by path-functions.idio
     */
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_evaluate_module, set_symbol_value);

#ifdef IDIO_DEBUG
    IDIO_ADD_PRIMITIVE (dump_module);
#endif
}

void idio_final_module ()
{
    if (idio_vm_tables) {
	FILE *fp = fopen ("idio-vm-modules", "w");
	if (NULL == fp) {
	    perror ("fopen (idio-vm-modules, w)");
	    return;
	}

	fprintf (fp, " %5s %5s %-40s%.2s %5s %4s %-10s %s\n", "SI", "CI", "symbol", "Exported", "GVI", "XI", "module", "desc");

	IDIO module_names = idio_hash_keys_to_list (idio_modules_hash);
	while (idio_S_nil != module_names) {
	    IDIO module_name = IDIO_PAIR_H (module_names);
	    IDIO module = idio_hash_ref (idio_modules_hash, module_name);

	    IDIO symbols = idio_hash_keys_to_list (IDIO_MODULE_SYMBOLS (module));

	    idio_debug_FILE (fp, "\nModule %s", module_name);
	    fprintf (fp, " %zd symbols\n", idio_list_length (symbols));

	    while (idio_S_nil != symbols) {
		IDIO sym = IDIO_PAIR_H (symbols);
		IDIO si = idio_hash_ref (IDIO_MODULE_SYMBOLS (module), sym);

		idio_debug_FILE (fp, " %5s", IDIO_SI_SI (si));
		idio_debug_FILE (fp, " %5s", IDIO_SI_CI (si));
		idio_debug_FILE (fp, " %-40s", sym);
		if (idio_S_false != idio_list_memq (sym, IDIO_MODULE_EXPORTS (module))) {
		    fprintf (fp, "E ");
		} else {
		    fprintf (fp, "  ");
		}
		idio_debug_FILE (fp, " %5s", IDIO_SI_VI (si));
		idio_debug_FILE (fp, " %4s", IDIO_SI_XI (si));
		idio_debug_FILE (fp, " %-10s", IDIO_MODULE_NAME (IDIO_SI_MODULE (si)));
		idio_debug_FILE (fp, " %s", IDIO_SI_DESCRIPTION (si));
		fprintf (fp, "\n");

		symbols = IDIO_PAIR_T (symbols);
	    }

	    module_names = IDIO_PAIR_T (module_names);
	}
	fclose (fp);
    }
}

void idio_init_module ()
{
    idio_module_table_register (idio_module_add_primitives, idio_final_module, NULL);

    idio_modules_hash = IDIO_HASH_EQP (1<<4);
    idio_gc_protect_auto (idio_modules_hash);

#define IDIO_MODULE_STRING(c,s) idio_module_ ## c ## _string = idio_string_C (s); idio_gc_protect_auto (idio_module_ ## c ## _string);

    IDIO_MODULE_STRING (direct_reference,    "idio_module_direct_reference");
    IDIO_MODULE_STRING (set_symbol_value,    "idio_module_set_symbol_value");
    IDIO_MODULE_STRING (add_computed_symbol, "idio_module_add_computed_symbol");
    IDIO_MODULE_STRING (init,                "idio_module_init");

    /*
     * XXX Create the module vtable before creating any modules,
     * eg. Idio, otherwise the Idio->vtable will be NULL.
     */
    idio_vtable_t *m_vt = idio_vtable (IDIO_TYPE_MODULE);

    idio_vtable_add_method (m_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_module));

    idio_vtable_add_method (m_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_module_method_2string));

    /*
     * We need to pre-seed the Idio module with the names of these
     * modules.
     *
     * So Idio is a symbol (and value) in the Idio module as a
     * bootstrap for other people to use.
     *
     * The implied usage is that in idio_module() we set the default
     * imports list for all other modules to be '(Idio)
     */

    IDIO Iname = IDIO_SYMBOL ("Idio");
    idio_Idio_module = idio_module (Iname);
    IDIO_MODULE_IMPORTS (idio_Idio_module) = idio_S_nil;
    IDIO Im_si = idio_vm_extend_tables (0, Iname, idio_S_toplevel, idio_Idio_module, idio_module_init_string);

    idio_module_set_symbol (Iname, Im_si, idio_Idio_module);

    IDIO dname = IDIO_SYMBOL ("debugger");
    idio_debugger_module = idio_module (dname);
    IDIO dm_si = idio_vm_extend_tables (0, dname, idio_S_toplevel, idio_Idio_module, idio_module_init_string);

    idio_module_set_symbol (dname, dm_si, idio_Idio_module);
}
