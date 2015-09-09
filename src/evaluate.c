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
 * evaluate.c
 * 
 */

#include "idio.h"

/*
 * There are three layers of environment in which you might find a
 * variable.  idio_variable_kind is used to return an indication
 * as to what sort of variable it is and some useful detail about it.

 * For local variables this is split into two parts: names and values.
 * Names are recorded during processing to provide a fast index into a
 * table of values for execution.

 * For non-local variables we can keep track of names we have seen in
 * each module.  Each new name is mapped into the VM's table of
 * values.

 * Dynamic and environment variables are like regular names in modules
 * except we look them up differently (on the stack first)..
 
 * The order of lookup as we see a symbol is:
 
 * 1. local environments:
 
 *    these are a hierarchy of (flat) environments corresponing to a
 *    hierarchy of new scopes as new blocks are entered.  So we have
 *    lists of association lists of the names to (idio_S_local i j)
 *    where i is the ith association list and j is the jth variable in
 *    that frame
 
 *    During execution we will be creating matching activation frames
 *    accessible through the *env* register and i/j can be used to
 *    dereference through *env* to access the value.
 
 * 2. in symbols of the current module
 
 *    In the text these are denoted as toplevel names and are denoted
 *    as (kind i) where kind can be idio_S_toplevel, idio_S_dynamic or
 *    idio_S_environ and i is an index into the VM's table of known
 *    values.
 
 *    There is a subtlety between referencing a variable and setting
 *    it.  You can reference a variable that is in the exported list
 *    of symbols of a module you import.  However, you can only set a
 *    variable if it is in the current module.
 
 * 3. in the predefined primitive functions.
 
 *    (These are not accessible to mortals, can only be looked up by
 *    the evaluation engine and are read-only.)
 *
 *    These are created by the IDIO_ADD_PRIMITIVE macros which
 *    populate a normal list of names in the *primitives* and
 *    *operator* modules.
 *
 *    *primitives* for the usual culprits ("+", "string-append", etc.)
 *    and *operators* for operators (primarily because, say, the
 *    operator "-" is an obvious name clash with the regular
 *    subtraction primitive "-" and, in fact, is simply a transformer
 *    into that).
 
 *    As an aside, template expansion is done in idio_expander_thread
 *    which has idio_expander_module as its current-module --
 *    expansion shouldn't sully the namespace of the current thread.
 *    The list of known expanders is kept in the variable
 *    *expander-list* in idio_expander_module.
 
 * Modules Symbols and Compiling

 * The compiler, when referencing toplevel values, should only be
 * using integer indexes.  It maintains a simple table of those values
 * which is the amalgamation of all the individual modules' toplevel
 * values.
 
 * During compilation we need to maintain the artiface of modules when
 * we lookup symbols in which case the values of the symbols of each
 * module will be a mapping to the toplevel description (nominally the
 * pair (idio_S_toplevel . i) where i is in the index into the
 * compilers tables of all known toplevel values.
 *
 */

/*
 * Expanders (aka templates) live in their own little world...
 */

IDIO idio_expander_module = idio_S_nil;
static IDIO idio_expander_list = idio_S_nil;
static IDIO idio_expander_list_src = idio_S_nil;
static IDIO idio_expander_thread = idio_S_nil;

IDIO idio_operator_module = idio_S_nil;
static IDIO idio_infix_operator_list = idio_S_nil;
static IDIO idio_infix_operator_group = idio_S_nil;
static IDIO idio_postfix_operator_list = idio_S_nil;
static IDIO idio_postfix_operator_group = idio_S_nil;

static IDIO idio_evaluation_module = idio_S_nil;

static IDIO idio_initial_expander (IDIO x, IDIO e);
static IDIO idio_macro_expand (IDIO e);

static void idio_warning_static_undefineds (IDIO diff)
{
    IDIO_ASSERT (diff);
    IDIO_TYPE_ASSERT (pair, diff);

    idio_debug ("WARNING: undefined variables: %s\n", diff);
}

static void idio_static_error_redefine (char *msg, IDIO name, IDIO cv, IDIO new, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cv);
    IDIO_ASSERT (new);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, loc);
    
    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (msg, dsh);
    idio_display_C (": currently ", dsh);
    idio_display (cv, dsh);
    idio_display_C (": proposed: ", dsh);
    idio_display (new, dsh);
    IDIO c = idio_struct_instance (idio_condition_st_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh),
					       name));
    idio_raise_condition (idio_S_true, c);
}

static void idio_static_error_variable (char *msg, IDIO name, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, loc);
    
    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO c = idio_struct_instance (idio_condition_st_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_S_nil,
					       name));
    idio_raise_condition (idio_S_true, c);
}

static void idio_static_error_unbound (IDIO name, IDIO loc)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, loc);
    
    idio_static_error_variable ("unbound", name, loc);
}

static void idio_static_error_immutable (IDIO name, IDIO loc)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, loc);
    
    idio_static_error_variable ("immutable", name, loc);
}

static void idio_static_error_arity (char *msg, IDIO args, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (string, loc);
    
    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (args, dsh);

    IDIO c = idio_struct_instance (idio_condition_st_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh)));
    idio_raise_condition (idio_S_true, c);
}

static void idio_static_error_primitive_arity (char *msg, IDIO f, IDIO args, IDIO primdata, IDIO loc)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (symbol, f);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (primitive, primdata);
    IDIO_TYPE_ASSERT (string, loc);
    
    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C ("primitive '", dsh);
    idio_display (f, dsh);
    char em[BUFSIZ];
    sprintf (em, "' has arity %" PRId8 "%s; passed: ", IDIO_PRIMITIVE_ARITY (primdata), IDIO_PRIMITIVE_VARARGS (primdata) ? "+" : "");
    idio_display_C (em, dsh);
    idio_display (args, dsh);
    IDIO c = idio_struct_instance (idio_condition_st_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh)));
    idio_raise_condition (idio_S_true, c);
}

static IDIO idio_predef_extend (IDIO name, IDIO primdata, IDIO module)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (primdata);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (primitive, primdata);
    IDIO_TYPE_ASSERT (module, module);

    idio_debug ("predef-extend: %-20s", name);
    idio_debug (" in %s\n", module);

    IDIO cv = idio_module_symbol (name, module);
    if (idio_S_unspec != cv) {
	IDIO fgvi = IDIO_PAIR_HTT (cv);
	IDIO pd = idio_vm_values_ref (IDIO_FIXNUM_VAL (fgvi));

	if (IDIO_PRIMITIVE_F (primdata) != IDIO_PRIMITIVE_F (pd)) {
	    idio_debug ("WARNING: predef-extend: %s: ", name);
	    fprintf (stderr, "%p -> %p\n", IDIO_PRIMITIVE_F (pd), IDIO_PRIMITIVE_F (primdata));
	    /* idio_static_error_redefine ("primitive value change", name, pd, primdata); */
	} else {
	    return fgvi;
	}
    }

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);
    
    IDIO fgci = idio_module_vci_get_or_set (module, fmci);
    idio_module_vvi_set (module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST3 (idio_S_predef, fmci, fgvi), module);  

    /*
     * idio_module_set_symbol_value() is a bit sniffy about setting
     * predefs -- rightly so -- so go under the hood!
     */
    idio_vm_values_set (gvi, primdata);
    
    return fgvi;
}

IDIO idio_add_module_primitive (IDIO module, idio_primitive_desc_t *d)
{
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);

    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (sym);
    IDIO fmci = idio_fixnum (mci);
    /*
     * XXX fmci -> fmci mapping this assumes there's no C init
     * module-specific symbol index mapping conflicts...
     */
    idio_module_vci_set (module, fmci, fmci);

    return idio_predef_extend (sym, primdata, module);
}

IDIO idio_export_module_primitive (IDIO module, idio_primitive_desc_t *d)
{
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);

    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (sym);
    IDIO fmci = idio_fixnum (mci);
    /*
     * XXX fmci -> fmci mapping this assumes there's no C init
     * module-specific symbol index mapping conflicts...
     */
    idio_module_vci_set (module, fmci, fmci);

    IDIO_MODULE_EXPORTS (module) = idio_pair (sym, IDIO_MODULE_EXPORTS (module));
    return idio_predef_extend (sym, primdata, module);
}

IDIO idio_add_primitive (idio_primitive_desc_t *d)
{
    return idio_add_module_primitive (idio_primitive_module_instance (), d);
}

static void idio_install_expander_source (IDIO id, IDIO proc, IDIO code);

static IDIO idio_evaluator_extend (IDIO name, IDIO primdata, IDIO module)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (primdata);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (primitive, primdata);
    IDIO_TYPE_ASSERT (module, module);

    IDIO cv = idio_module_symbol (name, module);
    if (idio_S_unspec != cv) {
	IDIO fvi = IDIO_PAIR_HTT (cv);
	IDIO pd = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));

	if (IDIO_PRIMITIVE_F (primdata) != IDIO_PRIMITIVE_F (pd)) {
	    idio_static_error_redefine ("evaluator value change", name, pd, primdata, IDIO_C_LOCATION ("idio_evaluator_extend"));
	}
	
	return fvi;
    }

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);
    
    /*
     * XXX fmci -> fmci mapping this assumes there's no C init
     * module-specific symbol index mapping conflicts...
     */
    idio_module_vci_set (module, fmci, fmci);
    idio_module_vvi_set (module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST3 (idio_S_predef, fmci, fgvi), module);

    /*
     * idio_module_set_symbol_value() is a bit sniffy about setting
     * predefs -- rightly so -- so go under the hood!
     */
    idio_vm_values_set (gvi, primdata);
    
    return fgvi;
}

IDIO idio_add_evaluation_primitive (idio_primitive_desc_t *d, IDIO module)
{
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);

    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);
    return idio_evaluator_extend (sym, primdata, module);
}

void idio_add_expander_primitive (idio_primitive_desc_t *d)
{
    idio_add_primitive (d); 
    IDIO primdata = idio_primitive_data (d);
    idio_install_expander_source (idio_symbols_C_intern (d->name), primdata, primdata);
}

void idio_add_infix_operator_primitive (idio_primitive_desc_t *d, int pri)
{
    idio_add_evaluation_primitive (d, idio_operator_module); 
    IDIO primdata = idio_primitive_data (d);
    idio_install_infix_operator (idio_symbols_C_intern (d->name), primdata, pri);
}

void idio_add_postfix_operator_primitive (idio_primitive_desc_t *d, int pri)
{
    idio_add_evaluation_primitive (d, idio_operator_module); 
    IDIO primdata = idio_primitive_data (d);
    idio_install_postfix_operator (idio_symbols_C_intern (d->name), primdata, pri);
}

IDIO idio_toplevel_extend (IDIO name, int flags)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO kind;
    switch (IDIO_MEANING_SCOPE (flags)) {
    case IDIO_MEANING_FLAG_LEXICAL_SCOPE:
	kind = idio_S_toplevel;
	break;
    case IDIO_MEANING_FLAG_DYNAMIC_SCOPE:
	kind = idio_S_dynamic;
	break;
    case IDIO_MEANING_FLAG_ENVIRON_SCOPE:
	kind = idio_S_environ;
	break;
    case IDIO_MEANING_FLAG_COMPUTED_SCOPE:
	kind = idio_S_computed;
	break;
    default:
	idio_error_printf (IDIO_C_LOCATION ("toplevel-extend"), "unexpected toplevel variable scope %#x", flags);
	return idio_S_unspec;
    }
    
    IDIO cv = idio_module_symbol (name, idio_evaluation_module);
    if (idio_S_unspec != cv) {
	IDIO curkind = IDIO_PAIR_H (cv);
	if (kind != curkind) {
	    if (idio_S_predef != curkind) {
		idio_static_error_redefine ("toplevel-extend: type change", name, cv, kind, IDIO_C_LOCATION ("idio_toplevel_extend"));
	    }
	} else {
	    return IDIO_PAIR_HT (cv);
	}
    }

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    /*
     * NB a vi of 0 indicates an unresolved value index to be resolved
     * (based on the current set of imports) during runtime
     */
    idio_module_set_symbol (name, IDIO_LIST3 (kind, fmci, idio_fixnum (0)), idio_evaluation_module);
    
    return fmci;
}

IDIO idio_environ_extend (IDIO name, IDIO val)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO im = idio_Idio_module_instance ();
    
    IDIO sk = idio_module_symbol (name, im);
    if (idio_S_unspec != sk) {
	IDIO kind = IDIO_PAIR_H (sk);
	IDIO fmci = IDIO_PAIR_HT (sk);

	if (idio_S_environ != kind) {
	    idio_static_error_redefine ("environ-extend: type change", name, sk, kind, IDIO_C_LOCATION ("idio_environ_extend"));
	} else {
	    return fmci;
	}
    }

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);
    
    /*
     * XXX fmci -> fmci mapping this assumes there's no C init
     * module-specific symbol index mapping conflicts...
     */
    idio_module_vci_set (im, fmci, fmci);
    idio_module_vvi_set (im, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST3 (idio_S_environ, fmci, fgvi), im);
    idio_module_set_symbol_value (name, val, im);
    
    return fmci;
}

static IDIO idio_variable_localp (IDIO nametree, size_t i, IDIO name)
{
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);

    if (idio_isa_pair (nametree)) {
	IDIO names = IDIO_PAIR_H (nametree);
	for (;;) {
	    if (idio_isa_pair (names)) {
		IDIO assq = idio_list_assq (name, names);
		if (idio_S_false != assq) {
		    IDIO kind = IDIO_PAIR_HT (assq);
		    if (idio_S_local == kind) {
			return IDIO_LIST3 (kind, idio_fixnum (i), IDIO_PAIR_HTT (assq));
		    } else if (idio_S_dynamic == kind ||
			       idio_S_environ == kind) {
			return IDIO_PAIR_T (assq);
		    } else {
			idio_error_C ("unexpected local variant", IDIO_LIST2 (name, nametree), IDIO_C_LOCATION ("idio_variable_localp"));
			return idio_S_unspec;
		    }
		} else {
		    names = IDIO_PAIR_T (names);
		}
	    } else if (idio_S_nil == names) {
		nametree = IDIO_PAIR_T (nametree);

		if (idio_S_nil == nametree) {
		    return idio_S_nil;
		}
		
		IDIO_TYPE_ASSERT (pair, nametree);
		
		names = IDIO_PAIR_H (nametree);
		i++;
	    } else {
		idio_error_C ("unexpected localp", IDIO_LIST2 (name, nametree), IDIO_C_LOCATION ("idio_variable_localp"));
		if (idio_eqp (name, names)) {
		    return IDIO_LIST3 (idio_S_local, idio_fixnum (i), idio_fixnum (0));
		} else {
		    return idio_S_nil;
		}
	    }
	}
    }

    return idio_S_nil;
}

static IDIO idio_nametree_extend (IDIO nametree, IDIO names)
{
    IDIO_ASSERT (names);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, names);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO alist = idio_S_nil;
    idio_ai_t i = 0;
    
    while (idio_S_nil != names) {
	alist = idio_pair (IDIO_LIST3 (IDIO_PAIR_H (names), idio_S_local, idio_fixnum (i++)), alist);
	names = IDIO_PAIR_T (names);
    }

    alist = idio_list_reverse (alist);

    return idio_pair (alist, nametree);
}

static IDIO idio_nametree_dynamic_extend (IDIO nametree, IDIO name, IDIO index, IDIO kind)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (index);
    IDIO_ASSERT (kind);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (fixnum, index);

    return idio_pair (IDIO_LIST1 (IDIO_LIST3 (name, kind, index)), nametree);
}

