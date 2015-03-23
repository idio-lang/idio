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
 * idio.h
 * 
 */

#ifndef IDIO_H
#define IDIO_H

#define IDIO_DEBUG	1

#if IDIO_DEBUG
#else
#define NDEBUG
#endif

/*
 * asprintf(3) in stdio.h
 * under CentOS requires _GNU_SOURCE
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>
#include <ffi.h>
#include <ctype.h>

#include <sys/resource.h>

#ifdef assert

#define IDIO_C_ASSERT(x)	(assert (x))

/* IDIO_TYPE_ASSERT assumes a local variable f */
#define IDIO_TYPE_ASSERT(t,x) {						\
	if (! idio_isa_ ## t (x)) {					\
	    char em[BUFSIZ];						\
	    sprintf (em, "%s %s is not a %s", idio_type2string (x), #x, #t); \
	    idio_error_add_C (em);					\
	    fprintf (stderr, "%s\n", em);				\
	    idio_expr_dump (x);						\
	    /*idio_frame_trace (f);*/					\
	    IDIO_C_ASSERT (0);						\
	}								\
    }

/*
 * A valid IDIO object must be:
 *
 * - non-NULL
 * - either have bottom 2 bits set
 * - therefore a pointer and type field < IDIO_TYPE_MAX
 *
 * obviously some random C pointer might pass but that's *always* true
 */
#define IDIO_ASSERT(x)		(assert(x),(((intptr_t) x)&3)?1:(assert((x)->type),assert((x)->type < IDIO_TYPE_MAX)))
#define IDIO_ASSERT_FREE(x)	(assert(((x)->flags & IDIO_FLAG_FREE_MASK) == IDIO_FLAG_FREE))
#define IDIO_ASSERT_NOT_FREED(x) (assert(((x)->flags & IDIO_FLAG_FREE_MASK) != IDIO_FLAG_FREE))
#define IDIO_EXIT(x)		{IDIO_C_ASSERT(0);exit(x);}
#define IDIO_C_EXIT(x)		{IDIO_C_ASSERT(0);exit(x);}

#else

#define IDIO_C_ASSERT(x)	((void) 0)
#define IDIO_TYPE_ASSERT(t,x)	((void) 0)
#define IDIO_ASSERT(x)		((void) 0)
#define IDIO_ASSERT_FREE(x)	((void) 0)
#define IDIO_ASSERT_NOT_FREED(x) ((void) 0)
#define IDIO_EXIT(x)		{exit(x);}
#define IDIO_C_EXIT(x)		{exit(x);}

#endif

#define IDIO_FLAG_FREE_SET(x) ((((x)->flags & IDIO_FLAG_FREE_MASK) == IDIO_FLAG_FREE))
/*
#define IDIO_IS_FREE(x)       (idio_S_nil == (x) || IDIO_FLAG_FREE_SET (x))
*/
#define IDIO_IS_SET(x)        (idio_S_nil != (x) && (IDIO_FLAG_FREE_SET (x) == 0))

#define IDIO_WORD_MAX_LEN	BUFSIZ

#include "gc.h"
#include "array.h"
#include "bignum.h"
#include "c-ffi.h"
#include "c-struct.h"
#include "c-type.h"
#include "closure.h"
#include "handle.h"
#include "error.h"
#include "file-handle.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "module.h"
#include "primitive.h"
#include "pair.h"
#include "path.h"
#include "scm-read.h"
#include "string.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"

/*
 * Some well-known constants.
 *
 * Update util.c:idio_as-string as well!
 */
#define IDIO_CONSTANT_NIL	      0
#define IDIO_CONSTANT_UNDEF           -1
#define IDIO_CONSTANT_UNSPEC          -2
#define IDIO_CONSTANT_EOF             3
#define IDIO_CONSTANT_TRUE            4
#define IDIO_CONSTANT_FALSE           5
#define IDIO_CONSTANT_NAN             -6
#define IDIO_CONSTANT_ELSE            7
#define IDIO_CONSTANT_EQ_GT           8
#define IDIO_CONSTANT_QUOTE           9
#define IDIO_CONSTANT_UNQUOTE         10
#define IDIO_CONSTANT_UNQUOTESPLICING 11
#define IDIO_CONSTANT_QUASIQUOTE      12
#define IDIO_CONSTANT_LAMBDA          13
#define IDIO_CONSTANT_MACRO           14
#define IDIO_CONSTANT_BEGIN           15
#define IDIO_CONSTANT_AND             16
#define IDIO_CONSTANT_OR              17
#define IDIO_CONSTANT_DEFINE          18
#define IDIO_CONSTANT_LETREC          19
#define IDIO_CONSTANT_BLOCK           20
#define IDIO_CONSTANT_TEMPLATE        21
#define IDIO_CONSTANT_FIXED_TEMPLATE  22
#define IDIO_CONSTANT_CLASS           23
#define IDIO_CONSTANT_SUPER           24
#define IDIO_CONSTANT_C_STRUCT        25
#define IDIO_CONSTANT_ROOT            26
#define IDIO_CONSTANT_INIT            27
#define IDIO_CONSTANT_THIS            28
#define IDIO_CONSTANT_ERROR           29
#define IDIO_CONSTANT_PROFILE         30
#define IDIO_CONSTANT_DLOADS          31
#define IDIO_CONSTANT_AMPERSAND       32
#define IDIO_CONSTANT_ASTERISK        33
#define IDIO_CONSTANT_NAMESPACE       34

#define idio_S_nil		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_NIL))
#define idio_S_undef		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_UNDEF))
#define idio_S_unspec		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_UNSPEC))
#define idio_S_eof		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_EOF))
#define idio_S_true		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_TRUE))
#define idio_S_false		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_FALSE))
#define idio_S_NaN		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_NAN))
#define idio_S_else		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_ELSE))
#define idio_S_eq_gt		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_EQ_GT))
#define idio_S_quote		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_QUOTE))
#define idio_S_unquote		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_UNQUOTE))
#define idio_S_unquotesplicing	((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_UNQUOTESPLICING))
#define idio_S_quasiquote	((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_QUASIQUOTE))
#define idio_S_lambda		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_LAMBDA))
#define idio_S_macro		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_MACRO))
#define idio_S_begin		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_BEGIN))
#define idio_S_and		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_AND))
#define idio_S_or		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_OR))
#define idio_S_define		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_DEFINE))
#define idio_S_letrec		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_LETREC))
#define idio_S_block		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_BLOCK))
#define idio_S_template		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_TEMPLATE))
#define idio_S_fixed_template	((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_FIXED_TEMPLATE))
#define idio_S_class		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_CLASS))
#define idio_S_super		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_SUPER))
#define idio_S_c_struct		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_C_STRUCT))
#define idio_S_root		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_ROOT))
#define idio_S_init		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_INIT))
#define idio_S_this		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_THIS))
#define idio_S_error		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_ERROR))
#define idio_S_profile		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_PROFILE))
#define idio_S_dloads		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_DLOADS))
#define idio_S_ampersand	((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_AMPERSAND))
#define idio_S_asterisk		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_ASTERISK))
#define idio_S_namespace	((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_NAMESPACE))

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
