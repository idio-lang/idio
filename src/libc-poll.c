/*
 * Copyright (c) 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * libc-poll.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "file-handle.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "libc-wrap.h"
#include "libc-poll.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"

#include "libc-api.h"

IDIO idio_libc_poll_names;

IDIO_DEFINE_PRIMITIVE0V_DS ("make-poller", make_poller, (IDIO args), "[args]", "\
Create a `poller` from `args`		\n\
					\n\
:param args: see below			\n\
:type args: list			\n\
:return: poller				\n\
:rtype: C/pointer			\n\
:raises ^rt-parameter-type-error:	\n\
					\n\
Each element of `args` should be a list	\n\
of :samp:`({fdh} {eventmask} [{eventmask} ...])`	\n\
where `fdh` can be an FD or an FD handle	\n\
and `eventmask` can be a ``POLL*`` name	\n\
or C/int derived from such names	\n\
")
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    idio_libc_poller_t *poller = idio_alloc (sizeof (idio_libc_poller_t));

    poller->fd_map = IDIO_HASH_EQP (8);
    idio_gc_protect (poller->fd_map);

    nfds_t nfds = idio_list_length (args);
    poller->nfds = nfds;
    poller->fds = idio_alloc (sizeof (struct pollfd));
    if (nfds) {
	while (idio_S_nil != args) {
	    idio_libc_poll_register (poller, IDIO_PAIR_H (args));
	    args = IDIO_PAIR_T (args);
	}
    }

    poller->valid = 0;
    poller->in_use = 0;

    IDIO C_poller = idio_C_pointer_free_me (poller);
    idio_gc_register_finalizer (C_poller, idio_libc_poll_finalizer);
    return C_poller;
}

void idio_libc_poll_set_pollfds (idio_libc_poller_t *poller)
{
    IDIO_C_ASSERT (poller);

    IDIO keys = idio_hash_keys_to_list (poller->fd_map);

    idio_hi_t nkeys = IDIO_HASH_COUNT (poller->fd_map);
    if (nkeys != poller->nfds) {
	poller->fds = idio_realloc (poller->fds, nkeys);
    }
    poller->nfds = nkeys;

    /* use nkeys as an index so reverse order! */
    nkeys--;
    while (idio_S_nil != keys) {
	IDIO k = IDIO_PAIR_H (keys);
	IDIO v = idio_hash_ref (poller->fd_map, k);

	struct pollfd *pfd = &(poller->fds[nkeys]);
	pfd->fd = IDIO_C_TYPE_int (k);
	pfd->events = IDIO_C_TYPE_short (IDIO_PAIR_H (v));

	keys = IDIO_PAIR_T (keys);
	nkeys--;
    }

    poller->valid = 1;
}

