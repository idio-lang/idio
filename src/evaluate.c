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
 * evaluate.c
 *
 */

#include "idio.h"

/*
 * There are three layers of environment in which you might find a
 * variable.  idio_meaning_variable_kind is used to return an
 * indication as to what sort of variable it is and some useful detail
 * about it.

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

static IDIO idio_evaluation_module = idio_S_nil;

static IDIO idio_meaning_error_location (IDIO lo)
{
    IDIO_ASSERT (lo);
    IDIO_TYPE_ASSERT (struct_instance, lo);

    IDIO lsh = idio_open_output_string_handle_C ();
    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME), lsh);
    idio_display_C (":line ", lsh);
    idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE), lsh);

    return idio_get_output_string (lsh);
}

static void idio_warning_static_undefineds (IDIO diff)
{
    IDIO_ASSERT (diff);
    IDIO_TYPE_ASSERT (pair, diff);

    idio_debug ("WARNING: undefined variables: %s\n", diff);
}

void idio_meaning_error_static_redefine (IDIO lo, IDIO c_location, char *msg, IDIO name, IDIO cv, IDIO new)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cv);
    IDIO_ASSERT (new);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (lo);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (msg, dsh);
    idio_display_C (": ", dsh);
    idio_display (name, dsh);
    idio_display_C (" is currently ", dsh);
    idio_display (cv, dsh);
    idio_display_C (": proposed: ", dsh);
    idio_display (new, dsh);

#ifdef IDIO_DEBUG
    idio_display_C (": ", dsh);
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_st_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh),
					       name));
    idio_raise_condition (idio_S_true, c);
}

static void idio_meaning_error_static_variable (IDIO lo, IDIO c_location, char *msg, IDIO name)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (lo);

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (c_location, sh);

    detail = idio_get_output_string (sh);
#endif

    IDIO c = idio_struct_instance (idio_condition_st_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       name));
    idio_raise_condition (idio_S_true, c);
}

static void idio_meaning_error_static_unbound (IDIO lo, IDIO c_location, IDIO name)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    idio_meaning_error_static_variable (lo, c_location, "unbound", name);
}

static void idio_meaning_error_static_immutable (IDIO lo, IDIO c_location, IDIO name)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    idio_meaning_error_static_variable (lo, c_location, "immutable", name);
}

void idio_meaning_error_static_arity (IDIO lo, IDIO c_location, char *msg, IDIO args)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (list, args);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (lo);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (args, dsh);

#ifdef IDIO_DEBUG
    idio_display_C (": ", dsh);
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_st_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh)));
    idio_raise_condition (idio_S_true, c);
}

static void idio_meaning_error_static_primitive_arity (IDIO lo, IDIO c_location, char *msg, IDIO f, IDIO args, IDIO primdata)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (f);
    IDIO_ASSERT (args);
    IDIO_ASSERT (primdata);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, f);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (primitive, primdata);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (lo);

    IDIO dsh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "ARITY != %" PRId8 "%s; primitive (", IDIO_PRIMITIVE_ARITY (primdata), IDIO_PRIMITIVE_VARARGS (primdata) ? "+" : "");
    idio_display_C (em, dsh);
    idio_display (f, dsh);
    idio_display_C (" ", dsh);
    IDIO sigstr = idio_property_get (primdata, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));
    idio_display (sigstr, dsh);
    idio_display_C (") was called as (", dsh);
    idio_display (f, dsh);
    if (idio_S_nil != args) {
	idio_display_C (" ", dsh);
	char *s = idio_display_string (args);
	idio_display_C_len (s + 1, strlen (s) - 2, dsh);
	free (s);
    }
    idio_display_C (")", dsh);

#ifdef IDIO_DEBUG
    idio_display_C (": ", dsh);
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_st_function_arity_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh)));
    idio_raise_condition (idio_S_true, c);
}

static IDIO idio_meaning_predef_extend (IDIO name, IDIO primdata, IDIO module, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (primdata);
    IDIO_ASSERT (module);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (primitive, primdata);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (array, cs);

    IDIO cv = idio_module_find_symbol (name, module);
    if (idio_S_unspec != cv) {
	IDIO fgvi = IDIO_PAIR_HTT (cv);
	IDIO pd = idio_vm_values_ref (IDIO_FIXNUM_VAL (fgvi));

	if (IDIO_PRIMITIVE_F (primdata) != IDIO_PRIMITIVE_F (pd)) {
	    idio_debug ("WARNING: predef-extend: %s: ", name);
	    fprintf (stderr, "%p -> %p\n", IDIO_PRIMITIVE_F (pd), IDIO_PRIMITIVE_F (primdata));

	    IDIO lo = idio_struct_instance (idio_lexobj_type,
					    idio_pair (idio_string_C (cpp__FILE__),
					    idio_pair (idio_integer (cpp__LINE__),
					    idio_pair (idio_integer (0),
					    idio_pair (name,
					    idio_S_nil)))));

	    idio_meaning_error_static_redefine (lo, IDIO_C_FUNC_LOCATION(), "primitive value change", name, pd, primdata);
	} else {
	    return fgvi;
	}
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_set_vvi (module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_predef, fmci, fgvi, module, idio_string_C ("idio_meaning_predef_extend")), module);

    /*
     * idio_module_set_symbol_value() is a bit sniffy about setting
     * predefs -- rightly so -- so go under the hood!
     */
    idio_vm_values_set (gvi, primdata);

    return fgvi;
}

IDIO idio_add_module_primitive (IDIO module, idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_ASSERT (module);
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (array, cs);

    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, sym);
    IDIO fmci = idio_fixnum (mci);
    idio_module_set_vci (module, fmci, fmci);

    return idio_meaning_predef_extend (sym, primdata, module, cs, cpp__FILE__, cpp__LINE__);
}

IDIO idio_export_module_primitive (IDIO module, idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_ASSERT (module);
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (array, cs);

    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, sym);
    IDIO fmci = idio_fixnum (mci);
    idio_module_set_vci (module, fmci, fmci);

    IDIO_MODULE_EXPORTS (module) = idio_pair (sym, IDIO_MODULE_EXPORTS (module));
    return idio_meaning_predef_extend (sym, primdata, module, cs, cpp__FILE__, cpp__LINE__);
}

IDIO idio_add_primitive (idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    return idio_add_module_primitive (idio_primitive_module_instance (), d, cs, cpp__FILE__, cpp__LINE__);
}

IDIO idio_toplevel_extend (IDIO lo, IDIO name, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

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

	return idio_S_notreached;
    }

    IDIO cv = idio_module_find_symbol (name, cm);
    if (idio_S_unspec != cv) {
	IDIO curkind = IDIO_PAIR_H (cv);
	if (kind != curkind) {
	    if (idio_S_predef != curkind) {
		idio_meaning_error_static_redefine (lo, IDIO_C_LOCATION ("idio_toplevel_extend"), "toplevel-extend: type change", name, cv, kind);

		return idio_S_notreached;
	    }
	} else {
	    return IDIO_PAIR_HT (cv);
	}
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    /*
     * NB a vi of 0 indicates an unresolved value index to be resolved
     * (based on the current set of imports) during runtime
     */
    idio_module_set_symbol (name, IDIO_LIST5 (kind, fmci, idio_fixnum (0), cm, idio_string_C ("idio_toplevel_extend")), cm);

    return fmci;
}

IDIO idio_environ_extend (IDIO lo, IDIO name, IDIO val, IDIO cs)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (val);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (array, cs);

    IDIO im = idio_Idio_module_instance ();

    IDIO sk = idio_module_find_symbol (name, im);

    if (idio_S_unspec != sk) {
	IDIO kind = IDIO_PAIR_H (sk);
	IDIO fmci = IDIO_PAIR_HT (sk);

	if (idio_S_environ != kind) {
	    idio_meaning_error_static_redefine (lo, IDIO_C_LOCATION ("idio_environ_extend"), "environ-extend: type change", name, sk, kind);

	    return idio_S_notreached;
	} else {
	    return fmci;
	}
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_set_vci (im, fmci, fmci);
    idio_module_set_vvi (im, fmci, fgvi);
    sk = IDIO_LIST5 (idio_S_environ, fmci, fgvi, im, idio_string_C ("idio_environ_extend"));
    idio_module_set_symbol (name, sk, im);
    idio_module_set_symbol_value (name, val, im);

    return fmci;
}

