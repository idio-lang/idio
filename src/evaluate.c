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

 * For local and predefined variables this is split into two parts:
 * names and values.  Names are recorded during processing to provide
 * a fast index into a table of values for execution.

 * For module-level ("global") variables we can keep track of names we
 * have seen a definition for and the names we have seen used in the
 * body of code and produce a report on the difference.

 * The order of lookup is:

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
 *    as (idio_S_toplevel i) where i is an index into the VM's table
 *    of known symbols.

 *    There is a subtlety between referencing a variable and setting
 *    it.  You can reference a variable that is in the exported list
 *    of symbols of a module you import.  However, you can only set a
 *    variable if it is in the current module.

 * 3. in the table of predefined primitives.

 *    (These are not accessible to mortals, can only be looked up by
 *    the evaluation engine and are read-only.)
 *
 *    These are created by the IDIO_ADD_PRIMITIVE macros which
 *    populate a list of name to index mappings and the index is used
 *    during execution to access the table of (primitive) values
 *    directly.
 */

static IDIO idio_predef_names = idio_S_nil;
static IDIO idio_predef_values = idio_S_nil;

static IDIO idio_toplevel_names = idio_S_nil;
static IDIO idio_dynamic_names = idio_S_nil;

/*
 * Expanders (aka macros) live in their own little world...
 */

static IDIO idio_evaluation_module = idio_S_nil;
static IDIO idio_expander_list = idio_S_nil;
static IDIO idio_expander_list_src = idio_S_nil;
static IDIO idio_expander_thread = idio_S_nil;
static IDIO idio_operator_list = idio_S_nil;

static IDIO idio_initial_expander (IDIO x, IDIO e);
static IDIO idio_macro_expand (IDIO e);

static void idio_warning_static_undefineds (IDIO diff)
{
    IDIO_ASSERT (diff);
    IDIO_TYPE_ASSERT (pair, diff);

    idio_debug ("WARNING: undefined variables: %s\n", diff);
}

static void idio_error_static_redefine (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    idio_error_message ("redefinition of %s", IDIO_SYMBOL_S (name));
}

static void idio_warning_static_redefine (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    fprintf (stderr, "WARNING: redefinition of %s\n", IDIO_SYMBOL_S (name));
}

static void idio_error_static_unbound (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    idio_error_message ("%s is unbound", IDIO_SYMBOL_S (name));
}

static void idio_warning_static_unbound (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    fprintf (stderr, "WARNING: %s is unbound\n", IDIO_SYMBOL_S (name));
}

static void idio_error_static_immutable (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    
    idio_error_message ("%s is immutable", IDIO_SYMBOL_S (name));
}

static void idio_error_static_arity (char *m, IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);
    
    idio_error_message ("%s: %s", m, idio_as_string (args, 2));
}

static void idio_error_static_primitive_arity (char *m, IDIO f, IDIO args, IDIO primdata)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, f);
    IDIO_TYPE_ASSERT (list, args);
    
    idio_error_message ("%s: arity (%s) is %zd%s: passed %s", m, idio_as_string (f, 2), IDIO_PRIMITIVE_ARITY (primdata), IDIO_PRIMITIVE_VARARGS (primdata) ? "+" : "", idio_as_string (args, 2));
}

static IDIO idio_undefined_code (char *format, ...)
{
    char *s;
    
    va_list fmt_args;
    va_start (fmt_args, format);
    vasprintf (&s, format, fmt_args);
    va_end (fmt_args);

    IDIO r = IDIO_LIST2 (idio_S_undef, idio_string_C (s));
    free (s);

    idio_error_message ("undefined-code");
    return r;
}

static IDIO idio_variable_predefp (IDIO names, IDIO name)
{
    IDIO_ASSERT (names);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, names);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO a = idio_list_assq (name, names);

    if (idio_isa_pair (a)) {
	return IDIO_PAIR_T (a);
    }

    return idio_S_nil;
}

static IDIO idio_predef_extend (IDIO name, IDIO primdata)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (primdata);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (primitive, primdata);

    idio_ai_t index = idio_vm_extend_primitives (primdata);
    IDIO i = IDIO_FIXNUM (index);
    
    IDIO_PAIR_H (idio_predef_names) = idio_pair (IDIO_LIST3 (name, idio_S_predef, i),
						 IDIO_PAIR_H (idio_predef_names));

    /* for idio_symbol_lookup etc. */
    idio_module_primitive_set_symbol_value (name, primdata);

    return i;
}

IDIO idio_get_primitive_data (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO k = idio_variable_predefp (IDIO_PAIR_H (idio_predef_names), name);

    if (idio_S_nil != k) {
	IDIO i = IDIO_PAIR_H (IDIO_PAIR_T (k));

	return idio_vm_primitives_ref (IDIO_FIXNUM_VAL (i));
    }

    return idio_S_unspec;
}

IDIO idio_add_primitive (idio_primitive_t *d)
{
    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);
    return idio_predef_extend (sym, primdata);
}

IDIO idio_add_special_primitive (idio_primitive_t *d)
{
    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);
    /* no description! */
    return idio_predef_extend (sym, primdata);
}

static void idio_install_expander_source (IDIO id, IDIO proc, IDIO code);

void idio_add_expander_primitive (idio_primitive_t *d)
{
    idio_add_primitive (d);
    IDIO primdata = idio_primitive_data (d);
    /* idio_vm_extend_primitives (primdata); */
    idio_install_expander_source (idio_symbols_C_intern (d->name), primdata, primdata);
}

void idio_add_operator_primitive (idio_primitive_t *d)
{
    idio_add_primitive (d);
    IDIO primdata = idio_primitive_data (d);
    /* idio_vm_extend_primitives (primdata); */
    idio_install_operator (idio_symbols_C_intern (d->name), primdata);
}

static IDIO idio_variable_toplevelp (IDIO names, IDIO name)
{
    IDIO_ASSERT (names);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, names);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO a = idio_list_assq (name, names);

    if (idio_isa_pair (a)) {
	return IDIO_PAIR_T (a);
    }

    return idio_S_nil;
}

static IDIO idio_toplevel_extend (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    idio_ai_t index = idio_vm_extend_symbols (name);
    IDIO i = IDIO_FIXNUM (index);
    
    IDIO_PAIR_H (idio_toplevel_names) = idio_pair (IDIO_LIST3 (name, idio_S_toplevel, i),
						       IDIO_PAIR_H (idio_toplevel_names));

    IDIO cv = idio_symbol_lookup (name, idio_current_module ());
    if (idio_S_unspec == cv) {
	idio_module_current_set_symbol_value (name, idio_S_undef);
    }
    
    return i;
}

static IDIO idio_variable_dynamicp (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO a = idio_list_assq (name, IDIO_PAIR_H (idio_dynamic_names));

    if (idio_isa_pair (a)) {
	return IDIO_PAIR_T (a);
    }

    return idio_S_nil;
}

static IDIO idio_dynamic_extend (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    idio_ai_t index = idio_vm_extend_symbols (name);
    IDIO i = IDIO_FIXNUM (index);
    
    IDIO_PAIR_H (idio_dynamic_names) = idio_pair (IDIO_LIST3 (name, idio_S_dynamic, i),
						      IDIO_PAIR_H (idio_dynamic_names));

    return i;
}

static idio_ai_t idio_get_dynamic_index (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO k = idio_variable_dynamicp (name);

    if (idio_S_nil == k) {
	return IDIO_FIXNUM_VAL (idio_dynamic_extend (name));
    } else {
	return IDIO_FIXNUM_VAL (IDIO_PAIR_H (IDIO_PAIR_T (k)));
    }
}

