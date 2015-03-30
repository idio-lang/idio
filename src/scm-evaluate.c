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
 * scm-evaluate.c
 * 
 */

#include "idio.h"

/*
 * toplevel and global environments:

 *   these are flat therefore the corresponding namelists can be
 *   association lists to (idio_S_toplevel i) or (idio_S_predef i)
 *   where i is the index into the corresponding array of values.

 * local environments:

 *   these are a hierarchy of (flat) environments corresponing to a
 *   hierarchy of new scopes as new blocks are entered.  So we have
 *   lists of association lists of the names to (idio_S_local i j)
 *   where i is the ith association list and j is the jth variable in
 *   that frame

 */

static IDIO idio_scm_predef_names = idio_S_nil;
static IDIO idio_scm_predef_values = idio_S_nil;

static IDIO idio_scm_toplevel_names = idio_S_nil;
static IDIO idio_scm_toplevel_values = idio_S_nil;

static IDIO idio_scm_defined_names = idio_S_nil;

IDIO idio_primitive_hash = idio_S_nil;

/*
 * Expanders (aka macros) live in their own little world...
 */

static IDIO idio_scm_evaluation_module = idio_S_nil;
static IDIO idio_scm_expander_list = idio_S_nil;
static IDIO idio_scm_expander_list_src = idio_S_nil;

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
    
    fprintf (stderr, "redefinition of %s\n", IDIO_SYMBOL_S (name));
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
    
    fprintf (stderr, "%s is unbound\n", IDIO_SYMBOL_S (name));
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

static void idio_error_static_primitive_arity (char *m, IDIO f, IDIO args, IDIO desc)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (symbol, f);
    IDIO_TYPE_ASSERT (list, args);
    
    idio_error_message ("%s: arity (%s) is %zd%s: passed %s", m, idio_as_string (f, 2), IDIO_PRIMITIVE_ARITY (desc), IDIO_PRIMITIVE_VARARGS (desc) ? "+" : "", idio_as_string (args, 2));
}

static IDIO idio_scm_undefined_code (char *format, ...)
{
    char *s;
    
    va_list fmt_args;
    va_start (fmt_args, format);
    vasprintf (&s, format, fmt_args);
    va_end (fmt_args);

    IDIO r = IDIO_LIST2 (idio_S_undef, idio_string_C (s));
    free (s);

    sleep (5);
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

static IDIO idio_predef_extend (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO pd_names = idio_module_current_symbol_value (idio_scm_predef_names);
    size_t gl = idio_list_length (pd_names);
    IDIO i = IDIO_FIXNUM (gl);
    
    idio_module_set_current_symbol_value (idio_scm_predef_names, idio_pair (IDIO_LIST3 (name, idio_S_predef, i),
									    pd_names));

    return i;
}

static IDIO idio_predef_ref (IDIO i)
{
    IDIO_ASSERT (i);
    IDIO_TYPE_ASSERT (fixnum, i);
    
    return idio_array_get_index (idio_module_current_symbol_value (idio_scm_predef_values), IDIO_FIXNUM_VAL (i));
}

void idio_add_description (IDIO sym, IDIO desc)
{
    idio_hash_put (idio_primitive_hash, sym, desc);
}

IDIO idio_get_description (IDIO sym)
{
    return idio_hash_get (idio_primitive_hash, sym);
}

void idio_add_primitive (idio_primitive_t *d)
{
    IDIO desc = idio_primitive_desc (d);
    IDIO sym = idio_symbols_C_intern (d->name);
    idio_add_description (sym, desc);
    idio_module_set_primitive_symbol_value (sym, desc);
    idio_predef_extend (sym);
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

static IDIO idio_toplevel_ref (IDIO i)
{
    IDIO_ASSERT (i);
    IDIO_TYPE_ASSERT (fixnum, i);
    
    return idio_array_get_index (idio_module_current_symbol_value (idio_scm_toplevel_values), IDIO_FIXNUM_VAL (i));
}

static void idio_toplevel_update (IDIO i, IDIO v)
{
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (fixnum, i);
    
    idio_array_insert_index (idio_module_current_symbol_value (idio_scm_toplevel_values), v, IDIO_FIXNUM_VAL (i));
}

static IDIO idio_toplevel_extend (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO tl_names = idio_module_current_symbol_value (idio_scm_toplevel_names);
    
    size_t gl = idio_list_length (tl_names);
    IDIO i = IDIO_FIXNUM (gl);
    
    idio_module_set_current_symbol_value (idio_scm_toplevel_names, idio_pair (IDIO_LIST3 (name, idio_S_toplevel, i),
									      tl_names));

    return i;
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

    if (idio_S_nil == names) {
	/* (lambda nil ...) */
	return nametree;
    }
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
	r = idio_variable_toplevelp (idio_module_current_symbol_value (idio_scm_toplevel_names), name);
	if (idio_S_nil == r) {
	    r = idio_variable_predefp (idio_module_current_symbol_value (idio_scm_predef_names), name);
	}
    }

    return r;
}

static void idio_predef_init (IDIO nametree, IDIO name)
{
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO k = idio_variable_kind (nametree, name);

    if (idio_S_nil == k) {
	IDIO i = idio_predef_extend (name);
	idio_array_insert_index (idio_module_current_symbol_value (idio_scm_predef_values), idio_S_undef, IDIO_FIXNUM_VAL (i));
    } else {
	if (idio_S_predef == IDIO_PAIR_H (k)) {
	    idio_array_insert_index (idio_module_current_symbol_value (idio_scm_predef_values), idio_S_undef, IDIO_FIXNUM_VAL (IDIO_PAIR_T (k)));
	} else {
	    idio_error_static_redefine (name);
	}
    }
}

static void idio_toplevel_init (IDIO nametree, IDIO name)
{
    IDIO_ASSERT (nametree);
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (list, nametree);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO k = idio_variable_kind (nametree, name);

    if (idio_S_nil == k) {
	IDIO i = idio_toplevel_extend (name);
	idio_array_insert_index (idio_module_current_symbol_value (idio_scm_toplevel_values), idio_S_undef, IDIO_FIXNUM_VAL (i));
    } else {
	if (idio_S_toplevel == IDIO_PAIR_H (k)) {
	    idio_array_insert_index (idio_module_current_symbol_value (idio_scm_toplevel_values), idio_S_undef, IDIO_FIXNUM_VAL (IDIO_PAIR_T (k)));
	} else {
	    idio_error_static_redefine (name);
	}
    }
}

static IDIO idio_scm_expanderp (IDIO name)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);

    return idio_list_assq (name, idio_module_symbol_value (idio_scm_expander_list, idio_scm_evaluation_module));
}

