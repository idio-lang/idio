/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * pair.c
 *
 */

#include "idio.h"

IDIO idio_pair (IDIO h, IDIO t)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (t);

    IDIO p = idio_gc_get (IDIO_TYPE_PAIR);

    IDIO_PAIR_GREY (p) = NULL;
    IDIO_PAIR_H (p) = h;
    IDIO_PAIR_T (p) = t;

    return p;
}

IDIO_DEFINE_PRIMITIVE2_DS ("pair", pair, (IDIO h, IDIO t), "h t", "\
create a `pair` from `h` and `t`	\n\
")
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (t);

    return idio_pair (h, t);
}

int idio_isa_pair (IDIO p)
{
    IDIO_ASSERT (p);

    return idio_isa (p, IDIO_TYPE_PAIR);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pair?", pair_p, (IDIO o), "o", "\
test if `o` is a pair				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a pair, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_pair (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_list (IDIO p)
{
    IDIO_ASSERT (p);

    return (idio_isa_pair (p) ||
	    idio_S_nil == p);
}

void idio_free_pair (IDIO p)
{
    IDIO_ASSERT (p);
    IDIO_TYPE_ASSERT (pair, p);

    /* idio_gc_stats_free (sizeof (idio_pair_t)); */

    /* IDIO_GC_FREE (p->u.pair); */
}

IDIO idio_pair_set_head (IDIO p, IDIO v)
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (pair, p);

    IDIO_PAIR_H (p) = v;
    return idio_S_unspec;
}

IDIO idio_pair_set_tail (IDIO p, IDIO v)
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (pair, p);

    IDIO_PAIR_T (p) = v;
    return idio_S_unspec;
}

IDIO idio_list_head (IDIO p)
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, p);

    return IDIO_PAIR_H (p);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ph", pair_head, (IDIO p), "p", "\
return the head of pair `p`			\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
						\n\
:return: head of `p`				\n\
")
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_USER_TYPE_ASSERT (pair, p);

    return idio_list_head (p);
}

IDIO_DEFINE_PRIMITIVE2_DS ("set-ph!", set_pair_head, (IDIO p, IDIO v), "p v", "\
set the head of pair `p` to ``v``		\n\
						\n\
:param p: pair to modify			\n\
:type p: pair					\n\
:param v: value					\n\
:type v: any					\n\
						\n\
:return: #unspec				\n\
")
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO_PAIR_H (p) = v;

    return idio_S_unspec;
}

IDIO idio_list_tail (IDIO p)
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, p);

    return IDIO_PAIR_T (p);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pt", pair_tail, (IDIO p), "p", "\
return the tail of pair `p`			\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
						\n\
:return: tail of `p`				\n\
")
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_USER_TYPE_ASSERT (pair, p);

    return idio_list_tail (p);
}

IDIO_DEFINE_PRIMITIVE2_DS ("set-pt!", set_pair_tail, (IDIO p, IDIO v), "p v", "\
set the tail of pair `p` to ``v``		\n\
						\n\
:param p: pair to modify			\n\
:type p: pair					\n\
:param v: value					\n\
:type v: any					\n\
						\n\
:return: #unspec				\n\
")
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO_PAIR_T (p) = v;

    return idio_S_unspec;
}

void idio_list_bind (IDIO *list, size_t nargs, ...)
{
    IDIO_ASSERT (*list);

    IDIO_TYPE_ASSERT (pair, *list);

    va_list ap;
    va_start (ap, nargs);

    size_t i;
    for (i = 0; i < nargs; i++) {
	IDIO *arg = va_arg (ap, IDIO *);
	*arg = idio_list_head (*list);
	*list = idio_list_tail (*list);
	IDIO_ASSERT (*arg);
    }

    va_end (ap);
}

IDIO idio_improper_list_reverse (IDIO l, IDIO last)
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (last);

    if (idio_S_nil == l) {
	/*
	 * An empty improper list, ie. "( . last)" is invalid and we
	 * shouldn't have gotten here, otherwise we're here because
	 * we're reversing an ordinary list in which case the result
	 * is nil
	 */
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, l);

    IDIO r = last;

    while (idio_S_nil != l) {
	IDIO h = idio_list_head (l);
	r = idio_pair (h, r);

	l = idio_list_tail (l);
    }

    return r;
}

