/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ffi.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "codegen.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "keyword.h"
#include "module.h"
#include "pair.h"
#include "primitive.h"
#include "read.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"

/*
 * There are three layers of environment in which you might find a
 * variable.  idio_meaning_variable_info is used to return an
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
 *    lists of association lists of the formal parameter names to
 *    (name idio_S_param j) where j is the jth formal parameter in
 *    that frame

 *    in addition we extend the lists of association lists of the
 *    locally scoped variable names to (name idio_S_local j) where j
 *    is the jth variable in that frame -- j starts at
 *    length(formals)+1 (for varargs)

 *    During execution we will be creating matching activation frames
 *    accessible through the *frame* register and i/j can be used to
 *    dereference through *frame* to access the value.

 * 2. in symbols of the current module

 *    In the text these are denoted as toplevel names and are denoted
 *    as (scope i) where scope can be idio_S_toplevel, idio_S_dynamic or
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
 *    *primitives* for the usual culprits ("+", "append-string", etc.)
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

IDIO idio_evaluate_module = idio_S_nil;
static IDIO idio_meaning_predef_extend_string = idio_S_nil;
static IDIO idio_meaning_toplevel_extend_string = idio_S_nil;
static IDIO idio_meaning_dynamic_extend_string = idio_S_nil;
static IDIO idio_meaning_environ_extend_string = idio_S_nil;
static IDIO idio_meaning_define_gvi0_string = idio_S_nil;
static IDIO idio_meaning_define_infix_operator_string = idio_S_nil;
static IDIO idio_meaning_define_postfix_operator_string = idio_S_nil;

void idio_meaning_dump_src_properties (const char *prefix, const char*name, IDIO e)
{
    IDIO_ASSERT (e);

    fprintf (stderr, "SRC %-10s %-14s=", prefix, name);
    idio_debug ("%s\n", e);

    if (idio_isa_pair (e)) {
	IDIO lo = idio_hash_ref (idio_src_properties, e);
	if (idio_S_unspec == lo){
	    idio_debug ("                              %s\n", lo);
	} else {
	    idio_debug ("                              %s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME));
	    idio_debug (": line % 3s\n", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE));
	}
    }
}

static IDIO idio_meaning_error_location (IDIO src)
{
    IDIO_ASSERT (src);

    IDIO lo = idio_S_nil;
    if (idio_S_nil != src) {
	lo = idio_hash_ref (idio_src_properties, src);
	if (idio_S_unspec == lo) {
	    lo = idio_S_nil;
	}
    }

    IDIO lsh = idio_open_output_string_handle_C ();
    if (idio_S_nil == lo) {
	idio_display_C ("<no lexobj> ", lsh);
    } else {
	idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_NAME), lsh);
	idio_display_C (":line ", lsh);
	idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_LINE), lsh);
    }

    return idio_get_output_string (lsh);
}

/*
 * all evaluation errors are decendents of ^evaluation-error
 */
static void idio_meaning_base_error (IDIO src, IDIO c_location, IDIO msg, IDIO expr)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (msg);
    IDIO_ASSERT (expr);

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

    IDIO location = idio_meaning_error_location (src);

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_evaluation_error_type,
				   IDIO_LIST4 (msg,
					       location,
					       detail,
					       expr));
    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

void idio_meaning_error_param_type (IDIO src, IDIO c_location, char *msg, IDIO expr)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("parameter type: ", sh);
    idio_display_C (msg, sh);

    idio_meaning_base_error (src, c_location, idio_get_output_string (sh), expr);

    /* notreached */
}

static void idio_meaning_error_param (IDIO src, IDIO c_location, char *msg, IDIO expr)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display (expr, sh);
    idio_display_C (": ", sh);
    idio_display_C (msg, sh);

    idio_meaning_base_error (src, c_location, idio_get_output_string (sh), expr);

    /* notreached */
}

void idio_meaning_evaluation_error (IDIO src, IDIO c_location, char *msg, IDIO expr)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (msg, sh);

    idio_meaning_base_error (src, c_location, idio_get_output_string (sh), expr);

    /* notreached */
}

/*
 * There are half a dozen calls to
 * idio_meaning_error_static_redefine() in places where it requires we
 * have broken the code to raise it.
 */
void idio_meaning_error_static_redefine (IDIO src, IDIO c_location, char *msg, IDIO name, IDIO cv, IDIO new)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cv);
    IDIO_ASSERT (new);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (src);

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

    /* notreached */
}

/*
 * The calls to idio_meaning_error_static_variable() requires we have
 * broken the code to raise it.
 */
static void idio_meaning_error_static_variable (IDIO src, IDIO c_location, char *msg, IDIO name)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (src);

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

    /* notreached */
}

static void idio_meaning_error_static_unbound (IDIO src, IDIO c_location, IDIO name)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    idio_meaning_error_static_variable (src, c_location, "unbound", name);

    /* notreached */
}

/*
 * The calls to idio_meaning_error_static_immutable() requires we have
 * broken the code to raise it.
 */
static void idio_meaning_error_static_immutable (IDIO src, IDIO c_location, IDIO name)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    idio_meaning_error_static_variable (src, c_location, "immutable", name);

    /* notreached */
}

void idio_meaning_error_static_arity (IDIO src, IDIO c_location, char *msg, IDIO args)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (list, args);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (src);

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

    /* notreached */
}

static void idio_meaning_error_static_primitive_arity (IDIO src, IDIO c_location, char *msg, IDIO f, IDIO args, IDIO primdata)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (f);
    IDIO_ASSERT (args);
    IDIO_ASSERT (primdata);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, f);
    IDIO_TYPE_ASSERT (list, args);
    IDIO_TYPE_ASSERT (primitive, primdata);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_meaning_error_location (src);

    IDIO dsh = idio_open_output_string_handle_C ();
    char em[BUFSIZ];
    sprintf (em, "ARITY != %" PRId8 "%s; primitive (", IDIO_PRIMITIVE_ARITY (primdata), IDIO_PRIMITIVE_VARARGS (primdata) ? "+" : "");
    idio_display_C (em, dsh);
    idio_display (f, dsh);
    idio_display_C (" ", dsh);
    IDIO sigstr = idio_ref_property (primdata, idio_KW_sigstr, IDIO_LIST1 (idio_S_nil));
    idio_display (sigstr, dsh);
    idio_display_C (") was called as (", dsh);
    idio_display (f, dsh);
    if (idio_S_nil != args) {
	idio_display_C (" ", dsh);
	size_t size = 0;
	char *s = idio_display_string (args, &size);
	idio_display_C_len (s + 1, size - 2, dsh);
	IDIO_GC_FREE (s);
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

    /* notreached */
}

#define IDIO_MEANING_PREDEF_FLAG_NONE	0
#define IDIO_MEANING_PREDEF_FLAG_EXPORT	1

static IDIO idio_meaning_predef_extend (idio_primitive_desc_t *d, int flags, IDIO module, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (module);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (array, cs);

    IDIO primdata = idio_primitive_data (d);
    IDIO name = idio_symbols_C_intern (d->name);

    if (IDIO_MEANING_PREDEF_FLAG_EXPORT == flags) {
	IDIO_MODULE_EXPORTS (module) = idio_pair (name, IDIO_MODULE_EXPORTS (module));
    }
    IDIO si = idio_module_find_symbol (name, module);
    if (idio_S_false != si) {
	IDIO fgvi = IDIO_PAIR_HTT (si);
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

	    idio_meaning_error_static_redefine (name, IDIO_C_FUNC_LOCATION(), "primitive value change", name, pd, primdata);

	    return idio_S_notreached;
	} else {
	    return fgvi;
	}
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);
    idio_module_set_vci (module, fmci, fmci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_set_vvi (module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_predef, fmci, fgvi, module, idio_meaning_predef_extend_string), module);

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

    return idio_meaning_predef_extend (d, IDIO_MEANING_PREDEF_FLAG_NONE, module, cs, cpp__FILE__, cpp__LINE__);
}

IDIO idio_export_module_primitive (IDIO module, idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_ASSERT (module);
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (module, module);
    IDIO_TYPE_ASSERT (array, cs);

    return idio_meaning_predef_extend (d, IDIO_MEANING_PREDEF_FLAG_EXPORT, module, cs, cpp__FILE__, cpp__LINE__);
}

IDIO idio_add_primitive (idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    return idio_export_module_primitive (idio_primitives_module_instance (), d, cs, cpp__FILE__, cpp__LINE__);
}

IDIO idio_toplevel_extend (IDIO src, IDIO name, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO scope;
    switch (IDIO_MEANING_SCOPE (flags)) {
    case IDIO_MEANING_FLAG_TOPLEVEL_SCOPE:
	scope = idio_S_toplevel;
	break;
    case IDIO_MEANING_FLAG_DYNAMIC_SCOPE:
	scope = idio_S_dynamic;
	break;
    case IDIO_MEANING_FLAG_ENVIRON_SCOPE:
	scope = idio_S_environ;
	break;
    case IDIO_MEANING_FLAG_COMPUTED_SCOPE:
	scope = idio_S_computed;
	break;
    default:
	/*
	 * Shouldn't get here without a developer coding error.
	 */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected toplevel variable scope %#x", flags);

	return idio_S_notreached;
    }

    IDIO si = idio_module_find_symbol (name, cm);
    if (idio_S_false != si) {
	IDIO curscope = IDIO_PAIR_H (si);
	if (scope != curscope) {
	    if (idio_S_predef != curscope) {
		/*
		 * I'm not sure we can get here as once the name
		 * exists in a toplevel then it we be found again
		 * before we can re-extend the toplevel.
		 *
		 * One to look out for.
		 */
		idio_meaning_error_static_redefine (src, IDIO_C_FUNC_LOCATION (), "toplevel-extend: type change", name, si, scope);

		return idio_S_notreached;
	    }
	} else {
	    return IDIO_PAIR_HT (si);
	}
    }

#ifdef IDIO_EVALUATE_DEBUG
    if (idio_S_toplevel == scope &&
	0 == IDIO_MEANING_IS_DEFINE (flags)) {
	idio_debug ("C/toplevel-extend: forward reference to %s at ", name);
	idio_debug ("%s\n", idio_meaning_error_location (src));
	if (idio_S_unspec == idio_hash_ref (idio_src_properties, src)) {
	    idio_debug ("src=%s\n", src);
	}
    }
#endif

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    /*
     * NB a vi of 0 indicates an unresolved value index to be resolved
     * (based on the current set of imports) during runtime
     */
    idio_module_set_symbol (name, IDIO_LIST5 (scope, fmci, idio_fixnum (0), cm, idio_meaning_toplevel_extend_string), cm);

    return fmci;
}

IDIO idio_dynamic_extend (IDIO src, IDIO name, IDIO val, IDIO cs)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (val);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (array, cs);

    IDIO im = idio_Idio_module_instance ();

    IDIO si = idio_module_find_symbol (name, im);

    if (idio_S_false != si) {
	IDIO scope = IDIO_PAIR_H (si);
	IDIO fmci = IDIO_PAIR_HT (si);

	if (idio_S_dynamic != scope) {
	    /*
	     * I'm not sure we can get here as once the name exists in
	     * a toplevel then it we be found again before we can
	     * re-extend the toplevel.
	     *
	     * One to look out for.
	     */
	    idio_meaning_error_static_redefine (src, IDIO_C_FUNC_LOCATION (), "dynamic-extend: type change", name, si, scope);

	    return idio_S_notreached;
	} else {
	    return fmci;
	}
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    si = idio_vm_add_dynamic (im, fmci, fgvi, idio_meaning_dynamic_extend_string);
    idio_module_set_symbol (name, si, im);
    idio_module_set_symbol_value (name, val, im);

    return fmci;
}

IDIO idio_environ_extend (IDIO src, IDIO name, IDIO val, IDIO cs)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (val);
    IDIO_ASSERT (cs);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (array, cs);

    IDIO im = idio_Idio_module_instance ();

    IDIO si = idio_module_find_symbol (name, im);

    if (idio_S_false != si) {
	IDIO scope = IDIO_PAIR_H (si);
	IDIO fmci = IDIO_PAIR_HT (si);

	if (idio_S_environ != scope) {
	    /*
	     * I'm not sure we can get here as once the name exists in
	     * a toplevel then it we be found again before we can
	     * re-extend the toplevel.
	     *
	     * One to look out for.
	     */
	    idio_meaning_error_static_redefine (src, IDIO_C_FUNC_LOCATION (), "environ-extend: type change", name, si, scope);

	    return idio_S_notreached;
	} else {
	    return fmci;
	}
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    si = idio_vm_add_environ (im, fmci, fgvi, idio_meaning_environ_extend_string);
    idio_module_set_symbol (name, si, im);
    idio_module_set_symbol_value (name, val, im);

    return fmci;
}

/*
 * idio_meaning_lexical_lookup
 *
 * {nametree} is a list of association lists with each association
 * list representing the names of variables introduced at some level
 * with newer levels preceding older ones with the effect that at the
 * time of lookup the innermost level is the first association list
 * and will therefore be searched first.
 *
 * local variables for a given level are stashed as (name 'local j)
 * where {name} is the {j}th variable introduced at that level.
 *
 * Nominally, you would walk through each "level" of names looking for
 * your variable name.  If you found it at depth {i} then you can
 * combine that with the corresponding {j} to give a SHALLOW (for {i}
 * == 0) or DEEP (for {i} > 0) variable reference.
 *
 * The return for a local is ('local {i} {j})
 *
 * It is interspersed with dynamic and environ variables.  They are
 * one-at-a-time variable introductions and they should not increment
 * {i}!
 *
 * The return is, say, ('dynamic {mci}) where {mci} is the constant
 * index associated with the dynamic/environ variable.
 */
