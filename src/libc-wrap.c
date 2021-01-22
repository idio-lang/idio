/*
 * Copyright (c) 2015, 2017, 2018, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * libc-wrap.c
 *
 */

#include "idio.h"

IDIO idio_libc_wrap_module = idio_S_nil;
IDIO idio_vm_signal_handler_conditions;
char **idio_libc_signal_names = NULL;
IDIO idio_vm_errno_conditions;
char **idio_libc_errno_names = NULL;
char **idio_libc_rlimit_names = NULL;
static IDIO idio_libc_struct_sigaction = NULL;
static IDIO idio_libc_struct_utsname = NULL;
static IDIO idio_libc_struct_rlimit = NULL;
static IDIO idio_libc_struct_tms = NULL;
IDIO idio_libc_struct_stat = NULL;

static long idio_SC_CLK_TCK = 0;

/*
 * Indexes into structures for direct references
 */
#define IDIO_STRUCT_SIGACTION_SA_HANDLER	0
#define IDIO_STRUCT_SIGACTION_SA_SIGACTION	1
#define IDIO_STRUCT_SIGACTION_SA_MASK		2
#define IDIO_STRUCT_SIGACTION_SA_FLAGS		3

#define IDIO_STRUCT_UTSNAME_SYSNAME		0
#define IDIO_STRUCT_UTSNAME_NODENAME		1
#define IDIO_STRUCT_UTSNAME_RELEASE		2
#define IDIO_STRUCT_UTSNAME_VERSION		3
#define IDIO_STRUCT_UTSNAME_MACHINE		4

#define IDIO_STRUCT_RLIMIT_RLIM_CUR		0
#define IDIO_STRUCT_RLIMIT_RLIM_MAX		1

#define IDIO_STRUCT_TMS_RTIME			0
#define IDIO_STRUCT_TMS_UTIME			1
#define IDIO_STRUCT_TMS_STIME			2
#define IDIO_STRUCT_TMS_CUTIME			3
#define IDIO_STRUCT_TMS_CSTIME			4

void idio_libc_format_error (char *msg, IDIO name, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_libc_format_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
					       name));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

IDIO idio_libc_export_symbol_value (IDIO symbol, IDIO value)
{
    IDIO_ASSERT (symbol);
    IDIO_ASSERT (value);
    IDIO_TYPE_ASSERT (symbol, symbol);

    return idio_module_export_symbol_value (symbol, value, idio_libc_wrap_module);
}

/*
 * Code coverage:
 *
 * Added as a system equivalent of error.  I think.
 *
 * Not used.
 */