static IDIO idio_variable_localp (IDIO nametree, size_t i, IDIO name)
{
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);

    if (idio_isa_pair (nametree)) {
	IDIO names = IDIO_PAIR_H (nametree);
	size_t j = 0;
	for (;;) {
	    if (idio_isa_pair (names)) {
		if (idio_eqp (name, IDIO_PAIR_H (names))) {
		    return IDIO_LIST3 (idio_S_local, IDIO_FIXNUM (i), IDIO_FIXNUM (j));
		} else {
		    names = IDIO_PAIR_T (names);
		    j++;
		}
	    } else if (idio_S_nil == names) {
		nametree = IDIO_PAIR_T (nametree);

		if (idio_S_nil == nametree) {
		    return idio_S_nil;
		}
		
		IDIO_TYPE_ASSERT (pair, nametree);
		
		names = IDIO_PAIR_H (nametree);
		i++;
		j = 0;
	    } else {
		if (idio_eqp (name, names)) {
		    return IDIO_LIST3 (idio_S_local, IDIO_FIXNUM (i), IDIO_FIXNUM (j));
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

    return idio_pair (names, nametree);
}

static IDIO idio_variable_kind (IDIO nametree, IDIO name)
{
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO r = idio_variable_localp (nametree, 0, name);
    
    if (idio_S_nil == r) {
	r = idio_variable_toplevelp (IDIO_PAIR_H (idio_toplevel_names), name);
	if (idio_S_nil == r) {
	    r = idio_variable_predefp (IDIO_PAIR_H (idio_predef_names), name);
	    if (idio_S_nil == r) {
		/*
		 * auto-extend toplevel names with this unknown
		 * variable -- it wasn't a lexical and can't be a
		 * primitive therefore we should (eventually) see a
		 * definition for it
		 */
		idio_toplevel_extend (name);
		r = idio_variable_toplevelp (IDIO_PAIR_H (idio_toplevel_names), name);
	    }
	}
    }

    return r;
}

/* static void idio_predef_init (IDIO nametree, IDIO name) */
/* { */
/*     IDIO_ASSERT (nametree); */
/*     IDIO_ASSERT (name); */
/*     IDIO_TYPE_ASSERT (list, nametree); */
/*     IDIO_TYPE_ASSERT (symbol, name); */

/*     IDIO k = idio_variable_kind (nametree, name); */

/*     if (idio_S_nil == k) { */
/* 	IDIO i = idio_predef_extend (name); */
/* 	idio_array_insert_index (idio_predef_values, idio_S_undef, IDIO_FIXNUM_VAL (i)); */
/*     } else { */
/* 	if (idio_S_predef == IDIO_PAIR_H (k)) { */
/* 	    idio_array_insert_index (idio_predef_values, idio_S_undef, IDIO_FIXNUM_VAL (IDIO_PAIR_T (k))); */
/* 	} else { */
/* 	    idio_error_static_redefine (name); */
/* 	} */
/*     } */
/* } */

/* static void idio_toplevel_init (IDIO nametree, IDIO name) */
/* { */
/*     IDIO_ASSERT (nametree); */
/*     IDIO_ASSERT (name); */
/*     IDIO_TYPE_ASSERT (list, nametree); */
/*     IDIO_TYPE_ASSERT (symbol, name); */

/*     IDIO k = idio_variable_kind (nametree, name); */

/*     if (idio_S_nil == k) { */
/* 	IDIO i = idio_toplevel_extend (name); */
/* 	idio_array_insert_index (idio_module_current_symbol_value (idio_toplevel_values), idio_S_undef, IDIO_FIXNUM_VAL (i)); */
/*     } else { */
/* 	if (idio_S_toplevel == IDIO_PAIR_H (k)) { */
/* 	    idio_array_insert_index (idio_module_current_symbol_value (idio_toplevel_values), idio_S_undef, IDIO_FIXNUM_VAL (IDIO_PAIR_T (k))); */
/* 	} else { */
/* 	    idio_error_static_redefine (name); */
/* 	} */
/*     } */
/* } */

static IDIO idio_evaluate_expander_source (IDIO x, IDIO e)
{
    IDIO_ASSERT (x);
    IDIO_ASSERT (e);

    /* fprintf (stderr, "evaluate-expander: in\n"); */

    IDIO cthr = idio_current_thread ();
    idio_set_current_thread (idio_expander_thread);
    idio_thread_save_state (idio_expander_thread);
    idio_vm_default_pc (idio_expander_thread);

    idio_initial_expander (x, e);
    IDIO r = idio_vm_run (idio_expander_thread, 0);
    
    idio_thread_restore_state (idio_expander_thread);
    idio_set_current_thread (cthr);

    /* idio_debug ("evaluate-expander: out: %s\n", r); */

    /* if (idio_S_nil == r) { */
    /* 	fprintf (stderr, "evaluate-expander: bad expansion?\n"); */
    /* } */
    
    return r;
}

static IDIO idio_expanderp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO expander_list = idio_module_symbol_value (idio_expander_list, idio_evaluation_module);
    /* fprintf (stderr, "expander?: %p in %p: *expander-list* = %p\n", idio_expander_list, idio_evaluation_module, expander_list); */
    /* idio_dump (expander_list, 1); */
    
    IDIO assq = idio_list_assq (name, expander_list);

    /* char *as = idio_as_string (assq,1); */
    /* fprintf (stderr, "expander?: assq %s\n", as); */
    /* free (as); */

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    /* idio_debug ("expander?: %s isa PAIR\n", name); */
	    IDIO lv = idio_symbol_lookup (name, idio_current_module ());
	    /* idio_debug ("expander?: lookup -> %s\n", lv); */
	    if (idio_isa_primitive (lv) ||
		idio_isa_closure (lv)) {
		IDIO_PAIR_T (assq) = lv;
	    }
	} else {
	    /* fprintf (stderr, "expander?: isa %s\n", idio_type2string (v)); */
	}
    }

    return assq;
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
    
    IDIO el = idio_module_symbol_value (idio_expander_list, idio_evaluation_module);
    IDIO old = idio_list_assq (id, el);

    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_expander_list,
				      idio_pair (idio_pair (id, proc),
						 el),
				      idio_evaluation_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

static void idio_install_expander_source (IDIO id, IDIO proc, IDIO code)
{
    /* idio_debug ("install-expander: %s\n", id); */

    idio_install_expander (id, proc);

    IDIO els = idio_module_symbol_value (idio_expander_list_src, idio_evaluation_module);
    IDIO old = idio_list_assq (id, els);
    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_expander_list_src,
				      idio_pair (idio_pair (id, proc),
						 els),
				      idio_evaluation_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

static IDIO idio_evaluate_expander_code (IDIO m)
{
    IDIO_ASSERT (m);

    /* idio_debug ("install-expander-code: %s\n", m); */

    IDIO cthr = idio_current_thread ();
    idio_set_current_thread (idio_expander_thread);
    idio_thread_save_state (idio_expander_thread);
    idio_vm_default_pc (idio_expander_thread);

    idio_vm_codegen (idio_expander_thread, m);
    IDIO r = idio_vm_run (idio_expander_thread, 0);
    
    idio_thread_restore_state (idio_expander_thread);
    idio_set_current_thread (cthr);

    /* idio_debug ("install-expander-code: out: %s\n", r);  */
    return r;
}

/* static void idio_push_expander (IDIO id, IDIO proc) */
/* { */
/*     idio_module_set_symbol_value (idio_expander_list, */
/* 				  idio_pair (idio_pair (id, proc), */
/* 					     idio_module_symbol_value (idio_expander_list, idio_evaluation_module)), */
/* 				  idio_evaluation_module); */
/* } */

/* static void idio_delete_expander (IDIO id) */
/* { */
/*     IDIO el = idio_module_symbol_value (idio_expander_list, idio_evaluation_module); */
/*     IDIO prv = idio_S_false; */

/*     for (;;) { */
/* 	if (idio_S_nil == el) { */
/* 	    return; */
/* 	} else if (idio_eqp (IDIO_PAIR_H (IDIO_PAIR_H (el)), id)) { */
/* 	    if (idio_S_false == prv) { */
/* 		idio_module_set_symbol_value (idio_expander_list, */
/* 					      IDIO_PAIR_T (el), */
/* 					      idio_evaluation_module); */
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

    /* fprintf (stderr, "macro-expand: %zd args in ", idio_list_length (e));  */
    /* idio_debug (" %s\n", e);  */
    
    return idio_evaluate_expander_source (e, idio_S_unspec);
}

static IDIO idio_macro_expands (IDIO e)
{
    IDIO_ASSERT (e);

    for (;;) {
	/* fprintf (stderr, "macro-expand*: e: %zd args in", idio_list_length (e)); */
	/* idio_debug (" %s\n", e); */

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

void idio_install_operator (IDIO id, IDIO proc)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);
    
    IDIO el = idio_module_symbol_value (idio_operator_list, idio_evaluation_module);
    IDIO old = idio_list_assq (id, el);

    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_operator_list,
				      idio_pair (idio_pair (id, proc),
						 el),
				      idio_evaluation_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

static IDIO idio_evaluate_operator_code (IDIO m)
{
    IDIO_ASSERT (m);

    /* idio_debug ("install-operator-code: %s\n", m); */

    IDIO cthr = idio_current_thread ();
    idio_set_current_thread (idio_expander_thread);
    idio_thread_save_state (idio_expander_thread);
    idio_vm_default_pc (idio_expander_thread);

    idio_vm_codegen (idio_expander_thread, m);
    IDIO r = idio_vm_run (idio_expander_thread, 0);
    
    idio_thread_restore_state (idio_expander_thread);
    idio_set_current_thread (cthr);

    /* idio_debug ("install-operator-code: out: %s\n", r);  */
    return r;
}

static IDIO idio_evaluate_operator (IDIO n, IDIO e, IDIO b, IDIO a)
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (e);
    IDIO_ASSERT (b);
    IDIO_ASSERT (a);

    /* fprintf (stderr, "evaluate-operator: in\n"); */

    IDIO cthr = idio_current_thread ();
    idio_set_current_thread (idio_expander_thread);
    idio_thread_save_state (idio_expander_thread);
    idio_vm_default_pc (idio_expander_thread);

    idio_apply (IDIO_PAIR_T (e), IDIO_LIST3 (n, b, IDIO_LIST1 (a)));
    IDIO r = idio_vm_run (idio_expander_thread, 0);
    
    idio_thread_restore_state (idio_expander_thread);
    idio_set_current_thread (cthr);

    /* idio_debug ("evaluate-operator: out: %s\n", r); */

    /* if (idio_S_nil == r) { */
    /* 	fprintf (stderr, "evaluate-operator: bad expansion?\n"); */
    /* } */
    
    return r;
}

static IDIO idio_operatorp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO operator_list = idio_module_symbol_value (idio_operator_list, idio_evaluation_module);
    /* fprintf (stderr, "operator?: %p in %p: *operator-list* = %p\n", idio_operator_list, idio_evaluation_module, operator_list); */
    /* idio_dump (operator_list, 1); */
    
    IDIO assq = idio_list_assq (name, operator_list);

    /* char *as = idio_as_string (assq,1); */
    /* fprintf (stderr, "operator?: assq %s\n", as); */
    /* free (as); */

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    /* idio_debug ("operator?: %s isa PAIR\n", name); */
	    IDIO lv = idio_symbol_lookup (name, idio_current_module ());
	    /* idio_debug ("operator?: lookup -> %s\n", lv); */
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

static IDIO idio_meaning (IDIO e, IDIO nametree, int tailp);

static IDIO idio_meaning_reference (IDIO name, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO k = idio_variable_kind (nametree, name);

    if (idio_S_nil == k) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_error_static_unbound (name);
	return idio_undefined_code ("meaning-reference: %s", idio_as_string (name, 1));
    }

    IDIO kt = IDIO_PAIR_H (k);
    IDIO kv = IDIO_PAIR_T (k);
    IDIO i = IDIO_PAIR_H (kv);
    
    if (idio_S_local == kt) {
	kv = IDIO_PAIR_T (kv);
	IDIO j = IDIO_PAIR_H (kv);
	if (0 == IDIO_FIXNUM_VAL (i)) {
	    return IDIO_LIST2 (idio_I_SHALLOW_ARGUMENT_REF, j);
	} else {
	    return IDIO_LIST3 (idio_I_DEEP_ARGUMENT_REF, i, j);
	}
    } else if (idio_S_toplevel == kt) {
	return IDIO_LIST2 (idio_I_CHECKED_GLOBAL_REF, i);
    } else if (idio_S_predef == kt) {
	/* fprintf (stderr, "meaning-reference: predefined #%zd\n", IDIO_FIXNUM_VAL (i)); */
	return IDIO_LIST2 (idio_I_PREDEFINED, i);
    } else {
	idio_error_static_unbound (name);
	return idio_undefined_code ("meaning-reference: %s", idio_as_string (name, 1));
    }
}

