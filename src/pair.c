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
 * pair.c
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
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

IDIO idio_pair (IDIO h, IDIO t)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (t);

    IDIO p = idio_gc_get (IDIO_TYPE_PAIR);
    p->vtable = idio_vtable (IDIO_TYPE_PAIR);

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
:return: ``#t`` if `o` is a pair, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_pair (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("list", list, (IDIO args), "args", "\
return `args` as a list				\n\
						\n\
:param args: arguments to convert		\n\
:return: list of `args`				\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (args);

    return args;
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

}

/*
 * idio_list_head() is slightly more lenient when passed #n
 */
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
:return: head of `p`				\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/ph-bad-type.idio
     *
     * ph #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    return IDIO_PAIR_H (p);
}

IDIO_DEFINE_PRIMITIVE2_DS ("set-ph!", set_pair_head, (IDIO p, IDIO v), "p v", "\
set the head of pair `p` to `v`			\n\
						\n\
:param p: pair to modify			\n\
:type p: pair					\n\
:param v: value					\n\
:type v: any					\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    /*
     * Test Case: pair-errors/set-ph-bad-type.idio
     *
     * set-ph! #t #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO_PAIR_H (p) = v;

    return idio_S_unspec;
}

/*
 * idio_list_tail() is slightly more lenient when passed #n
 */