static IDIO idio_scm_initial_expander (IDIO e);

static IDIO idio_scm_application_expander (IDIO expr)
{
    IDIO_ASSERT (expr);
    IDIO_TYPE_ASSERT (list, expr);

    /* (map* (lambda (y) (e y e)) expr) */
    IDIO r = idio_S_nil;
    
    for (;;) {
	if (idio_isa_pair (expr)) {
	    IDIO exprh = IDIO_PAIR_H (expr);
	    if (idio_S_nil == exprh) {
		r = idio_pair (idio_S_nil, r);
		break;
	    } else {
		r = idio_pair (idio_scm_initial_expander (IDIO_PAIR_H (expr)), r);
		expr = IDIO_PAIR_T (expr);
	    }
	} else {
	    fprintf (stderr, "application-expander: recursing!\n");
	    r = idio_pair (idio_scm_initial_expander (expr), r);
	    break;
	}
    }

    return idio_list_reverse (r);
}

static IDIO idio_scm_initial_expander (IDIO e)
{
    IDIO_ASSERT (e);
    IDIO_TYPE_ASSERT (list, e);

    if (! idio_isa_pair (e)) {
	return e;
    }

    IDIO eh = IDIO_PAIR_H (e);

    if (! idio_isa_symbol (eh)) {
	return idio_scm_application_expander (e);
    } else {
	if (idio_S_false != idio_scm_expanderp (eh)) {
	    /*
	     * apply the macro!
	     *
	     * ((cdr (assq functor *expander-list*)) x e)
	     */
	    fprintf (stderr, "apply-macro -> nil\n");
	    return idio_S_nil;
	} else {
	    return idio_scm_application_expander (e);
	}
    }
}

static void idio_scm_install_expander (IDIO id, IDIO proc, IDIO code)
{
    IDIO el = idio_module_symbol_value (idio_scm_expander_list, idio_scm_evaluation_module);
    IDIO old = idio_list_assq (id, el);

    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_scm_expander_list,
				      idio_pair (idio_pair (id, proc),
						 el),
				      idio_scm_evaluation_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }

    IDIO els = idio_module_symbol_value (idio_scm_expander_list_src, idio_scm_evaluation_module);
    old = idio_list_assq (id, els);
    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_scm_expander_list_src,
				      idio_pair (idio_pair (id, proc),
						 els),
				      idio_scm_evaluation_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