IDIO_DEFINE_PRIMITIVE0V_DS ("system-error", libc_system_error, (IDIO args), "[name [args]]", "\
raise a ^system-error						\n\
								\n\
:param name: error name						\n\
:type name: string or symbol					\n\
:param args: error args						\n\
:type args: list						\n\
")
{
    IDIO_ASSERT (args);

    /*
     * Test Case: n/a
     *
     * args is the varargs parameter -- should always be a list
     */
    IDIO_USER_TYPE_ASSERT (list, args);

    char *name = "n/k";

    if (idio_S_nil != args) {
	IDIO h = IDIO_PAIR_H (args);
	if (idio_isa_string (h)) {
	    size_t size = 0;
	    name = idio_string_as_C (h, &size);
	    args = IDIO_PAIR_T (args);
	} else if (idio_isa_symbol (h)) {
	    name = IDIO_SYMBOL_S (h);
	    args = IDIO_PAIR_T (args);
	}
    }

    idio_error_system_errno (name, args, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-rusage-ru_utime", libc_struct_rusage_ru_utime, (IDIO rusage), "rusage", "\
in C, rusage.ru_utime						\n\
								\n\
:param rusage: C struct rusage					\n\
:type rusage: C_pointer						\n\
:return: C struct timeval					\n\
:rtype: C_pointer						\n\
								\n\
See `getrusage` for a C struct rusage object.			\n\
								\n\
This function returns a copy of the ru_utime field.		\n\
")
{
    IDIO_ASSERT (rusage);

    /*
     * Test Case: libc-wrap-errors/struct-rusage-ru_utime-bad-type.idio
     *
     * struct-rusage-ru_utime #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, rusage);

    struct rusage *rup = IDIO_C_TYPE_POINTER_P (rusage);

    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    tvp->tv_sec = rup->ru_utime.tv_sec;
    tvp->tv_usec = rup->ru_utime.tv_usec;

    return idio_C_pointer_free_me (tvp);
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-rusage-ru_stime", libc_struct_rusage_ru_stime, (IDIO rusage), "rusage", "\
in C, rusage.ru_stime						\n\
								\n\
:param rusage: C struct rusage					\n\
:type rusage: C_pointer						\n\
:return: C struct timeval					\n\
:rtype: C_pointer						\n\
								\n\
See `getrusage` for a C struct rusage object.			\n\
								\n\
This function returns a copy of the ru_stime field.		\n\
")
{
    IDIO_ASSERT (rusage);

    /*
     * Test Case: libc-wrap-errors/struct-rusage-ru_stime-bad-type.idio
     *
     * struct-rusage-ru_stime #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, rusage);

    struct rusage *rup = IDIO_C_TYPE_POINTER_P (rusage);

    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    tvp->tv_sec = rup->ru_stime.tv_sec;
    tvp->tv_usec = rup->ru_stime.tv_usec;

    return idio_C_pointer_free_me (tvp);
}

char *idio_libc_struct_timeval_as_string (IDIO tv)
{
    IDIO_ASSERT (tv);

    IDIO_TYPE_ASSERT (C_pointer, tv);

    struct timeval *tvp = IDIO_C_TYPE_POINTER_P (tv);

    int prec = 6;
    if (idio_S_nil != idio_print_conversion_precision_sym) {
	IDIO ipcp = idio_module_symbol_value (idio_print_conversion_precision_sym,
					      idio_Idio_module,
					      IDIO_LIST1 (idio_S_false));

	if (idio_S_false != ipcp) {
	    if (idio_isa_fixnum (ipcp)) {
		prec = IDIO_FIXNUM_VAL (ipcp);
	    } else {
		/*
		 * Test Case: ??
		 *
		 * See test-bignum-error.idio -- messing with
		 * idio-print-conversion-* is "unwise."
		 */
		idio_error_param_type ("fixnum", ipcp, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}
    }

    char us[BUFSIZ];
    sprintf (us, "%06ld", tvp->tv_usec);
    char fmt[BUFSIZ];
    sprintf (fmt, "%%ld.%%.%ds", prec);
    char *buf;
    if (IDIO_ASPRINTF (&buf, fmt, tvp->tv_sec, us) == -1) {
	/*
	 * Test Case: ??
	 */
	idio_error_alloc ("asprintf");

	/* notreached */
	return NULL;
    }

    return buf;
}

IDIO_DEFINE_PRIMITIVE1_DS ("struct-timeval-as-string", libc_struct_timeval_as_string, (IDIO tv), "tv", "\
Return a C struct timeval as a string				\n\
								\n\
:param tv: C struct timeval					\n\
:type tv: C_pointer						\n\
:return: string							\n\
:rtype: string							\n\
")
{
    IDIO_ASSERT (tv);

    /*
     * Test Case: libc-wrap-errors/struct-timeval-as-string-bad-type.idio
     *
     * struct-timeval-as-string #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, tv);

    IDIO osh = idio_open_output_string_handle_C ();

    char *tvs = idio_libc_struct_timeval_as_string (tv);

    idio_display_C (tvs, osh);

    IDIO_GC_FREE (tvs);

    return idio_get_output_string (osh);
}

IDIO idio_libc_struct_timeval_pointer (struct timeval *tvp)
{
    IDIO_C_ASSERT (tvp);

    IDIO r = idio_C_pointer_free_me (tvp);
    IDIO_C_TYPE_POINTER_PRINTER (r)  = idio_libc_struct_timeval_as_string;

    return r;
}

IDIO_DEFINE_PRIMITIVE2_DS ("add-struct-timeval", libc_add_struct_timeval, (IDIO tv1, IDIO tv2), "tv1 tv2", "\
A simple function to calculate the sum of two C struct timevals \n\
								\n\
tv1 + tv2							\n\
								\n\
:param tv1: first timeval					\n\
:type tv1: C_pointer						\n\
:param tv2: second timeval					\n\
:type tv2: C_pointer						\n\
:return: C struct timeval or raises ^system-error		\n\
:rtype: C_pointer						\n\
")
{
    IDIO_ASSERT (tv1);
    IDIO_ASSERT (tv2);

    /*
     * Test Case: libc-wrap-errors/add-struct-timeval-bad-first-type.idio
     *
     * add-struct-timeval #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, tv1);
    /*
     * Test Case: libc-wrap-errors/add-struct-timeval-bad-second-type.idio
     *
     * add-struct-timeval (gettimeofday) #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, tv2);

    struct timeval *tv1p = IDIO_C_TYPE_POINTER_P (tv1);
    struct timeval *tv2p = IDIO_C_TYPE_POINTER_P (tv2);

    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    tvp->tv_sec = tv1p->tv_sec + tv2p->tv_sec;
    tvp->tv_usec = tv1p->tv_usec + tv2p->tv_usec;

    if (tvp->tv_usec > 1000000) {
	tvp->tv_usec -= 1000000;
	tvp->tv_sec += 1;
    }

    return idio_libc_struct_timeval_pointer (tvp);
}

IDIO_DEFINE_PRIMITIVE2_DS ("subtract-struct-timeval", libc_subtract_struct_timeval, (IDIO tv1, IDIO tv2), "tv1 tv2", "\
A simple function to calculate the difference between two C	\n\
struct timevals							\n\
								\n\
tv1 - tv2							\n\
								\n\
:param tv1: first timeval					\n\
:type tv1: C_pointer						\n\
:param tv2: second timeval					\n\
:type tv2: C_pointer						\n\
:return: C struct timeval or raises ^system-error		\n\
:rtype: C_pointer						\n\
")
{
    IDIO_ASSERT (tv1);
    IDIO_ASSERT (tv2);

    /*
     * Test Case: libc-wrap-errors/subtract-struct-timeval-bad-first-type.idio
     *
     * subtract-struct-timeval #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, tv1);
    /*
     * Test Case: libc-wrap-errors/subtract-struct-timeval-bad-second-type.idio
     *
     * subtract-struct-timeval (gettimeofday) #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, tv2);

    struct timeval *tv1p = IDIO_C_TYPE_POINTER_P (tv1);
    struct timeval *tv2p = IDIO_C_TYPE_POINTER_P (tv2);

    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    tvp->tv_sec = tv1p->tv_sec - tv2p->tv_sec;
    tvp->tv_usec = tv1p->tv_usec - tv2p->tv_usec;

    if (tvp->tv_usec < 0) {
	tvp->tv_usec += 1000000;
	tvp->tv_sec -= 1;
    }

    return idio_libc_struct_timeval_pointer (tvp);
}

IDIO_DEFINE_PRIMITIVE2_DS ("access", libc_access, (IDIO ipathname, IDIO imode), "pathname mode", "\
in C, access (pathname, mode)					\n\
a wrapper to libc access (2)					\n\
								\n\
:param pathname: file name					\n\
:type pathname: string						\n\
:param mode: accessibility check(s)				\n\
:type mode: C_int						\n\
:return: #t or #f						\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (ipathname);
    IDIO_ASSERT (imode);

    /*
     * Test Case: libc-wrap-errors/access-bad-pathname-type.idio
     *
     * access #t #t
     */
    IDIO_USER_TYPE_ASSERT (string, ipathname);
    /*
     * Test Case: libc-wrap-errors/access-bad-mode-type.idio
     *
     * access "." #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, imode);

    size_t size = 0;
    char *pathname = idio_string_as_C (ipathname, &size);
    size_t C_size = strlen (pathname);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/access-bad-format.idio
	 *
	 * access (join-string (make-string 1 #U+0) '("hello" "world")) libc/R_OK
	 */
	IDIO_GC_FREE (pathname);

	idio_libc_format_error ("access: pathname contains an ASCII NUL", ipathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int mode = IDIO_C_TYPE_INT (imode);

    IDIO r = idio_S_false;

    if (0 == access (pathname, mode)) {
	r = idio_S_true;
    }

    IDIO_GC_FREE (pathname);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("chdir", libc_chdir, (IDIO ipath), "path", "\
in C, chdir (path)						\n\
a wrapper to libc chdir (2)					\n\
								\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ipath);

    /*
     * Test Case: libc-wrap-errors/chdir-bad-type.idio
     *
     * chdir #t
     */
    IDIO_USER_TYPE_ASSERT (string, ipath);

    size_t size = 0;
    char *path = idio_string_as_C (ipath, &size);
    size_t C_size = strlen (path);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/chdir-bad-format.idio
	 *
	 * chdir (join-string (make-string 1 #U+0) '("hello" "world"))
	 */
	IDIO_GC_FREE (path);

	idio_libc_format_error ("chdir: path contains an ASCII NUL", ipath, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int r = chdir (path);

    IDIO_GC_FREE (path);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/chdir-non-existent.idio
	 *
	 * tmpdir := (make-tmp-dir)
	 * rmdir tmpdir
	 * chdir tmpdir
	 */
	idio_error_system_errno ("chdir", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("close", libc_close, (IDIO ifd), "fd", "\
in C, close (fd)						\n\
a wrapper to libc close (2)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ifd);

    /*
     * Test Case: libc-wrap-errors/close-bad-type.idio
     *
     * close #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = close (fd);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/close-bad-fd.idio
	 *
	 * fd+name := mkstemp tmpfilename
	 * close (ph fd+name)
	 * delete-file (pht fd+name)
	 * close (ph fd+name)
	 */
	idio_error_system_errno ("close", ifd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("dup", libc_dup, (IDIO ioldfd), "oldfd", "\
in C, dup (oldfd)						\n\
a wrapper to libc dup (2)					\n\
								\n\
:param oldfd: file descriptor					\n\
:type oldfd: C_int or fixnum					\n\
:return: new fd or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ioldfd);

    int oldfd = -1;
    if (idio_isa_fixnum (ioldfd)) {
	oldfd = IDIO_FIXNUM_VAL (ioldfd);
    } else if (idio_isa_C_int (ioldfd)) {
	oldfd = IDIO_C_TYPE_INT (ioldfd);
    } else {
	/*
	 * Test Case: libc-wrap-errors/dup-bad-type.idio
	 *
	 * dup #t
	 */
	idio_error_param_type ("fixnum|C_int oldfd", ioldfd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int r = dup (oldfd);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/dup-bad-fd.idio
	 *
	 * fd+name := mkstemp tmpfilename
	 * close (ph fd+name)
	 * delete-file (pht fd+name)
	 * dup (ph fd+name)
	 */
	idio_error_system_errno ("dup", ioldfd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2_DS ("dup2", libc_dup2, (IDIO ioldfd, IDIO inewfd), "oldfd newfd", "\
in C, dup2 (oldfd, newfd)					\n\
a wrapper to libc dup2 (2)					\n\
								\n\
:param oldfd: file descriptor					\n\
:type oldfd: C_int or fixnum					\n\
:param newfd: file descriptor					\n\
:type newfd: C_int or fixnum					\n\
:return: new fd or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ioldfd);
    IDIO_ASSERT (inewfd);

    int oldfd = -1;
    if (idio_isa_fixnum (ioldfd)) {
	oldfd = IDIO_FIXNUM_VAL (ioldfd);
    } else if (idio_isa_C_int (ioldfd)) {
	oldfd = IDIO_C_TYPE_INT (ioldfd);
    } else {
	/*
	 * Test Case: libc-wrap-errors/dup2-bad-type.idio
	 *
	 * dup2 #t 99
	 */
	idio_error_param_type ("fixnum|C_int oldfd", ioldfd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int newfd = -1;
    if (idio_isa_fixnum (inewfd)) {
	newfd = IDIO_FIXNUM_VAL (inewfd);
    } else if (idio_isa_C_int (inewfd)) {
	newfd = IDIO_C_TYPE_INT (inewfd);
    } else {
	/*
	 * Test Case: libc-wrap-errors/dup2-bad-type.idio
	 *
	 * dup2 99 #t
	 */
	idio_error_param_type ("fixnum|C_int newfd", inewfd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }


    int r = dup2 (oldfd, newfd);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/dup2-bad-fd.idio
	 *
	 * fd+name := mkstemp tmpfilename
	 * close (ph fd+name)
	 * delete-file (pht fd+name)
	 * dup2 (ph fd+name) 99
	 */
	idio_error_system_errno ("dup2", IDIO_LIST2 (ioldfd, inewfd), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("exit", libc_exit, (IDIO istatus), "status", "\
in C, close (status)						\n\
a wrapper to libc exit (2)					\n\
								\n\
:param status: exit status					\n\
:type status: fixnum						\n\
								\n\
DOES NOT RETURN :)						\n\
")
{
    IDIO_ASSERT (istatus);

    int status = 0;
    if (idio_isa_fixnum (istatus)) {
	status = IDIO_FIXNUM_VAL (istatus);
    } else {
	/*
	 * Test Case: libc-wrap-errors/exit-bad-type.idio
	 *
	 * exit #t
	 */
	idio_error_param_type ("fixnum", istatus, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    exit (status);

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2V_DS ("fcntl", libc_fcntl, (IDIO ifd, IDIO icmd, IDIO args), "fd cmd [args]", "\
in C, fcntl (oldfd, newfd)					\n\
a wrapper to libc fcntl (2)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int or fixnum					\n\
:param cmd: fcntl command					\n\
:type cmd: C_int						\n\
:param args: fcntl command args					\n\
:type args: list						\n\
:return: appropriate value or raises ^system-error		\n\
:rtype: appropriate value					\n\
								\n\
Supported commands include:					\n\
F_DUPFD								\n\
F_DUPFD_CLOEXEC (if supported)					\n\
F_GETFD								\n\
F_SETFD								\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (icmd);
    IDIO_ASSERT (args);

    int fd = 0;
    if (idio_isa_fixnum (ifd)) {
	fd = IDIO_FIXNUM_VAL (ifd);
    } else if (idio_isa_C_int (ifd)) {
	fd = IDIO_C_TYPE_INT (ifd);
    } else {
	/*
	 * Test Case: libc-wrap-errors/fcntl-bad-fd-type.idio
	 *
	 * fcntl #t F_DUPFD
	 */
	idio_error_param_type ("fixnum|C_int fd", ifd, IDIO_C_FUNC_LOCATION ());
    }

    int cmd = 0;
    if (idio_isa_C_int (icmd)) {
	cmd = IDIO_C_TYPE_INT (icmd);
    } else {
	/*
	 * Test Case: libc-wrap-errors/fcntl-bad-cmd-type.idio
	 *
	 * fcntl 0 1
	 */
	idio_error_param_type ("C_int cmd", icmd, IDIO_C_FUNC_LOCATION ());
    }

    IDIO iarg = idio_list_head (args);

    int r;

    switch (cmd) {
    case F_DUPFD:
	{
	    /*
	     * CentOS 6 i386 fcntl(2) says it wants long but accepts
	     * int
	     */
	    int arg;
	    if (idio_isa_fixnum (iarg)) {
		arg = IDIO_FIXNUM_VAL (iarg);
	    } else if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		/*
		 * Test Case: libc-wrap-errors/fcntl-F_DUPFD-bad-arg-type.idio
		 *
		 * fcntl 0 F_DUPFD #t
		 */
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
#if defined (F_DUPFD_CLOEXEC)
    case F_DUPFD_CLOEXEC:
	{
	    /*
	     * CentOS 6 i386 fcntl(2) says it wants long but accepts
	     * int
	     */
	    int arg;
	    if (idio_isa_fixnum (iarg)) {
		arg = IDIO_FIXNUM_VAL (iarg);
	    } else if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		/*
		 * Test Case: libc-wrap-errors/fcntl-F_DUPFD_CLOEXEC-bad-arg-type.idio
		 *
		 * fcntl 0 F_DUPFD_CLOEXEC #t
		 */
		idio_error_param_type ("fixnum|C_int", iarg, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
#endif
    case F_GETFD:
	{
	    r = fcntl (fd, cmd);
	}
	break;
    case F_SETFD:
	{
	    int arg;
	    if (idio_isa_C_int (iarg)) {
		arg = IDIO_C_TYPE_INT (iarg);
	    } else {
		/*
		 * Test Case: libc-wrap-errors/fcntl-F_SETFD-bad-arg-type.idio
		 *
		 * fcntl 0 F_SETFD #t
		 */
		idio_error_param_type ("C_int", iarg, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    r = fcntl (fd, cmd, arg);
	}
	break;
    default:
	/*
	 * Test Case: libc-wrap-errors/fcntl-unknown-cmd.idio
	 *
	 * fcntl 0 (C/integer-> 98765)
	 */
	idio_error_param_value ("fcntl", "unexpected cmd", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/fcntl-F_DUPFD-bad-fd.idio
	 *
	 * fd+name := mkstemp "XXXXXX"
	 * close (ph fd+name)
	 * delete-file (pht fd+name)
	 * fcntl (ph fd+name) F_DUPFD 0
	 */
	idio_error_system_errno ("fcntl", IDIO_LIST3 (ifd, icmd, args), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("fileno", libc_fileno, (IDIO ifilep), "FILE", "\
in C, fileno (FILE)						\n\
a wrapper to libc fileno (3)					\n\
								\n\
:param FILE: file handle					\n\
:type FILE: handle						\n\
:return: file descriptor					\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ifilep);

    int fd = 0;
    if (idio_isa_file_handle (ifilep)) {
	fd = idio_file_handle_fd (ifilep);
    } else {
	/*
	 * Test Case: libc-wrap-errors/fileno-bad-type.idio
	 *
	 * fileno #t
	 */
	idio_error_param_type ("file-handle", ifilep, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (fd);
}

IDIO_DEFINE_PRIMITIVE0_DS ("fork", libc_fork, (void), "", "\
in C, fork ()							\n\
a wrapper to libc fork (2)					\n\
								\n\
:return: 0 or PID						\n\
:rtype: C_int							\n\
")
{
    pid_t pid = fork ();

    if (-1 == pid) {
	/*
	 * Test Case: ??
	 *
	 * How do you make fork(2) fail?
	 */
	idio_error_system_errno ("fork", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0_DS ("getcwd", libc_getcwd, (void), "", "\
in C, getcwd ()							\n\
a wrapper to libc getcwd (3)					\n\
								\n\
:return: CWD or raises ^system-error				\n\
:rtype: string							\n\
")
{
    /*
     * getcwd(3) and its arguments
     *
     * A sensible {size}?
     *
     * PATH_MAX varies: POSIX is 256, CentOS 7 is 4096
     *
     * The Linux man page for realpath(3) suggests that calling
     * pathconf(3) for _PC_PATH_MAX doesn't improve matters a whole
     * bunch as it can return a value that is infeasible to allocate
     * in memory.
     *
     * Some systems (OS X, FreeBSD) suggest getcwd(3) should accept
     * MAXPATHLEN (which is #define'd as PATH_MAX in <sys/param.h>).
     *
     * A NULL {buf}?
     *
     * Some systems (older OS X, FreeBSD) do not support a zero {size}
     * parameter.  If passed a NULL {buf}, those systems seem to
     * allocate as much memory as is required to contain the result,
     * regardless of {size}.
     *
     * On systems that do support a zero {size} parameter then they
     * will limit themselves to allocating a maximum of {size} bytes
     * if passed a NULL {buf} and a non-zero {size}.
     *
     * Given that we can't set {size} to zero on some systems then
     * always set {size} to PATH_MAX which should be be enough.
     *
     * Bah! Until Fedora 33/gcc 10.2.1 which is complaining:
     *
     *  warning: argument 1 is null but the corresponding size argument 2 value is 4096
     *
     * It also helpfully reports that:
     *
     *  /usr/include/unistd.h:520:14: note: in a call to function ‘getcwd’ declared with attribute ‘write_only (1, 2)’
     *
     * See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=96832
     *
     * If getcwd(3) returns a value that consumes all of PATH_MAX (or
     * more) then we're doomed to hit other problems in the near
     * future anyway as other parts of the system try to use the
     * result.
     */
    char *cwd = getcwd (NULL, PATH_MAX);

    if (NULL == cwd) {
	/*
	 * Test Case: libc-wrap-errors/getcwd-no-access.idio
	 *
	 * tmpdir := (make-tmp-dir)
	 * chdir tmpdir
	 * chmod -rx tmpdir
	 * (getcwd)
	 */
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_string_C (cwd);
    /*
     * XXX getcwd() used system allocator
     */
    free (cwd);

    return r;
}

IDIO_DEFINE_PRIMITIVE0_DS ("getpgrp", libc_getpgrp, (void), "", "\
in C, getpgrp ()						\n\
a wrapper to libc getpgrp (2)					\n\
								\n\
:return: PGID or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    pid_t pid = getpgrp ();

    if (-1 == pid) {
	/*
	 * Test Case: ??
	 *
	 * Not sure this POSIX.1 variant getpgrp(2) can fail...here's
	 * hoping!
	 */
	idio_error_system_errno ("getpgrp", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE0_DS ("getpid", libc_getpid, (void), "", "\
in C, getpid ()							\n\
a wrapper to libc getpid (2)					\n\
								\n\
:return: PID or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    /*
     * XXX getpid(2) is always successful.
     */
    return idio_C_int (getpid ());
}

IDIO idio_libc_getrlimit (int resource)
{
    struct rlimit rlim;

    if (getrlimit (resource, &rlim) == -1) {
	/*
	 * Test Case:  libc-wrap-errors/getrlimit-bad-rlim.idio
	 *
	 * getrlimit (C/integer-> -1)
	 */
	idio_error_system_errno ("getrlimit", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * XXX
     *
     * rlim_t not ints!
     */
    return idio_struct_instance (idio_libc_struct_rlimit, IDIO_LIST2 (idio_C_int (rlim.rlim_cur),
								      idio_C_int (rlim.rlim_max)));
}

IDIO_DEFINE_PRIMITIVE1_DS ("getrlimit", libc_getrlimit, (IDIO iresource), "resource", "\
in C, getrlimit (resource)					\n\
a wrapper to libc getrlimit (2)					\n\
								\n\
:param resource: resource, see below				\n\
:type resource: C_int						\n\
:return: struct-rlimit or raises ^system-error			\n\
:rtype: struct instance						\n\
								\n\
The resource names follow C conventions such as ``RLIMIT_AS``	\n\
and ``RLIMIT_NOFILE``.						\n\
")
{
    IDIO_ASSERT (iresource);

    /*
     * Test Case: ??
     *
     * getrlimit's resource is overridden in the pursuit of
     * convenience
     */
    IDIO_USER_TYPE_ASSERT (C_int, iresource);

    return idio_libc_getrlimit (IDIO_C_TYPE_INT (iresource));
}

IDIO_DEFINE_PRIMITIVE1_DS ("getrusage", libc_getrusage, (IDIO who), "who", "\
in C, getrusage (who)						\n\
a wrapper to libc getrusage (2)					\n\
								\n\
:param who: who, see below					\n\
:type who: C_int						\n\
:return: struct-rusage or raises ^system-error			\n\
:rtype: struct-rusage						\n\
								\n\
The parameter `who` refers to RUSAGE_SELF or RUSAGE_CHILDREN	\n\
")
{
    IDIO_ASSERT (who);

    /*
     * Test Case: libc-wrap-errors/getrusage-bad-type.idio
     *
     * getrusage #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, who);

    struct rusage *rup = (struct rusage *) idio_alloc (sizeof (struct rusage));

    if (-1 == getrusage (IDIO_C_TYPE_INT (who), rup)) {
	/*
	 * Test Case:  libc-wrap-errors/getrusage-bad-who.idio
	 *
	 * On Linux:
	 *
	 * #define RUSAGE_SELF     0
	 * #define RUSAGE_CHILDREN (-1)
	 * #define RUSAGE_BOTH     (-2)
	 * #define RUSAGE_THREAD   1
	 *
	 * Is that positive/negative, a bit-mask?  15 seems to provoke
	 * EINVAL...
	 *
	 * getrusage (C/integer-> 15)
	 */
	idio_error_system_errno ("getrusage", who, IDIO_C_FUNC_LOCATION ());
    }

    return idio_C_pointer_free_me (rup);
}

IDIO idio_libc_struct_timeval_pointer (struct timeval *tvp);

IDIO_DEFINE_PRIMITIVE0_DS ("gettimeofday", libc_gettimeofday, (void), "", "\
in C, gettimeofday ()						\n\
a wrapper to libc gettimeofday (2)				\n\
								\n\
:return: struct-timeval or raises ^system-error			\n\
:rtype: struct-timeval						\n\
								\n\
The struct timezone parameter is not used.			\n\
")
{
    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    if (-1 == gettimeofday (tvp, NULL)) {
	/*
	 * Test Case: ??
	 *
	 * EFAULT One of tv or tz pointed outside the accessible
	 * address space.
	 */
	idio_error_system_errno ("gettimeofday", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_libc_struct_timeval_pointer (tvp);
}

IDIO_DEFINE_PRIMITIVE1_DS ("isatty", libc_isatty, (IDIO ifd), "fd", "\
in C, isatty (fd)						\n\
a wrapper to libc isatty (3)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:return: 1 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ifd);

    /*
     * Test Case: libc-wrap-errors/isatty-bad-type.idio
     *
     * isatty #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    int r = isatty (fd);

    if (0 == r) {
	/*
	 * Test Case: libc-wrap-errors/isatty-not-tty.idio
	 *
	 * fd+name := mkstemp "XXXXXX"
	 * delete-file (pht fd+name)
	 * isatty (ph fd+name)
	 */
	idio_error_system_errno ("isatty", ifd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2_DS ("kill", libc_kill, (IDIO ipid, IDIO isig), "pid sig", "\
in C, kill (pid, sig)						\n\
a wrapper to libc kill (2)					\n\
								\n\
:param pid: process ID						\n\
:type pid: C_int						\n\
:param fd: signal						\n\
:type fd: C_int							\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (isig);

    /*
     * Test Case: libc-wrap-errors/kill-bad-pid-type.idio
     *
     * kill #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ipid);
    /*
     * Test Case: libc-wrap-errors/kill-bad-sig-type.idio
     *
     * kill (C/integer-> PID) #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, isig);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int sig = IDIO_C_TYPE_INT (isig);

    int r = kill (pid, sig);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/kill-invalid-signal.idio
	 *
	 * ;; technically risky as 98765 could be a valid signal...
	 * kill (C/integer-> PID) (C/integer-> 98765)
	 */
	idio_error_system_errno ("kill", IDIO_LIST2 (ipid, isig), IDIO_C_FUNC_LOCATION ());
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2_DS ("mkdir", libc_mkdir, (IDIO ipathname, IDIO imode), "pathname mode", "\
in C, mkdir (pathname, mode)					\n\
a wrapper to libc mkdir (2)					\n\
								\n\
:param pathname: file name					\n\
:type pathname: string						\n\
:param mode: mkdiribility check(s)				\n\
:type mode: C_int						\n\
:return: #t or #f						\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (ipathname);
    IDIO_ASSERT (imode);

    /*
     * Test Case: libc-wrap-errors/mkdir-bad-pathname-type.idio
     *
     * mkdir #t #t
     */
    IDIO_USER_TYPE_ASSERT (string, ipathname);
    /*
     * Test Case: libc-wrap-errors/mkdir-bad-mode-type.idio
     *
     * mkdir "." #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, imode);

    size_t size = 0;
    char *pathname = idio_string_as_C (ipathname, &size);
    size_t C_size = strlen (pathname);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/mkdir-bad-format.idio
	 *
	 * mkdir (join-string (make-string 1 #U+0) '("hello" "world")) (C/integer-> #o555)
	 */
	IDIO_GC_FREE (pathname);

	idio_libc_format_error ("mkdir: pathname contains an ASCII NUL", ipathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int mode = IDIO_C_TYPE_INT (imode);

    int r = mkdir (pathname, mode);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/mkdir-pathname-exists.idio
	 *
	 * fd+name := mkstemp "XXXXXX"
	 * close (ph fd+name)
	 * mkdir (pht fd+name) (C/integer-> #o555)
	 *
	 * XXX You'll want an unwind-protect to actually delete the
	 * file!
	 */
	idio_error_system_errno ("mkdir", IDIO_LIST2 (ipathname, imode), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("mkdtemp", libc_mkdtemp, (IDIO itemplate), "template", "\
in C, mkdtemp (template)						\n\
a wrapper to libc mkdtemp (3)					\n\
								\n\
:param template: directory template				\n\
:type template: string						\n\
:return: modified template or raises ^system-error		\n\
:rtype: string							\n\
")
{
    IDIO_ASSERT (itemplate);

    /*
     * Test Case: libc-wrap-errors/mkdtemp-bad-type.idio
     *
     * mkdtemp #t
     */
    IDIO_USER_TYPE_ASSERT (string, itemplate);

    /*
     * XXX mkdtemp() requires a NUL-terminated C string and it will
     * modify the template part.
     */
    size_t size = 0;
    char *template = idio_string_as_C (itemplate, &size);
    size_t C_size = strlen (template);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/mkdtemp-bad-format.idio
	 *
	 * mkdtemp (join-string (make-string 1 #U+0) '("hello" "world"))
	 */
	IDIO_GC_FREE (template);

	idio_libc_format_error ("mkdtemp: template contains an ASCII NUL", itemplate, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    char *d = mkdtemp (template);

    if (NULL == d) {
	/*
	 * Test Case: libc-wrap-errors/mkdtemp-bad-template.idio
	 *
	 * mkdtemp "XXX"
	 */
	IDIO_GC_FREE (template);

	idio_error_system_errno ("mkdtemp", itemplate, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_string_C (d);

    IDIO_GC_FREE (template);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("mkstemp", libc_mkstemp, (IDIO itemplate), "template", "\
in C, mkstemp (template)						\n\
a wrapper to libc mkstemp (3)					\n\
								\n\
:param template: template template				\n\
:type template: string						\n\
:return: a list of (file descriptor and new filename) or raises ^system-error\n\
:rtype: list (C_int, string)					\n\
")
{
    IDIO_ASSERT (itemplate);

    /*
     * Test Case: libc-wrap-errors/mkstemp-bad-type.idio
     *
     * mkstemp #t
     */
    IDIO_USER_TYPE_ASSERT (string, itemplate);

    /*
     * XXX mkstemp() requires a NUL-terminated C string and it will
     * modify the template part.
     */
    size_t size = 0;
    char *template = idio_string_as_C (itemplate, &size);
    size_t C_size = strlen (template);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/mkstemp-bad-format.idio
	 *
	 * mkstemp (join-string (make-string 1 #U+0) '("hello" "world"))
	 */
	IDIO_GC_FREE (template);

	idio_libc_format_error ("mkstemp: template contains an ASCII NUL", itemplate, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int r = mkstemp (template);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/mkstemp-bad-template.idio
	 *
	 * mkstemp "XXX"
	 */
	IDIO_GC_FREE (template);

	idio_error_system_errno ("mkstemp", itemplate, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Yuk!  The semantics of mkstemp are slightly different to
     * mkdtemp, above.  mkdtemp "returns a pointer to the modified
     * template string on success" so we can return a new Idio string.
     *
     * mkstemp, however, "return the file descriptor of the temporary
     * file" with the tacit assumption that the caller can use the
     * modified template to unlink the file.
     *
     * Therefore, we need to return a tuple of the file descriptor and
     * a string of the created file name.
     */
    IDIO ifilename = idio_string_C (template);

    IDIO_GC_FREE (template);

    return IDIO_LIST2 (idio_C_int (r), ifilename);
}

IDIO_DEFINE_PRIMITIVE0_DS ("pipe", libc_pipe, (void), "", "\
in C, pipe ()							\n\
a wrapper to libc pipe (2)					\n\
								\n\
:return: pointer to pipe array or raises ^system-error		\n\
:rtype: C_pointer						\n\
								\n\
See ``pipe-reader`` and ``pipe-writer`` for accessors to	\n\
the pipe array.							\n\
")
{
    int *pipefd = idio_alloc (2 * sizeof (int));

    int r = pipe (pipefd);

    if (-1 == r) {
	/*
	 * Test Case: ??
	 *
	 * Short of reaching EMFILE/ENFILE there's not much we can do.
	 */
	idio_error_system_errno ("pipe", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_pointer_free_me (pipefd);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pipe-reader", libc_pipe_reader, (IDIO ipipefd), "pipefd", "\
Return the read end of the pipe array				\n\
								\n\
:param pipefd: pointer to pipe array				\n\
:type pipefd: C_pointer						\n\
:return: read end of the pipe array				\n\
:rtype: C_int							\n\
								\n\
See ``pipe`` for a constructor ofthe pipe array.		\n\
")
{
    IDIO_ASSERT (ipipefd);

    /*
     * Test Case: libc-wrap-errors/pipe-reader-bad-type.idio
     *
     * pipe-reader #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[0]);
}

IDIO_DEFINE_PRIMITIVE1_DS ("pipe-writer", libc_pipe_writer, (IDIO ipipefd), "pipefd", "\
Return the write end of the pipe array				\n\
								\n\
:param pipefd: pointer to pipe array				\n\
:type pipefd: C_pointer						\n\
:return: write end of the pipe array				\n\
:rtype: C_int							\n\
								\n\
See ``pipe`` for a constructor ofthe pipe array.		\n\
")
{
    IDIO_ASSERT (ipipefd);

    /*
     * Test Case: libc-wrap-errors/pipe-writer-bad-type.idio
     *
     * pipe-writer #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, ipipefd);

    int *pipefd = IDIO_C_TYPE_POINTER_P (ipipefd);

    return idio_C_int (pipefd[1]);
}

/*
 * XXX
 *
 * ssize_t read(int fd, void *buf, size_t count);
 *
 * The nominal limit for count is ssize_t in POSIX (noting that count
 * itself is size_t).
 *
 * Linux suggests it will limit read(2) to 0x7ffff000 (2,147,479,552)
 * bytes on both 32-bit and 64-bit systems.
 *
 * We allow a fixnum (2 bits short of an intptr_t) or a C_int -- while
 * we figure out the C FFI for typedefs -- which is a subset of
 * size_t.  Probably.
 */
IDIO_DEFINE_PRIMITIVE1V_DS ("read", libc_read, (IDIO ifd, IDIO icount), "fd [count]", "\
in C, read (fd[, count])					\n\
a wrapper to libc read (2)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:param count: number of bytes to read				\n\
:type count: fixnum or C_int					\n\
:return: string of bytes read or #<eof>				\n\
:rtype: string or #<eof>					\n\
")
{
    IDIO_ASSERT (ifd);

    /*
     * Test Case: libc-wrap-errors/read-bad-type.idio
     *
     * read #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    size_t count = BUFSIZ;

    if (idio_S_nil != icount) {
	icount = IDIO_PAIR_H (icount);

	if (idio_isa_fixnum (icount)) {
	    count = IDIO_FIXNUM_VAL (icount);
	} else if (idio_isa_C_int (icount)) {
	    count = IDIO_C_TYPE_INT (icount);
	} else {
	    /*
	     * Test Case: libc-wrap-errors/read-bad-count-type.idio
	     *
	     * read (C/integer-> 0) #t
	     */
	    idio_error_param_type ("fixnum|C_int", icount, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    char *buf = idio_alloc (count);

    ssize_t n = read (fd, buf, count);

    IDIO r;

    if (n) {
	r = idio_string_C_len (buf, n);
    } else {
	r = idio_S_eof;
    }

    IDIO_GC_FREE (buf);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("rmdir", libc_rmdir, (IDIO ipathname), "pathname", "\
in C, rmdir (pathname)						\n\
a wrapper to libc rmdir (2)					\n\
								\n\
:param pathname: directory to rmdir				\n\
:type pathname: string						\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ipathname);

    /*
     * Test Case: libc-wrap-errors/rmdir-bad-type.idio
     *
     * rmdir #t
     */
    IDIO_USER_TYPE_ASSERT (string, ipathname);

    size_t size = 0;
    char *pathname = idio_string_as_C (ipathname, &size);
    size_t C_size = strlen (pathname);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/rmdir-bad-format.idio
	 *
	 * rmdir (join-string (make-string 1 #U+0) '("hello" "world"))
	 */
	IDIO_GC_FREE (pathname);

	idio_libc_format_error ("rmdir: pathname contains an ASCII NUL", ipathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int r = rmdir (pathname);

    IDIO_GC_FREE (pathname);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/rmdir-non-existent.idio
	 *
	 * tmpdir := (make-tmp-dir)
	 * rmdir tmpdir
	 * rmdir tmpdir
	 */
	idio_error_system_errno ("rmdir", ipathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2_DS ("setpgid", libc_setpgid, (IDIO ipid, IDIO ipgid), "pid pgid", "\
in C, setpgid (pid, pgid)					\n\
a wrapper to libc setpgid (2)					\n\
								\n\
:param pid: process ID						\n\
:type pid: C_int						\n\
:param pgid: PGID						\n\
:type pgid: fixnum or C_int					\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (ipgid);

    /*
     * Test Case: libc-wrap-errors/setpgid-bad-pid-type.idio
     *
     * setpgid #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ipid);
    /*
     * Test Case: libc-wrap-errors/setpgid-bad-pgid-type.idio
     *
     * setpgid (C/integer-> PID) #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ipgid);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    pid_t pgid = IDIO_C_TYPE_INT (ipgid);

    int r = setpgid (pid, pgid);

    if (-1 == r) {
	if (EACCES == errno) {
	    /*
	     * The child has already successfully executed exec() =>
	     * EACCES for us.
	     *
	     * Since the child also ran setpgid() on itself before
	     * calling exec() we should be good.
	     */
	    r = 0;
	} else {
	    /*
	     * Test Case: libc-wrap-errors/setpgid-negative-pgid.idio
	     *
	     * setpgid (C/integer-> PID) (C/integer-> -1)
	     */
	    idio_error_system_errno ("setpgid", IDIO_LIST2 (ipid, ipgid), IDIO_C_FUNC_LOCATION ());
	}
    }

    return idio_C_int (r);
}

void idio_libc_setrlimit (int resource, struct rlimit *rlimp)
{
    if (setrlimit (resource, rlimp) == -1) {
	/*
	 * Test Case:  libc-wrap-errors/setrlimit-bad-rlim.idio
	 *
	 * setrlimit (C/integer-> -1) (getrlimit RLIMIT_CPU)
	 */
	idio_error_system_errno ("setrlimit", idio_S_nil, IDIO_C_FUNC_LOCATION ());
    }
}

IDIO_DEFINE_PRIMITIVE2_DS ("setrlimit", libc_setrlimit, (IDIO iresource, IDIO irlim), "resource rlim", "\
in C, setrlimit (resource, rlim)				\n\
a wrapper to libc setrlimit (2)					\n\
								\n\
:param resource: resource, see below				\n\
:type resource: C_int						\n\
:param rlim: struct-rlimit					\n\
:type rlim: struct instance					\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
								\n\
The resource names follow C conventions such as ``RLIMIT_AS``	\n\
and ``RLIMIT_NOFILE``.						\n\
								\n\
See ``getrlimit`` to obtain a struct-rlimit.			\n\
")
{
    IDIO_ASSERT (iresource);
    IDIO_ASSERT (irlim);

    /*
     * Test Case: ??
     *
     * setrlimit's resource is overridden in the pursuit of
     * convenience
     */
    IDIO_USER_TYPE_ASSERT (C_int, iresource);
    /*
     * Test Case: libc-wrap-errors/setrlimit-bad-rlim-type.idio
     *
     * setrlimit (C/integer-> 0) #t
     */
    IDIO_USER_TYPE_ASSERT (struct_instance, irlim);

    IDIO cur = idio_struct_instance_ref_direct (irlim, IDIO_STRUCT_RLIMIT_RLIM_CUR);
    IDIO max = idio_struct_instance_ref_direct (irlim, IDIO_STRUCT_RLIMIT_RLIM_MAX);

    struct rlimit rlim;
    /*
     * XXX
     *
     * rlim_t not ints!
     */
    rlim.rlim_cur = (rlim_t) idio_C_int_get (cur);
    rlim.rlim_max = (rlim_t) idio_C_int_get (max);

    idio_libc_setrlimit (IDIO_C_TYPE_INT (iresource), &rlim);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2_DS ("signal", libc_signal, (IDIO isig, IDIO ihandler), "sig handler", "\
in C, signal (sig, handler)					\n\
a wrapper to libc signal (2)					\n\
								\n\
:param sig: signal						\n\
:type sig: C_int						\n\
:param handler: signal disposition					\n\
:type handler: C_pointer						\n\
:return: previous disposition or raises ^system-error		\n\
:rtype: C_pointer						\n\
								\n\
The following dispositions are defined:				\n\
SIG_IGN								\n\
SIG_DFL								\n\
")
{
    IDIO_ASSERT (isig);
    IDIO_ASSERT (ihandler);

    /*
     * Test Case: libc-wrap-errors/signal-bad-sig-type.idio
     *
     * signal #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, isig);
    /*
     * Test Case: libc-wrap-errors/signal-bad-handler-type.idio
     *
     * signal (C/integer-> 0) #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, ihandler);

    int sig = IDIO_C_TYPE_INT (isig);
    void (*handler) (int) = IDIO_C_TYPE_POINTER_P (ihandler);

    void (*r) (int) = signal (sig, handler);

    if (SIG_ERR == r) {
	/*
	 * Test Case: libc-wrap-errors/signal-bad-signal.idio
	 *
	 * signal (C/integer-> -1) SIG_DFL
	 */
	idio_error_system_errno ("signal", IDIO_LIST2 (isig, ihandler), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_pointer (r);
}

/*
 * signal-handler isn't a real libc function.  It has been added in
 * to aid spotting if a parent process has kindly sigignored()d
 * SIGPIPE for us:
 *
 * == (libc/signal-handler SIGPIPE) SIG_IGN
 *
 * Hopefully we'll find other uses for it.
 */
IDIO_DEFINE_PRIMITIVE1_DS ("signal-handler", libc_signal_handler, (IDIO isig), "sig", "\
signal-handler (sig)						\n\
								\n\
A helper function to advise of the current disposition for a	\n\
given signal.							\n\
								\n\
== (libc/signal-handler SIGPIPE) SIG_IGN			\n\
								\n\
:param sig: signal						\n\
:type sig: C_int						\n\
:return: current disposition or raises ^system-error		\n\
:rtype: C_pointer						\n\
")
{
    IDIO_ASSERT (isig);

    /*
     * Test Case: libc-wrap-errors/signal-handler-bad-type.idio
     *
     * signal-handler #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, isig);

    int sig = IDIO_C_TYPE_INT (isig);

    struct sigaction osa;

    if (sigaction (sig, NULL, &osa) < 0) {
	/*
	 * Test Case: libc-wrap-errors/signal-handler-bad-signal.idio
	 *
	 * signal-handler (C/integer-> -1)
	 */
	idio_error_system_errno ("sigaction", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Our result be be either of:

     void     (*sa_handler)(int);
     void     (*sa_sigaction)(int, siginfo_t *, void *);

     * so, uh, prototype with no args!
     */
    void (*r) ();

    if (osa.sa_flags & SA_SIGINFO) {
	/*
	 * Code coverage:
	 *
	 * Not sure...
	 */
	r = osa.sa_sigaction;
    } else {
	r = osa.sa_handler;
    }

    return idio_C_pointer (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("sleep", libc_sleep, (IDIO iseconds), "seconds", "\
in C, sleep (seconds)						\n\
a wrapper to libc sleep (3)					\n\
								\n\
:param seconds: seconds to sleep				\n\
:type seconds: fixnum or C_int					\n\
:return: 0 or the number of seconds left if interrupted		\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (iseconds);

    unsigned int seconds = 0;
    if (idio_isa_fixnum (iseconds) &&
	IDIO_FIXNUM_VAL (iseconds) >= 0) {
	seconds = IDIO_FIXNUM_VAL (iseconds);
    } else if (idio_isa_C_uint (iseconds)) {
	seconds = IDIO_C_TYPE_UINT (iseconds);
    } else {
	/*
	 * Test Case: libc-wrap-errors/sleep-bad-type.idio
	 *
	 * sleep #t
	 */
	idio_error_param_type ("unsigned fixnum|C_uint", iseconds, IDIO_C_FUNC_LOCATION ());
    }

    unsigned int r = sleep (seconds);

    return idio_C_uint (r);
}

IDIO idio_libc_stat (IDIO pathname)
{
    IDIO_ASSERT (pathname);
    IDIO_TYPE_ASSERT (string, pathname);

    size_t size = 0;
    char *pathname_C = idio_string_as_C (pathname, &size);
    size_t C_size = strlen (pathname_C);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/access-bad-format.idio
	 *
	 * stat (join-string (make-string 1 #U+0) '("hello" "world"))
	 */
	IDIO_GC_FREE (pathname_C);

	idio_libc_format_error ("stat: pathname contains an ASCII NUL", pathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    struct stat sb;

    if (stat (pathname_C, &sb) == -1) {
	/*
	 * Test Case: libc-wrap-errors/stat-empty-pathname.idio
	 *
	 * stat ""
	 */
	IDIO_GC_FREE (pathname_C);

	idio_error_system_errno ("stat", pathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * XXX
     *
     * idio_C_uint for everything?  We should know more about these
     * typedefs.
     *
     * dev_t		st_dev;
     * ino_t		st_ino
     * mode_t		st_mode
     * nlink_t		st_nlink
     * uid_t		st_uid
     * gid_t		st_gid
     * dev_t		st_rdev
     * off_t		st_size
     * blksize_t	st_blksize
     * blkcnt_t		st_blocks
     * struct timespec	st_atim
     * struct timespec	st_mtim
     * struct timespec	st_ctim
     *
     * #define	st_atime st_atim.tv_sec
     * #define	st_mtime st_mtim.tv_sec
     * #define	st_ctime st_ctim.tv_sec
     */
    IDIO r = idio_struct_instance (idio_libc_struct_stat,
				   idio_pair (idio_C_uint (sb.st_dev),
				   idio_pair (idio_C_uint (sb.st_ino),
				   idio_pair (idio_C_uint (sb.st_mode),
				   idio_pair (idio_C_uint (sb.st_nlink),
				   idio_pair (idio_C_uint (sb.st_uid),
				   idio_pair (idio_C_uint (sb.st_gid),
				   idio_pair (idio_C_uint (sb.st_rdev),
				   idio_pair (idio_C_uint (sb.st_size),
				   idio_pair (idio_C_uint (sb.st_blksize),
				   idio_pair (idio_C_uint (sb.st_blocks),
				   idio_pair (idio_C_uint (sb.st_atime),
				   idio_pair (idio_C_uint (sb.st_mtime),
				   idio_pair (idio_C_uint (sb.st_ctime),
				   idio_S_nil))))))))))))));

    IDIO_GC_FREE (pathname_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("stat", libc_stat, (IDIO pathname), "pathname", "\
in C, stat (pathname)						\n\
a wrapper to libc stat (2)					\n\
								\n\
:param pathname: pathname to stat				\n\
:type pathname: string						\n\
:return: struct-stat or raises ^system-error			\n\
:rtype: struct instance						\n\
")
{
    IDIO_ASSERT (pathname);

    /*
     * Test Case: libc-wrap-errors/stat-bad-type.idio
     *
     * stat #t
     */
    IDIO_USER_TYPE_ASSERT (string, pathname);

    return idio_libc_stat (pathname);
}

IDIO_DEFINE_PRIMITIVE1_DS ("strerror", libc_strerror, (IDIO ierrnum), "errnum", "\
in C, strerror (errnum)						\n\
a wrapper to libc strerror (3)					\n\
								\n\
:param errnum: error code to describe				\n\
:type errnum : fixnum or C_int					\n\
:return: string describing errnum				\n\
:rtype: string							\n\
")
{
    IDIO_ASSERT (ierrnum);

    int errnum = 0;
    if (idio_isa_fixnum (ierrnum)) {
	errnum = IDIO_FIXNUM_VAL (ierrnum);
    } else if (idio_isa_C_int (ierrnum)) {
	errnum = IDIO_C_TYPE_INT (ierrnum);
    } else {
	/*
	 * Test Case: libc-wrap-errors/strerror-bad-type.idio
	 *
	 * strerror #t
	 */
	idio_error_param_type ("fixnum|C_int", ierrnum, IDIO_C_FUNC_LOCATION ());
    }

    /*
     * Arguably we could make a 0 < errnum < IDIO_LIBC_NERRNO check
     * ourselves, here.
     */

    errno = 0;
    char *r = strerror (errnum);

    if (0 != errno) {
	/*
	 * Test Case: ?? libc-wrap-errors/strerror-bad-errnum.idio
	 *
	 * strerror -1
	 *
	 * XXX this generates "Unknown error -1" which means, I guess, all ints are covered.
	 */
	idio_error_system_errno ("strerror", ierrnum, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_string_C (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("strsignal", libc_strsignal, (IDIO isignum), "sig", "\
in C, strsignal (sig)						\n\
a wrapper to libc strsignal (3)					\n\
								\n\
:param sig: signal number to describe				\n\
:type sig : fixnum or C_int					\n\
:return: string describing errnum				\n\
:rtype: string							\n\
								\n\
On some systems (Solaris) #n may be returned for an invalid	\n\
signal number.							\n\
")
{
    IDIO_ASSERT (isignum);

    int signum = 0;
    if (idio_isa_fixnum (isignum)) {
	signum = IDIO_FIXNUM_VAL (isignum);
    } else if (idio_isa_C_int (isignum)) {
	signum = IDIO_C_TYPE_INT (isignum);
    } else {
	/*
	 * Test Case: libc-wrap-errors/strsignal-bad-type.idio
	 *
	 * strsignal #t
	 */
	idio_error_param_type ("fixnum|C_int", isignum, IDIO_C_FUNC_LOCATION ());
    }

    char *r = strsignal (signum);

    if (NULL == r) {
	/*
	 * Code coverage: SunOS
	 *
	 * strsignal -1		; #n
	 */
	return idio_S_nil;
    } else {
	return idio_string_C (r);
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("tcgetattr", libc_tcgetattr, (IDIO ifd), "fd", "\
in C, tcgetattr (fd)						\n\
a wrapper to libc tcgetattr (3)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:return: struct termios or raises ^system-error			\n\
:rtype: C_pointer						\n\
")
{
    IDIO_ASSERT (ifd);

    /*
     * Test Case: libc-wrap-errors/tcgetattr-bad-type.idio
     *
     * tcgetattr #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    struct termios *tcattrs = idio_alloc (sizeof (struct termios));
    int r = tcgetattr (fd, tcattrs);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/tcgetattr-bad-fd.idio
	 *
	 * tcgetattr (C/integer-> -1)
	 */
	idio_error_system_errno ("tcgetattr", ifd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_pointer_free_me (tcattrs);
}

IDIO_DEFINE_PRIMITIVE1_DS ("tcgetpgrp", libc_tcgetpgrp, (IDIO ifd), "fd", "\
in C, tcgetpgrp (fd)						\n\
a wrapper to libc tcgetpgrp (3)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:return: PID or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ifd);

    /*
     * Test Case: libc-wrap-errors/tcgetpgrp-bad-type.idio
     *
     * tcgetpgrp #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);

    int fd = IDIO_C_TYPE_INT (ifd);

    pid_t pid = tcgetpgrp (fd);

    if (-1 == pid) {
	/*
	 * Test Case: libc-wrap-errors/tcgetpgrp-bad-fd.idio
	 *
	 * tcgetpgrp (C/integer-> -1)
	 */
	idio_error_system_errno ("tcgetpgrp", ifd, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (pid);
}

IDIO_DEFINE_PRIMITIVE3_DS ("tcsetattr", libc_tcsetattr, (IDIO ifd, IDIO ioptions, IDIO itcattrs), "fd options tcattrs", "\
in C, tcsetattr (fd)						\n\
a wrapper to libc tcsetattr (3)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:param options: see below					\n\
:type options: C_int						\n\
:param tcattrs: struct termios					\n\
:type tcattrs: C_pointer					\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
								\n\
The following options are defined:				\n\
TCSADRAIN							\n\
TCSAFLUSH							\n\
								\n\
See ``tcgetattr`` for obtaining a struct termios.		\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (ioptions);
    IDIO_ASSERT (itcattrs);

    /*
     * Test Case: libc-wrap-errors/tcsetattr-bad-fd-type.idio
     *
     * tcsetattr #t #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);
    /*
     * Test Case: libc-wrap-errors/tcsetattr-bad-options-type.idio
     *
     * tcsetattr (C/integer-> 0) #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ioptions);
    /*
     * Test Case: libc-wrap-errors/tcsetattr-bad-tcattrs-type.idio
     *
     * tcsetattr (C/integer-> 0) (C/integer-> 0) #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, itcattrs);

    int fd = IDIO_C_TYPE_INT (ifd);
    int options = IDIO_C_TYPE_INT (ioptions);
    struct termios *tcattrs = IDIO_C_TYPE_POINTER_P (itcattrs);

    int r = tcsetattr (fd, options, tcattrs);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/tcsetattr-bad-fd.idio
	 *
	 * get the attributes of stdin and assign them to fd -1
	 *
	 * tcsetattr (C/integer-> -1) (C/integer-> 0) (tcgetattr (C/integer-> 0))
	 */
	idio_error_system_errno ("tcsetattr", IDIO_LIST3 (ifd, ioptions, itcattrs), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2_DS ("tcsetpgrp", libc_tcsetpgrp, (IDIO ifd, IDIO ipgrp), "fd pgrp", "\
in C, tcsetpgrp (fd, pgrp)					\n\
a wrapper to libc tcsetpgrp (3)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:param pgrp: PGID						\n\
:type pgrp: C_int						\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (ipgrp);

    /*
     * Test Case: libc-wrap-errors/tcsetpgrp-bad-fd-type.idio
     *
     * tcsetpgrp #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);
    /*
     * Test Case: libc-wrap-errors/tcsetpgrp-bad-pgrp-type.idio
     *
     * tcsetpgrp (C/integer-> 0) #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ipgrp);

    int fd = IDIO_C_TYPE_INT (ifd);
    pid_t pgrp = IDIO_C_TYPE_INT (ipgrp);


    int r = tcsetpgrp (fd, pgrp);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/tcsetpgrp-bad-fd.idio
	 *
	 * tcsetpgrp (C/integer-> -1) (C/integer-> PID)
	 */
	idio_error_system_errno ("tcsetpgrp", IDIO_LIST2 (ifd, ipgrp), IDIO_C_FUNC_LOCATION ());
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("times", libc_times, (void), "", "\
in C, times ()							\n\
a wrapper to libc times (3)					\n\
								\n\
:return: struct-tms or raises ^system-error			\n\
:rtype: struct							\n\
								\n\
times(3) is complicated because we need to return the struct tms\n\
that the user would have passed in as a pointer and the clock_t,\n\
elapsed real time that times(3) returns.			\n\
								\n\
The elapsed real time appears as a new field, tms_rtime in the	\n\
structure.							\n\
								\n\
All fields are in clock ticks for which sysconf(_SC_CLK_TCK) is	\n\
available for reference as the exported symbol CLK_TCK.		\n\
								\n\
The fields are Idio numbers, not C_int types.			\n\
")
{
    struct tms tms_buf;

    clock_t ert = times (&tms_buf);

    if (-1 == ert) {
	/*
	 * Test Case: ??
	 *
	 * EFAULT tms points outside the process's address space.
	 */
	idio_error_system_errno ("times", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * XXX
     *
     * clock_t not integers!
     */
    return idio_struct_instance (idio_libc_struct_tms, IDIO_LIST5 (idio_integer (ert),
								   idio_integer (tms_buf.tms_utime),
								   idio_integer (tms_buf.tms_stime),
								   idio_integer (tms_buf.tms_cutime),
								   idio_integer (tms_buf.tms_cstime)));
}

/*
 * This function is only used to create a struct utsname instance
 * called libc/idio-uname so that you can invoke
 * libc/idio-uname.nodename in code.
 *
 * Should probably be migrated there and not confuse matters by being
 * a distinct function.
 */
IDIO idio_libc_uname ()
{
    struct utsname u;

    if (uname (&u) == -1) {
	/*
	 * Test Case: ??
	 *
	 * EFAULT buf is not valid.
	 */
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_struct_instance (idio_libc_struct_utsname, IDIO_LIST5 (idio_string_C (u.sysname),
								       idio_string_C (u.nodename),
								       idio_string_C (u.release),
								       idio_string_C (u.version),
								       idio_string_C (u.machine)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("uname", libc_uname, (void), "", "\
in C, uname ()						\n\
a wrapper to libc uname (3)					\n\
								\n\
:return: struct-utsname or raises ^system-error			\n\
:rtype: C_pointer						\n\
								\n\
Not strictly useful at the moment.  You might want to use	\n\
``libc/idio-uname`` such as:					\n\
								\n\
libc/idio-uname.nodename					\n\
								\n\
instead.							\n\
")
{
    struct utsname *up;
    up = idio_alloc (sizeof (struct utsname));

    if (uname (up) == -1) {
	/*
	 * Test Case: ??
	 *
	 * EFAULT buf is not valid.
	 */
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_pointer_free_me (up);
}

IDIO_DEFINE_PRIMITIVE1_DS ("unlink", libc_unlink, (IDIO ipathname), "pathname", "\
in C, unlink (pathname)						\n\
a wrapper to libc unlink (2)					\n\
								\n\
:param pathname: filename to unlink					\n\
:type pathname: string						\n\
:return: 0 or raises ^system-error				\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (ipathname);

    /*
     * Test Case: libc-wrap-errors/unlink-bad-type.idio
     *
     * unlink #t
     */
    IDIO_USER_TYPE_ASSERT (string, ipathname);

    size_t size = 0;
    char *pathname = idio_string_as_C (ipathname, &size);
    size_t C_size = strlen (pathname);
    if (C_size != size) {
	/*
	 * Test Case: libc-wrap-errors/access-bad-format.idio
	 *
	 * unlink (join-string (make-string 1 #U+0) '("hello" "world"))
	 */
	IDIO_GC_FREE (pathname);

	idio_libc_format_error ("unlink: pathname contains an ASCII NUL", ipathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int r = unlink (pathname);

    IDIO_GC_FREE (pathname);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/unlink-non-existent.idio
	 *
	 * fd+name := mkstemp "XXXXXX"
	 * close (ph fd+name)
	 * delete-file (pht fd+name)
	 * unlink (pht fd+name)
	 */
	idio_error_system_errno ("unlink", ipathname, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    return idio_C_int (r);
}

IDIO_DEFINE_PRIMITIVE2_DS ("waitpid", libc_waitpid, (IDIO ipid, IDIO ioptions), "pid options", "\
in C, waitpid (pid, options)					\n\
a wrapper to libc waitpid (2)					\n\
								\n\
:param pid: process ID						\n\
:type pid: C_int						\n\
:param options: see below					\n\
:type options: C_int						\n\
:return: list of (return code, status) or raises ^system-error	\n\
:rtype: list							\n\
								\n\
WAIT_ANY is defined as -1 in place of ``pid``.			\n\
								\n\
The following options are defined:				\n\
WNOHANG								\n\
WUNTRACED							\n\
								\n\
``status`` is C_pointer to a C ``int *``.  See ``WIFEXITED``,	\n\
``WEXITSTATUS``, ``WIFSIGNALLED``, ``WTERMSIG``, ``WIFSTOPPED``	\n\
for functions to manipulate ``status``.				\n\
")
{
    IDIO_ASSERT (ipid);
    IDIO_ASSERT (ioptions);

    /*
     * Test Case: libc-wrap-errors/waitpid-bad-pid-type.idio
     *
     * waitpid #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ipid);
    /*
     * Test Case: libc-wrap-errors/waitpid-bad-options-type.idio
     *
     * waitpid (C/integer-> 0) #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ioptions);

    pid_t pid = IDIO_C_TYPE_INT (ipid);
    int options = IDIO_C_TYPE_INT (ioptions);

    int *statusp = idio_alloc (sizeof (int));
    IDIO istatus = idio_C_pointer_free_me (statusp);

    pid_t r = waitpid (pid, statusp, options);

    if (-1 == r) {
	if (ECHILD == errno) {
	    return IDIO_LIST2 (idio_C_int (0), idio_S_nil);
	}

	/*
	 * Test Case: libc-wrap-errors/unlink-bad-options.idio
	 *
	 * waitpid (C/integer-> 0) (C/integer-> -1)
	 */
	idio_error_system_errno ("waitpid", IDIO_LIST2 (ipid, ioptions), IDIO_C_FUNC_LOCATION ());
    }

    return IDIO_LIST2 (idio_C_int (r), istatus);
}

IDIO_DEFINE_PRIMITIVE1_DS ("WEXITSTATUS", libc_WEXITSTATUS, (IDIO istatus), "status", "\
in C, WEXITSTATUS (status)					\n\
a wrapper to libc macro WEXITSTATUS, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C_pointer						\n\
:return: exit status of child					\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WEXITSTATUS-bad-type.idio
     *
     * WEXITSTATUS #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WEXITSTATUS (*statusp));
}

IDIO_DEFINE_PRIMITIVE1_DS ("WIFEXITED", libc_WIFEXITED, (IDIO istatus), "status", "\
in C, WIFEXITED (status)					\n\
a wrapper to libc macro WIFEXITED, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C_pointer						\n\
:return: #t if the child exited normally or #f			\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WIFEXITED-bad-type.idio
     *
     * WIFEXITED #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFEXITED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("WIFSIGNALED", libc_WIFSIGNALED, (IDIO istatus), "status", "\
in C, WIFSIGNALED (status)					\n\
a wrapper to libc macro WIFSIGNALED, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C_pointer						\n\
:return: #t if the child was terminated by a signal or #f	\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WIFSIGNALED-bad-type.idio
     *
     * WIFSIGNALED #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSIGNALED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("WIFSTOPPED", libc_WIFSTOPPED, (IDIO istatus), "status", "\
in C, WIFSTOPPED (status)					\n\
a wrapper to libc macro WIFSTOPPED, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C_pointer						\n\
:return: #t if the child was stopped by a signal or #f		\n\
:rtype: boolean							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WIFSTOPPED-bad-type.idio
     *
     * WIFSTOPPED #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    IDIO r = idio_S_false;

    if (WIFSTOPPED (*statusp)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("WTERMSIG", libc_WTERMSIG, (IDIO istatus), "status", "\
in C, WTERMSIG (status)						\n\
a wrapper to libc macro WTERMSIG, see waitpid (2)		\n\
								\n\
:param status: process status					\n\
:type status: C_pointer						\n\
:return: signal number that terminated child			\n\
:rtype: C_int							\n\
")
{
    IDIO_ASSERT (istatus);

    /*
     * Test Case: libc-wrap-errors/WTERMSIG-bad-type.idio
     *
     * WTERMSIG #t
     */
    IDIO_USER_TYPE_ASSERT (C_pointer, istatus);

    int *statusp = IDIO_C_TYPE_POINTER_P (istatus);

    return idio_C_int (WTERMSIG (*statusp));
}

IDIO_DEFINE_PRIMITIVE2_DS ("write", libc_write, (IDIO ifd, IDIO istr), "fd str", "\
in C, write (fd, str)						\n\
a wrapper to libc write (2)					\n\
								\n\
:param fd: file descriptor					\n\
:type fd: C_int							\n\
:param str: string						\n\
:type str: string						\n\
:return: number of bytes written or raises ^system-error	\n\
:rtype: integer							\n\
")
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (istr);

    /*
     * Test Case: libc-wrap-errors/write-bad-fd-type.idio
     *
     * write #t #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ifd);
    /*
     * Test Case: libc-wrap-errors/write-bad-str-type.idio
     *
     * write (C/integer-> 0) #t
     */
    IDIO_USER_TYPE_ASSERT (string, istr);

    int fd = IDIO_C_TYPE_INT (ifd);

    size_t blen = 0;
    char *str = idio_string_as_C (istr, &blen);

    ssize_t n = write (fd, str, blen);

    if (-1 == n) {
	/*
	 * Test Case: libc-wrap-errors/write-bad-fd.idio
	 *
	 * write (C/integer-> -1) "hello\n"
	 */
	idio_error_system_errno ("write", IDIO_LIST2 (ifd, istr), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_GC_FREE (str);

    return idio_integer (n);
}

/*
 * idio_libc_set_signal_names
 *
 * Surprisingly, despite using the macro value, say, SIGINT in code
 * there is no way to get the descriptive string "SIGINT" back out of
 * the system.  strsignal(3) provides the helpful string "Interrupt".
 *
 * Bash's support/signames.c leads the way
 */

/*
 * How many chars in SIGRTMIN+n ?
 *
 * strlen ("SIGRTMIN+") == 9
 * +1 for NUL == 10 chars
 *
 * IDIO_LIBC_SIGNAMELEN - 10 => max n of 9999
 */
#define IDIO_LIBC_SIGNAMELEN 14

static void idio_libc_set_signal_names ()
{
    idio_libc_signal_names = idio_alloc ((IDIO_LIBC_NSIG + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG; i++) {
	idio_libc_signal_names[i] = idio_alloc (IDIO_LIBC_SIGNAMELEN);
	*(idio_libc_signal_names[i]) = '\0';
    }
    idio_libc_signal_names[i] = NULL;

    /*
     * Linux and SIGRTMIN are a slippery pair.  To be fair, the header
     * file, asm-generic/signal.h, says "These should not be
     * considered constants from userland." [ie. userland, us, should
     * not consider these values as constants] but immediately defines
     * SIGRTMIN as 32.
     *
     * In practice, the code sees SIGRTMIN as 34 and a quick grep has
     * bits/signum.h #define'ing SIGRTMIN as (__libc_current_sigrtmin
     * ()).
     *
     * Which is neither clear nor portable (assuming bits/signum.h
     * speaks the truth).
     */
#if defined(SIGRTMIN) && defined(SIGRTMAX)
    int rtmin = SIGRTMIN;
    int rtmax = SIGRTMAX;
    if (rtmax > rtmin &&
	(rtmax - rtmin) > 7) {
        sprintf (idio_libc_signal_names[SIGRTMIN], "SIGRTMIN");
	sprintf (idio_libc_signal_names[SIGRTMAX], "SIGRTMAX");

	int rtmid = (rtmax - rtmin) / 2;
	int rtdiff = (rtmax - rtmin) - (rtmid * 2);
	if (rtdiff) {
	    rtmid++;
	}

	for (i = 1; i < rtmid ; i++) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	    sprintf (idio_libc_signal_names[rtmax - i], "SIGRTMAX-%d", i);
	}

	/*
	 * Can have an extra SIGRTMIN+n (the plus variant rather than
	 * SIGRTMIN-n) if there's an odd number -- don't forget it is
	 * SIGRTMIN -> SIGRTMAX *inclusive* so there is an off-by-one
	 * error tempting us here...
	 */
	if (0 == rtdiff) {
	    sprintf (idio_libc_signal_names[rtmin + i], "SIGRTMIN+%d", i);
	}
    }
#endif

#if defined(SIGHUP)
    IDIO_LIBC_SIGNAL (SIGHUP)
#endif

#if defined(SIGINT)
    IDIO_LIBC_SIGNAL (SIGINT)
#endif

#if defined(SIGQUIT)
    IDIO_LIBC_SIGNAL (SIGQUIT)
#endif

#if defined(SIGILL)
    IDIO_LIBC_SIGNAL (SIGILL)
#endif

#if defined(SIGTRAP)
    IDIO_LIBC_SIGNAL (SIGTRAP)
#endif

#if defined(SIGIOT)
    IDIO_LIBC_SIGNAL (SIGIOT)
#endif

#if defined(SIGEMT)
    IDIO_LIBC_SIGNAL (SIGEMT)
#endif

#if defined(SIGFPE)
    IDIO_LIBC_SIGNAL (SIGFPE)
#endif

#if defined(SIGKILL)
    IDIO_LIBC_SIGNAL_NAME (SIGKILL)
#endif

#if defined(SIGBUS)
    IDIO_LIBC_SIGNAL (SIGBUS)
#endif

#if defined(SIGSEGV)
    IDIO_LIBC_SIGNAL (SIGSEGV)
#endif

#if defined(SIGSYS)
    IDIO_LIBC_SIGNAL (SIGSYS)
#endif

#if defined(SIGPIPE)
    IDIO_LIBC_SIGNAL (SIGPIPE)
#endif

#if defined(SIGALRM)
    IDIO_LIBC_SIGNAL (SIGALRM)
#endif

#if defined(SIGTERM)
    IDIO_LIBC_SIGNAL (SIGTERM)
#endif

#if defined(SIGUSR1)
    IDIO_LIBC_SIGNAL (SIGUSR1)
#endif

#if defined(SIGUSR2)
    IDIO_LIBC_SIGNAL (SIGUSR2)
#endif

#if defined(SIGCHLD)
    IDIO_LIBC_SIGNAL (SIGCHLD)
#endif

#if defined(SIGPWR)
    IDIO_LIBC_SIGNAL (SIGPWR)
#endif

#if defined(SIGWINCH)
    IDIO_LIBC_SIGNAL (SIGWINCH)
#endif

#if defined(SIGURG)
    IDIO_LIBC_SIGNAL (SIGURG)
#endif

#if defined(SIGPOLL)
    IDIO_LIBC_SIGNAL (SIGPOLL)
#endif

#if defined(SIGSTOP)
    IDIO_LIBC_SIGNAL_NAME (SIGSTOP)
#endif

#if defined(SIGTSTP)
    IDIO_LIBC_SIGNAL (SIGTSTP)
#endif

#if defined(SIGCONT)
    IDIO_LIBC_SIGNAL (SIGCONT)
#endif

#if defined(SIGTTIN)
    IDIO_LIBC_SIGNAL (SIGTTIN)
#endif

#if defined(SIGTTOU)
    IDIO_LIBC_SIGNAL (SIGTTOU)
#endif

#if defined(SIGVTALRM)
    IDIO_LIBC_SIGNAL (SIGVTALRM)
#endif

#if defined(SIGPROF)
    IDIO_LIBC_SIGNAL (SIGPROF)
#endif

#if defined(SIGXCPU)
    IDIO_LIBC_SIGNAL (SIGXCPU)
#endif

#if defined(SIGXFSZ)
    IDIO_LIBC_SIGNAL (SIGXFSZ)
#endif

    /* SunOS */
#if defined(SIGWAITING)
    IDIO_LIBC_SIGNAL (SIGWAITING)
#endif

    /* SunOS */
#if defined(SIGLWP)
    IDIO_LIBC_SIGNAL (SIGLWP)
#endif

    /* SunOS */
#if defined(SIGFREEZE)
    IDIO_LIBC_SIGNAL (SIGFREEZE)
#endif

    /* SunOS */
#if defined(SIGTHAW)
    IDIO_LIBC_SIGNAL (SIGTHAW)
#endif

    /* SunOS */
#if defined(SIGCANCEL)
    IDIO_LIBC_SIGNAL (SIGCANCEL)
#endif

    /* SunOS */
#if defined(SIGLOST)
    IDIO_LIBC_SIGNAL (SIGLOST)
#endif

    /* SunOS */
#if defined(SIGXRES)
    IDIO_LIBC_SIGNAL (SIGXRES)
#endif

    /* SunOS */
#if defined(SIGJVM1)
    IDIO_LIBC_SIGNAL (SIGJVM1)
#endif

    /* SunOS */
#if defined(SIGJVM2)
    IDIO_LIBC_SIGNAL (SIGJVM2)
#endif

    /* OpenIndiana */
#if defined(SIGINFO)
    IDIO_LIBC_SIGNAL (SIGINFO)
#endif

    /* Linux */
#if defined(SIGSTKFLT)
    IDIO_LIBC_SIGNAL (SIGSTKFLT)
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FSIG ; i < IDIO_LIBC_NSIG ; i++) {
	if ('\0' == *(idio_libc_signal_names[i])) {
	    char sig_name[IDIO_LIBC_SIGNAMELEN + 3];
	    sprintf (sig_name, "SIGJUNK%d", i);
	    IDIO_LIBC_SIGNAL_NAME_ONLY (sig_name, i)
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped signal numbers:\n");
		fprintf (stderr, " %3.3s %-*.*s %s\n", "id", IDIO_LIBC_SIGNAMELEN, IDIO_LIBC_SIGNAMELEN, "Idio name", "strsignal()");
	    }
	    /*
	     * strsignal() leaks memory?
	     */
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_SIGNAMELEN, sig_name, strsignal (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

/*
 * (According to Bash's .../builtins/common.c)
 *
 * POSIX.2 says the signal names are displayed without the `SIG'
 * prefix.
 *
 * We'll support both.  Functions like ~signal~ says the full signal
 * name and ~sig~ says the POSIX.2 format (arguably it should be ~nal~
 * as we've stripped off the "sig" part...can't be witty in the API,
 * though!)
 */
char *idio_libc_signal_name (int signum)
{
    if (signum < IDIO_LIBC_FSIG ||
	signum > IDIO_LIBC_NSIG) {
	/*
	 * Test Case:  libc-wrap-errors/signal-name-bad-signum.idio
	 *
	 * signal-name (C/integer-> -1)
	 */
	idio_error_param_value ("signum", "should be 0 < int < NSIG (OS dependent)", IDIO_C_FUNC_LOCATION ());

	return NULL;
    }

    return idio_libc_signal_names[signum];
}

char *idio_libc_sig_name (int signum)
{
    char *signame = idio_libc_signal_name (signum);

    if (strncmp (signame, "SIG", 3) == 0) {
	return (signame + 3);
    } else {
	return signame;
    }
}

IDIO_DEFINE_PRIMITIVE1_DS ("sig-name", libc_sig_name, (IDIO isignum), "signum", "\
return the short signal name of ``signum``			\n\
								\n\
:param signum: signal number					\n\
:type signum: C_int						\n\
:return: short signal name					\n\
:rtype: string							\n\
								\n\
A short signal name would be \"QUIT\" or \"INT\".		\n\
								\n\
See ``signal-name`` for long versions.				\n\
")
{
    IDIO_ASSERT (isignum);

    /*
     * Test Case: libc-wrap-errors/sig-name-bad-type.idio
     *
     * sig-name #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, isignum);

    return idio_string_C (idio_libc_sig_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("sig-names", libc_sig_names, (void), "", "\
return a list of (number, short name) pairs of known signals	\n\
								\n\
:return: map of signal pairs					\n\
:rtype: list							\n\
								\n\
A short signal name would be \"QUIT\" or \"INT\".		\n\
								\n\
See ``signal-names`` for long versions.				\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_sig_name (i))), r);
    }

    return idio_list_reverse (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("signal-name", libc_signal_name, (IDIO isignum), "signum", "\
return the long signal name of ``signum``			\n\
								\n\
:param signum: signal number					\n\
:type signum: C_int						\n\
:return: long signal name					\n\
:rtype: string							\n\
								\n\
A long signal name would be \"SIGQUIT\" or \"SIGINT\".		\n\
								\n\
See ``sig-name`` for short versions.				\n\
")
{
    IDIO_ASSERT (isignum);

    /*
     * Test Case: libc-wrap-errors/signal-name-bad-type.idio
     *
     * signal-name #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, isignum);

    return idio_string_C (idio_libc_signal_name (IDIO_C_TYPE_INT (isignum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("signal-names", libc_signal_names, (void), "", "\
return a list of (number, long name) pairs of known signals	\n\
								\n\
:return: map of signal pairs					\n\
:rtype: list							\n\
								\n\
A long signal name would be \"SIGQUIT\" or \"SIGINT\".		\n\
								\n\
See ``sig-names`` for short versions.				\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FSIG; i < IDIO_LIBC_NSIG ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_signal_name (i))), r);
    }

    return idio_list_reverse (r);
}

/*
 * idio_libc_set_errno_names
 *
 * Surprisingly, despite using the macro value, say, ECHILD in code
 * there is no way to get the descriptive string "ECHILD" back out of
 * the system.  strerror(3) provides the helpful string "No child
 * processes".
 *
 * We must follow the path blazed above for signals
 */

/*
 * How many errnos are there?
 *
 * FreeBSD and OS X define ELAST but many others do not.
 *
 * Linux's errno(3) suggests we might be referring to the set
 * including *at least* Linux's own definitions, those of POSIX.1-2001
 * and those of C99.  (Open)Solaris' Intro(2) mentions the Single Unix
 * Specification as the source of all errnos and has gaps in its own
 * definitions for some BSD Networking (100-119) and XENIX (135-142).
 *
 * And there's a stragglers found in anything else that we stumble
 * across.
 *
 * CentOS 6&7	EHWPOISON	133
 * OI 151a8	ESTALE		151
 * OS X 9.8.0	ENOPOLICY	103	aka ELAST
 * OS X 14.4.0	EQFULL		106	aka ELAST
 * FreeBSD 10	EOWNERDEAD	96	aka ELAST
 * Ubuntu 14	EHWPOISON	133
 * Debian 8	EHWPOISON	133
 *
 */

#define IDIO_LIBC_FERRNO 1

#if defined (BSD)
#define IDIO_LIBC_NERRNO (ELAST + 1)
#elif defined (__linux__)
#define IDIO_LIBC_NERRNO (EHWPOISON + 1)
#elif defined (__APPLE__) && defined (__MACH__)
#define IDIO_LIBC_NERRNO (ELAST + 1)
#elif defined (__sun) && defined (__SVR4)
#define IDIO_LIBC_NERRNO (ESTALE + 1)
#else
#error Cannot define IDIO_LIBC_NERRNO
#endif

/*
 * How many chars in E-somename- ?
 *
 * Empirical study suggests ENOTRECOVERABLE, EPROTONOSUPPORT and
 * ESOCKTNOSUPPORT are the longest E-names at 15 chars each.
 */
#define IDIO_LIBC_ERRNAMELEN 20

static void idio_libc_set_errno_names ()
{
    idio_libc_errno_names = idio_alloc ((IDIO_LIBC_NERRNO + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO; i++) {
	idio_libc_errno_names[i] = idio_alloc (IDIO_LIBC_ERRNAMELEN);
	*(idio_libc_errno_names[i]) = '\0';
    }
    idio_libc_errno_names[i] = NULL;

    idio_libc_errno_names[0] = "E0";

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (E2BIG)
    IDIO_LIBC_ERRNO (E2BIG);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EACCES)
    IDIO_LIBC_ERRNO (EACCES);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRINUSE)
    IDIO_LIBC_ERRNO (EADDRINUSE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EADDRNOTAVAIL)
    IDIO_LIBC_ERRNO (EADDRNOTAVAIL);
#endif

    /* Linux, Solaris */
#if defined (EADV)
    IDIO_LIBC_ERRNO (EADV);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAFNOSUPPORT)
    IDIO_LIBC_ERRNO (EAFNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EAGAIN)
    IDIO_LIBC_ERRNO (EAGAIN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EALREADY)
    IDIO_LIBC_ERRNO (EALREADY);
#endif

    /* FreeBSD, OSX */
#if defined (EAUTH)
    IDIO_LIBC_ERRNO (EAUTH);
#endif

    /* OSX */
#if defined (EBADARCH)
    IDIO_LIBC_ERRNO (EBADARCH);
#endif

    /* Linux, Solaris */
#if defined (EBADE)
    IDIO_LIBC_ERRNO (EBADE);
#endif

    /* OSX */
#if defined (EBADEXEC)
    IDIO_LIBC_ERRNO (EBADEXEC);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADF)
    IDIO_LIBC_ERRNO (EBADF);
#endif

    /* Linux, Solaris */
#if defined (EBADFD)
    IDIO_LIBC_ERRNO (EBADFD);
#endif

    /* OSX */
#if defined (EBADMACHO)
    IDIO_LIBC_ERRNO (EBADMACHO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBADMSG)
    IDIO_LIBC_ERRNO (EBADMSG);
#endif

    /* Linux, Solaris */
#if defined (EBADR)
    IDIO_LIBC_ERRNO (EBADR);
#endif

    /* FreeBSD, OSX */
#if defined (EBADRPC)
    IDIO_LIBC_ERRNO (EBADRPC);
#endif

    /* Linux, Solaris */
#if defined (EBADRQC)
    IDIO_LIBC_ERRNO (EBADRQC);
#endif

    /* Linux, Solaris */
#if defined (EBADSLT)
    IDIO_LIBC_ERRNO (EBADSLT);
#endif

    /* Linux, Solaris */
#if defined (EBFONT)
    IDIO_LIBC_ERRNO (EBFONT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EBUSY)
    IDIO_LIBC_ERRNO (EBUSY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECANCELED)
    IDIO_LIBC_ERRNO (ECANCELED);
#endif

    /* FreeBSD */
#if defined (ECAPMODE)
    IDIO_LIBC_ERRNO (ECAPMODE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECHILD)
    IDIO_LIBC_ERRNO (ECHILD);
#endif

    /* Linux, Solaris */
#if defined (ECHRNG)
    IDIO_LIBC_ERRNO (ECHRNG);
#endif

    /* Linux, Solaris */
#if defined (ECOMM)
    IDIO_LIBC_ERRNO (ECOMM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNABORTED)
    IDIO_LIBC_ERRNO (ECONNABORTED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNREFUSED)
    IDIO_LIBC_ERRNO (ECONNREFUSED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ECONNRESET)
    IDIO_LIBC_ERRNO (ECONNRESET);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDEADLK)
    IDIO_LIBC_ERRNO (EDEADLK);
#endif

    /* Linux, Solaris */
#if defined (EDEADLOCK)
    IDIO_LIBC_ERRNO (EDEADLOCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDESTADDRREQ)
    IDIO_LIBC_ERRNO (EDESTADDRREQ);
#endif

    /* OSX */
#if defined (EDEVERR)
    IDIO_LIBC_ERRNO (EDEVERR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDOM)
    IDIO_LIBC_ERRNO (EDOM);
#endif

    /* FreeBSD */
#if defined (EDOOFUS)
    IDIO_LIBC_ERRNO (EDOOFUS);
#endif

    /* Linux */
#if defined (EDOTDOT)
    IDIO_LIBC_ERRNO (EDOTDOT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EDQUOT)
    IDIO_LIBC_ERRNO (EDQUOT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EEXIST)
    IDIO_LIBC_ERRNO (EEXIST);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFAULT)
    IDIO_LIBC_ERRNO (EFAULT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EFBIG)
    IDIO_LIBC_ERRNO (EFBIG);
#endif

    /* FreeBSD, OSX */
#if defined (EFTYPE)
    IDIO_LIBC_ERRNO (EFTYPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTDOWN)
    IDIO_LIBC_ERRNO (EHOSTDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EHOSTUNREACH)
    IDIO_LIBC_ERRNO (EHOSTUNREACH);
#endif

    /* Linux */
#if defined (EHWPOISON)
    IDIO_LIBC_ERRNO (EHWPOISON);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIDRM)
    IDIO_LIBC_ERRNO (EIDRM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EILSEQ)
    IDIO_LIBC_ERRNO (EILSEQ);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINPROGRESS)
    IDIO_LIBC_ERRNO (EINPROGRESS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINTR)
    IDIO_LIBC_ERRNO (EINTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EINVAL)
    IDIO_LIBC_ERRNO (EINVAL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EIO)
    IDIO_LIBC_ERRNO (EIO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISCONN)
    IDIO_LIBC_ERRNO (EISCONN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EISDIR)
    IDIO_LIBC_ERRNO (EISDIR);
#endif

    /* Linux */
#if defined (EISNAM)
    IDIO_LIBC_ERRNO (EISNAM);
#endif

    /* Linux */
#if defined (EKEYEXPIRED)
    IDIO_LIBC_ERRNO (EKEYEXPIRED);
#endif

    /* Linux */
#if defined (EKEYREJECTED)
    IDIO_LIBC_ERRNO (EKEYREJECTED);
#endif

    /* Linux */
#if defined (EKEYREVOKED)
    IDIO_LIBC_ERRNO (EKEYREVOKED);
#endif

    /* Linux, Solaris */
#if defined (EL2HLT)
    IDIO_LIBC_ERRNO (EL2HLT);
#endif

    /* Linux, Solaris */
#if defined (EL2NSYNC)
    IDIO_LIBC_ERRNO (EL2NSYNC);
#endif

    /* Linux, Solaris */
#if defined (EL3HLT)
    IDIO_LIBC_ERRNO (EL3HLT);
#endif

    /* Linux, Solaris */
#if defined (EL3RST)
    IDIO_LIBC_ERRNO (EL3RST);
#endif

    /* Linux, Solaris */
#if defined (ELIBACC)
    IDIO_LIBC_ERRNO (ELIBACC);
#endif

    /* Linux, Solaris */
#if defined (ELIBBAD)
    IDIO_LIBC_ERRNO (ELIBBAD);
#endif

    /* Linux, Solaris */
#if defined (ELIBEXEC)
    IDIO_LIBC_ERRNO (ELIBEXEC);
#endif

    /* Linux, Solaris */
#if defined (ELIBMAX)
    IDIO_LIBC_ERRNO (ELIBMAX);
#endif

    /* Linux, Solaris */
#if defined (ELIBSCN)
    IDIO_LIBC_ERRNO (ELIBSCN);
#endif

    /* Linux, Solaris */
#if defined (ELNRNG)
    IDIO_LIBC_ERRNO (ELNRNG);
#endif

    /* Solaris */
#if defined (ELOCKUNMAPPED)
    IDIO_LIBC_ERRNO (ELOCKUNMAPPED);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ELOOP)
    IDIO_LIBC_ERRNO (ELOOP);
#endif

    /* Linux */
#if defined (EMEDIUMTYPE)
    IDIO_LIBC_ERRNO (EMEDIUMTYPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMFILE)
    IDIO_LIBC_ERRNO (EMFILE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMLINK)
    IDIO_LIBC_ERRNO (EMLINK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMSGSIZE)
    IDIO_LIBC_ERRNO (EMSGSIZE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EMULTIHOP)
    IDIO_LIBC_ERRNO (EMULTIHOP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENAMETOOLONG)
    IDIO_LIBC_ERRNO (ENAMETOOLONG);
#endif

    /* Linux */
#if defined (ENAVAIL)
    IDIO_LIBC_ERRNO (ENAVAIL);
#endif

    /* FreeBSD, OSX */
#if defined (ENEEDAUTH)
    IDIO_LIBC_ERRNO (ENEEDAUTH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETDOWN)
    IDIO_LIBC_ERRNO (ENETDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETRESET)
    IDIO_LIBC_ERRNO (ENETRESET);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENETUNREACH)
    IDIO_LIBC_ERRNO (ENETUNREACH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENFILE)
    IDIO_LIBC_ERRNO (ENFILE);
#endif

    /* Linux, Solaris */
#if defined (ENOANO)
    IDIO_LIBC_ERRNO (ENOANO);
#endif

    /* FreeBSD, OSX */
#if defined (ENOATTR)
    IDIO_LIBC_ERRNO (ENOATTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOBUFS)
    IDIO_LIBC_ERRNO (ENOBUFS);
#endif

    /* Linux, Solaris */
#if defined (ENOCSI)
    IDIO_LIBC_ERRNO (ENOCSI);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENODATA)
    IDIO_LIBC_ERRNO (ENODATA);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENODEV)
    IDIO_LIBC_ERRNO (ENODEV);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOENT)
    IDIO_LIBC_ERRNO (ENOENT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOEXEC)
    IDIO_LIBC_ERRNO (ENOEXEC);
#endif

    /* Linux */
#if defined (ENOKEY)
    IDIO_LIBC_ERRNO (ENOKEY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLCK)
    IDIO_LIBC_ERRNO (ENOLCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOLINK)
    IDIO_LIBC_ERRNO (ENOLINK);
#endif

    /* Linux */
#if defined (ENOMEDIUM)
    IDIO_LIBC_ERRNO (ENOMEDIUM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMEM)
    IDIO_LIBC_ERRNO (ENOMEM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOMSG)
    IDIO_LIBC_ERRNO (ENOMSG);
#endif

    /* Linux, Solaris */
#if defined (ENONET)
    IDIO_LIBC_ERRNO (ENONET);
#endif

    /* Linux, Solaris */
#if defined (ENOPKG)
    IDIO_LIBC_ERRNO (ENOPKG);
#endif

    /* OSX */
#if defined (ENOPOLICY)
    IDIO_LIBC_ERRNO (ENOPOLICY);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOPROTOOPT)
    IDIO_LIBC_ERRNO (ENOPROTOOPT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSPC)
    IDIO_LIBC_ERRNO (ENOSPC);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSR)
    IDIO_LIBC_ERRNO (ENOSR);
#endif

    /* Linux, OSX, Solaris */
#if defined (ENOSTR)
    IDIO_LIBC_ERRNO (ENOSTR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOSYS)
    IDIO_LIBC_ERRNO (ENOSYS);
#endif

    /* Solaris */
#if defined (ENOTACTIVE)
    IDIO_LIBC_ERRNO (ENOTACTIVE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTBLK)
    IDIO_LIBC_ERRNO (ENOTBLK);
#endif

    /* FreeBSD */
#if defined (ENOTCAPABLE)
    IDIO_LIBC_ERRNO (ENOTCAPABLE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTCONN)
    IDIO_LIBC_ERRNO (ENOTCONN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTDIR)
    IDIO_LIBC_ERRNO (ENOTDIR);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTEMPTY)
    IDIO_LIBC_ERRNO (ENOTEMPTY);
#endif

    /* Linux */
#if defined (ENOTNAM)
    IDIO_LIBC_ERRNO (ENOTNAM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTRECOVERABLE)
    IDIO_LIBC_ERRNO (ENOTRECOVERABLE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTSOCK)
    IDIO_LIBC_ERRNO (ENOTSOCK);
#endif

    /* FreeBSD, OSX, Solaris */
#if defined (ENOTSUP)
    IDIO_LIBC_ERRNO (ENOTSUP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENOTTY)
    IDIO_LIBC_ERRNO (ENOTTY);
#endif

    /* Linux, Solaris */
#if defined (ENOTUNIQ)
    IDIO_LIBC_ERRNO (ENOTUNIQ);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ENXIO)
    IDIO_LIBC_ERRNO (ENXIO);
#endif

    /* FreeBSD, Linux, OSX, OSX, Solaris */
#if defined (EOPNOTSUPP)
    IDIO_LIBC_ERRNO (EOPNOTSUPP);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOVERFLOW)
    IDIO_LIBC_ERRNO (EOVERFLOW);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EOWNERDEAD)
    IDIO_LIBC_ERRNO (EOWNERDEAD);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPERM)
    IDIO_LIBC_ERRNO (EPERM);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPFNOSUPPORT)
    IDIO_LIBC_ERRNO (EPFNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPIPE)
    IDIO_LIBC_ERRNO (EPIPE);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCLIM)
    IDIO_LIBC_ERRNO (EPROCLIM);
#endif

    /* FreeBSD, OSX */
#if defined (EPROCUNAVAIL)
    IDIO_LIBC_ERRNO (EPROCUNAVAIL);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGMISMATCH)
    IDIO_LIBC_ERRNO (EPROGMISMATCH);
#endif

    /* FreeBSD, OSX */
#if defined (EPROGUNAVAIL)
    IDIO_LIBC_ERRNO (EPROGUNAVAIL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTO)
    IDIO_LIBC_ERRNO (EPROTO);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTONOSUPPORT)
    IDIO_LIBC_ERRNO (EPROTONOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EPROTOTYPE)
    IDIO_LIBC_ERRNO (EPROTOTYPE);
#endif

    /* OSX */
#if defined (EPWROFF)
    IDIO_LIBC_ERRNO (EPWROFF);
#endif

    /* OSX */
#if defined (EQFULL)
    IDIO_LIBC_ERRNO (EQFULL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ERANGE)
    IDIO_LIBC_ERRNO (ERANGE);
#endif

    /* Linux, Solaris */
#if defined (EREMCHG)
    IDIO_LIBC_ERRNO (EREMCHG);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EREMOTE)
    IDIO_LIBC_ERRNO (EREMOTE);
#endif

    /* Linux */
#if defined (EREMOTEIO)
    IDIO_LIBC_ERRNO (EREMOTEIO);
#endif

    /* Linux, Solaris */
#if defined (ERESTART)
    IDIO_LIBC_ERRNO (ERESTART);
#endif

    /* Linux */
#if defined (ERFKILL)
    IDIO_LIBC_ERRNO (ERFKILL);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EROFS)
    IDIO_LIBC_ERRNO (EROFS);
#endif

    /* FreeBSD, OSX */
#if defined (ERPCMISMATCH)
    IDIO_LIBC_ERRNO (ERPCMISMATCH);
#endif

    /* OSX */
#if defined (ESHLIBVERS)
    IDIO_LIBC_ERRNO (ESHLIBVERS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESHUTDOWN)
    IDIO_LIBC_ERRNO (ESHUTDOWN);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESOCKTNOSUPPORT)
    IDIO_LIBC_ERRNO (ESOCKTNOSUPPORT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESPIPE)
    IDIO_LIBC_ERRNO (ESPIPE);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESRCH)
    IDIO_LIBC_ERRNO (ESRCH);
#endif

    /* Linux, Solaris */
#if defined (ESRMNT)
    IDIO_LIBC_ERRNO (ESRMNT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ESTALE)
    IDIO_LIBC_ERRNO (ESTALE);
#endif

    /* Linux, Solaris */
#if defined (ESTRPIPE)
    IDIO_LIBC_ERRNO (ESTRPIPE);
#endif

    /* Linux, OSX, Solaris */
#if defined (ETIME)
    IDIO_LIBC_ERRNO (ETIME);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETIMEDOUT)
    IDIO_LIBC_ERRNO (ETIMEDOUT);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETOOMANYREFS)
    IDIO_LIBC_ERRNO (ETOOMANYREFS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (ETXTBSY)
    IDIO_LIBC_ERRNO (ETXTBSY);
#endif

    /* Linux */
#if defined (EUCLEAN)
    IDIO_LIBC_ERRNO (EUCLEAN);
#endif

    /* Linux, Solaris */
#if defined (EUNATCH)
    IDIO_LIBC_ERRNO (EUNATCH);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EUSERS)
    IDIO_LIBC_ERRNO (EUSERS);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EWOULDBLOCK)
    IDIO_LIBC_ERRNO (EWOULDBLOCK);
#endif

    /* FreeBSD, Linux, OSX, Solaris */
#if defined (EXDEV)
    IDIO_LIBC_ERRNO (EXDEV);
#endif

    /* Linux, Solaris */
#if defined (EXFULL)
    IDIO_LIBC_ERRNO (EXFULL);
#endif

    /*
     * OpenIndiana anomalies -- strerror returns a non "Error X"
     * string - sys/errno.h says "XENIX has 135 - 142"
     *
     * num	errno?		strerror
     *
     * 135	EUCLEAN		Structure needs cleaning
     * 137	ENOTNAM		Not a name file
     * 138	?		Not available
     * 139	EISNAM		Is a name file
     * 140	EREMOTEIO	Remote I/O error
     * 141	?		Reserved for future use
     */

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FERRNO ; i < IDIO_LIBC_NERRNO ; i++) {
	if ('\0' == *(idio_libc_errno_names[i])) {
	    char err_name[IDIO_LIBC_ERRNAMELEN + 2];
	    sprintf (err_name, "ERRUNKNOWN%d", i);
	    IDIO err_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (err_sym, idio_C_int (i));
	    sprintf (idio_libc_errno_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped errno numbers:\n");
		fprintf (stderr, " %3.3s %-*.*s %s\n", "id", IDIO_LIBC_ERRNAMELEN, IDIO_LIBC_ERRNAMELEN, "Idio name", "strerror ()");
	    }
	    /*
	     * strerror() leaks memory?
	     */
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_ERRNAMELEN, err_name, strerror (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

char *idio_libc_errno_name (int errnum)
{
    if (errnum < 0 ||
	errnum > IDIO_LIBC_NERRNO) {
	/*
	 * Test Case:  libc-wrap-errors/errno-name-bad-errnum.idio
	 *
	 * errno-name (C/integer-> -1)
	 */
	idio_error_param_value ("errnum", "should be 0 < int < NERRNO (OS dependent)", IDIO_C_FUNC_LOCATION ());

	return NULL;
    }

    return idio_libc_errno_names[errnum];
}

IDIO_DEFINE_PRIMITIVE1_DS ("errno-name", libc_errno_name, (IDIO ierrnum), "errnum", "\
return the error name of ``errnum``				\n\
								\n\
:param errnum: error number					\n\
:type errnum: C_int						\n\
:return: error name						\n\
:rtype: string							\n\
")
{
    IDIO_ASSERT (ierrnum);

    /*
     * Test Case: libc-wrap-errors/errno-name-bad-type.idio
     *
     * errno-name #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("errno-names", libc_errno_names, (void), "", "\
return a list of (number, name) pairs of known errno numbers	\n\
								\n\
:return: map of errno pairs					\n\
:rtype: list							\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_errno_name (i))), r);
    }

    return idio_list_reverse (r);
}

/*
 * Moral equivalent of strsignal(3) -- identical to errno-name, above.
 */
IDIO_DEFINE_PRIMITIVE1_DS ("strerrno", libc_strerrno, (IDIO ierrnum), "errnum", "\
return the error name of ``errnum``				\n\
								\n\
:param errnum: error number					\n\
:type errnum: C_int						\n\
:return: error name						\n\
:rtype: string							\n\
								\n\
Identical to ``errno-name``.					\n\
")
{
    IDIO_ASSERT (ierrnum);

    /*
     * Test Case: libc-wrap-errors/strerrno-bad-type.idio
     *
     * strerrno #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, ierrnum);

    return idio_string_C (idio_libc_errno_name (IDIO_C_TYPE_INT (ierrnum)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("errno/get", libc_errno_get, (void), "", "\
return ``errno``						\n\
								\n\
:return: errno							\n\
:rtype: C_int							\n\
								\n\
Identical to ``errno-name``.					\n\
")
{
    return idio_C_int (errno);
}

/*
 * How many rlimits are there?
 *
 * RLIMIT_NLIMITS
 */

#define IDIO_LIBC_FRLIMIT 0
#define IDIO_LIBC_NRLIMIT RLIMIT_NLIMITS

/*
 * How many chars in RLIMIT_-somename- ?
 *
 * Empirical study suggests RLIMIT_SIGPENDING is the longest
 * RLIMIT_-name at 17 chars.
 */
#define IDIO_LIBC_RLIMITNAMELEN 20

static void idio_libc_set_rlimit_names ()
{
    idio_libc_rlimit_names = idio_alloc ((IDIO_LIBC_NRLIMIT + 1) * sizeof (char *));

    int i;
    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT; i++) {
	idio_libc_rlimit_names[i] = idio_alloc (IDIO_LIBC_RLIMITNAMELEN);
	*(idio_libc_rlimit_names[i]) = '\0';
    }
    idio_libc_rlimit_names[i] = NULL;

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_CPU)
    IDIO_LIBC_RLIMIT (RLIMIT_CPU);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_FSIZE)
    IDIO_LIBC_RLIMIT (RLIMIT_FSIZE);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_DATA)
    IDIO_LIBC_RLIMIT (RLIMIT_DATA);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_STACK)
    IDIO_LIBC_RLIMIT (RLIMIT_STACK);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_CORE)
    IDIO_LIBC_RLIMIT (RLIMIT_CORE);
#endif

    /* Linux, FreeBSD */
#if defined (RLIMIT_RSS)
    IDIO_LIBC_RLIMIT (RLIMIT_RSS);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_NOFILE)
    IDIO_LIBC_RLIMIT (RLIMIT_NOFILE);
#endif

    /* Solaris, FreeBSD */
#if defined (RLIMIT_VMEM)
    IDIO_LIBC_RLIMIT (RLIMIT_VMEM);
#endif

    /* Linux, Solaris, FreeBSD */
#if defined (RLIMIT_AS)
    IDIO_LIBC_RLIMIT (RLIMIT_AS);
#endif

    /* Linux, FreeBSD */
#if defined (RLIMIT_NPROC)
    IDIO_LIBC_RLIMIT (RLIMIT_NPROC);
#endif

    /* Linux, FreeBSD */
#if defined (RLIMIT_MEMLOCK)
    IDIO_LIBC_RLIMIT (RLIMIT_MEMLOCK);
#endif

    /* Linux */
#if defined (RLIMIT_LOCKS)
    IDIO_LIBC_RLIMIT (RLIMIT_LOCKS);
#endif

    /* Linux */
#if defined (RLIMIT_SIGPENDING)
    IDIO_LIBC_RLIMIT (RLIMIT_SIGPENDING);
#endif

    /* Linux */
#if defined (RLIMIT_MSGQUEUE)
    IDIO_LIBC_RLIMIT (RLIMIT_MSGQUEUE);
#endif

    /* Linux */
#if defined (RLIMIT_NICE)
    IDIO_LIBC_RLIMIT (RLIMIT_NICE);
#endif

    /* Linux */
#if defined (RLIMIT_RTPRIO)
    IDIO_LIBC_RLIMIT (RLIMIT_RTPRIO);
#endif

    /* Linux */
#if defined (RLIMIT_RTTIME)
    IDIO_LIBC_RLIMIT (RLIMIT_RTTIME);
#endif

    /* FreeBSD */
#if defined (RLIMIT_SBSIZE)
    IDIO_LIBC_RLIMIT (RLIMIT_SBSIZE);
#endif

    /* FreeBSD */
#if defined (RLIMIT_NPTS)
    IDIO_LIBC_RLIMIT (RLIMIT_NPTS);
#endif

    /* FreeBSD */
#if defined (RLIMIT_SWAP)
    IDIO_LIBC_RLIMIT (RLIMIT_SWAP);
#endif

#if IDIO_DEBUG
    int first = 1;
    for (i = IDIO_LIBC_FRLIMIT ; i < IDIO_LIBC_NRLIMIT ; i++) {
	if ('\0' == *(idio_libc_rlimit_names[i])) {
	    char err_name[IDIO_LIBC_RLIMITNAMELEN + 2];
	    sprintf (err_name, "RLIMIT_UNKNOWN%d", i);
	    IDIO rlimit_sym = idio_symbols_C_intern (err_name);
	    idio_libc_export_symbol_value (rlimit_sym, idio_C_int (i));
	    sprintf (idio_libc_rlimit_names[i], "%s", err_name);
	    if (first) {
		first = 0;
		fprintf (stderr, "Unmapped rlimit numbers:\n");
		fprintf (stderr, " %3.3s %-*.*s %s\n", "id", IDIO_LIBC_RLIMITNAMELEN, IDIO_LIBC_RLIMITNAMELEN, "Idio name", "strerror ()");
	    }
	    fprintf (stderr, " %3d %-*s %s\n", i, IDIO_LIBC_RLIMITNAMELEN, err_name, strerror (i));
	}
    }
    if (0 == first) {
	fprintf (stderr, "\n");
    }
#endif
}

char *idio_libc_rlimit_name (int rlim)
{
    if (rlim < IDIO_LIBC_FRLIMIT ||
	rlim > IDIO_LIBC_NRLIMIT) {
	/*
	 * Test Case:  libc-wrap-errors/rlimit-name-bad-rlim.idio
	 *
	 * rlimit-name (C/integer-> -1)
	 */
	idio_error_param_value ("rlim", "should be an 0 <= int < RLIM_NLIMITS", IDIO_C_FUNC_LOCATION ());

	return NULL;
    }

    return idio_libc_rlimit_names[rlim];
}

IDIO_DEFINE_PRIMITIVE1_DS ("rlimit-name", libc_rlimit_name, (IDIO irlim), "irlim", "\
return the string name of the getrlimit(2)      \n\
C macro						\n\
						\n\
:param irlim: the C-int value of the macro	\n\
						\n\
:return: a string				\n\
						\n\
:raises: ^rt-parameter-type-error		\n\
")
{
    IDIO_ASSERT (irlim);

    /*
     * Test Case: libc-wrap-errors/rlimit-name-bad-type.idio
     *
     * rlimit-name #t
     */
    IDIO_USER_TYPE_ASSERT (C_int, irlim);

    return idio_string_C (idio_libc_rlimit_name (IDIO_C_TYPE_INT (irlim)));
}

IDIO_DEFINE_PRIMITIVE0_DS ("rlimit-names", libc_rlimit_names, (), "", "\
return a list of pairs of the getrlimit(2)      \n\
C macros					\n\
						\n\
each pair is the C value and string name	\n\
of the macro					\n\
						\n\
:return: a list of pairs			\n\
")
{
    IDIO r = idio_S_nil;

    int i;
    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT ; i++) {
	r = idio_pair (idio_pair (idio_C_int (i), idio_string_C (idio_libc_rlimit_name (i))), r);
    }

    return idio_list_reverse (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("EGID/get", EGID_get, (void), "", "\
getter for the computed value ``EGID`` which is a call to	\n\
getegid (2).							\n\
								\n\
:return: effective group ID					\n\
:rtype: C-int							\n\
")
{
    /*
     * XXX
     *
     * gid_t not C-int!
     */
    return idio_C_int (getegid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("EGID/set", EGID_set, (IDIO iegid), "egid", "\
setter for the computed value ``EGID`` which is a call to	\n\
setegid (2).							\n\
								\n\
:param egid: effective group ID					\n\
:type egid: fixnum or C-int					\n\
:return: 0 or raises ^system-error				\n\
:rtype: fixnum							\n\
")
{
    IDIO_ASSERT (iegid);

    gid_t egid = -1;

    /*
     * XXX
     *
     * gid_t not fixnum/C-int!
     */
    if (idio_isa_fixnum (iegid)) {
	egid = IDIO_FIXNUM_VAL (iegid);
    } else if (idio_isa_C_int (iegid)) {
	egid = IDIO_C_TYPE_INT (iegid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/EGID-set-bad-type.idio
	 *
	 * EGID = #t
	 */
	idio_error_param_type ("integer|C_int", iegid, IDIO_C_LOCATION ("EGID/set"));

	return idio_S_notreached;
    }

    int r = setegid (egid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/EGID-set-invalid-gid.idio
	 *
	 * ;; probably a fail...
	 * EGID = -1
	 */
	idio_error_system_errno ("setegid", iegid, IDIO_C_LOCATION ("EGID/set"));

	return idio_S_notreached;
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("EUID/get", EUID_get, (void), "", "\
getter for the computed value ``EUID`` which is a call to	\n\
geteuid (2).							\n\
								\n\
:return: effective user ID					\n\
:rtype: C-int							\n\
")
{
    /*
     * XXX
     *
     * uid_t not C-int!
     */
    return idio_C_int (geteuid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("EUID/set", EUID_set, (IDIO ieuid), "euid", "\
setter for the computed value ``EUID`` which is a call to	\n\
seteuid (2).							\n\
								\n\
:param euid: effective user ID					\n\
:type euid: fixnum or C-int					\n\
:return: 0 or raises ^system-error				\n\
:rtype: fixnum							\n\
")
{
    IDIO_ASSERT (ieuid);

    uid_t euid = -1;

    /*
     * XXX
     *
     * uid_t not fixnum/C-int!
     */
    if (idio_isa_fixnum (ieuid)) {
	euid = IDIO_FIXNUM_VAL (ieuid);
    } else if (idio_isa_C_int (ieuid)) {
	euid = IDIO_C_TYPE_INT (ieuid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/EUID-set-bad-type.idio
	 *
	 * EUID = #t
	 */
	idio_error_param_type ("integer|C_int", ieuid, IDIO_C_LOCATION ("EUID/set"));

	return idio_S_notreached;
    }

    int r = seteuid (euid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/EUID-set-invalid-gid.idio
	 *
	 * ;; probably a fail...
	 * EUID = -1
	 */
	idio_error_system_errno ("seteuid", ieuid, IDIO_C_LOCATION ("EUID/set"));

	return idio_S_notreached;
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("GID/get", GID_get, (void), "", "\
getter for the computed value ``GID`` which is a call to	\n\
getgid (2).							\n\
								\n\
:return: real group ID						\n\
:rtype: C-int							\n\
")
{
    /*
     * XXX
     *
     * gid_t not C-int!
     */
    return idio_C_int (getgid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("GID/set", GID_set, (IDIO igid), "gid", "\
setter for the computed value ``GID`` which is a call to	\n\
setgid (2).							\n\
								\n\
:param gid: real group ID					\n\
:type gid: fixnum or C-int					\n\
:return: 0 or raises ^system-error				\n\
:rtype: fixnum							\n\
")
{
    IDIO_ASSERT (igid);

    gid_t gid = -1;

    /*
     * XXX
     *
     * gid_t not fixnum/C-int!
     */
    if (idio_isa_fixnum (igid)) {
	gid = IDIO_FIXNUM_VAL (igid);
    } else if (idio_isa_C_int (igid)) {
	gid = IDIO_C_TYPE_INT (igid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/GID-set-bad-type.idio
	 *
	 * GID = #t
	 */
	idio_error_param_type ("integer|C_int", igid, IDIO_C_LOCATION ("GID/set"));

	return idio_S_notreached;
    }

    int r = setgid (gid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/GID-set-invalid-gid.idio
	 *
	 * ;; probably a fail...
	 * GID = -1
	 */
	idio_error_system_errno ("setgid", igid, IDIO_C_LOCATION ("GID/set"));

	return idio_S_notreached;
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("UID/get", UID_get, (void), "", "\
getter for the computed value ``UID`` which is a call to	\n\
getuid (2).							\n\
								\n\
:return: real user ID						\n\
:rtype: C-int							\n\
")
{
    /*
     * XXX
     *
     * uid_t not C-int!
     */
    return idio_C_int (getuid ());
}

IDIO_DEFINE_PRIMITIVE1_DS ("UID/set", UID_set, (IDIO iuid), "uid", "\
setter for the computed value ``UID`` which is a call to	\n\
setuid (2).							\n\
								\n\
:param uid: real user ID					\n\
:type uid: fixnum or C-int					\n\
:return: 0 or raises ^system-error				\n\
:rtype: fixnum							\n\
")
{
    IDIO_ASSERT (iuid);

    uid_t uid = -1;

    /*
     * XXX
     *
     * uid_t not fixnum/C-int!
     */
    if (idio_isa_fixnum (iuid)) {
	uid = IDIO_FIXNUM_VAL (iuid);
    } else if (idio_isa_C_int (iuid)) {
	uid = IDIO_C_TYPE_INT (iuid);
    } else {
	/*
	 * Test Case: libc-wrap-errors/UID-set-bad-type.idio
	 *
	 * UID = #t
	 */
	idio_error_param_type ("integer|C_int", iuid, IDIO_C_LOCATION ("UID/set"));

	return idio_S_notreached;
    }

    int r = setuid (uid);

    if (-1 == r) {
	/*
	 * Test Case: libc-wrap-errors/UID-set-invalid-uid.idio
	 *
	 * ;; probably a fail...
	 * UID = -1
	 */
	idio_error_system_errno ("setuid", iuid, IDIO_C_LOCATION ("UID/set"));

	return idio_S_notreached;
    }

    return idio_fixnum (r);
}

IDIO_DEFINE_PRIMITIVE0_DS ("STDIN/get", libc_STDIN_get, (void), "", "\
return the current input handle					\n\
								\n\
:return: current input handle					\n\
:rtype: handle							\n\
")
{
    return idio_thread_current_input_handle ();
}

IDIO_DEFINE_PRIMITIVE0_DS ("STDOUT/get", libc_STDOUT_get, (void), "", "\
return the current output handle				\n\
								\n\
:return: current output handle					\n\
:rtype: handle							\n\
")
{
    return idio_thread_current_output_handle ();
}

IDIO_DEFINE_PRIMITIVE0_DS ("STDERR/get", libc_STDERR_get, (void), "", "\
return the current error handle					\n\
								\n\
:return: current error handle					\n\
:rtype: handle							\n\
")
{
    return idio_thread_current_error_handle ();
}

void idio_libc_wrap_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_struct_rusage_ru_utime);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_struct_rusage_ru_stime);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_struct_timeval_as_string);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_add_struct_timeval);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_subtract_struct_timeval);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_system_error);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_access);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_chdir);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_close);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_dup);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_dup2);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_exit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fcntl);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fileno);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_fork);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getcwd);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getpid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getrlimit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_getrusage);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_gettimeofday);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_isatty);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_kill);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_mkdir);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_mkdtemp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_mkstemp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe_reader);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_pipe_writer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_read);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_rmdir);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_setpgid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_setrlimit);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_handler);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sleep);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_stat);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strerror);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strsignal);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcgetattr);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcgetpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcsetattr);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_tcsetpgrp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_times);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_uname);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_unlink);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_waitpid);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WEXITSTATUS);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFEXITED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFSIGNALED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WIFSTOPPED);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_WTERMSIG);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_write);

    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sig_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_sig_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_signal_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_names);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_strerrno);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_rlimit_name);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_rlimit_names);
}

void idio_final_libc_wrap ()
{
    int i;

    for (i = IDIO_LIBC_FSIG; NULL != idio_libc_signal_names[i]; i++) {
        IDIO_GC_FREE (idio_libc_signal_names[i]);
    }
    IDIO_GC_FREE (idio_libc_signal_names);

    for (i = IDIO_LIBC_FERRNO; i < IDIO_LIBC_NERRNO; i++) {
        IDIO_GC_FREE (idio_libc_errno_names[i]);
    }
    IDIO_GC_FREE (idio_libc_errno_names);

    for (i = IDIO_LIBC_FRLIMIT; i < IDIO_LIBC_NRLIMIT; i++) {
        IDIO_GC_FREE (idio_libc_rlimit_names[i]);
    }
    IDIO_GC_FREE (idio_libc_rlimit_names);
}

void idio_init_libc_wrap ()
{
    idio_module_table_register (idio_libc_wrap_add_primitives, idio_final_libc_wrap);

    idio_libc_wrap_module = idio_module (idio_symbols_C_intern ("libc"));
    IDIO_MODULE_IMPORTS (idio_libc_wrap_module) = IDIO_LIST2 (IDIO_LIST1 (idio_Idio_module),
							      IDIO_LIST1 (idio_primitives_module));

    idio_module_export_symbol_value (idio_symbols_C_intern ("0U"), idio_C_uint (0U), idio_libc_wrap_module);

    /* fcntl.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("FD_CLOEXEC"), idio_C_int (FD_CLOEXEC), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD"), idio_C_int (F_DUPFD), idio_libc_wrap_module);
#if defined (F_DUPFD_CLOEXEC)
    idio_add_feature (idio_symbols_C_intern ("F_DUPFD_CLOEXEC"));
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_DUPFD_CLOEXEC"), idio_C_int (F_DUPFD_CLOEXEC), idio_libc_wrap_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_GETFD"), idio_C_int (F_GETFD), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_SETFD"), idio_C_int (F_SETFD), idio_libc_wrap_module);

    /* signal.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_DFL"), idio_C_pointer (SIG_DFL), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("SIG_IGN"), idio_C_pointer (SIG_IGN), idio_libc_wrap_module);

    /* stdio.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("BUFSIZ"), idio_C_int (BUFSIZ), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("EOF"), idio_C_int (EOF), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("NULL"), idio_C_pointer (NULL), idio_libc_wrap_module);

    /* stdint.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MAX"), idio_C_int (INTMAX_MAX), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("INTMAX_MIN"), idio_C_int (INTMAX_MIN), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("UINTMAX_MAX"), idio_C_uint (UINTMAX_MAX), idio_libc_wrap_module);

    /* sys/resource.h */
    /*
     * NB RLIM_SAVED_* not defined in FreeBSD (10)
     */
#ifdef RLIM_SAVED_MAX
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_MAX"), idio_C_int (RLIM_SAVED_MAX), idio_libc_wrap_module);
#endif
#ifdef RLIM_SAVED_CUR
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_SAVED_CUR"), idio_C_int (RLIM_SAVED_CUR), idio_libc_wrap_module);
#endif
    idio_module_export_symbol_value (idio_symbols_C_intern ("RLIM_INFINITY"), idio_C_int (RLIM_INFINITY), idio_libc_wrap_module);

    /* sys/wait.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("WAIT_ANY"), idio_C_int (WAIT_ANY), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WNOHANG"), idio_C_int (WNOHANG), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("WUNTRACED"), idio_C_int (WUNTRACED), idio_libc_wrap_module);

    /* termios.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSADRAIN"), idio_C_int (TCSADRAIN), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("TCSAFLUSH"), idio_C_int (TCSAFLUSH), idio_libc_wrap_module);

    /* unistd.h */
    idio_module_export_symbol_value (idio_symbols_C_intern ("PATH_MAX"), idio_C_int (PATH_MAX), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDIN_FILENO"), idio_C_int (STDIN_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDOUT_FILENO"), idio_C_int (STDOUT_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("STDERR_FILENO"), idio_C_int (STDERR_FILENO), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("R_OK"), idio_C_int (R_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("W_OK"), idio_C_int (W_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("X_OK"), idio_C_int (X_OK), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("F_OK"), idio_C_int (F_OK), idio_libc_wrap_module);

    IDIO geti;
    IDIO seti;
    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_errno_get);
    idio_module_export_computed_symbol (idio_symbols_C_intern ("errno"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDIN_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDIN"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDOUT_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDOUT"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, libc_STDERR_get);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("STDERR"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_S_nil, idio_libc_wrap_module);

    IDIO name;
    name = idio_symbols_C_intern ("struct-sigaction");
    idio_libc_struct_sigaction = idio_struct_type (name,
						   idio_S_nil,
						   idio_pair (idio_symbols_C_intern ("sa_handler"),
						   idio_pair (idio_symbols_C_intern ("sa_sigaction"),
						   idio_pair (idio_symbols_C_intern ("sa_mask"),
						   idio_pair (idio_symbols_C_intern ("sa_flags"),
						   idio_S_nil)))));
    idio_module_export_symbol_value (name, idio_libc_struct_sigaction, idio_libc_wrap_module);

    name = idio_symbols_C_intern ("struct-utsname");
    idio_libc_struct_utsname = idio_struct_type (name,
						 idio_S_nil,
						 idio_pair (idio_symbols_C_intern ("sysname"),
						 idio_pair (idio_symbols_C_intern ("nodename"),
						 idio_pair (idio_symbols_C_intern ("release"),
						 idio_pair (idio_symbols_C_intern ("version"),
						 idio_pair (idio_symbols_C_intern ("machine"),
						 idio_S_nil))))));
    idio_module_export_symbol_value (name, idio_libc_struct_utsname, idio_libc_wrap_module);

    {
	char *field_names[] = { "sb_dev", "sb_ino", "sb_mode", "sb_nlink", "sb_uid", "sb_gid", "sb_rdev", "sb_size", "sb_blksize", "sb_blocks", "sb_atime", "sb_mtime", "sb_ctime", NULL };
	IDIO_DEFINE_MODULE_STRUCTn (idio_libc_wrap_module, idio_libc_struct_stat, "struct-stat", idio_S_nil);
    }

    name = idio_symbols_C_intern ("idio-uname");
    /*
     * XXX Here we actually invoke idio_libc_uname() to return a
     * struct instance of a struct type utsname
     */
    idio_module_export_symbol_value (name, idio_libc_uname (), idio_libc_wrap_module);

    name = idio_symbols_C_intern ("struct-rlimit");
    idio_libc_struct_rlimit = idio_struct_type (name,
						idio_S_nil,
						idio_pair (idio_symbols_C_intern ("rlim_cur"),
						idio_pair (idio_symbols_C_intern ("rlim_max"),
						idio_S_nil)));
    idio_module_export_symbol_value (name, idio_libc_struct_rlimit, idio_libc_wrap_module);

    idio_vm_signal_handler_conditions = idio_array (IDIO_LIBC_NSIG + 1);
    idio_gc_protect_auto (idio_vm_signal_handler_conditions);
    /*
     * idio_vm_run1() will be indexing anywhere into this array when
     * it gets a signal so make sure that the "used" size is up there
     * by poking something at at NSIG.
     */
    idio_array_insert_index (idio_vm_signal_handler_conditions, idio_S_nil, (idio_ai_t) IDIO_LIBC_NSIG);
    idio_libc_set_signal_names ();

    idio_vm_errno_conditions = idio_array (IDIO_LIBC_NERRNO + 1);
    idio_gc_protect_auto (idio_vm_errno_conditions);
    idio_array_insert_index (idio_vm_errno_conditions, idio_S_nil, (idio_ai_t) IDIO_LIBC_NERRNO);
    idio_libc_set_errno_names ();

    idio_libc_set_rlimit_names ();

    /*
     * Define some host/user/process variables
     */
    IDIO main_module = idio_Idio_module_instance ();

    struct utsname u;
    if (uname (&u) == -1) {
	/*
	 * Test Case: ??
	 *
	 * EFAULT buf is not valid.
	 */
	idio_error_system_errno ("uname", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (getenv ("HOSTNAME") == NULL) {
	idio_module_set_symbol_value (idio_symbols_C_intern ("HOSTNAME"), idio_string_C (u.nodename), main_module);
    }

    idio_add_feature_ps ("uname/sysname/", u.sysname);
    idio_add_feature_ps ("uname/nodename/", u.nodename);
    idio_add_feature_ps ("uname/release/", u.release);
    /* idio_add_feature (idio_string_C (u.version)); */
    idio_add_feature_ps ("uname/machine/", u.machine);
    idio_add_feature_pi ("sizeof/pointer/", sizeof (void *) * CHAR_BIT);

    /*
     * From getpwuid(3) on CentOS
     */

    /*
    struct passwd pwd;
    struct passwd *pwd_result;
    char *pwd_buf;
    size_t pwd_bufsize;
    int pwd_s;

    pwd_bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
    if (pwd_bufsize == -1)
	pwd_bufsize = 16384;

    pwd_buf = idio_alloc (pwd_bufsize);

    int pwd_exists = 1;
    pwd_s = getpwuid_r (getuid (), &pwd, pwd_buf, pwd_bufsize, &pwd_result);
    if (pwd_result == NULL) {
	if (pwd_s) {
	    errno = pwd_s;
	    idio_error_system_errno ("getpwnam_r", idio_integer (getuid ()), IDIO_C_FUNC_LOCATION ());
	}
	pwd_exists = 0;
    }

    IDIO blank = idio_string_C ("");
    IDIO HOME = blank;
    IDIO SHELL = blank;
    if (pwd_exists) {
	HOME = idio_string_C (pwd.pw_dir);
	SHELL = idio_string_C (pwd.pw_shell);
    }

    if (getenv ("HOME") == NULL) {
	IDIO name = idio_symbols_C_intern ("HOME");
	idio_toplevel_extend (name, IDIO_MEANING_ENVIRON_SCOPE (0));
	idio_module_export_symbol_value (name, HOME, idio_libc_wrap_module);
    }

    if (getenv ("SHELL") == NULL) {
	IDIO name = idio_symbols_C_intern ("SHELL");
	idio_toplevel_extend (name, IDIO_MEANING_ENVIRON_SCOPE (0));
	idio_module_export_symbol_value (name, SHELL, idio_libc_wrap_module);
    }

    IDIO_GC_FREE (pwd_buf);
    */

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, UID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, UID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("UID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EUID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EUID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EUID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, GID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, GID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("GID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    geti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EGID_get);
    seti = IDIO_ADD_MODULE_PRIMITIVE (idio_libc_wrap_module, EGID_set);
    idio_module_add_computed_symbol (idio_symbols_C_intern ("EGID"), idio_vm_values_ref (IDIO_FIXNUM_VAL (geti)), idio_vm_values_ref (IDIO_FIXNUM_VAL (seti)), main_module);

    int ngroups = getgroups (0, (gid_t *) NULL);

    if (-1 == ngroups) {
	/*
	 * Test Case: ??
	 *
	 * Can getgroups(2) fail with 0/NULL args?
	 */
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    gid_t *grp_list = idio_alloc (ngroups * sizeof (gid_t));

    int ng = getgroups (ngroups, grp_list);
    if (-1 == ng) {
	/*
	 * Test Case: ??
	 *
	 * Can getgroups(2) fail with 0/NULL args?
	 */
	idio_error_system_errno ("getgroups", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (ngroups != ng) {
	/*
	 * Test Case: ??
	 *
	 * Could this ever happen?
	 */
	idio_error_C ("getgroups", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO GROUPS = idio_array (ngroups);

    for (ng = 0; ng < ngroups ; ng++) {
	idio_array_insert_index (GROUPS, idio_integer (grp_list[ng]), ng);
    }
    IDIO_GC_FREE (grp_list);

    idio_module_set_symbol_value (idio_symbols_C_intern ("GROUPS"), GROUPS, main_module);

    idio_module_set_symbol_value (idio_symbols_C_intern ("PID"), idio_integer (getpid ()), main_module);
    idio_module_set_symbol_value (idio_symbols_C_intern ("PPID"), idio_integer (getppid ()), main_module);

    /*
     * POSIX times(3) and struct tms
     */
    idio_SC_CLK_TCK = sysconf (_SC_CLK_TCK);
    if (-1 == idio_SC_CLK_TCK){
	/*
	 * Test Case: ??
	 *
	 * _SC_CLK_TCK is wrong?
	 */
	idio_error_system_errno ("sysconf (_SC_CLK_TCK)", idio_integer (idio_SC_CLK_TCK), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
    /*
     * We can use an idio_integer () for CLK_TCK as it is a usage-only
     * value -- ie we use it in Idio but don't pass it back to C
     */
    idio_module_export_symbol_value (idio_symbols_C_intern ("CLK_TCK"), idio_integer (idio_SC_CLK_TCK), idio_libc_wrap_module);
    name = idio_symbols_C_intern ("struct-tms");
    idio_libc_struct_tms = idio_struct_type (name,
					     idio_S_nil,
					     idio_pair (idio_symbols_C_intern ("tms_rtime"),
					     idio_pair (idio_symbols_C_intern ("tms_utime"),
					     idio_pair (idio_symbols_C_intern ("tms_stime"),
					     idio_pair (idio_symbols_C_intern ("tms_cutime"),
					     idio_pair (idio_symbols_C_intern ("tms_cstime"),
					     idio_S_nil))))));
    idio_module_export_symbol_value (name, idio_libc_struct_tms, idio_libc_wrap_module);

    idio_module_export_symbol_value (idio_symbols_C_intern ("RUSAGE_SELF"), idio_C_int (RUSAGE_SELF), idio_libc_wrap_module);
    idio_module_export_symbol_value (idio_symbols_C_intern ("RUSAGE_CHILDREN"), idio_C_int (RUSAGE_CHILDREN), idio_libc_wrap_module);
    /*
     * It's not clear how portable the RUSAGE_THREAD parameter is.  In
     * Linux it requires the _GNU_SOURCE feature test macro.  In
     * FreeBSD 10 you might need to define it yourself (as 1).  It is
     * not in OpenIndiana.
     */
}