IDIO idio_list_tail (IDIO p)
{
    IDIO_ASSERT (p);

    if (idio_S_nil == p) {
	/*
	 * Code coverage:
	 *
	 * Seemingly, nobody calls idio_list_tail (idio_S_nil).
	 */
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
:return: tail of `p`				\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/pt-bad-type.idio
     *
     * pt #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    return IDIO_PAIR_T (p);
}

IDIO_DEFINE_PRIMITIVE2_DS ("set-pt!", set_pair_tail, (IDIO p, IDIO v), "p v", "\
set the tail of pair `p` to `v`			\n\
						\n\
:param p: pair to modify			\n\
:type p: pair					\n\
:param v: value					\n\
:type v: any					\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (p);
    IDIO_ASSERT (v);

    /*
     * Test Case: pair-errors/set-pt-bad-type.idio
     *
     * set-pt! #t #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO_PAIR_T (p) = v;

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("phh", pair_hh, (IDIO p), "p", "\
return :samp:`(ph (ph {p}))`			\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: head of the head of `p`		\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/phh-bad-type-1.idio
     *
     * phh #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_H (p);

    /*
     * Test Case: pair-errors/phh-bad-type-2.idio
     *
     * phh '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_H (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pht", pair_ht, (IDIO p), "p", "\
return :samp:`(ph (pt {p}))`			\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: head of the tail of `p`		\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/pht-bad-type-1.idio
     *
     * pht #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_T (p);

    /*
     * Test Case: pair-errors/pht-bad-type-2.idio
     *
     * pht '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_H (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pth", pair_th, (IDIO p), "p", "\
return :samp:`(pt (ph {p}))`			\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: tail of the head of `p`		\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/pth-bad-type-1.idio
     *
     * pth #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_H (p);

    /*
     * Test Case: pair-errors/pth-bad-type-2.idio
     *
     * pth '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("ptt", pair_tt, (IDIO p), "p", "\
return :samp:`(pt (pt {p}))`			\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: tail of the tail of `p`		\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/ptt-bad-type-1.idio
     *
     * ptt #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_T (p);

    /*
     * Test Case: pair-errors/ptt-bad-type-2.idio
     *
     * ptt '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("phhh", pair_hhh, (IDIO p), "p", "\
return :samp:`(ph (ph (ph {p})))`		\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: head of the head of the head of `p`	\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/phhh-bad-type-1.idio
     *
     * phhh #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_H (p);

    /*
     * Test Case: pair-errors/phhh-bad-type-2.idio
     *
     * phhh '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_H (r);

    /*
     * Test Case: pair-errors/phhh-bad-type-3.idio
     *
     * phhh '((#t))
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_H (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("phth", pair_hth, (IDIO p), "p", "\
return :samp:`(ph (pt (ph {p})))`		\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: head of the tail of the head of `p`	\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/phth-bad-type-1.idio
     *
     * phth #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_H (p);

    /*
     * Test Case: pair-errors/phth-bad-type-2.idio
     *
     * phth '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    /*
     * Test Case: pair-errors/phth-bad-type-3.idio
     *
     * phth '((#t))
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_H (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("phtt", pair_htt, (IDIO p), "p", "\
return :samp:`(ph (pt (pt {p})))`		\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: head of the tail of the tail of `p`	\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/phtt-bad-type-1.idio
     *
     * phtt #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_T (p);

    /*
     * Test Case: pair-errors/phtt-bad-type-2.idio
     *
     * phtt '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    /*
     * Test Case: pair-errors/phtt-bad-type-3.idio
     *
     * phtt '(#t (#t))
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_H (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("ptth", pair_tth, (IDIO p), "p", "\
return :samp:`(pt (pt (ph {p})))`		\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: tail of the tail of the head of `p`	\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/ptth-bad-type-1.idio
     *
     * ptth #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_H (p);

    /*
     * Test Case: pair-errors/ptth-bad-type-2.idio
     *
     * ptth '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    /*
     * Test Case: pair-errors/ptth-bad-type-3.idio
     *
     * ptth '((#t))
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("pttt", pair_ttt, (IDIO p), "p", "\
return :samp:`(pt (pt (pt {p})))`		\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: tail of the tail of the tail of `p`	\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/pttt-bad-type-1.idio
     *
     * pttt #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_T (p);

    /*
     * Test Case: pair-errors/pttt-bad-type-2.idio
     *
     * pttt '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    /*
     * Test Case: pair-errors/pttt-bad-type-3.idio
     *
     * pttt '(#t (#t))
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("phtth", pair_htth, (IDIO p), "p", "\
return :samp:`(ph (pt (pt (ph {p}))))`		\n\
						\n\
:param p: pair to query				\n\
:type p: pair					\n\
:return: head of the tail of the tail of the head of `p`	\n\
")
{
    IDIO_ASSERT (p);

    /*
     * Test Case: pair-errors/phtth-bad-type-1.idio
     *
     * phtth #t
     */
    IDIO_USER_TYPE_ASSERT (pair, p);

    IDIO r = IDIO_PAIR_H (p);

    /*
     * Test Case: pair-errors/phtth-bad-type-2.idio
     *
     * phtth '(#t)
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    /*
     * Test Case: pair-errors/phtth-bad-type-3.idio
     *
     * phtth '((#t))
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_T (r);

    /*
     * Test Case: pair-errors/phtth-bad-type-4.idio
     *
     * phtth '((#t #t))
     */
    IDIO_USER_TYPE_ASSERT (pair, r);

    r = IDIO_PAIR_H (r);

    return r;
}

IDIO idio_listv (size_t nargs, ...)
{
    if (0 == nargs) {
	return idio_S_nil;
    }

    IDIO r = idio_S_nil;
    IDIO p = r;

    va_list ap;
    va_start (ap, nargs);

    size_t i;
    for (i = 0; i < nargs; i++) {
	IDIO arg = va_arg (ap, IDIO);
	IDIO c = idio_pair (arg, idio_S_nil);
	if (idio_S_nil == r) {
	    r = c;
	} else {
	    IDIO_PAIR_T (p) = c;
	}

	p = c;
    }

    va_end (ap);

    return r;
}

/*
 * Code coverage:
 *
 * I did use this for some dubious task which I now forget.  However,
 * it feels suitably dubious that I'm leaving it in as a reminder.
 */
void idio_list_bind (IDIO *list, size_t const nargs, ...)
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
reverse the list `l`				\n\
						\n\
:param l: list to reverse			\n\
:type l: list					\n\
:return: reversed list				\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/reverse-bad-type.idio
     *
     * reverse #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_reverse (l);
}