static IDIO idio_variable_kind (IDIO nametree, IDIO name, int flags)
{
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO r = idio_variable_localp (nametree, 0, name);
    
    if (idio_S_nil == r) {
	/*
	 * NOTICE This must be a recursive lookup.  Otherwise we'll
	 * not see any bindings in Idio or *primitives*
	 */
        r = idio_module_symbol_recurse (name, idio_evaluation_module, 1);
	if (idio_S_unspec == r) {

	    r = idio_module_direct_reference (name);

	    if (idio_S_unspec != r) {
		r = IDIO_PAIR_HTT (r);
		idio_module_set_symbol (name, r, idio_evaluation_module);
	    } else {
		/*
		 * auto-extend the toplevel with this unknown variable
		 * -- we should (eventually) see a definition for it
		 */
		idio_toplevel_extend (name, flags);
		r = idio_module_symbol (name, idio_evaluation_module);
		idio_debug ("i_var_kind: %-20s auto toplevel extend", name);
		idio_debug (" => %s\n", r);
	    }
	}
    }

    return r;
}

static IDIO idio_evaluate_expander_source (IDIO x, IDIO e)
{
    IDIO_ASSERT (x);
    IDIO_ASSERT (e);

    /* idio_debug ("evaluate-expander: in: %s\n", x);  */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_initial_expander (x, e);
    IDIO r = idio_vm_run (ethr);
    
    idio_thread_restore_state (ethr);
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-expander: out: %s\n", r);     */

    /* if (idio_S_nil == r) { */
    /* 	fprintf (stderr, "evaluate-expander: bad expansion?\n"); */
    /* } */
    
    return r;
}

/*
 * Poor man's let:
 *
 * 1. (let bindings body)
 * 2. (let name bindings body)
 *
 * =>
 *
 * 1. (apply (lambda (map car bindings) body) (map cadr bindings))
 * 2. (apply (letrec ((name (lambda (map car bindings) body))) (map cadr bindings)))
 */

IDIO_DEFINE_PRIMITIVE1 ("let", let, (IDIO e))
{
    IDIO_ASSERT (e);
    IDIO_TYPE_ASSERT (list, e);

    size_t nargs = idio_list_length (e);

    if (nargs < 3) {
	idio_static_error_arity ("let: wrong arguments", e, IDIO_C_LOCATION ("let"));
	return idio_S_unspec;
    }

    e = IDIO_PAIR_T (e);

    IDIO bindings = IDIO_PAIR_H (e);
    IDIO vars = idio_S_nil;
    IDIO vals = idio_S_nil;
    IDIO name = idio_S_nil;
    if (idio_isa_symbol (bindings)) {
	name = bindings;
	e = IDIO_PAIR_T (e);
	bindings = IDIO_PAIR_H (e);
    }
    
    while (idio_S_nil != bindings) {
	IDIO binding = IDIO_PAIR_H (bindings);
	IDIO_TYPE_ASSERT (pair, bindings);
	vars = idio_pair (IDIO_PAIR_H (binding), vars);
	vals = idio_pair (IDIO_PAIR_HT (binding), vals);
	
	bindings = IDIO_PAIR_T (bindings);
    }

    /*
     * e is currently a list, either (body) or (body ...)
     *
     * body could be a single expression in which case we want the
     * head of e (otherwise we will attempt to apply the result of
     * body) or multiple expressions in which case we want to prefix e
     * with begin
     *
     * it could be nil too...
     */

    if (idio_S_nil != e) {
	e = IDIO_PAIR_T (e);
	if (idio_S_nil == IDIO_PAIR_T (e)) {
	    e = IDIO_PAIR_H (e);
	} else {
	    e = idio_list_append2 (IDIO_LIST1 (idio_S_begin), e);
	}
    }
    
    IDIO fn;

    if (idio_S_nil == name) {
	fn = IDIO_LIST3 (idio_S_lambda, idio_list_reverse (vars), e);

	return idio_list_append2 (IDIO_LIST1 (fn), idio_list_reverse (vals));
    } else {
	fn = IDIO_LIST3 (idio_S_letrec,
			 IDIO_LIST1 (IDIO_LIST2 (name,
						 IDIO_LIST3 (idio_S_lambda, idio_list_reverse (vars), e))),
			 idio_list_append2 (IDIO_LIST1 (name), idio_list_reverse (vals)));

	return fn;
    }
}

/*
 * Poor man's let*:
 *
 * (let bindings body)
 *
 * =>
 *
 * (apply (lambda (map car bindings) body) (map cdr bindings))
 */

IDIO_DEFINE_PRIMITIVE1 ("let*", lets, (IDIO e))
{
    IDIO_ASSERT (e);
    IDIO_TYPE_ASSERT (list, e);

    size_t nargs = idio_list_length (e);

    if (nargs < 3) {
	idio_static_error_arity ("let*: wrong arguments", e, IDIO_C_LOCATION ("let*"));
    }

    /* idio_debug ("let*: in %s\n", e); */
    
    e = IDIO_PAIR_T (e);

    IDIO bindings = idio_list_reverse (IDIO_PAIR_H (e));

    /*
     * e is currently a list, either (body) or (body ...)
     *
     * body could be a single expression in which case we want the
     * head of e (otherwise we will attempt to apply the result of
     * body) or multiple expressions in which case we want to prefix e
     * with begin
     *
     * it could be nil too...
     */

    if (idio_S_nil != e) {
	e = IDIO_PAIR_T (e);
	if (idio_S_nil == IDIO_PAIR_T (e)) {
	    e = IDIO_PAIR_H (e);
	} else {
	    e = idio_list_append2 (IDIO_LIST1 (idio_S_begin), e);
	}
    }
    
    IDIO lets = e;
    while (idio_S_nil != bindings) {
	lets = IDIO_LIST3 (idio_S_let,
			   IDIO_LIST1 (IDIO_PAIR_H (bindings)),
			   lets);
	bindings = IDIO_PAIR_T (bindings);
    }

    /* idio_debug ("let*: out %s\n", lets); */
    
    return lets;
}

/*
 * Poor man's letrec:
 *
 * (letrec bindings body)
 *
 * =>
 *
 * (apply (lambda (map car bindings) body) (map cdr bindings))
 */

IDIO_DEFINE_PRIMITIVE1 ("letrec", letrec, (IDIO e))
{
    IDIO_ASSERT (e);
    IDIO_TYPE_ASSERT (list, e);

    size_t nargs = idio_list_length (e);

    if (nargs < 3) {
	idio_static_error_arity ("letrec: wrong arguments", e, IDIO_C_LOCATION ("letrec"));
	return idio_S_unspec;
    }

    e = IDIO_PAIR_T (e);

    IDIO bindings = IDIO_PAIR_H (e);
    IDIO vars = idio_S_nil;
    IDIO tmps = idio_S_nil;
    IDIO vals = idio_S_nil;
    while (idio_S_nil != bindings) {
	IDIO binding = IDIO_PAIR_H (bindings);
	IDIO_TYPE_ASSERT (pair, bindings);
	vars = idio_pair (IDIO_PAIR_H (binding), vars);
	tmps = idio_pair (idio_gensym (NULL), tmps);
	vals = idio_pair (IDIO_PAIR_HT (binding), vals);
	
	bindings = IDIO_PAIR_T (bindings);
    }

    /*
     * e is currently a list, either (body) or (body ...)
     *
     * body could be a single expression in which case we want the
     * head of e (otherwise we will attempt to apply the result of
     * body) or multiple expressions in which case we want to prefix e
     * with begin
     *
     * it could be nil too...
     */

    if (idio_S_nil != e) {
	e = IDIO_PAIR_T (e);
	/* if (idio_S_nil == IDIO_PAIR_T (e)) { */
	/*     e = IDIO_PAIR_H (e); */
	/* } else { */
	/*     e = idio_list_append2 (IDIO_LIST1 (idio_S_begin), e); */
	/* } */
    }

    vars = idio_list_reverse (vars);
    tmps = idio_list_reverse (tmps);
    vals = idio_list_reverse (vals);
    
    IDIO ri = idio_S_nil;	/* init vars to #f */
    IDIO rt = idio_S_nil;	/* set tmps (in context of vars) */
    IDIO rs = idio_S_nil;	/* set vars */
    IDIO ns = vars;
    IDIO ts = tmps;
    IDIO vs = vals;
    while (idio_S_nil != ns) {
	ri = idio_pair (IDIO_LIST2 (IDIO_PAIR_H (ns), idio_S_false), ri);
	rt = idio_pair (IDIO_LIST2 (IDIO_PAIR_H (ts), IDIO_PAIR_H (vs)), rt);
	rs = idio_pair (IDIO_LIST3 (idio_S_set, IDIO_PAIR_H (ns), IDIO_PAIR_H (ts)), rs);
	ns = IDIO_PAIR_T (ns);
	ts = IDIO_PAIR_T (ts);
	vs = IDIO_PAIR_T (vs);
    }
    ri = idio_list_reverse (ri);
    rt = idio_list_reverse (rt);
    rs = idio_list_reverse (rs);
    IDIO r = idio_list_append2 (idio_list_reverse (rs), e);
    r = IDIO_LIST3 (idio_S_let,
		    ri,
		    IDIO_LIST3 (idio_S_let,
				rt,
				idio_list_append2 (IDIO_LIST1 (idio_S_begin),
						   idio_list_append2 (rs, e))));
    
    return r;
}

static IDIO idio_expanderp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO expander_list = idio_module_symbol_value (idio_expander_list, idio_expander_module);
    /* fprintf (stderr, "expander?: %p in %p: *expander-list* = %p\n", idio_expander_list, idio_expander_module, expander_list);   */
    /* idio_dump (expander_list, 1);   */
    
    IDIO assq = idio_list_assq (name, expander_list);

    /* idio_debug ("expander?: assq %s\n", assq);  */

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    /* idio_debug ("expander?: %s isa PAIR\n", name);   */
	    /* idio_debug ("expander?: %s\n", v); */

	    IDIO lv = idio_module_current_symbol_value_recurse (name);
	    /* idio_debug ("expander?: lookup/curr -> %s\n", lv); */
	    /* idio_debug ("/curr %s\n", idio_thread_current_module ()); */
	    if (idio_isa_primitive (lv) ||
		idio_isa_closure (lv)) {
		IDIO_PAIR_T (assq) = lv;
	    } else {
		idio_debug ("expander?: %s not an expander?\n", name);
	    }
	} else {
	    /* fprintf (stderr, "expander?: isa %s\n", idio_type2string (v));  */
	}
    }

    return assq;
}

IDIO_DEFINE_PRIMITIVE1 ("expander?", expanderp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_expanderp (o)) {
	r = idio_S_true;
    }

    return r;
}

static IDIO idio_application_expander (IDIO x, IDIO e)
{
    IDIO_ASSERT (x);
    IDIO_ASSERT (e);

    /*
     * (application-expander x e)
     * =>
     * (map* (function (y) (e y e)) x)
     *
     * map* is:
     */

    /* idio_debug ("application-expander: in %s\n", x); */

    IDIO r = idio_S_nil;
    
    IDIO xh = IDIO_PAIR_H (x);
    if (idio_S_nil == xh) {
	return idio_S_nil;
    } else if (idio_isa_pair (xh)) {
	IDIO mcar = idio_list_mapcar (x);
	IDIO mcdr = idio_list_mapcdr (x);

	/* idio_debug ("application-expander: mcar=%s", mcar); */
	/* idio_debug (" mcdr=%s\n", mcdr); */

	if (idio_S_false == e) {
	    r = idio_pair (mcar,
			   idio_application_expander (mcdr, e));
	} else {
	    r = idio_pair (idio_initial_expander (mcar, e),
			   idio_application_expander (mcdr, e));
	}
    } else {
	/* fprintf (stderr, "application-expander: else\n"); */
	if (idio_S_false == e) {
	    r = idio_pair (x, r);
	} else {
	    r = idio_pair (idio_initial_expander (x, e), r);
	}
    }

    /* idio_debug ("application-expander: r=%s\n", r); */
    
    return r;
}

static IDIO idio_initial_expander (IDIO x, IDIO e)
{
    IDIO_ASSERT (x);
    IDIO_ASSERT (e);

    /* idio_debug ("initial-expander: %s", x); */
    /* idio_debug (" %s\n", e); */

    if (! idio_isa_pair (x)) {
	return x;
    }

    IDIO xh = IDIO_PAIR_H (x);

    if (! idio_isa_symbol (xh)) {
	/* fprintf (stderr, "initial-expander: nota SYMBOL\n"); */
	return idio_application_expander (x, e);
    } else {
	IDIO expander = idio_expanderp (xh);
	if (idio_S_false != expander) {
	    /*
	     * apply the macro!
	     *
	     * ((cdr (assq functor *expander-list*)) x e)
	     */
	    /* fprintf (stderr, "initial-expander: isa EXPANDER\n"); */
	    return idio_apply (IDIO_PAIR_T (expander), IDIO_LIST3 (x, e, idio_S_nil));
	} else {
	    /* fprintf (stderr, "initial-expander: nota EXPANDER\n"); */
	    return idio_application_expander (x, e);
	}
    }
}

void idio_install_expander (IDIO id, IDIO proc)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);
    
    IDIO el = idio_module_symbol_value (idio_expander_list, idio_expander_module);
    IDIO old = idio_list_assq (id, el);

    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_expander_list,
				      idio_pair (idio_pair (id, proc),
						 el),
				      idio_expander_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