static IDIO idio_meaning_variable_localp (IDIO lo, IDIO nametree, size_t i, IDIO name)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (struct_instance, lo);
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
			idio_meaning_error_static_variable (lo, IDIO_C_LOCATION ("idio_meaning_variable_localp"), "unexpected local variant", name);

			return idio_S_notreached;
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
		idio_error_C ("unexpected localp", IDIO_LIST2 (name, nametree), IDIO_C_LOCATION ("idio_meaning_variable_localp"));

		return idio_S_notreached;

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

static IDIO idio_meaning_variable_kind (IDIO lo, IDIO nametree, IDIO name, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO r = idio_meaning_variable_localp (lo, nametree, 0, name);

    if (idio_S_nil == r) {
	/*
	 * NOTICE This must be a recursive lookup.  Otherwise we'll
	 * not see any bindings in Idio or *primitives*
	 */
        r = idio_module_find_symbol_recurse (name, cm, 1);
	if (idio_S_unspec == r) {

	    r = idio_module_direct_reference (name);

	    if (idio_S_unspec != r) {
		r = IDIO_PAIR_HTT (r);
		idio_module_set_symbol (name, r, cm);
	    } else {
		/*
		 * auto-extend the toplevel with this unknown variable
		 * -- we should (eventually) see a definition for it
		 */
		idio_toplevel_extend (lo, name, flags, cs, cm);
		r = idio_module_find_symbol (name, cm);
	    }
	}
    }

    return r;
}

static IDIO idio_meaning (IDIO lo, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_reference (IDIO lo, IDIO name, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO sk = idio_meaning_variable_kind (lo, nametree, name, flags, cs, cm);

    if (idio_S_unspec == sk) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_meaning_error_static_unbound (lo, IDIO_C_LOCATION ("idio_meaning_reference"), name);

	return idio_S_notreached;
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
	IDIO fgvi = IDIO_PAIR_HTT (sk);
	if (IDIO_FIXNUM_VAL (fgvi) > 0) {
	    return IDIO_LIST2 (idio_I_CHECKED_GLOBAL_REF, fmci);
	} else {
	    return IDIO_LIST2 (idio_I_GLOBAL_REF, fmci);
	}
    } else if (idio_S_dynamic == kind) {
	return IDIO_LIST2 (idio_I_DYNAMIC_REF, fmci);
    } else if (idio_S_environ == kind) {
	return IDIO_LIST2 (idio_I_ENVIRON_REF, fmci);
    } else if (idio_S_computed == kind) {
	return IDIO_LIST2 (idio_I_COMPUTED_REF, fmci);
    } else if (idio_S_predef == kind) {
	IDIO fgvi = IDIO_PAIR_HTT (sk);
	return IDIO_LIST2 (idio_I_PREDEFINED, fgvi);
    } else {
	idio_meaning_error_static_unbound (lo, IDIO_C_LOCATION ("idio_meaning_reference"), name);

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_function_reference (IDIO lo, IDIO name, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO sk = idio_meaning_variable_kind (lo, nametree, name, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);

    if (idio_S_unspec == sk) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_meaning_error_static_unbound (lo, IDIO_C_LOCATION ("idio_meaning_function_reference"), name);

	return idio_S_notreached;
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
	IDIO fgvi = IDIO_PAIR_HTT (sk);
	if (IDIO_FIXNUM_VAL (fgvi) > 0) {
	    return IDIO_LIST2 (idio_I_CHECKED_GLOBAL_FUNCTION_REF, fmci);
	} else {
	    return IDIO_LIST2 (idio_I_GLOBAL_FUNCTION_REF, fmci);
	}
    } else if (idio_S_dynamic == kind) {
	return IDIO_LIST2 (idio_I_DYNAMIC_FUNCTION_REF, fmci);
    } else if (idio_S_environ == kind) {
	return IDIO_LIST2 (idio_I_ENVIRON_REF, fmci);
    } else if (idio_S_computed == kind) {
	return IDIO_LIST2 (idio_I_COMPUTED_REF, fmci);
    } else if (idio_S_predef == kind) {
	IDIO fgvi = IDIO_PAIR_HTT (sk);
	return IDIO_LIST2 (idio_I_PREDEFINED, fgvi);
    } else {
	idio_meaning_error_static_unbound (lo, IDIO_C_LOCATION ("idio_meaning_function_reference"), name);

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_escape (IDIO lo, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning (lo, e, nametree, flags, cs, cm);
}

static IDIO idio_meaning_quotation (IDIO lo, IDIO v, IDIO nametree, int flags)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (v);
    IDIO_ASSERT (nametree);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_CONSTANT, v);
}

static IDIO idio_meaning_dequasiquote (IDIO lo, IDIO e, int level)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);

    IDIO_TYPE_ASSERT (struct_instance, lo);

    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	if (idio_S_quasiquote == eh) {
	    /* ('list ''quasiquote (de-qq (pht e) (+ level 1))) */
	    return IDIO_LIST3 (idio_S_list,
			       IDIO_LIST2 (idio_S_quote, idio_S_quasiquote),
			       idio_meaning_dequasiquote (lo, IDIO_PAIR_HT (e), level + 1));
	} else if (idio_S_unquote == eh) {
	    if (level <= 0) {
		return IDIO_PAIR_HT (e);
	    } else {
		/* ('list ''unquote (de-qq (pht e) (- level 1))) */
		return IDIO_LIST3 (idio_S_list,
				   IDIO_LIST2 (idio_S_quote, idio_S_unquote),
				   idio_meaning_dequasiquote (lo, IDIO_PAIR_HT (e), level - 1));
	    }
	} else if (idio_S_unquotesplicing == eh) {
	    if (level <= 0) {
		return IDIO_LIST3 (idio_S_pair,
				   idio_meaning_dequasiquote (lo, IDIO_PAIR_H (e),level),
				   idio_meaning_dequasiquote (lo, IDIO_PAIR_T (e), level));
	    } else {
		/* ('list ''unquotesplicing (de-qq (pht e) (- level 1))) */
		return IDIO_LIST3 (idio_S_list,
				   IDIO_LIST2 (idio_S_quote, idio_S_unquotesplicing),
				   idio_meaning_dequasiquote (lo, IDIO_PAIR_HT (e), level - 1));
	    }
	} else if (level <= 0 &&
		   idio_isa_pair (IDIO_PAIR_H (e)) &&
		   idio_S_unquotesplicing == IDIO_PAIR_HH (e)) {
	    if (idio_S_nil == IDIO_PAIR_T (e)) {
		return IDIO_PAIR_HTH (e);
	    } else {
		/* ('append (phth e) (de-qq (pt e) level)) */
		return IDIO_LIST3 (idio_S_append,
				   IDIO_PAIR_HTH (e),
				   idio_meaning_dequasiquote (lo, IDIO_PAIR_T (e), level));
	    }
	} else {
	    return IDIO_LIST3 (idio_S_pair,
			       idio_meaning_dequasiquote (lo, IDIO_PAIR_H (e), level),
			       idio_meaning_dequasiquote (lo, IDIO_PAIR_T (e), level));
	}
    } else if (idio_isa_array (e)) {
	return IDIO_LIST2 (idio_symbols_C_intern ("list->array"), idio_meaning_dequasiquote (lo, idio_array_to_list (e), level));
    } else if (idio_isa_symbol (e)) {
	return IDIO_LIST2 (idio_S_quote, e);
    } else {
	return e;
    }

    idio_error_C ("", IDIO_LIST1 (e), IDIO_C_LOCATION ("idio_meaning_dequasiquote"));

    return idio_S_notreached;
}

static IDIO idio_meaning_quasiquotation (IDIO lo, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO dq = idio_meaning_dequasiquote (lo, e, 0);

    return idio_meaning (lo, dq, nametree, flags, cs, cm);
}

