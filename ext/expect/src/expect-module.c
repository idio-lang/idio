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
 * expect-module.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"

#include "bignum.h"
#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "expect-system.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "idio.h"
#include "malloc.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "unicode.h"
#include "usi-wrap.h"
#include "usi.h"
#include "vm.h"

#include "expect-module.h"

IDIO idio_expect_module;
IDIO idio_expect_exp_human_sym;

/*
 * exp-send-human exists because maths in Idio is slow...
 *
 * The quantile (inverse cumulative distribution function) is
 *
 *   λ (- (ln R))^K
 *
 * which requires natural log and exponent of e.  Both of which
 * require infinite series to calculate.
 *
 * Even on my fastest box that was 6ms (and sometimes up to 190ms).
 * Multiply by six or more for a Raspberry Pi 3B+.  These become
 * significant fractions of the gap time we are calculating which
 * begins to defeat the point.
 *
 * So, for those rare occasions you want to send at a human speed
 * we'll call a dedicated function where that sort of maths in C takes
 * 10us (60us on the RPi).
 */
IDIO_DEFINE_PRIMITIVE2_DS ("exp-send-human", expect_send_human, (IDIO fd, IDIO msg), "fd msg", "\
send `msg` slowly as if a human was typing	\n\
					\n\
:param fd: file descriptor		\n\
:type fd: C/int				\n\
:param msg: message to send		\n\
:type msg: string			\n\
:return: ``#<unspec>``			\n\
					\n\
``exp-send-human`` uses a similar algorithm	\n\
to :manpage:`expect(1)`			\n\
					\n\
.. seealso:: :ref:`exp-send <expect/exp-send>` 	\n\
	for details		 	\n\
")
{
    IDIO_ASSERT (fd);
    IDIO_ASSERT (msg);

    /*
     * Test Case: expect-errors/exp-send-human-bad-fd-type.idio
     *
     * exp-send-human #t #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, fd);

    /*
     * Test Case: expect-errors/exp-send-human-bad-msg-type.idio
     *
     * exp-send-human C/0i #t
     */
    IDIO_USER_TYPE_ASSERT (string, msg);

    int C_fd = IDIO_C_TYPE_int (fd);

    IDIO exp_human = idio_module_symbol_value_recurse (idio_expect_exp_human_sym,
						       idio_thread_current_module (),
						       IDIO_LIST1 (idio_S_false));

    if (idio_S_false == exp_human) {
	/*
	 * Test Case: expect-errors/exp-send-human-exp-human-unset.idio
	 *
	 * {
	 *   !~ exp-human
	 *   exp-send-human C/0i "hello"
	 * }
	 */
	idio_error_param_value_msg_only ("exp-send-human", "exp-human", "should be a 5-tuple", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * validate exp_human: 5 elements, all integers (ms) expect the
     * 3rd, K, a number
     *
     * We'll convert into C values: intmax_t for ms and double for K.
     *
     * Check for negative values.  The inter-character gap is
     * implemented by poll(2) which uses an int for the timeout.  So
     * there's an ultimate limit there.
     */
    int n = 0;
    IDIO eh = exp_human;
    intmax_t in_ms, out_ms, min_ms, max_ms;
    double K;
    while (idio_S_nil != eh) {
	IDIO e = IDIO_PAIR_H (eh);
	n++;
	switch (n) {
	case 3:
	    if (idio_isa_fixnum (e)) {
		K = (double) IDIO_FIXNUM_VAL (e);
	    } else if (idio_isa_bignum (e)) {
		K = idio_bignum_double_value (e);
	    } else {
		/*
		 * Test Case: expect-errors/exp-send-human-exp-human-bad-K-type.idio
		 *
		 * {
		 *   exp-human :~ '(1 2 #t 4 5)
		 *   exp-send-human C/0i "hello"
		 * }
		 */
		idio_error_param_type ("fixnum|bignum", e, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    if (K < 0) {
		/*
		 * Test Case: expect-errors/exp-send-human-exp-human-K-negative.idio
		 *
		 * {
		 *   exp-human :~ '(1 2 -3 4 5)
		 *   exp-send-human C/0i "hello"
		 * }
		 */
		idio_error_param_value_msg ("exp-send-human", "K", e, "negative K", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    break;
	default:
	    if (idio_isa_integer (e)) {
		intmax_t ms;
		if (idio_isa_fixnum (e)) {
		    ms = (intmax_t) IDIO_FIXNUM_VAL (e);
		} else if (idio_isa_bignum (e)) {
		    ms = idio_bignum_intmax_t_value (e);
		} else {
		    /*
		     * Code coverage: strictly it is not possible to
		     * get here because we already checked this is an
		     * integer.  Coding error?
		     *
		     * Also we can squelch an uninitialised variable
		     * error.
		     */
		    ms = 0;
		}

		if (ms < 0) {
		    /*
		     * Test Case(s): expect-errors/exp-send-human-exp-human-negative-X.idio
		     *
		     * {
		     *   exp-human :~ '(-1 2 3 4 5)
		     *   exp-send-human C/0i "hello"
		     * }
		     */
		    idio_error_param_value_msg ("exp-send-human", "ms", e, "negative ms", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		} else if (ms > INT_MAX) {
		    /*
		     * Test Case(s): expect-errors/exp-send-human-exp-human-X-too-large.idio
		     *
		     * {
		     *   exp-human :~ (list large-int 2 3 4 5)
		     *   exp-send-human C/0i "hello"
		     * }
		     *
		     * XXX The point of this test is that the poll(2)
		     * timeout is an int and on many systems we can be
		     * given a number that is > INT_MAX.  However, if
		     * sizeof (int) == sizeof (intmax_t) then this
		     * test is not possible.
		     */
		    idio_error_param_value_msg ("exp-send-human", "ms", e, "ms too large", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}

		switch (n) {
		case 1:
		    in_ms = ms;
		    break;
		case 2:
		    out_ms = ms;
		    break;
		case 4:
		    min_ms = ms;
		    break;
		case 5:
		    max_ms = ms;
		    break;
		}
	    } else {
		/*
		 * Test Case(s): expect-errors/exp-send-human-exp-human-bad-X-type.idio
		 *
		 * {
		 *   exp-human :~ '(#t 2 3 4 5)
		 *   exp-send-human C/0i "hello"
		 * }
		 */
		idio_error_param_type ("integer", e, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	eh = IDIO_PAIR_T (eh);

	if (n >= 5) {
	    break;
	}
    }

    if (n < 5) {
	/*
	 * Test Case: expect-errors/exp-send-human-exp-human-bad-type.idio
	 *
	 * {
	 *   exp-human :~ '(1 2 3)
	 *   exp-send-human C/0i "hello"
	 * }
	 */
	idio_error_param_value_msg ("exp-send-human", "exp-human", exp_human, "5-tuple", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (min_ms > max_ms) {
	/*
	 * Test Case:  expect-errors/exp-send-human-exp-human-min-gt-max.idio
	 *
	 * {
	 *   exp-human :~ '(1 2 3 5 4)
	 *   exp-send-human C/0i "hello"
	 * }
	 */
	idio_error_param_value_msg ("exp-send-human", "exp-human", exp_human, "min-ms > max-ms", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (min_ms > in_ms ||
	min_ms > out_ms) {
	/*
	 * Test Case:  expect-errors/exp-send-human-exp-human-bad-type.idio
	 *
	 * {
	 *   exp-human :~ '( 1 2 3 10 20)
	 *   exp-human :~ '(10 2 3 10 20)
	 *   exp-send-human C/0i "hello"
	 * }
	 */
	idio_error_param_value_msg ("exp-send-human", "exp-human", exp_human, "min-ms > in-ms|out-ms", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (max_ms < in_ms ||
	max_ms < out_ms) {
	/*
	 * Test Case:  expect-errors/exp-send-human-exp-human-bad-type.idio
	 *
	 * {
	 *   exp-human :~ '(10 20 3 5  5)
	 *   exp-human :~ '(10 20 3 5 15)
	 *   exp-send-human C/0i "hello"
	 * }
	 */
	idio_error_param_value_msg ("exp-send-human", "exp-human", exp_human, "max-ms < in-ms|out-ms", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    struct pollfd fds[1];
    fds[0].fd = C_fd;
    fds[0].events = 0;

    size_t slen = IDIO_STRING_LEN (msg);
    size_t i;

    int in_word = 1;

    for (i = 0; i < slen; i++) {
	IDIO cp = idio_string_ref_C (msg, i);

	int not_word = (idio_usi_isa (cp, IDIO_USI_FLAG_White_Space) ||
			idio_usi_isa (cp, IDIO_USI_FLAG_Punctuation));

	intmax_t avg_ms = in_ms;
	if (in_word &&
	    not_word) {
	    avg_ms = out_ms;
	}
	in_word = ! not_word;

	/*
	 * random() is 2^31-1
	 */
	double R = (double) (random () / (0x7fffffff + 0.0));
	int gap = (int) (avg_ms * pow (- log (R), K));

	if (gap < min_ms) {
	    gap = (int) min_ms;
	} else if (gap > max_ms) {
	    gap = (int) max_ms;
	}

	int poll_r = poll (fds, 1, gap);

	if (-1 == poll_r) {
	    if (EINTR != errno) {
		/*
		 * Test Case: ??
		 */
		idio_error_system_errno ("poll", idio_S_nil, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}

	/* check fds? */

	char buf[4];
	int size = 0;
	idio_utf8_code_point (IDIO_UNICODE_VAL (cp), buf, &size);
	write (C_fd, buf, size);
    }

    return idio_S_unspec;
}

void idio_expect_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_expect_module, expect_send_human);
}

void idio_final_expect ()
{
}

void idio_init_expect (void *handle)
{
    idio_expect_module = idio_module (IDIO_SYMBOL ("expect"));

    idio_module_table_register (idio_expect_add_primitives, idio_final_expect, handle);

    idio_expect_exp_human_sym = IDIO_SYMBOL ("exp-human");

    idio_module_export_symbol_value (idio_S_version,
				     idio_string_C_len (EXPECT_SYSTEM_VERSION, sizeof (EXPECT_SYSTEM_VERSION) - 1),
				     idio_expect_module);

}
/* Local Variables: */
/* coding: utf-8-unix */
/* End: */
