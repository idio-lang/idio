/*
 * Copyright (c) 2020-2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * posix-regex.c
 *
 * In particular, regex(3), regcomp(3), regexec(3).
 *
 * This could have been part of libc-wrap.
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <regex.h>
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
#include "c-type.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "libc-wrap.h"
#include "pair.h"
#include "posix-regex.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"

static IDIO idio_S_REG_BASIC;
static IDIO idio_S_REG_EXTENDED;
static IDIO idio_S_REG_ICASE;
static IDIO idio_S_REG_NOSUB;
static IDIO idio_S_REG_NEWLINE;
static IDIO idio_S_REG_NOTBOL;
static IDIO idio_S_REG_NOTEOL;
#if ! defined (IDIO_NO_REG_STARTEND)
static IDIO idio_S_REG_STARTEND;
#endif
static IDIO idio_S_REG_VERBOSE;

/*
 * Technically, never returns
 */
static IDIO idio_posix_regex_error (int errcode, regex_t *preg, char const *C_func, IDIO c_location)
{
    IDIO_ASSERT (c_location);

    size_t errbufsiz = regerror (errcode, preg, NULL, 0);
    if (errbufsiz) {
	char *errbuf;
	IDIO_GC_ALLOC (errbuf, errbufsiz);
	regerror (errcode, preg, errbuf, errbufsiz);

	IDIO msh;
	IDIO lsh;
	IDIO dsh;
	idio_error_init (&msh, &lsh, &dsh, c_location);

	idio_display_C (C_func, msh);
	idio_display_C (" failure: ", msh);
	idio_display_C (errbuf, msh);

	IDIO_GC_FREE (errbuf, errbufsiz);

	idio_error_raise_cont (idio_condition_rt_regex_error_type,
			       IDIO_LIST3 (idio_get_output_string (msh),
					   idio_get_output_string (lsh),
					   idio_get_output_string (dsh)));

	return idio_S_notreached;
    } else {
	/*
	 * Code coverage:
	 *
	 * First get regerror() to fail...
	 */
	idio_error_printf (c_location, "%s failure: regerror() failed processing errcode %d", C_func, errcode);

	return idio_S_notreached;
    }
}