static IDIO idio_meaning_alternative (IDIO lo, IDIO e1, IDIO e2, IDIO e3, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e1);
    IDIO_ASSERT (e2);
    IDIO_ASSERT (e3);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m1 = idio_meaning (lo, e1, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO m2 = idio_meaning (lo, e2, nametree, flags, cs, cm);
    IDIO m3 = idio_meaning (lo, e3, nametree, flags, cs, cm);

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

	return idio_S_notreached;
    } else if (! idio_isa_pair (IDIO_PAIR_H (c))) {
	/* idio_debug ("pair/pair: c %s\n", c); */
	idio_error_param_type ("pair/pair", c, IDIO_C_LOCATION ("idio_rewrite_cond"));

	return idio_S_notreached;
    } else if (idio_S_else == IDIO_PAIR_HH (c)) {
	/* fprintf (stderr, "cond-rewrite: else clause\n");  */
	if (idio_S_nil == IDIO_PAIR_T (c)) {
	    return idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_TH (c));
	} else {
	    idio_error_C ("cond: else not in last clause", c, IDIO_C_LOCATION ("idio_rewrite_cond"));

	    return idio_S_notreached;
	}
    }

    if (idio_isa_pair (IDIO_PAIR_TH (c)) &&
	idio_S_eq_gt == IDIO_PAIR_HTH (c)) {
	/* fprintf (stderr, "cond-rewrite: => clause\n");  */
	if (idio_isa_list (IDIO_PAIR_H (c)) &&
	    idio_list_length (IDIO_PAIR_H (c)) == 3) {
	    IDIO gs = idio_gensym (NULL);
	    /*
	      `(let ((gs ,(phh c)))
	         (if gs
	             (,(phtth c) gs)
	             ,(rewrite-cond-clauses (pt c))))
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

	    return idio_S_notreached;
	}
    } else if (idio_S_nil == IDIO_PAIR_TH (c)) {
	/* fprintf (stderr, "cond-rewrite: null? pth clause\n");  */
	IDIO gs = idio_gensym (NULL);
	/*
	  `(let ((gs ,(phh c)))
	     (or gs
	         ,(rewrite-cond-clauses (pt c))))
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

static IDIO idio_meaning_assignment (IDIO lo, IDIO name, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (name)) {
	/*
	 * set! (foo x y z) v
	 *
	 * `((setter ,(ph name)) ,@(pt name) ,e)
	 */
	IDIO se = idio_list_append2 (IDIO_LIST1 (IDIO_LIST2 (idio_S_setter,
							     IDIO_PAIR_H (name))),
				     IDIO_PAIR_T (name));
	se = idio_list_append2 (se, IDIO_LIST1 (e));
	return idio_meaning (lo,
			     se,
			     nametree,
			     IDIO_MEANING_NO_DEFINE (flags),
			     cs,
			     cm);
    } else if (! idio_isa_symbol (name)) {
	idio_error_C ("cannot assign to", name, IDIO_C_LOCATION ("idio_meaning_assignment"));

	return idio_S_notreached;
    }

    /*
     * Normal assignment to a symbol
     */

    IDIO m = idio_meaning (lo, e, nametree, IDIO_MEANING_NO_DEFINE (IDIO_MEANING_NOT_TAILP (flags)), cs, cm);

    IDIO sk = idio_meaning_variable_kind (lo, nametree, name, flags, cs, cm);

    if (idio_S_unspec == sk) {
	idio_error_C ("unknown variable:", name, IDIO_C_LOCATION ("idio_meaning_assignment"));

	return idio_S_notreached;
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
	    return IDIO_LIST2 (IDIO_LIST4 (idio_I_GLOBAL_DEF, name, kind, fmci),
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
	IDIO new_mci = idio_toplevel_extend (lo, name, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);

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
	idio_module_set_symbol_value (name, idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi)), cm);
	assign = IDIO_LIST3 (idio_I_GLOBAL_SET, new_mci, m);
	fmci = new_mci;

	/* if we weren't allowing shadowing */

	/* idio_meaning_error_static_immutable (name, IDIO_C_LOCATION ("idio_meaning_assignment")); */
	/* return idio_S_notreached; */
    } else {
	idio_meaning_error_static_unbound (lo, IDIO_C_LOCATION ("idio_meaning_assignment"), name);

	return idio_S_notreached;
    }

    if (IDIO_MEANING_IS_DEFINE (flags)) {
	return IDIO_LIST2 (IDIO_LIST4 (idio_I_GLOBAL_DEF, name, kind, fmci),
			   assign);
    } else {
	return assign;
    }
}

static IDIO idio_meaning_define (IDIO lo, IDIO name, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

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

    return idio_meaning_assignment (lo, name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_LEXICAL_SCOPE (flags)), cs, cm);
}

static IDIO idio_meaning_define_macro (IDIO lo, IDIO name, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /* idio_debug ("meaning-define-macro:\nname=%s\n", name); */
    /* idio_debug ("e=%s\n", e); */

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
     * create an expander: (function (x e) (apply proc (pt x)))
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
     * with an expander and that expander takes the pt of the
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
     * take the pt of its first argument.  Left alone, expander "b"
     * will take the pt then expander "%b" will take the pt....  "A PT
     * Too Far", one would say, in hindsight and thinking of the big
     * budget movie potential.
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
	    return idio_meaning_assignment (lo, name, IDIO_PAIR_H (exp), nametree, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);
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

    /* idio_debug ("meaning-define-macro: expander=%s\n", expander); */

    IDIO m_a = idio_meaning_assignment (lo, name, expander, nametree,
					IDIO_MEANING_NOT_TAILP (IDIO_MEANING_DEFINE (IDIO_MEANING_LEXICAL_SCOPE (flags))),
					cs, cm);

    /* idio_debug ("meaning-define-macro: meaning=%s\n", m_a); */

    idio_install_expander_source (name, expander, expander);

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    IDIO ce = idio_thread_current_env ();

    idio_module_set_vci (ce, fmci, fmci);
    idio_module_set_vvi (ce, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, ce, idio_string_C ("idio_meaning_define_macro")), ce);

    /*
     * Careful!  Evaluate the expander code after we've called
     * idio_module_set_symbol() for the symbol.  For normal code this
     * wouldn't matter but macros are evaluated during compilation.
     */
    idio_evaluate_expander_code (m_a, cs);

    /*
     * NB.  This effectively creates/stores the macro body code a
     * second time *in this instance of the engine*.  When the object
     * code is read in there won't be an instance of the macro body
     * code lying around -- at least not one we can access.
     */

    IDIO r = IDIO_LIST3 (idio_I_EXPANDER, idio_fixnum (mci), m_a);
    /* idio_debug ("idio-meaning-define-macro %s", name); */
    /* idio_debug (" r=%s\n", r); */
    return r;
}

static IDIO idio_meaning_define_infix_operator (IDIO lo, IDIO name, IDIO pri, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /* idio_debug ("define-infix-operator: %s", name); */
    /* idio_debug (" %s\n", e); */

    if (IDIO_FIXNUM_VAL (pri) < 0) {
	idio_error_param_type ("positive fixnum", pri, IDIO_C_LOCATION ("idio_meaning_define_infix_operator"));

	return idio_S_notreached;
    }

    /*
     * Step 1: find the existing symbol for {name} or create a new one
     */
    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_set_vci (idio_operator_module, fmci, fmci);
    idio_module_set_vvi (idio_operator_module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, idio_operator_module, idio_string_C ("idio_meaning_define_infix_operator")), idio_operator_module);

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

	    return idio_S_notreached;
	}

	IDIO sve = IDIO_LIST3 (idio_symbols_C_intern ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       IDIO_LIST2 (idio_symbols_C_intern ("find-module"),
					   IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module))));

	m = idio_meaning (lo, sve, nametree, flags, cs, cm);
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

	m = idio_meaning (lo, fe, nametree, flags, cs, cm);
    }
    IDIO r = IDIO_LIST4 (idio_I_INFIX_OPERATOR, fmci, pri, m);

    /*
     * NB.  idio_evaluate_infix_operator_code will invoke
     * idio_install_infix_operator (see IDIO_A_INFIX_OPERATOR in vm.c).
     */
    IDIO cl = idio_evaluate_infix_operator_code (r, cs);

    /*
     * Step 3: insert the value "live"
     */
    idio_module_set_symbol_value (name, cl, idio_operator_module);

    return r;
}

