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
 * evaluate.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <inttypes.h>
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

#include "array.h"
#include "c-type.h"
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
#include "vm-asm.h"
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
 * each module.  Each new module/name is mapped into the VM's table of
 * values.

 * Dynamic and environment variables are like regular names in modules
 * except we look them up differently (on the stack first).

 * The order of lookup as we see a symbol is:

 * 1. local environments:

 *    these are a hierarchy of (flat) environments corresponding to a
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
 *    populate a normal list of names in the Idio and *operator*
 *    modules.
 *
 *    Idio for the usual culprits ("+", "append-string", etc.)  and
 *    *operators* for operators (primarily because, say, the operator
 *    "-" is an obvious name clash with the regular subtraction
 *    primitive "-" and, in fact, is simply a transformer into that).

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

 * During compilation we need to maintain the artifice of modules when
 * we lookup symbols in which case the values of the symbols of each
 * module will be a mapping to the toplevel description (nominally the
 * pair (idio_S_toplevel . i) where i is in the index into the
 * compilers tables of all known toplevel values.
 *
 */

IDIO idio_evaluate_module = idio_S_nil;
static IDIO idio_evaluate_evaluate_sym = idio_S_nil;
static IDIO idio_meaning_precompilation_string = idio_S_nil;
static IDIO idio_meaning_predef_extend_string = idio_S_nil;
static IDIO idio_meaning_toplevel_extend_string = idio_S_nil;
static IDIO idio_meaning_dynamic_extend_string = idio_S_nil;
static IDIO idio_meaning_environ_extend_string = idio_S_nil;
static IDIO idio_meaning_define_gvi0_string = idio_S_nil;
static IDIO idio_meaning_define_infix_operator_string = idio_S_nil;
static IDIO idio_meaning_define_postfix_operator_string = idio_S_nil;
static IDIO idio_meaning_fix_abstraction_string = idio_S_nil;
static IDIO idio_meaning_dotted_abstraction_string = idio_S_nil;

IDIO idio_evaluate_eenv_type = idio_S_nil;

void idio_meaning_warning (char const *prefix, char const *msg, IDIO e)
{
    IDIO_ASSERT (e);

    fprintf (stderr, "WARNING: %s: %s:", prefix, msg);

    if (idio_isa_pair (e)) {
	IDIO lo = idio_hash_ref (idio_src_properties, e);
	if (idio_S_unspec == lo){
	    fprintf (stderr, "<no lexobj>");
	} else {
	    idio_debug ("%s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_NAME));
	    idio_debug (":line %s", idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_LINE));
	}
    }

    idio_debug (": %s\n", e);
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
	idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_NAME), lsh);
	idio_display_C (":line ", lsh);
	idio_display (idio_struct_instance_ref_direct (lo, IDIO_LEXOBJ_ST_LINE), lsh);
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
     * Dirty hack!
     *
     * idio_error_init is going to figure out a normal function and
     * location using *func* and idio_vm_source_location() which is
     * fine for normal things.  This is an evaluation error,
     * ie. before the byte code has been generated for the VM to have
     * a sensible *expr* register (and we'll have whatever was left in
     * *func*).
     *
     * So, let's give the error subsystem a clue by copying what the
     * code generator does for normal calls.
     */
    idio_as_t mci = idio_codegen_extend_src_exprs (idio_default_eenv, src);
    IDIO thr = idio_thread_current_thread ();
    IDIO_THREAD_EXPR (thr) = idio_fixnum (mci);

    /*
     * We need to set *func* so that we don't have the last VM
     * function call indicated and it must be a valid invocable entity
     * so idio_vm_restore_all_state() doesn't complain.
     */
    IDIO_THREAD_FUNC (thr) = IDIO_SYMBOL ("evaluate");

    IDIO lsh;
    IDIO dsh;
    idio_error_init (NULL, &lsh, &dsh, c_location);

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

    idio_error_raise_noncont (idio_condition_evaluation_error_type,
			      IDIO_LIST4 (msg,
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh),
					  expr));

    /* notreached */
}

void idio_meaning_error_param_type (IDIO src, IDIO c_location, char const *msg, IDIO expr)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("parameter type: ", msh);
    idio_display_C (msg, msh);

    idio_meaning_base_error (src, c_location, idio_get_output_string (msh), expr);

    /* notreached */
}

static void idio_meaning_error_param (IDIO src, IDIO c_location, char const *msg, IDIO expr)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display (expr, msh);
    idio_display_C (": ", msh);
    idio_display_C (msg, msh);

    idio_meaning_base_error (src, c_location, idio_get_output_string (msh), expr);

    /* notreached */
}

void idio_meaning_evaluation_error (IDIO src, IDIO c_location, char const *msg, IDIO expr)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (expr);

    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    idio_meaning_base_error (src, c_location, idio_get_output_string (msh), expr);

    /* notreached */
}

/*
 * There are half a dozen calls to
 * idio_meaning_error_static_redefine() in places where it requires we
 * have broken the code to raise it.
 */
void idio_meaning_error_static_redefine (IDIO src, IDIO c_location, char const *msg, IDIO name, IDIO cv, IDIO new)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (cv);
    IDIO_ASSERT (new);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_display_C (msg, dsh);
    idio_display_C (": ", dsh);
    idio_display (name, dsh);
    idio_display_C (" is currently ", dsh);
    idio_display (cv, dsh);
    idio_display_C (": proposed: ", dsh);
    idio_display (new, dsh);

    idio_error_raise_noncont (idio_condition_st_variable_error_type,
			      IDIO_LIST4 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh),
					  name));

    /* notreached */
}

/*
 * The calls to idio_meaning_error_static_variable() requires we have
 * broken the code to raise it.
 */
static void idio_meaning_error_static_variable (IDIO src, IDIO c_location, char const *msg, IDIO name)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_noncont (idio_condition_st_variable_error_type,
			      IDIO_LIST4 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh),
					  name));

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

void idio_meaning_error_static_arity (IDIO src, IDIO c_location, char const *msg, IDIO args)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (c_location);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (args);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);
    IDIO_TYPE_ASSERT (list, args);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    idio_error_raise_noncont (idio_condition_st_function_arity_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

    /* notreached */
}

static void idio_meaning_error_static_primitive_arity (IDIO src, IDIO c_location, char const *msg, IDIO f, IDIO args, IDIO primdata)
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

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);

    char em[BUFSIZ];
    size_t eml = idio_snprintf (em, BUFSIZ, "ARITY != %" PRId8 "%s; primitive (", IDIO_PRIMITIVE_ARITY (primdata), IDIO_PRIMITIVE_VARARGS (primdata) ? "+" : "");
    idio_display_C_len (em, eml, dsh);

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

	IDIO_GC_FREE (s, size);
    }
    idio_display_C (")", dsh);

    idio_error_raise_noncont (idio_condition_st_function_arity_error_type,
			      IDIO_LIST3 (idio_get_output_string (msh),
					  idio_get_output_string (lsh),
					  idio_get_output_string (dsh)));

    /* notreached */
}

#define IDIO_MEANING_PREDEF_FLAG_NONE	0
#define IDIO_MEANING_PREDEF_FLAG_EXPORT	1

static IDIO idio_meaning_flags_scope (int flags)
{
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
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected variable scope %#x", flags);

	return idio_S_notreached;
    }

    return scope;
}

/*
 * How we look up a name depends on whether we are compiling "live" or
 * pre-compiling.
 *
 * "Live" compilation can perform a regulation
 * idio_module_find_symbol().
 *
 * Pre-compilation wants to maintain a list of names it wants to use
 * in the future.
 *
 * The eenv (evaluation environment) structure looks like:
 *
 * aot?			boolean: normal or ahead-of-time
 * symbols		list of (name {si-data})
 * values		list of indices (these are defines, dynamic and environs)
 * constants		array
 * constants-hash	constant to ci map
 * module		module
 * escapes		list
 * src-exprs
 * src-props		lexobj data
 * byte-code
 *
 * values is a placeholder for a table of gvi at runtime.  The
 * {si-data} should have an mci field indexing into the constants
 * array and a mvi indexing into the values table (for future
 * reference).
 *
 * It should be capturing the environment beyond the current
 * expression.  The top level.
 *
 * And escapes, even though they are dynamic like nametree.
 */

IDIO idio_meaning_eenv_symbol_index (IDIO eenv, IDIO si)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (si);

    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO_TYPE_ASSERT (list, si);

    IDIO sym_idx = idio_S_false;
    if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	sym_idx = IDIO_SI_SI (si);
    } else {
	sym_idx = IDIO_SI_CI (si);
    }

    return sym_idx;
}

static idio_as_t idio_meaning_constants_lookup_or_extend (IDIO eenv, IDIO name)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (name);

    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO_TYPE_ASSERT (symbol, name);

    if (idio_S_false == IDIO_MEANING_EENV_AOT (eenv)) {
	return idio_codegen_constants_lookup_or_extend (eenv, name);
    }

    IDIO cs = IDIO_MEANING_EENV_CONSTANTS (eenv);
    IDIO ch = IDIO_MEANING_EENV_CONSTANTS_HASH (eenv);

    idio_ai_t C_ci = -1;

    if (idio_S_nil != name) {
	IDIO ci = idio_hash_reference (ch, name, IDIO_LIST1 (idio_S_false));

	if (idio_S_false == ci) {
	    C_ci = idio_array_find_eqp (cs, name, 0);
	} else {
	    C_ci = IDIO_FIXNUM_VAL (ci);
	}
    }

    if (-1 == C_ci) {
	C_ci = idio_array_size (cs);
	idio_array_push (cs, name);

	if (idio_S_nil != name) {
	    idio_hash_put (ch, name, idio_fixnum (C_ci));
	}
    }

    return C_ci;
}

static idio_as_t idio_meaning_extend_tables (IDIO eenv,
					     IDIO name,
					     IDIO scope,
					     IDIO ci,
					     int use_vi,
					     IDIO module,
					     IDIO desc,
					     int set_symbol)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (name);
    IDIO_ASSERT (scope);
    IDIO_ASSERT (ci);
    IDIO_ASSERT (module);
    IDIO_ASSERT (desc);

    IDIO_TYPE_ASSERT (struct_instance, eenv);

    idio_array_push (IDIO_MEANING_EENV_ST (eenv), ci);

    if (idio_S_false == IDIO_MEANING_EENV_AOT (eenv)) {
	idio_array_push (IDIO_MEANING_EENV_VT (eenv), idio_fixnum (0));
	return idio_vm_extend_default_values ();
    }

    IDIO vs = IDIO_MEANING_EENV_VALUES (eenv);

    /*
     * NB Normally a vi of 0 indicates the symbol hasn't been defined
     * (yet) however these vi are placeholders.
     */
    idio_ai_t C_vi = 0;
    if (idio_isa_fixnum (vs)) {
	C_vi = IDIO_FIXNUM_VAL (vs);
	C_vi++;
    }

    IDIO vi = idio_fixnum (C_vi);
    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_VALUES, vi);

    if (idio_S_false == module) {
	module = IDIO_MEANING_EENV_MODULE (eenv);
    }

    IDIO used_vi = use_vi ? vi : idio_fixnum (0);
    idio_array_push (IDIO_MEANING_EENV_VT (eenv), used_vi);

    IDIO sym_si = IDIO_LIST7 (name,
			      scope,
			      vi,
			      ci,
			      used_vi,
			      module,
			      desc);

    idio_struct_instance_set_direct (eenv,
				     IDIO_EENV_ST_SYMBOLS,
				     idio_pair (sym_si, IDIO_MEANING_EENV_SYMBOLS (eenv)));

    if (set_symbol) {
	idio_module_set_symbol (name, IDIO_PAIR_T (sym_si), module);
    }

    return C_vi;
}