static IDIO idio_meaning_lexical_lookup (IDIO src, IDIO nametree, IDIO name)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);

    size_t i = 0;

    while (idio_S_nil != nametree) {
	IDIO names = IDIO_PAIR_H (nametree);

	if (idio_isa_pair (names)) {
	    IDIO assq = idio_list_assq (name, names);
	    if (idio_S_false != assq) {
		IDIO scope = IDIO_PAIR_HT (assq);
		if (idio_S_param == scope ||
		    idio_S_local == scope) {
		    return IDIO_LIST3 (scope, idio_fixnum (i), IDIO_PAIR_HTT (assq));
		} else if (idio_S_dynamic == scope ||
			   idio_S_environ == scope) {
		    return IDIO_PAIR_T (assq);
		} else {
		    /*
		     * I'm not sure we can get here without a
		     * coding error.
		     *
		     * One for developers.
		     */
		    idio_meaning_error_static_variable (src, IDIO_C_FUNC_LOCATION (), "unexpected local variant", name);

		    return idio_S_notreached;
		}
	    }

	    /*
	     * Only increment i if the names represented formal
	     * parameters
	     */
	    IDIO first = IDIO_PAIR_H (names);
	    if (idio_S_param == IDIO_PAIR_HT (first)) {
		i++;
	    }
	} else {
	    i++;
	}

	nametree = IDIO_PAIR_T (nametree);
    }

    return idio_S_false;
}

static IDIO idio_meaning_nametree_to_list (IDIO nametree)
{
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO r = idio_S_nil;

    /*
     * This loop is to gather the current (first) list of nametree
     * names.
     *
     * It is complicated because we have a nested set of locals to
     * catch on the way.
     */

    int done = 0;
    while (! done) {
	if (idio_S_nil != nametree) {
	    IDIO v_list = IDIO_PAIR_H (nametree);
	    while (idio_S_nil != v_list) {
		IDIO v_data = IDIO_PAIR_H (v_list);

		r = idio_pair (IDIO_PAIR_H (v_data), r);

		if (! done &&
		    idio_S_param == IDIO_PAIR_HT (v_data)) {
		    done = 1;
		}

		v_list = IDIO_PAIR_T (v_list);
	    }

	    nametree = IDIO_PAIR_T (nametree);
	}
    }

    return r;
}

static IDIO idio_meaning_nametree_extend_vparams (IDIO nametree, IDIO names)
{
    IDIO_ASSERT (names);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, names);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO alist = idio_S_nil;
    idio_ai_t i = 0;

    while (idio_S_nil != names) {
	alist = idio_pair (IDIO_LIST3 (IDIO_PAIR_H (names), idio_S_param, idio_fixnum (i++)), alist);
	names = IDIO_PAIR_T (names);
    }

    /*
     * Leave the order reversed (the entries know their own position)
     * for consistency with locals
     */

    return idio_pair (alist, nametree);
}

static IDIO idio_meaning_nametree_extend_params (IDIO nametree, IDIO names)
{
    IDIO_ASSERT (names);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, names);
    IDIO_TYPE_ASSERT (list, nametree);

    /*
     * Consume a slot for varargs
     */
    names = idio_list_append2 (names, IDIO_LIST1 (idio_S_false));

    return idio_meaning_nametree_extend_vparams (nametree, names);
}

static IDIO idio_meaning_nametree_extend_locals (IDIO nametree, IDIO names)
{
    IDIO_ASSERT (names);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, names);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO alist = idio_S_nil;

    /*
     * The correct value of i depends on what the params to the
     * current scope are.
     *
     * Given n params plus 1 varargs the starting value of i for a
     * local is n+1 (remembering the slots start at 0)
     */
    idio_ai_t i = 0;
    IDIO nt = nametree;
    while (idio_S_nil != nt) {
	IDIO ns = IDIO_PAIR_H (nt);
	if (idio_S_nil == ns) {
	    /*
	     * a thunk
	     */
	    alist = IDIO_LIST3 (idio_S_false, idio_S_param, idio_fixnum (0));
	    break;
	} else {
	    IDIO first = IDIO_PAIR_H (ns);
	    if (idio_S_local == IDIO_PAIR_HT (first)) {
		i++;
	    } else if (idio_S_param == IDIO_PAIR_HT (first)) {
		i += idio_list_length (ns);
		break;
	    }
	}
	nt = IDIO_PAIR_T (nt);
    }

    while (idio_S_nil != names) {
	alist = idio_pair (IDIO_LIST3 (IDIO_PAIR_H (names), idio_S_local, idio_fixnum (i++)), alist);
	names = IDIO_PAIR_T (names);
    }

    return idio_pair (alist, nametree);
}

static IDIO idio_meaning_nametree_dynamic_extend (IDIO nametree, IDIO name, IDIO index, IDIO scope)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (index);
    IDIO_ASSERT (scope);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (fixnum, index);

    return idio_pair (IDIO_LIST1 (IDIO_LIST3 (name, scope, index)), nametree);
}

static IDIO idio_meaning_variable_info (IDIO src, IDIO nametree, IDIO name, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO r = idio_meaning_lexical_lookup (src, nametree, name);

    if (idio_S_false == r) {
	/*
	 * NOTICE This must be a recursive lookup.  Otherwise we'll
	 * not see any bindings in Idio or *primitives*
	 */
        r = idio_module_find_symbol_recurse (name, cm, 1);
	if (idio_S_false == r) {

	    r = idio_module_direct_reference (name);

	    if (idio_S_false != r) {
		r = IDIO_PAIR_HTT (r);
		idio_module_set_symbol (name, r, cm);
	    } else {
		/*
		 * auto-extend the toplevel with this unknown variable
		 * -- we should (eventually) see a definition for it
		 */
		idio_toplevel_extend (src, name, flags, cs, cm);
		r = idio_module_find_symbol (name, cm);
	    }
	}
    }

    return r;
}

static IDIO idio_meaning_prefer_properties (IDIO e, IDIO src)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (src);

    if (idio_isa_pair (e) &&
	idio_S_unspec != idio_hash_ref (idio_src_properties, e)) {
	return e;
    } else {
	return src;
    }
}

#define IDIO_MPP(e,src) idio_meaning_prefer_properties (e, src)

void idio_meaning_copy_src_properties (IDIO src, IDIO dst)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (dst);

    if (idio_isa_pair (dst)) {
	IDIO dlo = idio_hash_ref (idio_src_properties, dst);
	if (idio_S_unspec == dlo) {
	    if (idio_isa_pair (src)) {
		IDIO slo = idio_hash_ref (idio_src_properties, src);
		if (idio_S_unspec == slo) {
#ifdef IDIO_DEBUG
		    if (idio_S_function == IDIO_PAIR_H (src)) {
			idio_debug ("im_csp !!!! no src lo for\n src=%s\n", src);
			idio_debug (" dst=%s\n", dst);
			IDIO_C_ASSERT (0);
		    }
#endif
		} else {
		    dlo = idio_copy (slo, IDIO_COPY_SHALLOW);
		    idio_struct_instance_set_direct (dlo, IDIO_LEXOBJ_EXPR, dst);
		    idio_hash_put (idio_src_properties, dst, dlo);
		}
	    }
	}
    }
}

static IDIO idio_meaning (IDIO src, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_reference (IDIO src, IDIO name, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO si = idio_meaning_variable_info (src, nametree, name, flags, cs, cm);

    if (idio_S_unspec == si) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), name);

	return idio_S_notreached;
    }

    IDIO scope = IDIO_PAIR_H (si);
    IDIO fmci = IDIO_PAIR_HT (si);

    if (idio_S_param == scope ||
	idio_S_local == scope) {
	IDIO fvi = IDIO_PAIR_HTT (si);
	if (0 == IDIO_FIXNUM_VAL (fmci)) {
	    return IDIO_LIST2 (IDIO_I_SHALLOW_ARGUMENT_REF, fvi);
	} else {
	    return IDIO_LIST3 (IDIO_I_DEEP_ARGUMENT_REF, fmci, fvi);
	}
    } else if (idio_S_toplevel == scope) {
	IDIO fgvi = IDIO_PAIR_HTT (si);
	if (IDIO_FIXNUM_VAL (fgvi) > 0) {
	    return IDIO_LIST2 (IDIO_I_CHECKED_GLOBAL_VAL_REF, fgvi);
	} else {
	    return IDIO_LIST2 (IDIO_I_GLOBAL_SYM_REF, fmci);
	}
    } else if (idio_S_dynamic == scope) {
	return IDIO_LIST2 (IDIO_I_DYNAMIC_SYM_REF, fmci);
    } else if (idio_S_environ == scope) {
	return IDIO_LIST2 (IDIO_I_ENVIRON_SYM_REF, fmci);
    } else if (idio_S_computed == scope) {
	return IDIO_LIST2 (IDIO_I_COMPUTED_SYM_REF, fmci);
    } else if (idio_S_predef == scope) {
	IDIO fgvi = IDIO_PAIR_HTT (si);
	return IDIO_LIST2 (IDIO_I_PREDEFINED, fgvi);
    } else {
	/*
	 * Shouldn't get here unless the if clauses above don't cover
	 * everything.
	 *
	 * Developer error.
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), name);

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_function_reference (IDIO src, IDIO name, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO si = idio_meaning_variable_info (src, nametree, name, IDIO_MEANING_TOPLEVEL_SCOPE (flags), cs, cm);

    if (idio_S_unspec == si) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), name);

	return idio_S_notreached;
    }

    IDIO scope = IDIO_PAIR_H (si);
    IDIO fmci = IDIO_PAIR_HT (si);

    if (idio_S_param == scope ||
	idio_S_local == scope) {
	IDIO fvi = IDIO_PAIR_HTT (si);
	if (0 == IDIO_FIXNUM_VAL (fmci)) {
	    return IDIO_LIST2 (IDIO_I_SHALLOW_ARGUMENT_REF, fvi);
	} else {
	    return IDIO_LIST3 (IDIO_I_DEEP_ARGUMENT_REF, fmci, fvi);
	}
    } else if (idio_S_toplevel == scope) {
	IDIO fgvi = IDIO_PAIR_HTT (si);
	if (IDIO_FIXNUM_VAL (fgvi) > 0) {
	    return IDIO_LIST2 (IDIO_I_CHECKED_GLOBAL_FUNCTION_VAL_REF, fgvi);
	} else {
	    return IDIO_LIST2 (IDIO_I_GLOBAL_FUNCTION_SYM_REF, fmci);
	}
    } else if (idio_S_dynamic == scope) {
	return IDIO_LIST2 (IDIO_I_DYNAMIC_FUNCTION_SYM_REF, fmci);
    } else if (idio_S_environ == scope) {
	return IDIO_LIST2 (IDIO_I_ENVIRON_SYM_REF, fmci);
    } else if (idio_S_computed == scope) {
	return IDIO_LIST2 (IDIO_I_COMPUTED_SYM_REF, fmci);
    } else if (idio_S_predef == scope) {
	IDIO fgvi = IDIO_PAIR_HTT (si);
	return IDIO_LIST2 (IDIO_I_PREDEFINED, fgvi);
    } else {
	/*
	 * Shouldn't get here unless the if clauses above don't cover
	 * everything.
	 *
	 * Developer error.
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), name);

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_escape (IDIO src, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning (IDIO_MPP (e, src), e, nametree, escapes, flags, cs, cm);
}

static IDIO idio_meaning_quotation (IDIO src, IDIO v, IDIO nametree, IDIO escapes, int flags)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (v);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);

    return IDIO_LIST2 (IDIO_I_CONSTANT_SYM_REF, v);
}

static IDIO idio_meaning_dequasiquote (IDIO src, IDIO e, int level, int indent)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);

    /*
     * {src} is a problem for us, here.  We enter with {src} being the
     * whole template (the quasi-quoted form), pretty much as we would
     * expect.
     *
     * Of course the template is multiple lines long and the reader
     * should have given each of those lines (and sub-expressions)
     * some source properties.
     *
     * However, our problem here in de-quasiquote is that we will
     * descend down inside all expressions until we find individual
     * symbols, say, at which point we will generate a mini-form,
     * (quote symbol), say, which we intend to be re-evaluated.
     *
     * Well, that mini-form needs some source properties but in the
     * case of our symbol, a symbol cannot have any source properties
     * itself so the {src} for source properties needs to have been
     * left behind at something that did have some source properties.
     *
     * In other words, we need to check carefully that what we pass in
     * as {src} does have some source properties worth passing.
     *
     * XXX
     *
     * Careful, whilst we can create source properties for all of the
     * activities of the template -- which are going to be a lot of
     * calls to {pair}, most of the time -- those calls to {pair}, the
     * expansion of the template are encoded as an expander and become
     * intermediate code and the *result* of them, the code to be
     * implemented, is a runtime thing.
     *
     * That is, all this effort fiddling with {src} here in
     * idio_meaning_dequasiquote() will get us some static evaluation
     * error handling of the template.
     *
     * In the future, the *application* of the template (ie. the call
     * to the template) will itself have a source property and that is
     * the only thing that can get passed to the expansion of the
     * template (at runtime).  So all of the expanded lines of code
     * will bear the source property of the single line where the
     * template was applied.  (We do *that* in a loop in
     * idio_meaning_expander().)
     *
     * That's sort of what you expect, the application, the use of the
     * template was at whatever line but it won't help us much if the
     * template has ballooned out to hundreds of lines of code and we
     * have a runtime error in its midst.
     *
     * Given that templates can begat templates can begat ... any hope
     * of reporting the line of the template (within the line of the
     * template (within the line of the template (...))) becomes a bit
     * onerous.  I suspect that's why in Scheme-ly languages you
     * simply get a complaint about the line where the template was
     * applied and then you're left running (template-expand) with your
     * arguments to figure out what went wrong.
     */

    IDIO r = idio_S_undef;

    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	if (idio_S_quasiquote == eh) {
	    /*
	     * e	~ (` v)
	     *
	     * (list 'quasiquote (de-qq (pht e) (level + 1)))
	     */

	    IDIO pht = IDIO_PAIR_HT (e);

	    if (idio_S_nil != pht &&
		idio_hash_exists_key (idio_src_properties, pht)) {
		src = IDIO_PAIR_HT (e);
	    }

	    r = IDIO_LIST3 (idio_S_list,
			    IDIO_LIST2 (idio_S_quote, idio_S_quasiquote),
			    idio_meaning_dequasiquote (src, pht, level + 1, indent + 1));
	} else if (idio_S_unquote == eh) {
	    if (level <= 0) {
		r = IDIO_PAIR_HT (e);
	    } else {
		/* ('list ''unquote (de-qq (pht e) (- level 1))) */

		IDIO pht = IDIO_PAIR_HT (e);

		if (idio_S_nil != pht &&
		    idio_hash_exists_key (idio_src_properties, pht)) {
		    src = IDIO_PAIR_HT (e);
		}

		r = IDIO_LIST3 (idio_S_list,
				IDIO_LIST2 (idio_S_quote, idio_S_unquote),
				idio_meaning_dequasiquote (src, pht, level - 1, indent + 1));
	    }
	} else if (idio_S_unquotesplicing == eh) {
	    if (level <= 0) {

		IDIO src_h = src;
		IDIO src_t = src;

		IDIO ph = IDIO_PAIR_H (e);
		IDIO pt = IDIO_PAIR_T (e);

		if (idio_S_nil != ph &&
		    idio_hash_exists_key (idio_src_properties, ph)) {
		    src_h = ph;
		}
		if (idio_S_nil != pt &&
		    idio_hash_exists_key (idio_src_properties, pt)) {
		    src_t = pt;
		}

		r = IDIO_LIST3 (idio_S_pair,
				idio_meaning_dequasiquote (src_h, ph,level, indent + 1),
				idio_meaning_dequasiquote (src_t, pt, level, indent + 1));
	    } else {
		/* ('list ''unquotesplicing (de-qq (pht e) (- level 1))) */

		IDIO pht = IDIO_PAIR_HT (e);

		if (idio_S_nil != pht &&
		    idio_hash_exists_key (idio_src_properties, pht)) {
		    src = pht;
		}

		r = IDIO_LIST3 (idio_S_list,
				IDIO_LIST2 (idio_S_quote, idio_S_unquotesplicing),
				idio_meaning_dequasiquote (src, pht, level - 1, indent + 1));
	    }
	} else if (level <= 0 &&
		   idio_isa_pair (IDIO_PAIR_H (e)) &&
		   idio_S_unquotesplicing == IDIO_PAIR_HH (e)) {
	    if (idio_S_nil == IDIO_PAIR_T (e)) {
		r = IDIO_PAIR_HTH (e);
	    } else {
		/* ('append (phth e) (de-qq (pt e) level)) */

		IDIO pt = IDIO_PAIR_T (e);

		if (idio_S_nil != pt &&
		    idio_hash_exists_key (idio_src_properties, pt)) {
		    src = pt;
		}

		r = IDIO_LIST3 (idio_S_append,
				IDIO_PAIR_HTH (e),
				idio_meaning_dequasiquote (src, pt, level, indent + 1));
	    }
	} else {
	    IDIO src_h = src;
	    IDIO src_t = src;

	    IDIO ph = IDIO_PAIR_H (e);
	    IDIO pt = IDIO_PAIR_T (e);

	    if (idio_S_nil != ph &&
		idio_hash_exists_key (idio_src_properties, ph)) {
		src_h = ph;
	    }
	    if (idio_S_nil != pt &&
		idio_hash_exists_key (idio_src_properties, pt)) {
		src_t = pt;
	    }

	    r = IDIO_LIST3 (idio_S_pair,
			    idio_meaning_dequasiquote (src_h, ph, level, indent + 1),
			    idio_meaning_dequasiquote (src_t, pt, level, indent + 1));
	}
    } else if (idio_isa_array (e)) {
	IDIO iatl = idio_array_to_list (e);

	r = IDIO_LIST2 (idio_symbols_C_intern ("list->array"), idio_meaning_dequasiquote (iatl, iatl, level, indent + 1));
    } else if (idio_isa_symbol (e)) {
	r = IDIO_LIST2 (idio_S_quote, e);
    } else {
	r = e;
    }

    return r;
}