static void idio_install_expander_source (IDIO id, IDIO proc, IDIO code)
{
    /* idio_debug ("install-expander-source: %s\n", id); */

    idio_install_expander (id, proc);

    IDIO els = idio_module_symbol_value (idio_expander_list_src, idio_expander_module);
    IDIO old = idio_list_assq (id, els);
    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_expander_list_src,
				      idio_pair (idio_pair (id, code),
						 els),
				      idio_expander_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

static IDIO idio_evaluate_expander_code (IDIO m)
{
    IDIO_ASSERT (m);

    /* idio_debug ("evaluate-expander-code: %s\n", m); */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_vm_codegen (ethr, m);
    IDIO r = idio_vm_run (ethr);
    
    idio_thread_restore_state (ethr);
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-expander-code: out: %s\n", r);     */

    return r;
}

/* static void idio_push_expander (IDIO id, IDIO proc) */
/* { */
/*     idio_module_set_symbol_value (idio_expander_list, */
/* 				  idio_pair (idio_pair (id, proc), */
/* 					     idio_module_symbol_value (idio_expander_list, idio_expander_module)), */
/* 				  idio_expander_module); */
/* } */

/* static void idio_delete_expander (IDIO id) */
/* { */
/*     IDIO el = idio_module_symbol_value (idio_expander_list, idio_expander_module); */
/*     IDIO prv = idio_S_false; */

/*     for (;;) { */
/* 	if (idio_S_nil == el) { */
/* 	    return; */
/* 	} else if (idio_eqp (IDIO_PAIR_HH (el)), id)) { */
/* 	    if (idio_S_false == prv) { */
/* 		idio_module_set_symbol_value (idio_expander_list, */
/* 					      IDIO_PAIR_T (el), */
/* 					      idio_expander_module); */
/* 		return; */
/* 	    } else { */
/* 		IDIO_PAIR_T (prv) = IDIO_PAIR_T (el); */
/* 		return; */
/* 	    } */
/* 	} */
/* 	prv = el; */
/* 	el = IDIO_PAIR_T (el); */
/*     } */

/*     IDIO_C_ASSERT (0); */
/*     return; */
/* } */

static IDIO idio_macro_expand (IDIO e)
{
    IDIO_ASSERT (e);

    /* fprintf (stderr, "macro-expand: %zd args in ", idio_list_length (e));    */
    /* idio_debug (" %s\n", e);    */
    
    IDIO r = idio_evaluate_expander_source (e, idio_S_unspec);
    /* idio_debug ("me r %s\n", r);  */
    return r;
}

IDIO_DEFINE_PRIMITIVE0V ("macro-expand", macro_expand, (IDIO x))
{
    IDIO_ASSERT (x);

    /* idio_debug ("primitive-macro-expand: %s\n", x); */
    return idio_macro_expand (x);
}

static IDIO idio_macro_expands (IDIO e)
{
    IDIO_ASSERT (e);

    for (;;) {
	/* fprintf (stderr, "macro-expand*: e: %zd args in", idio_list_length (e));  */
	/* idio_debug (" %s\n", e);  */

	IDIO new = idio_evaluate_expander_source (e, idio_S_false);
	/* idio_debug ("macro-expand*: new: %s\n", new); */
	
	if (idio_equalp (new, e)) {
	    /* fprintf (stderr, "macro-expand*: equal\n"); */
	    return new;
	} else {
	    /* fprintf (stderr, "macro-expand*: recurse\n"); */
	    e = new;
	}
    }
}

void idio_install_infix_operator (IDIO id, IDIO proc, int pri)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);
    
    /* idio_debug ("op install %s", id); */
    /* idio_debug (" as %s\n", proc); */

    if (idio_S_undef == proc) {
	IDIO_C_ASSERT (0);
    }
    
    idio_module_set_symbol_value (id, proc, idio_operator_module);

    IDIO ol = idio_module_symbol_value (idio_infix_operator_list, idio_operator_module);
    IDIO op = idio_list_assq (id, ol);

    if (idio_S_false == op) {
	idio_module_set_symbol_value (idio_infix_operator_list, 
				      idio_pair (idio_pair (id, proc),
						 ol), 
				      idio_operator_module); 
    } else {
	IDIO_PAIR_T (op) = proc;
    }

    IDIO og = idio_module_symbol_value (idio_infix_operator_group, idio_operator_module);

    IDIO fpri = idio_fixnum (pri);
    IDIO grp = idio_list_assq (fpri, og);

    if (idio_S_false == grp) {
	grp = IDIO_LIST1 (idio_pair (id, proc));

	if (idio_S_nil == og) {
	    idio_module_set_symbol_value (idio_infix_operator_group,
					  idio_pair (idio_pair (fpri, grp),
						     og),
					  idio_operator_module);
	} else {
	    IDIO c = og;
	    IDIO p = idio_S_nil;
	    while (idio_S_nil != c) {
		IDIO cpri = IDIO_PAIR_HH (c);
		if (IDIO_FIXNUM_VAL (cpri) > pri) {
		    if (idio_S_nil == p) {
			idio_module_set_symbol_value (idio_infix_operator_group,
						      idio_pair (idio_pair (fpri, grp),
								 c),
						      idio_operator_module);
		    } else {
			IDIO_PAIR_T (p) = idio_pair (idio_pair (fpri, grp),
						     c);
		    }
		    break;
		}
		p = c;
		c = IDIO_PAIR_T (c);
	    }
	    if (idio_S_nil == c) {
		IDIO_PAIR_T (p) = idio_pair (idio_pair (fpri, grp),
					     c);
	    }
	}
    } else {
	IDIO procs = IDIO_PAIR_T (grp);
	IDIO old = idio_list_assq (id, procs);

	if (idio_S_false == old) {
	    IDIO_PAIR_T (grp) = idio_pair (idio_pair (id, proc), procs);
	} else {
	    IDIO_PAIR_T (old) = proc;
	}
    }
}

static IDIO idio_evaluate_infix_operator_code (IDIO m)
{
    IDIO_ASSERT (m);

    /* idio_debug ("evaluate-infix-operator-code: %s\n", m);   */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_vm_codegen (ethr, m);
    IDIO r = idio_vm_run (ethr);
    
    idio_thread_restore_state (ethr);
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-infix-operator-code: out: %s\n", r);     */

    return r;
}

static IDIO idio_evaluate_infix_operator (IDIO n, IDIO e, IDIO b, IDIO a)
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (e);
    IDIO_ASSERT (b);
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (pair, e);
    
    /* idio_debug ("evaluate-infix-operator: in n %s", n);  */
    /* idio_debug (" b %s", b);  */
    /* idio_debug (" a %s\n", a);  */

    IDIO func = IDIO_PAIR_T (e);
    if (! (idio_isa_closure (func) ||
	   idio_isa_primitive (func))) {
	idio_error_C ("operator: invalid code", IDIO_LIST2 (n, e), IDIO_C_LOCATION ("idio_evaluate_infix_operator"));
    }
    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_apply (IDIO_PAIR_T (e), IDIO_LIST3 (n, b, IDIO_LIST1 (a)));
    IDIO r = idio_vm_run (ethr);
    
    idio_thread_restore_state (ethr);
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-infix-operator: out: %s\n", r);      */

    return r;
}

IDIO idio_infix_operatorp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO operator_list = idio_module_symbol_value (idio_infix_operator_list, idio_operator_module);
    /* idio_debug ("op? ol %s\n", operator_list);  */
    
    IDIO assq = idio_list_assq (name, operator_list);

    if (idio_S_false != assq) {
	/* idio_debug ("op %s?", name); */
	/* idio_debug (" %s\n", assq); */

	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    /* idio_debug ("operator?: %s isa PAIR\n", name);  */
	    IDIO lv = idio_module_current_symbol_value_recurse (name);
	    /* idio_debug ("operator?: lookup -> %s\n", lv);  */
	    if (idio_isa_primitive (lv) ||
		idio_isa_closure (lv)) {
		IDIO_PAIR_T (assq) = lv;
	    }
	} else {
	    /* fprintf (stderr, "operator?: isa %s\n", idio_type2string (v)); */
	}
    }

    return assq;
}

IDIO_DEFINE_PRIMITIVE1 ("infix-operator?", infix_operatorp, (IDIO x))
{
    IDIO_ASSERT (x);

    return idio_infix_operatorp (x);
}

void idio_install_postfix_operator (IDIO id, IDIO proc, int pri)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);
    
    /* idio_debug ("op install %s", id); */
    /* idio_debug (" as %s\n", proc); */
    
    idio_module_set_symbol_value (id, proc, idio_operator_module);

    IDIO ol = idio_module_symbol_value (idio_postfix_operator_list, idio_operator_module);
    IDIO op = idio_list_assq (id, ol);

    if (idio_S_false == op) {
	idio_module_set_symbol_value (idio_postfix_operator_list, 
				      idio_pair (idio_pair (id, proc),
						 ol), 
				      idio_operator_module); 
    } else {
	IDIO_PAIR_T (op) = proc;
    }

    IDIO og = idio_module_symbol_value (idio_postfix_operator_group, idio_operator_module);

    IDIO fpri = idio_fixnum (pri);
    IDIO grp = idio_list_assq (fpri, og);

    if (idio_S_false == grp) {
	grp = IDIO_LIST1 (idio_pair (id, proc));

	if (idio_S_nil == og) {
	    idio_module_set_symbol_value (idio_postfix_operator_group,
					  idio_pair (idio_pair (fpri, grp),
						     og),
					  idio_operator_module);
	} else {
	    IDIO c = og;
	    IDIO p = idio_S_nil;
	    while (idio_S_nil != c) {
		IDIO cpri = IDIO_PAIR_HH (c);
		if (IDIO_FIXNUM_VAL (cpri) > pri) {
		    if (idio_S_nil == p) {
			idio_module_set_symbol_value (idio_postfix_operator_group,
						      idio_pair (idio_pair (fpri, grp),
								 c),
						      idio_operator_module);
		    } else {
			IDIO_PAIR_T (p) = idio_pair (idio_pair (fpri, grp),
						     c);
		    }
		    break;
		}
		p = c;
		c = IDIO_PAIR_T (c);
	    }
	    if (idio_S_nil == c) {
		IDIO_PAIR_T (p) = idio_pair (idio_pair (fpri, grp),
					     c);
	    }
	}
    } else {
	IDIO procs = IDIO_PAIR_T (grp);
	IDIO old = idio_list_assq (id, procs);

	if (idio_S_false == old) {
	    IDIO_PAIR_T (grp) = idio_pair (idio_pair (id, proc), procs);
	} else {
	    IDIO_PAIR_T (old) = proc;
	}
    }
}

static IDIO idio_evaluate_postfix_operator_code (IDIO m)
{
    IDIO_ASSERT (m);

    /* idio_debug ("evaluate-postfix-operator-code: %s\n", m);   */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_vm_codegen (ethr, m);
    IDIO r = idio_vm_run (ethr);
    
    idio_thread_restore_state (ethr);
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-postfix-operator-code: out: %s\n", r);     */

    return r;
}

static IDIO idio_evaluate_postfix_operator (IDIO n, IDIO e, IDIO b, IDIO a)
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (e);
    IDIO_ASSERT (b);
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (pair, e);
    
    /* idio_debug ("evaluate-postfix-operator: in n %s", n);  */
    /* idio_debug (" b %s", b);  */
    /* idio_debug (" a %s\n", a);  */

    IDIO func = IDIO_PAIR_T (e);
    if (! (idio_isa_closure (func) ||
	   idio_isa_primitive (func))) {
	idio_error_C ("operator: invalid code", IDIO_LIST2 (n, e), IDIO_C_LOCATION ("idio_evaluate_postfix_operator"));
    }
    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_apply (IDIO_PAIR_T (e), IDIO_LIST3 (n, b, IDIO_LIST1 (a)));
    IDIO r = idio_vm_run (ethr);
    
    idio_thread_restore_state (ethr);
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-postfix-operator: out: %s\n", r);     */

    return r;
}

IDIO idio_postfix_operatorp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO operator_list = idio_module_symbol_value (idio_postfix_operator_list, idio_operator_module);
    /* idio_debug ("op? ol %s\n", operator_list); */
    
    IDIO assq = idio_list_assq (name, operator_list);

    if (idio_S_false != assq) {
	/* idio_debug ("op %s?", name); */
	/* idio_debug (" %s\n", assq); */

	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    /* idio_debug ("operator?: %s isa PAIR\n", name);  */
	    IDIO lv = idio_module_current_symbol_value_recurse (name);
	    /* idio_debug ("operator?: lookup -> %s\n", lv);  */
	    if (idio_isa_primitive (lv) ||
		idio_isa_closure (lv)) {
		IDIO_PAIR_T (assq) = lv;
	    }
	} else {
	    /* fprintf (stderr, "operator?: isa %s\n", idio_type2string (v)); */
	}
    }

    return assq;
}

IDIO_DEFINE_PRIMITIVE1 ("postfix-operator?", postfix_operatorp, (IDIO x))
{
    IDIO_ASSERT (x);

    return idio_postfix_operatorp (x);
}

IDIO idio_operatorp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO assq = idio_infix_operatorp (name);
    if (idio_S_false == assq) {
	assq = idio_postfix_operatorp (name);
    }

    return assq;
}

IDIO_DEFINE_PRIMITIVE1 ("operator?", operatorp, (IDIO x))
{
    IDIO_ASSERT (x);

    return idio_operatorp (x);
}

static IDIO idio_meaning (IDIO e, IDIO nametree, int flags);

static IDIO idio_meaning_reference (IDIO name, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO sk = idio_variable_kind (nametree, name, flags);

    if (idio_S_unspec == sk) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_static_error_unbound (name, IDIO_C_LOCATION ("idio_meaning_reference"));

	/* notreached */
	return idio_S_unspec;
    }

    IDIO kind = IDIO_PAIR_H (sk);
    IDIO fmci = IDIO_PAIR_HT (sk);
    
    if (idio_S_local == kind) {
	IDIO fvi = IDIO_PAIR_HTT (sk);
	if (0 == IDIO_FIXNUM_VAL (fmci)) {
	    return IDIO_LIST2 (idio_I_SHALLOW_ARGUMENT_REF, fvi);
	} else {
	    return IDIO_LIST3 (idio_I_DEEP_ARGUMENT_REF, fmci, fvi);
	}
    } else if (idio_S_toplevel == kind) {
	return IDIO_LIST2 (idio_I_CHECKED_GLOBAL_REF, fmci);
    } else if (idio_S_dynamic == kind) {
	return IDIO_LIST2 (idio_I_DYNAMIC_REF, fmci);
    } else if (idio_S_environ == kind) {
	return IDIO_LIST2 (idio_I_ENVIRON_REF, fmci);
    } else if (idio_S_computed == kind) {
	return IDIO_LIST2 (idio_I_COMPUTED_REF, fmci);
    } else if (idio_S_predef == kind) {
	IDIO fvi = IDIO_PAIR_HTT (sk);
	return IDIO_LIST2 (idio_I_PREDEFINED, fvi);
    } else {
	idio_static_error_unbound (name, IDIO_C_LOCATION ("idio_meaning_reference"));

	/* notreached */
	return idio_S_unspec;
    }
}

static IDIO idio_meaning_function_reference (IDIO name, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO sk = idio_variable_kind (nametree, name, IDIO_MEANING_LEXICAL_SCOPE (flags));

    if (idio_S_unspec == sk) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_static_error_unbound (name, IDIO_C_LOCATION ("idio_meaning_function_reference"));

	/* notreached */
	return idio_S_unspec;
    }

    IDIO kind = IDIO_PAIR_H (sk);
    IDIO fmci = IDIO_PAIR_HT (sk);
    
    if (idio_S_local == kind) {
	IDIO fvi = IDIO_PAIR_HTT (sk);
	if (0 == IDIO_FIXNUM_VAL (fmci)) {
	    return IDIO_LIST2 (idio_I_SHALLOW_ARGUMENT_REF, fvi);
	} else {
	    return IDIO_LIST3 (idio_I_DEEP_ARGUMENT_REF, fmci, fvi);
	}
    } else if (idio_S_toplevel == kind) {
	return IDIO_LIST2 (idio_I_CHECKED_GLOBAL_FUNCTION_REF, fmci);
    } else if (idio_S_dynamic == kind) {
	return IDIO_LIST2 (idio_I_DYNAMIC_FUNCTION_REF, fmci);
    } else if (idio_S_environ == kind) {
	return IDIO_LIST2 (idio_I_ENVIRON_REF, fmci);
    } else if (idio_S_computed == kind) {
	return IDIO_LIST2 (idio_I_COMPUTED_REF, fmci);
    } else if (idio_S_predef == kind) {
	IDIO fvi = IDIO_PAIR_HTT (sk);
	return IDIO_LIST2 (idio_I_PREDEFINED, fvi);
    } else {
	idio_static_error_unbound (name, IDIO_C_LOCATION ("idio_meaning_function_reference"));

	/* notreached */
	return idio_S_unspec;
    }
}

static IDIO idio_meaning_escape (IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);

    return idio_meaning (e, nametree, flags);
}

static IDIO idio_meaning_quotation (IDIO v, IDIO nametree, int flags)
{
    IDIO_ASSERT (v);

    return IDIO_LIST2 (idio_I_CONSTANT, v);
}

