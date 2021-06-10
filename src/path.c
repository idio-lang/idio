/*
 * Copyright (c) 2015, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

#include "idio.h"

IDIO idio_path_type;

static void idio_pathname_error (IDIO msg, IDIO detail, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (detail);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (string, detail);
    IDIO_TYPE_ASSERT (string, c_location);

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    if (idio_S_nil != detail) {
	idio_display (detail, dsh);
	idio_display_C (": ", dsh);
    }
    idio_display (c_location, dsh);

    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_path_error_type,
				   IDIO_LIST4 (msg,
					       idio_S_nil,
					       detail,
					       detail));

    idio_raise_condition (idio_S_false, c);

    /* notreached */
}

static void idio_path_base_error (IDIO msg, IDIO pattern, IDIO c_location)
{
    IDIO_ASSERT (msg);
    IDIO_ASSERT (pattern);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (string, msg);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO location = idio_vm_source_location ();

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_glob_error_type,
				   IDIO_LIST4 (msg,
					       location,
					       detail,
					       pattern));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_path_error_C (char *msg, IDIO pattern, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (pattern);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (struct_instance, pattern);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (msg, msh);

    idio_path_base_error (idio_get_output_string (msh), pattern, c_location);

    /* notreached */
}

void idio_pathname_format_error (char *msg, IDIO str, IDIO c_location)
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

IDIO idio_pathname_C_len (const char *s_C, size_t blen)
{
    IDIO_C_ASSERT (s_C);

    IDIO so = idio_gc_get (IDIO_TYPE_STRING);

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
	    sprintf (em, "contains an ASCII NUL at %zd/%zd", i + 1, blen);
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

IDIO idio_pathname_C (const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    return idio_pathname_C_len (s_C, strlen (s_C));
}

IDIO_DEFINE_PRIMITIVE1_DS ("pathname?", pathname_p, (IDIO o), "o", "\
test if `o` is an pathname				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an pathname, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_string (o) &&
	(IDIO_STRING_FLAGS (o) & IDIO_STRING_FLAG_PATHNAME)) {
	r = idio_S_true;
    }

    return r;
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
    size_t size = 0;
    char *pat_C = idio_string_as_C (pat, &size);

    size_t C_size = strlen (pat_C);
    if (C_size != size) {
	/*
	 * Test Case: path-errors/pattern-bad-format.idio
	 *
	 * p := make-struct-instance ~path (join-string (make-string 1 #U+0) '("hello" "world"))
	 * length p
	 */
	IDIO_GC_FREE (pat_C);

	idio_path_error_C ("pattern contains an ASCII NUL", p, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    glob_t g;
    IDIO r = idio_S_nil;

    int ret = glob (pat_C, 0, NULL, &g);

    switch (ret) {
    case GLOB_NOMATCH:
	break;
    case 0:
	{
	    size_t i;
	    for (i = 0; i < g.gl_pathc; i++) {
		r = idio_pair (idio_string_C (g.gl_pathv[i]), r);
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
	IDIO_GC_FREE (pat_C);
	idio_path_error_C ("pattern glob failed", p, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	break;
    }

    globfree (&g);
    IDIO_GC_FREE (pat_C);

    return idio_list_reverse (r);
}

void idio_path_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (pathname_p);
}

void idio_init_path ()
{
    idio_module_table_register (idio_path_add_primitives, NULL);

    IDIO_DEFINE_STRUCT1 (idio_path_type, "~path", idio_S_nil, "pattern");
}