static IDIO idio_meaning_quasiquotation (IDIO src, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO dq = idio_meaning_dequasiquote (src, e, 0, 0);

    return idio_meaning (src, dq, nametree, escapes, flags, cs, cm);
}

static IDIO idio_meaning_alternative (IDIO src, IDIO e1, IDIO e2, IDIO e3, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e1);
    IDIO_ASSERT (e2);
    IDIO_ASSERT (e3);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m1 = idio_meaning (IDIO_MPP (e1, src), e1, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO m2 = idio_meaning (IDIO_MPP (e2, src), e2, nametree, escapes, flags, cs, cm);
    IDIO m3 = idio_meaning (IDIO_MPP (e3, src), e3, nametree, escapes, flags, cs, cm);

    return IDIO_LIST4 (IDIO_I_ALTERNATIVE, m1, m2, m3);
}

/*
 * validate & rewrite the cond clauses, ``clauses``, noting the
 * special cases of ``=>``, ``else``
 */
static IDIO idio_meaning_rewrite_cond (IDIO prev, IDIO src, IDIO clauses)
{
    IDIO_ASSERT (prev);
    IDIO_ASSERT (src);
    IDIO_ASSERT (clauses);

    if (idio_S_nil == clauses) {
	return idio_S_void;
    } else if (! idio_isa_pair (clauses)) {
	/*
	 * Shouldn't get here, eg. "(cond)", as we test that the cond
	 * clauses are a pair in the main idio_meaning loop.
	 */
	idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "cond: no clauses", clauses);

	return idio_S_notreached;
    } else if (! idio_isa_pair (IDIO_PAIR_H (clauses))) {
	/*
	 * Test Cases:
	 * evaluation-errors/rewrite-cond-isa-pair-only.idio
	 * evaluation-errors/rewrite-cond-isa-pair-before.idio
	 * evaluation-errors/rewrite-cond-isa-pair-after.idio
	 *
	 * cond #t
	 * cond #t (#f 1)
	 * cond (#f 1) #t
	 */
	if (idio_isa_pair (IDIO_PAIR_T (clauses)) &&
	    idio_isa_pair (IDIO_PAIR_HT (clauses))) {
	    idio_meaning_error_param_type (IDIO_PAIR_HT (clauses), IDIO_C_FUNC_LOCATION (), "cond: clause is not a pair (before)", clauses);
	} else {
	    /*
	     * Test Case: ??
	     */
	    idio_meaning_error_param_type (prev, IDIO_C_FUNC_LOCATION (), "cond: clause is not a pair (in/after)", clauses);
	}

	return idio_S_notreached;
    } else if (idio_S_else == IDIO_PAIR_HH (clauses)) {
	if (idio_S_nil == IDIO_PAIR_T (clauses)) {
	    IDIO cs = idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_TH (clauses));
	    idio_meaning_copy_src_properties (IDIO_MPP (IDIO_PAIR_TH (clauses), src), cs);
	    return cs;
	} else {
	    /*
	     * Test Case: evaluation-errors/rewrite-cond-else-not-last.idio
	     *
	     * cond (else 1) (#t 2)
	     */
	    idio_meaning_evaluation_error (IDIO_PAIR_H (clauses), IDIO_C_FUNC_LOCATION (), "cond: else not in last clause", clauses);

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

	IDIO ph_clauses = IDIO_PAIR_H (clauses);

	if (idio_isa_list (ph_clauses) &&
	    idio_list_length (ph_clauses) == 3) {
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
	    IDIO phh_clauses = IDIO_PAIR_HH (clauses);

	    IDIO appl = IDIO_LIST2 (IDIO_PAIR_HTTH (clauses),
				    gs);

	    IDIO let = IDIO_LIST3 (idio_S_let,
				   IDIO_LIST1 (IDIO_LIST2 (gs, phh_clauses)),
				   IDIO_LIST4 (idio_S_if,
					       gs,
					       appl,
					       idio_meaning_rewrite_cond (ph_clauses,
									  IDIO_MPP (IDIO_PAIR_T (clauses), src),
									  IDIO_PAIR_T (clauses))));
	    idio_meaning_copy_src_properties (IDIO_MPP (ph_clauses, src), let);

	    return let;
	} else {
	    /*
	     * Test Cases:
	     * evaluation-errors/rewrite-cond-apply-two-args.idio
	     * evaluation-errors/rewrite-cond-apply-four-args.idio
	     *
	     * cond (1 =>)
	     * cond (1 => a b)
	     */
	    if (idio_isa_pair (src)) {
		idio_meaning_evaluation_error (IDIO_PAIR_H (src), IDIO_C_FUNC_LOCATION (), "cond: invalid => clause", clauses);
	    } else {
		/*
		 * Test Case: ??
		 */
		idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "cond: invalid => clause", clauses);
	    }

	    return idio_S_notreached;
	}
    } else if (idio_S_nil == IDIO_PAIR_TH (clauses)) {
	IDIO gs = idio_gensym (NULL);
	/*
	  `(let ((gs ,(phh clauses)))
	     (or gs
	         ,(rewrite-cond-clauses (pt clauses))))
	*/
	IDIO cs = IDIO_LIST3 (idio_S_let,
			   IDIO_LIST1 (IDIO_LIST2 (gs, IDIO_PAIR_HH (clauses))),
			   IDIO_LIST3 (idio_S_or,
				       gs,
				       idio_meaning_rewrite_cond (IDIO_PAIR_H (clauses),
								  IDIO_MPP (IDIO_PAIR_T (clauses), src),
								  IDIO_PAIR_T (clauses))));
	idio_meaning_copy_src_properties (IDIO_MPP (IDIO_PAIR_H (clauses), src), cs);
	return cs;
    } else {
	IDIO c = IDIO_LIST4 (idio_S_if,
			     IDIO_PAIR_HH (clauses),
			     idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_TH (clauses)),
			     idio_meaning_rewrite_cond (IDIO_PAIR_H (clauses),
							IDIO_MPP (IDIO_PAIR_T (clauses), src),
							IDIO_PAIR_T (clauses)));
	idio_meaning_copy_src_properties (IDIO_MPP (IDIO_PAIR_H (clauses), src), c);
	return c;
    }
}

static IDIO idio_meaning_assignment (IDIO src, IDIO name, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (name)) {
	/*
	 * Notionally, we are invoked as
	 *
	 * set! name expr
	 *
	 * but here {name} is itself an expression:
	 *
	 * set! (foo x y z) e
	 *
	 * which we want to transform into
	 *
	 * ((setter foo) x y z e)
	 *
	 * ie. get the "setter" of {foo} and apply it to all of the
	 * arguments both of the "name-expression" and the original
	 * expr being passed to {set!}
	 */
	IDIO args = IDIO_PAIR_T (name);

	IDIO setter = IDIO_LIST2 (idio_S_setter, IDIO_PAIR_H (name));

	/*
	 * Nominally we could do with an append3() function here but
	 * two calls to append2() will have to do...
	 */
	IDIO se = idio_list_append2 (IDIO_LIST1 (setter), args);
	se = idio_list_append2 (se, IDIO_LIST1 (e));

	return idio_meaning (src, se, nametree, escapes, IDIO_MEANING_NO_DEFINE (flags), cs, cm);
    } else if (! idio_isa_symbol (name)) {
	/*
	 * Test Case: evaluation-errors/assign-non-symbol.idio
	 *
	 * 1 = 3
	 */
	idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "cannot assign to", name);

	return idio_S_notreached;
    }

    /*
     * Normal assignment to a symbol
     */

    int mflags = IDIO_MEANING_NO_DEFINE (IDIO_MEANING_NOT_TAILP (flags));

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, escapes, mflags, cs, cm);

    IDIO si = idio_meaning_variable_info (src, nametree, name, flags, cs, cm);

    if (idio_S_unspec == si) {
	/*
	 * Not sure we can get here as idio_meaning_variable_info()
	 * should have created a reference (otherwise, what's it
	 * doing?)
	 */
	idio_error_C ("unknown variable:", name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO scope = IDIO_PAIR_H (si);
    IDIO fmci = IDIO_PAIR_HT (si);

    IDIO assign = idio_S_nil;

    if (idio_S_param == scope ||
	idio_S_local == scope) {
	IDIO fvi = IDIO_PAIR_HTT (si);
	if (0 == IDIO_FIXNUM_VAL (fmci)) {
	    return IDIO_LIST3 (IDIO_I_SHALLOW_ARGUMENT_SET, fvi, m);
	} else {
	    return IDIO_LIST4 (IDIO_I_DEEP_ARGUMENT_SET, fmci, fvi, m);
	}
    } else if (idio_S_toplevel == scope) {
	IDIO fgvi = IDIO_PAIR_HTT (si);
	if (0 == IDIO_FIXNUM_VAL (fgvi)) {
	    assign = IDIO_LIST3 (IDIO_I_GLOBAL_SYM_SET, fmci, m);
	} else {
	    assign = IDIO_LIST3 (IDIO_I_GLOBAL_VAL_SET, fgvi, m);
	}
    } else if (idio_S_dynamic == scope ||
	       idio_S_environ == scope) {
	assign = IDIO_LIST3 (IDIO_I_GLOBAL_SYM_SET, fmci, m);
    } else if (idio_S_computed == scope) {
	if (IDIO_MEANING_IS_DEFINE (flags)) {
	    return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_GLOBAL_SYM_DEF, name, scope, fmci),
			       IDIO_LIST3 (IDIO_I_COMPUTED_SYM_DEF, fmci, m));
	} else {
	    return IDIO_LIST3 (IDIO_I_COMPUTED_SYM_SET, fmci, m);
	}
    } else if (idio_S_predef == scope) {
	/*
	 * We can shadow predefs...semantically dubious!
	 *
	 * Dubious because many (most?) functions use, say, ph,
	 * eg. map.  If you redefine ph should map be affected?  Is it
	 * your intention that by changing ph everything should use
	 * the new definition of ph immediately or just that functions
	 * defined after this should use the new definition?
	 *
	 * We need a new mci as the existing one is tagged as a
	 * predef.  This new one will be tagged as a toplevel.
	 */
	IDIO new_mci = idio_toplevel_extend (src, name, IDIO_MEANING_DEFINE (IDIO_MEANING_TOPLEVEL_SCOPE (flags)), cs, cm);

	/*
	 * But now we have a problem.
	 *
	 * As we *compile* regular code, any subsequent reference to
	 * {name} will find this new toplevel and we'll embed
	 * GLOBAL-SYM-REFs to it etc..  All good for the future when we
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
	IDIO fvi = IDIO_PAIR_HTT (si);
	idio_module_set_symbol_value (name, idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi)), cm);
	assign = IDIO_LIST3 (IDIO_I_GLOBAL_SYM_SET, new_mci, m);
	fmci = new_mci;

	/* if we weren't allowing shadowing */

	/* idio_meaning_error_static_immutable (name, IDIO_C_FUNC_LOCATION ()); */
	/* return idio_S_notreached; */
    } else {
	/*
	 * Should only get here if we haven't accounted for all
	 * variants above.
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), name);

	return idio_S_notreached;
    }

    if (IDIO_MEANING_IS_DEFINE (flags)) {
	return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_GLOBAL_SYM_DEF, name, scope, fmci),
			   assign);
    } else {
	return assign;
    }
}

static IDIO idio_meaning_define (IDIO src, IDIO name, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

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

	idio_meaning_copy_src_properties (src, e);
    } else {
	if (idio_isa_pair (e)) {
	    e = IDIO_PAIR_H (e);
	    idio_meaning_copy_src_properties (src, e);
	}
    }

    int define_flags = IDIO_MEANING_DEFINE (IDIO_MEANING_TOPLEVEL_SCOPE (flags));
    IDIO si = idio_meaning_variable_info (src, nametree, name, define_flags, cs, cm);

    /*
     * if the act of looking the variable up auto-created it then
     * actually give it a value slot as this *is* the definition of it
     */

    IDIO scope = IDIO_PAIR_H (si);
    IDIO fmci = IDIO_PAIR_HT (si);
    IDIO fgvi = IDIO_PAIR_HTT (si);
    if (idio_S_toplevel == scope &&
	0 == IDIO_FIXNUM_VAL (fgvi)) {
	idio_ai_t gvi = idio_vm_extend_values ();
	fgvi = idio_fixnum (gvi);
	si = IDIO_LIST5 (scope, fmci, fgvi, cm, idio_meaning_define_gvi0_string);
	idio_module_set_symbol (name, si, cm);
    }

    return idio_meaning_assignment (src, name, e, nametree, escapes, define_flags, cs, cm);
}