static IDIO idio_meaning_define_postfix_operator (IDIO lo, IDIO name, IDIO pri, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (IDIO_FIXNUM_VAL (pri) < 0) {
	idio_error_param_type ("positive fixnum", pri, IDIO_C_LOCATION ("idio_meaning_define_postfix_operator"));

	return idio_S_notreached;
    }

    /*
     * Step 1: find the existing symbol for {name} or create a new one
     */
    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_set_vci (idio_operator_module, fmci, fmci);
    idio_module_set_vvi (idio_operator_module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_toplevel, fmci, fgvi, idio_operator_module, idio_string_C ("idio_meaning_define_postfix_operator")), idio_operator_module);

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

	    return idio_S_notreached;
	}

	IDIO sve = IDIO_LIST3 (idio_symbols_C_intern ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       IDIO_LIST2 (idio_symbols_C_intern ("find-module"),
					   IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module))));

	m = idio_meaning (lo, sve, nametree, flags, cs, cm);
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

	m = idio_meaning (lo, fe, nametree, flags, cs, cm);
    }
    IDIO r = IDIO_LIST4 (idio_I_POSTFIX_OPERATOR, fmci, pri, m);

    /*
     * NB.  idio_evaluate_postfix_operator_code will invoke
     * idio_install_postfix_operator (see IDIO_A_POSTFIX_OPERATOR in vm.c).
     */
    IDIO cl = idio_evaluate_postfix_operator_code (r, cs);

    /*
     * Step 3: insert the value "live"
     */
    idio_module_set_symbol_value (name, cl, idio_operator_module);

    return r;
}

static IDIO idio_meaning_define_dynamic (IDIO lo, IDIO name, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
    }

    return idio_meaning_assignment (lo, name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_DYNAMIC_SCOPE (flags)), cs, cm);
}

static IDIO idio_meaning_define_environ (IDIO lo, IDIO name, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
    }

    return idio_meaning_assignment (lo, name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_ENVIRON_SCOPE (flags)), cs, cm);
}

/*
 * A computed variable's value should be a pair, (getter & setter).
 * Either of getter or setter can be #n.
 *
 * We shouldn't have both #n as it wouldn't be much use.
 */
static IDIO idio_meaning_define_computed (IDIO lo, IDIO name, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

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
	idio_meaning_error_static_variable (lo, IDIO_C_LOCATION ("idio_meaning_define_computed"), "define-computed name getter setter", name);

	return idio_S_notreached;
    }

    if (idio_S_nil == getter &&
	idio_S_nil == setter) {
	idio_meaning_error_static_variable (lo, IDIO_C_LOCATION ("idio_meaning_define_computed"), "define-computed name: both setter and getter are #n", name);

	return idio_S_notreached;
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
    return idio_meaning_assignment (lo,
				    name,
				    IDIO_LIST3 (idio_S_pair, getter, setter),
				    nametree,
				    IDIO_MEANING_DEFINE (IDIO_MEANING_COMPUTED_SCOPE (flags)),
				    cs,
				    cm);
}

static IDIO idio_meaning_sequence (IDIO lo, IDIO ep, IDIO nametree, int flags, IDIO keyword, IDIO cs, IDIO cm);

static IDIO idio_meanings_single_sequence (IDIO lo, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning (lo, e, nametree, flags, cs, cm);
}

static IDIO idio_meanings_multiple_sequence (IDIO lo, IDIO e, IDIO ep, IDIO nametree, int flags, IDIO keyword, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (lo, e, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO mp = idio_meaning_sequence (lo, ep, nametree, flags, keyword, cs, cm);

    if (idio_S_and == keyword) {
	return IDIO_LIST3 (idio_I_AND, m, mp);
    } else if (idio_S_or == keyword) {
	return IDIO_LIST3 (idio_I_OR, m, mp);
    } else if (idio_S_begin == keyword) {
	return IDIO_LIST3 (idio_I_BEGIN, m, mp);
    } else {
	idio_error_C ("unexpected sequence keyword", keyword, IDIO_C_LOCATION ("idio_meanings_multiple_sequence"));

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_sequence (IDIO lo, IDIO ep, IDIO nametree, int flags, IDIO keyword, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

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
	    IDIO c = idio_S_nil;
	    if (idio_S_and == keyword) {
		c = idio_I_AND;
	    } else if (idio_S_or == keyword) {
		c = idio_I_OR;
	    } else if (idio_S_begin == keyword) {
		c = idio_I_BEGIN;
	    } else {
		idio_error_C ("unexpected sequence keyword", keyword, IDIO_C_LOCATION ("idio_meaning_sequence"));

		return idio_S_notreached;
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

		IDIO m = idio_meaning (lo, e, nametree, seq_flags, cs, cm);
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
	    return idio_meanings_single_sequence (lo, eph, nametree, flags, cs, cm);
	}
    }

    /*
     * We can get here for the x in the bindings of

     * (define (list . x) x)
     */
    return idio_meaning (lo, ep, nametree, flags, cs, cm);
}

static IDIO idio_meaning_fix_abstraction (IDIO lo, IDIO ns, IDIO sigstr, IDIO docstr, IDIO ep, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (sigstr);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, ns);

    IDIO mp = idio_meaning_sequence (lo, ep, nt2, IDIO_MEANING_SET_TAILP (flags), idio_S_begin, cs, cm);

    return IDIO_LIST5 (idio_I_FIX_CLOSURE, mp, idio_fixnum (arity), sigstr, docstr);
}

static IDIO idio_meaning_dotted_abstraction (IDIO lo, IDIO ns, IDIO n, IDIO sigstr, IDIO docstr, IDIO ep, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (sigstr);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append2 (ns, IDIO_LIST1 (n)));
    IDIO mp = idio_meaning_sequence (lo, ep, nt2, IDIO_MEANING_SET_TAILP (flags), idio_S_begin, cs, cm);

    return IDIO_LIST5 (idio_I_NARY_CLOSURE, mp, idio_fixnum (arity), sigstr, docstr);
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
		   idio_S_excl_star == IDIO_PAIR_H (cur)) {
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
		   idio_S_excl_tilde == IDIO_PAIR_H (cur)) {
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

	    return idio_S_notreached;
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
	    idio_debug ("e=%s\n", e);
	    idio_error_warning_message ("rewrite-body-letrec: empty body");

	    return idio_S_notreached;
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

	    return idio_S_notreached;
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
		IDIO ns = idio_list_map_ph (defs);
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

static IDIO idio_meaning_abstraction (IDIO lo, IDIO nns, IDIO docstr, IDIO ep, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (nns);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    ep = idio_rewrite_body (ep);

    IDIO ns = nns;
    IDIO regular = idio_S_nil;

    /*
     * signature string: (a b) -> "(a b)" and we don't want the parens
     */
    char *sigstr_C = idio_display_string (nns);
    IDIO sigstr = idio_S_nil;
    if ('(' == *sigstr_C) {
	size_t blen = strlen (sigstr_C);

	if (blen < 2) {
	    idio_error_C ("meaning-abstraction sigstr", IDIO_LIST2 (nns, ep), IDIO_C_LOCATION ("idio_meaning_abstraction"));

	    return idio_S_notreached;
	}
	sigstr = idio_string_C_len (sigstr_C + 1, blen -2);
    }
    free (sigstr_C);

    for (;;) {
	if (idio_isa_pair (ns))  {
	    regular = idio_pair (IDIO_PAIR_H (ns), regular);
	    ns = IDIO_PAIR_T (ns);
	} else if (idio_S_nil == ns) {
	    return idio_meaning_fix_abstraction (lo, nns, sigstr, docstr, ep, nametree, flags, cs, cm);
	} else {
	    return idio_meaning_dotted_abstraction (lo, idio_list_reverse (regular), ns, sigstr, docstr, ep, nametree, flags, cs, cm);
	}
    }

    idio_error_C ("meaning-abstraction", IDIO_LIST2 (nns, ep), IDIO_C_LOCATION ("idio_meaning_abstraction"));

    return idio_S_notreached;
}

static IDIO idio_meaning_block (IDIO lo, IDIO es, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    es = idio_rewrite_body (es);

    return idio_meaning_sequence (lo, es, nametree, flags, idio_S_begin, cs, cm);
}

static IDIO idio_meanings (IDIO lo, IDIO es, IDIO nametree, size_t size, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_some_arguments (IDIO lo, IDIO e, IDIO es, IDIO nametree, size_t size, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (lo, e, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO ms = idio_meanings (lo, es, nametree, size, flags, cs, cm);
    size_t rank = size - (idio_list_length (es) + 1);

    return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, idio_fixnum (rank));
}

static IDIO idio_meaning_no_argument (IDIO lo, IDIO nametree, size_t size, int flags)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (nametree);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, idio_fixnum (size));
}