static IDIO idio_meaning_find_symbol_recurse (IDIO name, IDIO eenv, IDIO scope, int recurse)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (scope);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO_TYPE_ASSERT (symbol, scope);

    IDIO name_si = idio_module_find_symbol_recurse (name, IDIO_MEANING_EENV_MODULE (eenv), recurse);

    if (idio_S_false == IDIO_MEANING_EENV_AOT (eenv)) {
	return name_si;
    }

    IDIO st_sym_si = idio_list_assq (name, IDIO_MEANING_EENV_SYMBOLS (eenv));

    if (idio_S_false == st_sym_si) {
	idio_as_t C_ci = idio_meaning_constants_lookup_or_extend (eenv, name);
	IDIO ci = idio_fixnum (C_ci);

	if (idio_isa_pair (name_si)) {
	    idio_meaning_extend_tables (eenv,
					name,
					IDIO_SI_SCOPE (name_si),
					ci,
					0,
					IDIO_SI_MODULE (name_si),
					idio_meaning_precompilation_string,
					0);

	    if (idio_S_predef == IDIO_SI_SCOPE (name_si)) {
		/*
		 * Notionally we don't care about the (future) vi
		 * right now just that we reserve a slot for
		 * ourselves.
		 *
		 * However, we want the running vi as later on we'll
		 * need to access the underlying primdata.
		 */
		st_sym_si = idio_list_assq (name, IDIO_MEANING_EENV_SYMBOLS (eenv));
		IDIO vt_tail = IDIO_PAIR_TT (IDIO_PAIR_TT (st_sym_si));
		IDIO_PAIR_H (vt_tail) = IDIO_SI_VI (name_si);
	    }
	} else {
	    /* is name M/S ? */
	    IDIO sdr = idio_module_direct_reference (name);

	    if (idio_S_false == sdr) {
		idio_meaning_extend_tables (eenv,
					    name,
					    scope,
					    ci,
					    1,
					    idio_S_false,
					    idio_meaning_precompilation_string,
					    0);
	    } else {
		/* (M S si) */
		IDIO sdr_si = IDIO_PAIR_HTT (sdr);

		/*
		 * The chances are that the existing scope, in sdr, is
		 * 'predef, however if we reuse that then subsequent
		 * lookups will try to run the 'predef code in
		 * meaning-application which gets us in a mess for
		 * pre-compilation.
		 *
		 * From our future compiled code's perspective we're
		 * looking up another 'toplevel (which will be
		 * resolved in the running code into the predef).
		 *
		 * If it's a dynamic/environ/computed we want to
		 * retain that information (as it generates different
		 * byte code).
		 */
		IDIO new_scope = IDIO_SI_SCOPE (sdr_si);
		if (idio_S_predef == new_scope) {
		    new_scope = idio_S_toplevel;
		}

		idio_meaning_extend_tables (eenv,
					    name,
					    new_scope,
					    ci,
					    0,
					    IDIO_SI_MODULE (sdr_si),
					    IDIO_SI_DESCRIPTION (sdr_si),
					    1);
	    }
	}

	st_sym_si = idio_list_assq (name, IDIO_MEANING_EENV_SYMBOLS (eenv));
    }

    return IDIO_PAIR_T (st_sym_si);
}

static IDIO idio_meaning_find_symbol (IDIO name, IDIO eenv, IDIO scope)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (scope);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO_TYPE_ASSERT (symbol, scope);

    return idio_meaning_find_symbol_recurse (name, eenv, scope, 0);
}

static IDIO idio_meaning_find_toplevel_symbol (IDIO name, IDIO eenv)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_find_symbol_recurse (name, eenv, idio_S_toplevel, 0);
}

static IDIO idio_meaning_find_dynamic_symbol (IDIO name, IDIO eenv)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_find_symbol_recurse (name, eenv, idio_S_dynamic, 0);
}

static IDIO idio_meaning_find_environ_symbol (IDIO name, IDIO eenv)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_find_symbol_recurse (name, eenv, idio_S_environ, 0);
}

/*
 * idio_meaning_predef_extend is ultimately called by
 * IDIO_ADD_MODULE() and IDIO_EXPORT_MODULE() macros in
 * idio_X_add_primitives() for some C code, X.c
 */
static IDIO predef_desc = idio_S_nil;
static IDIO idio_meaning_predef_extend (idio_primitive_desc_t *d, int flags, IDIO module, char const *cpp__FILE__, int cpp__LINE__)
{
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (module);

    IDIO_TYPE_ASSERT (module, module);

    IDIO primdata = idio_primitive_data (d);
    IDIO name = idio_symbols_C_intern (d->name, d->name_len);

    if (IDIO_MEANING_PREDEF_FLAG_EXPORT == flags) {
	IDIO_MODULE_EXPORTS (module) = idio_pair (name, IDIO_MODULE_EXPORTS (module));
    }

    if (idio_S_nil == predef_desc) {
	predef_desc = IDIO_STRING ("predef");
	idio_gc_protect_auto (predef_desc);
    }
    IDIO eenv = idio_evaluate_normal_eenv (predef_desc, module);

    IDIO si = idio_meaning_find_toplevel_symbol (name, eenv);

    if (idio_isa_pair (si)) {
	IDIO fgvi = IDIO_SI_VI (si);

	/*
	 * Should only be called in C bootstrap
	 */
	IDIO pd = idio_vm_default_values_ref (IDIO_FIXNUM_VAL (fgvi));

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

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, name);
    IDIO fmci = idio_fixnum (mci);
    idio_module_set_vci (module, fmci, fmci);

    idio_as_t gvi = idio_meaning_extend_tables (eenv,
						name,
						idio_S_predef,
						fmci,
						0,
						module,
						idio_meaning_predef_extend_string,
						1);
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_set_vvi (module, fmci, fgvi);

    /*
     * idio_meaning_extend_tables() doesn't set the symbol info for
     * the initial environment.  So patch things up here.
     */
    if (idio_S_false == IDIO_MEANING_EENV_AOT (eenv)) {
	IDIO sym_si = IDIO_LIST7 (name,
				  idio_S_predef,
				  fmci,
				  fmci,
				  fgvi,
				  module,
				  idio_meaning_predef_extend_string);

	idio_struct_instance_set_direct (eenv,
					 IDIO_EENV_ST_SYMBOLS,
					 idio_pair (sym_si, IDIO_MEANING_EENV_SYMBOLS (eenv)));

	idio_module_set_symbol (name, IDIO_PAIR_T (sym_si), module);
    }

    /*
     * idio_module_set_symbol_value() is a bit sniffy about setting
     * predefs -- rightly so -- so go under the hood!
     */
    idio_vm_default_values_set (gvi, primdata);

    return fgvi;
}

IDIO idio_add_module_primitive (IDIO module, idio_primitive_desc_t *d, char const *cpp__FILE__, int cpp__LINE__)
{
    IDIO_ASSERT (module);
    IDIO_C_ASSERT (d);

    IDIO_TYPE_ASSERT (module, module);

    return idio_meaning_predef_extend (d, IDIO_MEANING_PREDEF_FLAG_NONE, module, cpp__FILE__, cpp__LINE__);
}

IDIO idio_export_module_primitive (IDIO module, idio_primitive_desc_t *d, char const *cpp__FILE__, int cpp__LINE__)
{
    IDIO_ASSERT (module);
    IDIO_C_ASSERT (d);

    IDIO_TYPE_ASSERT (module, module);

    return idio_meaning_predef_extend (d, IDIO_MEANING_PREDEF_FLAG_EXPORT, module, cpp__FILE__, cpp__LINE__);
}

IDIO idio_add_primitive (idio_primitive_desc_t *d, char const *cpp__FILE__, int cpp__LINE__)
{
    IDIO_C_ASSERT (d);

    return idio_export_module_primitive (idio_Idio_module, d, cpp__FILE__, cpp__LINE__);
}

IDIO idio_toplevel_extend (IDIO src, IDIO name, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO scope = idio_meaning_flags_scope (flags);

    IDIO si = idio_meaning_find_symbol (name, eenv, scope);

    if (idio_isa_pair (si)) {
	IDIO curscope = IDIO_SI_SCOPE (si);
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
	    return idio_meaning_eenv_symbol_index (eenv, si);
	}
    }

#ifdef IDIO_EVALUATE_DEBUG
    if (idio_S_toplevel == scope &&
	0 == IDIO_MEANING_IS_DEFINE (flags)) {
	idio_meaning_warning ("toplevel-extend", "forward reference", src);
	idio_debug ("name=%s\n", name);
    }
#endif

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, name);
    IDIO fmci = idio_fixnum (mci);

    idio_meaning_extend_tables (eenv,
				name,
				scope,
				fmci,
				0,
				idio_S_false,
				idio_meaning_toplevel_extend_string,
				1);

    /*
     * idio_meaning_extend_tables() doesn't set the symbol info for
     * the initial environment.  So patch things up here.
     */
    if (idio_S_false == IDIO_MEANING_EENV_AOT (eenv)) {
	IDIO module = IDIO_MEANING_EENV_MODULE (eenv);
	idio_module_set_symbol (name,
				IDIO_LIST6 (scope,
					    fmci,
					    fmci,
					    idio_fixnum (0),
					    module,
					    idio_meaning_toplevel_extend_string),
				module);
    }
    return fmci;
}

/*
 * Called by code in vars.c
 */
IDIO idio_dynamic_extend (IDIO src, IDIO name, IDIO val, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (val);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    /*
     * All dynamic symbols live in the Idio module -- it doesn't make
     * any sense for dynamic symbols to be different in other modules
     * as the stack, where dynamic variables live, is shared across
     * all modules
     */
    IDIO dynamic_eenv = idio_struct_instance_copy (eenv);
    idio_struct_instance_set_direct (dynamic_eenv, IDIO_EENV_ST_MODULE, idio_Idio_module);

    IDIO si = idio_meaning_find_dynamic_symbol (name, dynamic_eenv);

    if (idio_isa_pair (si)) {
	IDIO scope = IDIO_SI_SCOPE (si);

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
	    return idio_meaning_eenv_symbol_index (dynamic_eenv, si);
	}
    }

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (dynamic_eenv, name);
    IDIO fmci = idio_fixnum (mci);

    idio_as_t gvi = idio_meaning_extend_tables (dynamic_eenv,
						name,
						idio_S_dynamic,
						fmci,
						1,
						idio_S_false,
						idio_meaning_dynamic_extend_string,
						1);
    IDIO fgvi = idio_fixnum (gvi);

    si = idio_vm_add_dynamic (idio_Idio_module, fmci, fgvi, idio_meaning_dynamic_extend_string);
    idio_module_set_symbol (name, si, idio_Idio_module);
    idio_module_set_symbol_value (name, val, idio_Idio_module);

    return fmci;
}

/*
 * Called by code in env.c
 */
IDIO idio_environ_extend (IDIO src, IDIO name, IDIO val, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (val);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    /*
     * All environ symbols live in the Idio module -- it doesn't make
     * any sense for environ symbols to be different in other modules
     * as the stack (and environ(3P)), where environ variables live,
     * is shared by all modules
     */
    IDIO environ_eenv = idio_struct_instance_copy (eenv);
    idio_struct_instance_set_direct (environ_eenv, IDIO_EENV_ST_MODULE, idio_Idio_module);

    IDIO si = idio_meaning_find_environ_symbol (name, environ_eenv);

    if (idio_isa_pair (si)) {
	IDIO scope = IDIO_SI_SCOPE (si);

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
	    return idio_meaning_eenv_symbol_index (environ_eenv, si);
	}
    }

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (environ_eenv, name);
    IDIO fmci = idio_fixnum (mci);

    idio_as_t gvi = idio_meaning_extend_tables (environ_eenv,
						name,
						idio_S_environ,
						fmci,
						1,
						idio_S_false,
						idio_meaning_environ_extend_string,
						1);
    IDIO fgvi = idio_fixnum (gvi);

    si = idio_vm_add_environ (idio_Idio_module, fmci, fgvi, idio_meaning_environ_extend_string);
    idio_module_set_symbol (name, si, idio_Idio_module);
    idio_module_set_symbol_value (name, val, idio_Idio_module);

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
 * The return is, say, ('dynamic {mci} {mci}) where {mci} is the
 * constant index associated with the dynamic/environ variable.  {mci}
 * is added twice to avoid inconsistencies with a top level dynamic
 * variable which will have a regular symbol information tuple.
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
	    if (idio_isa_pair (assq)) {
		IDIO scope = IDIO_PAIR_HT (assq);
		if (idio_S_param == scope ||
		    idio_S_local == scope) {
		    return IDIO_LIST3 (scope, idio_fixnum (i), IDIO_PAIR_HTT (assq));
		} else if (idio_S_dynamic == scope ||
			   idio_S_environ == scope) {
		    return IDIO_LIST3 (IDIO_PAIR_HT (assq), IDIO_PAIR_HTT (assq), IDIO_PAIR_HTT (assq));
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
    while (! done &&
	   idio_S_nil != nametree) {
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

static IDIO idio_meaning_variable_info (IDIO src, IDIO nametree, IDIO name, int flags, IDIO eenv, int recurse)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO si = idio_meaning_lexical_lookup (src, nametree, name);

    if (idio_S_false == si) {
	IDIO scope = idio_meaning_flags_scope (flags);

	/*
	 * NOTICE This must be a recursive lookup.  Otherwise we'll
	 * not see any bindings in Idio.
	 *
	 * Unless we're defining something in which case we must NOT
	 * recurse...although that's down to our caller to decide
	 */
        si = idio_meaning_find_symbol_recurse (name, eenv, scope, recurse);

	if (idio_S_false == si) {

	    /* is name M/S ? */
	    IDIO sdr = idio_module_direct_reference (name);

	    if (idio_isa_pair (sdr)) {
		/* (M S si) */
		si = IDIO_PAIR_HTT (sdr);
		idio_module_set_symbol (name, si, IDIO_MEANING_EENV_MODULE (eenv));
	    } else {
		/*
		 * auto-extend the toplevel with this unknown variable
		 * -- we should (eventually) see a definition for it
		 */
		idio_toplevel_extend (src, name, flags, eenv);
		si = idio_meaning_find_toplevel_symbol (name, eenv);
	    }
	}
    }

    return si;
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
		    if (idio_S_function_name == IDIO_PAIR_H (src) ||
			idio_S_function == IDIO_PAIR_H (src)) {
			idio_debug ("\n\nIDIO_DEBUG: FATAL: im_csp no src lo for\n src=%s\n", src);
			idio_debug (" dst=%s\n", dst);
			/* IDIO_C_ASSERT (0); */
		    }
#endif
		} else {
		    dlo = idio_copy (slo, IDIO_COPY_SHALLOW);
		    idio_struct_instance_set_direct (dlo, IDIO_LEXOBJ_ST_EXPR, dst);
		    idio_hash_put (idio_src_properties, dst, dlo);
		}
	    }
	}
    }
}