static IDIO idio_meaning_define_template (IDIO src, IDIO name, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /*
     * (define-template (name formal*) ...) => (define-template name (function (formal*) ...))
     */
    if (idio_isa_pair (name)) {
	e = IDIO_LIST3 (idio_S_function,
			IDIO_PAIR_T (name),
			e);
	name = IDIO_PAIR_H (name);

	idio_meaning_copy_src_properties (src, e);
    }

    /*
     * create an expander: (function (x e) (apply proc (pt x)))
     *
     * where proc is (function (formal*) ...) from above, ie. e
     */
    IDIO x_sym = idio_symbols_C_intern ("x");
    IDIO e_sym = idio_symbols_C_intern ("e");

    IDIO pt_x = IDIO_LIST2 (idio_S_pt, x_sym);
    idio_meaning_copy_src_properties (src, pt_x);

    IDIO appl = IDIO_LIST3 (idio_S_apply, e, pt_x);
    idio_meaning_copy_src_properties (src, appl);

    IDIO bindings = IDIO_LIST2 (x_sym, e_sym);

    IDIO dsh  = idio_open_output_string_handle_C ();
    idio_display_C ("define-template: ", dsh);
    idio_display (name, dsh);
    idio_display_C (" (x e)", dsh);

    IDIO docstr = idio_get_output_string (dsh);

    IDIO expander = IDIO_LIST4 (idio_S_function,
				bindings,
				docstr,
				appl);
    idio_meaning_copy_src_properties (src, expander);

    /*
     * In general (define-template a ...) means that "a" is associated
     * with an expander and that expander takes the pt of the
     * expression it is passed, "(a ...)" (ie. it skips over its own
     * name).
     *
     * It happens that people say
     *
     * (define-template %b ...)
     * (define-template b %b)
     *
     * (in particular where they are creating an enhanced version of b
     * which may require using the existing b to define itself hence
     * defining some other name, "%b", which can use "b" freely then
     * redefine b to this new version)
     *
     * However, we can't just use the current value of "%b" in
     * (define-template b %b) as this template-expander association
     * means we are replacing the nominal definition of a template
     * with an expander which takes two arguments and the body of
     * which will take the pt of its first argument.  Left alone,
     * expander "b" will take the pt then expander "%b" will take the
     * pt....  "A Pair-Tail Too Far", one would say, in hindsight and
     * thinking of the big budget movie potential.
     *
     * So catch the case where the value is already an expander and
     * replace it with the (obvious?) assignment with some
     * *expander-list* copying.
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
	    return idio_meaning_assignment (src, name, IDIO_PAIR_H (exp), nametree, escapes, IDIO_MEANING_TOPLEVEL_SCOPE (flags), cs, cm);
	}
    }

    /*
     * XXX define-template bootstrap
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
     * define-template who will have called idio_install_expander?
     *
     * In summary: we need the expander in the here and now as someone
     * might use it in the next line of source and we need to embed a
     * call to idio_install_expander in the object code for future
     * users.
     */

    int mflags = IDIO_MEANING_NOT_TAILP (IDIO_MEANING_TEMPLATE (IDIO_MEANING_DEFINE (IDIO_MEANING_TOPLEVEL_SCOPE (flags))));
    IDIO m_a = idio_meaning_assignment (expander, name, expander, nametree, escapes,
					mflags,
					cs, cm);

    idio_install_expander_source (name, expander, expander);

    /*
     * Generate a ci/vi for name.
     *
     * The ci is required so that the EXPANDER opcode can call
     * idio_install_expander() to overwrite the proc from the
     * idio_install_expander_source() call we just made
     *
     * The vi is because this is definitely a define
     */
    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    IDIO ce = idio_thread_current_env ();

    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_toplevel, fmci, idio_fixnum (0), ce, docstr), ce);

    return IDIO_LIST3 (IDIO_I_EXPANDER, idio_fixnum (mci), m_a);
}

static IDIO idio_meaning_define_infix_operator (IDIO src, IDIO name, IDIO pri, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (IDIO_FIXNUM_VAL (pri) < 0) {
	/*
	 * Test Case: evaluation-errors/infix-op-negative-priority.idio
	 *
	 * define-infix-operator qqq -300 ...
	 */
	idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "positive priority", pri);

	return idio_S_notreached;
    }

    /*
     * Step 1: find the existing symbol for {name} or create a new one
     */
    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_module_set_symbol (name,
			    IDIO_LIST5 (idio_S_toplevel,
					fmci,
					idio_fixnum (0),
					idio_operator_module,
					idio_meaning_define_infix_operator_string),
			    idio_operator_module);

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
	    idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "not an operator", e);

	    return idio_S_notreached;
	}

	IDIO find_module = IDIO_LIST2 (idio_symbols_C_intern ("find-module"),
				       IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module)));
	idio_meaning_copy_src_properties (src, find_module);

	IDIO sve = IDIO_LIST3 (idio_symbols_C_intern ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       find_module);
	idio_meaning_copy_src_properties (src, sve);

	idio_copy_infix_operator (name, pri, e);
	m = idio_meaning (sve, sve, nametree, escapes, flags, cs, cm);
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
			      idio_meaning_define_infix_operator_string,
			      e);

	idio_meaning_copy_src_properties (src, fe);

	m = idio_meaning (fe, fe, nametree, escapes, flags, cs, cm);
    }
    IDIO r = IDIO_LIST4 (IDIO_I_INFIX_OPERATOR, fmci, pri, m);

    return r;
}

static IDIO idio_meaning_define_postfix_operator (IDIO src, IDIO name, IDIO pri, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (IDIO_FIXNUM_VAL (pri) < 0) {
	/*
	 * Test Case: evaluation-errors/postfix-op-negative-priority.idio
	 *
	 * define-postfix-operator qqq -300 ...
	 */
	idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "positive priority", pri);

	return idio_S_notreached;
    }

    /*
     * Step 1: find the existing symbol for {name} or create a new one
     */
    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, name);
    IDIO fmci = idio_fixnum (mci);

    idio_module_set_symbol (name,
			    IDIO_LIST5 (idio_S_toplevel,
					fmci,
					idio_fixnum (0),
					idio_operator_module,
					idio_meaning_define_postfix_operator_string),
			    idio_operator_module);

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
	     * Test Case: evaluation-errors/postfix-op-not-an-operator.idio
	     *
	     * define-postfix-operator qqq 300 zzz
	     */
	    idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "not an operator", e);

	    return idio_S_notreached;
	}

	IDIO find_module = IDIO_LIST2 (idio_symbols_C_intern ("find-module"),
				       IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module)));
	idio_meaning_copy_src_properties (src, find_module);

	IDIO sve = IDIO_LIST3 (idio_symbols_C_intern ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       find_module);
	idio_meaning_copy_src_properties (src, sve);

	idio_copy_postfix_operator (name, pri, e);
	m = idio_meaning (sve, sve, nametree, escapes, flags, cs, cm);
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
			      idio_meaning_define_postfix_operator_string,
			      e);

	idio_meaning_copy_src_properties (src, fe);

	m = idio_meaning (fe, fe, nametree, escapes, flags, cs, cm);
    }
    IDIO r = IDIO_LIST4 (IDIO_I_POSTFIX_OPERATOR, fmci, pri, m);

    return r;
}

static IDIO idio_meaning_define_dynamic (IDIO src, IDIO name, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
	idio_meaning_copy_src_properties (src, e);
    }

    return idio_meaning_assignment (src, name, e, nametree, escapes, IDIO_MEANING_DEFINE (IDIO_MEANING_DYNAMIC_SCOPE (flags)), cs, cm);
}

static IDIO idio_meaning_define_environ (IDIO src, IDIO name, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
	idio_meaning_copy_src_properties (src, e);
    }

    return idio_meaning_assignment (src, name, e, nametree, escapes, IDIO_MEANING_DEFINE (IDIO_MEANING_ENVIRON_SCOPE (flags)), cs, cm);
}

/*
 * A computed variable's value should be a pair, (getter & setter).
 * Either of getter or setter can be #n.
 *
 * We shouldn't have both #n as it wouldn't be much use.
 */
static IDIO idio_meaning_define_computed (IDIO src, IDIO name, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
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
	idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "define-computed: no getter/setter", e);

	return idio_S_notreached;
    }

    if (idio_S_nil == getter &&
	idio_S_nil == setter) {
	/*
	 * Test Case: evaluation-errors/define-computed-no-args.idio
	 *
	 * C :$ #n
	 */
	idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "define-computed: no getter/setter", name);

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
    return idio_meaning_assignment (src,
				    name,
				    IDIO_LIST3 (idio_S_pair, getter, setter),
				    nametree,
				    escapes,
				    IDIO_MEANING_DEFINE (IDIO_MEANING_COMPUTED_SCOPE (flags)),
				    cs,
				    cm);
}

static IDIO idio_meaning_sequence (IDIO src, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO keyword, IDIO cs, IDIO cm);

static IDIO idio_meanings_single_sequence (IDIO src, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning (IDIO_MPP (e, src), e, nametree, escapes, flags, cs, cm);
}