IDIO idio_list_reverse (IDIO l)
{
    IDIO_ASSERT (l);

    return idio_improper_list_reverse (l, idio_S_nil);
}

IDIO_DEFINE_PRIMITIVE1_DS ("reverse", list_reverse, (IDIO l), "l", "\
reverse the list ``l``				\n\
						\n\
:param l: list to reverse			\n\
:type l: list					\n\
						\n\
:return: reversed list				\n\
")
{
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_reverse (l);
}

IDIO idio_list_to_array (IDIO l)
{
    IDIO_ASSERT (l);

    IDIO_TYPE_ASSERT (list, l);

    size_t ll = idio_list_length (l);

    IDIO r = idio_array (ll);

    size_t li = 0;
    while (idio_S_nil != l) {
	idio_array_insert_index (r, idio_list_head (l), li);
	l = idio_list_tail (l);
	li++;
    }

    return r;
}

size_t idio_list_length (IDIO l)
{
    IDIO_ASSERT (l);

    if (idio_S_nil == l) {
	return 0;
    }

    IDIO_TYPE_ASSERT (pair, l);

    size_t len = 0;

    while (idio_S_nil != l) {
	len++;
	l = idio_list_tail (l);
    }

    return len;
}

IDIO_DEFINE_PRIMITIVE1_DS ("length", list_length, (IDIO l), "l", "\
return the number of elements in list ``l``	\n\
						\n\
:param l: list to count				\n\
:type l: list					\n\
						\n\
:return: number of elements in ``l``		\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    size_t len = idio_list_length (l);

    return idio_integer (len);
}

IDIO idio_copy_pair (IDIO p, int depth)
{
    IDIO_ASSERT (p);
    IDIO_C_ASSERT (depth);

    if (idio_S_nil == p) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, p);

    if (IDIO_COPY_SHALLOW == depth) {
	return idio_pair (IDIO_PAIR_H (p), IDIO_PAIR_T (p));
    } else {
	return idio_pair (idio_copy (IDIO_PAIR_H (p), depth), idio_copy (IDIO_PAIR_T (p), depth));
    }
}