static IDIO idio_meanings (IDIO lo, IDIO es, IDIO nametree, size_t size, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (es)) {
	return idio_meaning_some_arguments (lo, IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, flags, cs, cm);
    } else {
	return idio_meaning_no_argument (lo, nametree, size, flags);
    }
}

static IDIO idio_meaning_fix_closed_application (IDIO lo, IDIO ns, IDIO body, IDIO es, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    body = idio_rewrite_body (body);

    IDIO ms = idio_meanings (lo, es, nametree, idio_list_length (es), IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO nt2 = idio_nametree_extend (nametree, ns);
    IDIO mbody = idio_meaning_sequence (lo, body, nt2, flags, idio_S_begin, cs, cm);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_meaning_dotteds (IDIO lo, IDIO es, IDIO nametree, size_t size, size_t arity, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_some_dotted_arguments (IDIO lo, IDIO e, IDIO es, IDIO nametree, size_t size, size_t arity, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (lo, e, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO ms = idio_meaning_dotteds (lo, es, nametree, size, arity, flags, cs, cm);
    size_t rank = size - (idio_list_length (es) + 1);

    if (rank < arity) {
	return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, idio_fixnum (rank));
    } else {
	return IDIO_LIST4 (idio_I_CONS_ARGUMENT, m, ms, idio_fixnum (arity));
    }
}

static IDIO idio_meaning_no_dotted_argument (IDIO lo, IDIO nametree, size_t size, size_t arity, int flags)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (nametree);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, idio_fixnum (arity));
}

static IDIO idio_meaning_dotteds (IDIO lo, IDIO es, IDIO nametree, size_t size, size_t arity, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (es)) {
	return idio_meaning_some_dotted_arguments (lo, IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, arity, flags, cs, cm);
    } else {
	return idio_meaning_no_dotted_argument (lo, nametree, size, arity, flags);
    }
}

static IDIO idio_meaning_dotted_closed_application (IDIO lo, IDIO ns, IDIO n, IDIO body, IDIO es, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO ms = idio_meaning_dotteds (lo, es, nametree, idio_list_length (es), idio_list_length (ns), IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append2 (ns, IDIO_LIST1 (n)));
    IDIO mbody = idio_meaning_sequence (lo, body, nt2, flags, idio_S_begin, cs, cm);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_meaning_closed_application (IDIO lo, IDIO e, IDIO ees, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ees);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /* idio_debug ("meaning-closed-application: %s\n", e); */

    /*
     * ((function ...) args)
     *
     * therefore (ph e) == 'function
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
		idio_meaning_error_static_arity (lo, IDIO_C_LOCATION ("idio_meaning_closed_application"), "not enough arguments", IDIO_LIST2 (nns, ees));
		return idio_S_unspec;
	    }
	} else if (idio_S_nil == ns) {
	    if (idio_S_nil == es) {
		return idio_meaning_fix_closed_application (lo, nns, IDIO_PAIR_T (et), ees, nametree, flags, cs, cm);
	    } else {
		idio_meaning_error_static_arity (lo, IDIO_C_LOCATION ("idio_meaning_closed_application"), "too many arguments", IDIO_LIST2 (e, ees));
		return idio_S_unspec;
	    }
	} else {
	    return idio_meaning_dotted_closed_application (lo, idio_list_reverse (regular), ns, IDIO_PAIR_T (et), ees, nametree, flags, cs, cm);
	}
    }

    idio_error_C ("meaning-closed-application", IDIO_LIST2 (e, ees), IDIO_C_LOCATION ("idio_meaning_closed_application"));

    return idio_S_notreached;
}

static IDIO idio_meaning_regular_application (IDIO lo, IDIO e, IDIO es, IDIO nametree, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_primitive_application (IDIO lo, IDIO e, IDIO es, IDIO nametree, int flags, size_t arity, IDIO index, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (index);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (symbol, e);
    IDIO_TYPE_ASSERT (list, es);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (fixnum, index);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

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
	return idio_meaning_regular_application (lo, e, es, nametree, flags, cs, cm);
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
		IDIO m1 = idio_meaning (lo, IDIO_PAIR_H (es), nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

		if (IDIO_STREQP (name, "ph")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_HEAD), m1);
		} else if (IDIO_STREQP (name, "pt")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1_TAIL), m1);
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
		IDIO m1 = idio_meaning (lo, IDIO_PAIR_H (es), nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
		IDIO m2 = idio_meaning (lo, IDIO_PAIR_HT (es), nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

		if (IDIO_STREQP (name, "pair")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_PAIR), m1, m2);
		} else if (IDIO_STREQP (name, "eq?")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_EQP), m1, m2);
		} else if (IDIO_STREQP (name, "set-ph!")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_SET_HEAD), m1, m2);
		} else if (IDIO_STREQP (name, "set-pt!")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2_SET_TAIL), m1, m2);
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
    return idio_meaning_regular_application (lo, e, es, nametree, flags, cs, cm);
}

static IDIO idio_meaning_regular_application (IDIO lo, IDIO e, IDIO es, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m;
    if (idio_isa_symbol (e)) {
	m = idio_meaning_function_reference (lo, e, nametree, flags, cs, cm);
    } else {
	m = idio_meaning (lo, e, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    }
    IDIO ms = idio_meanings (lo, es, nametree, idio_list_length (es), IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST3 (idio_I_TR_REGULAR_CALL, m, ms);
    } else {
	return IDIO_LIST3 (idio_I_REGULAR_CALL, m, ms);
    }
}

static IDIO idio_meaning_application (IDIO lo, IDIO e, IDIO es, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_symbol (e)) {
	IDIO sk = idio_meaning_variable_kind (lo, nametree, e, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);

	if (idio_isa_pair (sk)) {
	    IDIO kind = IDIO_PAIR_H (sk);

	    if (idio_S_predef == kind) {
		IDIO fvi = IDIO_PAIR_HTT (sk);
		IDIO p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));

		if (idio_S_unspec != p) {
		    if (idio_isa_primitive (p)) {
			size_t arity = IDIO_PRIMITIVE_ARITY (p);
			size_t nargs = idio_list_length (es);

			if ((IDIO_PRIMITIVE_VARARGS (p) &&
			     nargs >= arity) ||
			    arity == nargs) {
			    return idio_meaning_primitive_application (lo, e, es, nametree, flags, arity, fvi, cs, cm);
			} else {
			    idio_meaning_error_static_primitive_arity (lo, IDIO_C_LOCATION ("idio_meaning_application"), "wrong arity for primitive", e, es, p);

			    return idio_S_notreached;
			}
		    } else if (idio_isa_closure (p)) {
			return idio_meaning_regular_application (lo, e, es, nametree, flags, cs, cm);
		    } else {
			idio_debug ("BAD application: ! primitive|closure\npd %s\n", p);
			idio_debug ("sk %s\n", sk);
			idio_debug ("e %s\n", e);
			idio_debug ("ivvr %s\n", idio_vm_values_ref (IDIO_FIXNUM_VAL (IDIO_PAIR_HTT (sk))));
		    }
		}
	    }
	}
    }

    if (idio_isa_pair (e) &&
	idio_eqp (idio_S_function, IDIO_PAIR_H (e))) {
	return idio_meaning_closed_application (lo, e, es, nametree, flags, cs, cm);
    } else {
	return idio_meaning_regular_application (lo, e, es, nametree, flags, cs, cm);
    }

    idio_error_C ("meaning-application", IDIO_LIST2 (e, es), IDIO_C_LOCATION ("idio_meaning_application"));

    return idio_S_notreached;
}

static IDIO idio_meaning_dynamic_reference (IDIO lo, IDIO name, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_reference (lo, name, nametree, IDIO_MEANING_DYNAMIC_SCOPE (flags), cs, cm);
}

static IDIO idio_meaning_dynamic_let (IDIO lo, IDIO name, IDIO e, IDIO ep, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (lo, e, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO fmci;
    IDIO sk = idio_module_find_symbol (name, cm);

    int defining = 0;
    if (idio_S_unspec != sk) {
	fmci = IDIO_PAIR_HT (sk);
    } else {
	/*
	 * Get a new toplevel for this dynamic variable
	 */
	fmci = idio_toplevel_extend (lo, name, IDIO_MEANING_DYNAMIC_SCOPE (flags), cs, cm);
	defining = 1;
    }

    /*
     * Tell the tree of "locals" about this dynamic variable and find
     * the meaning of ep with the new tree
     */
    IDIO nt2 = idio_nametree_dynamic_extend (nametree, name, fmci, idio_S_dynamic);

    IDIO mp = idio_meaning_sequence (lo, ep, nt2, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, cs, cm);

    IDIO dynamic_wrap = IDIO_LIST3 (IDIO_LIST3 (idio_I_PUSH_DYNAMIC, fmci, m),
				    mp,
				    IDIO_LIST1 (idio_I_POP_DYNAMIC));

    return IDIO_LIST2 (IDIO_LIST4 (idio_I_GLOBAL_DEF, name, idio_S_dynamic, fmci),
		       dynamic_wrap);
    if (defining) {
	return IDIO_LIST2 (IDIO_LIST4 (idio_I_GLOBAL_DEF, name, idio_S_dynamic, fmci),
			   dynamic_wrap);
    } else {
	return dynamic_wrap;
    }
}