static IDIO idio_meanings_multiple_sequence (IDIO src, IDIO e, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO keyword, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO mp = idio_meaning_sequence (src, ep, nametree, escapes, flags, keyword, cs, cm);

    if (idio_S_and == keyword) {
	return IDIO_LIST3 (IDIO_I_AND, m, mp);
    } else if (idio_S_or == keyword) {
	return IDIO_LIST3 (IDIO_I_OR, m, mp);
    } else if (idio_S_begin == keyword) {
	return IDIO_LIST3 (IDIO_I_BEGIN, m, mp);
    } else {
	/*
	 * Not sure we can get here as it requires developer coding
	 * error.
	 */
	idio_error_C ("unexpected sequence keyword", keyword, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_sequence (IDIO src, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO keyword, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

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
		c = IDIO_I_AND;
	    } else if (idio_S_or == keyword) {
		c = IDIO_I_OR;
	    } else if (idio_S_begin == keyword) {
		c = IDIO_I_BEGIN;
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

		IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, escapes, seq_flags, cs, cm);
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
	    return idio_meanings_single_sequence (IDIO_MPP (eph, src), eph, nametree, escapes, flags, cs, cm);
	}
    } else {
	/*
	 * Not sure we can get here as it requires developer coding
	 * error.  The args were checked in idio_meaning().
	 */
	idio_meaning_evaluation_error (keyword, IDIO_C_FUNC_LOCATION (), "sequence: not a pair", ep);

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_escape_block (IDIO src, IDIO label, IDIO be, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_fix_abstraction (IDIO src, IDIO ns, IDIO formals, IDIO docstr, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (formals);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    size_t arity = idio_list_length (ns);

    IDIO ent = idio_meaning_nametree_extend_params (nametree, ns);

    IDIO mp = idio_meaning_sequence (src, ep, ent, escapes, IDIO_MEANING_SET_TAILP (flags), idio_S_begin, cs, cm);

    return IDIO_LIST5 (IDIO_I_FIX_CLOSURE, mp, idio_fixnum (arity), idio_meaning_nametree_to_list (ent), docstr);
}

static IDIO idio_meaning_dotted_abstraction (IDIO src, IDIO ns, IDIO n, IDIO formals, IDIO docstr, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (formals);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    size_t arity = idio_list_length (ns);
    IDIO fix_formals = idio_list_append2 (ns, IDIO_LIST1 (n));

    IDIO ent = idio_meaning_nametree_extend_vparams (nametree, fix_formals);

    IDIO mp = idio_meaning_sequence (src, ep, ent, escapes, IDIO_MEANING_SET_TAILP (flags), idio_S_begin, cs, cm);

    return IDIO_LIST5 (IDIO_I_NARY_CLOSURE, mp, idio_fixnum (arity), idio_meaning_nametree_to_list (ent), docstr);
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
 *
 * 3. source properties
 *
 *    If we're inventing code snippets then we should make some small
 *    effort to ensure that the source properties of the original code
 *    are propagated to our snippet.  This is especially important
 *    when several of these snippets invoke expanders -- which need to
 *    do much the same themselves!
 */
static IDIO idio_meaning_rewrite_body_letrec (IDIO src, IDIO e, IDIO nametree);

static IDIO idio_meaning_rewrite_body (IDIO src, IDIO e, IDIO nametree)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);

    if (! idio_isa_pair (e)) {
	return e;
    }

    IDIO l = e;

    IDIO r = idio_S_nil;

    for (;;) {
	IDIO cur = idio_S_unspec;
	if (idio_S_nil == l) {
	    break;
	} else if (idio_isa_pair (l) &&
		   idio_isa_pair (IDIO_PAIR_H (l)) &&
		   idio_S_false != idio_expanderp (IDIO_PAIR_HH (l))) {
	    cur = idio_template_expands (IDIO_PAIR_H (l));
	} else {
	    cur = IDIO_PAIR_H (l);
	}
	idio_meaning_copy_src_properties (IDIO_MPP (IDIO_PAIR_H (l), src), cur);

	if (idio_isa_pair (cur) &&
	    idio_S_begin == IDIO_PAIR_H (cur)) {
	    /*  redundant begin */
	    l = idio_list_append2 (IDIO_PAIR_T (cur), IDIO_PAIR_T (l));
	    continue;
	} else if (idio_isa_pair (cur) &&
		   (idio_S_define == IDIO_PAIR_H (cur) ||
		    idio_S_colon_plus == IDIO_PAIR_H (cur))) {
	    /* :+ or define -> letrec */

	    r = idio_pair (idio_meaning_rewrite_body_letrec (IDIO_MPP (l, src), l, nametree), r);
	    break;
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_eq == IDIO_PAIR_H (cur)) {
	    /* := -> let* */

	    /*
	     * At this point we have:
	     *
	     * l	~ ((:= name value-expr) ...)
	     * cur	~ (:= name value-expr)
	     * (pt cur)	~ (name value-expr)
	     * (pt l)	~ ... representing subsequent expressions
	     *
	     * we can rewrite as:
	     *
	     * (let ((name value-expr))
	     *      (begin
	     *        ...
	     *        ))
	     *
	     * where we can recurse on ..., which is now the body of
	     * our (let)
	     */

	    /*
	     * binding == (pt cur) ~ (name value-expr)
	     *
	     * we should have gotten an evaluation error in
	     * idio_meaning() if value-expr was not supplied so I think we
	     * can just dive in
	     */
	    IDIO binding = IDIO_PAIR_T (cur);

	    /*
	     * We need to create a variable.  Nominally it would be
	     * constructed with a let => closed application with
	     * attendent frame creation, linking etc..
	     *
	     * We could extend the current frame with "locals" -- in
	     * effect, extending the parameter list.  If the variable
	     * doesn't already exist lexically or if it does exist but
	     * is beyond the current frame.
	     */
	    IDIO assign = idio_S_let;

	    if (idio_S_nil != nametree) {
		assign = idio_S_functionp;
	    }

	    IDIO body = idio_meaning_rewrite_body (IDIO_MPP (cur, src), IDIO_PAIR_T (l), nametree);

	    if (idio_S_nil == body) {
		/*
		 * What if {value-expr} has side-effects?
		 */
		fprintf (stderr, "imrb :=	OPT: empty body for let/functionp => no eval of {value-expr}\n");
		idio_debug ("src=%s\n", src);
		IDIO location = idio_vm_source_location ();
		idio_debug ("loc=%s\n", location);

		return idio_list_reverse (r);
	    }

	    IDIO body_sequence = idio_list_append2 (IDIO_LIST1 (idio_S_begin), body);

	    IDIO r_cur = idio_S_false;
	    if (idio_S_let == assign) {
		r_cur = IDIO_LIST3 (assign,
				    IDIO_LIST1 (binding),
				    body_sequence);
	    } else {
		/*
		 * (function+ var val body)
		 */
		r_cur = IDIO_LIST4 (assign, IDIO_PAIR_H (binding), IDIO_PAIR_HT (binding), body_sequence);
	    }

	    idio_meaning_copy_src_properties (IDIO_MPP (cur, src), r_cur);
	    r = idio_list_append2 (IDIO_LIST1 (r_cur), r);
	    return idio_list_reverse (r);
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_star == IDIO_PAIR_H (cur)) {
	    /* :* -> environ-let */

	    /*
	     * See commentary in := above
	     */
	    IDIO body = idio_meaning_rewrite_body (IDIO_MPP (cur, src), IDIO_PAIR_T (l), nametree);
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }

	    if (idio_S_nil == body) {
		fprintf (stderr, "imrb :*	OPT: empty body for environ-let => no eval of {value-expr}\n");
		return r;
	    }

	    IDIO body_sequence = idio_list_append2 (IDIO_LIST1 (idio_S_begin), body);

	    IDIO binding = IDIO_PAIR_T (cur);

	    IDIO r_cur = IDIO_LIST3 (idio_S_environ_let,
				     binding,
				     body_sequence);
	    idio_meaning_copy_src_properties (IDIO_MPP (cur, src), r_cur);

	    r = idio_list_append2 (r, IDIO_LIST1 (r_cur));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_excl_star == IDIO_PAIR_H (cur)) {
	    /* !* -> environ-unset */

	    /*
	     * See commentary in := above
	     */
	    IDIO body = idio_meaning_rewrite_body (IDIO_MPP (cur, src), IDIO_PAIR_T (l), nametree);
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }

	    if (idio_S_nil == body) {
		fprintf (stderr, "imrb !*	OPT: empty body for environ-unset\n");
		return r;
	    }

	    IDIO body_sequence = idio_list_append2 (IDIO_LIST1 (idio_S_begin), body);

	    IDIO r_cur = IDIO_LIST3 (idio_S_environ_unset,
				     IDIO_PAIR_HT (cur),
				     body_sequence);
	    idio_meaning_copy_src_properties (IDIO_MPP (cur, src), r_cur);

	    r = idio_list_append2 (r, IDIO_LIST1 (r_cur));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_tilde == IDIO_PAIR_H (cur)) {
	    /* :~ -> dynamic-let */

	    /*
	     * See commentary in := above
	     */
	    IDIO body = idio_meaning_rewrite_body (IDIO_MPP (cur, src), IDIO_PAIR_T (l), nametree);
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }

	    if (idio_S_nil == body) {
		fprintf (stderr, "imrb :~	OPT: empty body for dynamic-let => no eval of {value-expr}\n");
		return r;
	    }

	    IDIO body_sequence = idio_list_append2 (IDIO_LIST1 (idio_S_begin), body);

	    IDIO binding = IDIO_PAIR_T (cur);

	    IDIO r_cur = IDIO_LIST3 (idio_S_dynamic_let,
				     binding,
				     body_sequence);
	    idio_meaning_copy_src_properties (IDIO_MPP (cur, src), r_cur);

	    r = idio_list_append2 (r, IDIO_LIST1 (r_cur));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_excl_tilde == IDIO_PAIR_H (cur)) {
	    /* !~ -> dynamic-unset */

	    /*
	     * See commentary in := above
	     */
	    IDIO body = idio_meaning_rewrite_body (IDIO_MPP (cur, src), IDIO_PAIR_T (l), nametree);
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }

	    if (idio_S_nil == body) {
		fprintf (stderr, "imrb !~	OPT: empty body for dynamic-unset => no eval of {value-expr}\n");
		return r;
	    }

	    IDIO body_sequence = idio_list_append2 (IDIO_LIST1 (idio_S_begin), body);


	    IDIO r_cur = IDIO_LIST3 (idio_S_dynamic_unset,
				     IDIO_PAIR_HT (cur),
				     body_sequence);
	    idio_meaning_copy_src_properties (IDIO_MPP (cur, src), r_cur);

	    r = idio_list_append2 (r, IDIO_LIST1 (r_cur));
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_define_template == IDIO_PAIR_H (cur)) {
	    /* internal define-template */

	    /*
	     * Test Case: evaluation-errors/internal-define-template.idio
	     *
	     * {
	     *   define-template (bar) {
	     *     #T{ 1 }
	     *   }
	     * }
	     */
	    idio_meaning_evaluation_error (cur, IDIO_C_FUNC_LOCATION (), "internal define-template", cur);

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

/*
 * In idio_meaning_rewrite_body_letrec() we're looking to accumulate a
 * sequence of :+ (ie. letrec) statements into {defs} and when we find
 * the first "anything else" statement we'll unbundle {defs} into a
 * {letrec} with "anything else" and subsequent expressions as the
 * body of the {letrec}.
 *
 * Each element of {defs] is the (name value-expr) tuple we would
 * expect.
 */
static IDIO idio_meaning_rewrite_body_letrec (IDIO src, IDIO e, IDIO nametree)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);

    IDIO l = e;
    IDIO defs = idio_S_nil;

    for (;;) {
	IDIO cur = idio_S_unspec;
	if (idio_S_nil == l) {
	    /*
	     * Test Case: evaluation-errors/letrec-empty-body.idio
	     *
	     * {
	     *   bar :+ "foo"
	     *
	     * }
	     *
	     *
	     * NB The point is that there is nothing else in the block
	     * after the creation of {bar}, so there is no "body" for
	     * the (generated) letrec of {bar}.
	     *
	     * Regardless of whether {bar} is a (faintly) pointless
	     * sort of letrec, notably not involving a function.
	     *
	     * There's an argument that it could be optimised away (if
	     * the value-expression has no side-effects, etc.).
	     */
	    if (idio_isa_pair (src)) {
		idio_meaning_evaluation_error (IDIO_PAIR_H (src), IDIO_C_FUNC_LOCATION (), "letrec: empty body", l);

		return idio_S_notreached;
	    } else {
		idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "letrec: empty body", l);

		return idio_S_notreached;
	    }

	    return idio_S_notreached;
	} else if (idio_isa_pair (l) &&
		   idio_isa_pair (IDIO_PAIR_H (l)) &&
		   idio_S_false != idio_expanderp (IDIO_PAIR_HH (l))) {
	    cur = idio_template_expands (IDIO_PAIR_H (l));
	} else {
	    cur = IDIO_PAIR_H (l);
	}
	idio_meaning_copy_src_properties (IDIO_MPP (IDIO_PAIR_H (l), src), cur);

	if (idio_isa_pair (cur) &&
	    idio_S_begin == IDIO_PAIR_H (cur)) {
	    /*  redundant begin */
	    l = idio_list_append2 (IDIO_PAIR_T (cur), IDIO_PAIR_T (l));
	    continue;
	} else if (idio_isa_pair (cur) &&
		   (idio_S_define == IDIO_PAIR_H (cur) ||
		    idio_S_colon_plus == IDIO_PAIR_H (cur))) {

	    /*
	     * cur	~ (define (name arg) ...)
	     * cur	~ (:+ name value-expr)
	     */
	    IDIO bindings = IDIO_PAIR_HT (cur);
	    IDIO form = idio_S_unspec;

	    if (idio_isa_pair (bindings)) {
		/*
		 * (define (name args) ...)
		 *
		 * (name (function (args) ...))
		 */
		IDIO dsh  = idio_open_output_string_handle_C ();
		idio_display_C ("rewrite body letrec: define ", dsh);
		idio_display (bindings, dsh);
		IDIO fn = idio_list_append2 (IDIO_LIST3 (idio_S_function,
							 IDIO_PAIR_T (bindings),
							 idio_get_output_string (dsh)),
					     IDIO_PAIR_TT (cur));
		form = IDIO_LIST2 (IDIO_PAIR_H (bindings), fn);
	    } else {
		/*
		 * (:+ name value-expr)
		 *
		 * (name value-expr)
		 */
		form = IDIO_PAIR_T (cur);
	    }
	    idio_meaning_copy_src_properties (IDIO_MPP (cur, src), form);
	    defs = idio_pair (form, defs);
	    l = IDIO_PAIR_T (l);
	    continue;
	} else if (idio_isa_pair (cur) &&
		   idio_S_define_template == IDIO_PAIR_H (cur)) {
	    /* internal define-template */
	    /*
	     * Test Case: (nominally) evaluation-errors/letrec-internal-define-template.idio
	     *
	     * bar :+ define-template (baz) { #T{ 1 } }
	     *
	     * XXX I can't get this to trigger the error
	     */
	    idio_meaning_evaluation_error (cur, IDIO_C_FUNC_LOCATION (), "letrec: internal define-template", cur);

	    return idio_S_notreached;
	} else {
	    /* body proper */
	    IDIO cur_props = IDIO_MPP (cur, src);
	    l = idio_meaning_rewrite_body (cur_props, l, nametree);

	    if (idio_S_nil == defs) {
		return l;
	    } else {
		/*
		 * poor man's letrec*
		 *
		 * We are aiming for:
		 *
		 * {
		 *   v1 :+ a1
		 *   v2 :+ a2
		 *   body
		 * }
		 *
		 * to become
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
		    /*
		     * Remember {vs} (ie. {defs}) is the list of
		     * tuples -- reversed because of the order we
		     * discovered them in
		     *
		     * ((v2 a2) (v1 a1))
		     *
		     * so that (ph vs) is (v1 a1) and therefore
		     *
		     * (append (set!) (v1 a1))
		     *
		     * gives us the desired
		     *
		     * (set! v1 a1)
		     *
		     * and that as we walk down {vs} we'll get a
		     * (reversed) list of assignments in {body} -- now
		     * back in the original order
		     *
		     * ((set! v1 a1)
		     *  (set! v2 a2))
		     */
		    IDIO assign = idio_list_append2 (IDIO_LIST1 (idio_S_set), IDIO_PAIR_H (vs));
		    body = idio_list_append2 (IDIO_LIST1 (assign), body);
		    vs = IDIO_PAIR_T (vs);
		}
		body = idio_list_append2 (body, l);

		IDIO let = idio_list_append2 (IDIO_LIST2 (idio_S_let, bindings), body);
		idio_meaning_copy_src_properties (cur_props, let);

		return let;
	    }
	}
    }
}