static IDIO idio_meaning_dequasiquote (IDIO e, int level)
{
    IDIO_ASSERT (e);

    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	if (idio_S_quasiquote == eh) {
	    /* ('list ''quasiquote (deqq (cadr e) (+ level 1))) */
	    return IDIO_LIST3 (idio_S_list,
			       IDIO_LIST2 (idio_S_quote, idio_S_quasiquote),
			       idio_meaning_dequasiquote (IDIO_PAIR_HT (e),
							  level + 1));
	} else if (idio_S_unquote == eh) {
	    if (level <= 0) {
		return IDIO_PAIR_HT (e);
	    } else {
		/* ('list ''unquote (deqq (cadr e) (- level 1))) */
		return IDIO_LIST3 (idio_S_list,
				   IDIO_LIST2 (idio_S_quote, idio_S_unquote),
				   idio_meaning_dequasiquote (IDIO_PAIR_HT (e),
							      level - 1));
	    }
	} else if (idio_S_unquotesplicing == eh) {
	    if (level <= 0) {
		return IDIO_LIST3 (idio_S_pair,
				   idio_meaning_dequasiquote (IDIO_PAIR_H (e), level),
				   idio_meaning_dequasiquote (IDIO_PAIR_T (e), level));
	    } else {
		/* ('list ''unquotesplicing (deqq (cadr e) (- level 1))) */
		return IDIO_LIST3 (idio_S_list,
				   IDIO_LIST2 (idio_S_quote, idio_S_unquotesplicing),
				   idio_meaning_dequasiquote (IDIO_PAIR_HT (e),
							      level - 1));
	    }
	} else if (level <= 0 &&
		   idio_isa_pair (IDIO_PAIR_H (e)) &&
		   idio_S_unquotesplicing == IDIO_PAIR_HH (e)) {
	    if (idio_S_nil == IDIO_PAIR_T (e)) {
		return IDIO_PAIR_HTH (e);
	    } else {
		/* ('append (cadar e) (deqq (cdr e) level)) */
		return IDIO_LIST3 (idio_S_append,
				   IDIO_PAIR_HTH (e),
				   idio_meaning_dequasiquote (IDIO_PAIR_T (e),
							      level));
	    }
	} else {
	    return IDIO_LIST3 (idio_S_pair,
			       idio_meaning_dequasiquote (IDIO_PAIR_H (e), level),
			       idio_meaning_dequasiquote (IDIO_PAIR_T (e), level));
	}
    } else if (idio_isa_array (e)) {
	return IDIO_LIST2 (idio_symbols_C_intern ("list->array"), idio_meaning_dequasiquote (idio_array_to_list (e), level));
    } else if (idio_isa_symbol (e)) {
	return IDIO_LIST2 (idio_S_quote, e);
    } else {
	return e;
    }

    /* not reached */
    return idio_S_unspec;
}

static IDIO idio_meaning_quasiquotation (IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);

    IDIO dq = idio_meaning_dequasiquote (e, 0);

    return idio_meaning (dq, nametree, flags);
}

static IDIO idio_meaning_alternative (IDIO e1, IDIO e2, IDIO e3, IDIO nametree, int flags)
{
    IDIO_ASSERT (e1);
    IDIO_ASSERT (e2);
    IDIO_ASSERT (e3);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m1 = idio_meaning (e1, nametree, IDIO_MEANING_NOT_TAILP (flags));
    IDIO m2 = idio_meaning (e2, nametree, flags);
    IDIO m3 = idio_meaning (e3, nametree, flags);
    
    return IDIO_LIST4 (idio_I_ALTERNATIVE, m1, m2, m3);
}

static IDIO idio_rewrite_cond (IDIO c)
{
    /* idio_debug ("cond-rewrite: %s\n", c);     */

    if (idio_S_nil == c) {
	/* fprintf (stderr, "cond-rewrite: nil clause\n");   */
	return idio_S_void;
    } else if (! idio_isa_pair (c)) {
	idio_error_param_type ("pair", c, IDIO_C_LOCATION ("idio_rewrite_cond"));

	/* notreached */
	return idio_S_unspec;
    } else if (! idio_isa_pair (IDIO_PAIR_H (c))) {
	/* idio_debug ("pair/pair: c %s\n", c); */
	idio_error_param_type ("pair/pair", c, IDIO_C_LOCATION ("idio_rewrite_cond"));

	/* notreached */
	return idio_S_unspec;
    } else if (idio_S_else == IDIO_PAIR_HH (c)) {
	/* fprintf (stderr, "cond-rewrite: else clause\n");  */
	if (idio_S_nil == IDIO_PAIR_T (c)) {
	    return idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_TH (c));
	} else {
	    idio_error_C ("cond: else not in last clause", c, IDIO_C_LOCATION ("idio_rewrite_cond"));

	    /* notreached */
	    return idio_S_unspec;
	}
    }

    if (idio_isa_pair (IDIO_PAIR_TH (c)) &&
	idio_S_eq_gt == IDIO_PAIR_HTH (c)) {
	/* fprintf (stderr, "cond-rewrite: => clause\n");  */
	if (idio_isa_list (IDIO_PAIR_H (c)) &&
	    idio_list_length (IDIO_PAIR_H (c)) == 3) {
	    IDIO gs = idio_gensym (NULL);
	    /*
	      `(let ((gs ,(caar c)))
	         (if gs
	             (,(caddar c) gs)
	             ,(rewrite-cond-clauses (cdr c))))
	     */
	    return IDIO_LIST3 (idio_S_let,
			       IDIO_LIST1 (IDIO_LIST2 (gs, IDIO_PAIR_HH (c))),
			       IDIO_LIST4 (idio_S_if,
					   gs,
					   IDIO_LIST2 (IDIO_PAIR_HTTH (c),
						       gs),
					   idio_rewrite_cond (IDIO_PAIR_T (c))));
	} else {
	    idio_error_param_type ("=>", c, IDIO_C_LOCATION ("idio_rewrite_cond"));

	    /* notreached */
	    return idio_S_unspec;
	}
    } else if (idio_S_nil == IDIO_PAIR_TH (c)) {
	/* fprintf (stderr, "cond-rewrite: null? cdar clause\n");  */
	IDIO gs = idio_gensym (NULL);
	/*
	  `(let ((gs ,(caar c)))
	     (or gs
	         ,(rewrite-cond-clauses (cdr c))))
	*/
	return IDIO_LIST3 (idio_S_let,
			   IDIO_LIST1 (IDIO_LIST2 (gs, IDIO_PAIR_HH (c))),
			   IDIO_LIST3 (idio_S_or,
				       gs,
				       idio_rewrite_cond (IDIO_PAIR_T (c))));
    } else {
	/* fprintf (stderr, "cond-rewrite: default clause\n");  */
	return IDIO_LIST4 (idio_S_if,
			   IDIO_PAIR_HH (c),
			   idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_TH (c)),
			   idio_rewrite_cond (IDIO_PAIR_T (c)));
    }
}

static IDIO idio_meaning_assignment (IDIO name, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (name)) {
	/*
	 * set! (foo x y z) v
	 *
	 * `((setter ,(car name)) ,@(cdr name) ,e)
	 */
	IDIO se = idio_list_append2 (IDIO_LIST1 (IDIO_LIST2 (idio_S_setter,
							     IDIO_PAIR_H (name))),
				     IDIO_PAIR_T (name));
	se = idio_list_append2 (se, IDIO_LIST1 (e));
	return idio_meaning (se,
			     nametree,
			     IDIO_MEANING_NO_DEFINE (flags));
    } else if (! idio_isa_symbol (name)) {
	idio_error_C ("cannot assign to", name, IDIO_C_LOCATION ("idio_meaning_assignment"));
    }

    /*
     * Normal assignment to a symbol
     */
    IDIO m = idio_meaning (e, nametree, IDIO_MEANING_NO_DEFINE (IDIO_MEANING_NOT_TAILP (flags)));

    IDIO sk = idio_variable_kind (nametree, name, flags);

    if (idio_S_unspec == sk) {
	idio_error_C ("unknown variable:", name, IDIO_C_LOCATION ("idio_meaning_assignment"));
    }

    IDIO kind = IDIO_PAIR_H (sk);
    IDIO fmci = IDIO_PAIR_HT (sk);
    
    IDIO assign = idio_S_nil;

    if (idio_S_local == kind) {
	IDIO fvi = IDIO_PAIR_HTT (sk);
	if (0 == IDIO_FIXNUM_VAL (fmci)) {
	    return IDIO_LIST3 (idio_I_SHALLOW_ARGUMENT_SET, fvi, m);
	} else {
	    return IDIO_LIST4 (idio_I_DEEP_ARGUMENT_SET, fmci, fvi, m);
	}
    } else if (idio_S_toplevel == kind) {
	assign = IDIO_LIST3 (idio_I_GLOBAL_SET, fmci, m);
    } else if (idio_S_dynamic == kind ||
	       idio_S_environ == kind) {
	assign = IDIO_LIST3 (idio_I_GLOBAL_SET, fmci, m);
    } else if (idio_S_computed == kind) {
	if (IDIO_MEANING_IS_DEFINE (flags)) {
	    return IDIO_LIST2 (IDIO_LIST3 (idio_I_GLOBAL_DEF, name, kind),
			       IDIO_LIST3 (idio_I_COMPUTED_DEFINE, fmci, m));
	} else {
	    return IDIO_LIST3 (idio_I_COMPUTED_SET, fmci, m);
	}
    } else if (idio_S_predef == kind) {
	/*
	 * We can shadow predefs...semantically dubious!
	 *
	 * Dubious because many (most?) functions use, say, ph,
	 * eg. map.  If you redefine ph should map be affected?  Is it
	 * your intention that by changing ph everything should use
	 * the new definition of ph immediately or just that functions
	 * defined after this should use the new definition?
	 *
	 * We need a new mci as the existing one is tagged as a predef.
	 * This new one will be tagged as a lexical.
	 */
	IDIO new_mci = idio_toplevel_extend (name, IDIO_MEANING_LEXICAL_SCOPE (flags));

	/*
	 * But now we have a problem.
	 *
	 * As we *compile* regular code, any subsequent reference to
	 * {name} will find this new toplevel and we'll embed
	 * GLOBAL-REFs to it etc..  All good for the future when we
	 * *run* the compiled code.
	 *
	 * However, if any templates in the here and now refer to
	 * {name} then we have a problem.  All we've done is extend
	 * the VM's table of known toplevels and given it a value of
	 * #undef.  If a template tries to use {name} it'll get back
	 * #undef and things will start to go wrong.
	 *
	 * What we need to do is patch up the toplevel with the
	 * current primitive value we're overriding.
	 */
	IDIO fvi = IDIO_PAIR_HTT (sk);
	idio_module_set_symbol_value (name, idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi)), idio_evaluation_module);
	assign = IDIO_LIST3 (idio_I_GLOBAL_SET, new_mci, m);
	fmci = new_mci;
	
	/* if we weren't allowing shadowing */

	/* idio_static_error_immutable (name, IDIO_C_LOCATION ("idio_meaning_assignment")); */
	/* return idio_S_unspec; */
    } else {
	idio_static_error_unbound (name, IDIO_C_LOCATION ("idio_meaning_assignment"));
	return idio_S_unspec;
    }

    if (IDIO_MEANING_IS_DEFINE (flags)) {
	return IDIO_LIST2 (IDIO_LIST3 (idio_I_GLOBAL_DEF, name, kind),
			   assign);
    } else {
	return assign;
    }
}

static IDIO idio_meaning_define (IDIO name, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-define: (define %s", name);   */
    /* idio_debug (" %s)\n", e);   */

    if (idio_isa_pair (name)) {
	/*
	 * (define (func arg) ...) => (define func (function (arg) ...))
	 *
	 * NB e is already a list
	 */
	e = idio_list_append2 (IDIO_LIST2 (idio_S_function, 
					   IDIO_PAIR_T (name)),
			       e);
	name = IDIO_PAIR_H (name);
    } else {
	if (idio_isa_pair (e)) {
	    e = IDIO_PAIR_H (e);
	}
    }

    return idio_meaning_assignment (name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_LEXICAL_SCOPE (flags)));
}

static IDIO idio_meaning_define_macro (IDIO name, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-define-macro:\nname=%s\n", name);  */
    /* idio_debug ("e=%s\n", e);  */

    /*
     * (define-macro (func arg) ...) => (define-macro func (function (arg) ...))
     */
    if (idio_isa_pair (name)) {
	e = IDIO_LIST3 (idio_S_function,  
			IDIO_PAIR_T (name), 
			e); 
	name = IDIO_PAIR_H (name);
    }

    /*
     * create an expander: (function (x e) (apply proc (cdr x)))
     *
     * where proc is (function (arg) ...) from above, ie. e
     */
    IDIO x_sym = idio_symbols_C_intern ("xx");
    IDIO e_sym = idio_symbols_C_intern ("ee");
    IDIO expander = IDIO_LIST3 (idio_S_function,
				IDIO_LIST2 (x_sym, e_sym),
				IDIO_LIST3 (idio_S_apply,
					    e,
					    IDIO_LIST2 (idio_S_pt, x_sym)));

    /*
     * In general (define-macro a ...) means that "a" is associated
     * with an expander and that expander takes the cdr of the
     * expression it is passed, "(a ...)" (ie. it skips over its own
     * name).
     *
     * It happens that people say
     *
     * (define-macro %b ...)
     * (define-macro b %b)
     *
     * (in particular where they are creating an enhanced version of b
     * which may require using the existing b to define itself hence
     * defining some other name, "%b", which can use "b" freely then
     * redefine b to this new version)
     *
     * However, we can't just use the current value of "%b" in
     * (define-macro b %b) as this macro-expander association means we
     * are replacing the nominal definition of a macro with an
     * expander which takes two arguments and the body of which will
     * take the cdr of its first argument.  Left alone, expander "b"
     * will take the cdr then expander "%b" will take the cdr....  "A
     * Cdr Too Far", one would say, in hindsight and thinking of the
     * big budget movie potential.
     *
     * So catch the case where the value is already an expander.
     */
    if (idio_isa_symbol (e)) {
	IDIO exp = idio_expanderp (e);

	if (idio_S_false != exp) {
	    /*
	     * Another "feature."  At this point in time {exp}'s value
	     * is a pair, ({e} . {CLOS}), and so you might think that
	     * simply supplying the (ultimate) code for {name}, ie (pt
	     * {exp}), ie. {CLOS}, would be enough.
	     *
	     * But no.  By suppling {exp}, which is a pair, we force
	     * the code in expanderp to delay the rewrite of the value
	     * of {name} to {CLOS} until after {name} has nominally
	     * been assigned to in the body of the code during
	     * runtime.  In the meanwhile, we obtain the current value
	     * with idio_module_current_symbol_value_recurse() which
	     * should, of course, exist in the usual way -- probably a
	     * primitive when inside a template.
	     *
	     * Why is this an issue?  Templates, of course!
	     *
	     * If a template comes along and tries to use the value of
	     * {name}, nominally {CLOS}, it'll get into a heap of
	     * trouble because {CLOS} can refer to variables/functions
	     * defined in the normal code.  Which, of course, don't
	     * exist yet as {CLOS} is being run during compilation.
	     *
	     * So, it's a bit of a hack, but by supplying a pair we
	     * can continue to use {name} with its original value in
	     * templates and, as the body of code is run, {name}'s
	     * value will be set to {CLOS} in due course.
	     *
	     *
	     * The canonical example is {let}.  On startup, {let} is
	     * the primitive defined in scm-evaluate.c which is very
	     * basic, PRIM{let}.
	     *
	     * A little later we define a template, {%ext-let},
	     * "extended-let" which supports named lets, which uses a
	     * regular function {check-bindings}.  If we are to use
	     * the resultant closure, {CLOS}, before {check-bindings}
	     * is assigned to during runtime then we are in trouble.
	     *
	     * {%ext-let} will initially be a pair, the generic
	     * expander function defined above, (function (xx ee)
	     * (apply ...)), the same as any other template (see the
	     * comments about "bootstrap" below) and the first use of
	     * it as an expander will recognise this and call
	     * --symbol_value_recurse() to get the value {CLOS}.
	     *
	     * We now define a template {let} to be {%ext-let}.  We
	     * set the expander code's value of {let} to be {ext},
	     * ie. the pair ({%ext-let} . {CLOS}).  The first use of
	     * {let} as an expander will see that the value is a pair
	     * and call --symbol_value_recurse() and get the
	     * value... PRIM{let} -- because that's what {let} is
	     * defined as in the main code (ie. nothing to do with
	     * expanders), no-one has redefined it yet, we're
	     * currently mucking about with templates not defining any
	     * functions.  Subsequent use of {let} as an expander will
	     * get PRIM{let}.
	     *
	     * But don't forget that the return statement here is
	     * returning an assignment (to the pair, (function (xx ee)
	     * (apply ...))) so that during *runtime* we will change
	     * the expander code's value of {let} to the pair -- in
	     * addition, we will have created a new toplevel {let}
	     * shadowing the predef {let} -- which is now fine as it
	     * will be after the definition (and assignment) of
	     * {check-bindings}.  Thereafter, of course,
	     * --symbol_value_recurse() in expanderp will return
	     * toplevel {let}, ie. {CLOS}, rather than the predef
	     * {let}, PRIM{let}.
	     *
	     * Got that?  Good.  My advice?  Don't muck it up as it is
	     * "hard" to debug.
	     */
	    idio_install_expander_source (name, exp, expander);
	    return idio_meaning_assignment (name, IDIO_PAIR_T (exp), nametree, IDIO_MEANING_LEXICAL_SCOPE (flags));
	}
    }

    /*
     * XXX define-macro bootstrap
     *
     * We really want the entry in *expander-list* to be some compiled
     * code but we don't know what that code is yet because we have't
     * processed the source code of the expander -- we only invented
     * it a couple of lines above -- let alone compiled it!
     *
     * So, we'll drop the "source" code of the expander into
     * *expander-list* and later, when someone calls expander? for
     * this name we'll notice the value is a pair and do a symbol
     * lookup for the closure that was created via
     * idio_meaning_assignment().
     *
     * As an alternative we could evaluate the source to the expander
     * now and install that code in *expander-list* directly -- but
     * watch out for embedded calls to regular functions defined in
     * the code (see comment above).
     *
     * As a further twist, we really need to embed a call to
     * idio_install_expander in the *object* code too!  When
     * someone in the future loads the object file containing this
     * define-macro who will have called idio_install_expander?
     *
     * In summary: we need the expander in the here and now as someone
     * might use it in the next line of source and we need to embed a
     * call to idio_install_expander in the object code for future
     * users.
     */

    IDIO m_a = idio_meaning_assignment (name, expander, nametree, IDIO_MEANING_NOT_TAILP (IDIO_MEANING_LEXICAL_SCOPE (flags)));

    idio_install_expander_source (name, expander, expander);

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);
    
    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    IDIO ce = idio_thread_current_env ();

    idio_module_vci_set (ce, fmci, fmci);
    idio_module_vvi_set (ce, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST3 (idio_S_toplevel, fmci, fgvi), ce);

    /*
     * Careful!  Evaluate the expander code after we've called
     * idio_module_set_symbol() for the symbol.  For normal code this
     * wouldn't matter but macros are evaluated during compilation.
     */
    idio_evaluate_expander_code (m_a);

    /*
     * NB.  This effectively creates/stores the macro body code a
     * second time *in this instance of the engine*.  When the object
     * code is read in there won't be an instance of the macro body
     * code lying around -- at least not one we can access.
     */

    IDIO r = IDIO_LIST3 (idio_I_EXPANDER, idio_fixnum (mci), m_a);
    /* idio_debug ("idio-meaning-define-macro %s", name);  */
    /* idio_debug (" r=%s\n", r);  */
    return r;
}