static void idio_scm_push_expander (IDIO id, IDIO proc)
{
    idio_module_set_symbol_value (idio_scm_expander_list,
				  idio_pair (idio_pair (id, proc),
					     idio_module_symbol_value (idio_scm_expander_list, idio_scm_evaluation_module)),
				  idio_scm_evaluation_module);
}

static void idio_scm_delete_expander (IDIO id)
{
    IDIO el = idio_module_symbol_value (idio_scm_expander_list, idio_scm_evaluation_module);
    IDIO prv = idio_S_false;

    for (;;) {
	if (idio_S_nil == el) {
	    return;
	} else if (idio_eqp (IDIO_PAIR_H (IDIO_PAIR_H (el)), id)) {
	    if (idio_S_false == prv) {
		idio_module_set_symbol_value (idio_scm_expander_list,
					      IDIO_PAIR_T (el),
					      idio_scm_evaluation_module);
		return;
	    } else {
		IDIO_PAIR_T (prv) = IDIO_PAIR_T (el);
		return;
	    }
	}
	prv = el;
	el = IDIO_PAIR_T (el);
    }

    IDIO_C_ASSERT (0);
    return;
}

static IDIO idio_scm_macro_expand (IDIO e)
{
    IDIO_ASSERT (e);

    return idio_scm_initial_expander (e);
}

static IDIO idio_scm_meaning (IDIO e, IDIO nametree, int tailp);

static IDIO idio_scm_meaning_reference (IDIO name, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO k = idio_variable_kind (nametree, name);

    if (idio_S_nil == k) {
	idio_warning_static_unbound (name);
	return idio_scm_undefined_code ("meaning-reference: %s", idio_as_string (name, 1));
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
	return IDIO_LIST2 (idio_I_PREDEFINED, i);
    } else {
	idio_error_static_unbound (name);
	return idio_scm_undefined_code ("meaning-reference: %s", idio_as_string (name, 1));
    }
}

static IDIO idio_scm_meaning_quotation (IDIO v, IDIO nametree, int tailp)
{
    IDIO_ASSERT (v);

    return IDIO_LIST2 (idio_I_CONSTANT, v);
}

static IDIO idio_scm_meaning_alternative (IDIO e1, IDIO e2, IDIO e3, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e1);
    IDIO_ASSERT (e2);
    IDIO_ASSERT (e3);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m1 = idio_scm_meaning (e1, nametree, 0);
    IDIO m2 = idio_scm_meaning (e2, nametree, tailp);
    IDIO m3 = idio_scm_meaning (e3, nametree, tailp);
    
    return IDIO_LIST4 (idio_I_ALTERNATIVE, m1, m2, m3);
}

static IDIO idio_scm_rewrite_cond (IDIO c)
{
    if (idio_S_nil == c) {
	return idio_S_unspec;
    } else if (! idio_isa_pair (c)) {
	idio_error_param_type ("pair", c);
	return idio_scm_undefined_code ("cond: %s", idio_as_string (c, 1));
    } else if (! idio_isa_pair (IDIO_PAIR_H (c))) {
	idio_error_param_type ("pair/pair", c);
	return idio_scm_undefined_code ("cond: %s", idio_as_string (c, 1));
    } else if (idio_S_else == IDIO_PAIR_H (IDIO_PAIR_H (c))) {
	if (idio_S_nil == IDIO_PAIR_T (c)) {
	    return IDIO_LIST2 (idio_S_begin, IDIO_PAIR_T (IDIO_PAIR_H (c)));
	} else {
	    return idio_scm_undefined_code ("cond: else not in last clause", idio_as_string (c, 1));
	}
    } else if (idio_isa_pair (IDIO_PAIR_T (IDIO_PAIR_H (c))) &&
	       idio_S_eq_gt == IDIO_PAIR_H ((IDIO_PAIR_T (IDIO_PAIR_H (c))))) {
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
					   idio_scm_rewrite_cond (IDIO_PAIR_T (c))));
	} else {
	    idio_error_param_type ("=>", c);
	    return idio_scm_undefined_code ("cond: => bad format", idio_as_string (c, 1));
	}
    } else if (idio_S_nil == IDIO_PAIR_T (IDIO_PAIR_H (c))) {
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
				       idio_scm_rewrite_cond (IDIO_PAIR_T (c))));
    } else {
	return IDIO_LIST4 (idio_S_if,
			   IDIO_PAIR_H (IDIO_PAIR_H (c)),
			   IDIO_LIST2 (idio_S_begin, IDIO_PAIR_T (IDIO_PAIR_H (c))),
			   idio_scm_rewrite_cond (IDIO_PAIR_T (c)));
    }
}