static IDIO idio_meaning_dynamic_unset (IDIO lo, IDIO name, IDIO ep, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_dynamic_let (lo, name, idio_S_undef, ep, nametree, flags, cs, cm);
}

static IDIO idio_meaning_environ_reference (IDIO lo, IDIO name, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_reference (lo, name, nametree, IDIO_MEANING_ENVIRON_SCOPE (flags), cs, cm);
}

static IDIO idio_meaning_environ_let (IDIO lo, IDIO name, IDIO e, IDIO ep, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (lo, e, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO fmci;
    IDIO sk = idio_module_find_symbol (name, cm);

    int defining = 0;
    if (idio_S_unspec != sk) {
	fmci = IDIO_PAIR_HT (sk);
    } else {
	/*
	 * Get a new toplevel for this environ variable
	 */
	fmci = idio_toplevel_extend (lo, name, IDIO_MEANING_ENVIRON_SCOPE (flags), cs, cm);
	defining = 1;
    }

    /*
     * Tell the tree of "locals" about this environ variable and find
     * the meaning of ep with the new tree
     */
    IDIO nt2 = idio_nametree_dynamic_extend (nametree, name, fmci, idio_S_environ);

    IDIO mp = idio_meaning_sequence (lo, ep, nt2, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, cs, cm);

    IDIO environ_wrap = IDIO_LIST3 (IDIO_LIST3 (idio_I_PUSH_ENVIRON, fmci, m),
				    mp,
				    IDIO_LIST1 (idio_I_POP_ENVIRON));

    return IDIO_LIST2 (IDIO_LIST4 (idio_I_GLOBAL_DEF, name, idio_S_environ, fmci),
		       environ_wrap);
    if (defining) {
	return IDIO_LIST2 (IDIO_LIST4 (idio_I_GLOBAL_DEF, name, idio_S_environ, fmci),
			   environ_wrap);
    } else {
	return environ_wrap;
    }
}

static IDIO idio_meaning_environ_unset (IDIO lo, IDIO name, IDIO ep, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_environ_let (lo, name, idio_S_undef, ep, nametree, flags, cs, cm);
}

static IDIO idio_meaning_computed_reference (IDIO lo, IDIO name, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_reference (lo, name, nametree, IDIO_MEANING_COMPUTED_SCOPE (flags), cs, cm);
}

/*
 * (trap condition       handler body ...)
 * (trap (condition ...) handler body ...)
 *
 * This is a bit complicated as condition can be a list.  For each
 * condition in the list we want to use *the same* handler code.  So
 * our intermediate code wants to leave the closure for the handler in
 * *val* then have a sequence of "PUSH_TRAP n" statements all re-using
 * the handler in *val* then the body code then a matching sequence of
 * POP_TRAP statements.
 */
static IDIO idio_meaning_trap (IDIO lo, IDIO ce, IDIO he, IDIO be, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (ce);
    IDIO_ASSERT (he);
    IDIO_ASSERT (be);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /*
     * We need the meaning of handler now as it'll be used by all the
     * traps.
     */
    he = idio_rewrite_body (he);

    IDIO mh = idio_meaning (lo, he, nametree, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    /*
     * if the condition expression is not a list then make it one
     */
    if (! idio_isa_pair (ce)) {
	ce = IDIO_LIST1 (ce);
    }

    /*
     * For each condition, resolve/discover the condition's name then
     * build pushs with the fmci.
     *
     * pushs is now the the reverse order of ce
     */
    IDIO pushs = idio_S_nil;
    IDIO pops = idio_S_nil;

    while (idio_S_nil != ce) {
	IDIO cname = IDIO_PAIR_H (ce);

	IDIO fmci;
	IDIO sk = idio_module_find_symbol_recurse (cname, cm, 1);

	if (idio_S_unspec != sk) {
	    fmci = IDIO_PAIR_HT (sk);
	} else {
	    fmci = idio_toplevel_extend (lo, cname, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);
	}

	pushs = idio_pair (IDIO_LIST2 (idio_I_PUSH_TRAP, fmci), pushs);
	pops = idio_pair (IDIO_LIST1 (idio_I_POP_TRAP), pops);

	ce = IDIO_PAIR_T (ce);
    }

    be = idio_rewrite_body (be);

    IDIO mb = idio_meaning_sequence (lo, be, nametree, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, cs, cm);

    IDIO r = idio_list_append2 (IDIO_LIST1 (mh), pushs);
    r = idio_list_append2 (r, IDIO_LIST1 (mb));
    r = idio_list_append2 (r, pops);

    return r;
}

static IDIO idio_meaning_include (IDIO lo, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    idio_thread_save_state (idio_thread_current_thread ());
    idio_load_file_name_aio (e, cs);
    idio_thread_restore_state (idio_thread_current_thread ());

    return IDIO_LIST1 (idio_I_NOP);
}

static IDIO idio_meaning_expander (IDIO lo, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO me = idio_macro_expand (e);

    return idio_meaning (lo, me, nametree, flags, cs, cm);
}

static IDIO idio_meaning (IDIO lo, IDIO e, IDIO nametree, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /* idio_debug ("meaning: e  in %s ", e);  */
    /* fprintf (stderr, "flags=%x\n", flags);  */

    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	IDIO et = IDIO_PAIR_T (e);

	if (idio_S_begin == eh ||
	    idio_S_and == eh ||
	    idio_S_or == eh) {
	    if (idio_isa_pair (et)) {
		return idio_meaning_sequence (lo, et, nametree, flags, eh, cs, cm);
	    } else if (idio_S_begin == eh) {
		return idio_meaning (lo, idio_S_void, nametree, flags, cs, cm);
	    } else if (idio_S_and == eh) {
		return idio_meaning (lo, idio_S_true, nametree, flags, cs, cm);
	    } else if (idio_S_or == eh) {
		return idio_meaning (lo, idio_S_false, nametree, flags, cs, cm);
	    } else {
		idio_error_C ("unexpected sequence keyword", eh, IDIO_C_LOCATION ("idio_meaning/gebin-and-or"));

		return idio_S_notreached;
	    }
	} else if (idio_S_escape == eh) {
	    /* (escape x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_escape (lo, IDIO_PAIR_H (et), nametree, flags, cs, cm);
	    } else {
		idio_error_param_nil ("(escape)", IDIO_C_LOCATION ("idio_meaning/escape"));

		return idio_S_notreached;
	    }
	} else if (idio_S_quote == eh) {
	    /* (quote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quotation (lo, IDIO_PAIR_H (et), nametree, flags);
	    } else {
		idio_error_param_nil ("(quote)", IDIO_C_LOCATION ("idio_meaning/quote"));

		return idio_S_notreached;
	    }
	} else if (idio_S_quasiquote == eh) {
	    /* (quasiquote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quasiquotation (lo, IDIO_PAIR_H (et), nametree, flags, cs, cm);
	    } else {
		idio_error_param_nil ("(quasiquote)", IDIO_C_LOCATION ("idio_meaning/quasiquote"));

		return idio_S_notreached;
	    }
	} else if (idio_S_function == eh ||
		   idio_S_lambda == eh) {
	    /* (function bindings [docstr] body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO etth = IDIO_PAIR_H (ett);
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_string (etth) &&
			idio_S_nil != ettt) {
			/*
			 * (function bindings "docstr" body ...)
			 */
			return idio_meaning_abstraction (lo, IDIO_PAIR_H (et), etth, ettt, nametree, flags, cs, cm);
		    } else {
			/*
			 * (function bindings body ...)
			 * (function bindings "...")
			 */
			return idio_meaning_abstraction (lo, IDIO_PAIR_H (et), idio_S_nil, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		    }
		} else {
		    /* (function bindings body ...) */
		    return idio_meaning_abstraction (lo, IDIO_PAIR_H (et), idio_S_nil, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		}
	    } else {
		idio_error_param_nil ("(function)", IDIO_C_LOCATION ("idio_meaning/function"));

		return idio_S_notreached;
	    }
	} else if (idio_S_if == eh) {
	    /* (if condition consequent alternative) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    IDIO ettth = idio_S_void; /* default: (if #f e) -> #void */
		    if (idio_isa_pair (ettt)) {
			ettth = IDIO_PAIR_H (ettt);
		    }
		    return idio_meaning_alternative (lo, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettth, nametree, flags, cs, cm);
		} else {
		    idio_error_param_nil ("(if cond)", IDIO_C_LOCATION ("idio_meaning/if"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(if)", IDIO_C_LOCATION ("idio_meaning/if"));

		return idio_S_notreached;
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

		IDIO etc = idio_rewrite_cond (et);

		IDIO c = idio_meaning (lo, etc, nametree, flags, cs, cm);

		return c;
	    } else {
		idio_error_C ("cond clause*:", e, IDIO_C_LOCATION ("idio_meaning/cond"));

		return idio_S_notreached;
	    }
	} else if (idio_S_set == eh ||
		   idio_S_eq == eh) {
	    /* (set! var expr) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_assignment (lo, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);
		} else {
		    idio_error_param_nil ("(set! symbol)", IDIO_C_LOCATION ("idio_meaning/set"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(set!)", IDIO_C_LOCATION ("idio_meaning/set"));

		return idio_S_notreached;
	    }
	} else if (idio_S_define_macro == eh) {
	    /* (define-macro bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_macro (lo, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, flags, cs, cm);
		} else {
		    idio_error_param_nil ("(define-macro symbol)", IDIO_C_LOCATION ("idio_meaning/define-macro"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(define-macro)", IDIO_C_LOCATION ("idio_meaning/define-macro"));

		return idio_S_notreached;
	    }
	} else if (idio_S_define_infix_operator == eh) {
	    /* (define-infix-operator bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_define_infix_operator (lo, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), IDIO_PAIR_H (ettt), nametree, flags, cs, cm);
		    } else {
			idio_error_param_nil ("(define-infix-operator symbol pri)", IDIO_C_LOCATION ("idio_meaning/define-infix-operator"));

			return idio_S_notreached;
		    }
		} else {
		    idio_error_param_nil ("(define-infix-operator symbol)", IDIO_C_LOCATION ("idio_meaning/define-infix-operator"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(define-infix-operator)", IDIO_C_LOCATION ("idio_meaning/define-infix-operator"));

		return idio_S_notreached;
	    }
	} else if (idio_S_define_postfix_operator == eh) {
	    /* (define-postfix-operator bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_define_postfix_operator (lo, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), IDIO_PAIR_H (ettt), nametree, flags, cs, cm);
		    } else {
			idio_error_param_nil ("(define-postfix-operator symbol pri)", IDIO_C_LOCATION ("idio_meaning/define-postfix-operator"));

			return idio_S_notreached;
		    }
		} else {
		    idio_error_param_nil ("(define-postfix-operator symbol)", IDIO_C_LOCATION ("idio_meaning/define-postfix-operator"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(define-postfix-operator)", IDIO_C_LOCATION ("idio_meaning/define-postfix-operator"));

		return idio_S_notreached;
	    }
	} else if (idio_S_define == eh) {
	    /*
	     * (define var expr)
	     * (define bindings body ...)
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define (lo, IDIO_PAIR_H (et), ett, nametree, flags, cs, cm);
		} else {
		    idio_error_param_nil ("(define symbol)", IDIO_C_LOCATION ("idio_meaning/define"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(define)", IDIO_C_LOCATION ("idio_meaning/define"));

		return idio_S_notreached;
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
		    return idio_meaning_define (lo, IDIO_PAIR_H (et), ett, nametree, flags, cs, cm);
		} else {
		    idio_error_param_nil ("(:= symbol)", IDIO_C_LOCATION ("idio_meaning/:="));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(:=)", IDIO_C_LOCATION ("idio_meaning/:="));

		return idio_S_notreached;
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
		    return idio_meaning_define_environ (lo, IDIO_PAIR_H (et), ett, nametree, flags, cs, cm);
		} else {
		    idio_error_param_nil ("(:* symbol)", IDIO_C_LOCATION ("idio_meaning/:*"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(:*)", IDIO_C_LOCATION ("idio_meaning/:*"));

		return idio_S_notreached;
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
		    return idio_meaning_define_dynamic (lo, IDIO_PAIR_H (et), ett, nametree, flags, cs, cm);
		} else {
		    idio_error_param_nil ("(:~ symbol)", IDIO_C_LOCATION ("idio_meaning/:~"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(:~)", IDIO_C_LOCATION ("idio_meaning/:~"));

		return idio_S_notreached;
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
		    return idio_meaning_define_computed (lo, IDIO_PAIR_H (et), ett, nametree, flags, cs, cm);
		} else {
		    idio_error_param_nil ("(:$ symbol)", IDIO_C_LOCATION ("idio_meaning/:$"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(:$)", IDIO_C_LOCATION ("idio_meaning/:$"));

		return idio_S_notreached;
	    }
	} else if (idio_S_block == eh) {
	    /*
	     * { ... }
	     */
	    if (idio_isa_pair (et)) {
		return idio_meaning_block (lo, et, nametree, flags, cs, cm);
	    } else {
		return idio_meaning (lo, idio_S_void, nametree, flags, cs, cm);
	    }
	} else if (idio_S_dynamic == eh) {
	    /* (dynamic var) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_dynamic_reference (lo, IDIO_PAIR_H (et), nametree, flags, cs, cm);
	    } else {
		idio_error_param_nil ("(dynamic)", IDIO_C_LOCATION ("idio_meaning/dynamic"));

		return idio_S_notreached;
	    }
	} else if (idio_S_dynamic_let == eh) {
	    /* (dynamic-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_dynamic_let (lo, IDIO_PAIR_H (eth), IDIO_PAIR_H (etht), IDIO_PAIR_T (et), nametree, flags, cs, cm);
		    } else {
			idio_error_param_type ("pair", etht, IDIO_C_LOCATION ("idio_meaning/dynamic-let"));

			return idio_S_notreached;
		    }
		} else {
		    idio_error_param_type ("pair", eth, IDIO_C_LOCATION ("idio_meaning/dynamic-let"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(dynamic-let)", IDIO_C_LOCATION ("idio_meaning/dynamic-let"));

		return idio_S_notreached;
	    }
	} else if (idio_S_dynamic_unset == eh) {
	    /* (dynamic-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_dynamic_unset (lo, eth, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		} else {
		    idio_error_param_type ("symbol", eth, IDIO_C_LOCATION ("idio_meaning/dynamic-unset"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(dynamic-unset)", IDIO_C_LOCATION ("idio_meaning/dynamic-unset"));

		return idio_S_notreached;
	    }
	} else if (idio_S_environ_let == eh) {
	    /* (environ-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_environ_let (lo, IDIO_PAIR_H (eth), IDIO_PAIR_H (etht), IDIO_PAIR_T (et), nametree, flags, cs, cm);
		    } else {
			idio_error_param_type ("pair", etht, IDIO_C_LOCATION ("idio_meaning/environ-let"));

			return idio_S_notreached;
		    }
		} else {
		    idio_error_param_type ("pair", eth, IDIO_C_LOCATION ("idio_meaning/environ-let"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(environ-let)", IDIO_C_LOCATION ("idio_meaning/environ-let"));

		return idio_S_notreached;
	    }
	} else if (idio_S_environ_unset == eh) {
	    /* (environ-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_environ_unset (lo, eth, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		} else {
		    idio_error_param_type ("symbol", eth, IDIO_C_LOCATION ("idio_meaning/environ-unset"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(environ-unset)", IDIO_C_LOCATION ("idio_meaning/environ-unset"));

		return idio_S_notreached;
	    }
	} else if (idio_S_trap == eh) {
	    /*
	     * (trap condition       handler body ...)
	     * (trap (condition ...) handler body ...)
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_trap (lo, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettt, nametree, flags, cs, cm);
		    } else {
			idio_error_param_type ("pair", ettt, IDIO_C_LOCATION ("idio_meaning/trap"));

			return idio_S_notreached;
		    }
		} else {
		    idio_error_param_type ("pair", ett, IDIO_C_LOCATION ("idio_meaning/trap"));

		    return idio_S_notreached;
		}
	    } else {
		idio_error_param_nil ("(trap)", IDIO_C_LOCATION ("idio_meaning/trap"));

		return idio_S_notreached;
	    }
	} else if (idio_S_include == eh) {
	    /* (include filename) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_include (lo, IDIO_PAIR_H (et), nametree, flags, cs, cm);
	    } else {
		idio_error_param_nil ("(include)", IDIO_C_LOCATION ("idio_meaning/include"));

		return idio_S_notreached;
	    }
	} else if (idio_S_module == eh) {
	    /* (module MODULE-NAME) */
	    if (idio_isa_pair (et)) {
		/*
		 * module MODULE-NAME is unusual in that it affects
		 * the evaluator here and now as we are changing the
		 * scope of names.
		 *
		 * We also need to change the global sense of
		 * current-module as subsequent read's will expect us
		 * to remain in the module
		 */
		cm = idio_module_find_or_create_module (IDIO_PAIR_H (et));
		idio_thread_set_current_module (cm);

		/*
		 * define-macro (module name) ... is in module.idio
		 */
		return idio_meaning_expander (lo, e, nametree, flags, cs, cm);
	    } else {
		idio_error_param_nil ("(module)", IDIO_C_LOCATION ("idio_meaning/module"));

		return idio_S_notreached;
	    }
	} else {
	    if (idio_isa_symbol (eh)) {
		IDIO k = idio_meaning_variable_kind (lo, nametree, eh, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);

		if (idio_S_unspec != k) {
		    if (idio_S_false != idio_expanderp (eh)) {
			return idio_meaning_expander (lo, e, nametree, flags, cs, cm);
		    }
		}
	    }

	    return idio_meaning_application (lo, eh, et, nametree, flags, cs, cm);
	}
    } else {
	switch ((intptr_t) e & IDIO_TYPE_MASK) {
	case IDIO_TYPE_FIXNUM_MARK:
	case IDIO_TYPE_CONSTANT_MARK:
	    return idio_meaning_quotation (lo, e, nametree, flags);
	case IDIO_TYPE_PLACEHOLDER_MARK:
	    idio_error_C ("invalid constant type", e, IDIO_C_LOCATION ("idio_meaning/quotation/placeholder"));

	    return idio_S_notreached;
	case IDIO_TYPE_POINTER_MARK:
	    {
		switch (e->type) {
		case IDIO_TYPE_SYMBOL:
		    return idio_meaning_reference (lo, e, nametree, IDIO_MEANING_LEXICAL_SCOPE (flags), cs, cm);

		case IDIO_TYPE_STRING:
		case IDIO_TYPE_KEYWORD:
		case IDIO_TYPE_PAIR:
		case IDIO_TYPE_ARRAY:
		case IDIO_TYPE_HASH:
		case IDIO_TYPE_BIGNUM:
		    return idio_meaning_quotation (lo, e, nametree, flags);

		case IDIO_TYPE_CLOSURE:
		case IDIO_TYPE_PRIMITIVE:
		    idio_error_C ("invalid constant type", e, IDIO_C_LOCATION ("idio_meaning/quotation/function"));

		    return idio_S_notreached;
		case IDIO_TYPE_STRUCT_INSTANCE:
		    return idio_meaning_quotation (lo, e, nametree, flags);

		case IDIO_TYPE_MODULE:
		case IDIO_TYPE_FRAME:
		case IDIO_TYPE_HANDLE:
		case IDIO_TYPE_STRUCT_TYPE:
		case IDIO_TYPE_THREAD:
		case IDIO_TYPE_CONTINUATION:
		case IDIO_TYPE_C_INT:
		case IDIO_TYPE_C_UINT:
		case IDIO_TYPE_C_FLOAT:
		case IDIO_TYPE_C_DOUBLE:
		case IDIO_TYPE_C_POINTER:
		case IDIO_TYPE_C_TYPEDEF:
		case IDIO_TYPE_C_STRUCT:
		case IDIO_TYPE_C_INSTANCE:
		case IDIO_TYPE_C_FFI:
		case IDIO_TYPE_OPAQUE:
		    idio_error_C ("invalid constant type", e, IDIO_C_LOCATION ("idio_meaning/quotation/various"));

		    return idio_S_notreached;
		default:
		    idio_error_C ("unimplemented type", e, IDIO_C_LOCATION ("idio_meaning/quotation"));
		    break;
		}
	    }
	    break;
	default:
	    /* inconceivable! */
	    idio_error_printf (IDIO_C_LOCATION ("idio_meaning/quotation"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", e, (intptr_t) e & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

	    return idio_S_notreached;
	}
    }

    idio_error_C ("meaning", e, IDIO_C_LOCATION ("idio_meaning"));

    return idio_S_notreached;
}

IDIO idio_evaluate (IDIO lo, IDIO cs)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (struct_instance, lo);

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
    idio_gc_pause ("idio_evaluate");

    /*
     * XXX
     *
     * This call to idio_meaning used to be with flags set to
     * IDIO_MEANING_FLAG_TAILP as it is obvious that this call is made
     * in tail position.
     *
     * However, with the advent of modules we get in a bit of a bind.
     * A TAILP closure call will switch but not restore the
     * environment.  If {e} is a call to a closure from outside of
     * this environment and calls any function in a non-TAILP position
     * then that second function will save and restore the definition
     * environment of the function in {e}.  But {e} won't restore what
     * we think is the current environment because {e} was in TAILP.
     *
     */
    IDIO e = idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR);
    IDIO m = idio_meaning (lo, e, idio_S_nil, IDIO_MEANING_FLAG_NONE, cs, idio_thread_current_module ());

    idio_gc_resume ("idio_evaluate");

    /* idio_debug ("evaluate: m %s\n", m); */

    return m;
}

IDIO_DEFINE_PRIMITIVE1 ("evaluate/meaning", evaluate_meaning, (IDIO e))
{
    IDIO_ASSERT (e);
    IDIO_VERIFY_PARAM_TYPE (list, e);

    return idio_evaluate (e, idio_vm_constants);
}

IDIO_DEFINE_PRIMITIVE1 ("environ?", environp, (IDIO name))
{
    IDIO_ASSERT (name);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (name)) {
	IDIO sk = idio_module_env_symbol_recurse (name);

	if (idio_S_unspec != sk &&
	    idio_S_environ == IDIO_PAIR_H (sk)) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("dynamic?", dynamicp, (IDIO name))
{
    IDIO_ASSERT (name);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (name)) {
	IDIO sk = idio_module_env_symbol_recurse (name);

	if (idio_S_unspec != sk &&
	    idio_S_dynamic == IDIO_PAIR_H (sk)) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("computed?", computedp, (IDIO name))
{
    IDIO_ASSERT (name);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (name)) {
	IDIO sk = idio_module_env_symbol_recurse (name);

	if (idio_S_unspec != sk &&
	    idio_S_computed == IDIO_PAIR_H (sk)) {
	    r = idio_S_true;
	}
    }

    return r;
}

void idio_init_evaluate ()
{
    idio_evaluation_module = idio_module (idio_symbols_C_intern ("*evaluation*"));
    /* idio_evaluation_module = idio_Idio_module_instance (); */
}

void idio_evaluate_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (evaluate_meaning);
    IDIO_ADD_PRIMITIVE (environp);
    IDIO_ADD_PRIMITIVE (dynamicp);
    IDIO_ADD_PRIMITIVE (computedp);
}

void idio_final_evaluate ()
{
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