static IDIO idio_meaning_define_infix_operator (IDIO name, IDIO pri, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("define-infix-operator: %s", name); */
    /* idio_debug (" %s\n", e); */

    if (IDIO_FIXNUM_VAL (pri) < 0) {
	idio_error_param_type ("positive fixnum", pri, IDIO_C_LOCATION ("idio_meaning_define_infix_operator"));
    }

    /*
     * Step 1: find the existing symbol for {name} or create a new one
     */
    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_vci_set (idio_operator_module, fmci, fmci);
    idio_module_vvi_set (idio_operator_module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST3 (idio_S_toplevel, fmci, fgvi), idio_operator_module);

    idio_debug ("imdio: %s", name);
    fprintf (stderr, " mci %td -> gvi %td\n", mci, gvi);
    idio_debug ("imdio? ivsr(mci) => %s\n", idio_vm_constants_ref (mci));

    /*
     * Step 2: rework the expression into some appropriate code and
     * compile
     */
    IDIO m;
    if (idio_isa_symbol (e)) {
	IDIO exp = idio_infix_operatorp (e);
	/*
	 * define-infix-operator X pri Y
	 *
	 * We happen to have the closure for Y, (pt {exp}), in our
	 * hands but when this object code is loaded the compiled
	 * code's value could be anywhere so we should be looking the
	 * value up.
	 */
	if (idio_S_false == exp) {
	    idio_error_param_type ("operator", e, IDIO_C_LOCATION ("idio_meaning_define_infix_operator"));
	}

	IDIO sve = IDIO_LIST3 (idio_symbols_C_intern ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       IDIO_LIST2 (idio_symbols_C_intern ("find-module"),
					   IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module))));

	idio_debug ("imdio: sve %s\n", sve);
	m = idio_meaning (sve, nametree, flags);
    } else {
	/*
	 * define-infix-operator X pri { ... }
	 *
	 * should really be a function definition with args: {op},
	 * {before} and {after}:
	 *
	 * define-infix-operator (X op before after) { ... }
	 */
	IDIO def_args = IDIO_LIST3 (idio_S_op, idio_S_before, idio_S_after);
	
	IDIO fe = IDIO_LIST3 (idio_S_function,
			      def_args,
			      e);
    
	idio_debug ("imdio:  fe %s\n", fe);
	m = idio_meaning (fe, nametree, flags);
    }
    IDIO r = IDIO_LIST4 (idio_I_INFIX_OPERATOR, fmci, pri, m);

    /*
     * NB.  idio_evaluate_infix_operator_code will invoke
     * idio_install_infix_operator (see IDIO_A_INFIX_OPERATOR in vm.c).
     */
    IDIO cl = idio_evaluate_infix_operator_code (r);

    /*
     * Step 3: insert the value "live"
     */
    idio_module_set_symbol_value (name, cl, idio_operator_module); 

    /* idio_debug ("idio-meaning-define-infix-operator %s", name);   */
    /* idio_debug (" cl=%s", cl);   */
    /* idio_debug (" r=%s\n", r);   */
    return r;
}

static IDIO idio_meaning_define_postfix_operator (IDIO name, IDIO pri, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);

    if (IDIO_FIXNUM_VAL (pri) < 0) {
	idio_error_param_type ("positive fixnum", pri, IDIO_C_LOCATION ("idio_meaning_define_postfix_operator"));
    }

    /*
     * Step 1: find the existing symbol for {name} or create a new one
     */
    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);
	
    idio_module_vci_set (idio_operator_module, fmci, fmci);
    idio_module_vvi_set (idio_operator_module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST3 (idio_S_toplevel, fmci, fgvi), idio_operator_module);

    /*
     * Step 2: rework the expression into some appropriate code and
     * compile
     */
    IDIO m;
    if (idio_isa_symbol (e)) {
	IDIO exp = idio_postfix_operatorp (e);
	/*
	 * define-postfix-operator X pri Y
	 *
	 * We happen to have the closure for Y, (pt {exp}), in our
	 * hands but when this object code is loaded the compiled
	 * code's value could be anywhere so we should be looking the
	 * value up.
	 */
	if (idio_S_false == exp) {
	    idio_error_param_type ("operator", e, IDIO_C_LOCATION ("idio_meaning_define_postfix_operator"));
	}

	IDIO sve = IDIO_LIST3 (idio_symbols_C_intern ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       IDIO_LIST2 (idio_symbols_C_intern ("find-module"),
					   IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module))));
	m = idio_meaning (sve, nametree, flags);
    } else {
	/*
	 * define-postfix-operator X pri { ... }
	 *
	 * should really be a function definition with args: {op},
	 * {before} and {after}:
	 *
	 * define-postfix-operator (X op before after) { ... }
	 */
	IDIO def_args = IDIO_LIST3 (idio_S_op, idio_S_before, idio_S_after);
	
	IDIO fe = IDIO_LIST3 (idio_S_function,
			      def_args,
			      e);
    
	m = idio_meaning (fe, nametree, flags);
    }
    IDIO r = IDIO_LIST4 (idio_I_POSTFIX_OPERATOR, fmci, pri, m);

    /*
     * NB.  idio_evaluate_postfix_operator_code will invoke
     * idio_install_postfix_operator (see IDIO_A_POSTFIX_OPERATOR in vm.c).
     */
    IDIO cl = idio_evaluate_postfix_operator_code (r);

    /*
     * Step 3: insert the value "live"
     */
    idio_module_set_symbol_value (name, cl, idio_operator_module); 

    /* idio_debug ("idio-meaning-define-postfix-operator %s", name);   */
    /* idio_debug (" cl=%s", cl);   */
    /* idio_debug (" r=%s\n", r);   */
    return r;
}

static IDIO idio_meaning_define_dynamic (IDIO name, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-define-dynamic: (define-dynamic %s", name);  */
    /* idio_debug (" %s)\n", e);  */

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
    }

    return idio_meaning_assignment (name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_DYNAMIC_SCOPE (flags)));
}

static IDIO idio_meaning_define_environ (IDIO name, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-define-environ: (define-environ %s", name);  */
    /* idio_debug (" %s)\n", e);  */

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
    }

    return idio_meaning_assignment (name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_ENVIRON_SCOPE (flags)));
}

/*
 * A computed variable's value should be a pair, (getter & setter).
 * Either of getter or setter can be #n.
 *
 * We shouldn't have both #n as it wouldn't be much use.
 */
static IDIO idio_meaning_define_computed (IDIO name, IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-define-computed: (define-computed %s", name);    */
    /* idio_debug (" %s)\n", e);    */

    IDIO getter = idio_S_nil;
    IDIO setter = idio_S_nil;

    if (idio_isa_pair (e)) {
	IDIO val = IDIO_PAIR_H (e);

	if (idio_isa_pair (val)) {
	    getter = IDIO_PAIR_H (val);
	    setter = IDIO_PAIR_HT (val);
	} else {
	    getter = val;
	}
    } else {
	idio_static_error_variable ("define-computed name getter setter", name, IDIO_C_LOCATION ("idio_meaning_define_computed"));
    }

    if (idio_S_nil == getter &&
	idio_S_nil == setter) {
	idio_static_error_variable ("define-computed name: both setter and getter are #n", name, IDIO_C_LOCATION ("idio_meaning_define_computed"));
    }

    /*
     * There are no property lists associated with variables so we
     * can't pull the same (set! (proc ...) v) => ((setter proc)
     * ... v) trick as we can with procs.
     *
     * So, to set the actual getter/setter pair, as opposed to setting
     * the variable (which is a call to the setter proc), we need a
     * magic flag for idio_meaning_assignment():
     * IDIO_MEANING_FLAG_DEFINE.  Here, then, this is a re-definition
     * rather than an assignment per se.
     */
    return idio_meaning_assignment (name, IDIO_LIST3 (idio_S_pair, getter, setter), nametree,
				    IDIO_MEANING_DEFINE (IDIO_MEANING_COMPUTED_SCOPE (flags)));
}

static IDIO idio_meaning_sequence (IDIO ep, IDIO nametree, int flags, IDIO keyword);

static IDIO idio_meanings_single_sequence (IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_meaning (e, nametree, flags);
}

static IDIO idio_meanings_multiple_sequence (IDIO e, IDIO ep, IDIO nametree, int flags, IDIO keyword)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, IDIO_MEANING_NOT_TAILP (flags));
    IDIO mp = idio_meaning_sequence (ep, nametree, flags, keyword);

    if (idio_S_and == keyword) {
	return IDIO_LIST3 (idio_I_AND, m, mp);
    } else if (idio_S_or == keyword) {
	return IDIO_LIST3 (idio_I_OR, m, mp);
    } else if (idio_S_begin == keyword) {
	return IDIO_LIST3 (idio_I_BEGIN, m, mp);
    } else {
	idio_error_C ("unexpected sequence keyword", keyword, IDIO_C_LOCATION ("idio_meanings_multiple_sequence"));

	/* notreached */
	return idio_S_unspec;
    }
}

static IDIO idio_meaning_sequence (IDIO ep, IDIO nametree, int flags, IDIO keyword)
{
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (keyword);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-sequence: %s\n", ep);    */
    
    if (idio_isa_pair (ep)) {
	IDIO eph = IDIO_PAIR_H (ep);
	IDIO ept = IDIO_PAIR_T (ep);

	if (idio_isa_pair (ept)) {
	    /*
	     * If we have just loaded a file, a sequence can be
	     * "really quite long" and blow C's stack up...  So,
	     * rather than calling
	     * idio_meanings_multiple_sequence() which calls us
	     * which ... we'll have to generate the solution in a
	     * loop.
	     */
	    /* IDIO r1 = idio_meanings_multiple_sequence (eph, ept, nametree, tailp, keyword); */
	    /* idio_debug ("o-m-s: %s\n", r1); */
	    /* return r1; */
	    IDIO c = idio_S_nil;
	    if (idio_S_and == keyword) {
		c = idio_I_AND;
	    } else if (idio_S_or == keyword) {
		c = idio_I_OR;
	    } else if (idio_S_begin == keyword) {
		c = idio_I_BEGIN;
	    } else {
		idio_error_C ("unexpected sequence keyword", keyword, IDIO_C_LOCATION ("idio_meaning_sequence"));

		/* notreached */
		return idio_S_unspec;
	    }

	    IDIO e = IDIO_PAIR_H (ep);
	    ep = IDIO_PAIR_T (ep);

	    IDIO mp = idio_S_nil;

	    /* 
	     * Generate meanings in order (partly so any defined names
	     * come out in order)
	     */
	    for (;;) {
		int seq_flags = IDIO_MEANING_NOT_TAILP (flags);
		if (idio_S_nil == ep) {
		    seq_flags = flags;
		}
		
		IDIO m = idio_meaning (e, nametree, seq_flags);
		mp = idio_pair (m, mp);

		if (idio_S_nil == ep) {
		    break;
		}

		e = IDIO_PAIR_H (ep);
		ep = IDIO_PAIR_T (ep);
	    }

	    /* 
	     * now loop over the reversed list of meanings prefixing
	     * AND/OR/BEGIN.
	     */
	    IDIO r = idio_S_nil;
	    for (;idio_S_nil != mp;) {
		r = idio_pair (IDIO_PAIR_H (mp), r);
		mp = IDIO_PAIR_T (mp);
	    }

	    return idio_pair (c, r);
	} else {
	    return idio_meanings_single_sequence (eph, nametree, flags);
	}
    }

    /*
     * We can get here for the x in the bindings of

     * (define (list . x) x)
     */
    return idio_meaning (ep, nametree, flags);
}

static IDIO idio_meaning_fix_abstraction (IDIO ns, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, ns);

    IDIO mp = idio_meaning_sequence (ep, nt2, IDIO_MEANING_SET_TAILP (flags), idio_S_begin);

    return IDIO_LIST3 (idio_I_FIX_CLOSURE, mp, idio_fixnum (arity));
}

static IDIO idio_meaning_dotted_abstraction (IDIO ns, IDIO n, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append2 (ns, IDIO_LIST1 (n)));
    IDIO mp = idio_meaning_sequence (ep, nt2, IDIO_MEANING_SET_TAILP (flags), idio_S_begin);

    return IDIO_LIST3 (idio_I_NARY_CLOSURE, mp, idio_fixnum (arity));
}


