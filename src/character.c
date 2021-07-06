/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * character.c
 *
 */

/**
 * DOC: Idio ``character`` type
 *
 * An Idio ``character`` represents a Unicode code point.  Or should
 * do, in practice, it represents an ASCII code point.
 *
 * We can extend ease of use by making the nominal names of the
 * characters available for the reader to consume.
 *
 * There are the ASCII C0 control names:
 *
 *  ``nul``, ``soh``, ``stx``, ``etx``, etc.
 *
 * and ANSI C names:
 *
 *  ``alarm``, ``backspace``, ``tab``, ``linefeed``, etc.
 *
 * Example:
 *
 * .. code-block:: scheme
 *
 *     display #\lf
 *     display #\newline
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <ctype.h>
#include <ffi.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "closure.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "hash.h"
#include "keyword.h"
#include "pair.h"
#include "primitive.h"
#include "vm.h"

/**
 * static idio_characters_hash - table of known character names to values
 *
 */
static IDIO idio_characters_hash = idio_S_nil;

int idio_character_C_eqp (void *s1, void *s2)
{
    /*
     * We should only be here for idio_characters_hash key comparisons
     * but hash keys default to idio_S_nil
     */
    if (idio_S_nil == s1 ||
	idio_S_nil == s2) {
	return 0;
    }

    return (0 == strcmp ((const char *) s1, (const char *) s2));
}

idio_hi_t idio_character_C_hash (IDIO h, void *s)
{
    size_t hvalue = (uintptr_t) s;

    if (idio_S_nil != s) {
	hvalue = idio_hash_default_hash_C_string_C (strlen ((char *) s), s);
    }

    return (hvalue & IDIO_HASH_MASK (h));
}

IDIO idio_characters_C_intern (char *s, IDIO v)
{
    IDIO_C_ASSERT (s);

    IDIO c = idio_hash_ref (idio_characters_hash, s);

    if (idio_S_unspec == c) {
	size_t blen = strlen (s);
	char *copy = idio_alloc (blen + 1);
	strcpy (copy, s);
	idio_hash_put (idio_characters_hash, copy, v);
    }

    return v;
}

/**
 * idio_character_lookup - return the code point of a named character
 * @s: character name (a C string)
 *
 * Return:
 * The Idio value of the code point or %idio_S_unspec.
 */
IDIO idio_character_lookup (char *s)
{
    IDIO_C_ASSERT (s);

    return idio_hash_ref (idio_characters_hash, s);
}

