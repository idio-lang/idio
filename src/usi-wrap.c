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
 * usi-wrap.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ffi.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bitset.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "usi.h"
#include "util.h"
#include "vm.h"

#include "usi-wrap.h"

int idio_usi_isa (IDIO o, int flag)
{
    IDIO_ASSERT (o);

    int r = 0;

    if (idio_isa_unicode (o)) {
	idio_unicode_t cp = IDIO_UNICODE_VAL (o);
	const idio_USI_t *var = idio_USI_codepoint (cp);
	if (var->flags & flag) {
	    r = 1;
	}
    } else if (idio_isa_string (o)) {
	size_t slen = idio_string_len (o);
	if (slen > 0) {
	    uint8_t *s8 = NULL;
	    uint16_t *s16 = NULL;
	    uint32_t *s32 = NULL;
	    size_t width = idio_string_storage_size (o);

	    if (idio_isa_substring (o)) {
		slen = IDIO_SUBSTRING_LEN (o);
		switch (width) {
		case 1:
		    s8 = (uint8_t *) IDIO_SUBSTRING_S (o);
		    break;
		case 2:
		    s16 = (uint16_t *) IDIO_SUBSTRING_S (o);
		    break;
		case 4:
		    s32 = (uint32_t *) IDIO_SUBSTRING_S (o);
		    break;
		}
	    } else {
		slen = IDIO_STRING_LEN (o);
		switch (width) {
		case 1:
		    s8 = (uint8_t *) IDIO_STRING_S (o);
		    break;
		case 2:
		    s16 = (uint16_t *) IDIO_STRING_S (o);
		    break;
		case 4:
		    s32 = (uint32_t *) IDIO_STRING_S (o);
		    break;
		}
	    }


	    for (size_t i = 0; i < slen; i++) {
		idio_unicode_t cp = 0;
		switch (width) {
		case 1:
		    cp = s8[i];
		    break;
		case 2:
		    cp = s16[i];
		    break;
		case 4:
		    cp = s32[i];
		    break;
		}

		const idio_USI_t *var = idio_USI_codepoint (cp);
		if (0 == (var->flags & flag)) {
		    return 0;
		}
	    }

	    r = 1;
	}
    } else {
	/*
	 * Test Case: usi-wrap-errors/<predicate>-bad-type.idio
	 *
	 * <predicate> #t
	 */
	idio_error_param_type ("unicode|string", o, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    return r;
}

void idio_usi_describe_code_point (idio_unicode_t cp)
{
    static char const *case_names[] = {
	"Uppercase",
	"Lowercase",
	"Titlecase",
    };

    IDIO oh = idio_thread_current_output_handle ();

    const idio_USI_t *var = idio_USI_codepoint (cp);

    /*
     * We'll reuse buf[] over and above the code point.
     *
     * A fraction can be an arbitrary length albeit the ones in 13.0.0
     * are, at most, five characters: 1/320, say.
     *
     * A 64-bit decimal number can be ~19 digits.
     */
    char buf[30];
    size_t blen = idio_snprintf (buf, 30, "%04X;;", cp);
    idio_display_C_len (buf, blen, oh);

    idio_display_C (idio_USI_Category_names[var->category], oh);
    idio_display_C (";;;;", oh);

    if (var->flags & IDIO_USI_FLAG_Fractional_Number) {
	blen = idio_snprintf (buf, 30, ";;%s;", var->u.frac);
	idio_display_C_len (buf, blen, oh);
    } else if (var->flags & IDIO_USI_FLAG_Number) {
	if (IDIO_USI_CATEGORY_Nl == var->category) {
	    blen = idio_snprintf (buf, 30, ";;%" PRId64 ";", var->u.dec);
	} else {
	    /*
	     * Only a single digit??
	     */
	    blen = idio_snprintf (buf, 30, "%" PRId64 ";%" PRId64 ";%" PRId64 ";", var->u.dec, var->u.dec, var->u.dec);
	}
	idio_display_C_len (buf, blen, oh);
    } else {
	idio_display_C (";;;", oh);
    }

    idio_display_C (";;;", oh);

    for (int i = 0; i < 3; i++) {
	if (var->cases[i]) {
	    blen = idio_snprintf (buf, 30, "%04X", cp + var->cases[i]);
	    idio_display_C_len (buf, blen, oh);
	}

	if (i < 2) {
	    idio_display_C (";", oh);
	}
    }

    idio_display_C (" # ", oh);
    for (int i = 0 ; i < IDIO_USI_FLAG_COUNT; i++) {
	int printed = 0;

	if (var->flags & (1 << i)) {
	    idio_display_C (idio_USI_flag_names[i], oh);

	    /*
	     * Hmm, a 64-bit decimal number can be ~19 digits but
	     * a fraction can be an arbitrary length albeit the
	     * ones in 13.0.0 are, at most, five characters:
	     * 1/320, say.
	     */
	    char buf[30];
	    switch (1 << i) {
	    case IDIO_USI_FLAG_Fractional_Number:
		blen = idio_snprintf (buf, 30, "=%s", var->u.frac);
		idio_display_C_len (buf, blen, oh);
		break;
	    case IDIO_USI_FLAG_Number:
		if (0 == (var->flags & IDIO_USI_FLAG_Fractional_Number)) {
		    blen = idio_snprintf (buf, 30, "=%" PRId64, var->u.dec);
		    idio_display_C_len (buf, blen, oh);
		}
		break;
	    }

	    printed = 1;
	}

	if (printed) {
	    idio_display_C (" ", oh);
	}
    }
    for (int i = 0; i < 3; i++) {
	if (var->cases[i]) {
	    idio_display_C (case_names[i], oh);
	    blen = idio_snprintf (buf, 30, "=%04X ", cp + var->cases[i]);
	    idio_display_C_len (buf, blen, oh);
	}
    }
    idio_display_C ("\n", oh);
}

IDIO_DEFINE_PRIMITIVE1_DS ("describe", usi_describe, (IDIO o), "o", "\
print the Unicode attributes of `o`		\n\
						\n\
:param o: value to describe			\n\
:type o: unicode or string			\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (o);

    /*
     * Test Case: usi-wrap-errors/describe-bad-type.idio
     *
     * describe #t
     */
    if (idio_isa_unicode (o)) {
	idio_usi_describe_code_point (IDIO_UNICODE_VAL (o));
    } else if (idio_isa_string (o)) {
	size_t slen = idio_string_len (o);
	if (slen > 0) {
	    uint8_t *s8 = NULL;
	    uint16_t *s16 = NULL;
	    uint32_t *s32 = NULL;
	    size_t width = idio_string_storage_size (o);

	    if (idio_isa_substring (o)) {
		slen = IDIO_SUBSTRING_LEN (o);
		switch (width) {
		case 1:
		    s8 = (uint8_t *) IDIO_SUBSTRING_S (o);
		    break;
		case 2:
		    s16 = (uint16_t *) IDIO_SUBSTRING_S (o);
		    break;
		case 4:
		    s32 = (uint32_t *) IDIO_SUBSTRING_S (o);
		    break;
		}
	    } else {
		slen = IDIO_STRING_LEN (o);
		switch (width) {
		case 1:
		    s8 = (uint8_t *) IDIO_STRING_S (o);
		    break;
		case 2:
		    s16 = (uint16_t *) IDIO_STRING_S (o);
		    break;
		case 4:
		    s32 = (uint32_t *) IDIO_STRING_S (o);
		    break;
		}
	    }


	    for (size_t i = 0; i < slen; i++) {
		idio_unicode_t cp = 0;
		switch (width) {
		case 1:
		    cp = s8[i];
		    break;
		case 2:
		    cp = s16[i];
		    break;
		case 4:
		    cp = s32[i];
		    break;
		}

		idio_usi_describe_code_point (cp);
	    }
	}
    } else {
	/*
	 * Test Case: usi-wrap-errors/describe-bad-type.idio
	 *
	 * describe #t
	 */
	idio_error_param_type ("unicode|string", o, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    return idio_S_unspec;
}

#define IDIO_USI_PREDICATE(name)					\
    IDIO_DEFINE_PRIMITIVE1 (#name "?",  usi_ ## name ## _p, (IDIO o))	\
    {									\
    IDIO_ASSERT (o);							\
    IDIO r = idio_S_false;						\
    if (idio_usi_isa (o, IDIO_USI_FLAG_ ## name)) {			\
	r = idio_S_true;						\
    }									\
    return r;								\
}

IDIO_USI_PREDICATE (Titlecase_Letter)
IDIO_USI_PREDICATE (Letter)
IDIO_USI_PREDICATE (Mark)
IDIO_USI_PREDICATE (Decimal_Number)
IDIO_USI_PREDICATE (Number)
IDIO_USI_PREDICATE (Punctuation)
IDIO_USI_PREDICATE (Symbol)
IDIO_USI_PREDICATE (Separator)
IDIO_USI_PREDICATE (Lowercase)
IDIO_USI_PREDICATE (Uppercase)
IDIO_USI_PREDICATE (Alphabetic)
IDIO_USI_PREDICATE (White_Space)
IDIO_USI_PREDICATE (ASCII_Hex_Digit)
IDIO_USI_PREDICATE (Control)
IDIO_USI_PREDICATE (Regional_Indicator)
IDIO_USI_PREDICATE (Extend)
IDIO_USI_PREDICATE (SpacingMark)
IDIO_USI_PREDICATE (L)
IDIO_USI_PREDICATE (V)
IDIO_USI_PREDICATE (T)
IDIO_USI_PREDICATE (LV)
IDIO_USI_PREDICATE (LVT)
IDIO_USI_PREDICATE (ZWJ)
IDIO_USI_PREDICATE (Fractional_Number)

IDIO_DEFINE_PRIMITIVE1_DS ("->Lowercase", usi_2Lowercase, (IDIO cp), "cp", "\
Return the Unicode ``Simple_Lowercase_Mapping`` of `cp`	\n\
						\n\
:param cp: value to convert			\n\
:type cp: unicode				\n\
:return: unicode				\n\
						\n\
Note that the default lower-case mapping is to	\n\
`cp`.						\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: usi-wrap-errors/2Lowercase-bad-type.idio
     *
     * ->Lowercase #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    idio_unicode_t cp_C = IDIO_UNICODE_VAL (cp);
    const idio_USI_t *var = idio_USI_codepoint (cp_C);

    return IDIO_UNICODE (cp_C + var->cases[IDIO_USI_LOWERCASE_OFFSET]);
}

IDIO_DEFINE_PRIMITIVE1_DS ("->Uppercase", usi_2Uppercase, (IDIO cp), "cp", "\
Return the Unicode ``Simple_Uppercase_Mapping`` of `cp`	\n\
						\n\
:param cp: value to convert			\n\
:type cp: unicode				\n\
:return: unicode				\n\
						\n\
Note that the default upper-case mapping is to	\n\
`cp`.						\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: usi-wrap-errors/2Uppercase-bad-type.idio
     *
     * ->Uppercase #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    idio_unicode_t cp_C = IDIO_UNICODE_VAL (cp);
    const idio_USI_t *var = idio_USI_codepoint (cp_C);

    return IDIO_UNICODE (cp_C + var->cases[IDIO_USI_UPPERCASE_OFFSET]);
}

IDIO_DEFINE_PRIMITIVE1_DS ("->Titlecase", usi_2Titlecase, (IDIO cp), "cp", "\
Return the Unicode ``Simple_Titlecase_Mapping`` of `cp`	\n\
						\n\
:param cp: value to convert			\n\
:type cp: unicode				\n\
:return: unicode				\n\
						\n\
Note that the default Title-case mapping is to	\n\
`cp`.						\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: usi-wrap-errors/2Titlecase-bad-type.idio
     *
     * ->Titlecase #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    idio_unicode_t cp_C = IDIO_UNICODE_VAL (cp);
    const idio_USI_t *var = idio_USI_codepoint (cp_C);

    return IDIO_UNICODE (cp_C + var->cases[IDIO_USI_TITLECASE_OFFSET]);
}

/*
 * This is used in %format in common.idio
 */
IDIO_DEFINE_PRIMITIVE1_DS ("ASCII-Decimal_Number?", usi_ASCII_Decimal_Number_p, (IDIO cp), "cp", "\
Is `cp` in the Unicode Category ``Nd`` and less than 0x80?	\n\
						\n\
:param cp: code point to test			\n\
:type cp: unicode				\n\
:return: boolean				\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: usi-wrap-errors/ASCII-numeric-bad-type.idio
     *
     * ASCII-Decimal_Number? #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    idio_unicode_t cp_C = IDIO_UNICODE_VAL (cp);

    IDIO r = idio_S_false;

    if (cp_C < 0x80) {
	const idio_USI_t *var = idio_USI_codepoint (cp_C);

	if (var->flags & IDIO_USI_FLAG_Decimal_Number) {
	    r = idio_S_true;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("numeric-value", usi_numeric_value, (IDIO cp), "cp", "\
return the Unicode ``Numeric_Value`` of `cp`	\n\
						\n\
:param cp: code point				\n\
:type cp: unicode				\n\
:return: integer or string			\n\
:raises ^rt-param-value-error: if `cp` is not ``Numeric?``	\n\
						\n\
The Unicode ``Numeric_Value`` can be a decimal integer	\n\
or a rational which is returned as a string	\n\
						\n\
.. seealso:: :ref:`Fractional_Number? <unicode/Fractional_Number?>`	\n\
")
{
    IDIO_ASSERT (cp);

    /*
     * Test Case: usi-wrap-errors/numeric-value-bad-type.idio
     *
     * numeric-value #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, cp);

    const idio_USI_t *var = idio_USI_codepoint (IDIO_UNICODE_VAL (cp));

    if (var->flags & IDIO_USI_FLAG_Fractional_Number) {
	return idio_string_C (var->u.frac);
    } else if (var->flags & IDIO_USI_FLAG_Number) {
	return idio_integer (var->u.dec);
    } else {
	/*
	 * Test Case: usi-wrap-errors/numeric-value-not-a-number.idio
	 *
	 * numeric-value #\a
	 */
	idio_error_param_value_msg ("numeric-value", "code point", cp, "is not a number", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_S_unspec;
}

void idio_usi_wrap_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_describe);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Titlecase_Letter_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Letter_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Mark_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Decimal_Number_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Number_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Punctuation_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Symbol_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Separator_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Lowercase_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Uppercase_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Alphabetic_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_White_Space_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_ASCII_Hex_Digit_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Control_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Regional_Indicator_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Extend_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_SpacingMark_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_L_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_V_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_T_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_LV_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_LVT_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_ZWJ_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_Fractional_Number_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_2Lowercase);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_2Uppercase);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_2Titlecase);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_ASCII_Decimal_Number_p);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_unicode_module, usi_numeric_value);
}

void idio_final_usi_wrap ()
{
}

enum SRFI_14_char_set_IDs {
    lower_case,
    upper_case,
    title_case,
    letter,
    digit,
    letter_digit,
    graphic,
    printing,
    whitespace,
    iso_control,
    punctuation,
    symbol,
    hex_digit,
    blank,
    ascii,
    empty,
    full,
    nonl,
    control,
    word_constituent,
    cased,
    ASCII_letter,
    ASCII_lower_case,
    ASCII_upper_case,
    ASCII_digit,
    ASCII_letter_digit,
    ASCII_punctuation,
    ASCII_symbol,
    ASCII_graphic,
    ASCII_whitespace,
    ASCII_printing,
    ASCII_iso_control,
    ASCII_word_constituent,
    regional_indicator,
    extend_or_spacing_mark,
    hangul_l,
    hangul_v,
    hangul_t,
    hangul_lv,
    hangul_lvt,
};

typedef struct SRFI_14_char_set_s {
    enum SRFI_14_char_set_IDs index;
    char const *name;
} SRFI_14_char_set_t;

const SRFI_14_char_set_t SRFI_14_char_sets[] = {
    { lower_case,             "char-set:lower-case" },
    { upper_case,             "char-set:upper-case" },
    { title_case,             "char-set:title-case" },
    { letter,                 "char-set:letter" },
    { digit,                  "char-set:digit" },
    { letter_digit,           "char-set:letter+digit" },
    { graphic,                "char-set:graphic" },
    { printing,               "char-set:printing" },
    { whitespace,             "char-set:whitespace" },
    { iso_control,            "char-set:iso-control" },
    { punctuation,            "char-set:punctuation" },
    { symbol,                 "char-set:symbol" },
    { hex_digit,              "char-set:hex-digit" },
    { blank,                  "char-set:blank" },
    { ascii,                  "char-set:ascii" },
    { empty,                  "char-set:empty" },
    { full,                   "char-set:full" },
    { nonl,                   "char-set:nonl" },
    { control,                "char-set:control" },
    { word_constituent,       "char-set:word-constituent" },
    { cased,                  "char-set:cased" },
    { ASCII_letter,           "%char-set:letter" },
    { ASCII_lower_case,       "%char-set:lower-case" },
    { ASCII_upper_case,       "%char-set:upper-case" },
    { ASCII_digit,            "%char-set:digit" },
    { ASCII_letter_digit,     "%char-set:letter+digit" },
    { ASCII_punctuation,      "%char-set:punctuation" },
    { ASCII_symbol,           "%char-set:symbol" },
    { ASCII_graphic,          "%char-set:graphic" },
    { ASCII_whitespace,       "%char-set:whitespace" },
    { ASCII_printing,         "%char-set:printing" },
    { ASCII_iso_control,      "%char-set:iso-control" },
    { ASCII_word_constituent, "%char-set:word-constituent" },
    { regional_indicator,     "char-set:regional-indicator" },
    { extend_or_spacing_mark, "char-set:extend-or-spacing-mark" },
    { hangul_l,               "char-set:hangul-l" },
    { hangul_v,               "char-set:hangul-v" },
    { hangul_t,               "char-set:hangul-t" },
    { hangul_lv,              "char-set:hangul-lv" },
    { hangul_lvt,             "char-set:hangul-lvt" },
};

int idio_usi_codepoint_is_category (idio_unicode_t cp, int cat)
{
    int r = 0;

    const idio_USI_t *var = idio_USI_codepoint (cp);
    if (cat == var->category) {
	r = 1;
    }

    return r;
}

int idio_usi_codepoint_has_attribute (idio_unicode_t cp, int flag)
{
    int r = 0;

    const idio_USI_t *var = idio_USI_codepoint (cp);
    if (var->flags & flag) {
	r = 1;
    }

    return r;
}

void idio_init_usi_wrap ()
{
    idio_module_table_register (idio_usi_wrap_add_primitives, idio_final_usi_wrap, NULL);

    /*
     * Create the SRFI-14 char-sets
     */

    IDIO idio_SRFI_14_module = idio_module (IDIO_SYMBOLS_C_INTERN ("SRFI-14"));

    IDIO sym = IDIO_SYMBOLS_C_INTERN ("sparse-char-set");
    IDIO sparse_char_set_type = idio_struct_type (sym,
						  idio_S_nil,
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("size"),
						  idio_pair (IDIO_SYMBOLS_C_INTERN ("planes"),
						  idio_S_nil)));

    idio_module_set_symbol_value (sym, sparse_char_set_type, idio_SRFI_14_module);

    /*
     * We're going to run through those char-sets where, once we get
     * going, we're normally dealing with the arrays of bitsets (per
     * char-set).
     *
     * {ncss} is the number of char-sets we're going to create
     *
     * {css} are the actual char-sets for which we only need to keep
     * this array hanging around until we've exported their names in
     * SRFI-14.  That's the first loop.
     *
     * Those {css} require the creation of {as} of which the elements
     * are #f until we decide to create a bitset.
     *
     * As we walk over each plane, {bss} is the current bitset for
     * each {as} for that plane.
     */
    int ncss = hangul_lvt + 1;
    IDIO *css = idio_alloc (ncss * sizeof (IDIO));
    IDIO *as = idio_alloc (ncss * sizeof (IDIO));
    for (int csi = 0; csi < ncss; csi++) {
	as[csi] = idio_array (IDIO_UNICODE_PLANE_COUNT);
	/*
	 * Dirty hack.  Pre-force the use of all elements of the
	 * array.  Otherwise there's a whinge that we tried to insert
	 * at index 0 when we haven't used index 0.
	 *
	 * I mean, come on, we know what we're doing!
	 */
	IDIO_ARRAY_USIZE (as[csi]) = IDIO_ARRAY_ASIZE (as[csi]);

	css[csi] = idio_struct_instance (sparse_char_set_type, IDIO_LIST2 (idio_integer (IDIO_UNICODE_SIZE), as[csi]));

	/*
	 * The longest SRFI_14_char_sets[csi].name is 31 chars hence
	 * the strnlen (..., 40) magic number to allow some future
	 * leeway.
	 *
	 * strnlen rather that idio_strnlen during bootstrap
	 */
	idio_module_export_symbol_value (idio_symbols_C_intern (SRFI_14_char_sets[csi].name, strnlen (SRFI_14_char_sets[csi].name, 40)), css[csi], idio_SRFI_14_module);
    }
    IDIO_GC_FREE (css);

    IDIO *bss = idio_alloc (ncss * sizeof (IDIO));
    idio_unicode_t cp = 0;
    uint8_t prev_plane = -1;
    for (; cp < IDIO_UNICODE_SIZE; cp++) {
	uint8_t plane = cp >> 16;
	uint16_t plane_cp = cp & IDIO_UNICODE_PLANE_MASK;

	if (plane != prev_plane) {
	    /*
	     * Reset {bss} for this plane.
	     */
	    for (int csi = 0; csi < ncss; csi++) {
		bss[csi] = idio_array_ref_index (as[csi], plane);
	    }
	}

	const idio_USI_t *var = idio_USI_codepoint (cp);

	/*
	 * Any given USI flag can affect multiple char-sets which
	 * means we might have to create the corresponding plane
	 * bitset
	 */

	/*
	 * char-set:lower-case is Property Lowercase
	 *
	 * char-set:cased is char-set:upper-case char-set:lower-case char-set:title-case
	 *
	 * %char-set:lower-case is char-set:lower-case restricted to ASCII range
	 */
	if (var->flags & IDIO_USI_FLAG_Lowercase) {
	    if (idio_S_false == bss[lower_case]) {
		bss[lower_case] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[lower_case], bss[lower_case], plane);
	    }

	    idio_bitset_set (bss[lower_case], plane_cp);

	    if (idio_S_false == bss[cased]) {
		bss[cased] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[cased], bss[cased], plane);
	    }

	    idio_bitset_set (bss[cased], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_lower_case]) {
		    bss[ASCII_lower_case] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_lower_case], bss[ASCII_lower_case], plane);
		}

		idio_bitset_set (bss[ASCII_lower_case], plane_cp);
	    }
	}

	/*
	 * char-set:upper-case is Property Uppercase
	 *
	 * char-set:cased is char-set:upper-case char-set:lower-case char-set:title-case
	 *
	 * %char-set:upper-case is char-set:upper-case restricted to ASCII range
	 */
	if (var->flags & IDIO_USI_FLAG_Uppercase) {
	    if (idio_S_false == bss[upper_case]) {
		bss[upper_case] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[upper_case], bss[upper_case], plane);
	    }

	    idio_bitset_set (bss[upper_case], plane_cp);

	    if (idio_S_false == bss[cased]) {
		bss[cased] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[cased], bss[cased], plane);
	    }

	    idio_bitset_set (bss[cased], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_upper_case]) {
		    bss[ASCII_upper_case] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_upper_case], bss[ASCII_upper_case], plane);
		}

		idio_bitset_set (bss[ASCII_upper_case], plane_cp);
	    }
	}

	/*
	 * char-set:title-case is Category Titlecase
	 *
	 * char-set:cased is char-set:upper-case char-set:lower-case char-set:title-case
	 */
	if (var->flags & IDIO_USI_FLAG_Titlecase_Letter) {
	    if (idio_S_false == bss[title_case]) {
		bss[title_case] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[title_case], bss[title_case], plane);
	    }

	    idio_bitset_set (bss[title_case], plane_cp);

	    if (idio_S_false == bss[cased]) {
		bss[cased] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[cased], bss[cased], plane);
	    }

	    idio_bitset_set (bss[cased], plane_cp);
	}

	/*
	 * char-set:letter is Property Alphabetic
	 *
	 * char-set:letter+digit is Property Alphabetic + Category Nd
	 *
	 * char-set:word-constituent is char-set:letter+digit + _
	 *
	 * %char-set:letter is char-set:letter restricted to ASCII range
	 *
	 * %char-set:letter+digit is char-set:letter+digit restricted to ASCII range
	 *
	 * %char-set:word-constituent is char-set:word-constituent
	 */
	if (var->flags & IDIO_USI_FLAG_Alphabetic) {
	    if (idio_S_false == bss[letter]) {
		bss[letter] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[letter], bss[letter], plane);
	    }

	    idio_bitset_set (bss[letter], plane_cp);

	    if (idio_S_false == bss[letter_digit]) {
		bss[letter_digit] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[letter_digit], bss[letter_digit], plane);
	    }

	    idio_bitset_set (bss[letter_digit], plane_cp);

	    if (idio_S_false == bss[word_constituent]) {
		bss[word_constituent] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[word_constituent], bss[word_constituent], plane);
	    }

	    idio_bitset_set (bss[word_constituent], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_letter]) {
		    bss[ASCII_letter] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_letter], bss[ASCII_letter], plane);
		}

		idio_bitset_set (bss[ASCII_letter], plane_cp);

		if (idio_S_false == bss[ASCII_letter_digit]) {
		    bss[ASCII_letter_digit] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_letter_digit], bss[ASCII_letter_digit], plane);
		}

		idio_bitset_set (bss[ASCII_letter_digit], plane_cp);

		if (idio_S_false == bss[ASCII_word_constituent]) {
		    bss[ASCII_word_constituent] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_word_constituent], bss[ASCII_word_constituent], plane);
		}

		idio_bitset_set (bss[ASCII_word_constituent], plane_cp);
	    }
	}

	/*
	 * char-set:digit is Category Nd
	 *
	 * char-set:letter+digit is Property Alphabetic + Category Nd
	 *
	 * %char-set:digit is char-set:digit restricted to ASCII range
	 *
	 * %char-set:letter+digit is char-set:letter+digit restricted to ASCII range
	 */
	if (var->flags & IDIO_USI_FLAG_Decimal_Number) {
	    if (idio_S_false == bss[digit]) {
		bss[digit] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[digit], bss[digit], plane);
	    }

	    idio_bitset_set (bss[digit], plane_cp);

	    if (idio_S_false == bss[letter_digit]) {
		bss[letter_digit] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[letter_digit], bss[letter_digit], plane);
	    }

	    idio_bitset_set (bss[letter_digit], plane_cp);

	    if (idio_S_false == bss[word_constituent]) {
		bss[word_constituent] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[word_constituent], bss[word_constituent], plane);
	    }

	    idio_bitset_set (bss[word_constituent], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_digit]) {
		    bss[ASCII_digit] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_digit], bss[ASCII_digit], plane);
		}

		idio_bitset_set (bss[ASCII_digit], plane_cp);

		if (idio_S_false == bss[ASCII_letter_digit]) {
		    bss[ASCII_letter_digit] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_letter_digit], bss[ASCII_letter_digit], plane);
		}

		idio_bitset_set (bss[ASCII_letter_digit], plane_cp);

		if (idio_S_false == bss[ASCII_word_constituent]) {
		    bss[ASCII_word_constituent] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_word_constituent], bss[ASCII_word_constituent], plane);
		}

		idio_bitset_set (bss[ASCII_word_constituent], plane_cp);
	    }
	}

	/*
	 * char-set:graphic is Category L* + Category N* + Category M* + Category S* + Category P*
	 *
	 * char-set:printing is char-set:graphic + char-set:whitespace
	 *
	 * %char-set:graphic is char-set:graphic restricted to ASCII range
	 *
	 * %char-set:printing is char-set:printing restricted to ASCII range
	 */
	if (var->flags & IDIO_USI_FLAG_Letter ||
	    var->flags & IDIO_USI_FLAG_Number ||
	    var->flags & IDIO_USI_FLAG_Mark ||
	    var->flags & IDIO_USI_FLAG_Symbol ||
	    var->flags & IDIO_USI_FLAG_Punctuation) {
	    if (idio_S_false == bss[graphic]) {
		bss[graphic] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[graphic], bss[graphic], plane);
	    }

	    idio_bitset_set (bss[graphic], plane_cp);

	    if (idio_S_false == bss[printing]) {
		bss[printing] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[printing], bss[printing], plane);
	    }

	    idio_bitset_set (bss[printing], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_graphic]) {
		    bss[ASCII_graphic] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_graphic], bss[ASCII_graphic], plane);
		}

		idio_bitset_set (bss[ASCII_graphic], plane_cp);

		if (idio_S_false == bss[ASCII_printing]) {
		    bss[ASCII_printing] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_printing], bss[ASCII_printing], plane);
		}

		idio_bitset_set (bss[ASCII_printing], plane_cp);
	    }
	}

	/*
	 * char-set:whitespace is Property White_Space
	 *
	 * char-set:printing is char-set:graphic + char-set:whitespace
	 *
	 * %char-set:whitespace is char-set:whitespace restricted to ASCII range
	 *
	 * %char-set:printing is char-set:printing restricted to ASCII range
	 */
	if (var->flags & IDIO_USI_FLAG_White_Space) {
	    if (idio_S_false == bss[whitespace]) {
		bss[whitespace] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[whitespace], bss[whitespace], plane);
	    }

	    idio_bitset_set (bss[whitespace], plane_cp);

	    if (idio_S_false == bss[printing]) {
		bss[printing] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[printing], bss[printing], plane);
	    }

	    idio_bitset_set (bss[printing], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_whitespace]) {
		    bss[ASCII_whitespace] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_whitespace], bss[ASCII_whitespace], plane);
		}

		idio_bitset_set (bss[ASCII_whitespace], plane_cp);

		if (idio_S_false == bss[ASCII_printing]) {
		    bss[ASCII_printing] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_printing], bss[ASCII_printing], plane);
		}

		idio_bitset_set (bss[ASCII_printing], plane_cp);
	    }
	}

	/*
	 * char-set:iso-control is 0000..001F + 007F..009F
	 *
	 * %char-set:iso-control is char-set:iso-control restricted to ASCII range
	 */
	if ((cp >= 0 &&
	     cp <= 0x1F) ||
	    (cp >= 0x7F &&
	     cp <= 0x9F)) {
	    if (idio_S_false == bss[iso_control]) {
		bss[iso_control] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[iso_control], bss[iso_control], plane);
	    }

	    idio_bitset_set (bss[iso_control], plane_cp);

	    /*
	     * Based on the above definition cp always of < 0x80 but
	     * leave this in, just in case
	     */
	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_iso_control]) {
		    bss[ASCII_iso_control] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_iso_control], bss[ASCII_iso_control], plane);
		}

		idio_bitset_set (bss[ASCII_iso_control], plane_cp);
	    }
	}

	/*
	 * char-set:punctuation is Category P*
	 *
	 * %char-set:punctuation is char-set:punctuation restricted to ASCII range
	 */
	if (var->flags & IDIO_USI_FLAG_Punctuation) {
	    if (idio_S_false == bss[punctuation]) {
		bss[punctuation] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[punctuation], bss[punctuation], plane);
	    }

	    idio_bitset_set (bss[punctuation], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_punctuation]) {
		    bss[ASCII_punctuation] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_punctuation], bss[ASCII_punctuation], plane);
		}

		idio_bitset_set (bss[ASCII_punctuation], plane_cp);
	    }
	}

	/*
	 * char-set:symbol is Category S*
	 *
	 * %char-set:symbol is char-set:symbol restricted to ASCII range
	 */
	if (var->flags & IDIO_USI_FLAG_Symbol) {
	    if (idio_S_false == bss[symbol]) {
		bss[symbol] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[symbol], bss[symbol], plane);
	    }

	    idio_bitset_set (bss[symbol], plane_cp);

	    if (cp < 0x80) {
		if (idio_S_false == bss[ASCII_symbol]) {
		    bss[ASCII_symbol] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		    idio_array_insert_index (as[ASCII_symbol], bss[ASCII_symbol], plane);
		}

		idio_bitset_set (bss[ASCII_symbol], plane_cp);
	    }
	}

	/*
	 * char-set:hex-digit is 0030..0039 + 0041..0046 + 0061..0066
	 */
	if (var->flags & IDIO_USI_FLAG_ASCII_Hex_Digit) {
	    if (idio_S_false == bss[hex_digit]) {
		bss[hex_digit] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[hex_digit], bss[hex_digit], plane);
	    }

	    idio_bitset_set (bss[hex_digit], plane_cp);
	}

	/*
	 * char-set:blank is Category Zs + 0009
	 */
	if (IDIO_USI_CATEGORY_Zs == var->category) {
	    if (idio_S_false == bss[blank]) {
		bss[blank] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[blank], bss[blank], plane);
	    }

	    idio_bitset_set (bss[blank], plane_cp);
	}

	/*
	 * char-set:ascii is 0000..007f
	 */
	if (cp >= 0 &&
	    cp < 0x80) {
	    if (idio_S_false == bss[ascii]) {
		bss[ascii] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[ascii], bss[ascii], plane);
	    }

	    idio_bitset_set (bss[ascii], plane_cp);
	}

	/*
	 * char-set:empty is empty
	 */

	/*
	 * char-set:control is Property Control
	 */
	if (var->flags & IDIO_USI_FLAG_Control) {
	    if (idio_S_false == bss[control]) {
		bss[control] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[control], bss[control], plane);
	    }

	    idio_bitset_set (bss[control], plane_cp);
	}

	/*
	 * char-set:regional-indicator is Property Regional_Indicator
	 */
	if (var->flags & IDIO_USI_FLAG_Regional_Indicator) {
	    if (idio_S_false == bss[regional_indicator]) {
		bss[regional_indicator] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[regional_indicator], bss[regional_indicator], plane);
	    }

	    idio_bitset_set (bss[regional_indicator], plane_cp);
	}

	/*
	 * char-set:extend-or-spacing-mark is Property Extend + Property SpacingMark
	 */
	if (var->flags & IDIO_USI_FLAG_Extend ||
	    var->flags & IDIO_USI_FLAG_SpacingMark) {
	    if (idio_S_false == bss[extend_or_spacing_mark]) {
		bss[extend_or_spacing_mark] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[extend_or_spacing_mark], bss[extend_or_spacing_mark], plane);
	    }

	    idio_bitset_set (bss[extend_or_spacing_mark], plane_cp);
	}

	/*
	 * char-set:hangul-l is Property L
	 */
	if (var->flags & IDIO_USI_FLAG_L) {
	    if (idio_S_false == bss[hangul_l]) {
		bss[hangul_l] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[hangul_l], bss[hangul_l], plane);
	    }

	    idio_bitset_set (bss[hangul_l], plane_cp);
	}

	/*
	 * char-set:hangul-v is Property V
	 */
	if (var->flags & IDIO_USI_FLAG_V) {
	    if (idio_S_false == bss[hangul_v]) {
		bss[hangul_v] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[hangul_v], bss[hangul_v], plane);
	    }

	    idio_bitset_set (bss[hangul_v], plane_cp);
	}

	/*
	 * char-set:hangul-t is Property T
	 */
	if (var->flags & IDIO_USI_FLAG_T) {
	    if (idio_S_false == bss[hangul_t]) {
		bss[hangul_t] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[hangul_t], bss[hangul_t], plane);
	    }

	    idio_bitset_set (bss[hangul_t], plane_cp);
	}

	/*
	 * char-set:hangul-lv is Property LV
	 */
	if (var->flags & IDIO_USI_FLAG_LV) {
	    if (idio_S_false == bss[hangul_lv]) {
		bss[hangul_lv] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[hangul_lv], bss[hangul_lv], plane);
	    }

	    idio_bitset_set (bss[hangul_lv], plane_cp);
	}

	/*
	 * char-set:hangul-lvt is Property LVT
	 */
	if (var->flags & IDIO_USI_FLAG_LVT) {
	    if (idio_S_false == bss[hangul_lvt]) {
		bss[hangul_lvt] = idio_bitset (IDIO_UNICODE_PLANE_SIZE);
		idio_array_insert_index (as[hangul_lvt], bss[hangul_lvt], plane);
	    }

	    idio_bitset_set (bss[hangul_lvt], plane_cp);
	}
    }

    /*
     * char-set:blank is Category Zs + 0009
     */
    idio_bitset_set (idio_array_ref_index (as[blank], 0), 0x9);

    /*
     * char-set:full is ~ empty
     *
     * char-set:nonl is char-set:full less #\{newline}
     */
    IDIO set_bs = idio_not_bitset (idio_bitset (IDIO_UNICODE_PLANE_SIZE));
    for (int pi = 0; pi < IDIO_UNICODE_PLANE_COUNT; pi++) {
	idio_array_insert_index (as[full], set_bs, pi);
	idio_array_insert_index (as[nonl], set_bs, pi);
    }
    /*
     * Careful, all the above bitsets are references to the same
     * bitset
     */
    idio_array_insert_index (as[nonl], idio_copy_bitset (set_bs), 0);
    idio_bitset_clear (idio_array_ref_index (as[nonl], 0), '\n');

    /*
     * char-set:word-constituent is char-set:letter+digit + _
     *
     * %char-set:word-constituent is char-set:word-constituent restricted to ASCII range
     */
    idio_bitset_set (idio_array_ref_index (as[word_constituent], 0), '_');
    idio_bitset_set (idio_array_ref_index (as[ASCII_word_constituent], 0), '_');

    IDIO_GC_FREE (bss);
    IDIO_GC_FREE (as);
}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: utf-8-unix */
/* End: */