static IDIO idio_meaning (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv);

static IDIO idio_meaning_reference (IDIO src, IDIO name, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO si = idio_meaning_variable_info (src, nametree, name, flags, eenv, 1);

    if (! idio_isa_pair (si)) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), name);

	return idio_S_notreached;
    }

    int aot = idio_S_true == IDIO_MEANING_EENV_AOT (eenv);

    IDIO scope = IDIO_SI_SCOPE (si);

    if (idio_S_param == scope ||
	idio_S_local == scope) {
	IDIO fi = IDIO_NT_PARAM_I (si);
	IDIO fj = IDIO_NT_PARAM_J (si);
	if (0 == IDIO_FIXNUM_VAL (fi)) {
	    return IDIO_LIST2 (IDIO_I_SHALLOW_ARGUMENT_REF, fj);
	} else {
	    return IDIO_LIST3 (IDIO_I_DEEP_ARGUMENT_REF, fi, fj);
	}
    } else if (idio_S_toplevel == scope) {
	IDIO fgvi = IDIO_SI_VI (si);
	if (0 == aot &&
	    IDIO_FIXNUM_VAL (fgvi) > 0) {
	    return IDIO_LIST2 (IDIO_I_VAL_REF, fgvi);
	} else {
	    return IDIO_LIST2 (IDIO_I_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
	}
    } else if (idio_S_dynamic == scope) {
	return IDIO_LIST2 (IDIO_I_DYNAMIC_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
    } else if (idio_S_environ == scope) {
	return IDIO_LIST2 (IDIO_I_ENVIRON_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
    } else if (idio_S_computed == scope) {
	return IDIO_LIST2 (IDIO_I_COMPUTED_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
    } else if (idio_S_predef == scope) {
	if (aot) {
	    return IDIO_LIST2 (IDIO_I_PREDEFINED, IDIO_SI_SI (si));
	} else {
	    return IDIO_LIST2 (IDIO_I_PREDEFINED, IDIO_SI_VI (si));
	}
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

static IDIO idio_meaning_function_reference (IDIO src, IDIO name, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO si = idio_meaning_variable_info (src, nametree, name, IDIO_MEANING_TOPLEVEL_SCOPE (flags), eenv, 1);

    if (idio_S_unspec == si) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), name);

	return idio_S_notreached;
    }

    int aot = idio_S_true == IDIO_MEANING_EENV_AOT (eenv);

    IDIO scope = IDIO_SI_SCOPE (si);

    if (idio_S_param == scope ||
	idio_S_local == scope) {
	IDIO fi = IDIO_NT_PARAM_I (si);
	IDIO fj = IDIO_NT_PARAM_J (si);
	if (0 == IDIO_FIXNUM_VAL (fi)) {
	    return IDIO_LIST2 (IDIO_I_SHALLOW_ARGUMENT_REF, fj);
	} else {
	    return IDIO_LIST3 (IDIO_I_DEEP_ARGUMENT_REF, fi, fj);
	}
    } else if (idio_S_toplevel == scope) {
	IDIO fgvi = IDIO_SI_VI (si);
	if (0 == aot &&
	    IDIO_FIXNUM_VAL (fgvi) > 0) {
	    return IDIO_LIST2 (IDIO_I_FUNCTION_VAL_REF, fgvi);
	} else {
	    return IDIO_LIST2 (IDIO_I_FUNCTION_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
	}
    } else if (idio_S_dynamic == scope) {
	return IDIO_LIST2 (IDIO_I_DYNAMIC_FUNCTION_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
    } else if (idio_S_environ == scope) {
	return IDIO_LIST2 (IDIO_I_ENVIRON_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
    } else if (idio_S_computed == scope) {
	return IDIO_LIST2 (IDIO_I_COMPUTED_SYM_REF, idio_meaning_eenv_symbol_index (eenv, si));
    } else if (idio_S_predef == scope) {
	if (aot) {
	    return IDIO_LIST2 (IDIO_I_PREDEFINED, IDIO_SI_SI (si));
	} else {
	    return IDIO_LIST2 (IDIO_I_PREDEFINED, IDIO_SI_VI (si));
	}
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

static IDIO idio_meaning_not (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO tailp = idio_S_false;
    if (IDIO_MEANING_IS_TAILP (flags)) {
	tailp = idio_S_true;
    }

    return IDIO_LIST3 (IDIO_I_NOT, tailp, m);
}

static IDIO idio_meaning_escape (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning (IDIO_MPP (e, src), e, nametree, flags, eenv);
}

static IDIO idio_meaning_quotation (IDIO src, IDIO v, IDIO nametree, int flags)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (v);
    IDIO_ASSERT (nametree);

    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (IDIO_I_CONSTANT_REF, v);
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
				idio_meaning_dequasiquote (src_h, ph, level, indent + 1),
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

	r = IDIO_LIST2 (IDIO_SYMBOL ("list->array"), idio_meaning_dequasiquote (iatl, iatl, level, indent + 1));
    } else if (idio_isa_symbol (e)) {
	r = IDIO_LIST2 (idio_S_quote, e);
    } else {
	r = e;
    }

    idio_meaning_copy_src_properties (src, r);

    return r;
}

static IDIO idio_meaning_quasiquotation (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    /*
     * Regarding source properties, {src} and {e} have the correct
     * source properties for the template *expansion* so we can use
     * those -- and subsequently {dq}'s -- within this expansion.
     *
     * However, our caller won't know any better and we'll be left
     * with the original {src} source properties, the call site for
     * when the code is *applied*.
     */
    IDIO dq = idio_meaning_dequasiquote (src, e, 0, 0);

    return idio_meaning (dq, dq, nametree, flags, eenv);
}

static IDIO idio_meaning_alternative (IDIO src, IDIO e1, IDIO e2, IDIO e3, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e1);
    IDIO_ASSERT (e2);
    IDIO_ASSERT (e3);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO m1 = idio_meaning (IDIO_MPP (e1, src), e1, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);
    IDIO m2 = idio_meaning (IDIO_MPP (e2, src), e2, nametree, flags, eenv);
    IDIO m3 = idio_meaning (IDIO_MPP (e3, src), e3, nametree, flags, eenv);

    return idio_pair (IDIO_I_ALTERNATIVE,
		      idio_pair (src,
		      idio_pair (m1,
		      idio_pair (m2,
		      idio_pair (m3, idio_S_nil)))));
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
	    IDIO gs = idio_gensym (NULL, 0);
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
	IDIO gs = idio_gensym (NULL, 0);
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

static IDIO idio_meaning_assignment (IDIO src, IDIO name, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

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

	return idio_meaning (src, se, nametree, IDIO_MEANING_NO_DEFINE (flags), eenv);
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

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, mflags, eenv);

    IDIO si = idio_meaning_variable_info (src, nametree, name, flags, eenv, 0 == IDIO_MEANING_IS_DEFINE (flags));

    if (idio_S_unspec == si) {
	/*
	 * Not sure we can get here as idio_meaning_variable_info()
	 * should have created a reference (otherwise, what's it
	 * doing?)
	 */
	idio_coding_error_C ("unknown variable:", name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int aot = idio_S_true == IDIO_MEANING_EENV_AOT (eenv);

    IDIO scope = IDIO_SI_SCOPE (si);
    IDIO fmci = idio_S_undef;

    IDIO assign = idio_S_nil;

    if (idio_S_param == scope ||
	idio_S_local == scope) {
	IDIO fi = IDIO_NT_PARAM_I (si);
	IDIO fj = IDIO_NT_PARAM_J (si);
	if (0 == IDIO_FIXNUM_VAL (fi)) {
	    return IDIO_LIST3 (IDIO_I_SHALLOW_ARGUMENT_SET, fj, m);
	} else {
	    return IDIO_LIST4 (IDIO_I_DEEP_ARGUMENT_SET, fi, fj, m);
	}
    } else if (idio_S_toplevel == scope) {
	if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	    fmci = IDIO_SI_SI (si);
	} else {
	    fmci = IDIO_SI_CI (si);
	}
	IDIO fgvi = IDIO_SI_VI (si);

	if (0 == aot &&
	    IDIO_FIXNUM_VAL (fgvi) > 0) {
	    assign = IDIO_LIST4 (IDIO_I_VAL_SET, src, fgvi, m);
	} else {
	    assign = IDIO_LIST4 (IDIO_I_SYM_SET, src, fmci, m);
	}
    } else if (idio_S_dynamic == scope ||
	       idio_S_environ == scope) {
	if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	    fmci = IDIO_SI_SI (si);
	} else {
	    fmci = IDIO_SI_CI (si);
	}
	assign = IDIO_LIST4 (IDIO_I_SYM_SET, src, fmci, m);
    } else if (idio_S_computed == scope) {
	IDIO sym_idx = idio_meaning_eenv_symbol_index (eenv, si);
	if (IDIO_MEANING_IS_DEFINE (flags)) {
	    return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_SYM_DEF, name, scope, sym_idx),
			       IDIO_LIST3 (IDIO_I_COMPUTED_SYM_DEF, sym_idx, m));
	} else {
	    return IDIO_LIST3 (IDIO_I_COMPUTED_SYM_SET, sym_idx, m);
	}
    } else if (idio_S_predef == scope) {
	fmci = IDIO_SI_CI (si);
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
	IDIO new_mci = idio_toplevel_extend (src, name, IDIO_MEANING_DEFINE (IDIO_MEANING_TOPLEVEL_SCOPE (flags)), eenv);

	/*
	 * But now we have a problem.
	 *
	 * As we *compile* regular code, any subsequent reference to
	 * {name} will find this new toplevel and we'll embed SYM-REFs
	 * to it etc..  All good for the future when we *run* the
	 * compiled code.
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
	if (idio_S_false == IDIO_MEANING_EENV_AOT (eenv)) {
	    IDIO fvi = IDIO_SI_VI (si);
	    idio_module_set_symbol_value (name,
					  idio_vm_default_values_ref (IDIO_FIXNUM_VAL (fvi)),
					  IDIO_MEANING_EENV_MODULE (eenv));
	}

	assign = IDIO_LIST4 (IDIO_I_SYM_SET, src, new_mci, m);
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
	return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_SYM_DEF, name, scope, idio_meaning_eenv_symbol_index (eenv, si)),
			   assign);
    } else {
	return assign;
    }
}

static IDIO idio_meaning_define (IDIO src, IDIO name, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_pair (name)) {
	/*
	 * (define (func arg) ...) =>
	 * (define func (function/name func (arg) ...))
	 *
	 * NB e is already a list
	 */
	e = idio_list_append2 (IDIO_LIST3 (idio_S_function_name,
					   IDIO_PAIR_H (name),
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

    /*
     * Careful!  When defining a value we don't want to recurse.
     */
    IDIO si = idio_meaning_variable_info (src, nametree, name, define_flags, eenv, 0);

    /*
     * if the act of looking the variable up auto-created it then
     * actually give it a value slot as this *is* the definition of it
     */

    IDIO scope = IDIO_SI_SCOPE (si);
    IDIO fmci = IDIO_SI_CI (si);
    IDIO fgvi = IDIO_SI_VI (si);
    if (idio_S_false == IDIO_MEANING_EENV_AOT (eenv) &&
	idio_S_toplevel == scope &&
	0 == IDIO_FIXNUM_VAL (fgvi)) {
	idio_meaning_extend_tables (eenv,
				    name,
				    scope,
				    fmci,
				    1,
				    idio_S_false,
				    idio_meaning_define_gvi0_string,
				    1);
    }

    return idio_meaning_assignment (src, name, e, nametree, define_flags, eenv);
}

static IDIO idio_meaning_define_template (IDIO src, IDIO name, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    /*
     * (define-template (name formal*) ...) =>
     * (define-template name (function/name name (formal*) ...))
     */
    if (idio_isa_pair (name)) {
	e = IDIO_LIST4 (idio_S_function_name,
			IDIO_PAIR_H (name),
			IDIO_PAIR_T (name),
			e);
	name = IDIO_PAIR_H (name);

	idio_meaning_copy_src_properties (src, e);
    }

    /*
     * create an expander: (function/name {name-expander} (x e) (apply proc (pt x)))
     *
     * where proc is (function/name name (formal*) ...) from above,
     * ie. e
     */
    IDIO x_sym = IDIO_SYMBOL ("x");
    IDIO e_sym = IDIO_SYMBOL ("e");

    IDIO pt_x = IDIO_LIST2 (idio_S_pt, x_sym);
    idio_meaning_copy_src_properties (src, pt_x);

    IDIO appl = IDIO_LIST3 (idio_S_apply, e, pt_x);
    idio_meaning_copy_src_properties (src, appl);

    IDIO bindings = IDIO_LIST2 (x_sym, e_sym);

    IDIO nsh  = idio_open_output_string_handle_C ();
    idio_display (name, nsh);
    idio_display_C ("-expander", nsh);

    IDIO dsh  = idio_open_output_string_handle_C ();
    idio_display_C ("define-template: ", dsh);
    idio_display (name, dsh);
    idio_display_C (" (x e)", dsh);

    IDIO docstr = idio_get_output_string (dsh);

    IDIO expander = IDIO_LIST5 (idio_S_function_name,
				idio_symbols_string_intern (idio_get_output_string (nsh)),
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
	    return idio_meaning_assignment (src,
					    name,
					    IDIO_PAIR_H (exp),
					    nametree,
					    IDIO_MEANING_TOPLEVEL_SCOPE (flags), eenv);
	}
    }

    /*
     * XXX define-template bootstrap
     *
     * Option 1 (when I was dealing with file contents "all in one"):
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
     * Option 2 (noted at the time but not implemented):
     *
     * As an alternative we could evaluate the source to the expander
     * now and install that code in *expander-list* directly -- but
     * watch out for embedded calls to regular functions defined in
     * the code (see comment above).
     *
     * In fact, because the evaluator changed to
     * expression-by-expression we effectively implement Option 2
     * (which handles the regular function calls as well).
     *
     * Requirement either way:
     *
     * As a further twist, we really need to embed a call to
     * idio_install_expander in the *object* code too!  When someone
     * in the future loads the object file containing this
     * define-template who will have called idio_install_expander?
     *
     * In summary: we need the expander in the here and now as someone
     * might use it in the next line of source and we need to embed a
     * call to idio_install_expander in the object code for future
     * users.
     */

    /*
     * this is a define at the toplevel and not tailp
     */
    int mflags = IDIO_MEANING_NOT_TAILP (IDIO_MEANING_TEMPLATE (IDIO_MEANING_DEFINE (IDIO_MEANING_TOPLEVEL_SCOPE (flags))));
    IDIO m_a = idio_meaning_assignment (expander, name, expander, nametree, mflags, eenv);

    idio_install_expander_source (name, expander, expander);

    IDIO si = idio_meaning_find_toplevel_symbol (name, eenv);

    /*
     * Generate a ci/vi for name.
     *
     * The ci is required so that the EXPANDER opcode can call
     * idio_install_expander() to overwrite the proc from the
     * idio_install_expander_source() call we just made
     */
    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, name);
    IDIO fmci = idio_fixnum (mci);

    IDIO ex_id = fmci;

    if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	ex_id = IDIO_SI_SI (si);
    }

    IDIO cm = idio_thread_current_module ();

    IDIO sym_si = IDIO_LIST6 (idio_S_toplevel,
			      ex_id,
			      fmci,
			      idio_fixnum (0),
			      cm,
			      docstr);

    idio_module_set_symbol (name, sym_si, cm);

    return IDIO_LIST3 (IDIO_I_EXPANDER, ex_id, m_a);
}

static IDIO idio_meaning_define_infix_operator (IDIO src, IDIO name, IDIO pri, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

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
    IDIO si = idio_meaning_find_toplevel_symbol (name, eenv);

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, name);
    IDIO fmci = idio_fixnum (mci);

    IDIO op_id = fmci;

    if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	op_id = IDIO_SI_SI (si);
    }

    IDIO sym_si = IDIO_LIST6 (idio_S_toplevel,
			      op_id,
			      fmci,
			      idio_fixnum (0),
			      idio_operator_module,
			      idio_meaning_define_infix_operator_string);

    idio_module_set_symbol (name, sym_si, idio_operator_module);

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

	IDIO find_module = IDIO_LIST2 (IDIO_SYMBOL ("find-module"),
				       IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module)));
	idio_meaning_copy_src_properties (src, find_module);

	IDIO sve = IDIO_LIST3 (IDIO_SYMBOL ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       find_module);
	idio_meaning_copy_src_properties (src, sve);

	idio_copy_infix_operator (IDIO_THREAD_XI (idio_thread_current_thread ()), name, pri, e);
	m = idio_meaning (sve, sve, nametree, flags, eenv);
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

	IDIO fe = IDIO_LIST5 (idio_S_function_name,
			      name,
			      def_args,
			      idio_meaning_define_infix_operator_string,
			      e);

	idio_meaning_copy_src_properties (src, fe);

	m = idio_meaning (fe, fe, nametree, flags, eenv);
    }
    IDIO r = IDIO_LIST4 (IDIO_I_INFIX_OPERATOR, op_id, pri, m);

    return r;
}