int idio_isa_character (IDIO o)
{
    IDIO_ASSERT (o);

    if (((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_CHARACTER_MARK) {
	intptr_t cv = IDIO_CHARACTER_VAL (o);

	/*
	 * https://www.gnu.org/software/guile/manual/html_node/Characters.html
	 *
	 * Scheme says a character is a valid Unicode code point.
	 *
	 * This covers the current implementation of ASCII-only code points.
	 */
	if ((cv >= 0 &&
	     cv <= 0xd7ff) ||
	    (cv >= 0xe000 &&
	     cv <= 0x10ffff)) {
	    return 1;
	}
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("char?",  char_p, (IDIO o), "o", "\
test if `o` is an character			\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an character, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_character (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("char->integer",  char2integer, (IDIO c), "c", "\
convert `c` to an integer				\n\
						\n\
:param c: character to convert			\n\
						\n\
:return: integer (fixnum) conversion of `c`	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    return idio_fixnum (IDIO_CHARACTER_VAL (c));
}

IDIO_DEFINE_PRIMITIVE1_DS ("char-alphabetic?",  char_alphabetic_p, (IDIO c), "c", "\
test if `c` is alphabetic			\n\
						\n\
:param c: character to test			\n\
						\n\
This implementation uses libc isalpha()		\n\
						\n\
:return: #t if `c` is an alphabetic character, #f otherwise	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    IDIO r = idio_S_false;

    if (isalpha (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("char-numeric?",  char_numeric_p, (IDIO c), "c", "\
test if `c` is numeric			\n\
						\n\
:param c: character to test			\n\
						\n\
This implementation uses libc isdigit()		\n\
						\n\
:return: #t if `c` is an numeric character, #f otherwise	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    IDIO r = idio_S_false;

    if (isdigit (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("char-whitespace?",  char_whitespace_p, (IDIO c), "c", "\
test if `c` is whitespace			\n\
						\n\
:param c: character to test			\n\
						\n\
This implementation uses libc isblank() and isspace()		\n\
						\n\
:return: #t if `c` is a whitespace character, #f otherwise	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    IDIO r = idio_S_false;

    if (isblank (IDIO_CHARACTER_VAL (c)) ||
	isspace (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

intptr_t idio_character_ival (IDIO ic)
{
    IDIO_ASSERT (ic);

    IDIO_USER_TYPE_ASSERT (character, ic);

    intptr_t c = IDIO_CHARACTER_VAL (ic);

    intptr_t r = c;

    /*
     * ASCII only
     */
    if (c < 0x80) {
	r = tolower (c);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("char-downcase",  char_downcase, (IDIO c), "c", "\
return lowercase variant of `c`			\n\
						\n\
:param c: character to convert			\n\
						\n\
This implementation uses libc tolower()		\n\
						\n\
:return: lowercase variant of `c`	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    return IDIO_CHARACTER (idio_character_ival (c));
}

IDIO_DEFINE_PRIMITIVE1_DS ("char-lower-case?",  char_lower_case_p, (IDIO c), "c", "\
test if `c` is lower case			\n\
						\n\
:param c: character to test			\n\
						\n\
This implementation uses libc islower()		\n\
						\n\
:return: #t if `c` is a lower case character, #f otherwise	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    IDIO r = idio_S_false;

    if (islower (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("char-upcase",  char_upcase, (IDIO c), "c", "\
return uppercase variant of `c`			\n\
						\n\
:param c: character to convert			\n\
						\n\
This implementation uses libc toupper()		\n\
						\n\
:return: uppercase variant of `c`	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    return IDIO_CHARACTER (toupper (IDIO_CHARACTER_VAL (c)));
}

IDIO_DEFINE_PRIMITIVE1_DS ("char-upper-case?",  char_upper_case_p, (IDIO c), "c", "\
test if `c` is upper case			\n\
						\n\
:param c: character to test			\n\
						\n\
This implementation uses libc isupper()		\n\
						\n\
:return: #t if `c` is a upper case character, #f otherwise	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_USER_TYPE_ASSERT (character, c);

    IDIO r = idio_S_false;

    if (isupper (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

/*
 * All the char-*? are essentially identical
 */
#define IDIO_DEFINE_CHARACTER_PRIMITIVE2V(name,cname,cmp,accessor)	\
    IDIO_DEFINE_PRIMITIVE2V (name, cname, (IDIO c1, IDIO c2, IDIO args)) \
    {									\
	IDIO_ASSERT (c1);						\
	IDIO_ASSERT (c2);						\
	IDIO_ASSERT (args);						\
									\
	IDIO_USER_TYPE_ASSERT (character, c1);				\
	IDIO_USER_TYPE_ASSERT (character, c2);				\
	IDIO_USER_TYPE_ASSERT (list, args);				\
									\
	args = idio_pair (c2, args);					\
									\
	IDIO r = idio_S_true;						\
									\
	while (idio_S_nil != args) {					\
	    c2 = IDIO_PAIR_H (args);					\
	    IDIO_USER_TYPE_ASSERT (character, c2);			\
	    if (! (accessor (c1) cmp accessor (c2))) {			\
		r = idio_S_false;					\
		break;							\
	    }								\
	    c1 = c2;							\
	    args = IDIO_PAIR_T (args);					\
	}								\
									\
	return r;							\
    }

#define IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_CHARACTER_PRIMITIVE2V (name, char_ ## cname ## _p, cmp, IDIO_CHARACTER_VAL)

#define IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_CHARACTER_PRIMITIVE2V (name, char_ci_ ## cname ## _p, cmp, idio_character_ival)

IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2V ("char-ci<=?", le, <=)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2V ("char-ci<?", lt, <)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2V ("char-ci=?", eq, ==)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2V ("char-ci>=?", ge, >=)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2V ("char-ci>?", gt, >)

IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2V ("char<=?", le, <=)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2V ("char<?", lt, <)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2V ("char=?", eq, ==)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2V ("char>=?", ge, >=)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2V ("char>?", gt, >)

#define IDIO_CHARACTER_INTERN_C(name,c)	(idio_characters_C_intern (name, IDIO_CHARACTER (c)))

void idio_character_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (char_p);
    IDIO_ADD_PRIMITIVE (char2integer);
    IDIO_ADD_PRIMITIVE (char_alphabetic_p);
    IDIO_ADD_PRIMITIVE (char_numeric_p);
    IDIO_ADD_PRIMITIVE (char_downcase);
    IDIO_ADD_PRIMITIVE (char_lower_case_p);
    IDIO_ADD_PRIMITIVE (char_upcase);
    IDIO_ADD_PRIMITIVE (char_upper_case_p);
    IDIO_ADD_PRIMITIVE (char_whitespace_p);

    /*
     * The char_* functions were autogenerated but we still need to
     * add the sigstr and docstr
     */
    IDIO fvi = IDIO_ADD_PRIMITIVE (char_le_p);
    IDIO p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted non-decreasing			\n\
									\n\
:param c1: char								\n\
:param c2: char								\n\
:param ...: chars							\n\
									\n\
:return: #t if arguments are sorted non-decreasing, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_lt_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted increasing		\n\
								\n\
:param c1: char							\n\
:param c2: char							\n\
:param ...: chars						\n\
								\n\
:return: #t if arguments are sorted increasing, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_eq_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are equal			\n\
							\n\
:param c1: char						\n\
:param c2: char						\n\
:param ...: chars					\n\
							\n\
:return: #t if arguments are equal, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_ge_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted non-increasing			\n\
									\n\
:param c1: char								\n\
:param c2: char								\n\
:param ...: chars							\n\
									\n\
:return: #t if arguments are sorted non-increasing, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_gt_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted decreasing		\n\
								\n\
:param c1: char							\n\
:param c2: char							\n\
:param ...: chars						\n\
								\n\
:return: #t if arguments are sorted decreasing, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_ci_le_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted non-decreasing case-insensitively		\n\
											\n\
:param c1: char										\n\
:param c2: char										\n\
:param ...: chars									\n\
											\n\
This implementation uses libc tolower()							\n\
											\n\
:return: #t if arguments are sorted non-decreasing case-insensitively, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_ci_lt_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted increasing case-insensitively		\n\
										\n\
:param c1: char									\n\
:param c2: char									\n\
:param ...: chars								\n\
										\n\
This implementation uses libc tolower()						\n\
										\n\
:return: #t if arguments are sorted increasing case-insensitively, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_ci_eq_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are equal case-insensitively		\n\
									\n\
:param c1: char								\n\
:param c2: char								\n\
:param ...: chars							\n\
									\n\
This implementation uses libc tolower()					\n\
									\n\
:return: #t if arguments are equal case-insensitively, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_ci_ge_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted non-increasing case-insensitively		\n\
											\n\
:param c1: char										\n\
:param c2: char										\n\
:param ...: chars									\n\
											\n\
This implementation uses libc tolower()							\n\
											\n\
:return: #t if arguments are sorted non-increasing case-insensitively, #f otherwise	\n\
");

    fvi = IDIO_ADD_PRIMITIVE (char_ci_gt_p);
    p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if character arguments are sorted decreasing case-insensitively		\n\
										\n\
:param c1: char									\n\
:param c2: char									\n\
:param ...: chars								\n\
										\n\
This implementation uses libc tolower()						\n\
										\n\
:return: #t if arguments are sorted decreasing case-insensitively, #f otherwise	\n\
");
}

void idio_init_character ()
{
    idio_module_table_register (idio_character_add_primitives, NULL);

    idio_characters_hash = idio_hash (1<<7, idio_character_C_eqp, idio_character_C_hash, idio_S_nil, idio_S_nil);
    idio_gc_protect_auto (idio_characters_hash);
    IDIO_HASH_FLAGS (idio_characters_hash) |= IDIO_HASH_FLAG_STRING_KEYS;

    /* ASCII C0 control characters */
    IDIO_CHARACTER_INTERN_C ("nul", 0);
    IDIO_CHARACTER_INTERN_C ("soh", 1);
    IDIO_CHARACTER_INTERN_C ("stx", 2);
    IDIO_CHARACTER_INTERN_C ("etx", 3);
    IDIO_CHARACTER_INTERN_C ("eot", 4);
    IDIO_CHARACTER_INTERN_C ("enq", 5);
    IDIO_CHARACTER_INTERN_C ("ack", 6);
    IDIO_CHARACTER_INTERN_C ("bel", 7);
    IDIO_CHARACTER_INTERN_C ("bs", 8);
    IDIO_CHARACTER_INTERN_C ("ht", 9);
    IDIO_CHARACTER_INTERN_C ("lf", 10);
    IDIO_CHARACTER_INTERN_C ("vt", 11);
    IDIO_CHARACTER_INTERN_C ("ff", 12);
    IDIO_CHARACTER_INTERN_C ("cr", 13);
    IDIO_CHARACTER_INTERN_C ("so", 14);
    IDIO_CHARACTER_INTERN_C ("si", 15);
    IDIO_CHARACTER_INTERN_C ("dle", 16);
    IDIO_CHARACTER_INTERN_C ("dcl", 17);
    IDIO_CHARACTER_INTERN_C ("dc2", 18);
    IDIO_CHARACTER_INTERN_C ("dc3", 19);
    IDIO_CHARACTER_INTERN_C ("dc4", 20);
    IDIO_CHARACTER_INTERN_C ("nak", 21);
    IDIO_CHARACTER_INTERN_C ("syn", 22);
    IDIO_CHARACTER_INTERN_C ("etb", 23);
    IDIO_CHARACTER_INTERN_C ("can", 24);
    IDIO_CHARACTER_INTERN_C ("em", 25);
    IDIO_CHARACTER_INTERN_C ("sub", 26);
    IDIO_CHARACTER_INTERN_C ("esc", 27);
    IDIO_CHARACTER_INTERN_C ("fs", 28);
    IDIO_CHARACTER_INTERN_C ("gs", 29);
    IDIO_CHARACTER_INTERN_C ("rs", 30);
    IDIO_CHARACTER_INTERN_C ("us", 31);
    IDIO_CHARACTER_INTERN_C ("sp", 32);

    /* C and other common names */
    /* nul as above */
    IDIO_CHARACTER_INTERN_C ("alarm",		'\a'); /* 0x07 */
    IDIO_CHARACTER_INTERN_C ("backspace",	'\b'); /* 0x08 */
    IDIO_CHARACTER_INTERN_C ("tab",		'\t'); /* 0x09 */
    IDIO_CHARACTER_INTERN_C ("linefeed",	'\n'); /* 0x0a */
    IDIO_CHARACTER_INTERN_C ("newline",		'\n'); /* 0x0a */
    IDIO_CHARACTER_INTERN_C ("vtab",		'\v'); /* 0x0b */
    IDIO_CHARACTER_INTERN_C ("page",		'\f'); /* 0x0c */
    IDIO_CHARACTER_INTERN_C ("return",		'\r'); /* 0x0d */
    IDIO_CHARACTER_INTERN_C ("carriage-return", '\r'); /* 0x0d */
    IDIO_CHARACTER_INTERN_C ("esc",		0x1b); /* 0x1b */
    IDIO_CHARACTER_INTERN_C ("escape",		0x1b); /* 0x1b */
    IDIO_CHARACTER_INTERN_C ("space",		' ');  /* 0x20 */
    IDIO_CHARACTER_INTERN_C ("del",		0x7f); /* 0x7f */
    IDIO_CHARACTER_INTERN_C ("delete",		0x7f); /* 0x7f */

    /* Unicode code points... */
}