static IDIO idio_meaning_abstraction (IDIO src, IDIO nns, IDIO docstr, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nns);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    ep = idio_meaning_rewrite_body (IDIO_MPP (ep, src), ep, nametree);
    idio_meaning_copy_src_properties (src, ep);

    IDIO ns = nns;
    IDIO regular = idio_S_nil;

    for (;;) {
	if (idio_isa_pair (ns))  {
	    regular = idio_pair (IDIO_PAIR_H (ns), regular);
	    ns = IDIO_PAIR_T (ns);
	} else if (idio_S_nil == ns) {
	    return idio_meaning_fix_abstraction (src, nns, nns, docstr, ep, nametree, escapes, flags, cs, cm);
	} else {
	    return idio_meaning_dotted_abstraction (src, idio_list_reverse (regular), ns, nns, docstr, ep, nametree, escapes, flags, cs, cm);
	}
    }

    /*
     * Shouldn't get here...
     */
    idio_error_C ("meaning-abstraction", IDIO_LIST2 (nns, ep), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

static IDIO idio_meaning_block (IDIO src, IDIO es, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    es = idio_meaning_rewrite_body (IDIO_MPP (es, src), es, nametree);
    idio_meaning_copy_src_properties (src, es);

    return idio_meaning_sequence (IDIO_MPP (es, src), es, nametree, escapes, flags, idio_S_begin, cs, cm);
}

static IDIO idio_meaning_arguments (IDIO src, IDIO aes, IDIO nametree, IDIO escapes, size_t arity, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_some_arguments (IDIO src, IDIO ae, IDIO aes, IDIO nametree, IDIO escapes, size_t arity, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ae);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO am = idio_meaning (IDIO_MPP (ae, src), ae, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO ams = idio_meaning_arguments (src, aes, nametree, escapes, arity, flags, cs, cm);
    size_t rank = arity - (idio_list_length (aes) + 1);

    return IDIO_LIST4 (IDIO_I_STORE_ARGUMENT, am, ams, idio_fixnum (rank));
}

static IDIO idio_meaning_no_argument (IDIO src, IDIO nametree, IDIO escapes, size_t arity, int flags, IDIO cs)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);

    return IDIO_LIST2 (IDIO_I_ALLOCATE_FRAME, idio_fixnum (arity));
}

static IDIO idio_meaning_arguments (IDIO src, IDIO aes, IDIO nametree, IDIO escapes, size_t arity, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (aes)) {
	return idio_meaning_some_arguments (src, IDIO_PAIR_H (aes), IDIO_PAIR_T (aes), nametree, escapes, arity, flags, cs, cm);
    } else {
	return idio_meaning_no_argument (src, nametree, escapes, arity, flags, cs);
    }
}

static IDIO idio_meaning_fix_closed_application (IDIO src, IDIO ns, IDIO body, IDIO aes, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (body);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    body = idio_meaning_rewrite_body (IDIO_MPP (body, src), body, nametree);
    idio_meaning_copy_src_properties (src, body);

    IDIO ams = idio_meaning_arguments (src, aes, nametree, escapes, idio_list_length (aes), IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO ent = idio_meaning_nametree_extend_params (nametree, ns);
    IDIO mbody = idio_meaning_sequence (IDIO_MPP (body, src), body, ent, escapes, flags, idio_S_begin, cs, cm);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST4 (IDIO_I_TR_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    } else {
	return IDIO_LIST4 (IDIO_I_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    }
}

static IDIO idio_meaning_dotted_arguments (IDIO src, IDIO aes, IDIO nametree, IDIO escapes, size_t size, size_t arity, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_some_dotted_arguments (IDIO src, IDIO ae, IDIO aes, IDIO nametree, IDIO escapes, size_t nargs, size_t arity, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ae);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO am = idio_meaning (IDIO_MPP (ae, src), ae, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    IDIO ams = idio_meaning_dotted_arguments (src, aes, nametree, escapes, nargs, arity, flags, cs, cm);
    size_t rank = nargs - (idio_list_length (aes) + 1);

    if (rank < arity) {
	return IDIO_LIST4 (IDIO_I_STORE_ARGUMENT, am, ams, idio_fixnum (rank));
    } else {
	return IDIO_LIST4 (IDIO_I_LIST_ARGUMENT, am, ams, idio_fixnum (arity));
    }
}

static IDIO idio_meaning_no_dotted_argument (IDIO src, IDIO nametree, IDIO escapes, size_t nargs, size_t arity, int flags, IDIO cs)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);

    return IDIO_LIST2 (IDIO_I_ALLOCATE_FRAME, idio_fixnum (arity));
}

static IDIO idio_meaning_dotted_arguments (IDIO src, IDIO aes, IDIO nametree, IDIO escapes, size_t nargs, size_t arity, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (aes)) {
	return idio_meaning_some_dotted_arguments (src, IDIO_PAIR_H (aes), IDIO_PAIR_T (aes), nametree, escapes, nargs, arity, flags, cs, cm);
    } else {
	return idio_meaning_no_dotted_argument (src, nametree, escapes, nargs, arity, flags, cs);
    }
}

static IDIO idio_meaning_dotted_closed_application (IDIO src, IDIO ns, IDIO n, IDIO body, IDIO aes, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (body);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    body = idio_meaning_rewrite_body (IDIO_MPP (body, src), body, nametree);
    idio_meaning_copy_src_properties (src, body);

    IDIO ams = idio_meaning_dotted_arguments (src, aes, nametree, escapes, idio_list_length (aes), idio_list_length (ns), IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO fix_formals = idio_list_append2 (ns, IDIO_LIST1 (n));

    IDIO ent = idio_meaning_nametree_extend_vparams (nametree, fix_formals);
    IDIO mbody = idio_meaning_sequence (IDIO_MPP (body, src), body, ent, escapes, flags, idio_S_begin, cs, cm);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST4 (IDIO_I_TR_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    } else {
	return IDIO_LIST4 (IDIO_I_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    }
}

static IDIO idio_meaning_closed_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /*
     * ((function ...) args)
     *
     * fe	~ (function ...)
     * (ph fe)	~ 'function
     * (pt fe)	~ ...		~ (formals* body)
     * (pht fe)	~ formals*
     * (ptt fe)	~ (body)
     */

    IDIO fet = IDIO_PAIR_T (fe);

    IDIO fns = IDIO_PAIR_H (fet); /* formals* */
    IDIO ns = fns;		  /* tmp to loop over formals* */
    IDIO es = aes;		  /* tmp to loop over aes */
    IDIO fixed_args = idio_S_nil;	  /* positional formals* */

    /*
     * Walk down the formals and the arguments checking that they
     * match up.
     *
     * Of course, you can have more arguments if the closed function
     * is varargs.
     */
    for (;;) {
	if (idio_isa_pair (ns)) {
	    if (idio_isa_pair (es)) {
		fixed_args = idio_pair (IDIO_PAIR_H (ns), fixed_args);
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
		idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "closed function: not enough arguments", IDIO_LIST2 (fns, aes));

		return idio_S_notreached;
	    }
	} else if (idio_S_nil == ns) {
	    if (idio_S_nil == es) {
		return idio_meaning_fix_closed_application (fe, fns, IDIO_PAIR_T (fet), aes, nametree, escapes, flags, cs, cm);
	    } else {
		/*
		 * Test Case: evaluation-errors/closed-function-too-many-args-{0,1}.idio
		 *
		 * (function () 1) 2
		 * (function (x) x) 1 2
		 */
		idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "closed function: too many arguments", IDIO_LIST2 (fns, aes));

		return idio_S_notreached;
	    }
	} else {
	    return idio_meaning_dotted_closed_application (fe, idio_list_reverse (fixed_args), ns, IDIO_PAIR_T (fet), aes, nametree, escapes, flags, cs, cm);
	}
    }

    /*
     * Can we get here?
     */

    idio_error_C ("meaning-closed-application", IDIO_LIST2 (fe, aes), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

static IDIO idio_meaning_local_application (IDIO src, IDIO n, IDIO ae, IDIO body, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (n);
    IDIO_ASSERT (ae);
    IDIO_ASSERT (body);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    body = idio_meaning_rewrite_body (IDIO_MPP (body, src), body, nametree);
    idio_meaning_copy_src_properties (src, body);

    IDIO am = idio_meaning (IDIO_MPP (ae, src), ae, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO ent = idio_meaning_nametree_extend_locals (nametree, IDIO_LIST1 (n));
    IDIO mbody = idio_meaning_sequence (IDIO_MPP (body, src), body, ent, escapes, flags, idio_S_begin, cs, cm);

    IDIO ll = idio_meaning_lexical_lookup (src, ent, n);

    return IDIO_LIST5 (IDIO_I_LOCAL, IDIO_PAIR_HTT (ll), am, mbody, idio_meaning_nametree_to_list (ent));
}

static IDIO idio_meaning_regular_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm);

static IDIO idio_meaning_primitive_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, IDIO escapes, int flags, size_t arity, IDIO gvi, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (gvi);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, fe);
    IDIO_TYPE_ASSERT (list, aes);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (fixnum, gvi);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /*
     * We used to follow LiSP and specialise PRIMCALLn with
     * PRIMCALLn_X but in practice we should just call the primitives
     * and save a run around with frames etc.
     *
     * One thing we need to be leery of is ensuring that we set the
     * *func* register -- which would have been done by the full
     * function call protocol.
     *
     * The problem here is incredibly subtle.  We had relied on the
     * *func* register being reset with every (true) function call
     * which meant that after a continuation had been called then the
     * continuation's value would have been flushed away by the next
     * function call.
     *
     * If we don't do that with primitive calls then we can leave a
     * continuation lying around in *func* which means it won't get
     * GC'd and in turn the values in that continuation's embedded
     * stack won't get GC'd.
     *
     * It just so happens that our "use all the file descriptors" test
     * will have had the array of file descriptors on the stack of the
     * wrappering continuation (via the saved *frame*?).  If we don't
     * flush the continuation from *func* then the array of fds won't
     * get flushed and we won't get to use any more file descriptors.
     * Not ideal.
     *
     * Need to figure out run-in-thread too...
     */
    IDIO primdata = idio_vm_values_ref (IDIO_FIXNUM_VAL (gvi));

    if (IDIO_PRIMITIVE_VARARGS (primdata)) {
	/*
	 * only a full function call protocol can cope with varargs!
	 */
	return idio_meaning_regular_application (src, fe, aes, nametree, escapes, flags, cs, cm);
    } else {
	char *name = IDIO_PRIMITIVE_NAME (primdata);

	switch (arity) {
	case 0:
	    {
		return IDIO_LIST3 (IDIO_I_PRIMCALL0, idio_fixnum (IDIO_A_PRIMCALL0), gvi);
	    }
	    break;
	case 1:
	    {
		IDIO m1 = idio_meaning (IDIO_MPP (IDIO_PAIR_H (aes), src), IDIO_PAIR_H (aes), nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

		return IDIO_LIST4 (IDIO_I_PRIMCALL1, idio_fixnum (IDIO_A_PRIMCALL1), m1, gvi);
	    }
	    break;
	case 2:
	    {
		IDIO m1 = idio_meaning (IDIO_MPP (IDIO_PAIR_H (aes), src), IDIO_PAIR_H (aes), nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
		IDIO m2 = idio_meaning (IDIO_MPP (IDIO_PAIR_HT (aes), src), IDIO_PAIR_HT (aes), nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

		if (IDIO_STREQP (name, "run-in-thread")) {
		    break;
		} else {
		    return IDIO_LIST5 (IDIO_I_PRIMCALL2, idio_fixnum (IDIO_A_PRIMCALL2), m1, m2, gvi);
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
    return idio_meaning_regular_application (src, fe, aes, nametree, escapes, flags, cs, cm);
}

static IDIO idio_meaning_regular_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO fm;
    if (idio_isa_symbol (fe)) {
	fm = idio_meaning_function_reference (src, fe, nametree, escapes, flags, cs, cm);
    } else {
	fm = idio_meaning (fe, fe, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);
    }

    int ams_flags = IDIO_MEANING_NOT_TAILP (flags);

    IDIO ams = idio_meaning_arguments (src, aes, nametree, escapes, idio_list_length (aes), ams_flags, cs, cm);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST4 (IDIO_I_TR_REGULAR_CALL, src, fm, ams);
    } else {
	return IDIO_LIST4 (IDIO_I_REGULAR_CALL, src, fm, ams);
    }
}

static IDIO idio_meaning_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_symbol (fe)) {
	IDIO si = idio_meaning_variable_info (src, nametree, fe, IDIO_MEANING_TOPLEVEL_SCOPE (flags), cs, cm);

	if (idio_isa_pair (si)) {
	    IDIO scope = IDIO_PAIR_H (si);

	    if (idio_S_predef == scope) {
		IDIO fvi = IDIO_PAIR_HTT (si);
		IDIO p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));

		if (idio_S_unspec != p) {
		    if (idio_isa_primitive (p)) {
			size_t arity = IDIO_PRIMITIVE_ARITY (p);
			size_t nargs = idio_list_length (aes);

			if ((IDIO_PRIMITIVE_VARARGS (p) &&
			     nargs >= arity) ||
			    arity == nargs) {
			    return idio_meaning_primitive_application (src, fe, aes, nametree, escapes, flags, arity, fvi, cs, cm);
			} else {
			    /*
			     * Test Case: evaluation-errors/primitive-arity.idio
			     *
			     * pair 1
			     */
			    idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "wrong arity for primitive", IDIO_LIST2 (fe, aes));

			    return idio_S_notreached;
			}
		    } else {
			/*
			 * Can we get here?  We'd need to be a predef
			 * but those are all, uh, primitives.
			 *
			 * Redefining a primitive doesn't get us here.
			 * Hmm.
			 */
			idio_debug ("BAD application: ! function\npd %s\n", p);
			idio_debug ("si %s\n", si);
			idio_debug ("e %s\n", fe);
			idio_debug ("ivvr %s\n", idio_vm_values_ref (IDIO_FIXNUM_VAL (IDIO_PAIR_HTT (si))));
		    }
		}
	    }
	}
    }

    if (idio_isa_pair (fe) &&
	idio_eqp (idio_S_function, IDIO_PAIR_H (fe))) {
	if (0 == idio_isa_string (IDIO_PAIR_HTT (fe))) {
	    IDIO dsh  = idio_open_output_string_handle_C ();
	    idio_display_C ("closed application: ", dsh);
	    idio_display (IDIO_PAIR_HT (fe), dsh);
	    fe = idio_list_append2 (IDIO_LIST3 (IDIO_PAIR_H (fe),
						IDIO_PAIR_HT (fe),
						idio_get_output_string (dsh)),
				    IDIO_PAIR_TT (fe));
	    idio_meaning_copy_src_properties (src, fe);
	}

	/*
	 * NB As this is a closed application (ie. not a regular
	 * function call) then the nominal form for {src} is the
	 * application of an anonymous function to some {args}
	 *
	 * ((function (params) ...) args)
	 *
	 * However, if our anonymous function had had a name and had
	 * been called in the regular way then {src} would have been
	 *
	 * (anon args)
	 *
	 * where the symbol 'anon has to substitute for the anonymous
	 * closure.  Unfortunately, that doesn't have any source
	 * properties (as we've just made it up) but {src} does have
	 * source properties.
	 *
	 * So we have the opportunity to use a potentially more
	 * palatable source form.
	 */

	return idio_meaning_closed_application (src, fe, aes, nametree, escapes, flags, cs, cm);
    } else {
	return idio_meaning_regular_application (src, fe, aes, nametree, escapes, flags, cs, cm);
    }

    /*
     * Can we get here?
     */

    idio_error_C ("meaning-application", IDIO_LIST2 (fe, aes), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

static IDIO idio_meaning_dynamic_reference (IDIO src, IDIO name, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_reference (src, name, nametree, escapes, IDIO_MEANING_DYNAMIC_SCOPE (flags), cs, cm);
}

static IDIO idio_meaning_dynamic_let (IDIO src, IDIO name, IDIO e, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO fmci;
    IDIO si = idio_module_find_symbol (name, cm);

    if (idio_S_false != si) {
	fmci = IDIO_PAIR_HT (si);
    } else {
	/*
	 * Get a new toplevel for this dynamic variable
	 */
	fmci = idio_toplevel_extend (src, name, IDIO_MEANING_DYNAMIC_SCOPE (flags), cs, cm);
    }

    /*
     * Tell the tree of "locals" about this dynamic variable and find
     * the meaning of ep with the new tree
     */
    IDIO ent = idio_meaning_nametree_dynamic_extend (nametree, name, fmci, idio_S_dynamic);

    IDIO mp = idio_meaning_sequence (src, ep, ent, escapes, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, cs, cm);

    IDIO dynamic_wrap = IDIO_LIST3 (IDIO_LIST3 (IDIO_I_PUSH_DYNAMIC, fmci, m),
				    mp,
				    IDIO_LIST1 (IDIO_I_POP_DYNAMIC));

    return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_GLOBAL_SYM_DEF, name, idio_S_dynamic, fmci),
		       dynamic_wrap);
}