static IDIO idio_meaning_define_postfix_operator (IDIO src, IDIO name, IDIO pri, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (pri);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (fixnum, pri);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

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
    IDIO si = idio_meaning_find_toplevel_symbol (name, eenv);

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, name);
    IDIO fmci = idio_fixnum (mci);

    IDIO op_id = fmci;

    if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	op_id = IDIO_SI_SI (si);
    }
    IDIO sym_si = IDIO_LIST6 (idio_S_toplevel,
			      op_id,
			      fmci,
			      idio_fixnum (0),
			      idio_operator_module,
			      idio_meaning_define_postfix_operator_string);

    idio_module_set_symbol (name, sym_si, idio_operator_module);

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

	IDIO find_module = IDIO_LIST2 (IDIO_SYMBOL ("find-module"),
				       IDIO_LIST2 (idio_S_quote, IDIO_MODULE_NAME (idio_operator_module)));
	idio_meaning_copy_src_properties (src, find_module);

	IDIO sve = IDIO_LIST3 (IDIO_SYMBOL ("symbol-value"),
			       IDIO_LIST2 (idio_S_quote, e),
			       find_module);
	idio_meaning_copy_src_properties (src, sve);

	idio_copy_postfix_operator (IDIO_THREAD_XI (idio_thread_current_thread ()), name, pri, e);
	m = idio_meaning (sve, sve, nametree, flags, eenv);
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

	IDIO fe = IDIO_LIST5 (idio_S_function_name,
			      name,
			      def_args,
			      idio_meaning_define_postfix_operator_string,
			      e);

	idio_meaning_copy_src_properties (src, fe);

	m = idio_meaning (fe, fe, nametree, flags, eenv);
    }
    IDIO r = IDIO_LIST4 (IDIO_I_POSTFIX_OPERATOR, op_id, pri, m);

    return r;
}

static IDIO idio_meaning_define_dynamic (IDIO src, IDIO name, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
	idio_meaning_copy_src_properties (src, e);
    }

    return idio_meaning_assignment (src, name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_DYNAMIC_SCOPE (flags)), eenv);
}

static IDIO idio_meaning_define_environ (IDIO src, IDIO name, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_pair (e)) {
	e = IDIO_PAIR_H (e);
	idio_meaning_copy_src_properties (src, e);
    }

    return idio_meaning_assignment (src, name, e, nametree, IDIO_MEANING_DEFINE (IDIO_MEANING_ENVIRON_SCOPE (flags)), eenv);
}

/*
 * A computed variable's value should be a pair, (getter & setter).
 * Either of getter or setter can be #n.
 *
 * We shouldn't have both #n as it wouldn't be much use.
 */
static IDIO idio_meaning_define_computed (IDIO src, IDIO name, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

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
				    IDIO_MEANING_DEFINE (IDIO_MEANING_COMPUTED_SCOPE (flags)),
				    eenv);
}

static IDIO idio_meaning_sequence (IDIO src, IDIO ep, IDIO nametree, int flags, IDIO keyword, IDIO eenv);

static IDIO idio_meanings_single_sequence (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning (IDIO_MPP (e, src), e, nametree, flags, eenv);
}

static IDIO idio_meanings_multiple_sequence (IDIO src, IDIO e, IDIO ep, IDIO nametree, int flags, IDIO keyword, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);
    IDIO mp = idio_meaning_sequence (src, ep, nametree, flags, keyword, eenv);

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
	idio_coding_error_C ("unexpected sequence keyword", keyword, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_sequence (IDIO src, IDIO ep, IDIO nametree, int flags, IDIO keyword, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_pair (ep)) {
	IDIO eph = IDIO_PAIR_H (ep);
	IDIO ept = IDIO_PAIR_T (ep);

	/*
	 * We're going to deviate here from LiSP which differentiates
	 * between multiple and single expression sequences.
	 *
	 * We choose not to do that because we want AND/OR to
	 * suppress-rcse thus handling external-command logic.
	 */

	if (1 || idio_isa_pair (ept)) {
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
		idio_coding_error_C ("unexpected sequence keyword", keyword, IDIO_C_FUNC_LOCATION ());

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

		IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, seq_flags, eenv);
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

	    IDIO tailp = idio_S_false;
	    if (IDIO_MEANING_IS_TAILP (flags)) {
		tailp = idio_S_true;
	    }

	    r = idio_list_append2 (IDIO_LIST1 (tailp), r);
	    return idio_pair (c, r);
	} else {
	    return idio_meanings_single_sequence (IDIO_MPP (eph, src), eph, nametree, flags, eenv);
	}
    } else {
	idio_meaning_evaluation_error (keyword, IDIO_C_FUNC_LOCATION (), "sequence: not a pair", src);

	return idio_S_notreached;
    }
}

static IDIO idio_meaning_escape_block (IDIO src, IDIO label, IDIO be, IDIO nametree, int flags, IDIO eenv);

static IDIO idio_meaning_fix_abstraction (IDIO src, IDIO name, IDIO ns, IDIO formals, IDIO docstr, IDIO ep, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (formals);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    size_t arity = idio_list_length (ns);

    IDIO ent = idio_meaning_nametree_extend_params (nametree, ns);

    IDIO mp = idio_meaning_sequence (src, ep, ent, IDIO_MEANING_SET_TAILP (flags), idio_S_begin, eenv);

    /*
     * CREATE-FUNCTION is going to consume a vi which we create here
     * so that subsequent references line up
     *
     * Do not give this entry a name as it is not for this symbol but
     * a side-effect of CREATE-IFUNCTION.
     */
    idio_as_t vi = idio_meaning_extend_tables (eenv,
					       idio_S_false,
					       idio_S_toplevel,
					       idio_S_false,
					       0,
					       idio_S_false,
					       idio_meaning_fix_abstraction_string,
					       0);

    return IDIO_LIST8 (IDIO_I_FIX_CLOSURE,
		       mp,
		       name,
		       idio_fixnum (arity),
		       idio_meaning_nametree_to_list (ent),
		       docstr,
		       src,
		       idio_fixnum (vi));
}

