/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

static void idio_path_error_glob (IDIO pattern, IDIO c_location)
{
    IDIO_ASSERT (pattern);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("pattern glob failed", sh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_glob_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       location,
					       c_location,
					       pattern));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

void idio_path_error_format (char *m, IDIO p, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (p);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (struct_instance, p);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_C (m, p, c_location);

    /* notreached */
}

char **idio_path_env_split (const char *path_env)
{
    int ndirs = 0;
    char sep = ':';

    if (NULL == path_env) {
	return NULL;
    }

    char *pe = (char *) path_env;
    while ((pe = strchr (pe, sep)) != NULL) {
	ndirs++;
	pe++;
    }

    /* the above loop doesn't count the final (non-sep'd) PATH and
       plus one for NULL */
    char **r = idio_alloc ((ndirs + 2) * sizeof (char *));
    pe = (char *) path_env;
    char *end;
    int len;
    ndirs = 0;
    while ((end = strchr (pe, sep)) != NULL) {
	len = end - pe;
	r[ndirs] = idio_alloc (len + 1);
	strncpy (r[ndirs], pe, len);
	r[ndirs][len] = '\0';
	ndirs++;
	pe = end;
    }
    len = strlen (pe);
    r[ndirs] = idio_alloc (len + 1);
    /*
     * gcc warns "output truncated before terminating nul copying as
     * many bytes from a string as its length [-Wstringop-truncation]"
     * for just strncpy(..., len)
     */
    strncpy (r[ndirs], pe, len + 1);
    r[ndirs][len] = '\0';
    ndirs++;
    r[ndirs] = NULL;

    return r;
}

char *idio_find_file (const char *file)
{
    const char *exts[] = { "",
			   ".idio",
			   ".scm1",
			   ".scm",
			   NULL
    };

    const char **ext = exts;

    char *idio_path_env = getenv ("IDIO_PATH");
    if (NULL == idio_path_env) {
	idio_path_env = ".";
    }

    char **idio_paths = idio_path_env_split (idio_path_env);

    if (NULL == idio_paths) {
	return 0;
    }

    char **idio_path = idio_paths;
    char *filename = NULL;

    while (*ext) {
	/* possible "file.ext", ie len(file) + len(ext) + 1 */
	size_t blen = strlen (file);
	size_t len_ext = strlen (*ext);
	char *file_with_ext = idio_alloc (blen + len_ext + 1);
	memcpy (file_with_ext, file, blen);
	file_with_ext[blen] = '\0';
	if (blen < (len_ext + 1) ||
	    strncmp (&file_with_ext[blen-len_ext], *ext, len_ext)) {
	    strcat (file_with_ext, *ext);
	}

	while (*idio_path) {
	    filename = idio_alloc (strlen (*idio_path) + 1 + strlen (file_with_ext) + 1);
	    strcpy (filename, *idio_path);
	    strcpy (filename + strlen (*idio_path), "/");
	    strcpy (filename + strlen (*idio_path) + 1, file_with_ext);

	    if (access (filename, R_OK) == 0) {
		break;
	    }

	    free (filename);
	    filename = NULL;
	    idio_path++;
	}

	if (*idio_path == NULL) {
	    if (NULL != filename) {
		free (filename);
	    }
	    filename = NULL;
	}

	free (file_with_ext);

	if (NULL != filename) {
	    break;
	}

	ext++;
    }

    idio_path = idio_paths;
    while (*idio_path) {
	free (*idio_path);
	idio_path++;
    }

    free (*idio_path);
    free (idio_paths);

    return filename;
}

IDIO idio_path_expand (IDIO p)
{
    IDIO_ASSERT (p);
    IDIO_TYPE_ASSERT (struct_instance, p);

    if (! idio_struct_type_isa (IDIO_STRUCT_INSTANCE_TYPE (p), idio_path_type)) {
	idio_error_param_type ("~path", p, IDIO_C_FUNC_LOCATION ());
    }

    IDIO pat = idio_array_get_index (IDIO_STRUCT_INSTANCE_FIELDS (p), IDIO_PATH_PATTERN);

    IDIO_TYPE_ASSERT (string, pat);
    size_t size = 0;
    char *pat_C = idio_string_as_C (pat, &size);

    size_t C_size = strlen (pat_C);
    if (C_size != size) {
	free (pat_C);

	idio_path_error_format ("pattern contains an ASCII NUL", p, IDIO_C_FUNC_LOCATION ());

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
	globfree (&g);
	free (pat_C);
	idio_path_error_glob (pat, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	break;
    }

    globfree (&g);
    free (pat_C);

    return idio_list_reverse (r);
}

void idio_init_path ()
{
    IDIO_DEFINE_STRUCT1 (idio_path_type, "~path", idio_S_nil, "pattern");
}

void idio_path_add_primitives ()
{
}

void idio_final_path ()
{
    idio_gc_expose (idio_path_type);
}


