/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * sqlite3-module.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <assert.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <time.h>

#include <sqlite3.h>

#include "gc.h"

#include "idio-config.h"

#include "bignum.h"
#include "c-type.h"
#include "closure.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "idio.h"
#include "keyword.h"
#include "libc-wrap.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "sqlite3-module.h"
#include "sqlite3-system.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"

#include "libc-api.h"

IDIO idio_sqlite3_module;

IDIO idio_condition_rt_sqlite3_error_type;

IDIO idio_sqlite3_db_type;
IDIO idio_sqlite3_stmt_map;

IDIO_C_STRUCT_IDENT_DECL (sqlite3_db);
IDIO_C_STRUCT_IDENT_DECL (sqlite3_stmt);

/*
 * XXX These are not in src/symbol.[ch] at the time of writing...
 */
IDIO_SYMBOL_DECL (blob);
IDIO_SYMBOL_DECL (double);
IDIO_SYMBOL_DECL (int);
IDIO_SYMBOL_DECL (null);
IDIO_SYMBOL_DECL (text);

IDIO idio_sqlite3_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == idio_vasprintf (&s, format, argp)) {
	idio_error_alloc ("asprintf");
    }

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C (s, sh);

    /*
     * idio_vasprintf will not have called idio_gc_alloc to no stats
     * decrement
     */
    idio_free (s);

    return idio_get_output_string (sh);
}

void idio_sqlite3_error_printf (sqlite3 *db, IDIO detail, IDIO c_location, char *format, ...)
{
    IDIO_ASSERT (detail);
    IDIO_ASSERT (c_location);
    assert (format);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO msg2 = idio_sqlite3_error_string (format, fmt_args);
    va_end (fmt_args);

    idio_display (msg2, msh);

    if (NULL != db) {
	idio_display_C (": ", msh);
	idio_display_C (sqlite3_errmsg (db), msh);
    }

    if (idio_S_nil != detail) {
	idio_display (detail, dsh);
    }

    IDIO c = idio_struct_instance (idio_condition_rt_sqlite3_error_type,
				   IDIO_LIST3 (idio_get_output_string (msh),
					       idio_get_output_string (lsh),
					       idio_get_output_string (dsh)));

    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

/*
 * The validate code is repeated a lot...
 */
sqlite3 *idio_sqlite3_validate_db (char *func, IDIO db, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (db);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    /*
     * Test Case: sqlite3-errors/{func}-bad-db-type.idio
     *
     * {func} #t ...
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, db);
    if (idio_CSI_sqlite3_db != IDIO_C_TYPE_POINTER_PTYPE (db)) {
	/*
	 * Test Case: sqlite3-errors/{func}-invalid-db-type.idio
	 *
	 * {func} libc/NULL ...
	 */
	idio_error_param_value_exp (func, "db", db, "sqlite3/db", c_location);

	/* notreached */
	return NULL;
    }

    return IDIO_C_TYPE_POINTER_P (db);
}

sqlite3_stmt *idio_sqlite3_validate_stmt (char *func, IDIO stmt, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, c_location);

    /*
     * Test Case: sqlite3-errors/{func}-bad-stmt-type.idio
     *
     * {func} #t ...
     */
    IDIO_USER_C_TYPE_ASSERT (pointer, stmt);
    if (idio_CSI_sqlite3_stmt != IDIO_C_TYPE_POINTER_PTYPE (stmt)) {
	/*
	 * Test Case: sqlite3-errors/{func}-invalid-stmt-type.idio
	 *
	 * {func} libc/NULL ...
	 */
	idio_error_param_value_exp (func, "stmt", stmt, "sqlite3/stmt", c_location);

	/* notreached */
	return NULL;
    }

    return IDIO_C_TYPE_POINTER_P (stmt);
}