void idio_libc_poll_register (idio_libc_poller_t *poller, IDIO pollee)
{
    IDIO_C_ASSERT (poller);
    IDIO_ASSERT (pollee);

    IDIO_TYPE_ASSERT (pair, pollee);

    IDIO fdh = IDIO_PAIR_H (pollee);

    IDIO fd = idio_S_false;
    if (idio_isa_fd_handle (fdh)) {
	fd = idio_C_int (IDIO_FILE_HANDLE_FD (fdh));
    } else if (idio_isa_C_int (fdh)) {
	fd = fdh;
    } else {
	/*
	 * Test Case: libc-poll-errors/poller-register-bad-fdh-type.idio
	 *
	 * libc/poller-register <poller> (list #t #t)
	 */
	idio_error_param_type ("fd-handle|C/int", fdh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO evm = IDIO_PAIR_T (pollee);
    short events = 0;

    if (idio_isa_pair (evm)) {
	while (idio_S_nil != evm) {
	    IDIO ev = IDIO_PAIR_H (evm);

	    /*
	     * Test Case: libc-poll-errors/poller-register-bad-eventmask-list-type.idio
	     *
	     * libc/poller-register <poller> (list C/0i POLLIN #t)
	     */
	    IDIO_USER_TYPE_ASSERT (C_int, ev);

	    events |= IDIO_C_TYPE_int (ev);

	    evm = IDIO_PAIR_T (evm);
	}
    } else {
	/*
	 * Test Case: libc-poll-errors/poller-register-bad-eventmask-type.idio
	 *
	 * libc/poller-register <poller> (list C/0i #t)
	 */
	idio_error_param_type ("C/int|list of C/int", evm, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_hash_put (poller->fd_map, fd, IDIO_LIST2 (idio_C_short (events), fdh));
    poller->valid = 0;
}

IDIO_DEFINE_PRIMITIVE2_DS ("poller-register", poller_register, (IDIO poller, IDIO pollee), "poller pollee", "\
Add `pollee` to `poller`		\n\
					\n\
:param poller: a poller from :ref:`libc/make-poller <libc/make-poller>`		\n\
:type poller: C/pointer			\n\
:param pollee: see below		\n\
:type pollee: list			\n\
:return: ``#<unspec>``			\n\
:raises ^rt-parameter-type-error:	\n\
					\n\
`pollee` should be a list of		\n\
:samp:`({fdh} {eventmask} [{eventmask} ...])`	\n\
where `fdh` can be an FD or an FD handle	\n\
and `eventmask` can be a ``POLL*`` name	\n\
or C/int derived from such names	\n\
")
{
    IDIO_ASSERT (poller);
    IDIO_ASSERT (pollee);

    IDIO_TYPE_ASSERT (C_pointer, poller);
    IDIO_TYPE_ASSERT (list, pollee);

    idio_libc_poller_t *C_poller = IDIO_C_TYPE_POINTER_P (poller);

    idio_libc_poll_register (C_poller, pollee);

    return idio_S_unspec;
}

void idio_libc_poll_deregister (idio_libc_poller_t *poller, IDIO fdh)
{
    IDIO_C_ASSERT (poller);
    IDIO_ASSERT (fdh);

    IDIO fd = idio_S_false;
    if (idio_isa_fd_handle (fdh)) {
	fd = idio_C_int (IDIO_FILE_HANDLE_FD (fdh));
    } else if (idio_isa_C_int (fdh)) {
	fd = fdh;
    } else {
	/*
	 * Test Case: libc-poll-errors/poller-deregister-bad-fdh-type.idio
	 *
	 * libc/poller-deregister <poller> #t
	 */
	idio_error_param_type ("fd-handle|C/int", fdh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_hash_delete (poller->fd_map, fd);
    poller->valid = 0;
}

IDIO_DEFINE_PRIMITIVE2_DS ("poller-deregister", poller_deregister, (IDIO poller, IDIO fdh), "poller fdh", "\
Remove `fdh` from `poller`		\n\
					\n\
:param poller: a poller from :ref:`libc/make-poller <libc/make-poller>`		\n\
:type poller: C/pointer			\n\
:param fdh: an FD or FD handle		\n\
:type fdh: C/int or FD handle		\n\
:return: ``#<unspec>``			\n\
:raises ^rt-parameter-type-error:	\n\
")
{
    IDIO_ASSERT (poller);
    IDIO_ASSERT (fdh);

    IDIO_TYPE_ASSERT (C_pointer, poller);

    idio_libc_poller_t *C_poller = IDIO_C_TYPE_POINTER_P (poller);

    idio_libc_poll_deregister (C_poller, fdh);

    return idio_S_unspec;
}

IDIO idio_libc_poll_poll (idio_libc_poller_t *poller, int timeout)
{
    IDIO_C_ASSERT (poller);

    if (poller->in_use) {
	idio_error_param_value_msg_only ("poller-poll", "poller", "in use", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    poller->in_use = 1;

    if (0 == poller->valid) {
	idio_libc_poll_set_pollfds (poller);
    }

    /*
     * XXX what is the meaning of nfds==0 and timeout==-1?
     *
     * Only interruptable by a signal.
     */

    int poll_r = poll (poller->fds, poller->nfds, timeout);

    poller->in_use = 0;

    if (-1 == poll_r) {
	idio_error_system_errno ("poll", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_nil;
    int n = 0;
    nfds_t i;
    for (i = 0; i < poller->nfds; i++) {
	if (poller->fds[i].revents) {
	    struct pollfd *pfd = &(poller->fds[i]);

	    n++;
	    IDIO k = idio_C_int (pfd->fd);
	    IDIO v = idio_hash_ref (poller->fd_map, k);
	    if (idio_S_unspec == v) {
		fprintf (stderr, "%ld: fd=%d %d\n", i, pfd->fd, poller->fds[i].fd);
		idio_debug ("ERROR: poller-poll v=#unspec for k=%s\n", k);
		idio_debug ("fd_map=%s\n", poller->fd_map);
		IDIO_C_ASSERT (0);
	    }
	    r = idio_pair (IDIO_LIST2 (IDIO_PAIR_HT (v), idio_C_short (pfd->revents)), r);
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("poller-poll", poller_poll, (IDIO poller, IDIO args), "poller [timeout]", "\
Poll `poller` for `timeout` milliseconds	\n\
						\n\
:param poller: a poller from :ref:`libc/make-poller <libc/make-poller>`		\n\
:type poller: C/pointer				\n\
:param timeout: timeout, defaults to ``#n``	\n\
:type timeout: fixnum or C/int			\n\
:return: list of :samp:`({fdh} {event})` tuples	\n\
:rtype: list					\n\
:raises ^rt-parameter-type-error:		\n\
:raises ^system-error:				\n\
")
{
    IDIO_ASSERT (poller);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (C_pointer, poller);

    idio_libc_poller_t *C_poller = IDIO_C_TYPE_POINTER_P (poller);

    int C_timeout = -1;
    if (idio_S_nil != args) {
	IDIO timeout = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (timeout)) {
	    C_timeout = IDIO_FIXNUM_VAL (timeout);
	} else if (idio_isa_C_int (timeout)) {
	    C_timeout = IDIO_C_TYPE_int (timeout);
	} else {
	    /*
	     * Test Case: libc-poll-errors/poller-poll-bad-timeout-type.idio
	     *
	     * libc/poller-poll <poller> #t
	     */
	    idio_error_param_type ("fixnum|C/int", timeout, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return idio_libc_poll_poll (C_poller, C_timeout);
}

void idio_libc_poll_finalizer (IDIO poller)
{
    IDIO_ASSERT (poller);
    IDIO_TYPE_ASSERT (C_pointer, poller);

    idio_libc_poller_t *C_poller = IDIO_C_TYPE_POINTER_P (poller);

    idio_gc_expose (C_poller->fd_map);
    IDIO_GC_FREE (C_poller->fds);
}

static void idio_libc_set_poll_names ()
{
    idio_libc_poll_names = IDIO_HASH_EQP (16);
    idio_gc_protect_auto (idio_libc_poll_names);

    /*
     * The POLL* values are not especially well-defined -- other than
     * being a short so there can be up to 16 of them.  I see a
     * reference on Linux to iBCS2 for 0x0001 through 0x0020.
     *
     * POLL{RD,WR}{NORM,BAND} appear to exist on all systems (with
     * POLLWRNORM often defined as POLLOUT) but after that it's a bit
     * of a free-for-all.
     *
     * XXX remember to update the predicates below as well!!
     */

    /* 0x0001 */
#if defined (POLLIN)
    IDIO_LIBC_POLL (POLLIN);
#endif

    /* 0x0002 */
#if defined (POLLPRI)
    IDIO_LIBC_POLL (POLLPRI);
#endif

    /* 0x0004 */
#if defined (POLLOUT)
    IDIO_LIBC_POLL (POLLOUT);
#endif

    /* 0x0008 */
#if defined (POLLERR)
    IDIO_LIBC_POLL (POLLERR);
#endif

    /* 0x0010 */
#if defined (POLLHUP)
    IDIO_LIBC_POLL (POLLHUP);
#endif

    /* 0x0020 */
#if defined (POLLNVAL)
    IDIO_LIBC_POLL (POLLNVAL);
#endif

    /* 0x0040 */
#if defined (POLLRDNORM)
    IDIO_LIBC_POLL (POLLRDNORM);
#endif

    /* 0x0080 */
#if defined (POLLRDBAND)
    IDIO_LIBC_POLL (POLLRDBAND);
#endif

    /* inconsistent values from now on! */

#if defined (POLLWRNORM)
    IDIO_LIBC_POLL (POLLWRNORM);
#endif

#if defined (POLLWRBAND)
    IDIO_LIBC_POLL (POLLWRBAND);
#endif

    /* Linux, SunOS */
#if defined (POLLREMOVE)
    IDIO_LIBC_POLL (POLLREMOVE);
#endif

    /* Linux, SunOS */
#if defined (POLLRDHUP)
    IDIO_LIBC_POLL (POLLRDHUP);
#endif

}

IDIO idio_libc_poll_name (IDIO pollevent)
{
    IDIO_ASSERT (pollevent);
    IDIO_TYPE_ASSERT (C_int, pollevent);

    return idio_hash_reference (idio_libc_poll_names, pollevent, idio_S_nil);
}

IDIO_DEFINE_PRIMITIVE1_DS ("poll-name", libc_poll_name, (IDIO pollevent), "pollevent", "\
return the string name of the :manpage:`poll(2)`      \n\
C macro						\n\
						\n\
:param pollevent: the value of the macro		\n\
:type pollevent: C/int				\n\
:return: a symbol				\n\
:raises ^rt-parameter-type-error:		\n\
:raises ^rt-hash-key-not-found-error: if `pollevent` not found	\n\
")
{
    IDIO_ASSERT (pollevent);

    if (idio_isa_fixnum (pollevent)) {
	pollevent = idio_C_int (IDIO_FIXNUM_VAL (pollevent));
    } else if (idio_isa_C_int (pollevent)) {
    } else {
	/*
	 * Test Case: libc-poll-errors/poll-name-bad-type.idio
	 *
	 * libc/poll-name #t
	 */
	idio_error_param_type ("fixnum|C/int", pollevent, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_libc_poll_name (pollevent);
}

IDIO_DEFINE_PRIMITIVE0_DS ("poll-names", libc_poll_names, (), "", "\
return a list of :samp:`({number} & {name})` pairs of the :manpage:`poll(2)`      \n\
C macros					\n\
						\n\
each pair is the C value and string name	\n\
of the macro					\n\
						\n\
:return: a list of pairs			\n\
")
{
    IDIO r = idio_S_nil;

    IDIO keys = idio_hash_keys_to_list (idio_libc_poll_names);

    while (idio_S_nil != keys) {
	IDIO key = IDIO_PAIR_H (keys);
	r = idio_pair (idio_pair (key, idio_hash_ref (idio_libc_poll_names, key)), r);
	keys = IDIO_PAIR_T (keys);
    }

    return idio_list_reverse (r);
}

/*
 * Bah!  struct pollfd.revents is a short and the C bitwise functions,
 * notably C/&, only handle ints.  So, whatever we do, either
 * system-wise or user-wise there's a load of mucking about to be
 * done.
 *
 * Hence a series of POLLIN? predicates that are expecting the C/short
 * and and POLLIN, a C int, and do the right thing.
 *
 * It's not Art especially as the set of (portable?) POLL* names are
 * not universal and we have to ifdef them all.
 */
#define IDIO_DEFINE_LIBC_POLL_PRIMITIVE(pollmask)			\
    IDIO_DEFINE_PRIMITIVE1 (#pollmask "?", libc_poll_ ## pollmask ## _p, (IDIO eventmask)) \
    {									\
	IDIO_ASSERT (eventmask);					\
	IDIO_USER_C_TYPE_ASSERT (short, eventmask);			\
									\
	int C_eventmask = IDIO_C_TYPE_short (eventmask);		\
									\
	IDIO r = idio_S_false;						\
									\
	if (C_eventmask & pollmask) {					\
	    r = idio_S_true;						\
	}								\
									\
	return r;							\
    }


#if defined (POLLIN)
IDIO_DEFINE_LIBC_POLL_PRIMITIVE(POLLIN)
#endif

#if defined (POLLPRI)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLPRI);
#endif

#if defined (POLLOUT)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLOUT);
#endif

#if defined (POLLERR)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLERR);
#endif

#if defined (POLLHUP)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLHUP);
#endif

#if defined (POLLNVAL)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLNVAL);
#endif

#if defined (POLLRDNORM)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLRDNORM);
#endif

#if defined (POLLRDBAND)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLRDBAND);
#endif

#if defined (POLLWRNORM)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLWRNORM);
#endif

#if defined (POLLWRBAND)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLWRBAND);
#endif

#if defined (POLLREMOVE)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLREMOVE);
#endif

#if defined (POLLRDHUP)
    IDIO_DEFINE_LIBC_POLL_PRIMITIVE (POLLRDHUP);
#endif

void idio_libc_poll_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, make_poller);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, poller_register);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, poller_deregister);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, poller_poll);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_names);

#if defined (POLLIN)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLIN_p)
#endif

#if defined (POLLPRI)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLPRI_p);
#endif

#if defined (POLLOUT)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLOUT_p);
#endif

#if defined (POLLERR)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLERR_p);
#endif

#if defined (POLLHUP)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLHUP_p);
#endif

#if defined (POLLNVAL)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLNVAL_p);
#endif

#if defined (POLLRDNORM)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLRDNORM_p);
#endif

#if defined (POLLRDBAND)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLRDBAND_p);
#endif

#if defined (POLLWRNORM)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLWRNORM_p);
#endif

#if defined (POLLWRBAND)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLWRBAND_p);
#endif

#if defined (POLLREMOVE)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLREMOVE_p);
#endif

#if defined (POLLRDHUP)
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_poll_POLLRDHUP_p);
#endif

}

void idio_final_libc_poll ()
{
}

void idio_init_libc_poll ()
{
    idio_module_table_register (idio_libc_poll_add_primitives, idio_final_libc_poll, NULL);

    idio_libc_set_poll_names ();
}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: utf-8-unix */
/* End: */