static IDIO idio_meaning_dotted_abstraction (IDIO src, IDIO name, IDIO ns, IDIO n, IDIO formals, IDIO docstr, IDIO ep, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (formals);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    size_t arity = idio_list_length (ns);
    IDIO fix_formals = idio_list_append2 (ns, IDIO_LIST1 (n));

    IDIO ent = idio_meaning_nametree_extend_vparams (nametree, fix_formals);

    IDIO mp = idio_meaning_sequence (src, ep, ent, IDIO_MEANING_SET_TAILP (flags), idio_S_begin, eenv);

    /*
     * CREATE-FUNCTION is going to consume a vi which we create here
     * so that subsequent references line up
     *
     * Do not give this entry a name as it is not for this symbol but
     * a side-effect of CREATE-IFUNCTION.
     */
    idio_as_t vi = idio_meaning_extend_tables (eenv,
					       idio_S_false,
					       idio_S_toplevel,
					       idio_S_false,
					       0,
					       idio_S_false,
					       idio_meaning_dotted_abstraction_string,
					       0);

    return IDIO_LIST8 (IDIO_I_NARY_CLOSURE,
		       mp,
		       name,
		       idio_fixnum (arity),
		       idio_meaning_nametree_to_list (ent),
		       docstr,
		       src,
		       idio_fixnum (vi));
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

static IDIO idio_meaning_rewrite_assign_anon_function (IDIO src, IDIO e)
{
    if (idio_isa_pair (IDIO_PAIR_T (e)) &&
	idio_isa_pair (IDIO_PAIR_HT (e)) &&
	idio_S_function == IDIO_PAIR_HHT (e)) {
	/*
	 * (name (function ...))
	 *
	 *   to
	 *
	 * (name (function/name name ...))
	 */
	e = IDIO_LIST2 (IDIO_PAIR_H (e),
			   idio_list_append2 (IDIO_LIST3 (idio_S_function_name,
							  IDIO_PAIR_H (e),
							  IDIO_PAIR_H (IDIO_PAIR_THT (e))),
					      IDIO_PAIR_T (IDIO_PAIR_THT (e))));
	idio_meaning_copy_src_properties (src, e);
    }

    return e;
}

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
	    IDIO binding = idio_meaning_rewrite_assign_anon_function (src, IDIO_PAIR_T (cur));

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
	    idio_meaning_copy_src_properties (cur, body);

	    if (idio_S_nil == body) {
		/*
		 * What if {value-expr} has side-effects?
		 */
		idio_meaning_warning ("rewrite-body", "empty body for let/function+", cur);

		return idio_list_nreverse (r);
	    }

	    IDIO body_sequence = idio_list_append2 (IDIO_LIST1 (idio_S_begin), body);
	    idio_meaning_copy_src_properties (cur, body_sequence);

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
	    return idio_list_nreverse (r);
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_star == IDIO_PAIR_H (cur)) {
	    /* :* -> environ-let */

	    /*
	     * See commentary in := above
	     */
	    IDIO body = idio_meaning_rewrite_body (IDIO_MPP (cur, src), IDIO_PAIR_T (l), nametree);
	    if (idio_S_nil != r) {
		r = idio_list_nreverse (r);
	    }

	    if (idio_S_nil == body) {
		idio_meaning_warning ("rewrite-body", "empty body for environ-let", cur);
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
		r = idio_list_nreverse (r);
	    }

	    if (idio_S_nil == body) {
		idio_meaning_warning ("rewrite-body", "empty body for environ-unset", cur);
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
		r = idio_list_nreverse (r);
	    }

	    if (idio_S_nil == body) {
		idio_meaning_warning ("rewrite-body", "empty body for dynamic-let", cur);
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
		r = idio_list_nreverse (r);
	    }

	    if (idio_S_nil == body) {
		idio_meaning_warning ("rewrite-body", "empty body for dynamic-unset", cur);
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

    return idio_list_nreverse (r);
}

/*
 * In idio_meaning_rewrite_body_letrec() we're looking to accumulate a
 * sequence of :+ (ie. letrec) statements into {defs} and when we find
 * the first "anything else" statement we'll unbundle {defs} into a
 * {letrec} with "anything else" and subsequent expressions as the
 * body of the {letrec}.
 *
 * Each element of {defs} is the (name value-expr) tuple we would
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
		idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "empty body after definition", src);

		return idio_S_notreached;
	    } else {
		idio_meaning_evaluation_error (src, IDIO_C_FUNC_LOCATION (), "empty body after definition", src);

		return idio_S_notreached;
	    }

	    return idio_S_notreached;
	} else if (idio_isa_pair (l) &&
		   idio_isa_pair (IDIO_PAIR_H (l)) &&
		   idio_S_false != idio_expanderp (IDIO_PAIR_HH (l))) {
	    cur = idio_template_expands (IDIO_PAIR_H (l));
	    idio_meaning_copy_src_properties (src, cur);
	} else {
	    cur = IDIO_PAIR_H (l);
	}
	idio_meaning_copy_src_properties (IDIO_MPP (IDIO_PAIR_H (l), src), cur);

	if (idio_isa_pair (cur) &&
	    idio_S_begin == IDIO_PAIR_H (cur)) {
	    /*  redundant begin */
	    l = idio_list_append2 (IDIO_PAIR_T (cur), IDIO_PAIR_T (l));
	    idio_meaning_copy_src_properties (cur, l);
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

		IDIO fn = idio_S_nil;
		/*
		 * Create a docstr if there isn't one already
		 */
		if (0 == idio_isa_string (IDIO_PAIR_HTT (cur)) ||
		    0 == idio_isa_pair (IDIO_PAIR_TTT (cur))) {
		    IDIO dsh  = idio_open_output_string_handle_C ();
		    idio_display_C ("rewrite body letrec: define ", dsh);
		    idio_display (bindings, dsh);
		    fn = idio_list_append2 (IDIO_LIST4 (idio_S_function_name,
							IDIO_PAIR_H (bindings),
							IDIO_PAIR_T (bindings),
							idio_get_output_string (dsh)),
					    IDIO_PAIR_TT (cur));
		} else {
		    fn = idio_list_append2 (IDIO_LIST3 (idio_S_function_name,
							IDIO_PAIR_H (bindings),
							IDIO_PAIR_T (bindings)),
					    IDIO_PAIR_TT (cur));
		}
		idio_meaning_copy_src_properties (IDIO_MPP (cur, src), fn);
		form = IDIO_LIST2 (IDIO_PAIR_H (bindings), fn);
	    } else {
		/*
		 * (:+ name value-expr)
		 *
		 * (name value-expr)
		 */
		form = idio_meaning_rewrite_assign_anon_function (src, IDIO_PAIR_T (cur));
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
		IDIO ns = idio_list_ph_of (defs);
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

static IDIO idio_meaning_abstraction (IDIO src, IDIO name, IDIO nns, IDIO docstr, IDIO ep, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nns);
    IDIO_ASSERT (docstr);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    ep = idio_meaning_rewrite_body (IDIO_MPP (ep, src), ep, nametree);
    idio_meaning_copy_src_properties (src, ep);

    IDIO ns = nns;
    IDIO regular = idio_S_nil;

    for (;;) {
	if (idio_isa_pair (ns))  {
	    regular = idio_pair (IDIO_PAIR_H (ns), regular);
	    ns = IDIO_PAIR_T (ns);
	} else if (idio_S_nil == ns) {
	    return idio_meaning_fix_abstraction (src, name, nns, nns, docstr, ep, nametree, flags, eenv);
	} else {
	    return idio_meaning_dotted_abstraction (src, name, idio_list_nreverse (regular), ns, nns, docstr, ep, nametree, flags, eenv);
	}
    }

    /*
     * Shouldn't get here...
     */
    idio_coding_error_C ("meaning-abstraction", IDIO_LIST2 (nns, ep), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

static IDIO idio_meaning_block (IDIO src, IDIO es, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    es = idio_meaning_rewrite_body (IDIO_MPP (es, src), es, nametree);
    idio_meaning_copy_src_properties (src, es);

    return idio_meaning_sequence (IDIO_MPP (es, src), es, nametree, flags, idio_S_begin, eenv);
}

static IDIO idio_meaning_arguments (IDIO src, IDIO aes, IDIO nametree, size_t const arity, int flags, IDIO eenv);

static IDIO idio_meaning_some_arguments (IDIO src, IDIO ae, IDIO aes, IDIO nametree, size_t const arity, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ae);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO am = idio_meaning (IDIO_MPP (ae, src), ae, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);
    IDIO ams = idio_meaning_arguments (src, aes, nametree, arity, flags, eenv);
    size_t rank = arity - (idio_list_length (aes) + 1);

    return IDIO_LIST4 (IDIO_I_STORE_ARGUMENT, am, ams, idio_fixnum (rank));
}

static IDIO idio_meaning_no_argument (IDIO src, IDIO nametree, size_t const arity, int flags, IDIO cs)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nametree);

    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (IDIO_I_ALLOCATE_FRAME, idio_fixnum (arity));
}

static IDIO idio_meaning_arguments (IDIO src, IDIO aes, IDIO nametree, size_t const arity, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_pair (aes)) {
	return idio_meaning_some_arguments (src, IDIO_PAIR_H (aes), IDIO_PAIR_T (aes), nametree, arity, flags, eenv);
    } else {
	return idio_meaning_no_argument (src, nametree, arity, flags, eenv);
    }
}

static IDIO idio_meaning_fix_closed_application (IDIO src, IDIO ns, IDIO body, IDIO aes, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (body);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    body = idio_meaning_rewrite_body (IDIO_MPP (body, src), body, nametree);
    idio_meaning_copy_src_properties (src, body);

    IDIO ams = idio_meaning_arguments (src, aes, nametree, idio_list_length (aes), IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO ent = idio_meaning_nametree_extend_params (nametree, ns);
    IDIO mbody = idio_meaning_sequence (IDIO_MPP (body, src), body, ent, flags, idio_S_begin, eenv);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST4 (IDIO_I_TR_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    } else {
	return IDIO_LIST4 (IDIO_I_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    }
}

static IDIO idio_meaning_dotted_arguments (IDIO src, IDIO aes, IDIO nametree, size_t const size, size_t arity, int flags, IDIO eenv);

static IDIO idio_meaning_some_dotted_arguments (IDIO src, IDIO ae, IDIO aes, IDIO nametree, size_t const nargs, size_t arity, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ae);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO am = idio_meaning (IDIO_MPP (ae, src), ae, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);
    IDIO ams = idio_meaning_dotted_arguments (src, aes, nametree, nargs, arity, flags, eenv);
    size_t rank = nargs - (idio_list_length (aes) + 1);

    if (rank < arity) {
	return IDIO_LIST4 (IDIO_I_STORE_ARGUMENT, am, ams, idio_fixnum (rank));
    } else {
	return IDIO_LIST4 (IDIO_I_LIST_ARGUMENT, am, ams, idio_fixnum (arity));
    }
}

static IDIO idio_meaning_no_dotted_argument (IDIO src, IDIO nametree, size_t const nargs, size_t arity, int flags, IDIO cs)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (nametree);

    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (IDIO_I_ALLOCATE_FRAME, idio_fixnum (arity));
}

static IDIO idio_meaning_dotted_arguments (IDIO src, IDIO aes, IDIO nametree, size_t const nargs, size_t arity, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_pair (aes)) {
	return idio_meaning_some_dotted_arguments (src, IDIO_PAIR_H (aes), IDIO_PAIR_T (aes), nametree, nargs, arity, flags, eenv);
    } else {
	return idio_meaning_no_dotted_argument (src, nametree, nargs, arity, flags, eenv);
    }
}