IDIO idio_list_copy (IDIO l)
{
    IDIO_ASSERT (l);

    if (idio_S_nil == l) {
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (pair, l);

    /*
     * Building a copy as you walk it then reversing gets too messy
     * for the reverse function if the list is improper.
     *
     * Let's do the modify-in-place variant
     */

    IDIO p = idio_pair (idio_S_nil, idio_S_nil);
    IDIO r = p;

    while (idio_S_nil != l) {
	IDIO h = IDIO_PAIR_H (l);
	IDIO t = IDIO_PAIR_T (l);

	if (0 && idio_S_nil == h &&
	    idio_S_nil == t) {
	    /* (1 2 3 #nil) */
	    break;
	}

	IDIO_PAIR_H (p) = h;

	if (idio_S_nil == t) {
	    break;
	}

	if (idio_isa_pair (t)) {
	    IDIO np = idio_pair (idio_S_nil, idio_S_nil);
	    IDIO_PAIR_T (p) = np;
	    p = np;
	    l = t;
	} else {
	    IDIO_PAIR_T (p) = t;
	    break;
	}
    }

    return r;
}

IDIO idio_list_append2 (IDIO l1, IDIO l2)
{
    IDIO_ASSERT (l1);
    IDIO_ASSERT (l2);

    if (idio_S_nil == l1) {
	return l2;
    }

    IDIO_TYPE_ASSERT (pair, l1);

    IDIO r = idio_S_nil;
    IDIO p = idio_S_nil;

    for (;;l1 = IDIO_PAIR_T (l1)) {
	if (idio_S_nil == l1) {
	    break;
	}
	if (! idio_isa_pair (l1)) {
	    idio_error_C ("not a list:", l1, IDIO_C_FUNC_LOCATION ());
	}

	IDIO t = idio_pair (IDIO_PAIR_H (l1), idio_S_nil);

	if (idio_S_nil == r) {
	    r = t;
	} else {
	    IDIO_PAIR_T (p) = t;
	}
	p = t;
    }

    IDIO_PAIR_T (p) = l2;

    return r;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("list", list, (IDIO args), "args", "\
return ``args`` as a list			\n\
						\n\
:param args: arguments to convert		\n\
						\n\
:return: list of ``args``			\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (args);

    return args;
}

IDIO_DEFINE_PRIMITIVE2_DS ("append", append, (IDIO a, IDIO b), "a b", "\
append list ``b`` to list ``a``			\n\
						\n\
list ``a`` is copied, list ``b`` is untouched	\n\
						\n\
:param a: list to be appended to		\n\
:type a: list					\n\
:param b: list to be appended			\n\
:type b: list					\n\
						\n\
:return: combined list				\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    if (idio_S_nil == a) {
	return b;
    }

    IDIO_USER_TYPE_ASSERT (list, a);

    if (idio_S_nil == b) {
	return a;
    }

    return idio_list_append2 (a, b);
}

IDIO idio_list_map_ph (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_H (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_map_pt (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_T (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_memq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	if (idio_eqp (k, IDIO_PAIR_H (l))) {
	    return l;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("memq", memq, (IDIO k, IDIO l), "k l", "\
return the remainder of the list `l` from the	\n\
first incidence of an element eq? `k`		\n\
or #f if `k` is not in `l`			\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
						\n\
:return: a list starting from `k`, #f if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_memq (k, l);
}

IDIO idio_list_memv (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	if (idio_eqvp (k, IDIO_PAIR_H (l))) {
	    return l;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("memv", memv, (IDIO k, IDIO l), "k l", "\
return the remainder of the list `l` from the	\n\
first incidence of an element eqv? `k`		\n\
or #f if `k` is not in `l`			\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
						\n\
:return: a list starting from `k`, #f if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_memv (k, l);
}

IDIO idio_list_member (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	if (idio_equalp (k, IDIO_PAIR_H (l))) {
	    return l;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("member", member, (IDIO k, IDIO l), "k l", "\
return the remainder of the list `l` from the	\n\
first incidence of an element equal? `k`	\n\
or #f if `k` is not in `l`			\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
						\n\
:return: a list starting from `k`, #f if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_member (k, l);
}

IDIO idio_list_assq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	IDIO p = IDIO_PAIR_H (l);

	if (idio_S_nil == p) {
	    return idio_S_false;
	}

	if (! idio_isa_pair (p)) {
	    idio_error_C ("not a pair in list", IDIO_LIST2 (p, l), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	if (idio_eqp (k, IDIO_PAIR_H (p))) {
	    return p;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("assq", assq, (IDIO k, IDIO l), "k l", "\
return the first entry of association list `l`	\n\
with a key eq? `k`				\n\
or #f if `k` is not a key in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: association list to search in		\n\
:type l: list					\n\
						\n\
:return: the list (`k` & value), #f if `k` is not a key in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_assq (k, l);
}

IDIO idio_list_assv (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	IDIO p = IDIO_PAIR_H (l);

	if (idio_S_nil == p) {
	    return idio_S_false;
	}

	if (! idio_isa_pair (p)) {
	    idio_error_C ("not a pair in list", IDIO_LIST2 (p, l), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	if (idio_eqvp (k, IDIO_PAIR_H (p))) {
	    return p;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("assv", assv, (IDIO k, IDIO l), "k l", "\
return the first entry of association list `l`	\n\
with a key eqv? `k`				\n\
or #f if `k` is not a key in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: association list to search in		\n\
:type l: list					\n\
						\n\
:return: the list (`k` & value), #f if `k` is not a key in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_assv (k, l);
}

IDIO idio_list_assoc (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	IDIO p = IDIO_PAIR_H (l);

	if (idio_S_nil == p) {
	    return idio_S_false;
	}

	if (! idio_isa_pair (p)) {
	    idio_error_C ("not a pair in list", IDIO_LIST2 (p, l), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	if (idio_equalp (k, IDIO_PAIR_H (p))) {
	    return p;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2_DS ("assoc", assoc, (IDIO k, IDIO l), "k l", "\
return the first entry of association list `l`	\n\
with a key equal? `k`				\n\
or #f if `k` is not a key in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: association list to search in		\n\
:type l: list					\n\
						\n\
:return: the list (`k` & value), #f if `k` is not a key in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_assoc (k, l);
}

IDIO idio_list_set_difference (IDIO set1, IDIO set2)
{
    if (idio_isa_pair (set1)) {
	if (idio_S_false != idio_list_memq (IDIO_PAIR_H (set1), set2)) {
	    return idio_list_set_difference (IDIO_PAIR_T (set1), set2);
	} else {
	    return idio_pair (IDIO_PAIR_H (set1),
			      idio_list_set_difference (IDIO_PAIR_T (set1), set2));
	}
    } else {
	if (idio_S_nil != set1) {
	    idio_error_C ("set1", set1, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	return idio_S_nil;
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("list->array", list2array, (IDIO l), "l", "\
convert list ``l`` to an array			\n\
						\n\
:param l: list to be converted			\n\
:type l: list					\n\
						\n\
:return: array					\n\
:rtype: array					\n\
")
{
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_to_array (l);
}

IDIO idio_list_nth (IDIO l, IDIO I_n, IDIO args)
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (I_n);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (list, l);

    intmax_t C_n = -1;

    if (idio_isa_fixnum (I_n)) {
	C_n = IDIO_FIXNUM_VAL (I_n);
    } else if (idio_isa_bignum (I_n)) {
	if (IDIO_BIGNUM_INTEGER_P (I_n)) {
	    C_n = idio_bignum_ptrdiff_value (I_n);
	} else {
	    IDIO bit_i = idio_bignum_real_to_integer (I_n);
	    if (idio_S_nil == bit_i) {
		idio_error_param_type ("integer", I_n, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		C_n = idio_bignum_ptrdiff_value (bit_i);
	    }
	}
    } else {
	idio_error_param_type ("integer", I_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_nil;

    IDIO_TYPE_ASSERT (list, args);

    if (idio_S_nil != args) {
	r = IDIO_PAIR_H (args);
    }

    while (idio_S_nil != l) {
	if (1 == C_n) {
	    r = IDIO_PAIR_H (l);
	    break;
	}

	C_n--;
	l = IDIO_PAIR_T (l);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2V_DS ("nth", nth, (IDIO l, IDIO I_n, IDIO args), "l n [default]", "\
return the nth (`n`) element from list `l`		\n\
							\n\
:param l: list						\n\
:type orig: list					\n\
:param n: nth element					\n\
:type n: integer					\n\
:param default: (optional) default value to return	\n\
:return: the element or `default` if supplied or #n	\n\
:rtype: array						\n\
")
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (I_n);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_nth (l, I_n, args);
}

void idio_pair_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (pair_p);
    IDIO_ADD_PRIMITIVE (pair);
    IDIO_ADD_PRIMITIVE (pair_head);
    IDIO_ADD_PRIMITIVE (pair_tail);
    IDIO_ADD_PRIMITIVE (set_pair_head);
    IDIO_ADD_PRIMITIVE (set_pair_tail);

    IDIO_ADD_PRIMITIVE (list_reverse);
    IDIO_ADD_PRIMITIVE (list_length);
    IDIO_ADD_PRIMITIVE (list);
    IDIO_ADD_PRIMITIVE (append);
    IDIO_ADD_PRIMITIVE (memq);
    IDIO_ADD_PRIMITIVE (memv);
    IDIO_ADD_PRIMITIVE (member);
    IDIO_ADD_PRIMITIVE (assq);
    IDIO_ADD_PRIMITIVE (assv);
    IDIO_ADD_PRIMITIVE (assoc);
    IDIO_ADD_PRIMITIVE (list2array);
    IDIO_ADD_PRIMITIVE (nth);
}

void idio_init_pair ()
{
    idio_module_table_register (idio_pair_add_primitives, NULL);
}