static IDIO idio_meaning_dynamic_unset (IDIO src, IDIO name, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_dynamic_let (src, name, idio_S_undef, ep, nametree, escapes, flags, cs, cm);
}

static IDIO idio_meaning_environ_reference (IDIO src, IDIO name, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_reference (src, name, nametree, escapes, IDIO_MEANING_ENVIRON_SCOPE (flags), cs, cm);
}

static IDIO idio_meaning_environ_let (IDIO src, IDIO name, IDIO e, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO fmci;
    IDIO si = idio_module_find_symbol (name, cm);

    if (idio_S_false != si) {
	fmci = IDIO_PAIR_HT (si);
    } else {
	/*
	 * Get a new toplevel for this environ variable
	 */
	fmci = idio_toplevel_extend (src, name, IDIO_MEANING_ENVIRON_SCOPE (flags), cs, cm);
    }

    /*
     * Tell the tree of "locals" about this environ variable and find
     * the meaning of ep with the new tree
     */
    IDIO ent = idio_meaning_nametree_dynamic_extend (nametree, name, fmci, idio_S_environ);

    IDIO mp = idio_meaning_sequence (src, ep, ent, escapes, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, cs, cm);

    IDIO environ_wrap = IDIO_LIST3 (IDIO_LIST3 (IDIO_I_PUSH_ENVIRON, fmci, m),
				    mp,
				    IDIO_LIST1 (IDIO_I_POP_ENVIRON));

    return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_GLOBAL_SYM_DEF, name, idio_S_environ, fmci),
		       environ_wrap);
}

static IDIO idio_meaning_environ_unset (IDIO src, IDIO name, IDIO ep, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_environ_let (src, name, idio_S_undef, ep, nametree, escapes, flags, cs, cm);
}

static IDIO idio_meaning_computed_reference (IDIO src, IDIO name, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    return idio_meaning_reference (src, name, nametree, escapes, IDIO_MEANING_COMPUTED_SCOPE (flags), cs, cm);
}

/*
 * (trap condition       handler body ...)
 * (trap (condition ...) handler body ...)
 *
 * This is a bit complicated as condition can be a list.  For each
 * condition in the list we want to use *the same* handler code.  So
 * our intermediate code wants to leave the closure for the handler in
 * *val* then have a sequence of "PUSH-TRAP n" statements all re-using
 * the handler in *val* then the body code then a matching sequence of
 * POP-TRAP statements.
 */
static IDIO idio_meaning_trap (IDIO src, IDIO ce, IDIO he, IDIO be, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ce);
    IDIO_ASSERT (he);
    IDIO_ASSERT (be);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    /*
     * We need the meaning of handler now as it'll be used by all the
     * traps.
     */
    he = idio_meaning_rewrite_body (IDIO_MPP (he, src), he, nametree);
    idio_meaning_copy_src_properties (src, he);

    IDIO hm = idio_meaning (IDIO_MPP (he, src), he, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

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
	IDIO si = idio_module_find_symbol_recurse (cname, cm, 1);

	if (idio_isa_pair (si)) {
	    fmci = IDIO_PAIR_HT (si);
	} else {
	    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION (), "trap: unknown condition name", cname);

	    return idio_S_notreached;
	}

	pushs = idio_pair (IDIO_LIST2 (IDIO_I_PUSH_TRAP, fmci), pushs);
	pops = idio_pair (IDIO_LIST1 (IDIO_I_POP_TRAP), pops);

	ce = IDIO_PAIR_T (ce);
    }

    be = idio_meaning_rewrite_body (IDIO_MPP (be, src), be, nametree);
    idio_meaning_copy_src_properties (src, be);

    IDIO bm = idio_meaning_sequence (IDIO_MPP (be, src), be, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, cs, cm);

    IDIO r = idio_list_append2 (IDIO_LIST1 (hm), pushs);
    r = idio_list_append2 (r, IDIO_LIST1 (bm));
    r = idio_list_append2 (r, pops);

    return r;
}

static IDIO idio_meaning_escape_block (IDIO src, IDIO label, IDIO be, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (label);
    IDIO_ASSERT (be);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, label);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, label);
    IDIO fmci = idio_fixnum (mci);

    be = idio_meaning_rewrite_body (IDIO_MPP (be, src), be, nametree);
    idio_meaning_copy_src_properties (src, be);

    IDIO ee = idio_pair (label, escapes);

    IDIO bm = idio_meaning_sequence (IDIO_MPP (be, src), be, nametree, ee, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, cs, cm);

    IDIO r = IDIO_LIST2 (IDIO_LIST3 (IDIO_I_PUSH_ESCAPER, fmci, bm),
			 IDIO_LIST1 (IDIO_I_POP_ESCAPER));

    return r;
}

