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
 * vars.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "idio-system.h"

#include "gc.h"
#include "idio.h"

#include "bignum.h"
#include "c-type.h"
#include "command.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "symbol.h"
#include "vm.h"

IDIO idio_vars_IFS_sym;
#define IDIO_VARS_IFS_DEFAULT	" \t\n"

IDIO idio_vars_suppress_exit_on_error_sym;
IDIO idio_vars_suppress_pipefail_sym;
IDIO idio_vars_suppress_async_command_report_sym;

static int idio_vars_set_dynamic_default (IDIO name, IDIO val)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (val);

    IDIO_TYPE_ASSERT (symbol, name);

    IDIO VARS = idio_module_current_symbol_value_recurse (name, IDIO_LIST1 (idio_S_false));
    if (idio_S_false == VARS) {
	idio_dynamic_extend (name, name, val, idio_default_eenv);
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE0_DS ("RANDOM/get", RANDOM_get, (void), "", "\
in C, :samp:`random ()`				\n\
a wrapper to libc :manpage:`random(3)`		\n\
						\n\
Return a random non-negative 32-bit number	\n\
						\n\
Normally accessed as the variable :ref:`RANDOM`	\n\
						\n\
:return: random non-negative 32-bit number	\n\
:rtype: integer					\n\
")
{
    return idio_integer (random ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("RANDOM/set", RANDOM_set, (IDIO seed), "seed", "\
in C, :samp:`srandom ({seed})`			\n\
a wrapper to libc :manpage:`srandom(3)`		\n\
						\n\
Seed the random number generator		\n\
						\n\
Normally invoked by setting the variable :ref:`RANDOM`	\n\
						\n\
:param seed: seed integer			\n\
:type seed: integer				\n\
:return: ``#<unspec>``				\n\
						\n\
Negative values for `seed` will be implicitly	\n\
converted to C unsigned	values.			\n\
")
{
    IDIO_ASSERT (seed);

    unsigned int C_seed = 1;
    if (idio_isa_C_uint (seed)) {
	C_seed = IDIO_C_TYPE_uint (seed);
    } else if (idio_isa_fixnum (seed)) {
	C_seed = IDIO_FIXNUM_VAL (seed);
    } else if (idio_isa_bignum (seed)) {
	if (IDIO_BIGNUM_INTEGER_P (seed)) {
	    C_seed = idio_bignum_ptrdiff_t_value (seed);
	} else {
	    IDIO seed_i = idio_bignum_real_to_integer (seed);
	    if (idio_S_nil == seed_i) {
		/*
		 * Test Case: libc-errors/RANDOM-set-float.idio
		 *
		 * RANDOM = 1.1
		 */
		idio_error_param_value_exp ("RANDOM", "seed", seed, "integer bignum", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		C_seed = idio_bignum_ptrdiff_t_value (seed_i);
	    }
	}
    } else {
	/*
	 * Test Case: libc-errors/RANDOM-set-bad-type.idio
	 *
	 * RANDOM = #t
	 */
	idio_error_param_type ("integer C/uint|fixnum|bignum", seed, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    srandom (C_seed);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0_DS ("SECONDS/get", SECONDS_get, (void), "", "\
Return the VM's elapsed running time in seconds	\n\
						\n\
Normally accessed as the variable :ref:`SECONDS`	\n\
						\n\
:return: elapsed VM running time		\n\
:rtype: integer					\n\
")
{
    return idio_integer (idio_vm_elapsed ());
}

IDIO_DEFINE_PRIMITIVE0_DS ("%suppress-rcse/get", suppress_rcse_get, (void), "", "\
Return the VM's \"suppress rcse\" state		\n\
						\n\
:return: VM's \"suppress rcse\" state		\n\
:rtype: boolean					\n\
")
{
    return idio_command_suppress_rcse;
}

void idio_vars_add_primitives ()
{
    /*
     * Seed the random number generator -- which requires an unsigned
     * int.
     *
     * https://en.wikipedia.org/wiki/Entropy-supplying_system_calls
     * describes the problems in getting some entropy albeit I'm not
     * sure anyone should be relying on RANDOM returning anything
     * truly cryptographically worthy.
     *
     * getrandom() isn't portable (even across Linux platforms) and we
     * can't rely on getentropy() either (missing on older SunOS, Mac
     * OS X etc.) so we need to cobble something together ourselves.
     *
     * 1. try /dev/urandom
     *
     * 2. try something to do with the time
     */
    size_t buflen = sizeof (unsigned int);
    char buf[buflen];

    int use_time = 0;
    int fd = open ("/dev/urandom", O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
	int n = read (fd, buf, buflen);
	if (n == (ssize_t) buflen) {
	    srandom ((unsigned int) *((unsigned int *) buf));
	} else {
	    use_time = 1;
	}
	close (fd);
    } else {
	use_time = 1;
    }

    if (use_time) {
	/*
	 * This is a bit old-school (and probably rubbish by today's
	 * standards).
	 *
	 * Should we be XOR'ing this with some tv_usec or tv_nsec?
	 */
	srandom ((unsigned int) time (NULL));
    }

    IDIO geti;
    IDIO seti;
    geti = IDIO_ADD_PRIMITIVE (RANDOM_get);
    seti = IDIO_ADD_PRIMITIVE (RANDOM_set);
    idio_module_add_computed_symbol (IDIO_SYMBOLS_C_INTERN ("RANDOM"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), idio_Idio_module);

    geti = IDIO_ADD_PRIMITIVE (SECONDS_get);
    idio_module_add_computed_symbol (IDIO_SYMBOLS_C_INTERN ("SECONDS"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_Idio_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_command_module, suppress_rcse_get);
    idio_module_export_computed_symbol (IDIO_SYMBOLS_C_INTERN ("%suppress-rcse"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_command_module);

    idio_vars_set_dynamic_default (idio_vars_IFS_sym, idio_string_C_len (IDIO_STATIC_STR_LEN (IDIO_VARS_IFS_DEFAULT)));
    idio_vars_set_dynamic_default (idio_vars_suppress_exit_on_error_sym, idio_S_false);
    idio_vars_set_dynamic_default (idio_vars_suppress_pipefail_sym, idio_S_false);
    idio_vars_set_dynamic_default (idio_vars_suppress_async_command_report_sym, idio_S_false);
}

void idio_final_vars ()
{
}

void idio_init_vars ()
{
    idio_module_table_register (idio_vars_add_primitives, idio_final_vars, NULL);

    idio_vars_IFS_sym                           = IDIO_SYMBOLS_C_INTERN ("IFS");
    idio_vars_suppress_exit_on_error_sym        = IDIO_SYMBOLS_C_INTERN ("suppress-exit-on-error!");
    idio_vars_suppress_pipefail_sym             = IDIO_SYMBOLS_C_INTERN ("suppress-pipefail!");
    idio_vars_suppress_async_command_report_sym = IDIO_SYMBOLS_C_INTERN ("suppress-async-command-report!");
}

