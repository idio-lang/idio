/*
 * Copyright (c) 2021-2022 Ian Fitchet <idf(at)idio-lang.org>
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
#include <stdarg.h>
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
#include "bignum.h"
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
IDIO_C_STRUCT_IDENT_DECL (idio_libc_poller_s);

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

    IDIO C_poller = idio_C_pointer_type (idio_CSI_idio_libc_poller_s, poller);
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

    /*
     * Test Case: libc-errors/poller-register-bad-poller-type.idio
     *
     * poller-register #t #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, poller);
    if (idio_CSI_idio_libc_poller_s != IDIO_C_TYPE_POINTER_PTYPE (poller)) {
	/*
	 * Test Case: libc-poll-errors/poller-register-invalid-poller-pointer-type.idio
	 *
	 * poller-register libc/NULL #t
	 */
	idio_error_param_value_exp ("poller-register", "poller", poller, "struct idio_libc_poller_s", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    idio_libc_poller_t *C_poller = IDIO_C_TYPE_POINTER_P (poller);

    IDIO_TYPE_ASSERT (list, pollee);

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

    /*
     * Test Case: libc-errors/poller-deregister-bad-poller-type.idio
     *
     * poller-deregister #t #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, poller);
    if (idio_CSI_idio_libc_poller_s != IDIO_C_TYPE_POINTER_PTYPE (poller)) {
	/*
	 * Test Case: libc-poll-errors/poller-deregister-invalid-poller-pointer-type.idio
	 *
	 * poller-deregister libc/NULL #t
	 */
	idio_error_param_value_exp ("poller-deregister", "poller", poller, "struct idio_libc_poller_s", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    idio_libc_poller_t *C_poller = IDIO_C_TYPE_POINTER_P (poller);

    idio_libc_poll_deregister (C_poller, fdh);

    return idio_S_unspec;
}

IDIO idio_libc_poll_poll (idio_libc_poller_t *poller, intmax_t timeout)
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

    /*
     * EINTR - what should we do if we are interrupted?
     *
     * (This seems to happen on some SunOS variants when an async
     * process we are polling for exits.  Timing, I guess.)
     *
     * I suspect we go round again but we should be shortening our
     * timeout appropriately which mean we really should have been
     * maintaining a target (real) time based on the original timeout.
     *
     * Remember, timeout is in milliseconds.
     */

    struct timeval tt;
    if (-1 == gettimeofday (&tt, NULL)) {
	idio_error_system_errno ("gettimeofday", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (timeout > 0) {
	int to = timeout;
	if (to > 1000) {
	    int ts = to / 1000;
	    tt.tv_sec += ts;
	    to -= (ts * 1000);
	}

	tt.tv_usec += to * 1000;
	if (tt.tv_usec > 1000000) {
	    tt.tv_usec -= 1000000;
	    tt.tv_sec += 1;
	}
    }

    int first = 1;
    int done = 0;

    while (! done) {
	if (first) {
	    first = 0;
	} else {
	    if (timeout > 0) {
		/*
		 * How much of timeout is left?
		 */
		struct timeval ct;
		if (-1 == gettimeofday (&ct, NULL)) {
		    idio_error_system_errno ("gettimeofday", idio_S_nil, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}

		time_t sec = tt.tv_sec - ct.tv_sec;
		suseconds_t usec = tt.tv_usec - ct.tv_usec;

		if (usec < 0) {
		    usec += 1000000;
		    sec -= 1;
		}

		if (sec < 0 ||
		    usec < 0) {
		    return idio_S_nil;
		}

		timeout = sec * 1000 + (usec / 1000);
	    }
	}

	int poll_r = poll (poller->fds, poller->nfds, timeout);

	if (-1 == poll_r) {
	    if (EINTR != errno) {
		/*
		 * Test Case: ??
		 */
		idio_error_system_errno ("poll", idio_S_nil, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    done = 1;
	}
    }

    poller->in_use = 0;

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
		/*
		 * XXX the nfds_t varies between a ulong and a uint so
		 * the printf format specifier is wrong (some of the
		 * time).
		 */
		fprintf (stderr, "%lu: fd=%d %d\n", (unsigned long) i, pfd->fd, poller->fds[i].fd);
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
:param timeout: timeout in milliseconds, defaults to ``#n``	\n\
:type timeout: fixnum, bignum or C/int		\n\
:return: list of :samp:`({fdh} {event})` tuples or ``#n``	\n\
:rtype: list					\n\
:raises ^rt-parameter-type-error:		\n\
:raises ^system-error:				\n\
")
{
    IDIO_ASSERT (poller);
    IDIO_ASSERT (args);

    /*
     * Test Case: libc-errors/poller-poll-bad-poller-type.idio
     *
     * poller-poll #t #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, poller);
    if (idio_CSI_idio_libc_poller_s != IDIO_C_TYPE_POINTER_PTYPE (poller)) {
	/*
	 * Test Case: libc-poll-errors/poller-poll-invalid-poller-pointer-type.idio
	 *
	 * poller-poll libc/NULL #t
	 */
	idio_error_param_value_exp ("poller-poll", "poller", poller, "struct idio_libc_poller_s", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    idio_libc_poller_t *C_poller = IDIO_C_TYPE_POINTER_P (poller);

    intmax_t C_timeout = -1;
    if (idio_S_nil != args) {
	IDIO timeout = IDIO_PAIR_H (args);
	if (idio_isa_fixnum (timeout)) {
	    C_timeout = IDIO_FIXNUM_VAL (timeout);
	} else if (idio_isa_C_int (timeout)) {
	    C_timeout = IDIO_C_TYPE_int (timeout);
	} else if (idio_isa_bignum (timeout)) {
	    if (IDIO_BIGNUM_INTEGER_P (timeout)) {
		C_timeout = idio_bignum_ptrdiff_t_value (timeout);
	    } else {
		IDIO timeout_i = idio_bignum_real_to_integer (timeout);
		if (idio_S_nil == timeout_i) {
		    /*
		     * Test Case: libc-poll-errors/poller-poll-timeout-float.idio
		     *
		     * poller-poll <poller> 1.1
		     */
		    idio_error_param_value_exp ("poller-poll", "timeout", timeout, "an integer bignum", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		} else {
		    C_timeout = idio_bignum_ptrdiff_t_value (timeout_i);
		}
	    }
	} else {
	    /*
	     * Test Case: libc-poll-errors/poller-poll-bad-timeout-type.idio
	     *
	     * libc/poller-poll <poller> #t
	     */
	    idio_error_param_type ("fixnum|bignum|C/int", timeout, IDIO_C_FUNC_LOCATION ());

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
    idio_free (C_poller->fds);
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

IDIO idio_libc_poll_select (IDIO rlist, IDIO wlist, IDIO elist, IDIO timeout)
{
    IDIO_ASSERT (rlist);
    IDIO_ASSERT (wlist);
    IDIO_ASSERT (elist);
    IDIO_ASSERT (timeout);

    fd_set rfds, wfds, efds;
    int max_fd = -1;		/* we will add one later */

    IDIO fd_map = IDIO_HASH_EQP (8);

    /*
     * Three identical loops over the fd lists
     */
    for (int i = 0; i < 3; i++) {
	fd_set *fdsp;
	IDIO fd_list;
	switch (i) {
	case 0:
	    fdsp = &rfds;
	    fd_list = rlist;
	    break;
	case 1:
	    fdsp = &wfds;
	    fd_list = wlist;
	    break;
	case 2:
	    fdsp = &efds;
	    fd_list = elist;
	    break;
	}

	FD_ZERO (fdsp);

	while (idio_S_nil != fd_list) {
	    IDIO e = IDIO_PAIR_H (fd_list);

	    int fd = -1;
	    if (idio_isa_C_int (e)) {
		fd = IDIO_C_TYPE_int (e);
	    } else if (idio_isa_fd_handle (e)) {
		fd = IDIO_FILE_HANDLE_FD (e);
	    } else {
		/*
		 * Test Case: libc-poll-errors/select-bad-list-element-type.idio
		 *
		 * libc/select '(#t) #n #n
		 */
		idio_error_param_type ("C/int|fd-handle", e, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    if (fd < 0 ||
		fd >= FD_SETSIZE) {
		/*
		 * Test Case: libc-poll-errors/select-bad-list-fd-value.idio
		 *
		 * libc/select (list (C/integer-> 98765)) #n #n
		 */
		idio_error_param_value_msg ("select", "fd", idio_C_int (fd), "0 <= fd < FD_SETSIZE", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    FD_SET (fd, fdsp);

	    if (fd > max_fd) {
		max_fd = fd;
	    }

	    /*
	     * There is a risk that different entities representing fd
	     * N could override each other here -- however, anyone
	     * with two entities in their hands that represent the
	     * same FD are going to be in a world of pain when one is
	     * GC'd and closed under the feet of the other.
	     */
	    idio_hash_put (fd_map, idio_C_int (fd), e);

	    fd_list = IDIO_PAIR_T (fd_list);
	}
    }
    max_fd++;

    /*
     * Calculate a (potential) relative time from timeout
     */
    int use_rt = 1;
    struct timeval rt;

    /*
     * While we're passing through we can make a decision about the
     * timeout value being passed to select, a NULL or not
     */
    struct timeval st;
    struct timeval *stp = &st;

    if (idio_S_nil == timeout) {
	use_rt = 0;
	stp = NULL;
    } else if (idio_isa_fixnum (timeout)) {
	intptr_t C_timeout = IDIO_FIXNUM_VAL (timeout);
	rt.tv_usec = C_timeout % 1000000;
	C_timeout -= rt.tv_usec;
	rt.tv_sec = C_timeout / 1000000;
    } else if (idio_isa_bignum (timeout)) {
	intmax_t C_timeout = 0;
	if (IDIO_BIGNUM_INTEGER_P (timeout)) {
	    /*
	     * Code coverage: big timeout...test at your leisure.
	     */
	    C_timeout = idio_bignum_ptrdiff_t_value (timeout);
	} else {
	    IDIO timeout_i = idio_bignum_real_to_integer (timeout);
	    if (idio_S_nil == timeout_i) {
		/*
		 * Test Case: libc-poll-errors/select-timeout-float.idio
		 *
		 * select #n #n #n 1.1
		 */
		idio_error_param_value_exp ("select", "timeout", timeout, "integer bignum", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		C_timeout = idio_bignum_ptrdiff_t_value (timeout_i);
	    }
	}
	rt.tv_usec = C_timeout % 1000000;
	C_timeout -= rt.tv_usec;
	rt.tv_sec = C_timeout / 1000000;
    } else if (idio_isa_C_pointer (timeout)) {
	if (idio_CSI_libc_struct_timeval != IDIO_C_TYPE_POINTER_PTYPE (timeout)) {
	    /*
	     * Test Case: libc-poll-errors/select-invalid-timeout-pointer-type.idio
	     *
	     * select #n #n #n libc/NULL
	     */
	    idio_error_param_value_exp ("select", "timeout", timeout, "struct idio_libc_struct_timeval", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	struct timeval *tvp = IDIO_C_TYPE_POINTER_P (timeout);
	rt.tv_sec = tvp->tv_sec;
	rt.tv_usec = tvp->tv_usec;
    } else {
	/*
	 * Test Case: libc-poll-errors/select-bad-timeout-type.idio
	 *
	 * libc/select #n #n #n #t
	 */
	idio_error_param_type ("fixnum|bignum|C/struct-timeval", timeout, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Similar to poll(2), we need to run round the loop again if we
     * get EINTR or EAGAIN.  So, in the same way, we need to calculate
     * the End of Times(tm) and set the actual timeout with respect to
     * that.
     */
    struct timeval tt;
    if (-1 == gettimeofday (&tt, NULL)) {
	idio_error_system_errno ("gettimeofday", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (use_rt) {
	tt.tv_sec += rt.tv_sec;
	tt.tv_usec += rt.tv_usec;
	if (tt.tv_usec > 1000000) {
	    tt.tv_usec -= 1000000;
	    tt.tv_sec += 1;
	}
    }

    int first = 1;
    int done = 0;

    while (! done) {
	if (first) {
	    first = 0;
	    if (use_rt) {
		st.tv_sec = rt.tv_sec;
		st.tv_usec = rt.tv_usec;
	    }
	} else {
	    if (use_rt) {
		/*
		 * How much of timeout is left?
		 */
		struct timeval ct;
		if (-1 == gettimeofday (&ct, NULL)) {
		    idio_error_system_errno ("gettimeofday", idio_S_nil, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}

		st.tv_sec = tt.tv_sec - ct.tv_sec;
		st.tv_usec = tt.tv_usec - ct.tv_usec;

		if (st.tv_usec < 0) {
		    st.tv_usec += 1000000;
		    st.tv_sec -= 1;
		}

		if (st.tv_sec < 0 ||
		    st.tv_usec < 0) {
		    return IDIO_LIST3 (idio_S_nil, idio_S_nil, idio_S_nil);
		}
	    }
	}

	int select_r = select (max_fd, &rfds, &wfds, &efds, stp);

	if (-1 == select_r) {
	    if (EINTR != errno &&
		EAGAIN != errno) {
		/*
		 * Test Case: ??
		 */
		idio_error_system_errno ("select", idio_S_nil, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else if (0 == select_r) {
	    return IDIO_LIST3 (idio_S_nil, idio_S_nil, idio_S_nil);
	} else {
	    done = 1;
	}
    }

    IDIO rr = idio_S_nil;
    IDIO wr = idio_S_nil;
    IDIO er = idio_S_nil;

    /*
     * Three identical loops over the fd lists
     */
    for (int i = 0; i < 3; i++) {
	fd_set *fdsp;
	switch (i) {
	case 0:
	    fdsp = &rfds;
	    break;
	case 1:
	    fdsp = &wfds;
	    break;
	case 2:
	    fdsp = &efds;
	    break;
	}

	IDIO rl = idio_S_nil;

	for (int i = 0; i < max_fd; i++) {
	    if (FD_ISSET (i, fdsp)) {
		rl = idio_pair (idio_hash_ref (fd_map, idio_C_int (i)), rl);
	    }
	}

	switch (i) {
	case 0:
	    rr = idio_list_reverse (rl);
	    break;
	case 1:
	    wr = idio_list_reverse (rl);
	    break;
	case 2:
	    er = idio_list_reverse (rl);
	    break;
	}
    }

    IDIO r = IDIO_LIST3 (rr, wr, er);

    return r;
}

IDIO_DEFINE_PRIMITIVE3V_DS ("select", libc_select, (IDIO rlist, IDIO wlist, IDIO elist, IDIO args), "rlist wlist elist [timeout]", "\
Call :manpage:`select(2)` for `timeout` microseconds	\n\
						\n\
:param rlist: a list of selectable entities for read events	\n\
:type rlist: list				\n\
:param wlist: a list of selectable entities for write events	\n\
:type wlist: list				\n\
:param elist: a list of selectable entities for exception events	\n\
:type elist: list				\n\
:param timeout: timeout in microseconds, defaults to ``#n``	\n\
:type timeout: fixnum, bignum or :ref:`libc/struct-timeval <libc/struct-timeval>`	\n\
:return: list of three lists of events, see below	\n\
:rtype: list					\n\
:raises ^rt-parameter-type-error:		\n\
:raises ^system-error:				\n\
						\n\
The return value is a list of three lists of 	\n\
ready objects, derived from the first three arguments.	\n\
						\n\
Selectable entities are file descriptors (C/int) 	\n\
and file descriptor handles.		 	\n\
")
{
    IDIO_ASSERT (rlist);
    IDIO_ASSERT (wlist);
    IDIO_ASSERT (elist);
    IDIO_ASSERT (args);

    /*
     * Test Cases: libc-errors/select-bad-Xlist-type.idio
     *
     * select #t #t #t
     * select #n #n #t
     * select #n #t #t
     */
    IDIO_USER_TYPE_ASSERT (list, rlist);
    IDIO_USER_TYPE_ASSERT (list, wlist);
    IDIO_USER_TYPE_ASSERT (list, elist);

    IDIO timeout = idio_S_nil;
    if (idio_isa_pair (args)) {
	timeout = IDIO_PAIR_H (args);
    }

    return idio_libc_poll_select (rlist, wlist, elist, timeout);
}

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

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_module, libc_select);
}

void idio_final_libc_poll ()
{
}

void idio_init_libc_poll ()
{
    idio_module_table_register (idio_libc_poll_add_primitives, idio_final_libc_poll, NULL);

    idio_libc_set_poll_names ();

    IDIO struct_name = IDIO_SYMBOLS_C_INTERN ("libc/struct-idio-libc-poller-s");
    IDIO_C_STRUCT_IDENT_DEF (struct_name, idio_S_nil, idio_libc_poller_s, idio_fixnum (0));
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
