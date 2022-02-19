/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * path.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <glob.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "handle.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

IDIO idio_path_type;

static void idio_pathname_error (IDIO msg, IDIO detail, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (detail);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (string, detail);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO lsh;
    IDIO dsh;
    idio_error_init (NULL, &lsh, &dsh, c_location);

    idio_error_raise_cont (idio_condition_rt_path_error_type,
			   IDIO_LIST4 (msg,
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       detail));

    /* notreached */
}

static void idio_glob_base_error (IDIO msg, IDIO pattern, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (pattern);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO lsh;
    IDIO dsh;
    idio_error_init (NULL, &lsh, &dsh, c_location);

    idio_error_raise_cont (idio_condition_rt_glob_error_type,
			   IDIO_LIST4 (msg,
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       pattern));

    /* notreached */
}

static void idio_glob_error_C (char const *msg, IDIO pattern, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (pattern);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, pattern);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    idio_glob_base_error (idio_get_output_string (msh), pattern, c_location);

    /* notreached */
}

void idio_pathname_format_error (char const *msg, IDIO str, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (str);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, str);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("pathname format: ", msh);
    idio_display_C (msg, msh);

    idio_pathname_error (idio_get_output_string (msh), str, c_location);

    /* notreached */
}

IDIO idio_pathname_C_len (char const *s_C, size_t const blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);
    so->vtable = idio_vtable (IDIO_TYPE_STRING);

    IDIO_GC_ALLOC (IDIO_STRING_S (so), blen + 1);
    IDIO_STRING_BLEN (so) = blen;

    uint8_t *us8 = (uint8_t *) IDIO_STRING_S (so);

    uint8_t *us_C = (unsigned char *) s_C;
    size_t i;
    for (i = 0; i < blen; i++) {
	if (0 == us_C[i]) {
	    /*
	     * Test Case: path-errors/pathname-bad-format.idio
	     *
	     * %P{hello\x0world}
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "contains an ASCII NUL at %zd/%zd", i + 1, blen);

	    idio_pathname_format_error (em, idio_string_C_len (s_C, blen), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	us8[i] = (uint8_t) us_C[i];
    }

    IDIO_STRING_S (so)[i] = '\0';

    IDIO_STRING_LEN (so) = blen;
    IDIO_STRING_FLAGS (so) = IDIO_STRING_FLAG_PATHNAME;

    return so;
}

IDIO idio_pathname_C (char const *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_pathname_C_len (s_C, strlen (s_C));
}

int idio_isa_pathname (IDIO o)
{
    IDIO_ASSERT (o);

    return ((idio_isa (o, IDIO_TYPE_STRING) &&
	     (IDIO_STRING_FLAGS (o) & IDIO_STRING_FLAG_PATHNAME)) ||
	    (idio_isa (o, IDIO_TYPE_SUBSTRING) &&
	     (IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (o)) & IDIO_STRING_FLAG_PATHNAME)));
}

IDIO_DEFINE_PRIMITIVE1_DS ("pathname?", pathname_p, (IDIO o), "o", "\
test if `o` is an pathname				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is an pathname, ``#f`` otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_pathname (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_fd_pathname_C_len (char const *s_C, size_t const blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_pathname_C_len (s_C, blen);
    IDIO_STRING_FLAGS (so) = IDIO_STRING_FLAG_FD_PATHNAME;

    return so;
}

IDIO idio_fd_pathname_C (char const *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_fd_pathname_C_len (s_C, strlen (s_C));
}

int idio_isa_fd_pathname (IDIO o)
{
    IDIO_ASSERT (o);

    return ((idio_isa (o, IDIO_TYPE_STRING) &&
	     (IDIO_STRING_FLAGS (o) & IDIO_STRING_FLAG_FD_PATHNAME)) ||
	    ((idio_isa (o, IDIO_TYPE_SUBSTRING) &&
	      (IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (o)) & IDIO_STRING_FLAG_FD_PATHNAME))));
}

IDIO idio_fifo_pathname_C_len (char const *s_C, size_t const blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_pathname_C_len (s_C, blen);
    IDIO_STRING_FLAGS (so) = IDIO_STRING_FLAG_FIFO_PATHNAME;

    return so;
}

IDIO idio_fifo_pathname_C (char const *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_fifo_pathname_C_len (s_C, strlen (s_C));
}

int idio_isa_fifo_pathname (IDIO o)
{
    IDIO_ASSERT (o);

    return ((idio_isa (o, IDIO_TYPE_STRING) &&
	     (IDIO_STRING_FLAGS (o) & IDIO_STRING_FLAG_FIFO_PATHNAME)) ||
	    ((idio_isa (o, IDIO_TYPE_SUBSTRING) &&
	      (IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (o)) & IDIO_STRING_FLAG_FIFO_PATHNAME))));
}

IDIO idio_glob_expand (IDIO s)
{
    IDIO_ASSERT (s);

    IDIO_TYPE_ASSERT (string, s);

    size_t size = 0;
    char *s_C = idio_string_as_C (s, &size);

    /*
     * Use size + 1 to avoid a truncation warning -- we're just seeing
     * if s_C includes a NUL
     */
    size_t C_size = idio_strnlen (s_C, size + 1);
    if (C_size != size) {
	/*
	 * Test Case: path-errors/pattern-bad-format.idio
	 *
	 * p := make-struct-instance ~path (join-string (make-string 1 #U+0) '("hello" "world"))
	 * length p
	 */
	IDIO_GC_FREE (s_C, size);

	idio_glob_error_C ("pattern contains an ASCII NUL", s, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    glob_t g;
    IDIO r = idio_S_nil;

    int ret = glob (s_C, 0, NULL, &g);

    switch (ret) {
    case GLOB_NOMATCH:
	break;
    case 0:
	{
	    size_t i;
	    for (i = 0; i < g.gl_pathc; i++) {
		r = idio_pair (idio_pathname_C (g.gl_pathv[i]), r);
	    }
	}
	break;
    default:
	/*
	 * Test Case: ??
	 *
	 * Check test-command-error.idio for an example of how you
	 * might make glob(3) fail but it requires C level changes --
	 * notably passing GLOB_ERR.
	 */
	globfree (&g);
	IDIO_GC_FREE (s_C, size);
	idio_glob_error_C ("pattern glob failed", s, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	break;
    }

    globfree (&g);
    IDIO_GC_FREE (s_C, size);

    return idio_list_nreverse (r);
}

IDIO_DEFINE_PRIMITIVE1_DS ("glob", glob, (IDIO s), "s", "\
glob expand `s`					\n\
						\n\
:param s: :manpage:`glob(3)` pattern		\n\
:type s: string					\n\
:return: list of matching pathnames		\n\
:rtype: list					\n\
:raises ^rt-glob-error:				\n\
")
{
    IDIO_ASSERT (s);

    IDIO_USER_TYPE_ASSERT (string, s);

    return idio_glob_expand (s);
}

IDIO idio_path_expand (IDIO p)
{
    IDIO_ASSERT (p);

    IDIO_TYPE_ASSERT (struct_instance, p);

    if (! idio_struct_type_isa (IDIO_STRUCT_INSTANCE_TYPE (p), idio_path_type)) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.  The only caller, idio_vm_values_ref() does
	 * the same test.
	 */
	idio_error_param_type ("~path", p, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO pat = IDIO_STRUCT_INSTANCE_FIELDS (p, IDIO_PATH_PATTERN);

    IDIO_TYPE_ASSERT (string, pat);

    return idio_glob_expand (pat);
}

void idio_path_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (pathname_p);
    IDIO_ADD_PRIMITIVE (glob);
}

void idio_init_path ()
{
    idio_module_table_register (idio_path_add_primitives, NULL, NULL);

    IDIO_DEFINE_STRUCT1 (idio_path_type, "~path", idio_S_nil, "pattern");
}