/*
 * With idio_rewrite_body* we want to massage the code to support some
 * semantic trickery.

 * The two broad swathes are:
 *
 * 1. multiple function defines in (Scheme: at the start of) the body.
 *
 *    {
 *      define (f) { ... }
 *      define (g) { ... }
 *      ...
 *    }
 *
 *    Here, these two are expected to be self or mutually recursive
 *    (as well as being of local scope) and so the body is rewritten
 *    such that these are letrec definitions (and can safely be
 *    self/mutually recursive) and rest of the the body becomes the
 *    body of the letrec
 *
 *    In Scheme these are only allowed at the start of the body
 *    whereas Idio allows them to appear anywhere in the body and when
 *    one appears we combine them with any immediately following.

 * 2. new variable introductions
 *
 *    a. := (let*)
 *
 *       This is an extension of the letrec mechanism above so that
 *       the remaining body is transformed into a let* of the variable
 *       assignment.
 *
 *    b. :* :~ !* !~
 *
 *       These require a similar transform as the introduction of
 *       (potentially shadowing) environment/dynamic variables
 *       requires that their definitions be removed from the stack at
 *       the end of the body.
 *
 *       Essentially the same as introducing environment/dynamic
 *       variables we can unset them which requires the same
 *       technique.
 */
static IDIO idio_rewrite_body_letrec (IDIO e);

static IDIO idio_rewrite_body (IDIO e)
{
    IDIO l = e;

    /* idio_debug ("rewrite-body: %s\n", l);  */

    IDIO r = idio_S_nil;
    
    for (;;) {
	IDIO cur = idio_S_unspec;
	if (idio_S_nil == l) {
	    break;
	} else if (idio_isa_pair (l) &&
		   idio_isa_pair (IDIO_PAIR_H (l)) &&
		   idio_S_false != idio_expanderp (IDIO_PAIR_HH (l))) {
	    cur = idio_macro_expands (IDIO_PAIR_H (l));
	} else {
	    cur = IDIO_PAIR_H (l);
	}

	if (idio_isa_pair (cur) &&
	    idio_S_begin == IDIO_PAIR_H (cur)) {
	    /*  redundant begin */
	    l = idio_list_append2 (IDIO_PAIR_T (cur), IDIO_PAIR_T (l));
	    continue;
	} else if (idio_isa_pair (cur) &&
		   (idio_S_define == IDIO_PAIR_H (cur) ||
		    idio_S_colon_plus == IDIO_PAIR_H (cur))) {
	    /* :+ or define -> letrec */

	    IDIO body = idio_list_append2 (IDIO_LIST1 (cur), IDIO_PAIR_T (l));
	    r = idio_pair (idio_rewrite_body_letrec (body), r);
	    break;
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_eq == IDIO_PAIR_H (cur)) {
	    /* := -> let* */

	    IDIO body = idio_rewrite_body (IDIO_PAIR_T (l));
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }
	    r = idio_list_append2 (r,
				   IDIO_LIST1 (IDIO_LIST3 (idio_S_let,
							   IDIO_LIST1 (IDIO_PAIR_T (cur)),
							   idio_list_append2 (IDIO_LIST1 (idio_S_begin), body))));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_star == IDIO_PAIR_H (cur)) {
	    /* :* -> environ-let */

	    IDIO body = idio_rewrite_body (IDIO_PAIR_T (l));
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }
	    r = idio_list_append2 (r,
				   IDIO_LIST1 (IDIO_LIST3 (idio_S_environ_let,
							   IDIO_PAIR_T (cur),
							   idio_list_append2 (IDIO_LIST1 (idio_S_begin), body))));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_ex_star == IDIO_PAIR_H (cur)) {
	    /* !* -> environ-unset */

	    IDIO body = idio_rewrite_body (IDIO_PAIR_T (l));
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }
	    r = idio_list_append2 (r,
				   IDIO_LIST1 (IDIO_LIST3 (idio_S_environ_unset,
							   IDIO_PAIR_HT (cur),
							   idio_list_append2 (IDIO_LIST1 (idio_S_begin), body))));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_tilde == IDIO_PAIR_H (cur)) {
	    /* :~ -> dynamic-let */

	    IDIO body = idio_rewrite_body (IDIO_PAIR_T (l));
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }
	    r = idio_list_append2 (r,
				   IDIO_LIST1 (IDIO_LIST3 (idio_S_dynamic_let,
							   IDIO_PAIR_T (cur),
							   idio_list_append2 (IDIO_LIST1 (idio_S_begin), body))));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_ex_tilde == IDIO_PAIR_H (cur)) {
	    /* !~ -> dynamic-unset */

	    IDIO body = idio_rewrite_body (IDIO_PAIR_T (l));
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }
	    r = idio_list_append2 (r,
				   IDIO_LIST1 (IDIO_LIST3 (idio_S_dynamic_unset,
							   IDIO_PAIR_HT (cur),
							   idio_list_append2 (IDIO_LIST1 (idio_S_begin), body))));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_define_macro == IDIO_PAIR_H (cur)) {
	    /* internal define-macro */
	    idio_debug ("%s\n", cur);
	    idio_error_printf (IDIO_C_LOCATION ("rewrite-body"), "internal define-macro");
	     return idio_S_unspec;
	} else {
	    /* body proper */
	    r = idio_pair (cur, r);
	    l = IDIO_PAIR_T (l);
	    continue;
	}
    }

    return idio_list_reverse (r);
}

static IDIO idio_rewrite_body_letrec (IDIO e)
{
    IDIO l = e;
    IDIO defs = idio_S_nil;

    /* idio_debug ("rewrite-body-letrec: %s\n", l);  */

    for (;;) {
	IDIO cur = idio_S_unspec;
	if (idio_S_nil == l) {
	    idio_error_warning_message ("empty body");
	    return idio_S_nil;
	} else if (idio_isa_pair (l) &&
		   idio_isa_pair (IDIO_PAIR_H (l)) &&
		   idio_S_false != idio_expanderp (IDIO_PAIR_HH (l))) {
	    cur = idio_macro_expands (IDIO_PAIR_H (l));
	} else {
	    cur = IDIO_PAIR_H (l);
	}

	if (idio_isa_pair (cur) &&
	    idio_S_begin == IDIO_PAIR_H (cur)) {
	    /*  redundant begin */
	    l = idio_list_append2 (IDIO_PAIR_T (cur), IDIO_PAIR_T (l));
	    continue;
	} else if (idio_isa_pair (cur) &&
		   (idio_S_define == IDIO_PAIR_H (cur) ||
		    idio_S_colon_plus == IDIO_PAIR_H (cur))) {

	    IDIO bindings = IDIO_PAIR_HT (cur);
	    IDIO form = idio_S_unspec;
	    if (idio_isa_pair (bindings)) {
		form = IDIO_LIST2 (IDIO_PAIR_H (bindings),
				   idio_list_append2 (IDIO_LIST2 (idio_S_function,
								  IDIO_PAIR_T (bindings)),
						      IDIO_PAIR_TT (cur)));
	    } else {
		form = IDIO_PAIR_T (cur);
	    }
	    defs = idio_pair (form, defs);
	    l = IDIO_PAIR_T (l);
	    continue;
	} else if (idio_isa_pair (cur) &&
		   idio_S_define_macro == IDIO_PAIR_H (cur)) {
	    /* internal define-macro */
	    idio_error_printf (IDIO_C_LOCATION ("idio_rewrite_body_letrec"), "internal define-macro");
	    return idio_S_unspec;
	} else {
	    /* body proper */
	    l = idio_rewrite_body (l);

	    /* idio_debug ("irb-letrec: body: l %s\n", l);  */

	    if (idio_S_nil == defs) {
		return l;
	    } else {
		/*
		 * poor man's letrec*
		 *
		 * We are aiming for:
		 *
		 * (let ((v1 #f)
		 *	 (v2 #f))
		 *   (set! v1 a1)
		 *   (set1 v2 a2)
		 *   body)
		 *
		 * but we return it as a list of one, ((let
		 * ... body)), so that idio_meaning_sequence will
		 * re-read the first element in the list and interpret
		 * it as the expander "let"
		 *
		 * NB Leave defs reversed as creating the
		 * bindings/assignments will implicitly re-order them
		 */
		IDIO bindings = idio_S_nil;
		IDIO ns = idio_list_mapcar (defs);
		while (idio_S_nil != ns) {
		    bindings = idio_pair (IDIO_LIST2 (IDIO_PAIR_H (ns), idio_S_false), bindings);
		    ns = IDIO_PAIR_T (ns);
		}
		IDIO body = idio_S_nil;
		IDIO vs = defs;
		while (idio_S_nil != vs) {
		    IDIO assign = idio_list_append2 (IDIO_LIST1 (idio_S_set), IDIO_PAIR_H (vs));
		    body = idio_list_append2 (IDIO_LIST1 (assign), body);
		    vs = IDIO_PAIR_T (vs);
		}
		body = idio_list_append2 (body, l);
		return IDIO_LIST2 (idio_S_begin, idio_list_append2 (IDIO_LIST2 (idio_S_let, bindings), body));
	    }
	}
    }
}

static IDIO idio_meaning_abstraction (IDIO nns, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (nns);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    ep = idio_rewrite_body (ep); 

    IDIO ns = nns;
    IDIO regular = idio_S_nil;

    for (;;) {
	if (idio_isa_pair (ns))  {
	    regular = idio_pair (IDIO_PAIR_H (ns), regular);
	    ns = IDIO_PAIR_T (ns);
	} else if (idio_S_nil == ns) {
	    return idio_meaning_fix_abstraction (nns, ep, nametree, flags);
	} else {
	    return idio_meaning_dotted_abstraction (idio_list_reverse (regular), ns, ep, nametree, flags);
	}
    }

    idio_error_C ("meaning-abstraction", IDIO_LIST2 (nns, ep), IDIO_C_LOCATION ("idio_meaning_abstraction"));

    /* notreached */
    return idio_S_unspec;
}

static IDIO idio_meaning_block (IDIO es, IDIO nametree, int flags)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    es = idio_rewrite_body (es);

    return idio_meaning_sequence (es, nametree, flags, idio_S_begin);
}

static IDIO idio_meanings (IDIO es, IDIO nametree, size_t size, int flags);

static IDIO idio_meaning_some_arguments (IDIO e, IDIO es, IDIO nametree, size_t size, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, IDIO_MEANING_NOT_TAILP (flags));
    IDIO ms = idio_meanings (es, nametree, size, flags);
    size_t rank = size - (idio_list_length (es) + 1);

    return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, idio_fixnum (rank));
}

static IDIO idio_meaning_no_argument (IDIO nametree, size_t size, int flags)
{
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, idio_fixnum (size));
}

static IDIO idio_meanings (IDIO es, IDIO nametree, size_t size, int flags)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (es)) {
	return idio_meaning_some_arguments (IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, flags);
    } else {
	return idio_meaning_no_argument (nametree, size, flags);
    }
}

static IDIO idio_meaning_fix_closed_application (IDIO ns, IDIO body, IDIO es, IDIO nametree, int flags)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    body = idio_rewrite_body (body);
    
    IDIO ms = idio_meanings (es, nametree, idio_list_length (es), IDIO_MEANING_NOT_TAILP (flags));
    IDIO nt2 = idio_nametree_extend (nametree, ns);
    IDIO mbody = idio_meaning_sequence (body, nt2, flags, idio_S_begin);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_meaning_dotteds (IDIO es, IDIO nametree, size_t size, size_t arity, int flags);

static IDIO idio_meaning_some_dotted_arguments (IDIO e, IDIO es, IDIO nametree, size_t size, size_t arity, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, IDIO_MEANING_NOT_TAILP (flags));
    IDIO ms = idio_meaning_dotteds (es, nametree, size, arity, flags);
    size_t rank = size - (idio_list_length (es) + 1);

    if (rank < arity) {
	return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, idio_fixnum (rank));
    } else {
	return IDIO_LIST4 (idio_I_CONS_ARGUMENT, m, ms, idio_fixnum (arity));
    }
}

static IDIO idio_meaning_no_dotted_argument (IDIO nametree, size_t size, size_t arity, int flags)
{
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, idio_fixnum (arity));
}

static IDIO idio_meaning_dotteds (IDIO es, IDIO nametree, size_t size, size_t arity, int flags)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (es)) {
	return idio_meaning_some_dotted_arguments (IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, arity, flags);
    } else {
	return idio_meaning_no_dotted_argument (nametree, size, arity, flags);
    }
}

static IDIO idio_meaning_dotted_closed_application (IDIO ns, IDIO n, IDIO body, IDIO es, IDIO nametree, int flags)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO ms = idio_meaning_dotteds (es, nametree, idio_list_length (es), idio_list_length (ns), IDIO_MEANING_NOT_TAILP (flags));
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append2 (ns, IDIO_LIST1 (n)));
    IDIO mbody = idio_meaning_sequence (body, nt2, flags, idio_S_begin);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_meaning_closed_application (IDIO e, IDIO ees, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (ees);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-closed-application: %s\n", e); */

    /*
     * ((function ...) args)
     *
     * therefore (car e) == 'function
     */
    IDIO et = IDIO_PAIR_T (e);
    
    IDIO nns = IDIO_PAIR_H (et);
    IDIO ns = nns;
    IDIO es = ees;
    IDIO regular = idio_S_nil;

    for (;;) {
	if (idio_isa_pair (ns)) {
	    if (idio_isa_pair (es)) {
		regular = idio_pair (IDIO_PAIR_H (ns), regular);
		ns = IDIO_PAIR_T (ns);
		es = IDIO_PAIR_T (es);
	    } else {
		idio_static_error_arity ("not enough arguments", IDIO_LIST2 (nns, ees), IDIO_C_LOCATION ("idio_meaning_closed_application"));
		return idio_S_unspec;
	    }
	} else if (idio_S_nil == ns) {
	    if (idio_S_nil == es) {
		return idio_meaning_fix_closed_application (nns, IDIO_PAIR_T (et), ees, nametree, flags);
	    } else {
		idio_static_error_arity ("too many arguments", IDIO_LIST2 (e, ees), IDIO_C_LOCATION ("idio_meaning_closed_application"));
		return idio_S_unspec;
	    }
	} else {
	    return idio_meaning_dotted_closed_application (idio_list_reverse (regular), ns, IDIO_PAIR_T (et), ees, nametree, flags);
	}
    }

    idio_error_C ("meaning-closed-application", IDIO_LIST2 (e, ees), IDIO_C_LOCATION ("idio_meaning_closed_application"));

    /* notreached */
    return idio_S_unspec;
}

static IDIO idio_meaning_regular_application (IDIO e, IDIO es, IDIO nametree, int flags);

