/*
 * Copyright (c) 2015, 2017 Ian Fitchet <idf(at)idio-lang.org>
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

#include <sys/types.h>

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
#include <glob.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <strings.h>
#include <pwd.h>
#include <grp.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>

/* 
 * BSD markers
 */
#if defined (__unix__)
#include <sys/param.h>
/* 
 * Now test for 
 *
 * #if defined(BSD)
 * ...
 * #endif
 */
#endif

/* Solaris doesn't define WAIT_ANY */
#ifndef WAIT_ANY
#define WAIT_ANY (-1)
#endif

#include <setjmp.h>

#include <termios.h>

/* 
 * How many signals are there?
 *
 * Linux, OpenSolaris and Mac OS X all seem to define NSIG as the
 * highest signal number.  On FreeBSD, NSIG is the "number of old
 * signals".  SIGRT* are in a range of their own.
 */

#define IDIO_LIBC_FSIG 1

#if defined (BSD)
#define IDIO_LIBC_NSIG (SIGRTMAX + 1)
#else
#define IDIO_LIBC_NSIG NSIG
#endif

#ifdef IDIO_VM_PERF
extern FILE *idio_vm_perf_FILE;
#endif

#ifdef IDIO_DEBUG

#define IDIO_C_ASSERT(x)	(assert (x))

/* IDIO_TYPE_ASSERT assumes a local variable f */
#define IDIO_TYPE_ASSERT(t,x) {						\
	if (! idio_isa_ ## t (x)) {					\
	    idio_error_param_type_C (#t, x, __FILE__, __func__, __LINE__); \
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

#define IDIO_S1(x)		#x
#define IDIO_S2(x)		IDIO_S1(x)
#define IDIO__LINE__		IDIO_S2(__LINE__)
#define IDIO_C_LOCATION(s)	(idio_string_C (s ":" __FILE__ ":" IDIO__LINE__))

#else

#define IDIO_C_ASSERT(x)	((void) 0)
#define IDIO_TYPE_ASSERT(t,x)	((void) 0)
#define IDIO_ASSERT(x)		((void) 0)
#define IDIO_ASSERT_FREE(x)	((void) 0)
#define IDIO_ASSERT_NOT_FREED(x) ((void) 0)
#define IDIO_EXIT(x)		{exit(x);}
#define IDIO_C_EXIT(x)		{exit(x);}
#define IDIO__LINE__		__LINE__
#define IDIO_C_LOCATION(s)	(idio_string_C (s))

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
 * The idio_S_nil is the default procedure properties.
 *
 * We are looking for the following for foo_C, ie. Idio's "foo-idio"

   IDIO_DEFINE_PRIMITIVE2 ("foo-idio", foo_C, (T1 a1, T2, a2))
   {
     ...
   }

 * should become
   
   IDIO idio_defprimitive_foo_C (T1 a1, T2 a2);
   static struct idio_primitive_desc_s idio_primitive_data_foo_C = {
      idio_defprimitive_foo_C,
      "foo-idio",
      idio_S_nil,
      2,
      0
   };
   IDIO idio_defprimitive_foo_C (T1 a1, T2 a2)
   {
     ...
   }


 * IDIO_ADD_PRIMITIVE (foo_C) can then access the static struct
 * idio_primitive_desc_s called idio_defprimitive_data_foo_C and pass
 * it to the code to actually add a primitive.
 */

#define IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,arity,varargs)	\
    IDIO idio_defprimitive_ ## cname params;				\
    static struct idio_primitive_desc_s idio_primitive_data_ ## cname = { \
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

#define IDIO_ADD_MODULE_PRIMITIVE(m,cname)	idio_add_module_primitive (m, &idio_primitive_data_ ## cname, idio_vm_constants);

#define IDIO_EXPORT_MODULE_PRIMITIVE(m,cname)	idio_export_module_primitive (m, &idio_primitive_data_ ## cname, idio_vm_constants);

#define IDIO_ADD_PRIMITIVE(cname)		idio_add_primitive (&idio_primitive_data_ ## cname, idio_vm_constants);

#define IDIO_ADD_EXPANDER(cname)		idio_add_expander_primitive (&idio_primitive_data_ ## cname, idio_vm_constants);

#define IDIO_DEFINE_INFIX_OPERATOR_DESC(iname,cname,params,arity,varargs) \
    IDIO idio_defoperator_ ## cname params;				\
    static struct idio_primitive_desc_s idio_infix_operator_data_ ## cname = { \
	idio_defoperator_ ## cname,					\
	iname,								\
	arity,								\
	varargs								\
    };									\
    IDIO idio_defoperator_ ## cname params

#define IDIO_DEFINE_INFIX_OPERATOR(iname,cname,params)			\
    IDIO_DEFINE_INFIX_OPERATOR_DESC(iname,cname,params,2,1)

#define IDIO_ADD_INFIX_OPERATOR(cname,pri)	  idio_add_infix_operator_primitive (&idio_infix_operator_data_ ## cname, pri);

#define IDIO_DEFINE_POSTFIX_OPERATOR_DESC(iname,cname,params,arity,varargs) \
    IDIO idio_defoperator_ ## cname params;				\
    static struct idio_primitive_desc_s idio_postfix_operator_data_ ## cname = { \
	idio_defoperator_ ## cname,					\
	iname,								\
	arity,								\
	varargs								\
    };									\
    IDIO idio_defoperator_ ## cname params

#define IDIO_DEFINE_POSTFIX_OPERATOR(iname,cname,params)		\
    IDIO_DEFINE_POSTFIX_OPERATOR_DESC(iname,cname,params,2,1)

#define IDIO_ADD_POSTFIX_OPERATOR(cname,pri)	  idio_add_postfix_operator_primitive (&idio_postfix_operator_data_ ## cname, pri);

#define IDIO_VERIFY_PARAM_TYPE(type,param)				\
    {									\
	if (! idio_isa_ ## type (param)) {				\
	    idio_error_param_type_C (#type, param, __FILE__, __func__, __LINE__); \
	}								\
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
#include "codegen.h"
#include "command.h"
#include "condition.h"
#include "continuation.h"
#include "env.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "keyword.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "primitive.h"
#include "read.h"
#include "string-handle.h"
#include "string.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"

#include "libc-wrap.h"

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

#define IDIO_CONSTANT_TOPLEVEL         30
#define IDIO_CONSTANT_PREDEF           31
#define IDIO_CONSTANT_LOCAL            32
#define IDIO_CONSTANT_ENVIRON          33
#define IDIO_CONSTANT_COMPUTED         34


#define idio_S_nil		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_NIL))
#define idio_S_undef		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_UNDEF))
#define idio_S_unspec		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_UNSPEC))
#define idio_S_eof		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_EOF))
#define idio_S_true		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_TRUE))
#define idio_S_false		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_FALSE))
#define idio_S_void		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_VOID))
#define idio_S_NaN		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_NAN))

#define idio_S_toplevel		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_TOPLEVEL))
#define idio_S_predef		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_PREDEF))
#define idio_S_local		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_LOCAL))
#define idio_S_environ		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_ENVIRON))
#define idio_S_computed		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_COMPUTED))

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