IDIO idio_list_nreverse (IDIO l)
{
    IDIO_ASSERT (l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO_TYPE_ASSERT (pair, l);

	IDIO t = IDIO_PAIR_T (l);
	IDIO_PAIR_T (l) = r;
	r = l;
	l = t;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("reverse!", list_nreverse, (IDIO l), "l", "\
reverse the list `l` destructively		\n\
						\n\
:param l: list to reverse			\n\
:type l: list					\n\
:return: reversed list				\n\
						\n\
Calling ``reverse!`` on a list that others are	\n\
referencing may have undesired effects:		\n\
						\n\
.. code-block:: idio-console			\n\
						\n\
   Idio> lst := '(1 2 3)			\n\
   (1 2 3)					\n\
   Idio> reverse! lst				\n\
   (3 2 1)					\n\
   Idio> lst					\n\
   (1)						\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/nreverse-bad-type.idio
     *
     * reverse! #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_nreverse (l);
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

IDIO_DEFINE_PRIMITIVE1_DS ("list->array", list2array, (IDIO l), "l", "\
convert list `l` to an array			\n\
						\n\
:param l: list to be converted			\n\
:type l: list					\n\
:return: array					\n\
:rtype: array					\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/list2array-bad-type.idio
     *
     * list->array #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_to_array (l);
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
return the number of elements in list `l`	\n\
						\n\
:param l: list to count				\n\
:type l: list					\n\
:return: number of elements in `l`		\n\
:rtype: integer					\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/length-bad-type.idio
     *
     * length #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    size_t len = idio_list_length (l);

    return idio_integer (len);
}

IDIO idio_copy_pair (IDIO p, int depth)
{
    IDIO_ASSERT (p);
    IDIO_C_ASSERT (depth);

    if (idio_S_nil == p) {
	return p;
    }

    IDIO_TYPE_ASSERT (pair, p);

    if (IDIO_COPY_SHALLOW == depth) {
	return idio_pair (IDIO_PAIR_H (p), IDIO_PAIR_T (p));
    } else {
	/*
	 * Uncomfortably similar to idio_list_append2() below but will
	 * handle improper lists
	 */
	IDIO l1 = p;

	IDIO r = idio_S_nil;
	IDIO p = idio_S_nil;

	while (idio_isa_pair (l1)) {
	    IDIO t = idio_pair (IDIO_PAIR_H (l1), idio_S_nil);

	    if (idio_S_nil == r) {
		r = t;
	    } else {
		IDIO_PAIR_T (p) = t;
	    }
	    p = t;

	    l1 = IDIO_PAIR_T (l1);
	}

	IDIO_PAIR_T (p) = l1;

	return r;
    }
}

/*
 * idio_list_append2() is useful for C code but doesn't handle
 * improper lists.
 */
IDIO idio_list_append2 (IDIO l1, IDIO l2)
{
    IDIO_ASSERT (l1);
    IDIO_ASSERT (l2);

    if (idio_S_nil == l1) {
	return l2;
    }

    IDIO r = idio_S_nil;
    IDIO p = idio_S_nil;

    while (idio_isa_pair (l1)) {
	IDIO t = idio_pair (IDIO_PAIR_H (l1), idio_S_nil);

	if (idio_S_nil == r) {
	    r = t;
	} else {
	    IDIO_PAIR_T (p) = t;
	}
	p = t;

	l1 = IDIO_PAIR_T (l1);
    }

    if (idio_S_nil != l1) {
	/*
	 * Code coverage:
	 *
	 * Coding error, probably.  The primitive, below, is
	 * overridden quite early on.
	 */
	fprintf (stderr, "append2: improper list\n");
    }

    IDIO_PAIR_T (p) = l2;

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("append", append, (IDIO a, IDIO b), "a b", "\
append list `b` to list `a`			\n\
						\n\
list `a` is copied, list `b` is untouched	\n\
						\n\
:param a: list to be appended to		\n\
:type a: list					\n\
:param b: list to be appended			\n\
:return: combined list				\n\
:rtype: list					\n\
")
{
    IDIO_ASSERT (a);
    IDIO_ASSERT (b);

    if (idio_S_nil == a) {
	return b;
    }

    /*
     * Test Case: pair-errors/append-bad-type.idio
     *
     * Idio/append #t #t
     *
     * NB append is redefined to be more list-y
     */
    IDIO_USER_TYPE_ASSERT (list, a);

    if (idio_S_nil == b) {
	return a;
    }

    return idio_list_append2 (a, b);
}

IDIO idio_list_ph_of (IDIO l)
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

    return idio_list_nreverse (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("ph-of", ph_of, (IDIO l), "l", "\
return the remainder of the list `l` from the	\n\
first incidence of an element :ref:`eq? <eq?>`	\n\
`k` or ``#f`` if `k` is not in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
:return: a list starting from `k`, ``#f`` if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/ph-of-bad-list-type.idio
     *
     * ph-of #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_ph_of (l);
}

IDIO idio_list_pt_of (IDIO l)
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

    return idio_list_nreverse (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pt-of", pt_of, (IDIO l), "l", "\
return the remainder of the list `l` from the	\n\
first incidence of an element :ref:`eq? <eq?>`	\n\
`k` or ``#f`` if `k` is not in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
:return: a list starting from `k`, ``#f`` if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/pt-of-bad-list-type.idio
     *
     * pt-of #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_pt_of (l);
}

IDIO_DEFINE_PRIMITIVE1_DS ("any-null?", any_nullp, (IDIO l), "l", "\
return the remainder of the list `l` from the	\n\
first incidence of an element :ref:`eq? <eq?>`	\n\
`k` or ``#f`` if `k` is not in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
:return: a list starting from `k`, ``#f`` if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/any-nullp-bad-list-type.idio
     *
     * any-null? #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	if (idio_S_nil == IDIO_PAIR_H (l)) {
	    return idio_S_true;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
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
first incidence of an element :ref:`eq? <eq?>`	\n\
`k` or ``#f`` if `k` is not in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
:return: a list starting from `k`, ``#f`` if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/memq-bad-list-type.idio
     *
     * memq #t #t
     */
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
first incidence of an element :ref:`eqv? <eqv?>`	\n\
`k` or ``#f`` if `k` is not in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
:return: a list starting from `k`, ``#f`` if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/memv-bad-list-type.idio
     *
     * memv #t #t
     */
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
first incidence of an element :ref:`equal? <equal?>`	\n\
`k` or ``#f`` if `k` is not in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: list to search in			\n\
:type l: list					\n\
:return: a list starting from `k`, ``#f`` if `k` is not in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/member-bad-list-type.idio
     *
     * member #t #t
     */
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
	    /*
	     * Test Case: pair-errors/assq-bad-pair.idio
	     *
	     * assq 3 '((1 2) 3)
	     */
	    idio_error_param_type ("pair", p, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
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
with a key :ref:`eq? <eq?>` `k`			\n\
or ``#f`` if `k` is not a key in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: association list to search in		\n\
:type l: list					\n\
:return: the list (`k` & value), ``#f`` if `k` is not a key in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/assq-bad-list-type.idio
     *
     * assq #t #t
     */
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
	    /*
	     * Test Case: pair-errors/assv-bad-pair.idio
	     *
	     * assv 3 '((1 2) 3)
	     */
	    idio_error_param_type ("pair", p, IDIO_C_FUNC_LOCATION ());

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
with a key :ref:`eqv? <eqv?>` `k`		\n\
or ``#f`` if `k` is not a key in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: association list to search in		\n\
:type l: list					\n\
:return: the list (`k` & value), ``#f`` if `k` is not a key in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/assv-bad-list-type.idio
     *
     * assv #t #t
     */
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
	    /*
	     * Test Case: pair-errors/assoc-bad-pair.idio
	     *
	     * assoc 3 '((1 2) 3)
	     */
	    idio_error_param_type ("pair", p, IDIO_C_FUNC_LOCATION ());

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
with a key :ref:`equal? <equal?>` `k`		\n\
or ``#f`` if `k` is not a key in `l`		\n\
						\n\
:param k: object to search for			\n\
:type k: any					\n\
:param l: association list to search in		\n\
:type l: list					\n\
:return: the list (`k` & value), ``#f`` if `k` is not a key in `l`\n\
")
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    /*
     * Test Case: pair-errors/assoc-bad-list-type.idio
     *
     * assoc #t #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_list_assoc (k, l);
}

IDIO idio_list_nth (IDIO l, intmax_t C_n, IDIO args)
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    IDIO_TYPE_ASSERT (list, args);

    if (idio_isa_pair (args)) {
	r = IDIO_PAIR_H (args);
    }

    while (idio_S_nil != l) {
	if (0 == C_n) {
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
:param default: value to return if not found, defaults to ``#n``	\n\
:type default: value, optional				\n\
:return: the element or `default`			\n\
:rtype: any						\n\
							\n\
``nth`` uses 0-based indexing.				\n\
							\n\
`n` can be negative to access the n\\ :sup:`th` from	\n\
the end of the list with ``-1`` being the last element.	\n\
")
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (I_n);
    IDIO_ASSERT (args);

    /*
     * Test Case: pair-errors/nth-bad-list-type.idio
     *
     * nth #t #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);
    ssize_t C_max = idio_list_length (l);

    intmax_t C_n = -1;

    if (idio_isa_fixnum (I_n)) {
	C_n = (intmax_t) IDIO_FIXNUM_VAL (I_n);
    } else if (idio_isa_integer_bignum (I_n)) {
	C_n = idio_bignum_ptrdiff_t_value (I_n);
    } else {
	/*
	 * Test Case: pair-errors/nth-bad-index-type.idio
	 *
	 * nth '(1 2 3) #t
	 */
	idio_error_param_type ("integer", I_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Complications: accessing out of bounds gets {default}/#n
     */

    IDIO r = idio_S_nil;

    if (idio_isa_pair (args)) {
	r = IDIO_PAIR_H (args);
    }

    if (C_n < 0) {
	C_n += C_max;

	if (C_n < 0) {
	    return r;
	}
    }

    if (C_n >= C_max) {
	return r;
    }

    return idio_list_nth (l, C_n, args);
}

IDIO idio_list_set_nth (IDIO l, intmax_t C_n, IDIO val)
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (val);

    IDIO_TYPE_ASSERT (list, l);

    while (idio_S_nil != l) {
	if (0 == C_n) {
	    IDIO_PAIR_H (l) = val;
	    break;
	}

	C_n--;
	l = IDIO_PAIR_T (l);
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("set-nth!", set_nth, (IDIO l, IDIO I_n, IDIO val), "l n val", "\
set the nth (`n`) element in list `l`			\n\
							\n\
:param l: list						\n\
:type orig: list					\n\
:param n: nth element					\n\
:type n: integer					\n\
:param val: value to assign				\n\
:type val: any						\n\
:return: ``#<unspec>``					\n\
							\n\
							\n\
``set-nth!`` uses 0-based indexing.			\n\
							\n\
`n` can be negative to set the n\\ :sup:`th` from the	\n\
end of the list with ``-1`` being the last element.	\n\
")
{
    IDIO_ASSERT (l);
    IDIO_ASSERT (I_n);
    IDIO_ASSERT (val);

    /*
     * Test Case: pair-errors/set-nth-bad-list-type.idio
     *
     * set-nth! #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (list, l);
    ssize_t C_max = idio_list_length (l);

    intmax_t C_n = -1;

    if (idio_isa_fixnum (I_n)) {
	C_n = (intmax_t) IDIO_FIXNUM_VAL (I_n);
    } else if (idio_isa_integer_bignum (I_n)) {
	C_n = idio_bignum_ptrdiff_t_value (I_n);
    } else {
	/*
	 * Test Case: pair-errors/set-nth-bad-index-type.idio
	 *
	 * set-nth '(1 2 3) #t #t
	 */
	idio_error_param_type ("integer", I_n, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (C_n < 0) {
	C_n += C_max;

	if (C_n < 0) {
	    /*
	     * Test Case: pair-errors/set-nth-bad-index-value.idio
	     *
	     * set-nth '(1 2 3) -4 #t
	     */
	    idio_error_param_value_msg ("set-nth!", "n", I_n, "negative index too large", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (C_n >= C_max) {
	/*
	 * Test Case: pair-errors/set-nth-bad-index-value.idio
	 *
	 * set-nth '(1 2 3) 3 #t
	 */
	idio_error_param_value_msg ("set-nth!", "n", I_n, "index too large", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_list_set_nth (l, C_n, val);
}

char *idio_pair_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (pair, v);

    char *r = NULL;

    /*
     * Technically a list (of pairs) should look like:
     *
     * "(a . (b . (c . (d . nil))))"
     *
     * but tradition dictates that we should flatten the list to:
     *
     * "(a b c d)"
     *
     * hence the while loop which continues if the tail is itself a
     * pair
    */
    {
	seen = idio_pair (v, seen);
	if (idio_isa_symbol (IDIO_PAIR_H (v))) {
	    int special = 0;
	    char *trail = NULL;
	    size_t tlen = 0;

	    if (idio_S_quote == IDIO_PAIR_H (v)) {
		special = 1;
		*sizep = idio_asprintf (&r, "'");
	    } else if (idio_S_unquote == IDIO_PAIR_H (v)) {
		special = 1;
		*sizep = idio_asprintf (&r, "$");
	    } else if (idio_S_unquotesplicing == IDIO_PAIR_H (v)) {
		special = 1;
		*sizep = idio_asprintf (&r, "$@");
	    } else if (idio_S_quasiquote == IDIO_PAIR_H (v)) {
		special = 1;
		trail = " }";
		tlen = 2;
		*sizep = idio_asprintf (&r, "#T{ ");
	    }

	    /*
	     * See corresponding clause in idio_pair_as_C_string()
	     * below.
	     */
	    if (special &&
		idio_isa_pair (IDIO_PAIR_T (v)) &&
		idio_S_nil == IDIO_PAIR_TT (v)) {
		size_t hs_size = 0;
		char *hs = idio_report_string (idio_list_head (IDIO_PAIR_T (v)), &hs_size, depth - 1, seen, 0);
		IDIO_STRCAT_FREE (r, sizep, hs, hs_size);

		if (NULL != trail) {
		    r = idio_strcat (r, sizep, trail, tlen);
		}

		return r;
	    }
	}

	*sizep = idio_asprintf (&r, "(");

	while (1) {
	    size_t hs_size = 0;
	    char *hs = idio_report_string (IDIO_PAIR_H (v), &hs_size, depth - 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, hs, hs_size);

	    v = IDIO_PAIR_T (v);
	    if (idio_type (v) != IDIO_TYPE_PAIR) {
		if (idio_S_nil != v) {
		    char *ps;
		    size_t ps_size = idio_asprintf (&ps, " %c ", IDIO_PAIR_SEPARATOR);
		    /* assumming IDIO_PAIR_SEPARATOR is 1 byte */
		    IDIO_STRCAT_FREE (r, sizep, ps, ps_size);

		    size_t t_size = 0;
		    char *t = idio_report_string (v, &t_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		}
		break;
	    } else {
		IDIO_STRCAT (r, sizep, " ");
	    }
	}
	IDIO_STRCAT (r, sizep, ")");
    }

    return r;
}

char *idio_pair_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth)
{
    IDIO_ASSERT (v);
    IDIO_ASSERT (seen);

    IDIO_TYPE_ASSERT (pair, v);

    char *r = NULL;

    /*
     * Technically a list (of pairs) should look like:
     *
     * "(a . (b . (c . (d . nil))))"
     *
     * but tradition dictates that we should flatten the list to:
     *
     * "(a b c d)"
     *
     * hence the while loop which continues if the tail is itself a
     * pair
    */
    {
	seen = idio_pair (v, seen);
	if (idio_isa_symbol (IDIO_PAIR_H (v))) {
	    int special = 0;
	    char *trail = NULL;
	    size_t tlen = 0;

	    if (idio_S_quote == IDIO_PAIR_H (v)) {
		special = 1;
		*sizep = idio_asprintf (&r, "'");
	    } else if (idio_S_unquote == IDIO_PAIR_H (v)) {
		special = 1;
		*sizep = idio_asprintf (&r, "$");
	    } else if (idio_S_unquotesplicing == IDIO_PAIR_H (v)) {
		special = 1;
		*sizep = idio_asprintf (&r, "$@");
	    } else if (idio_S_quasiquote == IDIO_PAIR_H (v)) {
		special = 1;
		trail = " }";
		tlen = 2;
		*sizep = idio_asprintf (&r, "#T{ ");
	    }

	    /*
	     * This bothered me before and still does now.  Broadly,
	     * quoting quote.  OK, not something you'd ordinarily do
	     * but, it turns out, lib/evaluate.idio does (but we'd
	     * never used it before in anger until pre-compilation
	     * running in parallel with this).
	     *
	     * The old code allowed #T{ '$x }, for x being the symbol
	     * quote, to become ''#n, which isn't great, it turns out,
	     * as it's more like (quote quote #n) which breaks a
	     * presumption about the single argument to a special.
	     * (And definitely not aided by this very code which
	     * "cleverly" prints it differently!)
	     *
	     * Here we're more strict and if the "special" isn't
	     * exactly one item then it will drop through and we'll
	     * get the '(quote) that the eps-qq-expand code is
	     * expecting.  It, in turn, is combined with several other
	     * lists of terminal symbols to give the desired quoted
	     * element.  (It all goes a bit Inception -- don't worry.)
	     *
	     * In the meanwhile, everything else doesn't fool about
	     * like evaluate.idio and no-one else notices...
	     */
	    if (special &&
		idio_isa_pair (IDIO_PAIR_T (v)) &&
		idio_S_nil == IDIO_PAIR_TT (v)) {
		size_t hs_size = 0;
		char *hs = idio_as_string (idio_list_head (IDIO_PAIR_T (v)), &hs_size, depth - 1, seen, 0);
		IDIO_STRCAT_FREE (r, sizep, hs, hs_size);

		if (NULL != trail) {
		    r = idio_strcat (r, sizep, trail, tlen);
		}

		return r;
	    }
	}

	*sizep = idio_asprintf (&r, "(");

	while (1) {
	    size_t hs_size = 0;
	    char *hs = idio_as_string (IDIO_PAIR_H (v), &hs_size, depth - 1, seen, 0);
	    IDIO_STRCAT_FREE (r, sizep, hs, hs_size);

	    v = IDIO_PAIR_T (v);
	    if (idio_type (v) != IDIO_TYPE_PAIR) {
		if (idio_S_nil != v) {
		    char *ps;
		    size_t ps_size = idio_asprintf (&ps, " %c ", IDIO_PAIR_SEPARATOR);
		    /* assuming IDIO_PAIR_SEPARATOR is 1 byte */
		    IDIO_STRCAT_FREE (r, sizep, ps, ps_size);

		    size_t t_size = 0;
		    char *t = idio_as_string (v, &t_size, depth - 1, seen, 0);
		    IDIO_STRCAT_FREE (r, sizep, t, t_size);
		}
		break;
	    } else {
		IDIO_STRCAT (r, sizep, " ");
	    }
	}
	IDIO_STRCAT (r, sizep, ")");
    }

    return r;
}

IDIO idio_pair_method_2string (idio_vtable_method_t *m, IDIO v, ...)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (v);

    va_list ap;
    va_start (ap, v);
    size_t *sizep = va_arg (ap, size_t *);
    IDIO seen = va_arg (ap, IDIO);
    int depth = va_arg (ap, int);
    va_end (ap);

    IDIO_ASSERT (seen);

    char *C_r = idio_pair_as_C_string (v, sizep, 0, seen, depth);

    IDIO r = idio_string_C_len (C_r, *sizep);

    IDIO_GC_FREE (C_r, *sizep);

    return r;
}

void idio_pair_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (pair_p);
    IDIO_ADD_PRIMITIVE (pair);
    IDIO_ADD_PRIMITIVE (pair_head);
    IDIO_ADD_PRIMITIVE (pair_tail);
    IDIO_ADD_PRIMITIVE (set_pair_head);
    IDIO_ADD_PRIMITIVE (set_pair_tail);

    IDIO_ADD_PRIMITIVE (pair_hh);
    IDIO_ADD_PRIMITIVE (pair_ht);
    IDIO_ADD_PRIMITIVE (pair_th);
    IDIO_ADD_PRIMITIVE (pair_tt);
    IDIO_ADD_PRIMITIVE (pair_hhh);
    IDIO_ADD_PRIMITIVE (pair_hth);
    IDIO_ADD_PRIMITIVE (pair_htt);
    IDIO_ADD_PRIMITIVE (pair_tth);
    IDIO_ADD_PRIMITIVE (pair_ttt);
    IDIO_ADD_PRIMITIVE (pair_htth);

    IDIO_ADD_PRIMITIVE (list_reverse);
    IDIO_ADD_PRIMITIVE (list_nreverse);
    IDIO_ADD_PRIMITIVE (list_length);
    IDIO_ADD_PRIMITIVE (list);
    IDIO_ADD_PRIMITIVE (append);
    IDIO_ADD_PRIMITIVE (ph_of);
    IDIO_ADD_PRIMITIVE (pt_of);
    IDIO_ADD_PRIMITIVE (any_nullp);
    IDIO_ADD_PRIMITIVE (memq);
    IDIO_ADD_PRIMITIVE (memv);
    IDIO_ADD_PRIMITIVE (member);
    IDIO_ADD_PRIMITIVE (assq);
    IDIO_ADD_PRIMITIVE (assv);
    IDIO_ADD_PRIMITIVE (assoc);
    IDIO_ADD_PRIMITIVE (list2array);

    idio_vtable_t *p_vt = idio_vtable (IDIO_TYPE_PAIR);
    IDIO ref = IDIO_ADD_PRIMITIVE (nth);
    idio_vtable_add_method (p_vt,
			    idio_S_value_index,
			    idio_vtable_create_method_value (idio_util_method_value_index,
							     idio_vm_values_ref (IDIO_FIXNUM_VAL (ref))));

    IDIO set = IDIO_ADD_PRIMITIVE (set_nth);
    idio_vtable_add_method (p_vt,
			    idio_S_set_value_index,
			    idio_vtable_create_method_value (idio_util_method_set_value_index,
							     idio_vm_values_ref (IDIO_FIXNUM_VAL (set))));
}

void idio_init_pair ()
{
    idio_module_table_register (idio_pair_add_primitives, NULL, NULL);

    idio_vtable_t *p_vt = idio_vtable (IDIO_TYPE_PAIR);

    idio_vtable_add_method (p_vt,
			    idio_S_typename,
			    idio_vtable_create_method_value (idio_util_method_typename,
							     idio_S_pair));

    idio_vtable_add_method (p_vt,
			    idio_S_2string,
			    idio_vtable_create_method_simple (idio_pair_method_2string));
}
