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
 * expander.c
 * 
 */

#include "idio.h"

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

static IDIO idio_initial_expander (IDIO x, IDIO e);

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
	    idio_meaning_error_static_redefine ("evaluator value change", name, pd, primdata, IDIO_C_LOCATION ("idio_evaluator_extend"));
	}
	
	return fvi;
    }

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

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
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);

    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);
    return idio_evaluator_extend (sym, primdata, module);
}

void idio_add_expander_primitive (idio_primitive_desc_t *d, IDIO cs)
{
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    idio_add_primitive (d, cs); 
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

IDIO idio_evaluate_expander_source (IDIO x, IDIO e)
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
	idio_meaning_error_static_arity ("let: wrong arguments", e, IDIO_C_LOCATION ("let"));
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
	idio_meaning_error_static_arity ("let*: wrong arguments", e, IDIO_C_LOCATION ("let*"));
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
	idio_meaning_error_static_arity ("letrec: wrong arguments", e, IDIO_C_LOCATION ("letrec"));
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

IDIO idio_expanderp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO expander_list = idio_module_symbol_value (idio_expander_list, idio_expander_module, idio_S_nil);
    
    IDIO assq = idio_list_assq (name, expander_list);

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {

	    IDIO lv = idio_module_current_symbol_value_recurse (name, idio_S_nil);

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

	if (idio_S_false == e) {
	    r = idio_pair (mcar,
			   idio_application_expander (mcdr, e));
	} else {
	    r = idio_pair (idio_initial_expander (mcar, e),
			   idio_application_expander (mcdr, e));
	}
    } else {
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
	return idio_application_expander (x, e);
    } else {
	IDIO expander = idio_expanderp (xh);
	if (idio_S_false != expander) {
	    /*
	     * apply the macro!
	     *
	     * ((cdr (assq functor *expander-list*)) x e)
	     */
	    return idio_apply (IDIO_PAIR_T (expander), IDIO_LIST3 (x, e, idio_S_nil));
	} else {
	    return idio_application_expander (x, e);
	}
    }
}

void idio_install_expander (IDIO id, IDIO proc)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);
    
    IDIO el = idio_module_symbol_value (idio_expander_list, idio_expander_module, idio_S_nil);
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

void idio_install_expander_source (IDIO id, IDIO proc, IDIO code)
{
    /* idio_debug ("install-expander-source: %s\n", id); */

    idio_install_expander (id, proc);

    IDIO els = idio_module_symbol_value (idio_expander_list_src, idio_expander_module, idio_S_nil);
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

IDIO idio_evaluate_expander_code (IDIO m, IDIO cs)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    /* idio_debug ("evaluate-expander-code: %s\n", m); */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_codegen (ethr, m, cs);
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

IDIO idio_macro_expand (IDIO e)
{
    IDIO_ASSERT (e);

    IDIO r = idio_evaluate_expander_source (e, idio_S_unspec);

    return r;
}

IDIO_DEFINE_PRIMITIVE0V ("macro-expand", macro_expand, (IDIO x))
{
    IDIO_ASSERT (x);

    return idio_macro_expand (x);
}

IDIO idio_macro_expands (IDIO e)
{
    IDIO_ASSERT (e);

    for (;;) {
	IDIO new = idio_evaluate_expander_source (e, idio_S_false);
	
	if (idio_equalp (new, e)) {
	    return new;
	} else {
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

    IDIO ol = idio_module_symbol_value (idio_infix_operator_list, idio_operator_module, idio_S_nil);
    IDIO op = idio_list_assq (id, ol);

    if (idio_S_false == op) {
	idio_module_set_symbol_value (idio_infix_operator_list, 
				      idio_pair (idio_pair (id, proc),
						 ol), 
				      idio_operator_module); 
    } else {
	IDIO_PAIR_T (op) = proc;
    }

    IDIO og = idio_module_symbol_value (idio_infix_operator_group, idio_operator_module, idio_S_nil);

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

IDIO idio_evaluate_infix_operator_code (IDIO m, IDIO cs)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    /* idio_debug ("evaluate-infix-operator-code: %s\n", m);   */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_codegen (ethr, m, cs);
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

    IDIO operator_list = idio_module_symbol_value (idio_infix_operator_list, idio_operator_module, idio_S_nil);
    
    IDIO assq = idio_list_assq (name, operator_list);

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    IDIO lv = idio_module_current_symbol_value_recurse (name, idio_S_nil);
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

    IDIO ol = idio_module_symbol_value (idio_postfix_operator_list, idio_operator_module, idio_S_nil);
    IDIO op = idio_list_assq (id, ol);

    if (idio_S_false == op) {
	idio_module_set_symbol_value (idio_postfix_operator_list, 
				      idio_pair (idio_pair (id, proc),
						 ol), 
				      idio_operator_module); 
    } else {
	IDIO_PAIR_T (op) = proc;
    }

    IDIO og = idio_module_symbol_value (idio_postfix_operator_group, idio_operator_module, idio_S_nil);

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

IDIO idio_evaluate_postfix_operator_code (IDIO m, IDIO cs)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    /* idio_debug ("evaluate-postfix-operator-code: %s\n", m);   */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */
    
    idio_thread_set_current_thread (ethr);
    idio_thread_save_state (ethr);
    idio_vm_default_pc (ethr);

    idio_codegen (ethr, m, cs);
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

    IDIO operator_list = idio_module_symbol_value (idio_postfix_operator_list, idio_operator_module, idio_S_nil);
    
    IDIO assq = idio_list_assq (name, operator_list);

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    IDIO lv = idio_module_current_symbol_value_recurse (name, idio_S_nil);
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

IDIO idio_infix_operator_expand (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("infix-operator-expand:   %s\n", e); */

    if (idio_isa_pair (e)) {
	IDIO og = idio_module_symbol_value (idio_infix_operator_group, idio_operator_module, idio_S_nil);
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
	IDIO pog = idio_module_symbol_value (idio_postfix_operator_group, idio_operator_module, idio_S_nil);
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

void idio_init_expander ()
{
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

void idio_expander_add_primitives ()
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
}

void idio_final_expander ()
{
    idio_gc_expose (idio_expander_thread);
}
