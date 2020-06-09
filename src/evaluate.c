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

/*
 * all evaluation errors are decendents of ^evaluation-error
 */
static void idio_meaning_error (IDIO lo, IDIO c_location, IDIO msg, IDIO expr)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (string, msg);

    /*
     * How do we describe our error?
     *
     * We have three things useful to the user:
     *
     *  * ``expr`` - the bit we're complaining about
     *
     *  * ``msg`` describing the error.
     *
     *  * ``lo`` where the lexical object began.
     *
     * and something more useful to the developer:
     *
     *  * ``c_location`` which is an IDIO_C_LOCATION() and probably
     * only useful for debug -- the user won't care where in the C
     * code the error in their user code was spotted.
     */

    IDIO location = idio_meaning_error_location (lo);

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (c_location, sh);

    detail = idio_get_output_string (sh);
#endif

    IDIO c = idio_struct_instance (idio_condition_evaluation_error_type,
				   IDIO_LIST4 (msg,
					       location,
					       detail,
					       expr));
    idio_raise_condition (idio_S_false, c);
}

static void idio_meaning_evaluation_error_param_type (IDIO lo, IDIO c_location, char *msg, IDIO expr)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_meaning_error (lo, c_location, idio_get_output_string (sh), expr);
}

static void idio_meaning_evaluation_error_param_nil (IDIO lo, IDIO c_location, char *msg, IDIO expr)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (expr, sh);
    idio_display_C (": ", sh);
    idio_display_C (msg, sh);

    idio_meaning_error (lo, c_location, idio_get_output_string (sh), expr);
}