IDIO_DEFINE_PRIMITIVE0_DS ("sqlite3-version", sqlite3_version, (), "", "\
Return the sqlite3 version	\n\
				\n\
:return: sqlite3 version	\n\
:rtype: string			\n\
")
{
    return idio_string_C (sqlite3_libversion ());
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%sqlite3-open", sqlite3_open, (IDIO name, IDIO args), "name [flags]", "\
Return the sqlite3 database connection for `name`	\n\
				\n\
:param name: database name	\n\
:type name: string		\n\
:param flags: database open flags	\n\
:type flags: C/int		\n\
:return: database connection	\n\
:rtype: C/pointer to a :ref:`sqlite3/db <sqlite3/sqlite3/db>`	\n\
:raises ^rt-libc-format-error: if `name` contains an ASCII NUL	\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (name);

    /*
     * Test Case: sqlite3-errors/sqlite3-open-bad-name-type.idio
     *
     * sqlite3-open #t
     */
    IDIO_USER_TYPE_ASSERT (string, name);

    char *C_name;
    size_t free_C_name = 0;

    /*
     * Test Case: sqlite3-errors/sqlite3-open-bad-name-format.idio
     *
     * sqlite3-open "hello\x0world"
     */
    C_name = idio_libc_string_C (name, "name", &free_C_name, IDIO_C_FUNC_LOCATION ());

    /*
     * default flags for sqlite3_open()
     */
#if defined (IDIO_NO_SQLITE3_OPEN_V2)
    /* it won't be used in a call */
    int C_flags = 0;
#else
    int C_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
#endif
    if (idio_isa_pair (args)) {
	IDIO flags = IDIO_PAIR_H (args);
	/*
	 * Test Case: sqlite3-errors/sqlite3-open-bad-flags-type.idio
	 *
	 * sqlite3-open "" #t
	 */
	IDIO_USER_C_TYPE_ASSERT (int, flags);

	C_flags = IDIO_C_TYPE_int (flags);
    }

    sqlite3 *C_db;

#if defined (IDIO_NO_SQLITE3_OPEN_V2)
    int rc = sqlite3_open (C_name, &C_db);
#else
    int rc = sqlite3_open_v2 (C_name, &C_db, C_flags, NULL);
#endif

    if (free_C_name) {
	idio_free (C_name);
    }

    IDIO db = idio_C_pointer_type (idio_CSI_sqlite3_db, C_db);

    /*
     * The finalizer, sqlite3_close(), frees this memory
     */
    IDIO_C_TYPE_POINTER_FREEP (db) = 0;

    idio_gc_register_finalizer (db, idio_sqlite3_db_finalizer);

    if (SQLITE_OK != rc) {
	/*
	 * Test Case: sqlite3-errors/sqlite3-open-non-existent.idio
	 *
	 * tmp := (libc/make-tmp-file)
	 * rm tmp
	 * sqlite3-open tmp SQLITE_OPEN_READONLY
	 */
	idio_sqlite3_error_printf (C_db, name, IDIO_C_FUNC_LOCATION (), "%%sqlite3-open");

	return idio_S_notreached;
    }

    return db;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%sqlite3-close", sqlite3_close, (IDIO db), "db", "\
Close the sqlite3 database connection to `db`	\n\
				\n\
:param db: database connection	\n\
:type db: C/pointer to :ref:`sqlite3/db <sqlite3/sqlite3/db>`	\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (db);

    /*
     * Test Case: sqlite3-errors/sqlite3-close-bad-db-type.idio
     *
     * sqlite3-close #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-close-invalid-db-type.idio
     *
     * sqlite3-close libc/NULL
     */
    sqlite3 *C_db = idio_sqlite3_validate_db ("%sqlite3-close", db, IDIO_C_FUNC_LOCATION ());

    idio_gc_deregister_finalizer (db);

#if defined (IDIO_NO_SQLITE3_CLOSE_V2)
    int rc = sqlite3_close (C_db);
#else
    int rc = sqlite3_close_v2 (C_db);
#endif

    idio_invalidate_C_pointer (db);

    return idio_C_int (rc);
}

IDIO_DEFINE_PRIMITIVE1_DS ("%sqlite3-errmsg", sqlite3_errmsg, (IDIO db), "db", "\
Return a description of the current error in `db`	\n\
				\n\
:param db: database connection	\n\
:type db: C/pointer to :ref:`sqlite3/db <sqlite3/sqlite3/db>`	\n\
:return: ``#<unspec>``		\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (db);

    /*
     * Test Case: sqlite3-errors/sqlite3-errmsg-bad-db-type.idio
     *
     * sqlite3-errmsg #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-errmsg-invalid-db-type.idio
     *
     * sqlite3-errmsg libc/NULL
     */
    sqlite3 *C_db = idio_sqlite3_validate_db ("%sqlite3-errmsg", db, IDIO_C_FUNC_LOCATION ());

    return idio_string_C (sqlite3_errmsg (C_db));
}

