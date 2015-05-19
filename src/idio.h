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

/* #define IDIO_DEBUG	1 */
/* #undef IDIO_DEBUG   */

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
#include <sys/time.h>

#ifdef assert

#define IDIO_C_ASSERT(x)	(assert (x))

/* IDIO_TYPE_ASSERT assumes a local variable f */
#define IDIO_TYPE_ASSERT(t,x) {						\
	if (! idio_isa_ ## t (x)) {					\
	    char em[BUFSIZ];						\
	    sprintf (em, "%s is a %s not a %s", #x, idio_type2string (x), #t); \
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
#define IDIO_ASSERT_FREE(x)	((((intptr_t) x)&3)?1:(assert(((x)->flags & IDIO_FLAG_FREE_MASK) == IDIO_FLAG_FREE)))
#define IDIO_ASSERT_NOT_FREED(x) ((((intptr_t) x)&3)?1:(assert(((x)->flags & IDIO_FLAG_FREE_MASK) != IDIO_FLAG_FREE)))
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

/*
 * A few helper macros for defining the C function that implements a
 * primitive and then for binding that C function to an Idio language
 * primitive.
 *
 * iname is the C string of how it appears to Idio users, cname should
 * be a (unique) C name.  cname is never used standalone but is
 * embedded within other C names, eg. idio_defprimitive_<cname>.
 *
 * params should be the list of type/name pairs that make up the
 * regular C argument list and will be inserted into C verbatim.
 *
 * The body of the primitive should follow immediately.
 *
 * PRIMITIVEx indicates that x is the arity of the function with
 * PRIMITIVExV meaning it has a variable arity with a minimum arity of
 * x.
 *
 * We are looking for the following for foo_C, ie. Idio's "foo-idio"

   IDIO_DEFINE_PRIMITIVE2 ("foo-idio", foo_C, (T1 a1, T2, a2))
   {
     ...
   }

 * should become
   
   IDIO idio_defprimitive_foo_C (T1 a1, T2 a2);
   static struct idio_primitive_s idio_primitive_data_foo_C = {
      idio_defprimitive_foo_C,
      "foo-idio",
      2,
      0
   };
   IDIO idio_defprimitive_foo_C (T1 a1, T2 a2)
   {
     ...
   }


 * IDIO_ADD_PRIMITIVE (foo_C) can then access the static struct
 * idio_primitive_s called idio_defprimitive_data_foo_C and pass it to
 * the code to actually add a primitive.
 *
 */

#define IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,arity,varargs)	\
    IDIO idio_defprimitive_ ## cname params;				\
    static struct idio_primitive_s idio_primitive_data_ ## cname = { \
	idio_defprimitive_ ## cname,					\
	iname,								\
	arity,								\
	varargs								\
    };									\
    IDIO idio_defprimitive_ ## cname params

#define IDIO_DEFINE_PRIMITIVE0(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,0,0)

#define IDIO_DEFINE_PRIMITIVE0V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,0,1)

#define IDIO_DEFINE_PRIMITIVE1(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,1,0)

#define IDIO_DEFINE_PRIMITIVE1V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,1,1)

#define IDIO_DEFINE_PRIMITIVE2(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,2,0)

#define IDIO_DEFINE_PRIMITIVE2V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,2,1)

#define IDIO_DEFINE_PRIMITIVE3(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,3,0)

#define IDIO_DEFINE_PRIMITIVE3V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,3,1)

#define IDIO_DEFINE_PRIMITIVE4(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,4,0)

#define IDIO_DEFINE_PRIMITIVE5(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,5,0)

#define IDIO_ADD_PRIMITIVE(cname)	  idio_add_primitive (&idio_primitive_data_ ## cname);

#define IDIO_ADD_SPECIAL_PRIMITIVE(cname) idio_add_special_primitive (&idio_primitive_data_ ## cname);

#define IDIO_ADD_EXPANDER(cname)	  idio_add_expander_primitive (&idio_primitive_data_ ## cname);

#define IDIO_DEFINE_OPERATOR_DESC(iname,cname,params,arity,varargs)	\
    IDIO idio_defoperator_ ## cname params;				\
    static struct idio_primitive_s idio_operator_data_ ## cname = { \
	idio_defoperator_ ## cname,					\
	iname,								\
	arity,								\
	varargs								\
    };									\
    IDIO idio_defoperator_ ## cname params

#define IDIO_DEFINE_OPERATOR(iname,cname,params)			\
    IDIO_DEFINE_OPERATOR_DESC(iname,cname,params,2,1)

#define IDIO_ADD_OPERATOR(cname)	  idio_add_operator_primitive (&idio_operator_data_ ## cname);

#define IDIO_VERIFY_PARAM_TYPE(type,param)		\
    {							\
	if (! idio_isa_ ## type (param)) {		\
	    idio_error_param_type (#type, param);	\
	    return idio_S_unspec;			\
	}						\
    }

#define IDIO_STREQP(s,cs)	(strlen (s) == strlen (cs) && strncmp (s, cs, strlen (cs)) == 0)

#include "gc.h"

#include "array.h"
#include "bignum.h"
#include "c-ffi.h"
#include "c-struct.h"
#include "c-type.h"
#include "character.h"
#include "closure.h"
#include "error.h"
#include "evaluate.h"
#include "scm-evaluate.h"
#include "file-handle.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "module.h"
#include "primitive.h"
#include "pair.h"
#include "path.h"
#include "read.h"
#include "scm-read.h"
#include "specialform.h"
#include "string.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"

/*
 * Some well-known constants.
 *
 * Update util.c:idio_as-string as well!
 */
#define IDIO_CONSTANT_NIL              0
#define IDIO_CONSTANT_UNDEF           -1
#define IDIO_CONSTANT_UNSPEC          -2
#define IDIO_CONSTANT_EOF              3
#define IDIO_CONSTANT_TRUE             4
#define IDIO_CONSTANT_FALSE            5
#define IDIO_CONSTANT_VOID            -6
#define IDIO_CONSTANT_NAN             -7

#define IDIO_CONSTANT_TOPLEVEL         36
#define IDIO_CONSTANT_PREDEF           37
#define IDIO_CONSTANT_LOCAL            38


#define idio_S_nil		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_NIL))
#define idio_S_undef		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_UNDEF))
#define idio_S_unspec		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_UNSPEC))
#define idio_S_eof		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_EOF))
#define idio_S_true		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_TRUE))
#define idio_S_false		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_FALSE))
#define idio_S_void		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_VOID))
#define idio_S_NaN		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_NAN))

#define idio_S_toplevel		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_TOPLEVEL))
#define idio_S_predef		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_PREDEF))
#define idio_S_local		((const IDIO) IDIO_CONSTANT (IDIO_CONSTANT_LOCAL))

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
