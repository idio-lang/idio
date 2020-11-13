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
#include <limits.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
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

/* Solaris doesn't define RLIMIT_NLIMITS */
#ifndef RLIMIT_NLIMITS
#ifdef RLIM_NLIMITS
#define RLIMIT_NLIMITS RLIM_NLIMITS
#else
wither RLIMIT_NLIMITS?
#endif
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

#ifdef IDIO_VM_PROF
extern FILE *idio_vm_perf_FILE;
#endif

#ifdef IDIO_DEBUG

#define IDIO_C_ASSERT(x)	(assert (x))

#define IDIO_TYPE_ASSERT(t,x) {						\
	if (! idio_isa_ ## t (x)) {					\
	    idio_error_param_type_C (#t, x, __FILE__, __func__, __LINE__); \
	}								\
    }

#define IDIO_ASSERT_NOT_CONST(t,x) {					\
	if (IDIO_FLAGS (x) & IDIO_FLAG_CONST) {				\
	    idio_error_const_param_C (#t, x, __FILE__, __func__, __LINE__); \
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
#define IDIO_ASSERT_FREE(x)	((((intptr_t) x)&3)?1:(assert(((x)->gc_flags & IDIO_GC_FLAG_FREE_MASK) == IDIO_GC_FLAG_FREE)))
#define IDIO_ASSERT_NOT_FREED(x) ((((intptr_t) x)&3)?1:(assert(((x)->gc_flags & IDIO_GC_FLAG_FREE_MASK) != IDIO_GC_FLAG_FREE)))
#define IDIO_EXIT(x)		{IDIO_C_ASSERT(0);exit(x);}
#define IDIO_C_EXIT(x)		{IDIO_C_ASSERT(0);exit(x);}

#define IDIO_S1(x)			#x
#define IDIO_S2(x)			IDIO_S1(x)
#define IDIO__LINE__			IDIO_S2(__LINE__)
#define IDIO_C_LOCATION(s)		(idio_string_C (s ":" __FILE__ ":" IDIO__LINE__))
#define IDIO_C_FUNC_LOCATION()		(idio_string_C_array (2, (char *[]) { __FILE__ ":" IDIO__LINE__ ":", (char *) __func__  }))
#define IDIO_C_FUNC_LOCATION_S(s)	(idio_string_C_array (3, (char *[]) { __FILE__ ":" IDIO__LINE__ ":", (char *) __func__, "/" s }))

#else

#define IDIO_C_ASSERT(x)	((void) 0)
#define IDIO_TYPE_ASSERT(t,x)	{					\
	if (! idio_isa_ ## t (x)) {					\
	    idio_error_param_type_C (#t, x, __FILE__, __func__, __LINE__); \
	}								\
    }
#define IDIO_ASSERT_NOT_CONST(t,x) {					\
	if (IDIO_FLAGS (x) & IDIO_FLAG_CONST) {				\
	    idio_error_const_param_C (#t, x, __FILE__, __func__, __LINE__); \
	}								\
    }
#define IDIO_ASSERT(x)		((void) 0)
#define IDIO_ASSERT_FREE(x)	((void) 0)
#define IDIO_ASSERT_NOT_FREED(x) ((void) 0)
#define IDIO_EXIT(x)		{exit(x);}
#define IDIO_C_EXIT(x)		{exit(x);}
#define IDIO__LINE__			__LINE__
#define IDIO_C_LOCATION(s)		(idio_string_C (s))
#define IDIO_C_FUNC_LOCATION()		IDIO_C_LOCATION(__func__)
#define IDIO_C_FUNC_LOCATION_S(s)	IDIO_C_LOCATION(__func__)

#endif

#define IDIO_GC_FLAG_FREE_SET(x) ((((x)->gc_flags & IDIO_GC_FLAG_FREE_MASK) == IDIO_GC_FLAG_FREE))
/*
#define IDIO_IS_FREE(x)       (idio_S_nil == (x) || IDIO_GC_FLAG_FREE_SET (x))
*/
#define IDIO_IS_SET(x)        (idio_S_nil != (x) && (IDIO_GC_FLAG_FREE_SET (x) == 0))

/*
 * IDIO_WORD_MAX_LEN was originally BUFSIZ but that is not a fixed
 * value across systems -- it is meant to be an efficient buffer size
 * for that system after all, albeit at least 256 bytes.
 *
 * It also makes it a bit hard to test when it varies across systems.
 *
 * It's also hard to image a useful word longer than a certain size.
 * But what's the lower bound of that size?  How big do you like your
 * symbolic names, sir?  Few people are going to be typing in long
 * symbolic names but it is plausible that some (errant?)
 * code-generator might pony up a big'un.
 *
 * In the meanwhile, we need some sort of definition as we must
 * allocate a char buf[n] to put the collected chars in (and to check
 * we've not overrun said buffer).  We could be truly dynamic...but
 * I'm really busy right now.
 */
#define IDIO_WORD_MAX_LEN	1023

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

   IDIO_DEFINE_PRIMITIVE2_DS ("foo-idio", foo_C, (T1 a1, T2 a2), sigstr, docstr)
   {
     ...
   }

 * should become

   IDIO idio_defprimitive_foo_C (T1 a1, T2 a2);
   static struct idio_primitive_desc_s idio_primitive_data_foo_C = {
      idio_defprimitive_foo_C,
      "foo-idio",
      2,
      0,
      sigstr,
      docstr
   };
   IDIO idio_defprimitive_foo_C (T1 a1, T2 a2)
   {
     ...
   }


 * IDIO_ADD_PRIMITIVE (foo_C) can then access the static struct
 * idio_primitive_desc_s called idio_defprimitive_data_foo_C and pass
 * it to the code to actually add a primitive.
 */

#define IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,arity,varargs,sigstr,docstr) \
    IDIO idio_defprimitive_ ## cname params;				\
    static struct idio_primitive_desc_s idio_primitive_data_ ## cname = { \
	idio_defprimitive_ ## cname,					\
	iname,								\
	arity,								\
	varargs,							\
	sigstr,								\
	docstr								\
    };									\
    IDIO idio_defprimitive_ ## cname params

#define IDIO_DEFINE_PRIMITIVE0_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,0,0,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE0(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,0,0,"","")

#define IDIO_DEFINE_PRIMITIVE0V_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,0,1,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE0V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,0,1,"","")

#define IDIO_DEFINE_PRIMITIVE1_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,1,0,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE1(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,1,0,"","")

#define IDIO_DEFINE_PRIMITIVE1V_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,1,1,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE1V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,1,1,"","")

#define IDIO_DEFINE_PRIMITIVE2_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,2,0,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE2(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,2,0,"","")

#define IDIO_DEFINE_PRIMITIVE2V_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,2,1,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE2V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,2,1,"","")

#define IDIO_DEFINE_PRIMITIVE3_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,3,0,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE3(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,3,0,"","")

#define IDIO_DEFINE_PRIMITIVE3V_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,3,1,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE3V(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,3,1,"","")

#define IDIO_DEFINE_PRIMITIVE4_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,4,0,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE4(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,4,0,"","")

#define IDIO_DEFINE_PRIMITIVE5_DS(iname,cname,params,sigstr,docstr)	\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,5,0,sigstr,docstr)

#define IDIO_DEFINE_PRIMITIVE5(iname,cname,params)			\
    IDIO_DEFINE_PRIMITIVE_DESC(iname,cname,params,5,0,"","")

#define IDIO_ADD_MODULE_PRIMITIVE(m,cname)	idio_add_module_primitive (m, &idio_primitive_data_ ## cname, idio_vm_constants, __FILE__, __LINE__);

#define IDIO_EXPORT_MODULE_PRIMITIVE(m,cname)	idio_export_module_primitive (m, &idio_primitive_data_ ## cname, idio_vm_constants, __FILE__, __LINE__);

#define IDIO_ADD_PRIMITIVE(cname)		idio_add_primitive (&idio_primitive_data_ ## cname, idio_vm_constants, __FILE__, __LINE__);

#define IDIO_ADD_EXPANDER(cname)		idio_add_expander_primitive (&idio_primitive_data_ ## cname, idio_vm_constants, __FILE__, __LINE__);

#define IDIO_DEFINE_INFIX_OPERATOR_DESC(iname,cname,params,arity,varargs) \
    IDIO idio_defoperator_ ## cname params;				\
    static struct idio_primitive_desc_s idio_infix_operator_data_ ## cname = { \
	idio_defoperator_ ## cname,					\
	iname,								\
	arity,								\
	varargs,							\
	(char *) 0,							\
	(char *) 0							\
    };									\
    IDIO idio_defoperator_ ## cname params