IDIO_DEFINE_PRIMITIVE2_DS ("%sqlite3-prepare", sqlite3_prepare, (IDIO db, IDIO sql), "db sql", "\
compile `sql` for `db`		\n\
				\n\
:param db: database connection	\n\
:type db: C/point to a :ref:`sqlite3/db <sqlite3/sqlite3/db>`	\n\
:param sql: SQL statement text	\n\
:type sql: string		\n\
:return: database stmt		\n\
:rtype: C/pointer to a :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:raises ^rt-libc-format-error: if `sql` contains an ASCII NUL	\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (db);
    IDIO_ASSERT (sql);

    /*
     * Test Case: sqlite3-errors/sqlite3-prepare-bad-db-type.idio
     *
     * sqlite3-prepare #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-prepare-invalid-db-type.idio
     *
     * sqlite3-prepare libc/NULL #t
     */
    sqlite3 *C_db = idio_sqlite3_validate_db ("%sqlite3-prepare", db, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-prepare-bad-sql-type.idio
     *
     * sqlite3-prepare db #t
     */
    IDIO_USER_TYPE_ASSERT (string, sql);

    char *C_sql;
    size_t free_C_sql = 0;

    /*
     * Test Case: sqlite3-errors/sqlite3-prepare-bad-sql-format.idio
     *
     * sqlite3-prepare db "hello\x0world"
     */
    C_sql = idio_libc_string_C (sql, "sql", &free_C_sql, IDIO_C_FUNC_LOCATION ());

    sqlite3_stmt *C_stmt;

    int rc = sqlite3_prepare_v2 (C_db, C_sql, -1, &C_stmt, 0);

    if (free_C_sql) {
	idio_free (C_sql);
    }

    if (SQLITE_OK != rc) {
	/*
	 * Test Case: ??
	 */
	idio_sqlite3_error_printf (C_db, sql, IDIO_C_FUNC_LOCATION (), "sqlite3_prepare()");

	return idio_S_notreached;
    }

    IDIO stmt = idio_C_pointer_type (idio_CSI_sqlite3_stmt, C_stmt);

    /*
     * The finalizer, sqlite3_finalize(), frees this memory
     */
    IDIO_C_TYPE_POINTER_FREEP (stmt) = 0;

    idio_gc_register_finalizer (stmt, idio_sqlite3_stmt_finalizer);

    return stmt;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%sqlite3-finalize", sqlite3_finalize, (IDIO stmt), "stmt", "\
Finalize `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (stmt);

    /*
     * Test Case: sqlite3-errors/sqlite3-finalize-bad-stmt-type.idio
     *
     * sqlite3-finalize #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-finalize-invalid-stmt-type.idio
     *
     * sqlite3-finalize libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-finalize", stmt, IDIO_C_FUNC_LOCATION ());

    idio_gc_deregister_finalizer (stmt);

    int rc = sqlite3_finalize (C_stmt);

    idio_invalidate_C_pointer (stmt);

    return idio_C_int (rc);
}

IDIO idio_sqlite3_bind_blob (sqlite3_stmt *stmt, int i, IDIO v)
{
    IDIO_ASSERT (v);

    if (! (idio_isa_octet_string (v) ||
	   idio_isa_pathname (v))) {
	/*
	 * Test Case: sqlite3-errors/sqlite3-bind-blob-bad-v-type.idio
	 *
	 * sqlite3-bind-blob stmt 1 #t
	 */
	idio_error_param_type ("octet-string|pathname", v, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * idio_string_len() is blen for octet-string and pathname
     */
    size_t blen = idio_string_len (v);
    char *s;
    if (idio_isa (v, IDIO_TYPE_SUBSTRING)) {
	s = IDIO_SUBSTRING_S (v);
    } else {
	s = IDIO_STRING_S (v);
    }

    void *C_v = idio_alloc (blen);
    memcpy (C_v, s, blen);

#if defined (IDIO_NO_SQLITE3_BIND_BLOB64)
    int rc = sqlite3_bind_blob (stmt, i, C_v, blen, idio_free);
#else
    int rc = sqlite3_bind_blob64 (stmt, i, C_v, blen, idio_free);
#endif

    return idio_C_int (rc);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%sqlite3-bind-blob", sqlite3_bind_blob, (IDIO stmt, IDIO i, IDIO v), "stmt i v", "\
Replace parameter `i` with blob `v` in `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:param i: parameter index	\n\
:type i: fixnum			\n\
:param v: parameter value	\n\
:type v: octet-string		\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
				\n\
.. note::			\n\
				\n\
   On versions of :lname:`sqlite3` prior to v3.8.7			\n\
   ``sqlite3_bind_blob()`` will be called which will limit the valid	\n\
   size of `v` to a C/int.						\n\
")
{
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-blob-bad-stmt-type.idio
     *
     * sqlite3-bind-blob #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-bind-blob-invalid-stmt-type.idio
     *
     * sqlite3-bind-blob libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-bind-blob", stmt, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-blob-bad-i-type.idio
     *
     * sqlite3-bind-blob stmt #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, i);
    idio_ai_t C_i = IDIO_FIXNUM_VAL (i);

    /*
     * XXX no validation of {v}, here.  Leave that to the bind-{type}
     * routines.
     */

    return idio_sqlite3_bind_blob (C_stmt, C_i, v);
}

IDIO idio_sqlite3_bind_double (sqlite3_stmt *stmt, int i, IDIO v)
{
    IDIO_ASSERT (v);

    double C_v = 0;

    if (idio_isa_bignum (v)) {
	C_v = idio_bignum_double_value (v);
    } else if (idio_isa_C_double (v)) {
	C_v = IDIO_C_TYPE_double (v);
    } else {
	/*
	 * Test Case: zlib-errors/sqlite3-bind-double-bad-v-type.idio
	 *
	 * sqlite3-bind-double stmt 1 #t 'double
	 */
	idio_error_param_type ("C/double|bignum", v, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int rc = sqlite3_bind_double (stmt, i, C_v);

    return idio_C_int (rc);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%sqlite3-bind-double", sqlite3_bind_double, (IDIO stmt, IDIO i, IDIO v), "stmt i v", "\
Replace parameter `i` with double `v` in `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:param i: parameter index	\n\
:type i: fixnum			\n\
:param v: parameter value	\n\
:type v: C/double or bignum	\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-double-bad-stmt-type.idio
     *
     * sqlite3-bind-double #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-bind-double-invalid-stmt-type.idio
     *
     * sqlite3-bind-double libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-bind-double", stmt, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-double-bad-i-type.idio
     *
     * sqlite3-bind-double stmt #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, i);
    idio_ai_t C_i = IDIO_FIXNUM_VAL (i);

    /*
     * XXX no validation of {v}, here.  Leave that to the bind-{type}
     * routines.
     */

    return idio_sqlite3_bind_double (C_stmt, C_i, v);
}

IDIO idio_sqlite3_bind_int (sqlite3_stmt *stmt, int i, IDIO v)
{
    IDIO_ASSERT (v);

    int64_t C_v = 0;

    if (idio_isa_fixnum (v)) {
	C_v = IDIO_FIXNUM_VAL (v);
    } else if (idio_isa_bignum (v)) {
	    if (IDIO_BIGNUM_INTEGER_P (v)) {
		C_v = idio_bignum_ptrdiff_t_value (v);
	    } else {
		IDIO v_i = idio_bignum_real_to_integer (v);
		if (idio_S_nil == v_i) {
		    /*
		     * Test Case: sqlite3-errors/sqlite3-bind-int-v-float.idio
		     *
		     * sqlite3-bind-int stmt 1 1.1
		     */
		    idio_error_param_value_exp ("%sqlite3-bind-int", "v", v, "an integer bignum", IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		} else {
		    C_v = idio_bignum_int64_t_value (v_i);
		}
	    }
    } else if (idio_isa_C_int (v)) {
	C_v = IDIO_C_TYPE_int (v);
    } else if (idio_isa_libc_int64_t (v)) {
	C_v = IDIO_C_TYPE_libc_int64_t (v);
    } else {
	/*
	 * Test Case: zlib-errors/sqlite3-bind-int-bad-v-type.idio
	 *
	 * sqlite3-bind-int stmt 1 #t 'int
	 */
	idio_error_param_type ("C/int|libc/int64_t|fixnum|integer bignum", v, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int rc = sqlite3_bind_int64 (stmt, i, C_v);

    return idio_C_int (rc);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%sqlite3-bind-int", sqlite3_bind_int, (IDIO stmt, IDIO i, IDIO v), "stmt i v", "\
Replace parameter `i` with integer `v` in `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:param i: parameter index	\n\
:type i: fixnum			\n\
:param v: parameter value	\n\
:type v: C/int|libc/int64_t|fixnum|integer bignum	\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-int-bad-stmt-type.idio
     *
     * sqlite3-bind-int #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-bind-int-invalid-stmt-type.idio
     *
     * sqlite3-bind-int libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-bind-int", stmt, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-int-bad-i-type.idio
     *
     * sqlite3-bind-int stmt #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, i);
    idio_ai_t C_i = IDIO_FIXNUM_VAL (i);

    /*
     * XXX no validation of {v}, here.  Leave that to the bind-{type}
     * routines.
     */

    return idio_sqlite3_bind_int (C_stmt, C_i, v);
}

IDIO idio_sqlite3_bind_null (sqlite3_stmt *stmt, int i, IDIO v)
{
    IDIO_ASSERT (v);

    if (idio_S_nil != v) {
	/*
	 * Test Case: zlib-errors/sqlite3-bind-null-bad-v-type.idio
	 *
	 * sqlite3-bind-null stmt 1 #t 'null
	 */
	idio_error_param_type ("null", v, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int rc = sqlite3_bind_null (stmt, i);

    return idio_C_int (rc);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%sqlite3-bind-null", sqlite3_bind_null, (IDIO stmt, IDIO i, IDIO v), "stmt i v", "\
Replace parameter `i` with NULL in `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:param i: parameter index	\n\
:type i: fixnum			\n\
:param v: parameter value	\n\
:type v: ``#n``			\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-null-bad-stmt-type.idio
     *
     * sqlite3-bind-null #t #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-bind-null-invalid-stmt-type.idio
     *
     * sqlite3-bind-null libc/NULL #t #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-bind-null", stmt, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-null-bad-i-type.idio
     *
     * sqlite3-bind-null stmt #t #n
     */
    IDIO_USER_TYPE_ASSERT (fixnum, i);
    idio_ai_t C_i = IDIO_FIXNUM_VAL (i);

    /*
     * XXX no validation of {v}, here.  Leave that to the bind-{type}
     * routines.
     */

    return idio_sqlite3_bind_null (C_stmt, C_i, v);
}

IDIO idio_sqlite3_bind_text (sqlite3_stmt *stmt, int i, IDIO v)
{
    IDIO_ASSERT (v);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-text-bad-v-type.idio
     *
     * sqlite3-bind-text stmt 1 1.1
     */
    IDIO_USER_TYPE_ASSERT (string, v);

    char *C_v;
    size_t free_C_v = 0;

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-text-bad-v-format.idio
     *
     * sqlite3-bind-text stmt 1 "hello\x0world"
     */
    C_v = idio_libc_string_C (v, "v", &free_C_v, IDIO_C_FUNC_LOCATION ());

    /*
     * We may or may not have allocated memory for C_v which means we
     * can't readily pass a destructor (idio_free) to
     * sqlite3_bind_text64().  Hence the easy option which is to ask
     * sqlite to copy the data and we can make the usual decision to
     * free it or not.
     */
#if defined (IDIO_NO_SQLITE3_BIND_TEXT64)
    int rc = sqlite3_bind_text (stmt, i, C_v, free_C_v, SQLITE_TRANSIENT);
#else
    int rc = sqlite3_bind_text64 (stmt, i, C_v, free_C_v, SQLITE_TRANSIENT, SQLITE_UTF8);
#endif

    if (free_C_v) {
	idio_free (C_v);
    }

    return idio_C_int (rc);
}

IDIO_DEFINE_PRIMITIVE3_DS ("%sqlite3-bind-text", sqlite3_bind_text, (IDIO stmt, IDIO i, IDIO v), "stmt i v", "\
Replace parameter `i` with string `v` in `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:param i: parameter index	\n\
:type i: fixnum			\n\
:param v: parameter value	\n\
:type v: string			\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
				\n\
.. note::			\n\
				\n\
   On versions of :lname:`sqlite3` prior to v3.8.7			\n\
   ``sqlite3_bind_text()`` will be called which will limit the valid	\n\
   size of `v` to a C/int.						\n\
")
{
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (i);
    IDIO_ASSERT (v);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-text-bad-stmt-type.idio
     *
     * sqlite3-bind-text #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-bind-text-invalid-stmt-type.idio
     *
     * sqlite3-bind-text libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-bind-text", stmt, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-text-bad-i-type.idio
     *
     * sqlite3-bind-text stmt #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, i);
    idio_ai_t C_i = IDIO_FIXNUM_VAL (i);

    /*
     * XXX no validation of {v}, here.  Leave that to the bind-{type}
     * routines.
     */

    return idio_sqlite3_bind_text (C_stmt, C_i, v);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%sqlite3-bind", sqlite3_bind, (IDIO stmt, IDIO args), "stmt [idx val ...]", "\
Replace parameters in `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
				\n\
Arguments should be supplied in `idx` `val` tuples.	\n\
				\n\
`idx` can be an integer or a (named parameter) string.	\n\
				\n\
The sqlite3 *type* will be inferred from `val`'s :lname:`Idio` type.	\n\
				\n\
.. tip::			\n\
				\n\
   parameters are indexed from 1	\n\
				\n\
.. seealso::			\n\
				\n\
   :ref:`sqlite3-bind-blob <sqlite3/sqlite3-bind-blob>`	\n\
   :ref:`sqlite3-bind-double <sqlite3/sqlite3-bind-double>`	\n\
   :ref:`sqlite3-bind-int <sqlite3/sqlite3-bind-int>`	\n\
   :ref:`sqlite3-bind-null <sqlite3/sqlite3-bind-null>`	\n\
   :ref:`sqlite3-bind-text <sqlite3/sqlite3-bind-text>`	\n\
")
{
    IDIO_ASSERT (stmt);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-bad-stmt-type.idio
     *
     * sqlite3-bind #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-bind-invalid-stmt-type.idio
     *
     * sqlite3-bind libc/NULL
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-bind", stmt, IDIO_C_FUNC_LOCATION ());

    IDIO rc = idio_C_int (SQLITE_OK);
    while (idio_S_nil != args) {
	if (!(idio_isa_pair (args) &&
	      idio_isa_pair (IDIO_PAIR_T (args)))) {
	    /*
	     * Test Case: sqlite3-errors/sqlite3-bind-invalid-tuple.idio
	     *
	     * sqlite3-bind stmt #t
	     */
	    idio_error_param_value_exp ("%sqlite3-bind", "tuple", args, "idx val tuple", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	IDIO idx = IDIO_PAIR_H (args);
	IDIO val = IDIO_PAIR_HT (args);

	int C_idx = 0;

	if (idio_isa_fixnum (idx)) {
	    C_idx = IDIO_FIXNUM_VAL (idx);
	} else if (idio_isa_string (idx)) {
	    char *C_key;
	    size_t free_C_key = 0;

	    /*
	     * Test Case: sqlite3-errors/sqlite3-bind-bad-idx-format.idio
	     *
	     * sqlite3-bind stmt "hello\x0world" #t
	     */
	    C_key = idio_libc_string_C (idx, "idx", &free_C_key, IDIO_C_FUNC_LOCATION ());

	    C_idx = sqlite3_bind_parameter_index (C_stmt, C_key);

	    if (0 == C_idx) {
		idio_sqlite3_error_printf (NULL, idx, IDIO_C_FUNC_LOCATION (), "sqlite3_bind_parameter_index(): no matching parameter");

		return idio_S_notreached;
	    }
	} else {
	    /*
	     * Test Case: sqlite3-errors/sqlite3-bind-bad-idx-type.idio
	     *
	     * sqlite3-bind stmt 'foo #t
	     */
	    idio_error_param_type ("fixnum|string", idx, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * Reversing an Idio type into a sqlite3 type is a
	 * surprisingly grey task.  bignums can be integers, strings
	 * can be blobs.
	 */
	switch (idio_type (val)) {
	case IDIO_TYPE_FIXNUM:
	    rc = idio_sqlite3_bind_int (C_stmt, C_idx, val);
	    break;
	case IDIO_TYPE_BIGNUM:
	    if (IDIO_BIGNUM_INTEGER_P (val)) {
		rc = idio_sqlite3_bind_int (C_stmt, C_idx, val);
	    } else {
		IDIO val_i = idio_bignum_real_to_integer (val);
		if (idio_S_nil == val_i) {
		    rc = idio_sqlite3_bind_double (C_stmt, C_idx, val);
		} else {
		    rc  = idio_sqlite3_bind_int (C_stmt, C_idx, val);
		}
	    }
	    break;
	case IDIO_TYPE_C_INT:
	    rc = idio_sqlite3_bind_int (C_stmt, C_idx, val);
	    break;
	case IDIO_TYPE_C_DOUBLE:
	    rc = idio_sqlite3_bind_double (C_stmt, C_idx, val);
	    break;
	default:
	    if (idio_S_nil == val) {
		rc = idio_sqlite3_bind_null (C_stmt, C_idx, val);
	    } else if (idio_isa_libc_int64_t (val)) {
		rc = idio_sqlite3_bind_int (C_stmt, C_idx, val);
	    } else if (idio_isa_octet_string (val) ||
		       idio_isa_pathname (val)) {
		rc = idio_sqlite3_bind_blob (C_stmt, C_idx, val);
	    } else if (idio_isa_string (val)) {
		rc = idio_sqlite3_bind_text (C_stmt, C_idx, val);
	    } else {
		/*
		 * Test Case: sqlite3-errors/sqlite3-bind-invalid-val-type.idio
		 *
		 * sqlite3-bind stmt 1 #t
		 */
		idio_error_param_value_exp ("%sqlite3-bind", "val", val, "handled val type", IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    break;
	}

	int C_rc = IDIO_C_TYPE_int (rc);
	if (SQLITE_OK != C_rc) {
	    /*
	     * How do we report which one failed?
	     */
	    break;
	}

	args = IDIO_PAIR_TT (args);
    }

    return rc;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%sqlite3-bind-parameter-index", sqlite3_bind_parameter_index, (IDIO stmt, IDIO key), "stmt key", "\
Return the index of parameter `key` in `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:param key: parameter name	\n\
:type key: string		\n\
:return: parameter index	\n\
:rtype: fixnum			\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (key);

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-parameter-index-bad-stmt-type.idio
     *
     * sqlite3-bind-parameter-index #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-bind-parameter-index-invalid-stmt-type.idio
     *
     * sqlite3-bind-parameter-index libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-bind-parameter-index", stmt, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-parameter-index-bad-key-type.idio
     *
     * sqlite3-bind-parameter-index stmt #t
     */
    IDIO_USER_TYPE_ASSERT (string, key);

    char *C_key;
    size_t free_C_key = 0;

    /*
     * Test Case: sqlite3-errors/sqlite3-bind-parameter-index-bad-key-format.idio
     *
     * sqlite3-bind-parameter-index stmt "hello\x0world"
     */
    C_key = idio_libc_string_C (key, "key", &free_C_key, IDIO_C_FUNC_LOCATION ());

    int i = sqlite3_bind_parameter_index (C_stmt, C_key);

    return idio_fixnum (i);
}

IDIO_DEFINE_PRIMITIVE1_DS ("%sqlite3-step", sqlite3_step, (IDIO stmt), "stmt", "\
Return the next row for `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:return: list of columns	\n\
:rtype: list or ``#f``		\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (stmt);

    /*
     * Test Case: sqlite3-errors/sqlite3-step-bad-stmt-type.idio
     *
     * sqlite3-step #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-step-invalid-stmt-type.idio
     *
     * sqlite3-step libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-step", stmt, IDIO_C_FUNC_LOCATION ());

    int rc = sqlite3_step (C_stmt);

    switch (rc) {
    case SQLITE_DONE:
	return idio_S_false;
    case SQLITE_ROW:
	{
	    int cc = sqlite3_column_count (C_stmt);

	    IDIO res = idio_S_nil;

	    if (cc) {
		for (int c = cc - 1; c >= 0; c--) {
		    IDIO col = idio_S_false;
		    switch (sqlite3_column_type (C_stmt, c)) {
		    case SQLITE_INTEGER:
			col = idio_integer ((intmax_t) sqlite3_column_int64 (C_stmt, c));
			break;
		    case SQLITE_FLOAT:
			col = idio_bignum_double (sqlite3_column_double (C_stmt, c));
			break;
		    case SQLITE_NULL:
			col = idio_S_nil;
			break;
		    case SQLITE_BLOB:
			{
			    const void *sblob = sqlite3_column_blob (C_stmt, c);

			    if (NULL == sblob) {
				/* sqlite3_errcode (db) */

				col = idio_C_pointer (NULL);
			    } else {
				int bytes = sqlite3_column_bytes (C_stmt, c);
				col = idio_octet_string_C_len ((char *) sblob, bytes);
			    }
			}
			break;
		    case SQLITE_TEXT:
			/* fall through */
		    default:
			{
			    const unsigned char *text = sqlite3_column_text (C_stmt, c);
			    int bytes = 0;

			    if (NULL == text) {
				/* sqlite3_errcode (db) */

				col = idio_C_pointer (NULL);
			    } else {
				bytes = sqlite3_column_bytes (C_stmt, c);
				col = idio_string_C_len ((const char *) text, bytes);
			    }
			}
			break;
		    }
		    res = idio_pair (col, res);
		}
	    }

	    return res;
	}
	break;
    default:
	{
	    /*
	     * Test Case: ??
	     */
	    idio_sqlite3_error_printf (NULL, stmt, IDIO_C_FUNC_LOCATION (), "sqlite3_step()");

	    return idio_S_notreached;
	}
    }

    /*
     * We shouldn't get here, right?
     */
    idio_sqlite3_error_printf (NULL, stmt, IDIO_C_FUNC_LOCATION (), "sqlite3_step()");

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE2_DS ("%sqlite3-column", sqlite3_column, (IDIO stmt, IDIO idx), "stmt idx", "\
Return column `idx` for the current row of `stmt`	\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:param idx: column index	\n\
:type idx: fixnum		\n\
:return: column			\n\
:rtype: based on column type	\n\
:raises ^rt-sqlite3-error:	\n\
				\n\
.. tip::			\n\
				\n\
   columns are indexed from 0	\n\
")
{
    IDIO_ASSERT (stmt);
    IDIO_ASSERT (idx);

    /*
     * Test Case: sqlite3-errors/sqlite3-column-bad-stmt-type.idio
     *
     * sqlite3-column #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-column-invalid-stmt-type.idio
     *
     * sqlite3-column libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-column", stmt, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-column-bad-idx-type.idio
     *
     * sqlite3-column stmt #t
     */
    IDIO_USER_TYPE_ASSERT (fixnum, idx);

    int C_idx = IDIO_FIXNUM_VAL (idx);

    IDIO col = idio_S_false;

    switch (sqlite3_column_type (C_stmt, C_idx)) {
    case SQLITE_INTEGER:
	col = idio_integer ((intmax_t) sqlite3_column_int64 (C_stmt, C_idx));
	break;
    case SQLITE_FLOAT:
	col = idio_bignum_double (sqlite3_column_double (C_stmt, C_idx));
	break;
    case SQLITE_NULL:
	col = idio_S_nil;
	break;
    case SQLITE_BLOB:
	{
	    const void *sblob = sqlite3_column_blob (C_stmt, C_idx);

	    if (NULL == sblob) {
		/* sqlite3_errcode (db) */

		col = idio_C_pointer (NULL);
	    } else {
		int bytes = sqlite3_column_bytes (C_stmt, C_idx);
		col = idio_octet_string_C_len ((char *) sblob, bytes);
	    }
	}
	break;
    case SQLITE_TEXT:
	/* fall through */
    default:
	{
	    const unsigned char *text = sqlite3_column_text (C_stmt, C_idx);
	    int bytes = 0;

	    if (NULL == text) {
		/* sqlite3_errcode (db) */

		col = idio_C_pointer (NULL);
	    } else {
		bytes = sqlite3_column_bytes (C_stmt, C_idx);
		col = idio_string_C_len ((const char *) text, bytes);
	    }
	}
	break;
    }

    return col;
}

IDIO_DEFINE_PRIMITIVE1_DS ("%sqlite3-reset", sqlite3_reset, (IDIO stmt), "stmt", "\
Reset `stmt`			\n\
				\n\
:param stmt: SQL statement	\n\
:type stmt: C/pointer to :ref:`sqlite3/stmt <sqlite3/sqlite3/stmt>`	\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
")
{
    IDIO_ASSERT (stmt);

    /*
     * Test Case: sqlite3-errors/sqlite3-reset-bad-stmt-type.idio
     *
     * sqlite3-reset #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-reset-invalid-stmt-type.idio
     *
     * sqlite3-reset libc/NULL #t
     */
    sqlite3_stmt *C_stmt = idio_sqlite3_validate_stmt ("%sqlite3-reset", stmt, IDIO_C_FUNC_LOCATION ());

    int rc = sqlite3_reset (C_stmt);

    return idio_C_int (rc);
}

int idio_sqlite3_exec_callback(void *ptr, int cols, char **C_texts, char **C_names)
{
    IDIO func = (IDIO) ptr;

    IDIO texts = idio_S_nil;
    IDIO names = idio_S_nil;

    if (cols) {
	for (int i = cols - 1; i >= 0; i--) {
	    if (NULL == C_texts[i]) {
		texts = idio_pair (idio_S_nil, texts);
	    } else {
		texts = idio_pair (idio_string_C (C_texts[i]), texts);
	    }
	    names = idio_pair (idio_string_C (C_names[i]), names);
	}
    }

    idio_vm_invoke_C (IDIO_LIST3 (func, texts, names));

    return 0;
}

IDIO_DEFINE_PRIMITIVE2V_DS ("%sqlite3-exec", sqlite3_exec, (IDIO db, IDIO sql, IDIO args), "db sql [callback]", "\
Execute `sql` against `db`	\n\
				\n\
:param db: SQL statement	\n\
:type db: C/pointer to :ref:`sqlite3/db <sqlite3/sqlite3/db>`	\n\
:param sql: SQL statement text	\n\
:type sql: string		\n\
:param callback: callback function, defaults to none	\n\
:type callback: function, optional	\n\
:return: sqlite3 return code	\n\
:rtype: C/int			\n\
:raises ^rt-sqlite3-error:	\n\
				\n\
`callback` will be called with a list of column values	\n\
as strings and a list of column names for each row in	\n\
the results.						\n\
")
{
    IDIO_ASSERT (db);
    IDIO_ASSERT (sql);

    /*
     * Test Case: sqlite3-errors/sqlite3-exec-bad-db-type.idio
     *
     * sqlite3-exec #t #t
     */
    /*
     * Test Case: sqlite3-errors/sqlite3-exec-invalid-db-type.idio
     *
     * sqlite3-exec libc/NULL #t
     */
    sqlite3 *C_db = idio_sqlite3_validate_db ("%sqlite3-exec", db, IDIO_C_FUNC_LOCATION ());

    /*
     * Test Case: sqlite3-errors/sqlite3-exec-bad-sql-type.idio
     *
     * sqlite3-exec db #t
     */
    IDIO_USER_TYPE_ASSERT (string, sql);

    char *C_sql;
    size_t free_C_sql = 0;

    /*
     * Test Case: sqlite3-errors/sqlite3-exec-bad-sql-format.idio
     *
     * sqlite3-exec db "hello\x0world"
     */
    C_sql = idio_libc_string_C (sql, "sql", &free_C_sql, IDIO_C_FUNC_LOCATION ());

    int (*callback) (void *, int, char **, char **) = NULL;
    void *ptr = NULL;

    if (idio_isa_pair (args)) {
	IDIO func = IDIO_PAIR_H (args);

	/*
	 * Test Case: sqlite3-errors/sqlite3-exec-bad-callback-type.idio
	 *
	 * sqlite3-exec db "" #t
	 */
	IDIO_USER_TYPE_ASSERT (function, func);

	callback = idio_sqlite3_exec_callback;
	ptr = (void *) func;
    }

    char *err_msg = NULL;
    int rc = sqlite3_exec (C_db, C_sql, callback, ptr, &err_msg);

    return idio_C_int (rc);
}

void idio_sqlite3_db_finalizer (IDIO db)
{
    IDIO_ASSERT (db);
    IDIO_TYPE_ASSERT (C_pointer, db);

    sqlite3 *C_db = IDIO_C_TYPE_POINTER_P (db);

#if defined (IDIO_NO_SQLITE3_CLOSE_V2)
    int rc = sqlite3_close (C_db);
#else
    int rc = sqlite3_close_v2 (C_db);
#endif

    idio_invalidate_C_pointer (db);

    /*
     * sqlite3_close_v2() always returns SQLITE_OK so the test here is
     * really for sqlite3_close() where we commonly expect to get
     * SQLITE_BUSY on shutdown and we won't trouble the user with it.
     */
    if (SQLITE_OK != rc) {
	if (SQLITE_BUSY != rc) {
	    idio_error_warning_message ("sqlite3_close() => %d: %s\n", rc, sqlite3_errmsg (C_db));
	}
    }
}

void idio_sqlite3_stmt_finalizer (IDIO stmt)
{
    IDIO_ASSERT (stmt);
    IDIO_TYPE_ASSERT (C_pointer, stmt);

    sqlite3_stmt *C_stmt = IDIO_C_TYPE_POINTER_P (stmt);

    int rc = sqlite3_finalize (C_stmt);

    idio_invalidate_C_pointer (stmt);

    if (SQLITE_OK != rc) {
	/* sqlite3_errcode (db) */

	idio_error_warning_message ("sqlite3_finalize() => %d\n", rc);
    }
}

void idio_sqlite3_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_version);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_open);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_close);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_errmsg);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_prepare);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_finalize);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_bind_blob);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_bind_double);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_bind_int);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_bind_null);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_bind_text);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_bind);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_bind_parameter_index);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_step);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_column);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_reset);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_sqlite3_module, sqlite3_exec);
}