IDIO idio_posix_regex_regcomp (IDIO rx, IDIO flags)
{
    IDIO_ASSERT (rx);
    IDIO_ASSERT (flags);

    IDIO_TYPE_ASSERT (string, rx);
    IDIO_TYPE_ASSERT (list, flags);

    int cflags = REG_EXTENDED;
    while (idio_S_nil != flags) {
	IDIO flag = IDIO_PAIR_H (flags);
	if (idio_isa_symbol (flag)) {
	    if (idio_S_REG_BASIC == flag) {
		cflags &= ~REG_EXTENDED;
	    } else if (idio_S_REG_EXTENDED == flag) {
		cflags |= REG_EXTENDED;
	    } else if (idio_S_REG_ICASE == flag) {
		cflags |= REG_ICASE;
	    } else if (idio_S_REG_NOSUB == flag) {
		/*
		 * XXX We *always* collect sub-expressions!
		 */
		/* cflags |= REG_NOSUB; */
	    } else if (idio_S_REG_NEWLINE == flag) {
		cflags |= REG_NEWLINE;
	    } else {
		/*
		 * Test Case: posix-regex-errors/regcomp-bad-flag.idio
		 *
		 * regcomp "" 'REG_PCRE
		 */
		idio_error_param_value_msg ("regcomp", "flag", flag, "unexpected flag", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: posix-regex-errors/regcomp-bad-flag-type.idio
	     *
	     * regcomp "" #t
	     */
	    idio_error_param_type ("symbol", flag, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	flags = IDIO_PAIR_T (flags);
    }

    size_t Crx_size = 0;
    char *Crx = idio_display_string (rx, &Crx_size);

    regex_t *preg;
    IDIO_GC_ALLOC (preg, sizeof (regex_t));

    int errcode = regcomp (preg, Crx, cflags);

    idio_gc_free (Crx, Crx_size);

    if (errcode) {
	regfree (preg);
	IDIO_GC_FREE (preg, sizeof (regex_t));

	/*
	 * Test Case: posix-regex-errors/regcomp-bad-pattern.idio
	 *
	 * regcomp "*"
	 *
	 * REG_BADRPT -- according to the manpage, "Invalid preceding
	 * regular expression" from regerror()
	 */
	idio_posix_regex_error (errcode, preg, "regcomp", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * regfree() is required to free up undocumented stuff but not the
     * regex_t itself:
     *
     *   Calling 'regfree' frees all the storage that '*COMPILED'
     *   points to.  This includes various internal fields of the
     *   'regex_t' structure that aren't documented in this manual.
     *
     *   'regfree' does not free the object '*COMPILED' itself.
     *
     * Hence we require a finaliser to call regfree() and use
     * idio_C_pointer_free_me() to ensure that we free *preg.
     */
    IDIO r = idio_C_pointer_free_me (preg);

    idio_gc_register_finalizer (r, idio_posix_regex_regcomp_finalizer);

    return r;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("regcomp", regcomp, (IDIO rx, IDIO flags), "rx [flags]", "\
POSIX :manpage:`regex(3)`				\n\
							\n\
compile the regular expression in `rx` suitable		\n\
for subsequent use in :ref:`regexec <regexec>`	\n\
						\n\
The `flags` are:	 			\n\
``REG_EXTENDED``	 			\n\
``REG_ICASE``		 			\n\
``REG_NOSUB`` (ignored)	 			\n\
``REG_NEWLINE``		 			\n\
						\n\
This code defaults to ``REG_EXTENDED`` so there is	\n\
an extra ``REG_BASIC`` flag to disable ``REG_EXTENDED``	\n\
						\n\
:param rx: regular expression			\n\
:type rx: string				\n\
:param flags: regcomp flags			\n\
:type flags: list of symbols			\n\
:return: compiled :manpage:`regex(3)`		\n\
:rtype: C/pointer				\n\
")
{
    IDIO_ASSERT (rx);
    IDIO_ASSERT (flags);

    /*
     * Test Case: posix-regex-errors/regcomp-bad-rx-type.idio
     *
     * regcomp #t
     */
    IDIO_USER_TYPE_ASSERT (string, rx);
    /*
     * Test Case: n/a
     *
     * flags is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, flags);

    return idio_posix_regex_regcomp (rx, flags);
}

void idio_posix_regex_regcomp_finalizer (IDIO rx)
{
    IDIO_ASSERT (rx);

    IDIO_TYPE_ASSERT (C_pointer, rx);

    regfree (IDIO_C_TYPE_POINTER_P (rx));
}

IDIO idio_posix_regex_regexec (IDIO rx, IDIO s, IDIO flags)
{
    IDIO_ASSERT (rx);
    IDIO_ASSERT (s);
    IDIO_ASSERT (flags);

    IDIO_TYPE_ASSERT (C_pointer, rx);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (list, flags);

    int eflags = 0;
    int verbose = 0;
    while (idio_S_nil != flags) {
	IDIO flag = IDIO_PAIR_H (flags);
	if (idio_isa_symbol (flag)) {
	    if (idio_S_REG_NOTBOL == flag) {
		eflags |= REG_NOTBOL;
	    } else if (idio_S_REG_NOTEOL == flag) {
		eflags |= REG_NOTEOL;
#if ! defined (IDIO_NO_REG_STARTEND)
	    } else if (idio_S_REG_STARTEND == flag) {
		/*
		 * We don't support this anyway as we're not in a
		 * position to pre-set pmatch[0]
		 */
		/* eflags |= REG_STARTEND; */
#endif
	    } else if (idio_S_REG_VERBOSE == flag) {
		verbose = 1;
	    } else {
		/*
		 * Test Case: posix-regex-errors/regexec-bad-flag.idio
		 *
		 * regexec (regcomp "") "" 'REG_BOTH
		 */
		idio_error_param_value_msg ("regexec", "flag", flag, "unexpected flag", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: posix-regex-errors/regexec-bad-flag-type.idio
	     *
	     * regexec (regcomp "") "" #t
	     */
	    idio_error_param_type ("symbol", flag, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	flags = IDIO_PAIR_T (flags);
    }

    size_t Cs_size = 0;
    char *Cs = idio_display_string (s, &Cs_size);

    regex_t *preg = IDIO_C_TYPE_POINTER_P (rx);
    /*
     * Subexpressions do not include the main matched expression -- so
     * add one
     */
    size_t nmatch = preg->re_nsub + 1;

    regmatch_t *matches;
    IDIO_GC_ALLOC (matches, nmatch * sizeof (regmatch_t));

    int errcode = regexec (preg, Cs, nmatch, matches, eflags);

    if (errcode) {
	IDIO_GC_FREE (matches, nmatch * sizeof (regmatch_t));

	idio_gc_free (Cs, Cs_size);

	if (REG_NOMATCH == errcode) {
	    return idio_S_false;
	}
    }

    IDIO r = idio_array_dv (nmatch, idio_S_false);
    for (size_t i = 0; i < nmatch; i++) {
	/*
	 * rm_so == rm_eo == -1		for no subexpression match
	 * rm_so == rm_eo		for empty subexpression
	 */
	if (-1 != matches[i].rm_so) {
	    IDIO match = idio_string_C_len (Cs + matches[i].rm_so, matches[i].rm_eo - matches[i].rm_so);

	    if (verbose) {
		/*
		 * Grr!  I'm impressed I stumbled across this, though,
		 * as it requires something of mine use non-ASCII code
		 * points.  In this case U+2018/U+2019 around an error
		 * message.  All hail buggy startups!  :)
		 *
		 * rm_so/rm_eo are in characters, as in C characters,
		 * not code points.  Cue an expensive recalculation.
		 */
		IDIO prefix = idio_string_C_len (Cs, matches[i].rm_so);
		size_t prefix_len = idio_string_len (prefix);
		idio_array_insert_index (r,
					 IDIO_LIST3 (match,
						     idio_integer (prefix_len),
						     idio_integer (prefix_len + idio_string_len (match))),
					 i);
	    } else {
		idio_array_insert_index (r, match, i);
	    }
	}
    }

    idio_gc_free (Cs, Cs_size);

    IDIO_GC_FREE (matches, nmatch * sizeof (regmatch_t));

    return r;
}

IDIO_DEFINE_PRIMITIVE2V_DS ("regexec", regexec, (IDIO rx, IDIO str, IDIO flags), "rx str [flags]", "\
POSIX :manpage:`regex(3)`				\n\
							\n\
match the regular expression in `rx` against the	\n\
string `str` where `rx` was compiled using		\n\
:ref:`regcomp <regcomp>`				\n\
						\n\
The `flags` are:	 			\n\
``REG_NOTBOL``		 			\n\
``REG_NOTEOL``		 			\n\
``REG_STARTEND`` (if supported, see below)	\n\
						\n\
``REG_VERBOSE`` return verbose results		\n\
						\n\
On a successful match an array of the subexpressions	\n\
in `rx` is returned with the first (zero-th) being	\n\
the entire matched string.				\n\
						\n\
If a subexpression in `rx` matched the corresponding 	\n\
array element will be the matched string.	\n\
						\n\
If a subexpression in `rx` did not match the 	\n\
corresponding array element will be ``#f``.	\n\
						\n\
:param rx: compiled regular expression		\n\
:type rx: C/pointer				\n\
:param str: string to match against		\n\
:type rx: string				\n\
:param flags: regexec flags			\n\
:type flags: list of symbols			\n\
:return: see below				\n\
:rtype: array or ``#f``				\n\
						\n\
By default `regexec` returns an array of	\n\
matching subexpressions or ``#f`` for no match.	\n\
						\n\
If ``REG_VERBOSE`` is passed in flags then each	\n\
element of the array is a list of the matched	\n\
sub-expression, its starting offset and its	\n\
ending offset plus one (suitable for		\n\
:ref:`substring <substring>`).			\n\
						\n\
``REG_STARTEND`` (if supported)	is a valid	\n\
:lname:`C` flag and accepted here but is	\n\
ignored as there is no means to pre-supply	\n\
``pmatch[0]`` (see :manpage:`regexec(3)`).	\n\
")
{
    IDIO_ASSERT (rx);
    IDIO_ASSERT (str);
    IDIO_ASSERT (flags);

    /*
     * Test Case: posix-regex-errors/regexec-bad-rx-type.idio
     *
     * regexec #t #t
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, rx);
    /*
     * Test Case: posix-regex-errors/regexec-bad-str-type.idio
     *
     * regexec (regcomp "") #t
     */
    IDIO_USER_TYPE_ASSERT (string, str);
    /*
     * Test Case: n/a
     *
     * flags is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, flags);

    return idio_posix_regex_regexec (rx, str, flags);
}

void idio_posix_regex_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (regcomp);
    IDIO_ADD_PRIMITIVE (regexec);
}

void idio_init_posix_regex ()
{
    idio_module_table_register (idio_posix_regex_add_primitives, NULL, NULL);

    /*
     * XXX REG_BASIC is not a thing in regex.h however we could do
     * with a "not-REG_EXTENDED" flag
     */
    idio_S_REG_BASIC    = IDIO_SYMBOL ("REG_BASIC");
    idio_S_REG_EXTENDED = IDIO_SYMBOL ("REG_EXTENDED");
    idio_S_REG_ICASE    = IDIO_SYMBOL ("REG_ICASE");
    idio_S_REG_NOSUB    = IDIO_SYMBOL ("REG_NOSUB");
    idio_S_REG_NEWLINE  = IDIO_SYMBOL ("REG_NEWLINE");
    idio_S_REG_NOTBOL   = IDIO_SYMBOL ("REG_NOTBOL");
    idio_S_REG_NOTEOL   = IDIO_SYMBOL ("REG_NOTEOL");
#ifdef IDIO_NO_REG_STARTEND
    idio_add_feature (IDIO_SYMBOL ("IDIO_NO_REG_STARTEND"));
#else
    idio_S_REG_STARTEND = IDIO_SYMBOL ("REG_STARTEND");
#endif
    idio_S_REG_VERBOSE  = IDIO_SYMBOL ("REG_VERBOSE");
}
