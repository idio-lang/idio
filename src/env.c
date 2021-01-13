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
 * env.c
 *
 */

#include "idio.h"

/* MacOS & Solaris doesn't have environ in unistd.h */
extern char **environ;

char *idio_env_PATH_default = "/bin:/usr/bin";
char *idio_env_IDIOLIB_default = NULL;
IDIO idio_env_IDIOLIB_sym;
IDIO idio_env_PATH_sym;
IDIO idio_env_PWD_sym;

/*
 * Code coverage:
 *
 * This code will only get called if IDIOLIB has an ASCII NULL in it
 * which...is unlikely.
 */
void idio_env_format_error (char *circumstance, char *msg, IDIO name, IDIO val, IDIO c_location)
{
    IDIO_C_ASSERT (circumstance);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (name);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C (circumstance, msh);
    idio_display_C (": environment variable", msh);
    if (idio_S_nil != name) {
	idio_display_C (" '", msh);
	idio_display (name, msh);
	idio_display_C ("' ", msh);
    } else {
	/*
	 * Code coverage:
	 *
	 * We only call this for IDIOLIB so this clause waits...
	 */
	idio_display_C (" ", msh);
    }
    idio_display_C (msg, msh);

    IDIO location = idio_vm_source_location ();

    IDIO dsh = idio_open_output_string_handle_C ();
#ifdef IDIO_DEBUG
    idio_display (c_location, dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_environ_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       idio_get_output_string (dsh),
					       val));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static int idio_env_set_default (IDIO name, char *val)
{
    IDIO_ASSERT (name);
    IDIO_C_ASSERT (val);
    IDIO_TYPE_ASSERT (symbol, name);

    IDIO ENV = idio_module_env_symbol_value (name, IDIO_LIST1 (idio_S_false));
    if (idio_S_false == ENV) {
	/*
	 * Code coverage:
	 *
	 * Not necessarily easy to get here normally.  To get here,
	 * one or more of PATH, PWD and IDIOLIB must be unset in the
	 * environment -- which is easy enough itself:
	 *
	 * env - .../bin/idio
	 *
	 * PATH and PWD you'd normally expect to be set in a user's
	 * environment but no-one (surely?) sets IDIOLIB in their
	 * environment.  Yet!
	 *
	 * So, we'll get here if no-one has set IDIOLIB otherwise it's
	 * a manual test.
	 */
	idio_environ_extend (name, name, idio_string_C (val), idio_vm_constants);
	return 1;
    }

    return 0;
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
	    IDIO_GC_FREE (name);

	    val = idio_string_C (e + 1);
	} else {
	    /*
	     * Code coverage:
	     *
	     * Hmm, can we have an environment where there is no =
	     * sign in the environ string?  environ(7) says:
	     *
	     *   By convention the strings in environ have the form
	     *   "name=value".
	     *
	     * We're probably in some shady territory, here.  Perhaps
	     * we should even skip such a thing.
	     */
	    var = idio_string_C (*env);
	}

	idio_environ_extend (var, var, val, idio_vm_constants);
    }

    /*
     * Hmm.  What if we have a "difficult" environment?  A particular
     * example is if we don't have a PATH.  *We* use the PATH
     * internally at the very least and not having one is llikely to
     * be described as an issue for a shell.
     *
     * So we'll test for some key environment variables we need and
     * set a default if there isn't one.
     *
     * PATH
     * PWD
     * IDIOLIB
     */

    idio_env_set_default (idio_env_PATH_sym, idio_env_PATH_default);

    /*
     * See comment in libc-wrap.c re: getcwd(3)
     */
    char *cwd = getcwd (NULL, PATH_MAX);

    if (NULL == cwd) {
	/*
	 * Test Case: ??
	 *
	 * There is a similar getcwd(3) call in
	 * idio_command_find_exe_C() which has the advantage of being
	 * able to relocate itself in an inaccessible directory before
	 * launching a command (or not).
	 *
	 * This is during bootstrap so, uh, not so easy.
	 */
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (idio_env_set_default (idio_env_PWD_sym, cwd) == 0) {
	/*
	 * On Mac OS X (Mavericks):
	 *
	 * (lldb) process launch -t -w X -- ...
	 *
	 * may well change the working directory to X but it doesn't
	 * change the environment variable PWD.  There must be any
	 * number of other situations where a process changes the
	 * working directory but doesn't change the environment
	 * variable -- no reason why it should, of course.
	 *
	 * For testing we can use:
	 *
	 * env PWD=/ .../idio
	 *
	 * So, if we didn't create a new variable in
	 * idio_env_set_default() then set the value regardless now.
	 */
	idio_module_env_set_symbol_value (idio_env_PWD_sym, idio_string_C (cwd));
    }

    /*
     * XXX getcwd() used system allocator
     */
    free (cwd);
}

/*
 * We want to generate a nominal IDIOLIB based on the path to the
 * running executable.  If argv0 is simply "idio" then we need to
 * discover where on the PATH it was found, otherwise we can normalize
 * argv0 with realpath(3).
 *
 * NB. We are called after idio_env_add_environ() as main() has to
 * pass us argv0.  main() could have passed argv0 to idio_init() then
 * to idio_env_add_primitives() then to idio_env_add_environ() and
 * then here to us.  Or it could just call us separately.  Which it
 * does.
 */