static IDIO idio_scm_meaning_assignment (IDIO name, IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_scm_meaning (e, nametree, 0);
    IDIO k = idio_variable_kind (nametree, name);

    if (idio_S_nil == k) {
	IDIO d = idio_list_memq (name, idio_module_current_symbol_value (idio_scm_defined_names));
	IDIO i = idio_S_nil;
	if (idio_S_false == d) {
	    i = idio_toplevel_extend (name);
	} else {
	    i = idio_variable_toplevelp (idio_module_current_symbol_value (idio_scm_toplevel_names), name);
	}
	if (idio_S_nil == i) {
	    fprintf (stderr, "meaning-assignment: %s not yet defined\n", idio_as_string (name, 1));
	}
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
	    return IDIO_LIST4 (idio_I_DEEP_ARGUMENT_SET, i, j, m);
	}
    } else if (idio_S_toplevel == kt) {
	return IDIO_LIST3 (idio_I_GLOBAL_SET, i, m);
    } else if (idio_S_predef == kt) {
	idio_error_static_immutable (name);
	return idio_S_unspec;
    } else {
	idio_error_static_unbound (name);
	return idio_S_unspec;
    }
}

static IDIO idio_scm_meaning_define (IDIO name, IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /*
     * (define (func arg) ...) => (define func (lambda (arg) ...))
     */
    if (idio_isa_pair (name)) {
	e = IDIO_LIST3 (idio_S_lambda, IDIO_PAIR_T (name), e);
	name = IDIO_PAIR_H (name);
    }

    IDIO defined = idio_module_current_symbol_value (idio_scm_defined_names);
    IDIO d = idio_list_memq (name, defined);

    if (idio_isa_pair (d)) {
	idio_warning_static_redefine (name);
	return idio_S_unspec;
    } else {
	idio_module_set_current_symbol_value (idio_scm_defined_names, idio_pair (name, defined));
    }

    if (idio_S_nil == nametree) {
	idio_toplevel_extend (name);
    } else {
	idio_toplevel_extend (name);
    }

    return idio_scm_meaning_assignment (name, e, nametree, tailp);
}

static IDIO idio_scm_meaning_define_macro (IDIO name, IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /*
     * (define-macro (func arg) ...) => (define-macro func (lambda (arg) ...))
     */
    if (idio_isa_pair (name)) {
	e = IDIO_LIST3 (idio_S_lambda, IDIO_PAIR_T (name), e);
	name = IDIO_PAIR_H (name);
    }

    IDIO defined = idio_module_current_symbol_value (idio_scm_defined_names);
    IDIO d = idio_list_memq (name, defined);

    if (idio_isa_pair (d)) {
	idio_warning_static_redefine (name);
	return idio_S_unspec;
    } else {
	idio_module_set_current_symbol_value (idio_scm_defined_names, idio_pair (name, defined));
    }

    if (idio_S_nil == nametree) {
	idio_toplevel_extend (name);
    } else {
	idio_toplevel_extend (name);
    }

    idio_scm_install_expander (name, e, e);

    return idio_scm_meaning_assignment (name, e, nametree, tailp);
}

static IDIO idio_scm_meaning_sequence (IDIO ep, IDIO nametree, int tailp);

static IDIO idio_scm_meanings_single_sequence (IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return idio_scm_meaning (e, nametree, tailp);
}

static IDIO idio_scm_meanings_multiple_sequence (IDIO e, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_scm_meaning (e, nametree, 0);
    IDIO mp = idio_scm_meaning_sequence (ep, nametree, tailp);
    
    return IDIO_LIST2 (m, mp);
}

static IDIO idio_scm_meaning_sequence (IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (ep)) {
	IDIO eph = IDIO_PAIR_H (ep);
	IDIO ept = IDIO_PAIR_T (ep);

	if (idio_isa_pair (ept)) {
	    return idio_scm_meanings_multiple_sequence (eph, ept, nametree, tailp);
	} else {
	    return idio_scm_meanings_single_sequence (eph, nametree, tailp);
	}
    }

    /*
     * We can get here for the x in the bindings of

     * (define (list . x) x)
     */
    return idio_scm_meaning (ep, nametree, tailp);
}

