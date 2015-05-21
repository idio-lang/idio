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
 * read.h
 * 
 */

#ifndef READ_H
#define READ_H

/*
 * These read specific constants don't have to be different to
 * idio_S_* but there's plenty of room so why not?
 *
 * Of course, they shouldn't leak out of here but idio_as_string
 * prints the unhelpful C=1005 otherwise
 */
#define IDIO_TOKEN_BASE		1000
#define IDIO_TOKEN_DOT		(IDIO_TOKEN_BASE+0)
#define IDIO_TOKEN_LPAREN	(IDIO_TOKEN_BASE+1)
#define IDIO_TOKEN_RPAREN	(IDIO_TOKEN_BASE+2)
#define IDIO_TOKEN_LBRACE	(IDIO_TOKEN_BASE+3)
#define IDIO_TOKEN_RBRACE	(IDIO_TOKEN_BASE+4)
#define IDIO_TOKEN_LBRACKET	(IDIO_TOKEN_BASE+5)
#define IDIO_TOKEN_RBRACKET	(IDIO_TOKEN_BASE+6)
#define IDIO_TOKEN_LANGLE	(IDIO_TOKEN_BASE+7)
#define IDIO_TOKEN_RANGLE	(IDIO_TOKEN_BASE+8)
#define IDIO_TOKEN_EOL		(IDIO_TOKEN_BASE+9)
#define IDIO_TOKEN_AMPERSAND	(IDIO_TOKEN_BASE+10)

#define idio_T_dot		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_DOT))
#define idio_T_lparen		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_LPAREN))
#define idio_T_rparen		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_RPAREN))
#define idio_T_lbrace		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_LBRACE))
#define idio_T_rbrace		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_RBRACE))
#define idio_T_lbracket		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_LBRACKET))
#define idio_T_rbracket		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_RBRACKET))
#define idio_T_langle		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_LANGLE))
#define idio_T_rangle		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_RANGLE))
#define idio_T_eol		((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_EOL))
#define idio_T_ampersand	((const IDIO) IDIO_CONSTANT (IDIO_TOKEN_AMPERSAND))

IDIO idio_read (IDIO handle);
IDIO idio_read_expr (IDIO handle);
IDIO idio_read_char (IDIO handle);

void idio_init_read ();
void idio_read_add_primitives ();
void idio_final_read ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