void idio_env_init_idiolib (char *argv0)
{
    IDIO_C_ASSERT (argv0);

    char *dir = rindex (argv0, '/');

    if (NULL == dir) {
	/*
	 * Test Case: ??
	 *
	 * Actually, the problem with the test case isn't that we
	 * can't get here, we simply need to invoke idio from a
	 * directory on the PATH but that we can't invoke *both* an
	 * explicit exec, ".../idio", and an inplicit one,
	 * "PATH=... idio", in the same test suite.
	 */
	argv0 = idio_command_find_exe_C (argv0);
    }

    char resolved_path[PATH_MAX];
    /* a0rp => argv0_realpath */
    char *a0rp = realpath (argv0, resolved_path);

    if (NULL == a0rp) {
	/*
	 * Test Case: ??
	 *
	 * Like the getcwd(3) case, above, this is hard to emulate
	 * during bootstrap.
	 */
	idio_error_system_errno ("realpath(3) => NULL", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    /*
     * a0rp	~ /a/b/c/idio
     *
     * dir	~ /idio
     *
     * pdir	~ /c/idio		parent dir (of dir)
     *
     * If pdir is actually /bin/idio, ie. starts with /bin, then we
     * can make an assumption that there is a parallel /lib containing
     * Idio stuff which become idio_env_IDIOLIB_default.
     *
     * Finally, if IDIOLIB is unset we can set it to
     * idio_env_IDIOLIB_default.
     *
     * If IDIOLIB is already set we check to see if
     * idio_env_IDIOLIB_default isn't already on IDIOLIB and append
     * it.
     */
    dir = rindex (a0rp, '/');

    if (dir != a0rp) {
	char *pdir = dir - 1;
	while (pdir > a0rp &&
	       '/' != *pdir) {
	    pdir--;
	}

	if (pdir >= a0rp) {
	    if (strncmp (pdir, "/bin", 4) == 0) {
		/* ... + /lib */
		size_t ieId_len = pdir - a0rp + 4;
		idio_env_IDIOLIB_default = idio_alloc (ieId_len + 1);
		strncpy (idio_env_IDIOLIB_default, a0rp, pdir - a0rp);
		idio_env_IDIOLIB_default[pdir - a0rp] = '\0';
		strcat (idio_env_IDIOLIB_default, "/lib");

		if (! idio_env_set_default (idio_env_IDIOLIB_sym, idio_env_IDIOLIB_default)) {
		    /*
		     * Code coverage:
		     *
		     * We'll roll through here if IDIOLIB is already
		     * in the environment when we start.
		     */
		    IDIO idiolib = idio_module_env_symbol_value (idio_env_IDIOLIB_sym, IDIO_LIST1 (idio_S_false));

		    size_t idiolib_len = 0;
		    char *sidiolib = idio_string_as_C (idiolib, &idiolib_len);
		    size_t C_size = strlen (sidiolib);
		    if (C_size != idiolib_len) {
			/*
			 * Test Case: ??
			 *
			 * This is a bit hard to conceive and, indeed,
			 * might be a wild goose chase.
			 *
			 * This code is only called on startup and if
			 * IDIOLIB is (deliberately) goosed[sic] then
			 * everything thereafter is banjaxed as well.
			 * Which makes the collective testing tricky.
			 *
			 * In the meanwhilst, how do you inject an
			 * environment variable into idio's
			 * environment with an ASCII NUL in it?
			 */
			IDIO_GC_FREE (sidiolib);

			idio_env_format_error ("bootstrap", "contains an ASCII NUL", idio_env_IDIOLIB_sym, idiolib, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return;
		    }

		    char *index = strstr (sidiolib, idio_env_IDIOLIB_default);
		    int append = 0;
		    if (index) {
			/*
			 * Code coverage:
			 *
			 * These should be picked up if a pre-existing
			 * IDIOLIB is one of:
			 *
			 * .../lib
			 * .../lib:...
			 *
			 * Both of which will fall into the manual
			 * test category.
			 *
			 * We do these extra checks in case someone
			 * has added .../libs -- which index will
			 * match.
			 */
			if (! ('\0' == index[ieId_len] ||
			       ':' == index[ieId_len])) {
			    append = 1;
			}
		    } else {
			/*
			 * Code coverage:
			 *
			 * IDIOLIB is set but doesn't have .../lib
			 */
			append = 1;
		    }

		    if (append) {
			size_t ni_len = idiolib_len + 1 + ieId_len + 1;
			if (0 == idiolib_len) {
			    ni_len = ieId_len + 1;
			}
			char *ni = idio_alloc (ni_len + 1);
			ni[0] = '\0';
			if (idiolib_len) {
			    strcpy (ni, sidiolib);
			    strcat (ni, ":");
			}
			strcat (ni, idio_env_IDIOLIB_default);
			ni[ni_len] = '\0';
			idio_module_env_set_symbol_value (idio_env_IDIOLIB_sym, idio_string_C (ni));
			IDIO_GC_FREE (ni);
		    }

		    IDIO_GC_FREE (sidiolib);
		}
	    }
	}
    }
}

void idio_env_add_primitives ()
{
    idio_env_add_environ ();
}

void idio_final_env ()
{
    IDIO_GC_FREE (idio_env_IDIOLIB_default);
}

void idio_init_env ()
{
    idio_module_table_register (idio_env_add_primitives, idio_final_env);

    idio_env_IDIOLIB_sym = idio_symbols_C_intern ("IDIOLIB");
    idio_env_PATH_sym = idio_symbols_C_intern ("PATH");
    idio_env_PWD_sym = idio_symbols_C_intern ("PWD");
}