static void idio_meaning_evaluation_error (IDIO lo, IDIO c_location, char *msg, IDIO expr)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (struct_instance, lo);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_meaning_error (lo, c_location, idio_get_output_string (sh), expr);
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

	    /*
	     * Tricky to generate a test case for this as it requires
	     * that we really do redefine a primitive.
	     *
	     * It should catch any developer foobars, though.
	     */

	    IDIO lo = idio_struct_instance (idio_lexobj_type,
					    idio_pair (idio_string_C (cpp__FILE__),
					    idio_pair (idio_integer (cpp__LINE__),
					    idio_pair (idio_integer (0),
					    idio_pair (name,
					    idio_S_nil)))));

	    idio_meaning_error_static_redefine (lo, IDIO_C_FUNC_LOCATION(), "primitive value change", name, pd, primdata);

	    return idio_S_notreached;
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

    return idio_add_module_primitive (idio_primitives_module_instance (), d, cs, cpp__FILE__, cpp__LINE__);
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
	/*
	 * Shouldn't get here without a developer coding error.
	 */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected toplevel variable scope %#x", flags);

	return idio_S_notreached;
    }

    IDIO cv = idio_module_find_symbol (name, cm);
    if (idio_S_unspec != cv) {
	IDIO curkind = IDIO_PAIR_H (cv);
	if (kind != curkind) {
	    if (idio_S_predef != curkind) {
		/*
		 * I'm not sure we can get here as once the name
		 * exists in a toplevel then it we be found again
		 * before we can re-extend the toplevel.
		 *
		 * One to look out for.
		 */
		idio_meaning_error_static_redefine (lo, IDIO_C_FUNC_LOCATION (), "toplevel-extend: type change", name, cv, kind);

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
	    /*
	     * I'm not sure we can get here as once the name exists in
	     * a toplevel then it we be found again before we can
	     * re-extend the toplevel.
	     *
	     * One to look out for.
	     */
	    idio_meaning_error_static_redefine (lo, IDIO_C_FUNC_LOCATION (), "environ-extend: type change", name, sk, kind);

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
			/*
			 * I'm not sure we can get here without a
			 * coding error.
			 *
			 * One for developers.
			 */
			idio_meaning_error_static_variable (lo, IDIO_C_FUNC_LOCATION (), "unexpected local variant", name);

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
		/*
		 * To get here would be require a duff names list
		 * being passed in.
		 *
		 * Developer error.
		 */
		idio_error_C ("unexpected localp", IDIO_LIST2 (name, nametree), IDIO_C_FUNC_LOCATION ());

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
	idio_meaning_error_static_unbound (lo, IDIO_C_FUNC_LOCATION (), name);

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
	/*
	 * Shouldn't get here unless the if clauses above don't cover
	 * everything.
	 *
	 * Developer error.
	 */
	idio_meaning_error_static_unbound (lo, IDIO_C_FUNC_LOCATION (), name);

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
	idio_meaning_error_static_unbound (lo, IDIO_C_FUNC_LOCATION (), name);

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
	/*
	 * Shouldn't get here unless the if clauses above don't cover
	 * everything.
	 *
	 * Developer error.
	 */
	idio_meaning_error_static_unbound (lo, IDIO_C_FUNC_LOCATION (), name);

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

    /*
     * Shouldn't get here...
     */
    idio_error_C ("", IDIO_LIST1 (e), IDIO_C_FUNC_LOCATION ());

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

/*
 * validate & rewrite the cond clauses, ``clauses``, noting the
 * special cases of ``=>``, ``else``
 */
static IDIO idio_meaning_rewrite_cond (IDIO lo, IDIO clauses)
{
    /* idio_debug ("cond-rewrite: %s\n", clauses);     */

    if (idio_S_nil == clauses) {
	return idio_S_void;
    } else if (! idio_isa_pair (clauses)) {
	/*
	 * Shouldn't get here, eg. "(cond)", as we test that the cond
	 * clauses are a pair in the main idio_meaning loop.
	 */
	idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "cond: no clauses", clauses);

	return idio_S_notreached;
    } else if (! idio_isa_pair (IDIO_PAIR_H (clauses))) {
	/*
	 * Test Cases:
	 * evaluation-errors/rewrite-cond-isa-pair-first.idio
	 * evaluation-errors/rewrite-cond-isa-pair-after.idio
	 *
	 * cond #t
	 * cond (#f 1) #t
	 */
	idio_meaning_evaluation_error_param_type (lo, IDIO_C_FUNC_LOCATION (), "cond: clause is not a pair", clauses);

	return idio_S_notreached;
    } else if (idio_S_else == IDIO_PAIR_HH (clauses)) {
	if (idio_S_nil == IDIO_PAIR_T (clauses)) {
	    return idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_TH (clauses));
	} else {
	    /*
	     * Test Case: evaluation-errors/rewrite-cond-else-not-last.idio
	     *
	     * cond (else 1) (#t 2)
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "cond: else not in last clause", clauses);

	    return idio_S_notreached;
	}
    }

    if (idio_isa_pair (IDIO_PAIR_TH (clauses)) &&
	idio_S_eq_gt == IDIO_PAIR_HTH (clauses)) {
	/*
	 * XXX We've just looked at the ``phth clauses`` so *of
	 * course* it's a list...the only useful thing we're doing
	 * here is checking there's explicitly three elements.
	 */
	if (idio_isa_list (IDIO_PAIR_H (clauses)) &&
	    idio_list_length (IDIO_PAIR_H (clauses)) == 3) {
	    /*
	     * The ``=>`` operator is a bit of a Scheme-ism in that
	     * the clause says ``(c => f)`` and means that if ``c`` is
	     * true then apply ``f`` to ``c`` and return the result of
	     * that.  Of course we need to stage the (result of the)
	     * evaluation of ``c`` in a temporary variable because we
	     * don't want it evaluated twice.
	     *
	     * There is an unhelpful indirection complication in that
	     * we are looking at ``clauses`` so this clause is ``ph
	     * clauses`` and then ``c`` and ``f`` are the ``ph
	     * clause`` (therefore ``phh clauses``) and ``phtt
	     * clause`` (and therefore ``phtth clauses``)
	     * respectively.
	     *
	     * Finally, this clause is just one of many, so we need to
	     * keep rewriting all the remaining clauses (``pt
	     * clauses``).
	     */
	    IDIO gs = idio_gensym (NULL);
	    /*
	      `(let ((gs ,(phh clauses)))
	         (if gs
	             (,(phtth clauses) gs)
	             ,(rewrite-cond-clauses (pt clauses))))
	     */
	    return IDIO_LIST3 (idio_S_let,
			       IDIO_LIST1 (IDIO_LIST2 (gs, IDIO_PAIR_HH (clauses))),
			       IDIO_LIST4 (idio_S_if,
					   gs,
					   IDIO_LIST2 (IDIO_PAIR_HTTH (clauses),
						       gs),
					   idio_meaning_rewrite_cond (lo, IDIO_PAIR_T (clauses))));
	} else {
	    /*
	     * Test Case: evaluation-errors/rewrite-cond-apply-two-args.idio evaluation-errors/rewrite-cond-apply-four-args.idio
	     *
	     * cond (1 =>)
	     * cond (1 => a b)
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "cond: invalid => clause", clauses);

	    return idio_S_notreached;
	}
    } else if (idio_S_nil == IDIO_PAIR_TH (clauses)) {
	IDIO gs = idio_gensym (NULL);
	/*
	  `(let ((gs ,(phh clauses)))
	     (or gs
	         ,(rewrite-cond-clauses (pt clauses))))
	*/
	return IDIO_LIST3 (idio_S_let,
			   IDIO_LIST1 (IDIO_LIST2 (gs, IDIO_PAIR_HH (clauses))),
			   IDIO_LIST3 (idio_S_or,
				       gs,
				       idio_meaning_rewrite_cond (lo, IDIO_PAIR_T (clauses))));
    } else {
	return IDIO_LIST4 (idio_S_if,
			   IDIO_PAIR_HH (clauses),
			   idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_TH (clauses)),
			   idio_meaning_rewrite_cond (lo, IDIO_PAIR_T (clauses)));
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
	/*
	 * Test Case: evaluation-errors/assign-non-symbol.idio
	 *
	 * 1 = 3
	 */
	idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "cannot assign to", name);

	return idio_S_notreached;
    }

    /*
     * Normal assignment to a symbol
     */

    IDIO m = idio_meaning (lo, e, nametree, IDIO_MEANING_NO_DEFINE (IDIO_MEANING_NOT_TAILP (flags)), cs, cm);

    IDIO sk = idio_meaning_variable_kind (lo, nametree, name, flags, cs, cm);

    if (idio_S_unspec == sk) {
	/*
	 * Not sure we can get here as idio_meaning_variable_kind()
	 * should have created a reference (otherwise, what's it
	 * doing?)
	 */
	idio_error_C ("unknown variable:", name, IDIO_C_FUNC_LOCATION ());

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

	/* idio_meaning_error_static_immutable (name, IDIO_C_FUNC_LOCATION ()); */
	/* return idio_S_notreached; */
    } else {
	/*
	 * Should only get here if we haven't accounted for all
	 * variants above.
	 */
	idio_meaning_error_static_unbound (lo, IDIO_C_FUNC_LOCATION (), name);

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
    IDIO expander = IDIO_LIST4 (idio_S_function,
				IDIO_LIST2 (x_sym, e_sym),
				idio_string_C ("im_dm"),
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
	/*
	 * Test Case: evaluation-errors/infix-op-negative-priority.idio
	 *
	 * define-infix-operator qqq -300 ...
	 */
	idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "positive priority", pri);

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
	    /*
	     * Test Case: evaluation-errors/infix-op-not-an-operator.idio
	     *
	     * define-infix-operator qqq 300 zzz
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "not an operator", e);

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

	IDIO fe = IDIO_LIST4 (idio_S_function,
			      def_args,
			      idio_string_C ("im_dio"),
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
	/*
	 * Test Case: evaluation-errors/infix-op-negative-priority.idio
	 *
	 * define-infix-operator qqq -300 ...
	 */
	idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "positive priority", pri);

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
	    /*
	     * Test Case: evaluation-errors/infix-op-not-an-operator.idio
	     *
	     * define-infix-operator qqq 300 zzz
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "not an operator", e);

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

	IDIO fe = IDIO_LIST4 (idio_S_function,
			      def_args,
			      idio_string_C ("im_dpo"),
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
	/*
	 * Not sure we can get here as ``e`` is either #n or a list
	 */
	idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "define-computed: no getter/setter", e);

	return idio_S_notreached;
    }

    if (idio_S_nil == getter &&
	idio_S_nil == setter) {
	/*
	 * Test Case: evaluation-errors/define-computed-no-args.idio
	 *
	 * C :$ #n
	 */
	idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "define-computed: no getter/setter", name);

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
	/*
	 * Not sure we can get here as it requires developer coding
	 * error.
	 */
	idio_error_C ("unexpected sequence keyword", keyword, IDIO_C_FUNC_LOCATION ());

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
		/*
		 * Not sure we can get here as it requires developer
		 * coding error.
		 */
		idio_error_C ("unexpected sequence keyword", keyword, IDIO_C_FUNC_LOCATION ());

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
 * With idio_meaning_rewrite_body* we want to massage the code to support some
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
static IDIO idio_meaning_rewrite_body_letrec (IDIO lo, IDIO e);