#define IDIO_DEFINE_INFIX_OPERATOR(iname,cname,params)			\
    IDIO_DEFINE_INFIX_OPERATOR_DESC(iname,cname,params,2,1)

#define IDIO_ADD_INFIX_OPERATOR(cname,pri)	  idio_add_infix_operator_primitive (&idio_infix_operator_data_ ## cname, pri, __FILE__, __LINE__);

#define IDIO_DEFINE_POSTFIX_OPERATOR_DESC(iname,cname,params,arity,varargs) \
    IDIO idio_defoperator_ ## cname params;				\
    static struct idio_primitive_desc_s idio_postfix_operator_data_ ## cname = { \
	idio_defoperator_ ## cname,					\
	iname,								\
	arity,								\
	varargs,							\
	(char *) 0,							\
	(char *) 0							\
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

#define IDIO_KEYWORD_DECL(n)		IDIO idio_KW_ ## n
#define IDIO_KEYWORD_DEF(iname,cname)	idio_KW_ ## cname = idio_keywords_C_intern (iname);

#include "gc.h"

#include "array.h"
#include "bignum.h"
#include "bitset.h"
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

#define IDIO_CONSTANT_PRESERVE_STATE		 20
#define IDIO_CONSTANT_PRESERVE_ALL_STATE	 21
#define IDIO_CONSTANT_PUSH_TRAP			 22
#define IDIO_CONSTANT_PRESERVE_CONTINUATION	 23