static IDIO idio_meaning_primitive_application (IDIO e, IDIO es, IDIO nametree, int flags, size_t arity, IDIO index)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, e);
    IDIO_TYPE_ASSERT (list, es);
    IDIO_TYPE_ASSERT (list, nametree);

    /*
     * Of these IDIO_A_PRIMCALL1_SET_CUR_MOD *must* be a specialized
     * primitive call because it must be called "inline", ie. not in
     * the context of a regular Idio FUNCTION_INVOKE.
     *
     * If you allow the latter then IDIO_THREAD_ENV() is
     * saved/restored around the call which defeats the point in it
     * trying to alter IDIO_THREAD_ENV().
     *
     * Everything will remain permanently in the main Idio module.
     */

    /*
     * Yuk!

     * For regular function calls the duty cycle is to evaluate all
     * the arguments, pushing them onto the stack, create a frame, pop
     * the arguments off the stack into the frame, evaluate the
     * functional argument and push the resultant function in a
     * register then call FUNCTION_INVOKE or FUNCTION_GOTO.

     * For our hand-crafted primitives written in C we know they take
     * a small number (three or fewer, probably) arguments and that
     * mechanically we're going to end up with a call to the
     * primitive's C function with those three or fewer arguments.  We
     * must be able to save some time.

     * We can accelerate fixed-arity primitive calls which, rather
     * than allocating frames on the stack, can just call the
     * primitive function with the contents of the VM registers
     * directly.  Better yet, we can accelerate some of them by having
     * a dedicated VM instruction thus avoiding having to pass the
     * index of the primitive at all.

     * However, if we leave the decision as to which calls to
     * accelerate to the compiler then the compiler must be able to
     * fall back to the general function call evaluator,
     * idio_meaning_regular_application().  Which is very complex.

     * For us to do it here we must know which primitive calls the VM
     * is capable of specializing which is knowledge that our
     * strongly-held pure encapsulation beliefs say we shouldn't have.

     * There must be a better way...but in the meanwhile it's much
     * less code for us to check the specialization here.
     */

    IDIO primdata = idio_vm_values_ref (IDIO_FIXNUM_VAL (index));

    if (IDIO_PRIMITIVE_VARARGS (primdata)) {
	/*
	 * only a full function call protocol can cope with varargs!
	 */
	return idio_meaning_regular_application (e, es, nametree, flags);
    } else {
	char *name = IDIO_PRIMITIVE_NAME (primdata);
	    
	switch (arity) {
	case 0:
	    {
		if (IDIO_STREQP (name, "read")) {
		    return IDIO_LIST2 (idio_I_PRIMCALL0, idio_fixnum (IDIO_A_PRIMCALL0_READ));
		} else if (IDIO_STREQP (name, "newline")) {
		    return IDIO_LIST2 (idio_I_PRIMCALL0, idio_fixnum (IDIO_A_PRIMCALL0_NEWLINE));
		} else {
		    break;
		}
	    }
	    break;
	case 1:
	    {
		IDIO m1 = idio_meaning (IDIO_PAIR_H (es), nametree, IDIO_MEANING_NOT_TAILP (flags));
	    
		if (IDIO_STREQP (name, "car") ||
		    IDIO_STREQP (name, "ph")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_CAR), m1);
		} else if (IDIO_STREQP (name, "cdr") ||
			   IDIO_STREQP (name, "pt")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_CDR), m1);
		} else if (IDIO_STREQP (name, "pair?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_PAIRP), m1);
		} else if (IDIO_STREQP (name, "symbol?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_SYMBOLP), m1);
		} else if (IDIO_STREQP (name, "display")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_DISPLAY), m1);
		} else if (IDIO_STREQP (name, "primitive?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_PRIMITIVEP), m1);
		} else if (IDIO_STREQP (name, "null?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_NULLP), m1);
		} else if (IDIO_STREQP (name, "continuation?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_CONTINUATIONP), m1);
		} else if (IDIO_STREQP (name, "eof?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_EOFP), m1);
		} else if (IDIO_STREQP (name, "%set-current-module!")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_SET_CUR_MOD), m1);
		} else {
		    break;
		}
	    }
	    break;
	case 2:
	    {
		IDIO m1 = idio_meaning (IDIO_PAIR_H (es), nametree, IDIO_MEANING_NOT_TAILP (flags));
		IDIO m2 = idio_meaning (IDIO_PAIR_HT (es), nametree, IDIO_MEANING_NOT_TAILP (flags));

		if (IDIO_STREQP (name, "cons") ||
		    IDIO_STREQP (name, "pair")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_CONS), m1, m2);
		} else if (IDIO_STREQP (name, "eq?")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_EQP), m1, m2);
		} else if (IDIO_STREQP (name, "set-car!") ||
			   IDIO_STREQP (name, "set-ph!")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_SET_CAR), m1, m2);
		} else if (IDIO_STREQP (name, "set-cdr!") ||
			   IDIO_STREQP (name, "set-pt!")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_SET_CDR), m1, m2);
		} else if (IDIO_STREQP (name, "+")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_ADD), m1, m2);
		} else if (IDIO_STREQP (name, "-")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_SUBTRACT), m1, m2);
		} else if (IDIO_STREQP (name, "=")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_EQ), m1, m2);
		} else if (IDIO_STREQP (name, "lt")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_LT), m1, m2);
		} else if (IDIO_STREQP (name, "gt")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_GT), m1, m2);
		} else if (IDIO_STREQP (name, "*")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_MULTIPLY), m1, m2);
		} else if (IDIO_STREQP (name, "le")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_LE), m1, m2);
		} else if (IDIO_STREQP (name, "ge")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_GE), m1, m2);
		} else if (IDIO_STREQP (name, "remainder")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_REMAINDER), m1, m2);
		} else if (IDIO_STREQP (name, "remainder")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_REMAINDER), m1, m2);
		} else {
		    break;
		}
	    }
	    break;
	case 3:
	    /* no 3-arity primitive calls */
	    break;
	default:
	    break;
	}
    }

    /*
     * Anything else goes through the full function call protocol
     */
    return idio_meaning_regular_application (e, es, nametree, flags);
}

static IDIO idio_meaning_regular_application (IDIO e, IDIO es, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m;
    if (idio_isa_symbol (e)) {
	m = idio_meaning_function_reference (e, nametree, flags);
    } else {
	m = idio_meaning (e, nametree, IDIO_MEANING_NOT_TAILP (flags));
    }
    IDIO ms = idio_meanings (es, nametree, idio_list_length (es), IDIO_MEANING_NOT_TAILP (flags));

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST3 (idio_I_TR_REGULAR_CALL, m, ms);
    } else {
	return IDIO_LIST3 (idio_I_REGULAR_CALL, m, ms);
    }
}

static IDIO idio_meaning_application (IDIO e, IDIO es, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_symbol (e)) {
	IDIO sk = idio_variable_kind (nametree, e, IDIO_MEANING_LEXICAL_SCOPE (flags));

	if (idio_isa_pair (sk)) {
	    IDIO kind = IDIO_PAIR_H (sk);

	    if (idio_S_predef == kind) {
		IDIO fvi = IDIO_PAIR_HTT (sk);
		IDIO primdata = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));

		if (idio_S_unspec != primdata) {
		    if (! idio_isa_primitive (primdata)) {
			idio_debug ("BAD! pd %s\n", primdata);
			idio_debug ("sk %s\n", sk);
			idio_debug ("e %s\n", e);
			idio_debug ("ivvr %s\n", idio_vm_values_ref (IDIO_FIXNUM_VAL (IDIO_PAIR_HTT (sk))));
		    }
		    IDIO_TYPE_ASSERT (primitive, primdata);

		    size_t arity = IDIO_PRIMITIVE_ARITY (primdata);
		    size_t nargs = idio_list_length (es);
		    
		    if ((IDIO_PRIMITIVE_VARARGS (primdata) &&
			 nargs >= arity) ||
			arity == nargs) {
			return idio_meaning_primitive_application (e, es, nametree, flags, arity, fvi);
		    } else {
			idio_static_error_primitive_arity ("wrong arity for primitive", e, es, primdata, IDIO_C_LOCATION ("idio_meaning_application"));
		    }
		}
	    }
	}
    }

    if (idio_isa_pair (e) &&
	idio_eqp (idio_S_function, IDIO_PAIR_H (e))) {
	return idio_meaning_closed_application (e, es, nametree, flags);
    } else {
	return idio_meaning_regular_application (e, es, nametree, flags);
    }

    idio_error_C ("meaning-application", IDIO_LIST2 (e, es), IDIO_C_LOCATION ("idio_meaning_application"));

    /* notreached */
    return idio_S_unspec;
}

static IDIO idio_meaning_dynamic_reference (IDIO name, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_meaning_reference (name, nametree, IDIO_MEANING_DYNAMIC_SCOPE (flags));
}

static IDIO idio_meaning_dynamic_let (IDIO name, IDIO e, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, IDIO_MEANING_NOT_TAILP (flags));

    /*
     * Get a new toplevel for this dynamic variable
     */
    IDIO fmci = idio_toplevel_extend (name, IDIO_MEANING_DYNAMIC_SCOPE (flags));

    /*
     * Get a unique value index and patch up the symbol's entry
     */
    idio_ai_t vi = idio_vm_extend_values ();
    IDIO fvi = idio_fixnum (vi);

    IDIO sk = idio_module_symbol (name, idio_evaluation_module);
    IDIO_PAIR_HTT (sk) = fvi;
    
    /*
     * Tell the tree of "locals" about this dynamic variable and find
     * the meaning of ep with the new tree
     */
    IDIO nt2 = idio_nametree_dynamic_extend (nametree, name, fmci, idio_S_dynamic);

    IDIO mp = idio_meaning_sequence (ep, nt2, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin);

    return IDIO_LIST4 (IDIO_LIST3 (idio_I_GLOBAL_DEF, name, idio_S_dynamic),
		       IDIO_LIST3 (idio_I_PUSH_DYNAMIC, fmci, m),
		       mp,
		       IDIO_LIST1 (idio_I_POP_DYNAMIC));
}

static IDIO idio_meaning_dynamic_unset (IDIO name, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_meaning_dynamic_let (name, idio_S_unset, ep, nametree, flags);
}

static IDIO idio_meaning_environ_reference (IDIO name, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_meaning_reference (name, nametree, IDIO_MEANING_ENVIRON_SCOPE (flags));
}

static IDIO idio_meaning_environ_let (IDIO name, IDIO e, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, IDIO_MEANING_NOT_TAILP (flags));

    /*
     * Get a new toplevel for this environ variable
     */
    IDIO fmci = idio_toplevel_extend (name, IDIO_MEANING_ENVIRON_SCOPE (flags));

    /*
     * Get a unique value index and patch up the symbol's entry
     */
    idio_ai_t vi = idio_vm_extend_values ();
    IDIO fvi = idio_fixnum (vi);

    IDIO sk = idio_module_symbol (name, idio_evaluation_module);
    IDIO_PAIR_HTT (sk) = fvi;
    
    /*
     * Tell the tree of "locals" about this environ variable and find
     * the meaning of ep with the new tree
     */
    IDIO nt2 = idio_nametree_dynamic_extend (nametree, name, fmci, idio_S_environ);

    IDIO mp = idio_meaning_sequence (ep, nt2, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin);

    return IDIO_LIST4 (IDIO_LIST3 (idio_I_GLOBAL_DEF, name, idio_S_environ),
		       IDIO_LIST3 (idio_I_PUSH_ENVIRON, fmci, m),
		       mp,
		       IDIO_LIST1 (idio_I_POP_ENVIRON));
}

static IDIO idio_meaning_environ_unset (IDIO name, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_meaning_environ_let (name, idio_S_unset, ep, nametree, flags);
}

static IDIO idio_meaning_computed_reference (IDIO name, IDIO nametree, int flags)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_meaning_reference (name, nametree, IDIO_MEANING_COMPUTED_SCOPE (flags));
}

static IDIO idio_meaning_monitor (IDIO e, IDIO ep, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, IDIO_MEANING_NOT_TAILP (flags));
    IDIO mp = idio_meaning_sequence (ep, nametree, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin);

    return IDIO_LIST4 (m, IDIO_LIST1 (idio_I_PUSH_HANDLER), mp, IDIO_LIST1 (idio_I_POP_HANDLER));
}

static IDIO idio_meaning_include (IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    idio_thread_save_state (idio_thread_current_thread ());
    idio_load_file (e);
    idio_thread_restore_state (idio_thread_current_thread ());

    return IDIO_LIST1 (idio_I_NOP);
}

static IDIO idio_meaning_expander (IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-expander: %s\n", e); */

    IDIO me = idio_macro_expand (e);
    
    /* idio_debug ("meaning-expander: %s\n", e); */
    /* idio_debug ("me=%s\n", me); */

    /* if (idio_S_nil == me) { */
    /* 	fprintf (stderr, "meaning-expander: bad expansion?\n"); */
    /* } */
    
    return idio_meaning (me, nametree, flags);
}

IDIO idio_infix_operator_expand (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("infix-operator-expand:   %s\n", e); */

    if (idio_isa_pair (e)) {
	IDIO og = idio_module_symbol_value (idio_infix_operator_group, idio_operator_module);
	while (idio_S_nil != og) {
	    IDIO ogp = IDIO_PAIR_H (og);
	    IDIO ops = IDIO_PAIR_T (ogp);

	    IDIO b = IDIO_LIST1 (IDIO_PAIR_H (e));
	    IDIO a = IDIO_PAIR_T (e);
	    while (idio_S_nil != a) {
		IDIO s = IDIO_PAIR_H (a);

		if (idio_isa_pair (s) &&
		    idio_S_escape == IDIO_PAIR_H (s)) {
		    /* s = IDIO_PAIR_HT (s); */
		} else {
		    IDIO opex = idio_list_assq (s, ops);
		    
		    if (idio_S_false != opex) {
			b = idio_evaluate_infix_operator (s, opex, b, IDIO_PAIR_T (a));
			return idio_infix_operator_expand (b, depth + 1);
			break;
		    }
		}
		b = idio_list_append2 (b, IDIO_LIST1 (s));
		a = IDIO_PAIR_T (a);
	    }

	    e = b;
	    og = IDIO_PAIR_T (og);
	}
    }

    return e;
}

IDIO_DEFINE_PRIMITIVE1 ("infix-operator-expand", infix_operator_expand, (IDIO l))
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    return idio_infix_operator_expand (l, 0);
}

IDIO idio_postfix_operator_expand (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("postfix-operator-expand:   %s\n", e); */

    if (idio_isa_pair (e)) {
	IDIO pog = idio_module_symbol_value (idio_postfix_operator_group, idio_operator_module);
	while (idio_S_nil != pog) {
	    IDIO pogp = IDIO_PAIR_H (pog);
	    IDIO ops = IDIO_PAIR_T (pogp);

	    IDIO b = IDIO_LIST1 (IDIO_PAIR_H (e));
	    IDIO a = IDIO_PAIR_T (e);
	    while (idio_S_nil != a) {
		IDIO s = IDIO_PAIR_H (a);

		if (idio_isa_pair (s) &&
		    idio_S_escape == IDIO_PAIR_H (s)) {
		    /* s = IDIO_PAIR_HT (s); */
		} else {
		    IDIO opex = idio_list_assq (s, ops);
		    
		    if (idio_S_false != opex) {
			b = idio_evaluate_postfix_operator (s, opex, b, IDIO_PAIR_T (a));
			return idio_postfix_operator_expand (b, depth + 1);
			break;
		    }
		}
		b = idio_list_append2 (b, IDIO_LIST1 (s));
		a = IDIO_PAIR_T (a);
	    }

	    e = b;
	    pog = IDIO_PAIR_T (pog);
	}
    }

    return e;
}

IDIO_DEFINE_PRIMITIVE1 ("postfix-operator-expand", postfix_operator_expand, (IDIO l))
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    return idio_postfix_operator_expand (l, 0);
}

IDIO idio_operator_expand (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("operator-expand:   %s\n", e); */

    e = idio_infix_operator_expand (e, depth);
    e = idio_postfix_operator_expand (e, depth);

    return e;
}

IDIO_DEFINE_PRIMITIVE1 ("operator-expand", operator_expand, (IDIO l))
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    return idio_operator_expand (l, 0);
}