static IDIO idio_meaning_dotted_closed_application (IDIO src, IDIO ns, IDIO n, IDIO body, IDIO aes, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (body);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    body = idio_meaning_rewrite_body (IDIO_MPP (body, src), body, nametree);
    idio_meaning_copy_src_properties (src, body);

    IDIO ams = idio_meaning_dotted_arguments (src, aes, nametree, idio_list_length (aes), idio_list_length (ns), IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO fix_formals = idio_list_append2 (ns, IDIO_LIST1 (n));

    IDIO ent = idio_meaning_nametree_extend_vparams (nametree, fix_formals);
    IDIO mbody = idio_meaning_sequence (IDIO_MPP (body, src), body, ent, flags, idio_S_begin, eenv);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST4 (IDIO_I_TR_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    } else {
	return IDIO_LIST4 (IDIO_I_FIX_LET, ams, mbody, idio_meaning_nametree_to_list (ent));
    }
}

static IDIO idio_meaning_closed_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

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
		return idio_meaning_fix_closed_application (fe, fns, IDIO_PAIR_T (fet), aes, nametree, flags, eenv);
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
	    return idio_meaning_dotted_closed_application (fe, idio_list_nreverse (fixed_args), ns, IDIO_PAIR_T (fet), aes, nametree, flags, eenv);
	}
    }

    /*
     * Can we get here?
     */

    idio_coding_error_C ("meaning-closed-application", IDIO_LIST2 (fe, aes), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

static IDIO idio_meaning_local_application (IDIO src, IDIO n, IDIO ae, IDIO body, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (n);
    IDIO_ASSERT (ae);
    IDIO_ASSERT (body);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    body = idio_meaning_rewrite_body (IDIO_MPP (body, src), body, nametree);
    idio_meaning_copy_src_properties (src, body);

    IDIO am = idio_meaning (IDIO_MPP (ae, src), ae, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO ent = idio_meaning_nametree_extend_locals (nametree, IDIO_LIST1 (n));
    IDIO mbody = idio_meaning_sequence (IDIO_MPP (body, src), body, ent, flags, idio_S_begin, eenv);

    IDIO ll = idio_meaning_lexical_lookup (src, ent, n);

    return IDIO_LIST6 (IDIO_I_LOCAL, src, IDIO_PAIR_HTT (ll), am, mbody, idio_meaning_nametree_to_list (ent));
}

static IDIO idio_meaning_regular_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, int flags, IDIO eenv);

static IDIO idio_meaning_primitive_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, int flags, size_t const arity, IDIO gvi, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (gvi);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, fe);
    IDIO_TYPE_ASSERT (list, aes);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (fixnum, gvi);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

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
    IDIO primdata = idio_vm_default_values_ref (IDIO_FIXNUM_VAL (gvi));

    if (IDIO_PRIMITIVE_VARARGS (primdata)) {
	/*
	 * only a full function call protocol can cope with varargs!
	 */
	return idio_meaning_regular_application (src, fe, aes, nametree, flags, eenv);
    } else {
	char *name = IDIO_PRIMITIVE_NAME (primdata);

	switch (arity) {
	case 0:
	    {
		return IDIO_LIST4 (IDIO_I_PRIMCALL0, src, idio_fixnum (IDIO_A_PRIMCALL0), gvi);
	    }
	    break;
	case 1:
	    {
		IDIO m1 = idio_meaning (IDIO_MPP (IDIO_PAIR_H (aes), src),
					IDIO_PAIR_H (aes),
					nametree,
					IDIO_MEANING_NOT_TAILP (flags),
					eenv);

		return IDIO_LIST5 (IDIO_I_PRIMCALL1, src, idio_fixnum (IDIO_A_PRIMCALL1), m1, gvi);
	    }
	    break;
	case 2:
	    {
		IDIO m1 = idio_meaning (IDIO_MPP (IDIO_PAIR_H (aes), src),
					IDIO_PAIR_H (aes),
					nametree,
					IDIO_MEANING_NOT_TAILP (flags),
					eenv);
		IDIO m2 = idio_meaning (IDIO_MPP (IDIO_PAIR_HT (aes), src),
					IDIO_PAIR_HT (aes),
					nametree,
					IDIO_MEANING_NOT_TAILP (flags),
					eenv);

#define IDIO_EVALUATE_RUN_IN_THREAD	"run-in-thread"
		size_t rit_len = sizeof (IDIO_EVALUATE_RUN_IN_THREAD) - 1;

		if (rit_len == IDIO_PRIMITIVE_NAME_LEN (primdata) &&
		    strncmp (name, IDIO_EVALUATE_RUN_IN_THREAD, rit_len) == 0) {
		    break;
		} else {
		    return IDIO_LIST6 (IDIO_I_PRIMCALL2, src, idio_fixnum (IDIO_A_PRIMCALL2), m1, m2, gvi);
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
    return idio_meaning_regular_application (src, fe, aes, nametree, flags, eenv);
}

static IDIO idio_meaning_regular_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO fm;
    if (idio_isa_symbol (fe)) {
	fm = idio_meaning_function_reference (src, fe, nametree, flags, eenv);
    } else {
	fm = idio_meaning (fe, fe, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);
    }

    int ams_flags = IDIO_MEANING_NOT_TAILP (flags);

    if (idio_isa_pair (aes) &&
	idio_S_nil != IDIO_PAIR_T (aes) &&
	! idio_isa_pair (IDIO_PAIR_T (aes))) {
	idio_debug ("mra ? %s\n", aes);
    }
    IDIO ams = idio_meaning_arguments (src, aes, nametree, idio_list_length (aes), ams_flags, eenv);

    if (IDIO_MEANING_IS_TAILP (flags)) {
	return IDIO_LIST4 (IDIO_I_TR_REGULAR_CALL, src, fm, ams);
    } else {
	return IDIO_LIST4 (IDIO_I_REGULAR_CALL, src, fm, ams);
    }
}

static IDIO idio_meaning_application (IDIO src, IDIO fe, IDIO aes, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (fe);
    IDIO_ASSERT (aes);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_symbol (fe)) {
	IDIO si = idio_meaning_variable_info (src, nametree, fe, IDIO_MEANING_TOPLEVEL_SCOPE (flags), eenv, 1);

	if (idio_isa_pair (si)) {
	    IDIO scope = IDIO_SI_SCOPE (si);

	    if (idio_S_predef == scope) {
		IDIO fvi = IDIO_SI_VI (si);
		IDIO p = idio_vm_default_values_ref (IDIO_FIXNUM_VAL (fvi));

		if (idio_S_unspec != p) {
		    if (idio_isa_primitive (p)) {
			size_t arity = IDIO_PRIMITIVE_ARITY (p);
			size_t nargs = idio_list_length (aes);

			if ((IDIO_PRIMITIVE_VARARGS (p) &&
			     nargs >= arity) ||
			    arity == nargs) {
			    return idio_meaning_primitive_application (src, fe, aes, nametree, flags, arity, fvi, eenv);
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
			idio_debug ("ivvr %s\n", idio_vm_default_values_ref (IDIO_FIXNUM_VAL (IDIO_SI_VI (si))));
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

	return idio_meaning_closed_application (src, fe, aes, nametree, flags, eenv);
    } else {
	return idio_meaning_regular_application (src, fe, aes, nametree, flags, eenv);
    }

    /*
     * Can we get here?
     */

    idio_coding_error_C ("meaning-application", IDIO_LIST2 (fe, aes), IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

static IDIO idio_meaning_dynamic_reference (IDIO src, IDIO name, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_reference (src, name, nametree, IDIO_MEANING_DYNAMIC_SCOPE (flags), eenv);
}

static IDIO idio_meaning_dynamic_let (IDIO src, IDIO name, IDIO e, IDIO ep, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO sym_idx;
    IDIO si = idio_meaning_find_symbol_recurse (name, eenv, idio_S_dynamic, 1);

    if (idio_isa_pair (si)) {
	sym_idx = idio_meaning_eenv_symbol_index (eenv, si);
	if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	    fprintf (stderr, "WARNING: dynamic-extend: likely si clash\n");
	}
    } else {
	/*
	 * Get a new toplevel for this dynamic variable
	 */
	sym_idx = idio_toplevel_extend (src, name, IDIO_MEANING_DYNAMIC_SCOPE (flags), eenv);
    }

    /*
     * Tell the tree of "locals" about this dynamic variable and find
     * the meaning of ep with the new tree
     */
    IDIO ent = idio_meaning_nametree_dynamic_extend (nametree, name, sym_idx, idio_S_dynamic);

    IDIO mp = idio_meaning_sequence (src, ep, ent, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, eenv);

    IDIO dynamic_wrap = IDIO_LIST3 (IDIO_LIST3 (IDIO_I_PUSH_DYNAMIC, sym_idx, m),
				    mp,
				    IDIO_LIST1 (IDIO_I_POP_DYNAMIC));

    return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_SYM_DEF, name, idio_S_dynamic, sym_idx),
		       dynamic_wrap);
}

static IDIO idio_meaning_dynamic_unset (IDIO src, IDIO name, IDIO ep, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_dynamic_let (src, name, idio_S_undef, ep, nametree, flags, eenv);
}

static IDIO idio_meaning_environ_reference (IDIO src, IDIO name, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_reference (src, name, nametree, IDIO_MEANING_ENVIRON_SCOPE (flags), eenv);
}

static IDIO idio_meaning_environ_let (IDIO src, IDIO name, IDIO e, IDIO ep, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO m = idio_meaning (IDIO_MPP (e, src), e, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO sym_idx;
    IDIO si = idio_meaning_find_symbol_recurse (name, eenv, idio_S_environ, 1);

    if (idio_isa_pair (si)) {
	sym_idx = idio_meaning_eenv_symbol_index (eenv, si);
	if (idio_S_true == IDIO_MEANING_EENV_AOT (eenv)) {
	    fprintf (stderr, "WARNING: environ-extend: likely si clash\n");
	}
    } else {
	/*
	 * Get a new toplevel for this environ variable
	 */
	sym_idx = idio_toplevel_extend (src, name, IDIO_MEANING_ENVIRON_SCOPE (flags), eenv);
    }

    /*
     * Tell the tree of "locals" about this environ variable and find
     * the meaning of ep with the new tree
     */
    IDIO ent = idio_meaning_nametree_dynamic_extend (nametree, name, sym_idx, idio_S_environ);

    IDIO mp = idio_meaning_sequence (src, ep, ent, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, eenv);

    IDIO environ_wrap = IDIO_LIST3 (IDIO_LIST3 (IDIO_I_PUSH_ENVIRON, sym_idx, m),
				    mp,
				    IDIO_LIST1 (IDIO_I_POP_ENVIRON));

    return IDIO_LIST2 (IDIO_LIST4 (IDIO_I_SYM_DEF, name, idio_S_environ, sym_idx),
		       environ_wrap);
}

static IDIO idio_meaning_environ_unset (IDIO src, IDIO name, IDIO ep, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_environ_let (src, name, idio_S_undef, ep, nametree, flags, eenv);
}

static IDIO idio_meaning_computed_reference (IDIO src, IDIO name, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    return idio_meaning_reference (src, name, nametree, IDIO_MEANING_COMPUTED_SCOPE (flags), eenv);
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
static IDIO idio_meaning_trap (IDIO src, IDIO ce, IDIO he, IDIO be, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (ce);
    IDIO_ASSERT (he);
    IDIO_ASSERT (be);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    /*
     * We need the meaning of handler now as it'll be used by all the
     * traps.
     */
    he = idio_meaning_rewrite_body (IDIO_MPP (he, src), he, nametree);
    idio_meaning_copy_src_properties (src, he);

    IDIO hm = idio_meaning (IDIO_MPP (he, src), he, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);

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
	IDIO si = idio_meaning_find_symbol_recurse (cname, eenv, idio_S_toplevel, 1);

	if (idio_isa_pair (si)) {
	    fmci = IDIO_SI_CI (si);
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

    IDIO bm = idio_meaning_sequence (IDIO_MPP (be, src), be, nametree, IDIO_MEANING_NOT_TAILP (flags), idio_S_begin, eenv);

    IDIO r = idio_list_append2 (IDIO_LIST1 (hm), pushs);
    r = idio_list_append2 (r, IDIO_LIST1 (bm));
    r = idio_list_append2 (r, pops);

    return r;
}

static IDIO idio_meaning_escape_block (IDIO src, IDIO label, IDIO be, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (label);
    IDIO_ASSERT (be);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, label);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, label);
    IDIO fmci = idio_fixnum (mci);

    be = idio_meaning_rewrite_body (IDIO_MPP (be, src), be, nametree);
    idio_meaning_copy_src_properties (src, be);

    IDIO escapes_eenv = idio_struct_instance_copy (eenv);
    idio_struct_instance_set_direct (escapes_eenv,
				     IDIO_EENV_ST_ESCAPES,
				     idio_pair (label,
						idio_struct_instance_ref_direct (eenv, IDIO_EENV_ST_ESCAPES)));

    idio_gc_protect (escapes_eenv);

    IDIO bm = idio_meaning_sequence (IDIO_MPP (be, src),
				     be,
				     nametree,
				     IDIO_MEANING_NOT_TAILP (flags),
				     idio_S_begin,
				     escapes_eenv);

    idio_gc_expose (escapes_eenv);

    IDIO r = IDIO_LIST2 (IDIO_LIST3 (IDIO_I_PUSH_ESCAPER, fmci, bm),
			 IDIO_LIST1 (IDIO_I_POP_ESCAPER));

    return r;
}

static IDIO idio_meaning_escape_from (IDIO src, IDIO label, IDIO ve, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (label);
    IDIO_ASSERT (ve);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, label);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO esc = idio_list_memq (label, IDIO_MEANING_EENV_ESCAPES (eenv));

    if (idio_S_false == esc) {
	/*
	 * Test Case: evaluation-errors/escape-from-unbound.idio
	 *
	 * escape-from new-york #t
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), label);

	return idio_S_notreached;
    }

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, label);
    IDIO fmci = idio_fixnum (mci);

    IDIO vm = idio_meaning (IDIO_MPP (ve, src), ve, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO r = IDIO_LIST3 (IDIO_I_ESCAPER_LABEL_REF, fmci, vm);

    return r;
}

static IDIO idio_meaning_escape_label (IDIO src, IDIO label, IDIO ve, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (label);
    IDIO_ASSERT (ve);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (symbol, label);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO esc = idio_list_memq (label, IDIO_MEANING_EENV_ESCAPES (eenv));

    if (idio_S_false == esc) {
	/*
	 * Test Case: evaluation-errors/return-unbound.idio
	 *
	 * return #t
	 */
	idio_meaning_error_static_unbound (src, IDIO_C_FUNC_LOCATION (), label);

	return idio_S_notreached;
    }

    idio_as_t mci = idio_meaning_constants_lookup_or_extend (eenv, label);
    IDIO fmci = idio_fixnum (mci);

    if (idio_isa_pair (ve)) {
	ve = IDIO_PAIR_H (ve);
    } else {
	ve = idio_S_void;
    }

    IDIO vm = idio_meaning (IDIO_MPP (ve, src), ve, nametree, IDIO_MEANING_NOT_TAILP (flags), eenv);

    IDIO r = IDIO_LIST3 (IDIO_I_ESCAPER_LABEL_REF, fmci, vm);

    return r;
}

static IDIO idio_meaning_include (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO thr = idio_thread_current_thread ();
    idio_pc_t pc0 = IDIO_THREAD_PC (thr);

    idio_load_file_name (e, eenv);

    idio_pc_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }

    return IDIO_LIST1 (IDIO_I_NOP);
}

static IDIO idio_meaning_expander (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    IDIO te = idio_template_expand (e);
    idio_meaning_copy_src_properties (src, te);

    return idio_meaning (te, te, nametree, flags, eenv);
}

static IDIO idio_meaning (IDIO src, IDIO e, IDIO nametree, int flags, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (struct_instance, eenv);

    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	IDIO et = IDIO_PAIR_T (e);

	if (idio_S_begin == eh ||
	    idio_S_and == eh ||
	    idio_S_or == eh) {
	    if (idio_isa_pair (et)) {
		return idio_meaning_sequence (src, et, nametree, flags, eh, eenv);
	    } else if (idio_S_begin == eh) {
		return idio_meaning (src, idio_S_void, nametree, flags, eenv);
	    } else if (idio_S_and == eh) {
		return idio_meaning (src, idio_S_true, nametree, flags, eenv);
	    } else if (idio_S_or == eh) {
		return idio_meaning (src, idio_S_false, nametree, flags, eenv);
	    } else {
		/*
		 * Not sure we can get here as it requires developer
		 * coding error.
		 */
		idio_coding_error_C ("unexpected sequence keyword", eh, IDIO_C_FUNC_LOCATION_S ("begin-and-or"));

		return idio_S_notreached;
	    }
	} else if (idio_S_not == eh) {
	    /* (not x) */
	    if (idio_isa_pair (et)) {
		if (idio_S_nil != IDIO_PAIR_T (et)) {
		    /*
		     * Test Case: evaluation-errors/not-multiple-args.idio
		     *
		     * (not 1 2)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("not"), "too many arguments", et);

		    return idio_S_notreached;
		} else {
		    return idio_meaning_not (src, IDIO_PAIR_H (et), nametree, flags, eenv);
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/not-nil.idio
		 *
		 * (not)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("not"), "no argument", eh);

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
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("escape"), "too many arguments", et);

		    return idio_S_notreached;
		} else {
		    return idio_meaning_escape (src, IDIO_PAIR_H (et), nametree, flags, eenv);
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
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quote"), "too many arguments", et);

		    return idio_S_notreached;
		} else {
		    return idio_meaning_quotation (src, IDIO_PAIR_H (et), nametree, flags);
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
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("quasiquote"), "too many arguments", et);

		    return idio_S_notreached;
		} else {
		    return idio_meaning_quasiquotation (src, IDIO_PAIR_H (et), nametree, flags, eenv);
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
	    /*
	     * (function bindings [docstr] body ...)
	     *
	     *   becomes
	     *
	     * (function/name anon/nnn bindings [docstr] body ...)
	     */
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
			return idio_meaning_abstraction (src,
							 IDIO_GENSYM ("anon"),
							 IDIO_PAIR_H (et),
							 etth,
							 ettt,
							 nametree,
							 flags,
							 eenv);
		    } else {
			/*
			 * (function bindings body ...)
			 * (function bindings "...")
			 *
			 * The second is a function whose body is a
			 * string.
			 */
			return idio_meaning_abstraction (src,
							 IDIO_GENSYM ("anon"),
							 IDIO_PAIR_H (et),
							 idio_S_nil,
							 ett,
							 nametree,
							 flags,
							 eenv);
		    }
		} else {
		/*
		 * Test Case: evaluation-errors/function-no-body.idio
		 *
		 * (function bindings)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function"), "no body", eh);

		return idio_S_notreached;
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
	} else if (idio_S_function_name == eh) {
	    /* (function/name name bindings [docstr] body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO etth = IDIO_PAIR_H (ett);
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			IDIO ettth = IDIO_PAIR_H (ettt);
			IDIO etttt = IDIO_PAIR_T (ettt);
			if (idio_isa_string (ettth) &&
			    idio_S_nil != etttt) {
			    /*
			     * (function/name name bindings "docstr" body ...)
			     */
			    return idio_meaning_abstraction (src,
							     IDIO_PAIR_H (et),
							     etth,
							     ettth,
							     etttt,
							     nametree,
							     flags,
							     eenv);
			} else {
			    /*
			     * (function/name name bindings body ...)
			     * (function/name name bindings "...")
			     *
			     * The second is a function whose body is
			     * a string.
			     */
			    return idio_meaning_abstraction (src,
							     IDIO_PAIR_H (et),
							     etth,
							     idio_S_nil,
							     ettt,
							     nametree,
							     flags,
							     eenv);
			}
		    } else {
			/*
			 * Test Case: evaluation-errors/function-name-no-body.idio
			 *
			 * (function/name name bindings)
			 */
			idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function/name"), "no body", eh);

			return idio_S_notreached;
		    }
		} else {
		    /*
		     * Test Case: evaluation-errors/function-name-no-bindings.idio
		     *
		     * (function/name name)
		     */
		    idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function/name"), "no bindings", eh);

		    return idio_S_notreached;
		}
	    } else {
		/*
		 * Test Case: evaluation-errors/function-name-nil.idio
		 *
		 * (function/name)
		 */
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function/name"), "no arguments", eh);

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
			return idio_meaning_local_application (src,
							       IDIO_PAIR_H (et),
							       IDIO_PAIR_H (ett),
							       ettt,
							       nametree,
							       flags,
							       eenv);
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
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("function+"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_if == eh) {
	    /* (if condition consequent alternative) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    IDIO ettth = idio_S_void; /* default: (if #f e) -> #<void> */
		    if (idio_isa_pair (ettt)) {
			ettth = IDIO_PAIR_H (ettt);
		    }
		    return idio_meaning_alternative (src,
						     IDIO_PAIR_H (et),
						     IDIO_PAIR_H (ett),
						     ettth,
						     nametree,
						     flags,
						     eenv);
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

		IDIO c = idio_meaning (etc, etc, nametree, flags, eenv);

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
		    return idio_meaning_assignment (src,
						    IDIO_PAIR_H (et),
						    IDIO_PAIR_H (ett),
						    nametree,
						    IDIO_MEANING_TOPLEVEL_SCOPE (flags),
						    eenv);
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
		    return idio_meaning_define_template (src,
							 IDIO_PAIR_H (et),
							 IDIO_PAIR_H (ett),
							 nametree,
							 flags,
							 eenv);
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
			return idio_meaning_define_infix_operator (src,
								   IDIO_PAIR_H (et),
								   IDIO_PAIR_H (ett),
								   IDIO_PAIR_H (ettt),
								   nametree,
								   flags,
								   eenv);
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
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-infix-operator"), "no arguments", e);

		return idio_S_notreached;
	    }
	} else if (idio_S_define_postfix_operator == eh) {
	    /* (define-postfix-operator bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_define_postfix_operator (src,
								     IDIO_PAIR_H (et),
								     IDIO_PAIR_H (ett),
								     IDIO_PAIR_H (ettt),
								     nametree,
								     flags,
								     eenv);
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
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("define-postfix-operator"), "no arguments", e);

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
		    return idio_meaning_define (src, IDIO_PAIR_H (et), ett, nametree, flags, eenv);
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
		    return idio_meaning_define (src, IDIO_PAIR_H (et), ett, nametree, flags, eenv);
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
		    return idio_meaning_define_environ (src, IDIO_PAIR_H (et), ett, nametree, flags, eenv);
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
		    return idio_meaning_define_dynamic (src, IDIO_PAIR_H (et), ett, nametree, flags, eenv);
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
		    return idio_meaning_define_computed (src, IDIO_PAIR_H (et), ett, nametree, flags, eenv);
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
		return idio_meaning_block (src, et, nametree, flags, eenv);
	    } else {
		idio_meaning_warning ("idio_meaning", "empty body for block => void", src);
		return idio_meaning (src, idio_S_void, nametree, flags, eenv);
	    }
	} else if (idio_S_dynamic == eh) {
	    /* (dynamic var) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_dynamic_reference (src, IDIO_PAIR_H (et), nametree, flags, eenv);
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
			return idio_meaning_dynamic_let (src,
							 IDIO_PAIR_H (eth),
							 IDIO_PAIR_H (etht),
							 IDIO_PAIR_T (et),
							 nametree,
							 flags,
							 eenv);
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
		    return idio_meaning_dynamic_unset (src, eth, IDIO_PAIR_T (et), nametree, flags, eenv);
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
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("dynamic-unset"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_environ_let == eh) {
	    /* (environ-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_environ_let (src,
							 IDIO_PAIR_H (eth),
							 IDIO_PAIR_H (etht),
							 IDIO_PAIR_T (et),
							 nametree,
							 flags,
							 eenv);
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
		    return idio_meaning_environ_unset (src, eth, IDIO_PAIR_T (et), nametree, flags, eenv);
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
		idio_meaning_error_param (src, IDIO_C_FUNC_LOCATION_S ("environ-unset"), "no arguments", eh);

		return idio_S_notreached;
	    }
	} else if (idio_S_pct_trap == eh) {
	    /*
	     * (%trap condition       handler body ...)
	     * (%trap (condition ...) handler body ...)
	     *
	     * NB {trap}, itself, is a syntax transformer, partly to
	     * allow (trap ...) to be wrapped in a prompt-at giving us
	     * trap-return.  All of that means that we need a distinct
	     * symbol, %trap, to be the special form.
	     */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    if (idio_isa_pair (ettt)) {
			return idio_meaning_trap (src, IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettt, nametree, flags, eenv);
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
		IDIO r = idio_meaning_include (src, IDIO_PAIR_H (et), nametree, flags, eenv);
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
		IDIO si = idio_meaning_variable_info (src, nametree, eh, IDIO_MEANING_TOPLEVEL_SCOPE (flags), eenv, 1);

		if (idio_S_unspec != si) {
		    if (idio_S_false != idio_expanderp (eh)) {
			return idio_meaning_expander (src, e, nametree, flags, eenv);
		    }
		}
	    }

	    idio_meaning_copy_src_properties (src, e);
	    return idio_meaning_application (e, eh, et, nametree, flags, eenv);
	}
    } else {
	switch ((intptr_t) e & IDIO_TYPE_MASK) {
	case IDIO_TYPE_FIXNUM_MARK:
	case IDIO_TYPE_CONSTANT_MARK:
	    return idio_meaning_quotation (src, e, nametree, flags);
	case IDIO_TYPE_PLACEHOLDER_MARK:
	    /*
	     * Can'r get here without a developer coding issue.
	     */
	    idio_coding_error_C ("invalid constant type", e, IDIO_C_FUNC_LOCATION_S ("quotation/placeholder"));

	    return idio_S_notreached;
	case IDIO_TYPE_POINTER_MARK:
	    {
		switch (e->type) {
		case IDIO_TYPE_SYMBOL:
		    return idio_meaning_reference (src, e, nametree, IDIO_MEANING_TOPLEVEL_SCOPE (flags), eenv);
		case IDIO_TYPE_STRING:
		case IDIO_TYPE_KEYWORD:
		case IDIO_TYPE_ARRAY:
		case IDIO_TYPE_HASH:
		case IDIO_TYPE_BIGNUM:
		case IDIO_TYPE_BITSET:
		    return idio_meaning_quotation (src, e, nametree, flags);

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
		    return idio_meaning_quotation (src, e, nametree, flags);

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
    idio_coding_error_C ("meaning", e, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO idio_evaluate (IDIO src, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (eenv);

    IDIO_TYPE_ASSERT (struct_instance, eenv);

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

    /*
     * We must call this with the current module.  The previous
     * expression (when run) may well have changed the current module.
     */
    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_MODULE, idio_thread_current_module ());

    IDIO m = idio_meaning (src, src, idio_S_nil, IDIO_MEANING_FLAG_NONE, eenv);

    idio_gc_resume ("idio_evaluate");

    return IDIO_LIST2 (IDIO_LIST2 (IDIO_I_PUSH_ABORT, m),
		       IDIO_LIST1 (IDIO_I_POP_ABORT));
}

IDIO_DEFINE_PRIMITIVE1V_DS ("evaluate", evaluate, (IDIO src, IDIO args), "src [eenv]", "\
evaluate Idio source code `src` in the context of	\n\
`eenv` and return intermediate code for the		\n\
code generator						\n\
							\n\
:param src: Idio source code				\n\
:type src: Abstract Syntax Tree				\n\
:param eenv: evaluation environment			\n\
:type eenv: struct instance, optional			\n\
							\n\
.. seealso: :ref:`%evaluation-environment <evaluate/%evaluation-environment>`	\n\
")
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (args);

    IDIO eenv = idio_S_nil;

    if (idio_isa_pair (args)) {
	eenv = IDIO_PAIR_H (args);

	IDIO_USER_TYPE_ASSERT (struct_instance, eenv);
    }

    if (idio_S_nil == eenv) {
	eenv = idio_default_eenv;
    }

    IDIO r = idio_evaluate (src, eenv);

    return r;
}

/*
 * At some point we will switch from the C "evaluate" to the Idio
 * "evaluate" and this function, called from, say, idio_load_handle(),
 * figures out which.
 */
IDIO idio_evaluate_func (IDIO src, IDIO eenv)
{
    IDIO_ASSERT (src);
    IDIO_ASSERT (eenv);

    IDIO ev_func = idio_module_symbol_value (idio_evaluate_evaluate_sym,
					     idio_evaluate_module,
					     idio_S_nil);

    return idio_vm_invoke_C (IDIO_LIST3 (ev_func, src, eenv));
}

IDIO idio_evaluate_eenv (IDIO desc, IDIO aotp, IDIO module)
{
    IDIO_ASSERT (desc);
    IDIO_ASSERT (aotp);
    IDIO_ASSERT (module);

    IDIO_TYPE_ASSERT (string, desc);
    IDIO_TYPE_ASSERT (boolean, aotp);
    IDIO_TYPE_ASSERT (module, module);

    /*
     * Slightly annoyingly, idio_C_pointer_type(), used to wrap the
     * generic IDIO_IA_T byte code, will free() the embedded pointer
     * by default.  Which isn't great for the shared idio_all_code (or
     * anything else).
     */
    if (idio_S_true == aotp) {
	IDIO_IA_T byte_code = idio_ia (1000);
	idio_codegen_code_prologue (byte_code);
	IDIO CPT_byte_code = idio_C_pointer_type (idio_CSI_idio_ia_s, byte_code);
	IDIO_C_TYPE_POINTER_FREEP (CPT_byte_code) = 0;

	return idio_struct_instance (idio_evaluate_eenv_type,
				     idio_listv (IDIO_EENV_ST_SIZE,
						 desc,
						 idio_S_false, /* chksum */
						 aotp,
						 idio_S_nil, /* symbols */
						 idio_array (0), /* st */
						 idio_S_nil, /* values */
						 idio_array_dv (0, idio_fixnum (0)), /* vt */
						 idio_array (0), /* constants */
						 IDIO_HASH_EQP (8), /* constants-hash */
						 module,
						 idio_S_nil, /* escapes */
						 idio_array (0), /* src-exprs */
						 idio_array (0), /* src-props */
						 CPT_byte_code));
    } else {
	IDIO CPT_byte_code = idio_C_pointer_type (idio_CSI_idio_ia_s, idio_all_code);
	IDIO_C_TYPE_POINTER_FREEP (CPT_byte_code) = 0;

	return idio_struct_instance (idio_evaluate_eenv_type,
				     idio_listv (IDIO_EENV_ST_SIZE,
						 desc,
						 idio_S_false, /* chksum */
						 aotp,
						 idio_S_nil, /* symbols */
						 idio_array (0), /* st */
						 idio_S_nil, /* values */
						 idio_array_dv (0, idio_fixnum (0)), /* vt */
						 idio_vm_constants,
						 idio_vm_constants_hash,
						 module,
						 idio_S_nil, /* escapes */
						 idio_vm_src_exprs,
						 idio_vm_src_props,
						 CPT_byte_code));
    }
}

IDIO_DEFINE_PRIMITIVE2V_DS ("%evaluation-environment", evaluation_environment, (IDIO desc, IDIO aotp, IDIO args), "desc, aot? [module]", "\
return an evaluation environ using `module` if supplied	\n\
							\n\
:param desc: description, usually file name		\n\
:type desc: string					\n\
:param aot?: pre-compile?				\n\
:type aot?: boolean					\n\
:param module: module defaults to the current module	\n\
:type module: symbol or module, optional		\n\
:return: eenv						\n\
:rtype: struct-instance					\n\
")
{
    IDIO_ASSERT (desc);
    IDIO_ASSERT (aotp);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (string, desc);
    IDIO_USER_TYPE_ASSERT (boolean, aotp);

    IDIO module = idio_thread_current_module ();

    if (idio_isa_pair (args)) {
	IDIO m_or_n = IDIO_PAIR_H (args);

	if (idio_isa_module (m_or_n)) {
	    module = m_or_n;
	} else if (idio_isa_symbol (m_or_n)) {
	    module = idio_module_find_module (m_or_n);

	    if (idio_S_unspec == module) {
		/*
		 * Test Case: module-errors/set-module-vci-unbound.idio
		 *
		 * set-module-vci! (gensym) 1 1
		 */
		idio_meaning_error_static_unbound (idio_S_nil, IDIO_C_FUNC_LOCATION (), m_or_n);

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: module-errors/set-module-vci-bad-module-type.idio
	     *
	     * set-module-vci! #t 1 1
	     */
	    idio_error_param_type ("module|symbol", m_or_n, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

    }

    return idio_evaluate_eenv (desc, aotp, module);
}

IDIO idio_evaluate_normal_eenv (IDIO desc, IDIO module)
{
    IDIO_ASSERT (desc);
    IDIO_ASSERT (module);

    assert (idio_isa_string (desc));
    IDIO_TYPE_ASSERT (string, desc);
    IDIO_TYPE_ASSERT (module, module);

    return idio_evaluate_eenv (desc, idio_S_false, module);
}

IDIO_DEFINE_PRIMITIVE1_DS ("environ?", environp, (IDIO o), "o", "\
test if `o` is an environ variable		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an environ variable, ``#f`` otherwise	\n\
						\n\
``environ?`` is a function so :ref:`quote <quote special form>`	\n\
any argument likely to be expanded by the evaluator:	\n\
						\n\
.. code-block:: idio				\n\
						\n\
   environ? HOME	; == environ? %P\"/home/me\"	\n\
   environ? 'HOME	; #t (or #f?)		\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	IDIO si = idio_module_env_symbol_recurse (o);

	if (idio_isa_pair (si) &&
	    idio_S_environ == IDIO_SI_SCOPE (si)) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("dynamic?", dynamicp, (IDIO o), "o", "\
test if `o` is a dynamic variable		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a dynamic variable, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	IDIO si = idio_module_env_symbol_recurse (o);

	if (idio_isa_pair (si) &&
	    idio_S_dynamic == IDIO_SI_SCOPE (si)) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("computed?", computedp, (IDIO o), "o", "\
test if `o` is a computed variable		\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a computed variable, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_symbol (o)) {
	IDIO si = idio_module_env_symbol_recurse (o);

	if (idio_isa_pair (si) &&
	    idio_S_computed == IDIO_SI_SCOPE (si)) {
	    r = idio_S_true;
	}
    }

    return r;
}

void idio_evaluate_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_evaluate_module, evaluate);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_evaluate_module, evaluation_environment);
    IDIO_ADD_PRIMITIVE (environp);
    IDIO_ADD_PRIMITIVE (dynamicp);
    IDIO_ADD_PRIMITIVE (computedp);
}

void idio_init_evaluate ()
{
    idio_module_table_register (idio_evaluate_add_primitives, NULL, NULL);

    idio_evaluate_evaluate_sym = IDIO_SYMBOL ("evaluate");
    idio_evaluate_module = idio_module (idio_evaluate_evaluate_sym);

#define IDIO_MEANING_STRING(c,s) idio_meaning_ ## c ## _string = idio_string_C (s); idio_gc_protect_auto (idio_meaning_ ## c ## _string);

    IDIO_MEANING_STRING (precompilation,         "idio_precompilation");
    IDIO_MEANING_STRING (predef_extend,          "idio_predef_extend");
    IDIO_MEANING_STRING (toplevel_extend,        "idio_toplevel_extend");
    IDIO_MEANING_STRING (dynamic_extend,         "idio_dynamic_extend");
    IDIO_MEANING_STRING (environ_extend,         "idio_environ_extend");
    IDIO_MEANING_STRING (define_gvi0,            "idio-meaning-define/gvi=0");
    IDIO_MEANING_STRING (define_infix_operator,  "idio-meaning-define-infix-operator");
    IDIO_MEANING_STRING (define_postfix_operator, "idio-meaning-define-postfix-operator");
    IDIO_MEANING_STRING (fix_abstraction,        "idio-meaning-fix-abstraction");
    IDIO_MEANING_STRING (dotted_abstraction,     "idio-meaning-dotted-abstraction");

    /*
     * An evaluation environment structure extended to support/update
     * execution environments.
     *
     * The problem here is two-fold:
     *
     * * pre-compilation is our base setting gathering all the
     *   information necessary to compute the pre-compilation files
     *
     *   Key to this is maintaining a per-file set of symbols (and
     *   consequently, values) in use by this file.
     *
     * * load-handle complicates matters as it is a mixture of
     *   pre-compilation and the execution environment.  We can
     *   convert an evaluation environment to an execution environment
     *   easily enough however, we want to avoid re-working the
     *   execution environment each time a new line of code is read
     *   in.
     *
     *   Having the evaluation environment maintain the execution
     *   environment as it goes along (and the execution environment
     *   simply re-uses the values) makes some sense.
     *
     * The evaluation environment also includes convenience attributes
     * such as:
     *
     * * constants-hash used as a fast lookup into the constants array
     *
     * * escapes even though they are dynamic like nametree
     *
     * Perhaps calling it a compilation unit context might be better.
     */
    IDIO sym = IDIO_SYMBOL ("%eenv");
    idio_evaluate_eenv_type = idio_struct_type (sym,
						idio_S_nil,
						idio_listv (IDIO_EENV_ST_SIZE,
							    IDIO_SYMBOL ("desc"),
							    IDIO_SYMBOL ("chksum"),
							    IDIO_SYMBOL ("aot?"),
							    IDIO_SYMBOL ("symbols"),
							    IDIO_SYMBOL ("st"),
							    IDIO_SYMBOL ("values"),
							    IDIO_SYMBOL ("vt"),
							    IDIO_SYMBOL ("constants"),
							    IDIO_SYMBOL ("constants-hash"),
							    IDIO_SYMBOL ("module"),
							    IDIO_SYMBOL ("escapes"),
							    IDIO_SYMBOL ("src-exprs"),
							    IDIO_SYMBOL ("src-props"),
							    IDIO_SYMBOL ("byte-code")));
    idio_module_set_symbol_value (sym, idio_evaluate_eenv_type, idio_evaluate_module);
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
