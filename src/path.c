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
 * path.c
 * 
 */

#include "idio.h"

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
    strncpy (r[ndirs], pe, len);
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
    char *filename;

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

