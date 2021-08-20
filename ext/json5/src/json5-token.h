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
 * json5-token.h
 *
 * The result of wandering through https://spec.json5.org
 *
 * From a portability perspective, this code is limited to C's int64_t
 * integers and double floating point numbers.
 */

#ifndef JSON5_TOKEN_H
#define JSON5_TOKEN_H

typedef enum {
    JSON5_MEMBER_STRING,
    JSON5_MEMBER_IDENTIFIER
} json5_member_type_t;

typedef struct json5_member_s {
    json5_member_type_t type;
    json5_unicode_string_t *name;
    struct json5_value_s *value;
} json5_member_t;

typedef struct json5_object_s {
    struct json5_object_s *next;
    json5_member_t *member;
} json5_object_t;

typedef struct json5_array_s {
    struct json5_array_s *next;
    struct json5_value_s *element;
} json5_array_t;

typedef enum {
    JSON5_NUMBER_INFINITY,
    JSON5_NUMBER_NEG_INFINITY,
    JSON5_NUMBER_NAN,
    JSON5_NUMBER_NEG_NAN,
    ECMA_NUMBER_INTEGER,
    ECMA_NUMBER_FLOAT
} json5_number_type_t;

typedef struct json5_number_s {
    json5_number_type_t type;
    union {
	int64_t i;
	double f;
    } u;
} json5_number_t;

typedef enum {
    JSON5_LITERAL_NULL,
    JSON5_LITERAL_TRUE,
    JSON5_LITERAL_FALSE
} json5_literal_t;

typedef enum {
    JSON5_PUNCTUATOR_LBRACE,
    JSON5_PUNCTUATOR_RBRACE,
    JSON5_PUNCTUATOR_LBRACKET,
    JSON5_PUNCTUATOR_RBRACKET,
    JSON5_PUNCTUATOR_COLON,
    JSON5_PUNCTUATOR_COMMA,
} json5_punctuator_type_t;

typedef enum {
    JSON5_VALUE_NULL,
    JSON5_VALUE_BOOLEAN,
    JSON5_VALUE_STRING,
    JSON5_VALUE_NUMBER,
    JSON5_VALUE_OBJECT,
    JSON5_VALUE_ARRAY,

    /*
     * Each JSON5 token is associated with a JSON5 value which means
     * we need to extend the nominal set of value types
     */
    JSON5_VALUE_PUNCTUATOR,
    JSON5_VALUE_IDENTIFIER,	/* implemented as JSON5_VALUE_STRING */
    
} json5_value_type_t;

typedef struct json5_value_s {
    json5_value_type_t type;
    union {
	json5_literal_t l;
	json5_unicode_string_t *s;
	json5_number_t *n;
	json5_object_t *o;
	json5_array_t *a;

	json5_punctuator_type_t p;
    } u;
} json5_value_t;

typedef enum {
    JSON5_TOKEN_ROOT,
    JSON5_TOKEN_IDENTIFIER,
    JSON5_TOKEN_PUNCTUATOR,
    JSON5_TOKEN_STRING,
    JSON5_TOKEN_NUMBER,    
} json5_token_type_t;

typedef struct json5_token_s {
    struct json5_token_s *next;
    json5_token_type_t type;
    size_t start;
    size_t end;
    json5_value_t *value;
} json5_token_t;

json5_token_t *json5_tokenize_string_C (char *s_C, size_t slen);

#endif