static IDIO idio_scm_meaning_fix_abstraction (IDIO ns, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, ns);

    IDIO mp = idio_scm_meaning_sequence (ep, nt2, 1);

    return IDIO_LIST3 (idio_I_FIX_CLOSURE, mp, IDIO_FIXNUM (arity));
}

static IDIO idio_scm_meaning_dotted_abstraction (IDIO ns, IDIO n, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    size_t arity = idio_list_length (ns);
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append (ns, IDIO_LIST1 (n)));
    IDIO mp = idio_scm_meaning_sequence (ep, nt2, 1);

    return IDIO_LIST3 (idio_I_NARY_CLOSURE, mp, IDIO_FIXNUM (arity));
}

static IDIO idio_scm_meaning_abstraction (IDIO nns, IDIO ep, IDIO nametree, int tailp)
{
    IDIO_ASSERT (nns);
    IDIO_ASSERT (ep);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO ns = nns;
    IDIO regular = idio_S_nil;

    for (;;) {
	if (idio_isa_pair (ns))  {
	    regular = idio_pair (IDIO_PAIR_H (ns), regular);
	    ns = IDIO_PAIR_T (ns);
	} else if (idio_S_nil == ns) {
	    return idio_scm_meaning_fix_abstraction (nns, ep, nametree, tailp);
	} else {
	    return idio_scm_meaning_dotted_abstraction (idio_list_reverse (regular), ns, ep, nametree, tailp);
	}
    }

    return idio_scm_undefined_code ("meaning-abstraction: %s %s", idio_as_string (nns, 1), idio_as_string (ep, 1));
}

static IDIO idio_scm_meanings (IDIO es, IDIO nametree, size_t size, int tailp);

static IDIO idio_scm_meaning_some_arguments (IDIO e, IDIO es, IDIO nametree, size_t size, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_scm_meaning (e, nametree, 0);
    IDIO ms = idio_scm_meanings (es, nametree, size, tailp);
    size_t rank = size - (idio_list_length (es) + 1);

    return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, IDIO_FIXNUM (rank));
}

static IDIO idio_scm_meaning_no_argument (IDIO nametree, size_t size, int tailp)
{
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, IDIO_FIXNUM (size));
}

static IDIO idio_scm_meanings (IDIO es, IDIO nametree, size_t size, int tailp)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (es)) {
	return idio_scm_meaning_some_arguments (IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, tailp);
    } else {
	return idio_scm_meaning_no_argument (nametree, size, tailp);
    }
}