static IDIO idio_meaning_function_reference (IDIO name, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO k = idio_variable_kind (nametree, name);

    if (idio_S_nil == k) {
	/*
	 * shouldn't get here as unknowns are automatically
	 * toplevel...
	 */
	idio_error_static_unbound (name);
	return idio_undefined_code ("meaning-reference: %s", idio_as_string (name, 1));
    }

    IDIO kt = IDIO_PAIR_H (k);
    IDIO kv = IDIO_PAIR_T (k);
    IDIO i = IDIO_PAIR_H (kv);
    
    if (idio_S_local == kt) {
	kv = IDIO_PAIR_T (kv);
	IDIO j = IDIO_PAIR_H (kv);
	if (0 == IDIO_FIXNUM_VAL (i)) {
	    return IDIO_LIST2 (idio_I_SHALLOW_ARGUMENT_REF, j);
	} else {
	    return IDIO_LIST3 (idio_I_DEEP_ARGUMENT_REF, i, j);
	}
    } else if (idio_S_toplevel == kt) {
	return IDIO_LIST2 (idio_I_CHECKED_GLOBAL_FUNCTION_REF, i);
    } else if (idio_S_predef == kt) {
	/* fprintf (stderr, "meaning-reference: predefined #%zd\n", IDIO_FIXNUM_VAL (i)); */
	return IDIO_LIST2 (idio_I_PREDEFINED, i);
    } else {
	idio_error_static_unbound (name);
	return idio_undefined_code ("meaning-reference: %s", idio_as_string (name, 1));
    }
}

static IDIO idio_meaning_quotation (IDIO v, IDIO nametree, int tailp)
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
			       idio_meaning_dequasiquote (IDIO_PAIR_H (IDIO_PAIR_T (e)),
							  level + 1));
	} else if (idio_S_unquote == eh) {
	    if (level <= 0) {
		return IDIO_PAIR_H (IDIO_PAIR_T (e));
	    } else {
		/* ('list ''unquote (deqq (cadr e) (- level 1))) */
		return IDIO_LIST3 (idio_S_list,
				   IDIO_LIST2 (idio_S_quote, idio_S_unquote),
				   idio_meaning_dequasiquote (IDIO_PAIR_H (IDIO_PAIR_T (e)),
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
				   idio_meaning_dequasiquote (IDIO_PAIR_H (IDIO_PAIR_T (e)),
							      level - 1));
	    }
	} else if (level <= 0 &&
		   idio_isa_pair (IDIO_PAIR_H (e)) &&
		   idio_S_unquotesplicing == IDIO_PAIR_H (IDIO_PAIR_H (e))) {
	    if (idio_S_nil == IDIO_PAIR_T (e)) {
		return IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_H (e)));
	    } else {
		/* ('append (cadar e) (deqq (cdr e) level)) */
		return IDIO_LIST3 (idio_S_append,
				   IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_H (e))),
				   idio_meaning_dequasiquote (IDIO_PAIR_T (e),
							      level));
	    }
	} else {
	    return IDIO_LIST3 (idio_S_pair,
			       idio_meaning_dequasiquote (IDIO_PAIR_H (e), level),
			       idio_meaning_dequasiquote (IDIO_PAIR_T (e), level));
	}
    } else if (idio_isa_array (e)) {
	return IDIO_LIST2 (idio_symbols_C_intern ("list->vector"), idio_meaning_dequasiquote (idio_array_to_list (e), level));
    } else if (idio_isa_symbol (e)) {
	return IDIO_LIST2 (idio_S_quote, e);
    } else {
	return e;
    }

    /* not reached */
    return idio_S_unspec;
}

static IDIO idio_meaning_quasiquotation (IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);

    /* idio_debug ("meaning-quasiquote: in %s\n", e);  */
    IDIO dq = idio_meaning_dequasiquote (e, 0);
    /* idio_debug ("meaning-quasiquote: dq %s\n", dq);  */

    IDIO dqm = idio_meaning (dq, nametree, tailp);
    /* idio_debug ("meaning-quasiquote: m => %s\n", dqm); */
    
    return dqm;
}

static IDIO idio_meaning_alternative (IDIO e1, IDIO e2, IDIO e3, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e1);
    IDIO_ASSERT (e2);
    IDIO_ASSERT (e3);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m1 = idio_meaning (e1, nametree, 0);
    IDIO m2 = idio_meaning (e2, nametree, tailp);
    IDIO m3 = idio_meaning (e3, nametree, tailp);
    
    return IDIO_LIST4 (idio_I_ALTERNATIVE, m1, m2, m3);
}