static IDIO idio_meaning (IDIO e, IDIO nametree, int flags)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning: e  in %s ", e);  */
    /* fprintf (stderr, "tail=%d\n", flags);  */
    
    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	IDIO et = IDIO_PAIR_T (e);

	if (idio_S_begin == eh ||
	    idio_S_and == eh ||
	    idio_S_or == eh) {
	    if (idio_isa_pair (et)) {
		return idio_meaning_sequence (et, nametree, flags, eh);
	    } else if (idio_S_begin == eh) {
		return idio_meaning (idio_S_void, nametree, flags);
	    } else if (idio_S_and == eh) {
		return idio_meaning (idio_S_true, nametree, flags);
	    } else if (idio_S_or == eh) {
		return idio_meaning (idio_S_false, nametree, flags);
	    } else {
		idio_error_C ("unexpected sequence keyword", eh, IDIO_C_LOCATION ("idio_meaning"));
	    }
	} else if (idio_S_escape == eh) {
	    /* (escape x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_escape (IDIO_PAIR_H (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(escape)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_quote == eh) {
	    /* (quote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quotation (IDIO_PAIR_H (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(quote)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_quasiquote == eh) {
	    /* (quasiquote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quasiquotation (IDIO_PAIR_H (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(quasiquote)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_function == eh ||
		   idio_S_lambda == eh) {
	    /* (function bindings body ...) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_abstraction (IDIO_PAIR_H (et), IDIO_PAIR_T (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(function)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_if == eh) {
	    /* (if cond cons alt) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    IDIO ettth = idio_S_void;
		    if (idio_isa_pair (ettt)) {
			ettth = IDIO_PAIR_H (ettt);
		    }
		    return idio_meaning_alternative (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettth, nametree, flags);
		} else {
		    idio_error_param_nil ("(if cond)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(if)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_cond == eh) {
	    /* (cond clause ...) */
	    if (idio_isa_pair (et)) {
		if (idio_S_nil == IDIO_PAIR_T (et)) {
		    IDIO eth = IDIO_PAIR_H (et);
		    if (idio_isa_pair (eth) &&
			idio_S_block == IDIO_PAIR_H (eth)) {
			et = IDIO_PAIR_T (eth);
		    }
		} 
		/* idio_debug ("cond in:  %s\n", et);  */
		IDIO etc = idio_rewrite_cond (et);
		/* idio_debug ("cond in+: %s\n", etc);  */
		IDIO c = idio_meaning (etc, nametree, flags);
		/* idio_debug ("cond out: %s\n", c); */
		return c;
	    } else {
		idio_error_C ("cond clause*:", e, IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_set == eh ||
		   idio_S_eq == eh) {
	    /* (set! var expr) */
	    /* idio_debug ("%s\n", e); */
	    /* fprintf (stderr, "set! flags %x: tailp? %s scope %x def? %s\n", flags, IDIO_MEANING_IS_TAILP (flags) ? "y" : "n", IDIO_MEANING_SCOPE (flags), IDIO_MEANING_IS_DEFINE (flags) ? "y" : "n"); */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_assignment (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, IDIO_MEANING_LEXICAL_SCOPE (flags));
		} else {
		    idio_error_param_nil ("(set! symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(set!)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_define_macro == eh) {
	    /* (define-macro bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_macro (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, flags);
		} else {
		    idio_error_param_nil ("(define-macro symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define-macro)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_define_infix_operator == eh) {
	    /* (define-infix-operator bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_define_infix_operator (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), IDIO_PAIR_H (ettt), nametree, flags);
		    } else {
			idio_error_param_nil ("(define-infix-operator symbol pri)", IDIO_C_LOCATION ("idio_meaning"));
			return idio_S_unspec;
		    }
		} else {
		    idio_error_param_nil ("(define-infix-operator symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define-infix-operator)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_define_postfix_operator == eh) {
	    /* (define-postfix-operator bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_define_postfix_operator (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), IDIO_PAIR_H (ettt), nametree, flags);
		    } else {
			idio_error_param_nil ("(define-postfix-operator symbol pri)", IDIO_C_LOCATION ("idio_meaning"));
			return idio_S_unspec;
		    }
		} else {
		    idio_error_param_nil ("(define-postfix-operator symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define-postfix-operator)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_define == eh) {
	    /*
	     * (define var expr)
	     * (define bindings body ...)
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define (IDIO_PAIR_H (et), ett, nametree, flags);
		} else {
		    idio_error_param_nil ("(define symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_colon_eq == eh) {
	    /*
	     * (:= var expr)
	     * (:= vars expr)	;; ?? cf. let-values (call-with-values producer consumer)
	     *
	     * in the short term => define
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define (IDIO_PAIR_H (et), ett, nametree, flags);
		} else {
		    idio_error_param_nil ("(:= symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(:=)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_colon_star == eh) {
	    /*
	     * (:* var expr)
	     *
	     * in the short term => define-environ
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_environ (IDIO_PAIR_H (et), ett, nametree, flags);
		} else {
		    idio_error_param_nil ("(:* symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(:*)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_colon_tilde == eh) {
	    /*
	     * (:~ var expr)
	     *
	     * in the short term => define-dynamic
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_dynamic (IDIO_PAIR_H (et), ett, nametree, flags);
		} else {
		    idio_error_param_nil ("(:~ symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(:~)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_colon_dollar == eh) {
	    /*
	     * (:$ var getter setter)
	     *
	     * in the short term => define-computed
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_computed (IDIO_PAIR_H (et), ett, nametree, flags);
		} else {
		    idio_error_param_nil ("(:$ symbol)", IDIO_C_LOCATION ("idio_meaning"));
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(:$)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_block == eh) {
	    /*
	     * { ...}
	     */
	    if (idio_isa_pair (et)) {
		return idio_meaning_block (et, nametree, flags);
	    } else {
		return idio_meaning (idio_S_void, nametree, flags);
	    }
	} else if (idio_S_dynamic == eh) {
	    /* (dynamic var) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_dynamic_reference (IDIO_PAIR_H (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(dynamic)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_dynamic_let == eh) {
	    /* (dynamic-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_dynamic_let (IDIO_PAIR_H (eth), IDIO_PAIR_H (etht), IDIO_PAIR_T (et), nametree, flags);
		    } else {
			idio_error_param_type ("pair", etht, IDIO_C_LOCATION ("idio_meaning"));
		    }
		} else {
		    idio_error_param_type ("pair", eth, IDIO_C_LOCATION ("idio_meaning"));
		}
	    } else {
		idio_error_param_nil ("(dynamic-let)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_dynamic_unset == eh) {
	    /* (dynamic-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_dynamic_unset (eth, IDIO_PAIR_T (et), nametree, flags);
		} else {
		    idio_error_param_type ("symbol", eth, IDIO_C_LOCATION ("idio_meaning"));
		}
	    } else {
		idio_error_param_nil ("(dynamic-unset)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_environ_let == eh) {
	    /* (environ-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_environ_let (IDIO_PAIR_H (eth), IDIO_PAIR_H (etht), IDIO_PAIR_T (et), nametree, flags);
		    } else {
			idio_error_param_type ("pair", etht, IDIO_C_LOCATION ("idio_meaning"));
		    }
		} else {
		    idio_error_param_type ("pair", eth, IDIO_C_LOCATION ("idio_meaning"));
		}
	    } else {
		idio_error_param_nil ("(environ-let)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_environ_unset == eh) {
	    /* (environ-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_environ_unset (eth, IDIO_PAIR_T (et), nametree, flags);
		} else {
		    idio_error_param_type ("symbol", eth, IDIO_C_LOCATION ("idio_meaning"));
		}
	    } else {
		idio_error_param_nil ("(environ-unset)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_monitor == eh) {
	    /* (monitor handler body ...) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_monitor (IDIO_PAIR_H (et), IDIO_PAIR_T (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(monitor)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else if (idio_S_include == eh) {
	    /* (include filename) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_include (IDIO_PAIR_H (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(include)", IDIO_C_LOCATION ("idio_meaning"));
		return idio_S_unspec;
	    }
	} else {
	    if (idio_isa_symbol (eh)) {
		IDIO k = idio_variable_kind (nametree, eh, IDIO_MEANING_LEXICAL_SCOPE (flags));

		if (idio_S_unspec != k) {
		    if (idio_S_false != idio_expanderp (eh)) {
			return idio_meaning_expander (e, nametree, flags);
		    }
		}
	    }
	    
	    return idio_meaning_application (eh, et, nametree, flags);
	}
    } else {
	if (idio_isa_symbol (e)) {
	    return idio_meaning_reference (e, nametree, IDIO_MEANING_LEXICAL_SCOPE (flags));
	} else {
	    return idio_meaning_quotation (e, nametree, flags);
	}
    }

    idio_error_C ("meaning", e, IDIO_C_LOCATION ("idio_meaning"));

    /* notreached */
    return idio_S_unspec;
}

IDIO idio_evaluate (IDIO e)
{
    /* idio_debug ("evaluate: e %s\n", e);  */

    /*
     * In the course of evaluating expressions we create a lot of
     * values in C-land which are not protected.  There is a strong
     * possibility of invoking the code of an expander which will call
     * the GC (in idio_vm_run()) which will, like as not, invalidate
     * all those unprotected values.  Which is bad because we wanted
     * to keep them...
     *
     * We could protect every individual value everywhere where a
     * future C-function call might run an expander or we could simply
     * pause the GC during the entirety of evaluation.
     *
     * The risk is we get a lot of cruft building up if the expression
     * being evaluated is complex, eg. (begin <contents of file>).
     *
     * It will all get cleaned up eventually when the GC is finally
     * called -- probably when the code is run shortly after returning
     * from here.
     */
    idio_gc_pause ();
    /* IDIO m = idio_meaning (e, idio_module_current_symbols (), 1); */

    /*
     * XXX
     *
     * This call to idio_meaning used to be with flags set to
     * IDIO_MEANING_FLAG_TAILP as it is obvious that this call is made
     * in tail position.
     *
     * However, with the advent of modules we get in a bit of a bind.
     * A TAILP function call will switch but not restore the
     * environment.  If {e} is a call to a function from outside of
     * this environment and calls any function in a non-TAILP position
     * then that second function will save and restore the definition
     * environment of the function in {e}.  But {e} won't restore what
     * we think is the current environment because {e} was in TAILP.
     *
     */
    IDIO m = idio_meaning (e, idio_S_nil, IDIO_MEANING_FLAG_NONE);
    idio_gc_resume ();

    /* idio_debug ("evaluate: m %s\n", m); */

    return m;
}

#define IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR(iname,cname) \
    IDIO_DEFINE_INFIX_OPERATOR (iname, cname, (IDIO op, IDIO before, IDIO args)) \
    {									\
	IDIO_ASSERT (op);						\
	IDIO_ASSERT (before);						\
	IDIO_ASSERT (args);						\
									\
	if (idio_S_nil != IDIO_PAIR_T (before)) {			\
	    idio_error_C ("too many args before " #iname, IDIO_LIST2 (before, args), idio_string_C (#iname)); \
	}								\
    									\
	if (idio_S_nil != args) {					\
	    IDIO after = IDIO_PAIR_H (args);				\
	    if (idio_S_nil == after) {					\
		idio_error_C ("too few args after " #iname, IDIO_LIST1 (before), idio_string_C (#iname)); \
	    }								\
	    if (idio_S_nil == IDIO_PAIR_T (after)) {			\
		after = IDIO_PAIR_H (after);				\
	    } else {							\
		after = idio_operator_expand (after, 0);		\
	    }								\
	    return IDIO_LIST3 (op, IDIO_PAIR_H (before), after);	\
	}								\
									\
	return idio_S_unspec;						\
    }

IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR ("=", set);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":=", colon_eq);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":+", colon_plus);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":*", colon_star);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":~", colon_tilde);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":$", colon_dollar);

IDIO_DEFINE_PRIMITIVE1 ("environ?", environp, (IDIO name))
{
    IDIO_ASSERT (name);
    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    IDIO r = idio_S_false;

    IDIO sk = idio_module_env_symbol_recurse (name);
    
    if (idio_S_unspec != sk &&
	idio_S_environ == IDIO_PAIR_H (sk)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("dynamic?", dynamicp, (IDIO name))
{
    IDIO_ASSERT (name);
    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    IDIO r = idio_S_false;

    IDIO sk = idio_module_env_symbol_recurse (name);

    if (idio_S_unspec != sk &&
	idio_S_dynamic == IDIO_PAIR_H (sk)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("computed?", computedp, (IDIO name))
{
    IDIO_ASSERT (name);
    IDIO_VERIFY_PARAM_TYPE (symbol, name);

    IDIO r = idio_S_false;

    IDIO sk = idio_module_env_symbol_recurse (name);

    if (idio_S_unspec != sk &&
	idio_S_computed == IDIO_PAIR_H (sk)) {
	r = idio_S_true;
    }

    return r;
}

void idio_init_evaluate ()
{
    idio_evaluation_module = idio_module (idio_symbols_C_intern ("*evaluation*"));
    /* idio_evaluation_module = idio_Idio_module_instance (); */

    idio_expander_module = idio_module (idio_symbols_C_intern ("*expander*"));
    
    idio_expander_list = idio_symbols_C_intern ("*expander-list*");
    idio_module_set_symbol_value (idio_expander_list, idio_S_nil, idio_expander_module);

    idio_expander_list_src = idio_symbols_C_intern ("*expander-list-src*");
    idio_module_set_symbol_value (idio_expander_list_src, idio_S_nil, idio_expander_module);

    idio_operator_module = idio_module (idio_symbols_C_intern ("*operator*"));

    idio_infix_operator_list = idio_symbols_C_intern ("*infix-operator-list*");
    idio_module_set_symbol_value (idio_infix_operator_list, idio_S_nil, idio_operator_module);

    idio_infix_operator_group = idio_symbols_C_intern ("*infix-operator-group*");
    idio_module_set_symbol_value (idio_infix_operator_group, idio_S_nil, idio_operator_module);

    idio_postfix_operator_list = idio_symbols_C_intern ("*postfix-operator-list*");
    idio_module_set_symbol_value (idio_postfix_operator_list, idio_S_nil, idio_operator_module);

    idio_postfix_operator_group = idio_symbols_C_intern ("*postfix-operator-group*");
    idio_module_set_symbol_value (idio_postfix_operator_group, idio_S_nil, idio_operator_module);
}

void idio_evaluate_add_primitives ()
{
    idio_expander_thread = idio_thread (40);
    idio_gc_protect (idio_expander_thread);

    /* IDIO_THREAD_MODULE (idio_expander_thread) = idio_expander_module; */
    IDIO_THREAD_PC (idio_expander_thread) = 1;

    IDIO_ADD_PRIMITIVE (expanderp);
    IDIO_ADD_EXPANDER (let);
    IDIO_ADD_EXPANDER (lets);
    IDIO_ADD_EXPANDER (letrec);
    IDIO_ADD_PRIMITIVE (macro_expand);

    IDIO_ADD_PRIMITIVE (infix_operatorp);
    IDIO_ADD_PRIMITIVE (infix_operator_expand);
    IDIO_ADD_PRIMITIVE (postfix_operatorp);
    IDIO_ADD_PRIMITIVE (postfix_operator_expand);
    IDIO_ADD_PRIMITIVE (operatorp);
    IDIO_ADD_PRIMITIVE (operator_expand);

    IDIO_ADD_INFIX_OPERATOR (set, 1000); 
    IDIO_ADD_INFIX_OPERATOR (colon_eq, 1000);  
    IDIO_ADD_INFIX_OPERATOR (colon_plus, 1000);  
    IDIO_ADD_INFIX_OPERATOR (colon_star, 1000);  
    IDIO_ADD_INFIX_OPERATOR (colon_tilde, 1000);  
    IDIO_ADD_INFIX_OPERATOR (colon_dollar, 1000);  

    IDIO_ADD_PRIMITIVE (environp);
    IDIO_ADD_PRIMITIVE (dynamicp);
    IDIO_ADD_PRIMITIVE (computedp);
}

void idio_final_evaluate ()
{
    idio_gc_expose (idio_expander_thread);
}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: undecided-unix */
/* End: */