/*
 * Stack markers
 */
#define IDIO_STACK_MARKER_PRESERVE_STATE		 20
#define IDIO_STACK_MARKER_PRESERVE_ALL_STATE		 21
#define IDIO_STACK_MARKER_PUSH_TRAP			 22
#define IDIO_STACK_MARKER_PRESERVE_CONTINUATION		 23
#define IDIO_STACK_MARKER_RETURN			 24
#define IDIO_STACK_MARKER_DYNAMIC			 25
#define IDIO_STACK_MARKER_ENVIRON			 26


/*
 * A distinguished value to a) shut the C compiler up when we know
 * we'll have sigsetjmp'd out of a function and b) we can potentially
 * catch and warn.
 */
#define IDIO_CONSTANT_NOTREACHED      -100



#define idio_S_nil		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_NIL))
#define idio_S_undef		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_UNDEF))
#define idio_S_unspec		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_UNSPEC))
#define idio_S_eof		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_EOF))
#define idio_S_true		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_TRUE))
#define idio_S_false		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_FALSE))
#define idio_S_void		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_VOID))
#define idio_S_NaN		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_NAN))

#define idio_SM_preserve_state		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_STACK_MARKER_PRESERVE_STATE))
#define idio_SM_preserve_all_state	((const IDIO) IDIO_CONSTANT_IDIO (IDIO_STACK_MARKER_PRESERVE_ALL_STATE))
#define idio_SM_push_trap		((const IDIO) IDIO_CONSTANT_IDIO (IDIO_STACK_MARKER_PUSH_TRAP))
#define idio_SM_preserve_continuation	((const IDIO) IDIO_CONSTANT_IDIO (IDIO_STACK_MARKER_PRESERVE_CONTINUATION))
#define idio_SM_return			((const IDIO) IDIO_CONSTANT_IDIO (IDIO_STACK_MARKER_RETURN))
#define idio_SM_dynamic			((const IDIO) IDIO_CONSTANT_IDIO (IDIO_STACK_MARKER_DYNAMIC))
#define idio_SM_environ			((const IDIO) IDIO_CONSTANT_IDIO (IDIO_STACK_MARKER_ENVIRON))

#define idio_S_notreached	((const IDIO) IDIO_CONSTANT_IDIO (IDIO_CONSTANT_NOTREACHED))

extern pid_t idio_pid;
extern int idio_bootstrap_complete;
extern int idio_exit_status;
extern IDIO idio_k_exit;
void idio_final ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