static IDIO idio_rewrite_cond (IDIO c)
{
    /* idio_debug ("cond-rewrite: %s\n", c);    */

    if (idio_S_nil == c) {
	/* fprintf (stderr, "cond-rewrite: nil clause\n");   */
	return idio_S_void;
    } else if (! idio_isa_pair (c)) {
	idio_error_param_type ("pair", c);
	return idio_undefined_code ("cond: %s", idio_as_string (c, 1));
    } else if (! idio_isa_pair (IDIO_PAIR_H (c))) {
	/* idio_debug ("pair/pair: c %s\n", c); */
	idio_error_param_type ("pair/pair", c);
	return idio_undefined_code ("cond: %s", idio_as_string (c, 1));
    } else if (idio_S_else == IDIO_PAIR_H (IDIO_PAIR_H (c))) {
	/* fprintf (stderr, "cond-rewrite: else clause\n");  */
	if (idio_S_nil == IDIO_PAIR_T (c)) {
	    return idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_T (IDIO_PAIR_H (c)));
	} else {
	    return idio_undefined_code ("cond: else not in last clause", idio_as_string (c, 1));
	}
    }

    if (idio_isa_pair (IDIO_PAIR_T (IDIO_PAIR_H (c))) &&
	idio_S_eq_gt == IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_H (c)))) {
	/* fprintf (stderr, "cond-rewrite: => clause\n");  */
	if (idio_isa_list (IDIO_PAIR_H (c)) &&
	    idio_list_length (IDIO_PAIR_H (c)) == 3) {
	    IDIO gs = idio_gensym ();
	    /*
	      `(let ((gs ,(caar c)))
	         (if gs
	             (,(caddar c) gs)
	             ,(rewrite-cond-clauses (cdr c))))
	     */
	    return IDIO_LIST3 (idio_S_let,
			       IDIO_LIST1 (IDIO_LIST2 (gs, IDIO_PAIR_H (IDIO_PAIR_H (c)))),
			       IDIO_LIST4 (idio_S_if,
					   gs,
					   IDIO_LIST2 (IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (IDIO_PAIR_H (c)))),
						       gs),
					   idio_rewrite_cond (IDIO_PAIR_T (c))));
	} else {
	    idio_error_param_type ("=>", c);
	    return idio_undefined_code ("cond: => bad format", idio_as_string (c, 1));
	}
    } else if (idio_S_nil == IDIO_PAIR_T (IDIO_PAIR_H (c))) {
	/* fprintf (stderr, "cond-rewrite: null? cdar clause\n");  */
	IDIO gs = idio_gensym ();
	/*
	  `(let ((gs ,(caar c)))
	     (or gs
	         ,(rewrite-cond-clauses (cdr c))))
	*/
	return IDIO_LIST3 (idio_S_let,
			   IDIO_LIST1 (IDIO_LIST2 (gs, IDIO_PAIR_H (IDIO_PAIR_H (c)))),
			   IDIO_LIST3 (idio_S_or,
				       gs,
				       idio_rewrite_cond (IDIO_PAIR_T (c))));
    } else {
	/* fprintf (stderr, "cond-rewrite: default clause\n");  */
	return IDIO_LIST4 (idio_S_if,
			   IDIO_PAIR_H (IDIO_PAIR_H (c)),
			   idio_list_append2 (IDIO_LIST1 (idio_S_begin), IDIO_PAIR_T (IDIO_PAIR_H (c))),
			   idio_rewrite_cond (IDIO_PAIR_T (c)));
    }
}

static IDIO idio_meaning_assignment (IDIO name, IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, 0);
    IDIO k = idio_variable_kind (nametree, name);

    if (idio_S_nil == k) {
	IDIO d = idio_list_memq (name, idio_module_current_defined ());
	IDIO i = idio_S_nil;
	if (idio_S_false == d) {
	    i = idio_toplevel_extend (name);
	} else {
	    i = idio_variable_toplevelp (IDIO_PAIR_H (idio_toplevel_names), name);
	}
	/* if (idio_S_nil == i) { */
	/*     fprintf (stderr, "meaning-assignment: %s not yet defined\n", idio_as_string (name, 1)); */
	/* } */
	IDIO_C_ASSERT (IDIO_TYPE_FIXNUM == idio_type (i));
	return IDIO_LIST3 (idio_I_GLOBAL_SET, i, m);
    }

    IDIO kt = IDIO_PAIR_H (k);
    IDIO kv = IDIO_PAIR_T (k);
    IDIO i = IDIO_PAIR_H (kv);
    
    if (idio_S_local == kt) {
	kv = IDIO_PAIR_T (kv);
	IDIO j = IDIO_PAIR_H (kv);
	if (0 == IDIO_FIXNUM_VAL (i)) {
	    return IDIO_LIST3 (idio_I_SHALLOW_ARGUMENT_SET, j, m);
	} else {
	    /* fprintf (stderr, "meaning-assignment: %s: deep-argument-set %zd %zd <= %s\n", idio_as_string (name, 1), IDIO_FIXNUM_VAL (i), IDIO_FIXNUM_VAL (j), idio_as_string (m, 1)); */
	    return IDIO_LIST4 (idio_I_DEEP_ARGUMENT_SET, i, j, m);
	}
    } else if (idio_S_toplevel == kt) {
	return IDIO_LIST3 (idio_I_GLOBAL_SET, i, m);
    } else if (idio_S_predef == kt) {
	/*
	 * We can shadow predefs...semantically dubious
	 */
	i = idio_toplevel_extend (name);
	return IDIO_LIST3 (idio_I_GLOBAL_SET, i, m);

	/* if we weren't allowing shadowing */
	idio_error_static_immutable (name);
	return idio_S_unspec;
    } else {
	idio_error_static_unbound (name);
	return idio_S_unspec;
    }
}

static IDIO idio_meaning_define (IDIO name, IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-define: (define %s", name);  */
    /* idio_debug (" %s)\n", e);  */

    if (idio_isa_pair (name)) {
	/*
	 * (define (func arg) ...) => (define func (function (arg) ...))
	 *
	 * NB e is already a list
	 */
	/* idio_debug ("meaning-define: rewrite: (define %s", name); */
	/* idio_debug (" %s)\n", e); */
	e = idio_list_append2 (IDIO_LIST2 (idio_S_function, 
					   IDIO_PAIR_T (name)),
			       e);
	name = IDIO_PAIR_H (name);

	/* fprintf (stderr, "meaning-define: rewrite: (define %s", IDIO_SYMBOL_S (name)); */
	/* idio_debug (" %s)\n", e); */
    } else {
	if (idio_isa_pair (e)) {
	    e = IDIO_PAIR_H (e);
	}
    }

    /* idio_debug ("meaning-define: (define %s", name); */
    /* idio_debug (" %s)\n", e); */

    IDIO d = idio_list_memq (name, idio_module_current_defined ());

    if (idio_isa_pair (d)) {
	/* idio_warning_static_redefine (name); */
    } else {
	idio_module_current_extend_defined (name);
    }

    return idio_meaning_assignment (name, e, nametree, tailp);
}

static IDIO idio_meaning_define_macro (IDIO name, IDIO e, IDIO nametree, int tailp)
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
	/* idio_debug ("meaning-define-macro: rewrite: (define-macro %s", name); */
	/* idio_debug (" %s)\n", e); */
	e = IDIO_LIST3 (idio_S_function,  
			IDIO_PAIR_T (name), 
			e); 
	/* e = idio_list_append2 (IDIO_LIST2 (idio_S_function,  */
	/* 				   IDIO_PAIR_T (name)), */
	/* 		       e); */
	name = IDIO_PAIR_H (name);
	/* fprintf (stderr, "meaning-define-macro: rewrite: (define-macro %s", IDIO_SYMBOL_S (name)); */
	/* idio_debug (" %s)\n", e); */
    }

    /* idio_debug ("meaning-define-macro:\nname=%s\n", name); */
    /* idio_debug ("e=%s\n", e); */

    IDIO d = idio_list_memq (name, idio_module_current_defined ());

    if (idio_isa_pair (d)) {
	/* idio_warning_static_redefine (name); */
    } else {
	idio_module_current_extend_defined (name);
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

    /* idio_debug ("define-macro: => expander %s\n", e); */

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
     * which requires using the existing b to define itself hence
     * defining some other name, "%b", which can use "b" freely then
     * redefine b to this new version)
     *
     * However, we can't just use the current value of "%b" in
     * (define-macro b %b) as this macro-expander association means we
     * are replacing the nominal definition of a macro with an
     * expander which takes two arguments and the body of which will
     * take the cdr of its first argument.  Left alone, expander "b"
     * will take the cdr then expander "%b" will take the cdr....  A
     * Cdr Too Far, one would say, in hindsight.
     *
     * So catch the case where the value is already an expander.
     */
    if (idio_isa_symbol (e)) {
	IDIO exp = idio_expanderp (e);

	if (idio_S_false != exp) {
	    idio_install_expander_source (name, exp, expander);
	    return idio_meaning_assignment (name, IDIO_PAIR_T (exp), nametree, 0);
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
     * now and install that code in *expander-list* directly.
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

    IDIO m_a = idio_meaning_assignment (name, expander, nametree, 0);

    idio_install_expander_source (name, expander, expander);

    idio_evaluate_expander_code (m_a);

    idio_ai_t i = idio_vm_symbols_lookup (name);
    if (-1 == i) {
	idio_debug ("extending symbols for define-macro %s\n", name);
	i = idio_vm_extend_symbols (name);
    }

    /*
     * NB.  This effectively creates/stores the macro body code a
     * second time *in this instance of the engine*.  When the object
     * code is read in there won't be an instance of the macro body
     * code lying around -- at least not one we can access.
     */

    IDIO r = IDIO_LIST3 (idio_I_EXPANDER, IDIO_FIXNUM (i), m_a);
    /* idio_debug ("idio-meaning-define-macro %s", name);  */
    /* idio_debug (" r=%s\n", r);  */
    return r;
}

static IDIO idio_meaning_sequence (IDIO ep, IDIO nametree, int tailp, IDIO keyword);

static IDIO idio_meanings_single_sequence (IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_meaning (e, nametree, tailp);
}

static IDIO idio_meanings_multiple_sequence (IDIO e, IDIO ep, IDIO nametree, int tailp, IDIO keyword)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (keyword);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, 0);
    IDIO mp = idio_meaning_sequence (ep, nametree, tailp, keyword);

    if (idio_S_and == keyword) {
	return IDIO_LIST3 (idio_I_AND, m, mp);
    } else if (idio_S_or == keyword) {
	return IDIO_LIST3 (idio_I_OR, m, mp);
    } else if (idio_S_begin == keyword) {
	return IDIO_LIST3 (idio_I_BEGIN, m, mp);
    } else {
	char *ks = idio_as_string (keyword, 1);
	idio_error_message ("unexpected sequence keyword: %s", ks);
	free (ks);
	return idio_S_unspec;
    }
}

