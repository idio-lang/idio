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
 * character.c
 * 
 */

#include "idio.h"

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

size_t idio_character_C_hash (IDIO h, void *s)
{
    size_t hvalue = (uintptr_t) s;

    if (idio_S_nil != s) {
	hvalue = idio_hash_hashval_string_C (strlen ((char *) s), s);
    }
    
    return (hvalue & IDIO_HASH_MASK (h));
}

IDIO idio_characters_C_intern (char *s, IDIO v)
{
    IDIO_C_ASSERT (s);

    IDIO c = idio_hash_get (idio_characters_hash, s);

    if (idio_S_unspec == c) {
	idio_hash_put (idio_characters_hash, s, v);
    }

    return v;
}

IDIO idio_character_lookup (char *s)
{
    IDIO_C_ASSERT (s);

    return idio_hash_get (idio_characters_hash, s);
}

int idio_isa_character (IDIO o)
{
    IDIO_ASSERT (o);

    if (((intptr_t) o & 3) == IDIO_TYPE_CHARACTER_MARK) {
	uintptr_t cv = IDIO_CHARACTER_VAL (o);

	/*
	 * https://www.gnu.org/software/guile/manual/html_node/Characters.html
	 *
	 * Scheme says a character is a valid Unicode code point.
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

IDIO_DEFINE_PRIMITIVE1 ("char?",  char_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO_VERIFY_PARAM_TYPE (character, o);

    IDIO r = idio_S_false;

    if (idio_isa_character (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("char->integer",  char2integer, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    return IDIO_FIXNUM (IDIO_CHARACTER_VAL (c));
}

IDIO_DEFINE_PRIMITIVE1 ("char-alphabetic?",  char_alphabetic_p, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    IDIO r = idio_S_false;

    if (isalpha (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("char-numeric?",  char_numeric_p, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    IDIO r = idio_S_false;

    if (isdigit (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("char-whitespace?",  char_whitespace_p, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    IDIO r = idio_S_false;

    if (isblank (IDIO_CHARACTER_VAL (c)) ||
	isspace (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("char-downcase",  char_downcase, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    return IDIO_CHARACTER (IDIO_CHARACTER_IVAL (c));
}

IDIO_DEFINE_PRIMITIVE1 ("char-lower-case?",  char_lower_case_p, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    IDIO r = idio_S_false;

    if (islower (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("char-upcase",  char_upcase, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    return IDIO_CHARACTER (toupper (IDIO_CHARACTER_VAL (c)));
}

IDIO_DEFINE_PRIMITIVE1 ("char-upper-case?",  char_upper_case_p, (IDIO c))
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (character, c);

    IDIO r = idio_S_false;

    if (isupper (IDIO_CHARACTER_VAL (c))) {
	r = idio_S_true;
    }

    return r;
}

/*
 * All the char-*? are essentially identical
 */
#define IDIO_DEFINE_CHARACTER_PRIMITIVE2(name,cname,cmp,accessor)	\
    IDIO_DEFINE_PRIMITIVE2 (name, cname, (IDIO c1, IDIO c2))		\
    {									\
	IDIO_ASSERT (c1);						\
	IDIO_ASSERT (c2);						\
									\
	IDIO_VERIFY_PARAM_TYPE (character, c1);				\
	IDIO_VERIFY_PARAM_TYPE (character, c2);				\
									\
	IDIO r = idio_S_false;						\
									\
	if (accessor (c1) cmp accessor (c2)) {				\
	    r = idio_S_true;						\
	}								\
									\
	return r;							\
    }

#define IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2(name,cname,cmp)		\
    IDIO_DEFINE_CHARACTER_PRIMITIVE2 (name, char_ci_ ## cname ## _p, cmp, IDIO_CHARACTER_IVAL)

#define IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2(name,cname,cmp)		\
    IDIO_DEFINE_CHARACTER_PRIMITIVE2 (name, char_ ## cname ## _p, cmp, IDIO_CHARACTER_VAL)

IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2 ("char-ci<=?", le, <=)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2 ("char-ci<?", lt, <)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2 ("char-ci=?", eq, ==)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2 ("char-ci>=?", ge, >=)
IDIO_DEFINE_CHARACTER_CI_PRIMITIVE2 ("char-ci>?", gt, >)

IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2 ("char<=?", le, <=)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2 ("char<?", lt, <)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2 ("char=?", eq, ==)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2 ("char>=?", ge, >=)
IDIO_DEFINE_CHARACTER_CS_PRIMITIVE2 ("char>?", gt, >)

#define IDIO_CHARACTER_INTERN_C(name,c)	(idio_characters_C_intern (name, IDIO_CHARACTER (c)))

void idio_init_character ()
{
    idio_characters_hash = idio_hash (1<<7, idio_character_C_eqp, idio_character_C_hash);
    idio_gc_protect (idio_characters_hash);
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
    IDIO_CHARACTER_INTERN_C ("vt", 10);
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

    /* C and other common names*/
    /* nul as above */
    IDIO_CHARACTER_INTERN_C ("alarm", '\a');	       /* 0x07 */
    IDIO_CHARACTER_INTERN_C ("backspace", '\b');       /* 0x08 */
    IDIO_CHARACTER_INTERN_C ("tab", '\t');	       /* 0x09 */
    IDIO_CHARACTER_INTERN_C ("linefeed", '\n');	       /* 0x0a */
    IDIO_CHARACTER_INTERN_C ("newline", '\n');	       /* 0x0a */
    IDIO_CHARACTER_INTERN_C ("vtab", '\v');	       /* 0x0b */
    IDIO_CHARACTER_INTERN_C ("page", '\f');	       /* 0x0c */
    IDIO_CHARACTER_INTERN_C ("return", '\r');	       /* 0x0d */
    IDIO_CHARACTER_INTERN_C ("carriage-return", '\r'); /* 0x0d */
    IDIO_CHARACTER_INTERN_C ("esc", 0x1b);	       /* 0x1b */
    IDIO_CHARACTER_INTERN_C ("escape", 0x1b);	       /* 0x1b */
    IDIO_CHARACTER_INTERN_C ("space", ' ');	       /* 0x20 */
    IDIO_CHARACTER_INTERN_C ("del", 0x7f);	       /* 0x7f */
    IDIO_CHARACTER_INTERN_C ("delete", 0x7f);	       /* 0x7f */

    /* Unicode code points... */

    IDIO_ADD_PRIMITIVE (char_p);
    IDIO_ADD_PRIMITIVE (char2integer);
    IDIO_ADD_PRIMITIVE (char_alphabetic_p);
    IDIO_ADD_PRIMITIVE (char_numeric_p);
    IDIO_ADD_PRIMITIVE (char_downcase);
    IDIO_ADD_PRIMITIVE (char_lower_case_p);
    IDIO_ADD_PRIMITIVE (char_upcase);
    IDIO_ADD_PRIMITIVE (char_upper_case_p);
    IDIO_ADD_PRIMITIVE (char_whitespace_p);
    IDIO_ADD_PRIMITIVE (char_le_p);
    IDIO_ADD_PRIMITIVE (char_lt_p);
    IDIO_ADD_PRIMITIVE (char_eq_p);
    IDIO_ADD_PRIMITIVE (char_ge_p);
    IDIO_ADD_PRIMITIVE (char_gt_p);
    IDIO_ADD_PRIMITIVE (char_ci_le_p);
    IDIO_ADD_PRIMITIVE (char_ci_lt_p);
    IDIO_ADD_PRIMITIVE (char_ci_eq_p);
    IDIO_ADD_PRIMITIVE (char_ci_ge_p);
    IDIO_ADD_PRIMITIVE (char_ci_gt_p);
}

void idio_final_character ()
{
    idio_gc_expose (idio_characters_hash);
}

