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
 * posix-regex.c
 *
 * In particular, regex(3), regcomp(3), regexec(3).
 *
 * This could have been part of libc-wrap.
 *
 */

#include "idio.h"

IDIO idio_posix_regex_REG_BASIC_sym;
IDIO idio_posix_regex_REG_EXTENDED_sym;
IDIO idio_posix_regex_REG_ICASE_sym;
IDIO idio_posix_regex_REG_NOSUB_sym;
IDIO idio_posix_regex_REG_NEWLINE_sym;
IDIO idio_posix_regex_REG_NOTBOL_sym;
IDIO idio_posix_regex_REG_NOTEOL_sym;
#ifdef REG_STARTEND
IDIO idio_posix_regex_REG_STARTEND_sym;
#endif

/*
 * Technically, never returns
 */
static IDIO idio_posix_regex_error (int errcode, regex_t *preg, char *C_func, IDIO c_location)
{
    IDIO_ASSERT (c_location);

    size_t errbufsiz = regerror (errcode, preg, NULL, 0);
    if (errbufsiz) {
	char *errbuf;
	IDIO_GC_ALLOC (errbuf, errbufsiz);
	regerror (errcode, preg, errbuf, errbufsiz);
	IDIO_GC_FREE (errbuf);
	idio_error_printf (c_location, "%s failure", C_func);

	return idio_S_notreached;
    } else {
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
	    if (idio_posix_regex_REG_BASIC_sym == flag) {
		cflags &= ~REG_EXTENDED;
	    } else if (idio_posix_regex_REG_EXTENDED_sym == flag) {
		cflags |= REG_EXTENDED;
	    } else if (idio_posix_regex_REG_ICASE_sym == flag) {
		cflags |= REG_ICASE;
	    } else if (idio_posix_regex_REG_NOSUB_sym == flag) {
		/*
		 * XXX We *always* collect sub-expressions!
		 */
		/* cflags |= REG_NOSUB; */
	    } else if (idio_posix_regex_REG_NEWLINE_sym == flag) {
		cflags |= REG_NEWLINE;
	    } else {
		idio_error (flag, idio_string_C ("regcomp(): unexpected flag"), flag, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    idio_error_param_type ("symbol", flag, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	flags = IDIO_PAIR_T (flags);
    }

    size_t size = 0;
    char *Crx = idio_display_string (rx, &size);

    regex_t *preg;
    IDIO_GC_ALLOC (preg, sizeof (regex_t));

    int errcode = regcomp (preg, Crx, cflags);

    idio_gc_free (Crx);

    if (errcode) {
	idio_debug ("ERROR: regcomp (%s): ", rx);
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
POSIX regex(3)						\n\
							\n\
compile the regular expression in ``rx`` suitable	\n\
for subsequent use in ``regexec``			\n\
						\n\
The ``flags`` are:	 			\n\
REG_EXTENDED		 			\n\
REG_ICASE		 			\n\
REG_NOSUB (ignored)	 			\n\
REG_NEWLINE		 			\n\
						\n\
This code defaults to REG_EXTENDED so there is	\n\
an extra REG_BASIC flag to disable REG_EXTENDED	\n\
						\n\
:param rx: regular expression			\n\
:type rx: string				\n\
:param flags: regcomp flags			\n\
:type flags: list of symbols			\n\
						\n\
:return: compiled regex(3)			\n\
:rtype: C-pointer				\n\
")
{
    IDIO_ASSERT (rx);
    IDIO_ASSERT (flags);

    IDIO_USER_TYPE_ASSERT (string, rx);
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
    while (idio_S_nil != flags) {
	IDIO flag = IDIO_PAIR_H (flags);
	if (idio_isa_symbol (flag)) {
	    if (idio_posix_regex_REG_NOTBOL_sym == flag) {
		eflags |= REG_NOTBOL;
	    } else if (idio_posix_regex_REG_NOTEOL_sym == flag) {
		eflags |= REG_NOTEOL;
#ifdef REG_STARTEND
	    } else if (idio_posix_regex_REG_STARTEND_sym == flag) {
		eflags |= REG_STARTEND;
#endif
	    } else {
		idio_error (flag, idio_string_C ("regexec(): unexpected flag"), flag, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    idio_error_param_type ("symbol", flag, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	flags = IDIO_PAIR_T (flags);
    }

    size_t size = 0;
    char *Cs = idio_display_string (s, &size);

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
	idio_gc_free (Cs);

	if (REG_NOMATCH == errcode) {
	    return idio_S_false;
	} else {
	    idio_debug ("regexec (rx, %s): ", s);
	    idio_posix_regex_error (errcode, preg, "regexec", IDIO_C_FUNC_LOCATION ());
	}
    }

    IDIO r = idio_array_dv (nmatch, idio_S_false);
    for (size_t i = 0; i < nmatch; i++) {
	/*
	 * rm_so == rm_eo == -1		for no subexpression match
	 * rm_so == rm_eo		for empty subexpression
	 */
	if (-1 != matches[i].rm_so) {
	    idio_array_insert_index (r, idio_string_C_len (Cs + matches[i].rm_so, matches[i].rm_eo - matches[i].rm_so), i);
	}
    }

    idio_gc_free (Cs);
    IDIO_GC_FREE (matches);

    return r;
}

IDIO_DEFINE_PRIMITIVE2V_DS ("regexec", regexec, (IDIO rx, IDIO str, IDIO flags), "rx str [flags]", "\
POSIX regex(3)						\n\
							\n\
match the regular expression in ``rx`` against the	\n\
string ``str`` where ``rx`` was compiled using		\n\
``regcomp``						\n\
						\n\
The ``flags`` are:	 			\n\
REG_NOTBOL		 			\n\
REG_NOTEOL		 			\n\
REG_STARTEND (if supported) 			\n\
						\n\
On a successful match an array of the subexpressions	\n\
in ``rx`` is returned with the first (zero-th) being	\n\
the entire matched string.				\n\
						\n\
If a subexpression in ``rx`` matched the corresponding 	\n\
array element will be the matched string.	\n\
						\n\
If a subexpression in ``rx`` did not match the 	\n\
corresponding array element will be #f	.	\n\
						\n\
:param rx: compiled regular expression		\n\
:type rx: C-pointer				\n\
:param str: string to match against		\n\
:type rx: string				\n\
:param flags: regexec flags			\n\
:type flags: list of symbols			\n\
						\n\
:return: array of matching subexpressions or #f for no match	\n\
:rtype: array or #f				\n\
")
{
    IDIO_ASSERT (rx);
    IDIO_ASSERT (str);
    IDIO_ASSERT (flags);

    IDIO_USER_TYPE_ASSERT (C_pointer, rx);
    IDIO_USER_TYPE_ASSERT (string, str);
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
    idio_module_table_register (idio_posix_regex_add_primitives, NULL);

    /*
     * XXX REG_BASIC is not a thing in regex.h however we could do
     * with a "not-REG_EXTENDED" flag
     */
    idio_posix_regex_REG_BASIC_sym = idio_symbols_C_intern ("REG_BASIC");
    idio_posix_regex_REG_EXTENDED_sym = idio_symbols_C_intern ("REG_EXTENDED");
    idio_posix_regex_REG_ICASE_sym = idio_symbols_C_intern ("REG_ICASE");
    idio_posix_regex_REG_NOSUB_sym = idio_symbols_C_intern ("REG_NOSUB");
    idio_posix_regex_REG_NEWLINE_sym = idio_symbols_C_intern ("REG_NEWLINE");
    idio_posix_regex_REG_NOTBOL_sym = idio_symbols_C_intern ("REG_NOTBOL");
    idio_posix_regex_REG_NOTEOL_sym = idio_symbols_C_intern ("REG_NOTEOL");
#ifdef REG_STARTEND
    idio_posix_regex_REG_STARTEND_sym = idio_symbols_C_intern ("REG_STARTEND");
#endif
}