static IDIO idio_meaning_sequence (IDIO ep, IDIO nametree, int tailp, IDIO keyword)
{
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (keyword);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-sequence: %s\n", ep);   */
    
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
		char *ks = idio_as_string (keyword, 1);
		idio_error_message ("unexpected sequence keyword: %s", ks);
		free (ks);
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
		IDIO m = idio_meaning (e, nametree, 0);
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
	    return idio_meanings_single_sequence (eph, nametree, tailp);
	}
    }

    /*
     * We can get here for the x in the bindings of

     * (define (list . x) x)
     */
    return idio_meaning (ep, nametree, tailp);
}

static IDIO idio_meaning_fix_abstraction (IDIO ns, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, ns);

    IDIO mp = idio_meaning_sequence (ep, nt2, 1, idio_S_begin);

    return IDIO_LIST3 (idio_I_FIX_CLOSURE, mp, IDIO_FIXNUM (arity));
}

static IDIO idio_meaning_dotted_abstraction (IDIO ns, IDIO n, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append2 (ns, IDIO_LIST1 (n)));
    IDIO mp = idio_meaning_sequence (ep, nt2, 1, idio_S_begin);

    return IDIO_LIST3 (idio_I_NARY_CLOSURE, mp, IDIO_FIXNUM (arity));
}

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
		   idio_S_false != idio_expanderp (IDIO_PAIR_H (IDIO_PAIR_H (l)))) {
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
	    /* internal define -> letrec */
	    /* idio_debug ("irb: internal define: cur %s\n", cur);  */

	    IDIO body = idio_list_append2 (IDIO_LIST1 (cur), IDIO_PAIR_T (l));
	    /* idio_debug ("irb: internal define: body %s\n", body); */
	    r = idio_pair (idio_rewrite_body_letrec (body), r);
	    break;
	} else if (idio_isa_pair (cur) &&
		   idio_S_colon_eq == IDIO_PAIR_H (cur)) {
	    /* internal := -> let* */
	    /* idio_debug ("irb: internal := cur %s\n", cur);  */

	    IDIO body = idio_rewrite_body (IDIO_PAIR_T (l));
	    /* idio_debug ("irb: internal := body %s\n", body); */
	    if (idio_S_nil != r) {
		r = idio_list_reverse (r);
	    }
	    r = idio_list_append2 (r,
				   IDIO_LIST1 (IDIO_LIST3 (idio_S_let,
							   IDIO_LIST1 (IDIO_PAIR_T (cur)),
							   idio_list_append2 (IDIO_LIST1 (idio_S_begin), body))));
	    /* idio_debug ("irb: internal := r %s\n", r); */
	    return r;
	} else if (idio_isa_pair (cur) &&
		   idio_S_define_macro == IDIO_PAIR_H (cur)) {
	    /* internal define-macro */
	    idio_debug ("%s\n", cur);
	     idio_error_message ("rewrite-body: internal define-macro");
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
	    idio_warning_message ("empty body");
	    return idio_S_nil;
	} else if (idio_isa_pair (l) &&
		   idio_isa_pair (IDIO_PAIR_H (l)) &&
		   idio_S_false != idio_expanderp (IDIO_PAIR_H (IDIO_PAIR_H (l)))) {
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
	    /* idio_debug ("irbd: internal letrec: cur %s\n", cur);  */

	    IDIO bindings = IDIO_PAIR_H (IDIO_PAIR_T (cur));
	    IDIO form = idio_S_unspec;
	    if (idio_isa_pair (bindings)) {
		form = IDIO_LIST2 (IDIO_PAIR_H (bindings),
				   idio_list_append2 (IDIO_LIST2 (idio_S_function,
								  IDIO_PAIR_T (bindings)),
						      IDIO_PAIR_T (IDIO_PAIR_T (cur))));
	    } else {
		form = IDIO_PAIR_T (cur);
	    }
	    defs = idio_pair (form, defs);
	    l = IDIO_PAIR_T (l);
	    continue;
	} else if (idio_isa_pair (cur) &&
		   idio_S_define_macro == IDIO_PAIR_H (cur)) {
	    /* internal define-macro */
	     idio_error_message ("letrec: internal define-macro");
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

static IDIO idio_meaning_abstraction (IDIO nns, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (nns);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /*
     * Internal defines:
     *
     * (function bindings
     *   (define b1 e1)
     *   (define b2 e2)
     *   body)
     *
     * is equivalent to:
     *
     * (function bindings
     *   (letrec ((b1 e1)
     *            (b2 e2))
     *     body))
     *
     * Noting that bX could be a pair and therefore a function
     * expression.
     *
     * The idea being that you can define local functions in parallel
     * with body rather than embedded as with a letrec directly.
     *
     * Of course that means muggins has to do the legwork.
     */

    /* idio_debug ("meaning-abstraction: in: %s\nep=%s\n", ep);  */
    ep = idio_rewrite_body (ep); 
    /* idio_debug ("meaning-abstraction: in: %s\nep=%s\n", ep);  */

    IDIO ns = nns;
    IDIO regular = idio_S_nil;

    for (;;) {
	if (idio_isa_pair (ns))  {
	    regular = idio_pair (IDIO_PAIR_H (ns), regular);
	    ns = IDIO_PAIR_T (ns);
	} else if (idio_S_nil == ns) {
	    return idio_meaning_fix_abstraction (nns, ep, nametree, tailp);
	} else {
	    return idio_meaning_dotted_abstraction (idio_list_reverse (regular), ns, ep, nametree, tailp);
	}
    }

    return idio_undefined_code ("meaning-abstraction: %s %s", idio_as_string (nns, 1), idio_as_string (ep, 1));
}

static IDIO idio_meaning_block (IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-block:  in { %s }\n", es);  */

    es = idio_rewrite_body (es);

    /* idio_debug ("meaning-block: out { %s }\n", es);  */

    return idio_meaning_sequence (es, nametree, tailp, idio_S_begin);
}

static IDIO idio_meanings (IDIO es, IDIO nametree, size_t size, int tailp);

static IDIO idio_meaning_some_arguments (IDIO e, IDIO es, IDIO nametree, size_t size, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, 0);
    IDIO ms = idio_meanings (es, nametree, size, tailp);
    size_t rank = size - (idio_list_length (es) + 1);

    return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, IDIO_FIXNUM (rank));
}

static IDIO idio_meaning_no_argument (IDIO nametree, size_t size, int tailp)
{
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, IDIO_FIXNUM (size));
}

static IDIO idio_meanings (IDIO es, IDIO nametree, size_t size, int tailp)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (es)) {
	return idio_meaning_some_arguments (IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, tailp);
    } else {
	return idio_meaning_no_argument (nametree, size, tailp);
    }
}

static IDIO idio_meaning_fix_closed_application (IDIO ns, IDIO body, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    body = idio_rewrite_body (body);
    
    IDIO ms = idio_meanings (es, nametree, idio_list_length (es), 0);
    IDIO nt2 = idio_nametree_extend (nametree, ns);
    IDIO mbody = idio_meaning_sequence (body, nt2, tailp, idio_S_begin);

    if (tailp) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_meaning_dotteds (IDIO es, IDIO nametree, size_t size, size_t arity, int tailp);

static IDIO idio_meaning_some_dotted_arguments (IDIO e, IDIO es, IDIO nametree, size_t size, size_t arity, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, 0);
    IDIO ms = idio_meaning_dotteds (es, nametree, size, arity, tailp);
    size_t rank = size - (idio_list_length (es) + 1);

    if (rank < arity) {
	return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, IDIO_FIXNUM (rank));
    } else {
	return IDIO_LIST4 (idio_I_CONS_ARGUMENT, m, ms, IDIO_FIXNUM (arity));
    }
}

