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
 * json5_unicode.h
 */

#ifndef JSON5_UNICODE_H
#define JSON5_UNICODE_H

typedef uint32_t json5_unicode_t;
#define JSON5_UNICODE_INVALID	0x110000

typedef enum {
    JSON5_UNICODE_STRING_WIDTH_1BYTE = 1,
    JSON5_UNICODE_STRING_WIDTH_2BYTE = 2,
    JSON5_UNICODE_STRING_WIDTH_4BYTE = 4
} json5_unicode_string_width_t;

typedef struct json5_unicode_string_s {
    json5_unicode_string_width_t width;
    size_t len;
    size_t i;			/* 0 <= index < len */
    char *s;
} json5_unicode_string_t;

unsigned int h2i (char c);
int json5_unicode_valid_code_point (json5_unicode_t cp);
int json5_ECMA_LineTerminator (json5_unicode_string_t *s, json5_unicode_t *cpp);
int json5_ECMA_LineTerminatorSequence (json5_unicode_string_t *s, json5_unicode_t *cpp);
int json5_ECMA_SingleEscapeCharacter (json5_unicode_string_t *s, json5_unicode_t *cpp);
int json5_ECMA_NonEscapeCharacter (json5_unicode_string_t *s, json5_unicode_t *cpp);
int json5_ECMA_CharacterEscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp);
int json5_ECMA_HexEscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp, json5_token_t *ft, json5_unicode_string_t *so);
int json5_ECMA_UnicodeEscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp, json5_token_t *ft, json5_unicode_string_t *so);
int json5_ECMA_EscapeSequence (json5_unicode_string_t *s, json5_unicode_t *cpp, json5_token_t *ft, json5_unicode_string_t *so);
int json5_ECMA_IdentifierStart (json5_unicode_t cp, json5_unicode_string_t *s);
int json5_ECMA_IdentifierPart (json5_unicode_t cp, json5_unicode_string_t *s);
json5_unicode_string_t *json5_unicode_string_C_len (char *s_C, const size_t slen);
void json5_unicode_string_set (json5_unicode_string_t *s, size_t i, json5_unicode_t cp);
int json5_unicode_string_n_equal (json5_unicode_string_t *s, const char *scmp, size_t n);
json5_unicode_t json5_unicode_string_peek (json5_unicode_string_t *s, size_t i);
json5_unicode_t json5_unicode_string_next (json5_unicode_string_t *s);
int json5_unicode_string_available (json5_unicode_string_t *s, size_t const reqd);
void json5_unicode_skip_ws (json5_unicode_string_t *s);
void json5_unicode_skip_slc (json5_unicode_string_t *s);
void json5_unicode_skip_bc (json5_unicode_string_t *s, json5_token_t *ft);

/*
 * Not Art as the definition is outside of either libjson5.so
 * (json5-module.c) or libjson5-bare.so (test.c).  Everything
 * JSON5-ish includes this file, though.
 */
#ifndef json5_error_printf
void json5_error_printf (char *format, ...);
#endif

#endif
