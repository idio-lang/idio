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
 * usi-wrap.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "idio-config.h"

#include "gc.h"

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
    static char const *idio_usi_case_names[] = {
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
	    idio_display_C (idio_usi_case_names[i], oh);
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
    IDIO_DEFINE_PRIMITIVE1_DS (#name "?",  usi_ ## name ## _p, (IDIO o), "o", "") \
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

int idio_usi_codepoint_is_category (idio_unicode_t cp, int cat)
{
    int r = 0;

    const idio_USI_t *var = idio_USI_codepoint (cp);
    if (cat == (int) var->category) {
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
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