static IDIO idio_meaning_no_dotted_argument (IDIO nametree, size_t size, size_t arity, int tailp)
{
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, IDIO_FIXNUM (arity));
}

static IDIO idio_meaning_dotteds (IDIO es, IDIO nametree, size_t size, size_t arity, int tailp)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (es)) {
	return idio_meaning_some_dotted_arguments (IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, arity, tailp);
    } else {
	return idio_meaning_no_dotted_argument (nametree, size, arity, tailp);
    }
}

static IDIO idio_meaning_dotted_closed_application (IDIO ns, IDIO n, IDIO body, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO ms = idio_meaning_dotteds (es, nametree, idio_list_length (es), idio_list_length (ns), 0);
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append2 (ns, IDIO_LIST1 (n)));
    IDIO mbody = idio_meaning_sequence (body, nt2, tailp, idio_S_begin);

    if (tailp) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_meaning_closed_application (IDIO e, IDIO ees, IDIO nametree, int tailp)
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
		idio_error_static_arity ("not enough arguments", IDIO_LIST2 (nns, ees));
		return idio_S_unspec;
	    }
	} else if (idio_S_nil == ns) {
	    if (idio_S_nil == es) {
		return idio_meaning_fix_closed_application (nns, IDIO_PAIR_T (et), ees, nametree, tailp);
	    } else {
		idio_error_static_arity ("too many arguments", IDIO_LIST2 (e, ees));
		return idio_S_unspec;
	    }
	} else {
	    return idio_meaning_dotted_closed_application (idio_list_reverse (regular), ns, IDIO_PAIR_T (et), ees, nametree, tailp);
	}
    }

    return idio_undefined_code ("meaning-closed-application: %s %s", idio_as_string (e, 1), idio_as_string (ees, 1));
}

static IDIO idio_meaning_regular_application (IDIO e, IDIO es, IDIO nametree, int tailp);

static IDIO idio_meaning_primitive_application (IDIO e, IDIO es, IDIO nametree, int tailp, size_t arity, IDIO index)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, e);
    IDIO_TYPE_ASSERT (list, es);
    IDIO_TYPE_ASSERT (list, nametree);

    /*
     * Yuk!

     * We can accelerate fixed-arity primitive calls which, rather
     * than allocating frames on the stack, can just call the
     * primitive function with the contents of the VM registers
     * directly.  Better yet, we can accelerate some of them by having
     * a specialized VM instruction thus avoiding having to pass the
     * index of the primitive at all.

     * However, if we leave the decision as to which calls to
     * accelerate to the compiler then the compiler must be able to
     * fall back to the general idio_meaning_regular_application()
     * functionality.  Which is very complex.

     * For us to do it here we must know which primitive calls the VM
     * is capable of specializing which is knowledge we shouldn't
     * have.

     * There must be a better way...but in the meanwhile it's much
     * less code for us to check the specialization here.
     */

    IDIO primdata = idio_vm_primitives_ref (IDIO_FIXNUM_VAL (index));

    if (IDIO_PRIMITIVE_VARARGS (primdata)) {
	/*
	 * only a full function call protocol can cope with varargs!
	 */
	return idio_meaning_regular_application (e, es, nametree, tailp);
    } else {
	char *name = IDIO_PRIMITIVE_NAME (primdata);
	    
	switch (arity) {
	case 0:
	    {
		if (IDIO_STREQP (name, "read")) {
		    return IDIO_LIST2 (idio_I_PRIMCALL0, IDIO_FIXNUM (IDIO_A_PRIMCALL0_READ));
		} else if (IDIO_STREQP (name, "newline")) {
		    return IDIO_LIST2 (idio_I_PRIMCALL0, IDIO_FIXNUM (IDIO_A_PRIMCALL0_NEWLINE));
		} else {
		    break;
		}
	    }
	    break;
	case 1:
	    {
		IDIO m1 = idio_meaning (IDIO_PAIR_H (es), nametree, 0);
	    
		if (IDIO_STREQP (name, "car") ||
		    IDIO_STREQP (name, "ph")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_CAR), m1);
		} else if (IDIO_STREQP (name, "cdr") ||
			   IDIO_STREQP (name, "pt")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_CDR), m1);
		} else if (IDIO_STREQP (name, "pair?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_PAIRP), m1);
		} else if (IDIO_STREQP (name, "symbol?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_SYMBOLP), m1);
		} else if (IDIO_STREQP (name, "display")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_DISPLAY), m1);
		} else if (IDIO_STREQP (name, "primitive?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_PRIMITIVEP), m1);
		} else if (IDIO_STREQP (name, "null?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_NULLP), m1);
		} else if (IDIO_STREQP (name, "continuation?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_CONTINUATIONP), m1);
		} else if (IDIO_STREQP (name, "eof?")) {
		    return IDIO_LIST3 (idio_I_PRIMCALL1, IDIO_FIXNUM (IDIO_A_PRIMCALL1_EOFP), m1);
		} else {
		    break;
		}
	    }
	    break;
	case 2:
	    {
		IDIO m1 = idio_meaning (IDIO_PAIR_H (es), nametree, 0);
		IDIO m2 = idio_meaning (IDIO_PAIR_H (IDIO_PAIR_T (es)), nametree, 0);

		if (IDIO_STREQP (name, "cons") ||
		    IDIO_STREQP (name, "pair")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_CONS), m1, m2);
		} else if (IDIO_STREQP (name, "eq?")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_EQP), m1, m2);
		} else if (IDIO_STREQP (name, "set-car!") ||
			   IDIO_STREQP (name, "set-ph!")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_SET_CAR), m1, m2);
		} else if (IDIO_STREQP (name, "set-cdr!") ||
			   IDIO_STREQP (name, "set-pt!")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_SET_CDR), m1, m2);
		} else if (IDIO_STREQP (name, "+")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_ADD), m1, m2);
		} else if (IDIO_STREQP (name, "-")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_SUBTRACT), m1, m2);
		} else if (IDIO_STREQP (name, "=")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_EQ), m1, m2);
		} else if (IDIO_STREQP (name, "<") ||
			   IDIO_STREQP (name, "lt")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_LT), m1, m2);
		} else if (IDIO_STREQP (name, ">") ||
			   IDIO_STREQP (name, "gt")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_GT), m1, m2);
		} else if (IDIO_STREQP (name, "*")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_MULTIPLY), m1, m2);
		} else if (IDIO_STREQP (name, "<=") ||
			   IDIO_STREQP (name, "le")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_LE), m1, m2);
		} else if (IDIO_STREQP (name, ">=") ||
			   IDIO_STREQP (name, "ge")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_GE), m1, m2);
		} else if (IDIO_STREQP (name, "remainder")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_REMAINDER), m1, m2);
		} else if (IDIO_STREQP (name, "remainder")) {
		    return IDIO_LIST4 (idio_I_PRIMCALL2, IDIO_FIXNUM (IDIO_A_PRIMCALL2_REMAINDER), m1, m2);
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

    return idio_meaning_regular_application (e, es, nametree, tailp);
}

static IDIO idio_meaning_regular_application (IDIO e, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("imra: %s", e); */
    /* idio_debug (" es %s\n", es); */
    IDIO m;
    if (idio_isa_symbol (e)) {
	m = idio_meaning_function_reference (e, nametree, tailp);
    } else {
	m = idio_meaning (e, nametree, 0);
    }
    IDIO ms = idio_meanings (es, nametree, idio_list_length (es), 0);

    if (tailp) {
	return IDIO_LIST3 (idio_I_TR_REGULAR_CALL, m, ms);
    } else {
	return IDIO_LIST3 (idio_I_REGULAR_CALL, m, ms);
    }
}

static IDIO idio_meaning_application (IDIO e, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_symbol (e)) {
	IDIO k = idio_variable_kind (nametree, e);

	if (idio_isa_pair (k)) {
	    IDIO kt = IDIO_PAIR_H (k);

	    if (idio_S_predef == kt) {
		IDIO primdata = idio_get_primitive_data (e);

		if (idio_S_unspec != primdata) {

		    size_t arity = IDIO_PRIMITIVE_ARITY (primdata);
		    size_t nargs = idio_list_length (es);
		    /* fprintf (stderr, "primitive-application: %s nargs=%zd arity=%zd, varargs=%d\n", IDIO_PRIMITIVE_NAME (primdata),  nargs, arity, IDIO_PRIMITIVE_VARARGS (primdata)); */
		    
		    if ((IDIO_PRIMITIVE_VARARGS (primdata) &&
			 nargs >= arity) ||
			arity == nargs) {
			return idio_meaning_primitive_application (e, es, nametree, tailp, arity, IDIO_PAIR_H (IDIO_PAIR_T (k)));
		    } else {
			idio_error_static_primitive_arity ("wrong arity for primitive", e, es, primdata);
		    }
		}
	    }
	}
    }

    if (idio_isa_pair (e) &&
	idio_eqp (idio_S_function, IDIO_PAIR_H (e))) {
	return idio_meaning_closed_application (e, es, nametree, tailp);
    } else {
	return idio_meaning_regular_application (e, es, nametree, tailp);
    }

    return idio_undefined_code ("meaning-application: %s %s", idio_as_string (e, 1), idio_as_string (es, 1));
}

