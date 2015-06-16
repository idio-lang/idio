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
 * env.c
 * 
 */

#include "idio.h"

/* MacOS & Solaris doesn't have environ in unistd.h */
extern char **environ;

char *idio_env_PATH_default = "/bin:/usr/bin";
char *idio_env_IDIOLIB_default = NULL;
IDIO idio_env_PATH_sym;
IDIO idio_env_IDIOLIB_sym;

static void idio_env_set_default (IDIO name, char *val)
{
    IDIO_ASSERT (name);
    IDIO_C_ASSERT (val);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO ENV = idio_module_current_symbol_value (name);
    if (idio_S_unspec == ENV) {
	idio_toplevel_extend (name, IDIO_ENVIRON_SCOPE);
	idio_module_current_set_symbol_value (name, idio_string_C (val));
    }
}

static void idio_env_add_environ ()
{
    char **env;
    for (env = environ; *env ; env++) {
	char *e = strchr (*env, '=');

	IDIO var;
	IDIO val = idio_S_undef;
	
	if (NULL != e) {
	    char *name = idio_alloc (e - *env + 1);
	    strncpy (name, *env, e - *env);
	    name[e - *env] = '\0';
	    var = idio_symbols_C_intern (name);
	    free (name);
	    
	    val = idio_string_C (e + 1);
	} else {
	    var = idio_string_C (*env);
	}

	idio_toplevel_extend (var, IDIO_ENVIRON_SCOPE);
	idio_module_current_set_symbol_value (var, val);
    }

    /*
     * Hmm.  What if we have a "difficult" environment?  A particular
     * example is if we don't have a PATH.  We use the PATH internally
     * at the very least and not having one is an issue.
     *
     * So we'll test for some environment variables we need and set a
     * default if there isn't one.
     */

    idio_env_set_default (idio_env_PATH_sym, idio_env_PATH_default);
}

/*
 * We want to generate a nominal IDIOLIB based on the path to the
 * running executable.  If argv0 is "idio" then we need to discover
 * where on th PATH it was found, otherwise we can nornalize argv0
 * with realpath(3).
 */
void idio_env_init_idiolib (char *argv0)
{
    IDIO_C_ASSERT (argv0);

    char *dir = rindex (argv0, '/');

    if (NULL == dir) {
	argv0 = idio_command_find_exe_C (argv0);
    }
    
    char *path = realpath (argv0, NULL);

    if (NULL == path) {
	idio_error_system_errno ("realpath(3) => NULL", idio_S_nil);
    }

    dir = rindex (path, '/');

    if (dir != path) {
	char *pdir = dir - 1;
	while (pdir > path &&
	       '/' != *pdir) {
	    pdir--;
	}

	if (pdir >= path) {
	    if (strncmp (pdir, "/bin", 4) == 0) {
		/* ... + /lib */
		char *idio_env_IDIOLIB_default = idio_alloc (pdir - path + 4 + 1);
		strncpy (idio_env_IDIOLIB_default, path, pdir - path);
		idio_env_IDIOLIB_default[pdir - path] = '\0';
		strcat (idio_env_IDIOLIB_default, "/lib");
	    }
	}
    }

    free (path);
}

void idio_init_env ()
{
    idio_env_PATH_sym = idio_symbols_C_intern ("PATH");
    idio_env_IDIOLIB_sym = idio_symbols_C_intern ("IDIOLIB");
}

void idio_env_add_primitives ()
{
    idio_env_add_environ ();
}

void idio_final_env ()
{
    free (idio_env_IDIOLIB_default);
}