static IDIO idio_scm_meaning_fix_closed_application (IDIO ns, IDIO body, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO ms = idio_scm_meanings (es, nametree, idio_list_length (es), 0);
    IDIO nt2 = idio_nametree_extend (nametree, ns);
    IDIO mbody = idio_scm_meaning_sequence (body, nt2, tailp);

    if (tailp) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_scm_meaning_dotteds (IDIO es, IDIO nametree, size_t size, size_t arity, int tailp);

static IDIO idio_scm_meaning_some_dotted_arguments (IDIO e, IDIO es, IDIO nametree, size_t size, size_t arity, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_scm_meaning (e, nametree, 0);
    IDIO ms = idio_scm_meaning_dotteds (es, nametree, size, arity, tailp);
    size_t rank = size - (idio_list_length (es) + 1);

    if (rank < arity) {
	return IDIO_LIST4 (idio_I_STORE_ARGUMENT, m, ms, IDIO_FIXNUM (rank));
    } else {
	return IDIO_LIST4 (idio_I_CONS_ARGUMENT, m, ms, IDIO_FIXNUM (arity));
    }
}

static IDIO idio_scm_meaning_no_dotted_argument (IDIO nametree, size_t size, size_t arity, int tailp)
{
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    return IDIO_LIST2 (idio_I_ALLOCATE_FRAME, IDIO_FIXNUM (arity));
}

static IDIO idio_scm_meaning_dotteds (IDIO es, IDIO nametree, size_t size, size_t arity, int tailp)
{
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    if (idio_isa_pair (es)) {
	return idio_scm_meaning_some_dotted_arguments (IDIO_PAIR_H (es), IDIO_PAIR_T (es), nametree, size, arity, tailp);
    } else {
	return idio_scm_meaning_no_dotted_argument (nametree, size, arity, tailp);
    }
}

static IDIO idio_scm_meaning_dotted_closed_application (IDIO ns, IDIO n, IDIO body, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (ns);
    IDIO_ASSERT (n);
    IDIO_ASSERT (body);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO ms = idio_scm_meaning_dotteds (es, nametree, idio_list_length (es), idio_list_length (ns), 0);
    IDIO nt2 = idio_nametree_extend (nametree, idio_list_append (ns, IDIO_LIST1 (n)));
    IDIO mbody = idio_scm_meaning_sequence (body, nt2, tailp);

    if (tailp) {
	return IDIO_LIST3 (idio_I_TR_FIX_LET, ms, mbody);
    } else {
	return IDIO_LIST3 (idio_I_FIX_LET, ms, mbody);
    }
}

static IDIO idio_scm_meaning_closed_application (IDIO e, IDIO ees, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (ees);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    /*
     * ((lambda ...) args)
     *
     * therefore (car e) == 'lambda
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
		return idio_scm_meaning_fix_closed_application (nns, IDIO_PAIR_T (et), ees, nametree, tailp);
	    } else {
		idio_error_static_arity ("too many arguments", IDIO_LIST2 (e, ees));
		return idio_S_unspec;
	    }
	} else {
	    return idio_scm_meaning_dotted_closed_application (idio_list_reverse (regular), ns, IDIO_PAIR_T (et), ees, nametree, tailp);
	}
    }

    return idio_scm_undefined_code ("meaning-closed-application: %s %s", idio_as_string (e, 1), idio_as_string (ees, 1));
}

static IDIO idio_scm_meaning_regular_application (IDIO e, IDIO es, IDIO nametree, int tailp);

static IDIO idio_scm_meaning_primitive_application (IDIO e, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (symbol, e);
    IDIO_TYPE_ASSERT (list, es);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO desc = idio_get_description (e);
    if (idio_S_unspec == desc) {
	idio_error_message ("unable to find primitive description for %s", idio_as_string (e, 4));
	return idio_S_unspec;
    }

    size_t size = idio_list_length (es);

    switch (size) {
    case 0:
	return IDIO_LIST2 (idio_I_CALL0, desc);
    case 1:
	{
	    IDIO m1 = idio_scm_meaning (IDIO_PAIR_H (es), nametree, 0);
	    return IDIO_LIST3 (idio_I_CALL1, desc, m1);
	}
    case 2:
	{
	    IDIO m1 = idio_scm_meaning (IDIO_PAIR_H (es), nametree, 0);
	    IDIO m2 = idio_scm_meaning (IDIO_PAIR_H (IDIO_PAIR_T (es)), nametree, 0);
	    return IDIO_LIST4 (idio_I_CALL2, desc, m1, m2);
	}
    case 4:
	{
	    IDIO m1 = idio_scm_meaning (IDIO_PAIR_H (es), nametree, 0);
	    IDIO m2 = idio_scm_meaning (IDIO_PAIR_H (IDIO_PAIR_T (es)), nametree, 0);
	    IDIO m3 = idio_scm_meaning (IDIO_PAIR_H (IDIO_PAIR_T (IDIO_PAIR_T (es))), nametree, 0);
	    return IDIO_LIST5 (idio_I_CALL3, desc, m1, m2, m3);
	}
    default:
	return idio_scm_meaning_regular_application (e, es, nametree, tailp);
    }
}

static IDIO idio_scm_meaning_regular_application (IDIO e, IDIO es, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (es);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    IDIO m = idio_scm_meaning (e, nametree, 0);
    IDIO ms = idio_scm_meanings (es, nametree, idio_list_length (es), 0);

    if (tailp) {
	return IDIO_LIST3 (idio_I_TR_REGULAR_CALL, m, ms);
    } else {
	return IDIO_LIST3 (idio_I_REGULAR_CALL, m, ms);
    }
}

static IDIO idio_scm_meaning_application (IDIO e, IDIO es, IDIO nametree, int tailp)
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
		IDIO desc = idio_get_description (e);

		if (idio_S_unspec != desc) {

		    size_t nargs = idio_list_length (es);
		    fprintf (stderr, "nargs=%zd arity=%zd, varargs=%d\n", nargs, IDIO_PRIMITIVE_ARITY (desc), IDIO_PRIMITIVE_VARARGS (desc));
		    
		    if (((IDIO_PRIMITIVE_VARARGS (desc) &&
			  nargs >= IDIO_PRIMITIVE_ARITY (desc))) ||
			(IDIO_PRIMITIVE_ARITY (desc) == nargs)) {
			return idio_scm_meaning_primitive_application (e, es, nametree, tailp);
		    } else {
			idio_error_static_primitive_arity ("wrong arity for primitive", e, es, desc);
		    }
		}
	    }
	}
    }

    if (idio_isa_pair (e) &&
	idio_eqp (idio_S_lambda, IDIO_PAIR_H (e))) {
	return idio_scm_meaning_closed_application (e, es, nametree, tailp);
    } else {
	return idio_scm_meaning_regular_application (e, es, nametree, tailp);
    }

    return idio_scm_undefined_code ("meaning-application: %s %s", idio_as_string (e, 1), idio_as_string (es, 1));
}

static IDIO idio_scm_meaning (IDIO e, IDIO nametree, int tailp)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (nametree);
    IDIO_TYPE_ASSERT (list, nametree);

    fprintf (stderr, "meaning: %s\n", idio_as_string (e, 1));
    if (idio_isa_pair (e)) {
	IDIO eh = IDIO_PAIR_H (e);
	IDIO et = IDIO_PAIR_T (e);

	if (idio_S_begin == eh ||
	    idio_S_and == eh ||
	    idio_S_or == eh) {
	    if (idio_isa_pair (et)) {
		return idio_scm_meaning_sequence (et, nametree, tailp);
	    } else if (idio_S_begin == eh) {
		return idio_S_unspec;
	    } else if (idio_S_and == eh) {
		return idio_S_true;
	    } else if (idio_S_or == eh) {
		return idio_S_false;
	    } else {
		IDIO_C_ASSERT (0);
	    }
	} else if (idio_S_quote == eh) {
	    if (idio_isa_pair (et)) {
		return idio_scm_meaning_quotation (IDIO_PAIR_H (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(quote)");
		return idio_S_unspec;
	    }
	} else if (idio_S_lambda == eh) {
	    if (idio_isa_pair (et)) {
		return idio_scm_meaning_abstraction (IDIO_PAIR_H (et), IDIO_PAIR_T (et), nametree, tailp);
	    } else {
		idio_error_param_nil ("(lambda)");
		return idio_S_unspec;
	    }
	} else if (idio_S_if == eh) {
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    IDIO ettt = IDIO_PAIR_T (ett);
		    IDIO ettth = idio_S_nil;
		    if (idio_isa_pair (ettt)) {
			ettth = IDIO_PAIR_H (ettt);
		    }
		    return idio_scm_meaning_alternative (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), ettth, nametree, tailp);
		} else {
		    idio_error_param_nil ("(if cond)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(if)");
		return idio_S_unspec;
	    }
	} else if (idio_S_cond == eh) {
	    IDIO c = idio_scm_meaning (idio_scm_rewrite_cond (et), nametree, tailp);
	    fprintf (stderr, "cond: %s\n%s\n", idio_as_string (et, 1), idio_as_string (c, 1));
	    return c;
	} else if (idio_S_set == eh) {
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_scm_meaning_assignment (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, tailp);
		} else {
		    idio_error_param_nil ("(set! symbol)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(set!)");
		return idio_S_unspec;
	    }
	} else if (idio_S_define_macro == eh) {
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_scm_meaning_define_macro (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, tailp);
		} else {
		    idio_error_param_nil ("(define-macro symbol)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define-macro)");
		return idio_S_unspec;
	    }
	} else if (idio_S_define == eh) {
	    if (idio_isa_pair (et)) {
		IDIO ett = IDIO_PAIR_T (et);
		if (idio_isa_pair (ett)) {
		    return idio_scm_meaning_define (IDIO_PAIR_H (et), IDIO_PAIR_H (ett), nametree, tailp);
		} else {
		    idio_error_param_nil ("(define symbol)");
		    return idio_S_unspec;
		}
	    } else {
		idio_error_param_nil ("(define)");
		return idio_S_unspec;
	    }
	} else {
	    if (idio_isa_symbol (eh)) {
		IDIO k = idio_variable_kind (nametree, eh);

		if (idio_S_nil != k) {
		    if (idio_S_false != idio_scm_expanderp (eh)) {
			IDIO me = idio_scm_macro_expand (e);
			fprintf (stderr, "macro-expand:\ne=%s\nme=%s\n", idio_as_string (e, 4), idio_as_string (me, 4));
			return idio_scm_meaning (me, nametree, tailp);
		    }
		}
	    }
	    
	    return idio_scm_meaning_application (eh, et, nametree, tailp);
	}
    } else {
	if (idio_isa_symbol (e)) {
	    return idio_scm_meaning_reference (e, nametree, tailp);
	} else {
	    return idio_scm_meaning_quotation (e, nametree, tailp);
	}
    }

    return idio_scm_undefined_code ("meaning: %s", idio_as_string (e, 1));
}

IDIO idio_scm_evaluate (IDIO e)
{
    IDIO m = idio_scm_meaning (e, idio_S_nil, 1);
    IDIO d = idio_module_current_symbol_value (idio_scm_defined_names);
    IDIO t = idio_list_mapcar (idio_module_current_symbol_value (idio_scm_toplevel_names));
    IDIO diff = idio_list_set_difference (t, d);
    if (idio_S_nil == diff) {
	size_t tl = idio_list_length (t);
	IDIO tv = idio_module_current_symbol_value (idio_scm_toplevel_values);
	size_t tvl = idio_array_size (tv);

	if (tl < tvl) {
	    /* what sorcery is this? */
	    IDIO_C_ASSERT (0);
	}

	if (tl > tvl) {
	    fprintf (stderr, "scm-evaluate: growing toplevel_values by %zd to %zd\n", tl - tvl, tl);
	    IDIO tv2 = idio_array_copy (tv, tl - tvl);
	    idio_module_set_current_symbol_value (idio_scm_toplevel_values, tv2);
	    idio_index_t i = tvl;
	    for (; i < tl; i++) {
		idio_array_insert_index (tv2, idio_S_undef, i);
	    }
	}
    }
    size_t tl = idio_list_length (t);
    size_t dl = idio_list_length (d);
    if (tl != dl) {
	fprintf (stderr, "scm-evaluate: after: %zd toplevel vars\n", tl);
	fprintf (stderr, "scm-evaluate: after: %zd defined vars\n", dl);
	fprintf (stderr, "diff t, d = %s\n", idio_as_string (diff, 1));
	diff = idio_list_set_difference (d, t);
	fprintf (stderr, "diff d, t = %s\n", idio_as_string (diff, 1));
	fprintf (stderr, "t = %s\n", idio_as_string (t, 1));
	fprintf (stderr, "d = %s\n", idio_as_string (d, 1));
	IDIO_C_ASSERT (0);
    }
    return m;
}