static IDIO idio_meaning_rewrite_body (IDIO lo, IDIO e)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);

    IDIO_TYPE_ASSERT (struct_instance, lo);

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
	    r = idio_pair (idio_meaning_rewrite_body_letrec (lo, body), r);
	    break;
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_eq == IDIO_PAIR_H (cur)) {
	    /* := -> let* */

	    IDIO body = idio_meaning_rewrite_body (lo, IDIO_PAIR_T (l));
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

	    IDIO body = idio_meaning_rewrite_body (lo, IDIO_PAIR_T (l));
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

	    IDIO body = idio_meaning_rewrite_body (lo, IDIO_PAIR_T (l));
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

	    IDIO body = idio_meaning_rewrite_body (lo, IDIO_PAIR_T (l));
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

	    IDIO body = idio_meaning_rewrite_body (lo, IDIO_PAIR_T (l));
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

	    /*
	     * Test Case: evaluation-errors/internal-define-macro.idio
	     *
	     * {
	     *   define-macro (bar) {
	     *     #T{ 1 }
	     *   }
	     * }
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "internal define-macro", cur);

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

static IDIO idio_meaning_rewrite_body_letrec (IDIO lo, IDIO e)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (e);

    IDIO_TYPE_ASSERT (struct_instance, lo);

    IDIO l = e;
    IDIO defs = idio_S_nil;

    /* idio_debug ("rewrite-body-letrec: %s\n", l); */

    for (;;) {
	IDIO cur = idio_S_unspec;
	if (idio_S_nil == l) {
	    /* idio_debug ("e=%s\n", e); */
	    /*
	     * Test Case: evaluation-errors/letrec-empty-body.idio
	     *
	     * {
	     *   bar :+ {
	     *   }
	     * }
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "letrec: empty body", l);

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
				   idio_list_append2 (IDIO_LIST3 (idio_S_function,
								  IDIO_PAIR_T (bindings),
								  idio_string_C ("im_rbl :+")),
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
	    /*
	     * Test Case: (nominally) evaluation-errors/letrec-internal-define-macro.idio
	     *
	     * bar :+ define-macro (baz) { #T{ 1 } }
	     *
	     * XXX I can't get this to trigger the error
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "letrec: internal define-macro", cur);

	    return idio_S_notreached;
	} else {
	    /* body proper */
	    l = idio_meaning_rewrite_body (lo, l);

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

    ep = idio_meaning_rewrite_body (lo, ep);

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
	    /*
	     * Test Case: ??
	     *
	     * Not sure we can generate this as it requires that
	     * ``idio_display_string (nns)`` return the representation
	     * of an empty list of args (``()``) as less than 2
	     * characters.
	     */
	    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "invalid sigstr", nns);

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

    /*
     * Shouldn't get here...
     */
    idio_error_C ("meaning-abstraction", IDIO_LIST2 (nns, ep), IDIO_C_FUNC_LOCATION ());

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

    es = idio_meaning_rewrite_body (lo, es);

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

    body = idio_meaning_rewrite_body (lo, body);

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
		/*
		 * Test Cases: evaluation-errors/closed-function-not-enough-args-{0,1}.idio
		 *
		 * ((function (x) x) )
		 * (function (x y) x) 1
		 *
		 * XXX Note that in the first case we must wrap the
		 * putative ``func-defn args`` in parens otherwise
		 * we're simply writing ``func-defn`` which defines
		 * and immediately throws away the function.
		 *
		 * In the second case the system can see there's an arg!
		 */
		idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "closed function: not enough arguments", IDIO_LIST2 (nns, ees));

		return idio_S_notreached;
	    }
	} else if (idio_S_nil == ns) {
	    if (idio_S_nil == es) {
		return idio_meaning_fix_closed_application (lo, nns, IDIO_PAIR_T (et), ees, nametree, flags, cs, cm);
	    } else {
		/*
		 * Test Case: evaluation-errors/closed-function-too-many-args-{0,1}.idio
		 *
		 * (function () 1) 2
		 * (function (x) x) 1 2
		 */
		idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "closed function: too many arguments", IDIO_LIST2 (nns, ees));

		return idio_S_notreached;
	    }
	} else {
	    return idio_meaning_dotted_closed_application (lo, idio_list_reverse (regular), ns, IDIO_PAIR_T (et), ees, nametree, flags, cs, cm);
	}
    }

    /*
     * Can we get here?
     */

    idio_error_C ("meaning-closed-application", IDIO_LIST2 (e, ees), IDIO_C_FUNC_LOCATION ());

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
			    /*
			     * Test Case: evaluation-errors/primitive-arity.idio
			     *
			     * pair 1
			     */
			    idio_meaning_evaluation_error (lo, IDIO_C_FUNC_LOCATION (), "wrong arity for primitive", IDIO_LIST2 (e, es));

			    return idio_S_notreached;
			}
		    } else if (idio_isa_closure (p)) {
			return idio_meaning_regular_application (lo, e, es, nametree, flags, cs, cm);
		    } else {
			/*
			 * Can we get here?  We'd need to be a predef
			 * but those are all, uh, primitives.
			 *
			 * Redefining a primitive doesn't get us here.
			 * Hmm.
			 */
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
	if (0 == idio_isa_string (IDIO_PAIR_HTT (e))) {
	    e = idio_list_append2 (IDIO_LIST3 (IDIO_PAIR_H (e),
					       IDIO_PAIR_HT (e),
					       idio_string_C ("im_appl closed")),
				   IDIO_PAIR_TT (e));
	    /* idio_debug ("im_appl closed %s\n", e); */
	}
	return idio_meaning_closed_application (lo, e, es, nametree, flags, cs, cm);
    } else {
	return idio_meaning_regular_application (lo, e, es, nametree, flags, cs, cm);
    }

    /*
     * Can we get here?
     */

    idio_error_C ("meaning-application", IDIO_LIST2 (e, es), IDIO_C_FUNC_LOCATION ());

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
    he = idio_meaning_rewrite_body (lo, he);

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

    be = idio_meaning_rewrite_body (lo, be);

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
		/*
		 * Not sure we can get here as it requires developer
		 * coding error.
		 */
		idio_error_C ("unexpected sequence keyword", eh, IDIO_C_FUNC_LOCATION_S ("begin-and-or"));

		return idio_S_notreached;
	    }
	} else if (idio_S_escape == eh) {
	    /* (escape x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_escape (lo, IDIO_PAIR_H (et), nametree, flags, cs, cm);
	    } else {
		/*
		 * Test Case: evaluation-errors/escape-nil.idio
		 *
		 * (escape)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("escape"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_quote == eh) {
	    /* (quote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quotation (lo, IDIO_PAIR_H (et), nametree, flags);
	    } else {
		/*
		 * Test Case: evaluation-errors/quote-nil.idio
		 *
		 * (quote)
		 *
		 * XXX sight annoyance as ``(quote)`` is nominally
		 * ``'#n``
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("quote"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_quasiquote == eh) {
	    /* (quasiquote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quasiquotation (lo, IDIO_PAIR_H (et), nametree, flags, cs, cm);
	    } else {
		/*
		 * Test Case: evaluation-errors/quasiquote-nil.idio
		 *
		 * (quasiquote)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("quasiquote"), "no argument", eh);

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
			 *
			 * The second is a function whose body is a
			 * string.
			 */
			return idio_meaning_abstraction (lo, IDIO_PAIR_H (et), idio_S_nil, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		    }
		} else {
		    /* (function bindings body ...) */
		    return idio_meaning_abstraction (lo, IDIO_PAIR_H (et), idio_S_nil, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/function-nil.idio
		 *
		 * (function)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("function"), "no arguments", eh);

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
		    /*
		     * Test Case: evaluation-errors/if-cond-nil.idio
		     *
		     * (if 1)
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("if cond"), "no consequent/alternative", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/if-nil.idio
		 *
		 * (if)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("if"), "no arguments", eh);

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

		IDIO etc = idio_meaning_rewrite_cond (lo, et);

		IDIO c = idio_meaning (lo, etc, nametree, flags, cs, cm);

		return c;
	    } else {
		/*
		 * Test Case: evaluation-errors/cond-nil.idio
		 *
		 * (cond)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("cond"), "no clauses", eh);

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
		    /*
		     * Test Case: evaluation-errors/set-symbol-nil.idio
		     *
		     * (set! x )
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("set!"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/set-nil.idio
		 *
		 * (set!)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("set!"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_define_macro == eh) {
	    /* (define-macro bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_macro (lo, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/define-macro-bindings-nil.idio
		     *
		     * define-macro (m)
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-macro"), "no body", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-macro-nil.idio
		 *
		 * (define-macro)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-macro"), "no arguments", eh);

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
			/*
			 * Test Case: evaluation-errors/define-infix-op-symbol-pri-nil.idio
			 *
			 * define-infix-operator sym pri
			 */
			idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-infix-operator"), "no body", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/define-infix-op-symbol-nil.idio
		     *
		     * define-infix-operator sym
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-infix-operator"), "no pri body", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-infix-op-nil.idio
		 *
		 * (define-infix-operator)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-infix-operator"), "no arguments", eh);

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
			/*
			 * Test Case: evaluation-errors/define-infix-op-symbol-pri-nil.idio
			 *
			 * define-postfix-operator sym pri
			 */
			idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-postfix-operator"), "no body", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/define-infix-op-symbol-nil.idio
		     *
		     * define-postfix-operator sym
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-postfix-operator"), "no pri body", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-infix-op-nil.idio
		 *
		 * (define-postfix-operator)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define-postfix-operator"), "no arguments", eh);

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
		    /*
		     * Test Case: evaluation-errors/define-sym-nil.idio
		     *
		     * define sym
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-nil.idio
		 *
		 * (define)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("define"), "no arguments", eh);

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
		    /*
		     * Test Case: evaluation-errors/toplevel-define-sym-nil.idio
		     *
		     * := sym
		     *
		     * XXX can't do ``sym :=`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":="), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/toplevel-define-nil.idio
		 *
		 * (:=)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":="), "no arguments", eh);

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
		    /*
		     * Test Case: evaluation-errors/environ-define-sym-nil.idio
		     *
		     * :* sym
		     *
		     * XXX can't do ``sym :*`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":*"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/environ-define-nil.idio
		 *
		 * (:*)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":*"), "no arguments", eh);

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
		    /*
		     * Test Case: evaluation-errors/dynamic-define-sym-nil.idio
		     *
		     * :~ sym
		     *
		     * XXX can't do ``sym :~`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":~"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/dynamic-define-nil.idio
		 *
		 * (:~)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":~"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_colon_dollar == eh) {
	    /*
	     * (:$ var getter setter)
	     * (:$ var getter)
	     *
	     * in the short term => define-computed
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_computed (lo, IDIO_PAIR_H (et), ett, nametree, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/computed-define-sym-nil.idio
		     *
		     * :$ sym
		     *
		     * XXX can't do ``sym :$`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":$"), "no getter [setter]", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/computed-define-nil.idio
		 *
		 * (:$)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S (":$"), "no arguments", eh);

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
		/*
		 * Test Case: evaluation-errors/dynamic-nil.idio
		 *
		 * (dynamic)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("dynamic"), "no argument", eh);

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
			/*
			 * Test Case: evaluation-errors/dynamic-let-bindings-not-tuple.idio
			 *
			 * dynamic-let (d)
			 */
			idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("dynamic-let"), "bindings not a tuple", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/dynamic-let-bindings-not-pair.idio
		     *
		     * dynamic-let #n
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("dynamic-let"), "bindings not a pair", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/dynamic-let-nil.idio
		 *
		 * (dynamic-let)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("dynamic-let"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_dynamic_unset == eh) {
	    /* (dynamic-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_dynamic_unset (lo, eth, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/dynamic-unset-non-sym.idio
		     *
		     * dynamic-unset 1
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("dynamic-unset"), "not a symbol", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/dynamic-unset-nil.idio
		 *
		 * (dynamic-unset)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("dynamic-unset"), "no argument", eh);

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
			/*
			 * Test Case: evaluation-errors/environ-let-bindings-not-tuple.idio
			 *
			 * environ-let (e)
			 */
			idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("environ-let"), "bindings not a tuple", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/environ-let-bindings-not-pair.idio
		     *
		     * environ-let #n
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("environ-let"), "bindings not a pair", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/environ-let-nil.idio
		 *
		 * (environ-let)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("environ-let"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_environ_unset == eh) {
	    /* (environ-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_environ_unset (lo, eth, IDIO_PAIR_T (et), nametree, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/environ-unset-non-sym.idio
		     *
		     * environ-unset 1
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("environ-unset"), "not a symbol", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/environ-unset-nil.idio
		 *
		 * (environ-unset)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("environ-unset"), "no argument", eh);

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
			/*
			 * Test Case: evaluation-errors/trap-condition-handler-nil.idio
			 *
			 * trap condition
			 */
			idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("environ-unset"), "no body", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/trap-condition-nil.idio
		     *
		     * (trap)
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("trap"), "no handler", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/trap-nil.idio
		 *
		 * (trap)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("trap"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_include == eh) {
	    /* (include filename) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_include (lo, IDIO_PAIR_H (et), nametree, flags, cs, cm);
	    } else {
		/*
		 * Test Case: evaluation-errors/include-nil.idio
		 *
		 * (include)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("include"), "no argument", eh);

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
		/*
		 * Test Case: evaluation-errors/module-nil.idio
		 *
		 * (module)
		 */
		idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("module"), "no argument", eh);

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
	    /*
	     * Can'r get here without a developer coding issue.
	     */
	    idio_error_C ("invalid constant type", e, IDIO_C_FUNC_LOCATION_S ("quotation/placeholder"));

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
		    /*
		     * Test Case: ??
		     *
		     * XXX I used to get here because of a coding error...
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("quotation/function"), "invalid constant type", e);

		    return idio_S_notreached;
		case IDIO_TYPE_STRUCT_INSTANCE:
		    /*
		     * Needed for conditions, pathnames, ...
		     */
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
		    /*
		     * Test Case: ??
		     *
		     * Should only be a developer coding error...
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("quotation/various"), "invalid constant type", e);

		    return idio_S_notreached;
		default:
		    /*
		     * Test Case: ??
		     *
		     * Definitely a developer coding error...
		     */
		    idio_meaning_evaluation_error_param_nil (lo, IDIO_C_FUNC_LOCATION_S ("quotation/various"), "unimplemented type", e);

		    return idio_S_notreached;
		    break;
		}
	    }
	    break;
	default:
	    /* inconceivable! */
	    idio_error_printf (IDIO_C_FUNC_LOCATION_S ("quotation"), "v=n/k o=%#p o&3=%x F=%x C=%x P=%x", e, (intptr_t) e & IDIO_TYPE_MASK, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);

	    return idio_S_notreached;
	}
    }

    /*
     * Can I get here?
     */
    idio_error_C ("meaning", e, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO idio_evaluate (IDIO lo, IDIO cs)
{
    IDIO_ASSERT (lo);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (struct_instance, lo);

    /* idio_debug ("evaluate: e %s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_EXPR)); */
    /* idio_debug (" in %s\n", IDIO_MODULE_NAME (idio_thread_current_module ())); */

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