static IDIO idio_meaning_dynamic_reference (IDIO name, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    idio_ai_t index = idio_get_dynamic_index (name);

    if (index >= 0) {
	return IDIO_LIST2 (idio_I_DYNAMIC_REF, IDIO_FIXNUM (index));
    } else {
	idio_error_static_unbound (name);
	return idio_S_unspec;
    }
}

static IDIO idio_meaning_dynamic_let (IDIO name, IDIO e, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    idio_ai_t index = idio_get_dynamic_index (name);

    if (index >= 0) {
	IDIO m = idio_meaning (e, nametree, 0);
	IDIO mp = idio_meaning_sequence (ep, nametree, 0, idio_S_begin);

	return IDIO_LIST5 (m, idio_I_PUSH_DYNAMIC, IDIO_FIXNUM (index), mp, idio_I_POP_DYNAMIC);
    } else {
	idio_error_static_unbound (name);
	return idio_S_unspec;
    }
}

static IDIO idio_meaning_monitor (IDIO e, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_meaning (e, nametree, 0);
    IDIO mp = idio_meaning_sequence (ep, nametree, 0, idio_S_begin);

    return IDIO_LIST4 (m, IDIO_LIST1 (idio_I_PUSH_HANDLER), mp, IDIO_LIST1 (idio_I_POP_HANDLER));
}

static IDIO idio_meaning_include (IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    idio_thread_save_state (idio_current_thread ());
    idio_load_file (e);
    idio_thread_restore_state (idio_current_thread ());

    return IDIO_LIST1 (idio_I_NOP);
}

static IDIO idio_meaning_expander (IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning-expander: %s\n", e); */

    IDIO me = idio_macro_expand (e);
    
    /* idio_debug ("meaning-expander: %s\n", e); */
    /* idio_debug ("me=%s\n", me);  */

    /* if (idio_S_nil == me) { */
    /* 	fprintf (stderr, "meaning-expander: bad expansion?\n"); */
    /* } */
    
    return idio_meaning (me, nametree, tailp);
}

IDIO idio_meaning_operators (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("meaning-operators: %s\n", e); */

    if (idio_isa_pair (e)) {
	IDIO b = IDIO_LIST1 (IDIO_PAIR_H (e));
	e = IDIO_PAIR_T (e);

	while (idio_S_nil != e) {
	    IDIO h = IDIO_PAIR_H (e);

	    if (idio_isa_pair (h) &&
		idio_S_escape == IDIO_PAIR_H (h)) {
		h = IDIO_PAIR_H (IDIO_PAIR_T (h));
	    } else {
		IDIO opex = idio_operatorp (h);

		if (idio_S_false != opex) {
		    IDIO rhs =  idio_meaning_operators (IDIO_PAIR_T (e), depth + 1);
		    /* idio_debug ("meaning-operator:pre b %s: ", b); */
		    /* idio_debug ("rhs %s\n", rhs); */
		    b = idio_evaluate_operator (h, opex, b, rhs);
		    /* fprintf (stderr, "meaning-operator: depth %d: ", depth); */
		    /* idio_debug ("%s\n", b); */
		    if (0 && 0 == depth) {
			if (idio_isa_pair (b)) {
			    b = IDIO_PAIR_H (b);
			    idio_debug ("meaning-operator: b %s\n", b);
			}
		    }
		    break;
		}
	    }
	    b = idio_list_append2 (b, IDIO_LIST1 (h));
	    e = IDIO_PAIR_T (e);
	}
	e = b;
    }

    return e;
}

static IDIO idio_meaning (IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /* idio_debug ("meaning: e  in %s\n", e);  */
    
    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	IDIO et = IDIO_PAIR_T (e);

	if (idio_S_begin == eh ||
	    idio_S_and == eh ||
	    idio_S_or == eh) {
	    if (idio_isa_pair (et)) {
		return idio_meaning_sequence (et, nametree, tailp, eh);
	    } else if (idio_S_begin == eh) {
		return idio_meaning (idio_S_void, nametree, tailp);
	    } else if (idio_S_and == eh) {
		return idio_meaning (idio_S_true, nametree, tailp);
	    } else if (idio_S_or == eh) {
		return idio_meaning (idio_S_false, nametree, tailp);
	    } else {
		char *ehs = idio_as_string (eh, 1);
		idio_error_message ("unexpected sequence keyword: %s", ehs);
		free (ehs);
		IDIO_C_ASSERT (0);
	    }
	} else if (idio_S_quote == eh) {
	    /* (quote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quotation (IDIO_PAIR_H (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(quote)");
		return idio_S_unspec;
	    }
	} else if (idio_S_quasiquote == eh) {
	    /* (quasiquote x) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_quasiquotation (IDIO_PAIR_H (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(quasiquote)");
		return idio_S_unspec;
	    }
	} else if (idio_S_function == eh ||
		   idio_S_lambda == eh) {
	    /* (function bindings body ...) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_abstraction (IDIO_PAIR_H (et), IDIO_PAIR_T (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(function)");
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
		    return idio_meaning_alternative (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettth, nametree, tailp);
		} else {
		    idio_error_param_nil ("(if cond)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(if)");
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
		IDIO etc = idio_rewrite_cond (et);
		IDIO c = idio_meaning (etc, nametree, tailp);
		/* idio_debug ("cond in: %s\n", et); */
		/* idio_debug ("cond out: %s\n", c); */
		return c;
	    } else {
		idio_error_message ("cond clause*");
		return idio_S_unspec;
	    }
	} else if (idio_S_set == eh) {
	    /* (set! var expr) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_assignment (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, tailp);
		} else {
		    idio_error_param_nil ("(set! symbol)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(set!)");
		return idio_S_unspec;
	    }
	} else if (idio_S_define_macro == eh) {
	    /* (define-macro bindings body ...) */
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_meaning_define_macro (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, tailp);
		} else {
		    idio_error_param_nil ("(define-macro symbol)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define-macro)");
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
		    return idio_meaning_define (IDIO_PAIR_H (et), ett, nametree, tailp);
		} else {
		    idio_error_param_nil ("(define symbol)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define)");
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
		    return idio_meaning_define (IDIO_PAIR_H (et), ett, nametree, tailp);
		} else {
		    idio_error_param_nil ("(:= symbol)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(:=)");
		return idio_S_unspec;
	    }
	} else if (idio_S_block == eh) {
	    /*
	     * { ...}
	     */
	    if (idio_isa_pair (et)) {
		return idio_meaning_block (et, nametree, tailp);
	    } else {
		return idio_meaning (idio_S_void, nametree, tailp);
	    }
	} else if (idio_S_dynamic == eh) {
	    /* (dynamic var) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_dynamic_reference (IDIO_PAIR_H (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(dynamic)");
		return idio_S_unspec;
	    }
	} else if (idio_S_dynamic_let == eh) {
	    /* (dynamic-let (var expr) body) */
	    if (idio_isa_pair (et)) {
		IDIO eth = IDIO_PAIR_H (et);
		if (idio_isa_pair (eth)) {
		    IDIO etht = IDIO_PAIR_T (eth);
		    if (idio_isa_pair (etht)) {
			return idio_meaning_dynamic_let (IDIO_PAIR_H (eth), IDIO_PAIR_H (etht), IDIO_PAIR_T (et), nametree, tailp);
		    } else {
			idio_error_param_type ("pair", etht);
		    }
		} else {
		    idio_error_param_type ("pair", eth);
		}
	    } else {
		idio_error_param_nil ("(dynamic-let)");
		return idio_S_unspec;
	    }
	} else if (idio_S_monitor == eh) {
	    /* (monitor handler body ...) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_monitor (IDIO_PAIR_H (et), IDIO_PAIR_T (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(monitor)");
		return idio_S_unspec;
	    }
	} else if (idio_S_include == eh) {
	    /* (include filename) */
	    if (idio_isa_pair (et)) {
		return idio_meaning_include (IDIO_PAIR_H (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(include)");
		return idio_S_unspec;
	    }
	} else {
	    if (idio_isa_symbol (eh)) {
		IDIO k = idio_variable_kind (nametree, eh);

		if (idio_S_nil != k) {
		    if (idio_S_false != idio_expanderp (eh)) {
			return idio_meaning_expander (e, nametree, tailp);
		    }
		}
	    }
	    
	    return idio_meaning_application (eh, et, nametree, tailp);
	}
    } else {
	if (idio_isa_symbol (e)) {
	    return idio_meaning_reference (e, nametree, tailp);
	} else {
	    return idio_meaning_quotation (e, nametree, tailp);
	}
    }

    return idio_undefined_code ("meaning: %s", idio_as_string (e, 1));
}