IDIO_DEFINE_PRIMITIVE2V ("apply", apply, (IDIO p, IDIO a))
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (a);

    idio_error_message ("apply: shouldn't be here!");
    return idio_S_unspec;
}

void idio_init_scm_evaluate ()
{
    idio_scm_toplevel_names = idio_symbols_C_intern ("g.current");
    idio_module_set_current_symbol_value (idio_scm_toplevel_names, idio_S_nil);

    idio_scm_toplevel_values = idio_symbols_C_intern ("sg.current");
    idio_module_set_current_symbol_value (idio_scm_toplevel_values, idio_array (1));

    idio_scm_predef_names = idio_symbols_C_intern ("g.init");
    idio_module_set_current_symbol_value (idio_scm_predef_names, idio_S_nil);

    idio_scm_predef_values = idio_symbols_C_intern ("sg.init");
    idio_module_set_current_symbol_value (idio_scm_predef_values, idio_array (1));

    idio_scm_defined_names = idio_symbols_C_intern ("*defined*");
    idio_module_set_current_symbol_value (idio_scm_defined_names, idio_S_nil);

    idio_scm_evaluation_module = idio_module (idio_symbols_C_intern ("SCM.evaluation"));
    idio_scm_expander_list = idio_symbols_C_intern ("*expander-list*");
    idio_module_set_symbol_value (idio_scm_expander_list, idio_S_nil, idio_scm_evaluation_module);
    
    idio_scm_expander_list_src = idio_symbols_C_intern ("*expander-list-src*");
    idio_module_set_symbol_value (idio_scm_expander_list_src, idio_S_nil, idio_scm_evaluation_module);

    IDIO_ADD_PRIMITIVE (apply);
}

void idio_final_scm_evaluate ()
{
}

/* Local Variables: */
/* mode: C/l */
/* buffer-file-coding-system: undecided-unix */
/* End: */