void idio_final_sqlite3 ()
{
}

void idio_init_sqlite3 (void *handle)
{
    idio_sqlite3_module = idio_module (IDIO_SYMBOL ("sqlite3"));

    idio_module_table_register (idio_sqlite3_add_primitives, idio_final_sqlite3, handle);

    idio_module_export_symbol_value (idio_S_version,
				     idio_string_C_len (SQLITE3_SYSTEM_VERSION, sizeof (SQLITE3_SYSTEM_VERSION) - 1),
				     idio_sqlite3_module);

    IDIO sym = IDIO_SYMBOL ("sqlite3-db");
    idio_sqlite3_db_type = idio_struct_type (sym,
					     idio_S_nil,
					     idio_listv (IDIO_SQLITE3_DB_ST_SIZE,
							 IDIO_SYMBOL ("name"),
							 IDIO_SYMBOL ("%db"),
							 IDIO_SYMBOL ("stmts")));
    idio_module_set_symbol_value (sym, idio_sqlite3_db_type, idio_sqlite3_module);

    idio_sqlite3_stmt_map = IDIO_HASH_EQP (4);
    idio_gc_add_weak_object (idio_sqlite3_stmt_map);
    idio_gc_protect_auto (idio_sqlite3_stmt_map);

    sym = IDIO_SYMBOL ("%sqlite3-stmt-map");
    idio_module_set_symbol_value (sym, idio_sqlite3_stmt_map, idio_sqlite3_module);

    IDIO_C_STRUCT_IDENT_DEF (IDIO_SYMBOL ("sqlite3/db"), idio_S_nil, sqlite3_db, idio_fixnum0);
    IDIO_C_STRUCT_IDENT_DEF (IDIO_SYMBOL ("sqlite3/stmt"), idio_S_nil, sqlite3_stmt, idio_fixnum0);

    IDIO_DEFINE_CONDITION0 (idio_condition_rt_sqlite3_error_type, "^rt-sqlite3-error", idio_condition_runtime_error_type);

    IDIO_SYMBOL_DEF ("blob", blob);
    IDIO_SYMBOL_DEF ("double", double);
    IDIO_SYMBOL_DEF ("int", int);
    IDIO_SYMBOL_DEF ("null", null);
    IDIO_SYMBOL_DEF ("text", text);

#if defined (IDIO_NO_SQLITE3_OPEN_V2)
    idio_add_feature (IDIO_SYMBOL ("IDIO_NO_SQLITE3_OPEN_V2"));
#else
    idio_module_export_symbol_value (IDIO_SYMBOL ("SQLITE_OPEN_READONLY"), idio_C_int (SQLITE_OPEN_READONLY), idio_sqlite3_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("SQLITE_OPEN_READWRITE"), idio_C_int (SQLITE_OPEN_READWRITE), idio_sqlite3_module);
    idio_module_export_symbol_value (IDIO_SYMBOL ("SQLITE_OPEN_CREATE"), idio_C_int (SQLITE_OPEN_CREATE), idio_sqlite3_module);
#if defined (SQLITE_OPEN_URI)
    /* introduced in v3.7.7.1 */
    idio_module_export_symbol_value (IDIO_SYMBOL ("SQLITE_OPEN_URI"), idio_C_int (SQLITE_OPEN_URI), idio_sqlite3_module);
#endif
#endif

    idio_module_export_symbol_value (IDIO_SYMBOL ("SQLITE_OK"), idio_C_int (SQLITE_OK), idio_sqlite3_module);
}