static IDIO idio_meaning_escape_from (IDIO src, IDIO label, IDIO ve, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (label);
    IDIO_ASSERT (ve);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, label);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO esc = idio_list_memq (label, escapes);

    if (idio_S_false == esc) {
	/*
	 * Test Case: evaluation-errors/escape-from-unbound.idio
	 *
	 * escape-from new-york #t
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), label);

	return idio_S_notreached;
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, label);
    IDIO fmci = idio_fixnum (mci);

    IDIO vm = idio_meaning (IDIO_MPP (ve, src), ve, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO r = IDIO_LIST3 (IDIO_I_ESCAPER_LABEL_REF, fmci, vm);

    return r;
}

static IDIO idio_meaning_escape_label (IDIO src, IDIO label, IDIO ve, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (label);
    IDIO_ASSERT (ve);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (symbol, label);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO esc = idio_list_memq (label, escapes);

    if (idio_S_false == esc) {
	/*
	 * Test Case: evaluation-errors/return-unbound.idio
	 *
	 * return #t
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), label);

	return idio_S_notreached;
    }

    idio_ai_t mci = idio_codegen_constants_lookup_or_extend (cs, label);
    IDIO fmci = idio_fixnum (mci);

    if (idio_isa_pair (ve)) {
	ve = IDIO_PAIR_H (ve);
    } else {
	ve = idio_S_void;
    }

    IDIO vm = idio_meaning (IDIO_MPP (ve, src), ve, nametree, escapes, IDIO_MEANING_NOT_TAILP (flags), cs, cm);

    IDIO r = IDIO_LIST3 (IDIO_I_ESCAPER_LABEL_REF, fmci, vm);

    return r;
}

static IDIO idio_meaning_include (IDIO src, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t pc0 = IDIO_THREAD_PC (thr);

    idio_load_file_name (e, cs);

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }

    return IDIO_LIST1 (IDIO_I_NOP);
}

static IDIO idio_meaning_expander (IDIO src, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    IDIO te = idio_template_expand (e);
    idio_meaning_copy_src_properties (src, te);

    return idio_meaning (te, te, nametree, escapes, flags, cs, cm);
}

static IDIO idio_meaning (IDIO src, IDIO e, IDIO nametree, IDIO escapes, int flags, IDIO cs, IDIO cm)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (escapes);
    IDIO_ASSERT (cs);
    IDIO_ASSERT (cm);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (list, escapes);
    IDIO_TYPE_ASSERT (array, cs);
    IDIO_TYPE_ASSERT (module, cm);

    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	IDIO et = IDIO_PAIR_T (e);

	if (idio_S_begin == eh ||
	    idio_S_and == eh ||
	    idio_S_or == eh) {
	    if (idio_isa_pair (et)) {
		return idio_meaning_sequence (src, et, nametree, escapes, flags, eh, cs, cm);
	    } else if (idio_S_begin == eh) {
		return idio_meaning (src, idio_S_void, nametree, escapes, flags, cs, cm);
	    } else if (idio_S_and == eh) {
		return idio_meaning (src, idio_S_true, nametree, escapes, flags, cs, cm);
	    } else if (idio_S_or == eh) {
		return idio_meaning (src, idio_S_false, nametree, escapes, flags, cs, cm);
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
		if (idio_S_nil != IDIO_PAIR_T (et)) {
		    /*
		     * Test Case: evaluation-errors/escape-multiple-args.idio
		     *
		     * (escape 1 2)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("escape"), "too many arguments", eh);

		    return idio_S_notreached;
		} else {
		    return idio_meaning_escape (src, IDIO_PAIR_H (et), nametree, escapes, flags, cs, cm);
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/escape-nil.idio
		 *
		 * (escape)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("escape"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_quote == eh) {
	    /* (quote x) */
	    if (idio_isa_pair (et)) {
		if (idio_S_nil != IDIO_PAIR_T (et)) {
		    /*
		     * Test Case: evaluation-errors/quote-multiple-args.idio
		     *
		     * (quote 1 2)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quote"), "too many arguments", eh);

		    return idio_S_notreached;
		} else {
		    return idio_meaning_quotation (src, IDIO_PAIR_H (et), nametree, escapes, flags);
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/quote-nil.idio
		 *
		 * (quote)
		 *
		 * XXX sight annoyance as ``(quote)`` is nominally
		 * ``'#n``
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quote"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_quasiquote == eh) {
	    /* (quasiquote x) */
	    if (idio_isa_pair (et)) {
		if (idio_S_nil != IDIO_PAIR_T (et)) {
		    /*
		     * Test Case: evaluation-errors/quasiquote-multiple-args.idio
		     *
		     * (quasiquote 1 2)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quasiquote"), "too many arguments", eh);

		    return idio_S_notreached;
		} else {
		    return idio_meaning_quasiquotation (src, IDIO_PAIR_H (et), nametree, escapes, flags, cs, cm);
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/quasiquote-nil.idio
		 *
		 * (quasiquote)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quasiquote"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_function == eh) {
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
			return idio_meaning_abstraction (src, IDIO_PAIR_H (et), etth, ettt, nametree, escapes, flags, cs, cm);
		    } else {
			/*
			 * (function bindings body ...)
			 * (function bindings "...")
			 *
			 * The second is a function whose body is a
			 * string.
			 */
			return idio_meaning_abstraction (src, IDIO_PAIR_H (et), idio_S_nil, ett, nametree, escapes, flags, cs, cm);
		    }
		} else {
		    /* (function bindings body ...) */
		    return idio_meaning_abstraction (src, IDIO_PAIR_H (et), idio_S_nil, ett, nametree, escapes, flags, cs, cm);
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/function-nil.idio
		 *
		 * (function)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_functionp == eh) {
	    /* (function+ var val body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			/*
			 * (function+ var val body ...)
			 */
			return idio_meaning_local_application (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettt, nametree, escapes, flags, cs, cm);
		    } else {
			/*
			 * Test Case: evaluation-errors/function+-no-body.idio
			 *
			 * (function+ var val)
			 */
			idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function+"), "no body", eh);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/function+-no-value.idio
		     *
		     * (function+ var)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function+"), "no value", eh);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/function+-nil.idio
		 *
		 * (function+)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("functionp"), "no arguments", eh);

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
		    return idio_meaning_alternative (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettth, nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/if-cond-nil.idio
		     *
		     * (if 1)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("if cond"), "no consequent/alternative", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/if-nil.idio
		 *
		 * (if)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("if"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_cond == eh) {
	    /* (cond clause ...) */
	    if (idio_isa_pair (et)) {
		/*
		 * What was this clause covering?
		 */
		if (idio_S_nil == IDIO_PAIR_T (et)) {
		    IDIO eth = IDIO_PAIR_H (et);
		    if (idio_isa_pair (eth) &&
			idio_S_block == IDIO_PAIR_H (eth)) {
			idio_debug ("cond %s\n", e);
			et = IDIO_PAIR_T (eth);
		    }
		}

		IDIO etc = idio_meaning_rewrite_cond (e, IDIO_MPP (et, src), et);
		idio_meaning_copy_src_properties (src, etc);

		IDIO c = idio_meaning (etc, etc, nametree, escapes, flags, cs, cm);

		return c;
	    } else {
		/*
		 * Test Case: evaluation-errors/cond-nil.idio
		 *
		 * (cond)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("cond"), "no clauses", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_set == eh ||
		   idio_S_eq == eh) {
	    /* (set! var expr) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_assignment (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, escapes, IDIO_MEANING_TOPLEVEL_SCOPE (flags), cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/set-symbol-nil.idio
		     *
		     * (set! x )
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("set!"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/set-nil.idio
		 *
		 * (set!)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("set!"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_define_template == eh) {
	    /* (define-template bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_template (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/define-template-bindings-nil.idio
		     *
		     * define-template (m)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-template"), "no body", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-template-nil.idio
		 *
		 * (define-template)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-template"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_define_infix_operator == eh) {
	    /* (define-infix-operator sym pri body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_define_infix_operator (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), IDIO_PAIR_H (ettt), nametree, escapes, flags, cs, cm);
		    } else {
			/*
			 * Test Case: evaluation-errors/define-infix-op-symbol-pri-nil.idio
			 *
			 * define-infix-operator sym pri
			 */
			idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-infix-operator"), "no body", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/define-infix-op-symbol-nil.idio
		     *
		     * define-infix-operator sym
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-infix-operator"), "no pri body", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-infix-op-nil.idio
		 *
		 * (define-infix-operator)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-infix-operator"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_define_postfix_operator == eh) {
	    /* (define-postfix-operator bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_define_postfix_operator (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), IDIO_PAIR_H (ettt), nametree, escapes, flags, cs, cm);
		    } else {
			/*
			 * Test Case: evaluation-errors/define-infix-op-symbol-pri-nil.idio
			 *
			 * define-postfix-operator sym pri
			 */
			idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-postfix-operator"), "no body", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/define-infix-op-symbol-nil.idio
		     *
		     * define-postfix-operator sym
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-postfix-operator"), "no pri body", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-infix-op-nil.idio
		 *
		 * (define-postfix-operator)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-postfix-operator"), "no arguments", eh);

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
		    return idio_meaning_define (src, IDIO_PAIR_H (et), ett, nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/define-sym-nil.idio
		     *
		     * define sym
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/define-nil.idio
		 *
		 * (define)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define"), "no arguments", eh);

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
		    return idio_meaning_define (src, IDIO_PAIR_H (et), ett, nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/toplevel-define-sym-nil.idio
		     *
		     * := sym
		     *
		     * XXX can't do ``sym :=`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":="), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/toplevel-define-nil.idio
		 *
		 * (:=)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":="), "no arguments", eh);

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
		    return idio_meaning_define_environ (src, IDIO_PAIR_H (et), ett, nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/environ-define-sym-nil.idio
		     *
		     * :* sym
		     *
		     * XXX can't do ``sym :*`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":*"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/environ-define-nil.idio
		 *
		 * (:*)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":*"), "no arguments", eh);

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
		    return idio_meaning_define_dynamic (src, IDIO_PAIR_H (et), ett, nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/dynamic-define-sym-nil.idio
		     *
		     * :~ sym
		     *
		     * XXX can't do ``sym :~`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":~"), "no value", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/dynamic-define-nil.idio
		 *
		 * (:~)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":~"), "no arguments", eh);

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
		    return idio_meaning_define_computed (src, IDIO_PAIR_H (et), ett, nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/computed-define-sym-nil.idio
		     *
		     * :$ sym
		     *
		     * XXX can't do ``sym :$`` as you'll get the EOF
		     * in list error from the reader.
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":$"), "no getter [setter]", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/computed-define-nil.idio
		 *
		 * (:$)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S (":$"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_block == eh) {
	    /*
	     * { ... }
	     */
	    if (idio_isa_pair (et)) {
		return idio_meaning_block (src, et, nametree, escapes, flags, cs, cm);
	    } else {
		fprintf (stderr, "empty body for block => void\n");
		idio_debug ("src=%s\n", src);
		idio_meaning_dump_src_properties ("idio_meaning", "block", src);
		return idio_meaning (src, idio_S_void, nametree, escapes, flags, cs, cm);
	    }
	} else if (idio_S_dynamic == eh) {
	    /* (dynamic var) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_dynamic_reference (src, IDIO_PAIR_H (et), nametree, escapes, flags, cs, cm);
	    } else {
		/*
		 * Test Case: evaluation-errors/dynamic-nil.idio
		 *
		 * (dynamic)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("dynamic"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_dynamic_let == eh) {
	    /* (dynamic-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_dynamic_let (src, IDIO_PAIR_H (eth), IDIO_PAIR_H (etht), IDIO_PAIR_T (et), nametree, escapes, flags, cs, cm);
		    } else {
			/*
			 * Test Case: evaluation-errors/dynamic-let-bindings-not-tuple.idio
			 *
			 * dynamic-let (d)
			 */
			idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("dynamic-let"), "bindings not a tuple", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/dynamic-let-bindings-not-pair.idio
		     *
		     * dynamic-let #n
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("dynamic-let"), "bindings not a pair", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/dynamic-let-nil.idio
		 *
		 * (dynamic-let)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("dynamic-let"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_dynamic_unset == eh) {
	    /* (dynamic-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_dynamic_unset (src, eth, IDIO_PAIR_T (et), nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/dynamic-unset-non-sym.idio
		     *
		     * dynamic-unset 1
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("dynamic-unset"), "not a symbol", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/dynamic-unset-nil.idio
		 *
		 * (dynamic-unset)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("dynamic-unset"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_environ_let == eh) {
	    /* (environ-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_environ_let (src, IDIO_PAIR_H (eth), IDIO_PAIR_H (etht), IDIO_PAIR_T (et), nametree, escapes, flags, cs, cm);
		    } else {
			/*
			 * Test Case: evaluation-errors/environ-let-bindings-not-tuple.idio
			 *
			 * environ-let (e)
			 */
			idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("environ-let"), "bindings not a tuple", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/environ-let-bindings-not-pair.idio
		     *
		     * environ-let #n
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("environ-let"), "bindings not a pair", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/environ-let-nil.idio
		 *
		 * (environ-let)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("environ-let"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_environ_unset == eh) {
	    /* (environ-unset var body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_symbol (eth)) {
		    return idio_meaning_environ_unset (src, eth, IDIO_PAIR_T (et), nametree, escapes, flags, cs, cm);
		} else {
		    /*
		     * Test Case: evaluation-errors/environ-unset-non-sym.idio
		     *
		     * environ-unset 1
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("environ-unset"), "not a symbol", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/environ-unset-nil.idio
		 *
		 * (environ-unset)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("environ-unset"), "no argument", eh);

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
			return idio_meaning_trap (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettt, nametree, escapes, flags, cs, cm);
		    } else {
			/*
			 * Test Case: evaluation-errors/trap-condition-handler-nil.idio
			 *
			 * trap condition handler
			 */
			idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("environ-unset"), "no body", e);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/trap-condition-nil.idio
		     *
		     * trap condition
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("trap"), "no handler", e);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/trap-nil.idio
		 *
		 * (trap)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("trap"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_include == eh) {
	    /* (include filename) */
	    if (idio_isa_pair (et)) {
#ifdef IDIO_INCLUDE_TIMING
		struct timeval t0;
		if (gettimeofday (&t0, NULL) == -1) {
		    perror ("gettimeofday");
		}
#endif
		IDIO r = idio_meaning_include (src, IDIO_PAIR_H (et), nametree, escapes, flags, cs, cm);
#ifdef IDIO_INCLUDE_TIMING
		struct timeval te;
		if (gettimeofday (&te, NULL) == -1) {
		    perror ("gettimeofday");
		}
		struct timeval td;
		td.tv_sec = te.tv_sec - t0.tv_sec;
		td.tv_usec = te.tv_usec - t0.tv_usec;
		if (td.tv_usec < 0) {
		    td.tv_usec += 1000000;
		    td.tv_sec -= 1;
		}
		fprintf (stderr, "include: +%2lds %ld.%06ld e ", idio_vm_elapsed (), td.tv_sec, td.tv_usec);
		idio_debug ("%s\n", IDIO_PAIR_H (et));
#endif
		return r;
	    } else {
		/*
		 * Test Case: evaluation-errors/include-nil.idio
		 *
		 * (include)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("include"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_template_expand == eh) {
	    /* (template-expand expr) */

	    /*
	     * template-expand is here as a special form because it saves
	     * a bit of legwork jumping to and from C and Idio.
	     *
	     * If it had been a regular template then (template-expand
	     * expr) would be identified as a template and
	     * idio_template_expand will be called with the full
	     * expression from which idio_initial_expander will
	     * recognise template-expand as an expander and call
	     * idio_apply with the associated function, the Primitive
	     * template-expand, and the full expression.  Just as it
	     * would for any other template.  The Primitive would then
	     * call idio_template_expand (again!)  with just the expr.
	     *
	     * You sense we could skip the middle man...
	     */
	    if (idio_isa_pair (et)) {
		return idio_template_expand (et);
	    } else {
		/*
		 * Test Case: evaluation-errors/template-expand-nil.idio
		 *
		 * (template-expand)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("template-expand"), "no argument", eh);

		return idio_S_notreached;
	    }
	} else {
	    if (idio_isa_symbol (eh)) {
		IDIO si = idio_meaning_variable_info (src, nametree, eh, IDIO_MEANING_TOPLEVEL_SCOPE (flags), cs, cm);

		if (idio_S_unspec != si) {
		    if (idio_S_false != idio_expanderp (eh)) {
			return idio_meaning_expander (src, e, nametree, escapes, flags, cs, cm);
		    }
		}
	    }

	    return idio_meaning_application (src, eh, et, nametree, escapes, flags, cs, cm);
	}
    } else {
	switch ((intptr_t) e & IDIO_TYPE_MASK) {
	case IDIO_TYPE_FIXNUM_MARK:
	case IDIO_TYPE_CONSTANT_MARK:
	    return idio_meaning_quotation (src, e, nametree, escapes, flags);
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
		    return idio_meaning_reference (src, e, nametree, escapes, IDIO_MEANING_TOPLEVEL_SCOPE (flags), cs, cm);
		case IDIO_TYPE_STRING:
		case IDIO_TYPE_KEYWORD:
		case IDIO_TYPE_ARRAY:
		case IDIO_TYPE_HASH:
		case IDIO_TYPE_BIGNUM:
		case IDIO_TYPE_BITSET:
		    return idio_meaning_quotation (src, e, nametree, escapes, flags);

		case IDIO_TYPE_CLOSURE:
		case IDIO_TYPE_PRIMITIVE:
		    /*
		     * Test Case: ??
		     *
		     * XXX I used to get here because of a coding error...
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quotation/function"), "invalid constant type", e);

		    return idio_S_notreached;
		case IDIO_TYPE_STRUCT_INSTANCE:
		    /*
		     * Needed for conditions, pathnames, ...
		     */
		    return idio_meaning_quotation (src, e, nametree, escapes, flags);

		case IDIO_TYPE_MODULE:
		case IDIO_TYPE_FRAME:
		case IDIO_TYPE_HANDLE:
		case IDIO_TYPE_STRUCT_TYPE:
		case IDIO_TYPE_THREAD:
		case IDIO_TYPE_CONTINUATION:
		case IDIO_TYPE_C_CHAR:
		case IDIO_TYPE_C_SCHAR:
		case IDIO_TYPE_C_UCHAR:
		case IDIO_TYPE_C_SHORT:
		case IDIO_TYPE_C_USHORT:
		case IDIO_TYPE_C_INT:
		case IDIO_TYPE_C_UINT:
		case IDIO_TYPE_C_LONG:
		case IDIO_TYPE_C_ULONG:
		case IDIO_TYPE_C_LONGLONG:
		case IDIO_TYPE_C_ULONGLONG:
		case IDIO_TYPE_C_FLOAT:
		case IDIO_TYPE_C_DOUBLE:
		case IDIO_TYPE_C_LONGDOUBLE:
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
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quotation/various"), "invalid constant type", e);

		    return idio_S_notreached;
		default:
		    /*
		     * Test Case: ??
		     *
		     * Definitely a developer coding error...
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quotation/various"), "unimplemented type", e);

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

IDIO idio_evaluate (IDIO src, IDIO cs)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (cs);


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
    IDIO m = idio_meaning (src, src, idio_S_nil, idio_S_nil, IDIO_MEANING_FLAG_NONE, cs, idio_thread_current_module ());

    idio_gc_resume ("idio_evaluate");

    return IDIO_LIST2 (IDIO_I_ABORT, m);
}

IDIO_DEFINE_PRIMITIVE1_DS ("environ?", environp, (IDIO o), "o", "\
test if `o` is an environ variable		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an environ variable, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	IDIO si = idio_module_env_symbol_recurse (o);

	if (idio_S_false != si &&
	    idio_S_environ == IDIO_PAIR_H (si)) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("dynamic?", dynamicp, (IDIO o), "o", "\
test if `o` is a dynamic variable		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a dynamic variable, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	IDIO si = idio_module_env_symbol_recurse (o);

	if (idio_S_false != si &&
	    idio_S_dynamic == IDIO_PAIR_H (si)) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("computed?", computedp, (IDIO o), "o", "\
test if `o` is a computed variable		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a computed variable, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	IDIO si = idio_module_env_symbol_recurse (o);

	if (idio_S_false != si &&
	    idio_S_computed == IDIO_PAIR_H (si)) {
	    r = idio_S_true;
	}
    }

    return r;
}

void idio_evaluate_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (environp);
    IDIO_ADD_PRIMITIVE (dynamicp);
    IDIO_ADD_PRIMITIVE (computedp);
}

void idio_init_evaluate ()
{
    idio_module_table_register (idio_evaluate_add_primitives, NULL, NULL);

    idio_evaluate_module = idio_module (idio_symbols_C_intern ("evaluate"));

#define IDIO_MEANING_STRING(c,s) idio_meaning_ ## c ## _string = idio_string_C (s); idio_gc_protect_auto (idio_meaning_ ## c ## _string);

    IDIO_MEANING_STRING (predef_extend, "idio_predef_extend");
    IDIO_MEANING_STRING (toplevel_extend, "idio_toplevel_extend");
    IDIO_MEANING_STRING (dynamic_extend, "idio_dynamic_extend");
    IDIO_MEANING_STRING (environ_extend, "idio_environ_extend");
    IDIO_MEANING_STRING (define_gvi0, "idio-meaning-define/gvi=0");
    IDIO_MEANING_STRING (define_infix_operator, "idio-meaning-define-infix-operator");
    IDIO_MEANING_STRING (define_postfix_operator, "idio-meaning-define-postfix-operator");
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
