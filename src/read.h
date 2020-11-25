/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * read.h
 *
 */

#ifndef READ_H
#define READ_H

extern IDIO idio_lexobj_type;
extern IDIO idio_src_properties;

/*
 * Indexes into structures for direct references
 *
 * Lexical Objects that contain information about the source
 */
#define IDIO_LEXOBJ_NAME		0
#define IDIO_LEXOBJ_LINE		1
#define IDIO_LEXOBJ_POS			2
#define IDIO_LEXOBJ_EXPR		3


/*
 * These read specific constants don't have to be different to
 * idio_S_* but there's plenty of room so why not?
 *
 * Of course, they shouldn't leak out of here but idio_as_string
 * prints the unhelpful C=1005 otherwise
 */
#define IDIO_TOKEN_DOT            0
#define IDIO_TOKEN_LPAREN         1
#define IDIO_TOKEN_RPAREN         2
#define IDIO_TOKEN_LBRACE         3
#define IDIO_TOKEN_RBRACE         4
#define IDIO_TOKEN_LBRACKET       5
#define IDIO_TOKEN_RBRACKET       6
#define IDIO_TOKEN_LANGLE         7
#define IDIO_TOKEN_RANGLE         8
#define IDIO_TOKEN_EOL            9
#define IDIO_TOKEN_PAIR_SEPARATOR 10
#define IDIO_TOKEN_SEMICOLON      11

#define idio_T_dot		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_DOT))
#define idio_T_lparen		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_LPAREN))
#define idio_T_rparen		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_RPAREN))
#define idio_T_lbrace		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_LBRACE))
#define idio_T_rbrace		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_RBRACE))
#define idio_T_lbracket		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_LBRACKET))
#define idio_T_rbracket		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_RBRACKET))
#define idio_T_langle		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_LANGLE))
#define idio_T_rangle		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_RANGLE))
#define idio_T_eol		((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_EOL))
#define idio_T_pair_separator	((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_PAIR_SEPARATOR))
#define idio_T_semicolon	((const IDIO) IDIO_CONSTANT_TOKEN (IDIO_TOKEN_SEMICOLON))

#define IDIO_READ_CHARACTER_SIMPLE	0
#define IDIO_READ_CHARACTER_EXTENDED	1

IDIO idio_read (IDIO handle);
IDIO idio_read_expr (IDIO handle);
idio_unicode_t idio_read_character_int (IDIO handle, IDIO lo, int kind);
IDIO idio_read_character (IDIO handle, IDIO lo, int kind);

void idio_init_read ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