IDIO idio_evaluate (IDIO e)
{
    /* idio_debug ("evaluate: e %s\n", e); */
    
    idio_gc_pause ();
    IDIO m = idio_meaning (e, idio_module_current_defined (), 1);
    idio_gc_resume ();

    /* idio_debug ("evaluate: m %s\n", m); */

    if (0) {
	IDIO d = idio_module_current_defined ();
	IDIO t = idio_module_current_symbols ();


	/*
	 * 1. Have we tried to use a name without having seen a definition
	 * for it?
	 */
	IDIO diff = idio_list_set_difference (t, d);
	if (idio_S_nil != diff) {
	    idio_warning_static_undefineds (diff);
	}

	size_t tl = idio_list_length (t);
	size_t dl = idio_list_length (d);
	if (tl > dl) {
	    fprintf (stderr, "evaluate: module=%s\n", IDIO_SYMBOL_S (IDIO_MODULE_NAME (idio_current_module ())));
	    /* idio_debug ("evaluate: e=%s\n", e); */

	    fprintf (stderr, "evaluate: after: %zd toplevel vars\n", tl);
	    fprintf (stderr, "evaluate: after: %zd defined vars\n", dl);

	    idio_debug ("diff t, d = %s\n", diff);

	    diff = idio_list_set_difference (d, t);

	    idio_debug ("diff d, t = %s\n", diff);

	    /* idio_debug ("t = %s\n", t); */

	    /* idio_debug ("d = %s\n", d); */

	    sleep (0);
	}
    }

    /* idio_debug ("meaning: => %s\n", m); */
    
    return m;
}

#define IDIO_DEFINE_ARITHMETIC_OPERATOR(iname,cname)			\
    IDIO_DEFINE_OPERATOR (iname, cname, (IDIO n, IDIO b, IDIO args))	\
    {									\
	IDIO_ASSERT (n);						\
	IDIO_ASSERT (b);						\
	IDIO_ASSERT (args);						\
									\
	/* idio_debug ("op: " #iname " args %s\n", args); */		\
	IDIO prefix = idio_S_nil;					\
	while (idio_S_nil != IDIO_PAIR_T (b)) {				\
	    prefix = idio_pair (IDIO_PAIR_H (b), prefix);		\
	    b = IDIO_PAIR_T (b);					\
	}								\
									\
	if (idio_S_nil != args) {					\
	    IDIO a = IDIO_PAIR_H (args);				\
	    if (idio_S_nil == a) {					\
		/* idio_error_message ("too few args after " #iname); */ \
		return IDIO_LIST2 (b, n);				\
	    }								\
									\
	    if (idio_S_nil == IDIO_PAIR_T (a)) {			\
		a = IDIO_PAIR_H (a);					\
	    }								\
									\
	    IDIO expr = IDIO_LIST3 (n, IDIO_PAIR_H (b), a);		\
            if (idio_S_nil != prefix) {					\
		expr = idio_list_append2 (idio_list_reverse (prefix), IDIO_LIST1 (expr)); \
	    }								\
									\
	    return expr;						\
	}								\
    									\
	return idio_S_unspec;						\
    }

IDIO_DEFINE_ARITHMETIC_OPERATOR ("+", add);
IDIO_DEFINE_ARITHMETIC_OPERATOR ("-", subtract);
IDIO_DEFINE_ARITHMETIC_OPERATOR ("*", multiply);
IDIO_DEFINE_ARITHMETIC_OPERATOR ("/", divide);

IDIO_DEFINE_OPERATOR ("=", set, (IDIO n, IDIO b, IDIO args))
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (b);
    IDIO_ASSERT (args);

    /* idio_debug ("op: = %s", n); */
    /* idio_debug (" %s", b); */
    /* idio_debug (" %s\n", args); */
    if (idio_S_nil != IDIO_PAIR_T (b)) {
	idio_error_message ("too many args before =");
    }

    if (idio_S_nil != args) {
	IDIO a = IDIO_PAIR_H (args);
	if (idio_S_nil == a) {
	    idio_error_message ("too few args after =");
	}
	if (idio_S_nil == IDIO_PAIR_T (a)) { 
	    a = IDIO_PAIR_H (a);
	}
	return IDIO_LIST3 (idio_S_set, IDIO_PAIR_H (b), a);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_OPERATOR (":=", colon_eq, (IDIO n, IDIO b, IDIO args))
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (b);
    IDIO_ASSERT (args);

    /* idio_debug ("op: := args %s\n", args); */
    if (idio_S_nil != IDIO_PAIR_T (b)) {
	idio_error_message ("too many args before :=");
    }

    if (idio_S_nil != args) {
	IDIO a = IDIO_PAIR_H (args);
	if (idio_S_nil == a) {
	    idio_error_message ("too few args after :=");
	}
	if (idio_S_nil == IDIO_PAIR_T (a)) { 
	    a = IDIO_PAIR_H (a);
	}
	return IDIO_LIST3 (idio_S_colon_eq, IDIO_PAIR_H (b), a);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_OPERATOR (":+", colon_plus, (IDIO n, IDIO b, IDIO args))
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (b);
    IDIO_ASSERT (args);

    /* idio_debug ("op: :+ args %s\n", args); */
    if (idio_S_nil != IDIO_PAIR_T (b)) {
	idio_error_message ("too many args before :+");
    }

    if (idio_S_nil != args) {
	IDIO a = IDIO_PAIR_H (args);
	if (idio_S_nil == a) {
	    idio_error_message ("too few args after :+");
	}
	if (idio_S_nil == IDIO_PAIR_T (a)) { 
	    a = IDIO_PAIR_H (a);
	}
	return IDIO_LIST3 (idio_S_colon_plus, IDIO_PAIR_H (b), a);
    }

    return idio_S_unspec;
}

void idio_init_evaluate ()
{
    idio_toplevel_names = idio_pair (idio_S_nil, idio_S_nil);
    idio_gc_protect (idio_toplevel_names);

    idio_predef_names = idio_pair (idio_S_nil, idio_S_nil);
    idio_gc_protect (idio_predef_names);

    idio_predef_values = idio_array (1);
    idio_gc_protect (idio_predef_values);

    idio_dynamic_names = idio_pair (idio_S_nil, idio_S_nil);
    idio_gc_protect (idio_dynamic_names);

    idio_evaluation_module = idio_module (idio_symbols_C_intern ("evaluation"));

    idio_expander_list = idio_symbols_C_intern ("*expander-list*");
    idio_module_set_symbol_value (idio_expander_list, idio_S_nil, idio_evaluation_module);

    IDIO_MODULE_EXPORTS (idio_evaluation_module) = idio_pair (idio_expander_list, IDIO_MODULE_EXPORTS (idio_evaluation_module));
    
    idio_expander_list_src = idio_symbols_C_intern ("*expander-list-src*");
    idio_module_set_symbol_value (idio_expander_list_src, idio_S_nil, idio_evaluation_module);

    idio_operator_list = idio_symbols_C_intern ("*operator-list*");
    idio_module_set_symbol_value (idio_operator_list, idio_S_nil, idio_evaluation_module);

}

void idio_evaluate_add_primitives ()
{
    idio_expander_thread = idio_thread (40);
    idio_gc_protect (idio_expander_thread);

    IDIO_THREAD_MODULE (idio_expander_thread) = idio_evaluation_module;
    IDIO_THREAD_PC (idio_expander_thread) = 1;

    IDIO_ADD_OPERATOR (add);
    IDIO_ADD_OPERATOR (subtract);
    IDIO_ADD_OPERATOR (multiply);
    IDIO_ADD_OPERATOR (divide);
    IDIO_ADD_OPERATOR (set);
    IDIO_ADD_OPERATOR (colon_eq);
    IDIO_ADD_OPERATOR (colon_plus);
}

void idio_final_evaluate ()
{
    idio_gc_expose (idio_toplevel_names);
    idio_gc_expose (idio_predef_names);
    idio_gc_expose (idio_predef_values);
    idio_gc_expose (idio_dynamic_names);
    idio_gc_expose (idio_expander_thread);
}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: undecided-unix */
/* End: */
