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

#include <ffi.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#include "gc.h"
#include "idio.h"

#include "error.h"
#include "evaluate.h"
#include "idio-string.h"
#include "module.h"
#include "symbol.h"
#include "unicode.h"
#include "usi.h"
#include "vm.h"

#include "usi-wrap.h"

IDIO idio_unicode_module = idio_S_nil;

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
		    break;
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
    static const char *case_names[] = {
	"Uppercase",
	"Lowercase",
	"Titlecase",
    };

    const idio_USI_t *usi = idio_USI_codepoint (cp);
    printf ("%04X %2s ", cp, idio_USI_Categories[usi->category]);
    for (int i = 0; i < 3; i++) {
	if (usi->cases[i]) {
	    printf ("%s=%04X ", case_names[i], cp + usi->cases[i]);
	}
    }
    for (int i = 0 ; i < IDIO_USI_FLAG_COUNT; i++) {
	int printed = 0;

	if (usi->flags & (1 << i)) {
	    printf ("%s", idio_USI_flag_names[i]);

	    switch (1 << i) {
	    case IDIO_USI_FLAG_Fractional_Number:
		printf ("=%s", usi->frac);
		break;
	    case IDIO_USI_FLAG_Decimal_Number:
		printf ("=%" PRId64, usi->dec);
		break;
	    }

	    printed = 1;
	}

	if (printed) {
	    printf (" ");
	}
    }
    printf ("\n");
}

IDIO_DEFINE_PRIMITIVE1_DS ("describe", usi_describe, (IDIO o), "o", "\
print the Unicode attributes of ``o``		\n\
						\n\
:param o: value to describe			\n\
:type o: unicode or string			\n\
:return: #unspec				\n\
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
    IDIO_DEFINE_PRIMITIVE1 (#name "?",  usi_ ## name ## _p, (IDIO o))		\
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
}

void idio_final_usi_wrap ()
{
}

void idio_init_usi_wrap ()
{
    idio_module_table_register (idio_usi_wrap_add_primitives, idio_final_usi_wrap);

    idio_unicode_module = idio_module (idio_symbols_C_intern ("unicode"));

}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: utf-8-unix */
/* End: */
