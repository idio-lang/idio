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
 * command.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/utsname.h>

#include <assert.h>
#include <errno.h>
#include <glob.h>
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
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "c-type.h"
#include "condition.h"
#include "env.h"
#include "error.h"
#include "evaluate.h"
#include "file-handle.h"
#include "fixnum.h"
#include "frame.h"
#include "handle.h"
#include "idio-string.h"
#include "job-control.h"
#include "libc-wrap.h"
#include "module.h"
#include "pair.h"
#include "path.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"

#include "libc-api.h"

IDIO idio_command_module = idio_S_nil;
IDIO idio_command_suppress_rcse = idio_S_false;

/*
 * Code coverage:
 *
 * idio_command_glob_error() is only called if we pass GLOB_ERR to
 * glob(3).  Which we don't.
 */
static void idio_command_glob_error (IDIO pattern, IDIO c_location)
{
    IDIO_ASSERT (pattern);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("pattern glob failed", msh);

    idio_error_raise_cont (idio_condition_rt_glob_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       pattern));

    /* notreached */
}

static void idio_command_arg_type_error (IDIO arg, char const *reason_C, IDIO c_location)
{
    IDIO_ASSERT (arg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("can't convert a ", msh);
    if (idio_isa_struct_instance (arg)) {
	idio_display (IDIO_STRUCT_TYPE_NAME (IDIO_STRUCT_INSTANCE_TYPE (arg)), msh);
    } else {
	idio_display_C ((char *) idio_type2string (arg), msh);
    }
    idio_display_C (" to an execve argument: ", msh);
    idio_display_C (reason_C, msh);

    idio_error_raise_cont (idio_condition_rt_command_argv_type_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       arg));

    /* notreached */
}

static void idio_command_env_type_error (IDIO name, IDIO c_location)
{
    IDIO_ASSERT (name);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("environment variable '", msh);
    idio_display (name, msh);
    idio_display_C ("' is not a string", msh);

    idio_error_raise_cont (idio_condition_rt_command_env_type_error_type,
			   IDIO_LIST5 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       name,
				       idio_S_nil));

    /* notreached */
}

static void idio_command_format_error (char const *circumstance, char const *msg, IDIO var, IDIO val, IDIO c_location)
{
    IDIO_C_ASSERT (circumstance);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (var);
    IDIO_ASSERT (val);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, val);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (circumstance, msh);
    if (idio_S_nil != var) {
	idio_display_C (" '", msh);
	idio_display (var, msh);
	idio_display_C ("' ", msh);
    } else {
	idio_display_C (" ", msh);
    }
    idio_display_C (msg, msh);

    idio_error_raise_cont (idio_condition_rt_command_format_error_type,
			   IDIO_LIST5 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       var,
				       val));

    /* notreached */
}

void idio_command_not_found_error (char const *msg, IDIO cmd, IDIO c_location)
{
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (cmd);
    IDIO_ASSERT (c_location);

    IDIO_TYPE_ASSERT (list, cmd);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C (msg, msh);
    idio_display_C (" ", msh);
    idio_display (cmd, msh);

    idio_error_raise_cont (idio_condition_rt_command_error_type,
			   IDIO_LIST3 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh)));

    /* notreached */
}

/*
 * Code coverage:
 *
 * idio_command_exec_error() is only called if the execve(2) in %exec
 * fails (and it is no longer commented out...).
 */
static void idio_command_exec_error (char **argv, char **envp, IDIO c_location)
{
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh;
    IDIO lsh;
    IDIO dsh;
    idio_error_init (&msh, &lsh, &dsh, c_location);

    idio_display_C ("exec:", msh);
    int j;
    for (j = 0; NULL != argv[j]; j++) {
	/*
	 * prefix each argv[*] with a space
	 */
	idio_display_C (" ", msh);

	/*
	 * quote argv[*] if necessary
	 *
	 * XXX needs smarter quoting for "s, 's, etc.
	 *
	 * try:

	 char *qs = NULL;
	 qs = "\"";
	 idio_display_C (qs, msh);

	 */
	int q = 0;
	if (strchr (argv[j], ' ') != NULL) {
	    q = 1;
	}
	if (q) {
	    idio_display_C ("\"", msh);
	}
	idio_display_C (argv[j], msh);
	if (q) {
	    idio_display_C ("\"", msh);
	}
    }

    idio_error_raise_cont (idio_condition_rt_command_exec_error_type,
			   IDIO_LIST4 (idio_get_output_string (msh),
				       idio_get_output_string (lsh),
				       idio_get_output_string (dsh),
				       idio_C_int (errno)));

    /* notreached */
}

char *idio_command_string_C (IDIO var, IDIO val, size_t *sizep, char const *op_C, int *free_me_p, IDIO c_location)
{
    IDIO_ASSERT (var);
    IDIO_ASSERT (val);
    IDIO_C_ASSERT (sizep);
    IDIO_C_ASSERT (op_C);
    IDIO_C_ASSERT (free_me_p);

    *free_me_p = 0;

    if (idio_isa_symbol (val)) {
	*sizep = IDIO_SYMBOL_BLEN (val);
	return IDIO_SYMBOL_S (val);
    } else if (idio_isa_string (val)) {
	char *val_C = idio_string_as_C (val, sizep);

	/*
	 * Use size + 1 to avoid a truncation warning -- we're just
	 * seeing if val_C includes a NUL
	 */
	size_t C_size = idio_strnlen (val_C, *sizep + 1);
	if (C_size != *sizep) {
	    IDIO_GC_FREE (val_C, *sizep);

	    idio_command_format_error (op_C, "contains an ASCII NUL", var, val, c_location);

	    /* notreached */
	    return NULL;
	}
	*free_me_p = 1;

	return val_C;
    } else {
	/*
	 * Code coverage: coding error
	 */
	idio_error_param_type ("symbol|string", val, c_location);

	/* notreached */
	return NULL;
    }
}

/*
 * I'm not sure we can second guess strlen() calls here as the
 * environment is allowed to get "quite large" and the only limit is
 * whatever execve() places on it -- which presumes execve() is going
 * to get called.
 */
char **idio_command_get_envp (size_t *sizep)
{
    IDIO symbols = idio_module_visible_symbols (idio_thread_env_module (), idio_S_environ);
    size_t n = idio_list_length (symbols);

    *sizep = (n + 1) * sizeof (char *);
    char **envp = idio_alloc (*sizep);

    n = 0;
    while (idio_S_nil != symbols) {
	IDIO symbol = IDIO_PAIR_H (symbols);

	size_t slen = IDIO_SYMBOL_BLEN (symbol);

	/*
	 * NOTE
	 *
	 * We are recursing so that we pick up all environment
	 * variables including those defined by default in the Idio
	 * module (not just those in the current module).
	 *
	 * However, every environ variable will exist at the toplevel
	 * even if it was defined transiently inside a block (all
	 * environ variables have a unique gvi so we can distinguish
	 * them on the stack).
	 *
	 * That means we must ensure that, ultimately,
	 * idio_vm_environ_ref() doesn't invoke an error by passing a
	 * default value to the C function.  All environ variables
	 * must be a string so a boolean is a safe (and traditional)
	 * marker.
	 */
	IDIO val = idio_module_env_symbol_value_recurse (symbol, IDIO_LIST1 (idio_S_false));

	if (idio_S_false != val) {
	    if (idio_isa_string (val)) {
		int free_val_C = 0;

		/*
		 * Test Case: command-errors/env-format.idio
		 *
		 * IDIO-LIB :* join-string (make-string 1 #U+0) '("hello" "world")
		 * (env)
		 */
		size_t vlen;
		char *val_C = idio_command_string_C (symbol, val, &vlen, "environ", &free_val_C, IDIO_C_FUNC_LOCATION ());

		envp[n] = idio_alloc (slen + 1 + vlen + 1);
		memcpy (envp[n], IDIO_SYMBOL_S (symbol), slen);
		memcpy (envp[n] + slen, "=", 1);
		memcpy (envp[n] + slen + 1, val_C, vlen);
		envp[n][slen + 1 + vlen] = '\0';

		n++;

		if (free_val_C) {
		    IDIO_GC_FREE (val_C, vlen);
		}
	    } else {
		IDIO_GC_FREE (envp, *sizep);

		/*
		 * Test Case: command-errors/env-format.idio
		 *
		 * IDIO-LIB :* 'foo
		 * (env)
		 */
		idio_command_env_type_error (symbol, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }
	}

	symbols = IDIO_PAIR_T (symbols);
    }
    envp[n] = NULL;

    return envp;
}

char *idio_command_find_exe_C (char const *command, size_t const cmdlen, size_t *lenp)
{
    IDIO_C_ASSERT (command);
    IDIO_C_ASSERT (cmdlen > 0);
    IDIO_C_ASSERT (lenp);

    *lenp = 0;

    IDIO PATH = idio_module_current_symbol_value_recurse (idio_env_PATH_sym, idio_S_nil);

    int free_path_C = 0;
    char *path_C = NULL;
    size_t plen;
    char *path;
    char *pathe;
    if (idio_S_undef == PATH ||
	! idio_isa_string (PATH)) {
	/*
	 * Code coverage:
	 *
	 * Slightly tricky to get here from Idio.
	 *
	 * If PATH is unset then we'll get an error from
	 * idio_module_current_symbol_value_recurse() -- at the top of
	 * this function -- that the environment variable PATH is, er,
	 * unset.
	 *
	 * If PATH is not a string then we'll get a subsequent error
	 * when constructing the environment!
	 *
	 * A C unit test, then.
	 */
	path = idio_env_PATH_default;
	pathe = path + sizeof (path);
    } else {
	/*
	 * Test Case: command-errors/PATH-format.idio
	 *
	 * PATH :* join-string (make-string 1 #U+0) '("hello" "world")
	 * (env)
	 */
	path_C = idio_command_string_C (idio_env_PATH_sym, PATH, &plen, "find-exe", &free_path_C, IDIO_C_FUNC_LOCATION ());

	path = path_C;
	pathe = path + idio_string_len (PATH);
    }

    /*
     * See comment in libc-wrap.c re: getcwd(3)
     */
    char exename[PATH_MAX];
    char cwd[PATH_MAX];
    if (idio_getcwd ("find-exe/getcwd", cwd, PATH_MAX) == NULL) {
	if (free_path_C){
	    IDIO_GC_FREE (path_C, plen);
	}

	/*
	 * Test Case: command-errors/getcwd-rmdir.idio
	 *
	 * tmpdir := (make-tmp-dir)
	 * chdir tmpdir
	 * rmdir tmpdir
	 * (true)
	 */
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    size_t cwdlen = idio_strnlen (cwd, PATH_MAX);
    size_t exelen = 0;

    int done = 0;
    while (! done) {
	size_t pathlen = pathe - path;
	char * colon = NULL;

	if (0 == pathlen) {
	    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
		/*
		 * Test Case: command-errors/find-exe-cmd-PATH_MAX.idio
		 *
		 * PATH = ""
		 * %find-exe (string->symbol (make-string (C/->integer PATH_MAX) #\A))
		 */

		if (free_path_C) {
		    IDIO_GC_FREE (path_C, plen);
		}

		idio_error_system ("cwd+command exename length", NULL, IDIO_LIST2 (PATH, idio_string_C_len (command, cmdlen)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }

	    /*
	     * Code coverage:
	     *
	     * PATH = ""
	     */
	    memcpy (exename, cwd, cwdlen);
	    exename[cwdlen] = '\0';
	    exelen = cwdlen;
	} else {
	    colon = memchr (path, ':', pathlen);

	    if (NULL == colon) {
		if ((pathlen + 1 + cmdlen + 1) >= PATH_MAX) {
		    /*
		     * Test Case: command-errors/find-exe-dir-cmd-PATH_MAX-1.idio
		     *
		     * PATH = "foo"
		     * %find-exe (string->symbol (make-string (C/->integer PATH_MAX) #\A))
		     */

		    if (free_path_C) {
			IDIO_GC_FREE (path_C, plen);
		    }

		    idio_error_system ("PATH+command exename length", NULL, IDIO_LIST2 (PATH, idio_string_C_len (command, cmdlen)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		}

		memcpy (exename, path, pathlen);
		exename[pathlen] = '\0';
		exelen = pathlen;
	    } else {
		size_t dirlen = colon - path;

		if (0 == dirlen) {
		    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
			/*
			 * Test Case: command-errors/find-exe-dir-cmd-PATH_MAX-2.idio
			 *
			 * PATH = ":"
			 * %find-exe (string->symbol (make-string (C/->integer PATH_MAX) #\A))
			 */

			if (free_path_C) {
			    IDIO_GC_FREE (path_C, plen);
			}

			idio_error_system ("cwd+command exename length", NULL, IDIO_LIST2 (PATH, idio_string_C_len (command, cmdlen)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }

		    /*
		     * Code coverage:
		     *
		     * PATH = ":"
		     * PATH = ":foo"
		     * PATH = "foo:"
		     * PATH = "foo::bar"
		     */
		    memcpy (exename, cwd, cwdlen);
		    exename[cwdlen] = '\0';
		    exelen = cwdlen;
		} else {
		    if ((dirlen + 1 + cmdlen + 1) >= PATH_MAX) {
			/*
			 * Test Case: command-errors/find-exe-dir-cmd-PATH_MAX-3.idio
			 *
			 * PATH = "foo:"
			 * %find-exe (string->symbol (make-string (C/->integer PATH_MAX) #\A))
			 */

			if (free_path_C) {
			    IDIO_GC_FREE (path_C, plen);
			}

			idio_error_system ("dir+command exename length", NULL, IDIO_LIST2 (PATH, idio_string_C_len (command, cmdlen)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

			/* notreached */
			return NULL;
		    }

		    memcpy (exename, path, dirlen);
		    exename[dirlen] = '\0';
		    exelen = dirlen;
		}
	    }
	}

	memcpy (exename + exelen, "/", 1);
	memcpy (exename + exelen + 1, command, cmdlen);
	exelen += 1 + cmdlen;
	exename[exelen] = '\0';

	if (access (exename, X_OK) == 0) {
	    struct stat sb;

	    if (stat (exename, &sb) == -1) {
		/*
		 * Test Case: ??
		 *
		 * Can this fail if access(2) just above has
		 * succeeded?  Other than the obvious race condition
		 * of substituting an alternative exename between the
		 * two calls.
		 */

		if (free_path_C) {
		    IDIO_GC_FREE (path_C, plen);
		}

		idio_error_system_errno ("stat", idio_string_C_len (exename, exelen), IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
	    }

	    if (S_ISREG (sb.st_mode)) {
		done = 1;
		break;
	    }
	}

	if (NULL == colon) {
	    done = 1;
	    exename[0] = '\0';
	    break;
	}

	path = colon + 1;
    }

    char *pathname = NULL;

    if (0 != exename[0]) {
	pathname = idio_alloc (exelen + 1);
	memcpy (pathname, exename, exelen);
	pathname[exelen] = '\0';

	/*
	 * Now that we finally have an answer we can set *lenp
	 */
	*lenp = exelen;
    }

    if (free_path_C) {
	IDIO_GC_FREE (path_C, plen);
    }

    return pathname;
}

char *idio_command_find_exe (IDIO func, size_t *sizep)
{
    IDIO_ASSERT (func);

    int free_func_C = 0;

    /*
     * Test Cases:
     *   command-errors/find-exe-bad-format.idio
     *   command-errors/find-exe-bad-type.idio
     *
     * %find-exe (join-string (make-string 1 #U+0) '("hello" "world"))
     % %find-exe #t 2 3
     */
    char *func_C = idio_command_string_C (idio_S_nil, func, sizep, "command", &free_func_C, IDIO_C_FUNC_LOCATION ());

    if (strchr (func_C, '/') == NULL) {
	size_t rlen = 0;
	char *r = idio_command_find_exe_C (func_C, *sizep, &rlen);

	if (free_func_C) {
	    IDIO_GC_FREE (func_C, *sizep);
	}

	*sizep = rlen;
	return r;
    } else {
	/*
	 * Looks slightly pointless but our caller is going to free
	 * the value returned and we don't want to free {command}
	 * prematurely.
	 *
	 * The test was "../bin/idio empty" which runs fine but *we*
	 * accidentally returned {command}, the symbol ../bin/idio,
	 * which our caller frees.  We then get a second attempt to
	 * free it (from the symbol table) when the VM shuts down.
	 */
	char *cmdname = idio_alloc (*sizep + 1);
	memcpy (cmdname, func_C, *sizep);
	cmdname[*sizep] = '\0';

	if (free_func_C) {
	    IDIO_GC_FREE (func_C, *sizep);
	}

	return cmdname;
    }
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%find-exe", find_exe, (IDIO command), "command", "\
find `command` on :var:`PATH`			\n\
						\n\
:param command: command name			\n\
:type command: symbol or string			\n\
:return: pathname of `command`			\n\
:rtype: pathname				\n\
")
{
    IDIO_ASSERT (command);

    /*
     * idio_command_find_exe() via idio_command_string_C() will handle
     * either a symbol or a string
     */

    size_t pathname_len = 0;
    char *pathname = idio_command_find_exe (command, &pathname_len);
    if (NULL == pathname) {
	/*
	 * Test Case: command-errors/find-exe-not-found.idio
	 *
	 * create a temporary dirctory
	 * point PATH at that (and only that)
	 * %exec foo
	 */
	idio_command_not_found_error ("command not found", IDIO_LIST1 (command), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_pathname_C (pathname);

    IDIO_GC_FREE (pathname, pathname_len);

    return r;
}

static char *idio_command_glob_charp (char const *src)
{
    IDIO_C_ASSERT (src);

    while (*src) {
	switch (*src) {
	case '*':
	case '?':
	case '[':
	    return (char *) src;
	}
	src++;
    }

    return NULL;
}

/*
 * Globbing is a bit more tricky than at first blush.
 *
 * glob(3) is using C strings whereas we are using Idio strings.
 * There are two problems here:
 *
 * 1. We will convert the Idio string into a UTF-8 representation.
 *    That is technically wrong.  (The worst kind of wrong.)
 *
 *    As there are *no* -- precisely zero -- encoding specifications
 *    for *nix filenames then so is every other possible conversion
 *    from anything to a *nix filename.
 *
 *    About the nearest you'll get to a specification is that a *nix
 *    filename is a sequence of bytes excluding U+0 (ASCII NUL) and
 *    U+27 (SOLIDUS -- forward slash).  How you interpret those bytes
 *    is up to you.
 *
 *    So we'll roll with a UTF-8 conversion and be as wrong as the
 *    next guy.
 *
 * 2. We need to identify if no match occurred.  Well, glob(3) can
 *    tell us with GLOB_NOMATCH but how do we tell our caller?
 *
 *    In particular, how do we differentiate between the arg having no
 *    globbing metacharacters and having no matches: ab and a*b?
 *
 *    We'll return -1 for the no metacharacters case and 0 for no
 *    matches.  Bash has the nullglob option to handle the
 *    GLOB_NOMATCH case where you can chose to have the word removed
 *    from the arg list.
 *
 *    Of course, that makes this a signed return type.
 */

static ssize_t idio_command_possible_filename_glob (IDIO arg, glob_t *gp)
{
    IDIO_ASSERT (arg);

    int free_glob_C = 0;

    /*
     * Test Case: command-errors/glob-format.idio
     *
     * str := join-string (make-string 1 #U+0) '("hello" "world")
     * path-glob := make-struct-instance ~path str
     * env path-glob
     */
    size_t glen;
    char *glob_C = idio_command_string_C (idio_S_nil, arg, &glen, "glob", &free_glob_C, IDIO_C_FUNC_LOCATION ());

    char *match = idio_command_glob_charp (glob_C);

    size_t r = 0;

    if (NULL == match) {
	r = -1;
    } else {
	/*
	 * XXX Do not pass GLOB_NOCHECK in flags as we rely on
	 * capturing GLOB_NOMATCH.
	 */
	int glob_r = glob (glob_C, 0, NULL, gp);

	if (0 == glob_r) {
	    r = gp->gl_pathc;
	} else if (GLOB_NOMATCH == glob_r) {
	    /*
	     * gp->gl_pathc happens to be zero on Fedora when we get
	     * GLOB_NOMATCH -- can we rely on that elsewhere?
	     */
	    r = 0;
	} else {
	    /*
	     * Test Case: ?? command-errors/arg-glob-aborted.idio
	     *
	     * We need glob(3) to fail.  We're unlikely to run out of
	     * memory (by which I mean we're unlikely to arrange for
	     * this specific test to run out of memory just in time
	     * for glob(3)) and we specifically check for
	     * GLOB_NOMATCH.  That leaves GLOB_ABORTED.

	     * How to make glob fail with GLOB_ABORTED?  Hmm, the man
	     * page says "for a read error" and reading between the
	     * lines suggests you need to pass GLOB_ERR.
	     *
	     * Without GLOB_ERR, the default, is to "carry on despite
	     * errors, reading all of the directories it can."
	     *
	     * As hinted above, command-errors/arg-glob-aborted.idio
	     * will generate a GLOB_ABORTED *if* we pass GLOB_ERR.
	     * But we don't pass GLOB_ERR and the test is commented
	     * out so we don't get here.
	     */
	    if (free_glob_C) {
		IDIO_GC_FREE (glob_C, glen);
	    }

	    idio_command_glob_error (arg, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	}
    }

    if (free_glob_C) {
	IDIO_GC_FREE (glob_C, glen);
    }

    return r;
}

char **idio_command_argv (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_TYPE_ASSERT (list, args);

    /*
     * argv[] needs:
     * 1. path to command
     * 2+ arg1+
     * 3. NULL (terminator)
     *
     * Here we will flatten any lists and expand filename
     * patterns which means the arg list will grow as we
     * determine what each argument means.
     *
     * We know the basic size
     */
    int argc = 1 + idio_list_length (args) + 1;
    char **argv = idio_alloc (argc * sizeof (char *));
    int i = 1;		/* index into argv */

    while (idio_S_nil != args) {
	IDIO arg = IDIO_PAIR_H (args);

	switch ((intptr_t) arg & IDIO_TYPE_MASK) {
	case IDIO_TYPE_FIXNUM_MARK:
	    {
		size_t size = 0;
		argv[i++] = idio_display_string (arg, &size);
	    }
	    break;
	case IDIO_TYPE_CONSTANT_MARK:
	    {
		switch ((intptr_t) arg & IDIO_TYPE_CONSTANT_MASK) {
		case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		    {
			size_t size = 0;
			argv[i++] = idio_display_string (arg, &size);
		    }
		    break;
		default:
		    /*
		     * Test Case: command-errors/arg-constant-idio.idio
		     *
		     * env #t
		     */
		    idio_command_arg_type_error (arg, "inconvertible constant", IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		    break;
		}
	    }
	    break;
	case IDIO_TYPE_PLACEHOLDER_MARK:
	    /*
	     * Test Case: ??
	     *
	     * Requires a coding error.
	     */
	    idio_command_arg_type_error (arg, "inconvertible value", IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	    break;
	case IDIO_TYPE_POINTER_MARK:
	    {
		switch (idio_type (arg)) {
		case IDIO_TYPE_STRING:
		case IDIO_TYPE_SUBSTRING:
		    {
			int free_argv_i = 0;

			/*
			 * Test Case: command-errors/arg-string-format.idio
			 *
			 * env (join-string (make-string 1 #U+0) '("hello" "world"))
			 */
			size_t alen;
			argv[i] = idio_command_string_C (idio_S_nil, arg, &alen, "argument", &free_argv_i, IDIO_C_FUNC_LOCATION ());
			i++;
		    }
		    break;
		case IDIO_TYPE_SYMBOL:
		    {
			glob_t g;
			ssize_t n = idio_command_possible_filename_glob (arg, &g);

			if (n <= 0) {
			    /*
			     * Consider Bash-like nullglob, errglob
			     */
			    idio_asprintf (&argv[i++], "%s", IDIO_SYMBOL_S (arg));
			} else {
			    /*
			     * NB "gl_pathc - 1" as we reserved a slot
			     * for the original pattern so the
			     * increment is one less
			     */
			    argc += g.gl_pathc - 1;

			    argv = idio_realloc (argv, argc * sizeof (char *));

			    size_t n;
			    for (n = 0; n < g.gl_pathc; n++) {
				/*
				 * Can we know anything about the size
				 * of g.gl_pathv[n] to constrain the
				 * strnlen?  PATH_MAX?
				 */
				size_t plen = idio_strnlen (g.gl_pathv[n], PATH_MAX);
				argv[i] = idio_alloc (plen + 1);
				memcpy (argv[i], g.gl_pathv[n], plen);
				argv[i][plen] = '\0';

				i++;
			    }

			    globfree (&g);
			}
		    }
		    break;
		case IDIO_TYPE_PAIR:
		    {
			/*
			 * Pairs can be quite rich data structures but
			 * what we can accept is a flat list of
			 * strings (eg. pathnames from glob)
			 */
			int len = 0;
			int valid = 1;
			IDIO names = arg;
			while (idio_S_nil != names) {
			    IDIO name = IDIO_PAIR_H (names);
			    len++;

			    if (! idio_isa_string (name)) {
				valid = 0;
				break;
			    }

			    names = IDIO_PAIR_T (names);
			}

			if (0 == valid) {
			    /*
			     * Test Case: command-errors/arg-bad-list.idio
			     *
			     * env '(#t)
			     */
			    idio_command_arg_type_error (arg, "inconvertible list", IDIO_C_FUNC_LOCATION ());

			    /* notreached */
			    return NULL;
			}

			/*
			 * NB "len - 1" as we reserved a slot for the
			 * original list so the increment is one less
			 */
			argc += len - 1;

			argv = idio_realloc (argv, argc * sizeof (char *));

			names = arg;
			while (idio_S_nil != names) {
			    IDIO name = IDIO_PAIR_H (names);
			    int free_me = 0;
			    size_t namelen;
			    argv[i] = idio_command_string_C (idio_S_nil, name, &namelen, "argument", &free_me, IDIO_C_FUNC_LOCATION ());
			    i++;
			    names = IDIO_PAIR_T (names);
			}
		    }
		    break;
		case IDIO_TYPE_BIGNUM:
		case IDIO_TYPE_C_CHAR:
		case IDIO_TYPE_C_SCHAR:
		case IDIO_TYPE_C_UCHAR:
		case IDIO_TYPE_C_SHORT:
		case IDIO_TYPE_C_USHORT:
		case IDIO_TYPE_C_INT:
		case IDIO_TYPE_C_UINT:
		case IDIO_TYPE_C_LONG:
		case IDIO_TYPE_C_ULONG:
		case IDIO_TYPE_C_LONGLONG:
		case IDIO_TYPE_C_ULONGLONG:
		case IDIO_TYPE_C_FLOAT:
		case IDIO_TYPE_C_DOUBLE:
		case IDIO_TYPE_C_LONGDOUBLE:
		    {
			size_t size = 0;
			argv[i++] = idio_display_string (arg, &size);
		    }
		    break;
		case IDIO_TYPE_STRUCT_INSTANCE:
		    {
			IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (arg);
			if (idio_struct_type_isa (sit, idio_path_type)) {
			    IDIO pattern = idio_struct_instance_ref_direct (arg, IDIO_PATH_PATTERN);

			    glob_t g;
			    ssize_t n = idio_command_possible_filename_glob (pattern, &g);

			    if (n <= 0) {
				/*
				 * Consider Bash-like nullglob, errglob
				 */
				/*
				 * Code coverage:
				 *
				 * make-struct-instance ~path "a"	n == -1
				 * make-struct-instance ~path "*a"	n == 0
				 */
				size_t size = 0;
				argv[i++] = idio_display_string (pattern, &size);
			    } else {
				/*
				 * NB "gl_pathc - 1" as we reserved a slot
				 * for the original pattern so the
				 * increment is one less
				 */
				argc += g.gl_pathc - 1;

				argv = idio_realloc (argv, argc * sizeof (char *));

				size_t n;
				for (n = 0; n < g.gl_pathc; n++) {
				    /*
				     * Can we know anything about the
				     * size of g.gl_pathv[n] to
				     * constrain the strnlen?
				     * PATH_MAX?
				     */
				    size_t plen = idio_strnlen (g.gl_pathv[n], PATH_MAX);
				    argv[i] = idio_alloc (plen + 1);
				    memcpy (argv[i], g.gl_pathv[n], plen);
				    argv[i][plen] = '\0';

				    i++;
				}

				globfree (&g);
			    }
			} else {
			    /*
			     * Test Case: command-errors/arg-bad-struct.idio
			     *
			     * env %%last-job
			     */
			    idio_command_arg_type_error (arg, "only ~path structs", IDIO_C_FUNC_LOCATION ());

			    /* notreached */
			    return NULL;
			}
		    }
		    break;

		    /*
		     * No useful representation of the other types for
		     * a command we are about to exec().
		     */
		default:
		    /*
		     * Test Case: command-errors/arg-bad-value.idio
		     *
		     * env ^condition
		     */
		    idio_command_arg_type_error (arg, "inconvertible type", IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		    break;
		}
	    }
	    break;
	default:
	    /*
	     * Test Case: ??
	     *
	     * Coding error.
	     */
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object type: %s", idio_type2string (arg));

	    /* notreached */
	    return NULL;
	    break;
	}

	args = IDIO_PAIR_T (args);
    }
    argv[i] = NULL;

    return argv;
}

void idio_command_free_argv1 (char **argv)
{
    /*
     * NB don't free pathname, argv[0] -- we didn't allocate it
     *
     * XXX we've lost the argv[i] lengths
     */
    int j;
    for (j = 1; NULL != argv[j]; j++) {
	idio_free (argv[j]);
    }
    idio_free (argv);
}

/*
 * idio_vm_invoke() has spotted that the argument in functional
 * position is a symbol *and* that the "symbol" exists as an
 * executable entry on PATH so it's calling us to exec() the command
 * with whatever args were passed.
 *
 * It would be better if we could call fork-command, the Idio code
 * that does this, and not have everything duplicated but we're not
 * quite there yet.
 */
IDIO idio_command_invoke (IDIO name, IDIO thr, char const *pathname)
{
    IDIO val = IDIO_THREAD_VAL (thr);

    IDIO last = IDIO_FRAME_ARGS (val, IDIO_FRAME_NPARAMS (val));

    if (idio_S_nil != last) {
	/*
	 * Test Case: ??
	 *
	 * I can't remember what I was defending against, here.
	 */
	idio_error_param_value_exp ("invoke", "last arg", last, "#n", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /* IDIO_FRAME_NPARAMS (val) -= 1; */
    IDIO args = idio_frame_args_as_list (val);

    char **argv = idio_command_argv (args);

    if (idio_isa_symbol (name)) {
	argv[0] = IDIO_SYMBOL_S (name);
    } else if (idio_isa_string (name)) {
	size_t size = 0;
	argv[0] = idio_string_as_C (name, &size);
    } else {
	fprintf (stderr, "ici: name isa %s\n", idio_type2string (name));
	argv[0] = (char *) pathname;
    }

    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * We're going to call idio_vm_invoke_C() to get some things done
     * which may in turn call idio_gc_collect().  That means that any
     * IDIO objects we have here are at risk of being collected unless
     * we protect them.
     *
     * Given we have a hotchpotch of objects, create an array and just
     * push anything we want to keep on the end.
     *
     * We don't want to use idio_gc_protect(), here, because of
     * continuations which might be fired when the VM runs.  If they
     * do get fired then nothing will idio_gc_expose() the protected
     * array.
     *
     * So, we want to push the array itself on the stack (and pop it
     * off again when we're done).
     *
     * If a continuation is invoked then the stack should be safely
     * cleared (in practice, replaced) for us.
     */
    IDIO protected = idio_array (10);
    idio_array_push (stack, protected);

    IDIO command = idio_list_append2 (IDIO_LIST1 (name), args);
    idio_array_push (protected, command);

    IDIO proc = idio_struct_instance (idio_job_control_process_type,
				      IDIO_LIST6 (command,
						  idio_S_nil,
						  idio_C_int (-1),
						  idio_S_false,
						  idio_S_false,
						  idio_S_nil));
    idio_array_push (protected, proc);

    IDIO cmd_sym;
    cmd_sym = idio_module_symbol_value (idio_S_stdin_fileno, idio_libc_module, idio_S_nil);
    IDIO job_stdin = idio_vm_invoke_C (thr, IDIO_LIST1 (cmd_sym));
    idio_array_push (protected, job_stdin);

    IDIO job_stdin_fd = job_stdin;
    IDIO close_stdin = idio_S_false;
    if (idio_isa_pair (job_stdin)) {
	close_stdin = IDIO_PAIR_H (job_stdin);
	job_stdin_fd = idio_C_int (IDIO_FILE_HANDLE_FD (close_stdin));
    }

    cmd_sym = idio_module_symbol_value (idio_S_stdout_fileno, idio_libc_module, idio_S_nil);
    IDIO job_stdout = idio_vm_invoke_C (thr, IDIO_LIST1 (cmd_sym));
    idio_array_push (protected, job_stdout);

    IDIO recover_stdout = idio_S_false;
    if (idio_isa_pair (job_stdout)) {
	recover_stdout = IDIO_PAIR_HT (job_stdout);
	job_stdout = IDIO_PAIR_H (job_stdout);
    }

    cmd_sym = idio_module_symbol_value (idio_S_stderr_fileno, idio_libc_module, idio_S_nil);
    IDIO job_stderr = idio_vm_invoke_C (thr, IDIO_LIST1 (cmd_sym));
    idio_array_push (protected, job_stderr);

    IDIO recover_stderr = idio_S_false;
    if (idio_isa_pair (job_stderr)) {
	recover_stderr = IDIO_PAIR_HT (job_stderr);
	job_stderr = IDIO_PAIR_H (job_stderr);
    }

    /*
     * That was the last call to idio_vm_invoke_C() in this function
     * but idio_command_launch_1proc_job() also calls
     * idio_vm_invoke_C() so keep protecting objects!
     */

    /*
     * some job accounting
     */
    struct timeval *tvp = (struct timeval *) idio_alloc (sizeof (struct timeval));

    if (-1 == gettimeofday (tvp, NULL)) {
	idio_error_system_errno ("gettimeofday", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    struct rusage *rusage_selfp = (struct rusage *) idio_alloc (sizeof (struct rusage));

    if (-1 == getrusage (RUSAGE_SELF, rusage_selfp)) {
	idio_error_system_errno ("getrusage", idio_C_int (RUSAGE_SELF), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    struct rusage *rusage_childrenp = (struct rusage *) idio_alloc (sizeof (struct rusage));

    if (-1 == getrusage (RUSAGE_CHILDREN, rusage_childrenp)) {
	idio_error_system_errno ("getrusage", idio_C_int (RUSAGE_CHILDREN), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO timing_start = IDIO_LIST3 (idio_C_pointer_type (idio_CSI_libc_struct_timeval, tvp),
				    idio_C_pointer_type (idio_CSI_libc_struct_rusage, rusage_selfp),
				    idio_C_pointer_type (idio_CSI_libc_struct_rusage, rusage_childrenp));

    /*
     * When ^rt-command-status-error is being suppressed we 1) don't
     * want to raise an rcse (duh) but 2) we don't want the stock
     * wait-for-job/do-job-notification routines to alert us that the
     * job has failed.
     *
     * For 1) we pass the inverse of suppress_rcse and for 2) we pass
     * suppress_rcse as notify-completed as that (non-false) value
     * means the code won't run.
     */
    IDIO inverted_suppress_rcse = idio_command_suppress_rcse == idio_S_false ? idio_S_true : idio_S_false;

    IDIO job = idio_struct_instance (idio_job_control_job_type,
				     idio_listv (16,
						 command,
						 IDIO_LIST1 (proc),
						 idio_C_int (0),
						 idio_S_false,
						 idio_S_false,
						 inverted_suppress_rcse,
						 idio_command_suppress_rcse,
						 idio_S_nil,
						 job_stdin_fd,
						 job_stdout,
						 job_stderr,
						 idio_S_false,
						 timing_start,
						 idio_S_false,
						 idio_S_false,
						 idio_S_true));
    idio_array_push (protected, job);

    IDIO r = idio_job_control_launch_1proc_job (job, 1, pathname, argv, args);

    /*
     * OK, we're in the clear now!
     */
    idio_array_pop (stack);

    /*
     * Recovering [the data for] standard I/O handles.  This is when
     * we've intended to direct the output of a standard *nix command
     * into a, say, string handle.  *nix commands obviously(?) do not
     * know anything about string handles they only know about file
     * descriptors.  So we've taken the liberty of redirecting the
     * command's output to a temporary file and intend to replay that
     * output into the desired string handle.
     *
     * stdin is pretty easy, we just close the temporary input handle.
     *
     * Erm, except we must close the file handle if it was supplied
     * (as the first element of the list from stdin-fileno).  If we
     * just close the FD then we have a file handle which will get
     * finalized in the future.  However, the FD in that file handle
     * is free to be reused.  We can get caught out by a newly created
     * file handle using FD being closed under its feet when the GC
     * reaps the one left over on the heap.
     *
     * For stdout and stderr, we've sent the output to a temporary
     * file which we hold as job_stdout/job_stderr.  We need to open
     * that file, read the contents and print them to the original
     * stdout/stderr which we hold as recover_stdout/recover_stderr.
     *
     * Technically, we already have the temporary file open as
     * job_std* should be a C file descriptor open onto the (now
     * unlinked) temporary file.  In practice we want a FILE* to that
     * file descriptor -- for reasons I don't quite remember.
     *
     * Even more technically, since we've just finished writing to
     * that temporary file the file pointer is at the end of the file
     * so we need to fseek(3) back to the beginning.
     */

    if (idio_S_false != close_stdin) {
	if (idio_close_handle (close_stdin) < 0) {
	    if (EBADF == errno) {
		char em[BUFSIZ];
		snprintf (em, BUFSIZ, "[%d] ici 0: close (%d): EBADF", getpid (), IDIO_FILE_HANDLE_FD (close_stdin));
		perror (em);
	    } else {
		/*
		 * Test Case: ??
		 *
		 * This is a file descriptor opened onto a temporary file
		 * (which has been unlinked).
		 *
		 * It's not obvious how you provoke close(2) into failing
		 * under these circumstances.
		 */
		idio_command_free_argv1 (argv);

		idio_error_system_errno ("close", job_stdin, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	}
    }

    if (idio_S_false != recover_stdout) {
	FILE *filep = fdopen (IDIO_C_TYPE_int (job_stdout), "r");

	if (NULL == filep) {
	    /*
	     * Test Case: ??
	     *
	     * See above.
	     */
	    idio_command_free_argv1 (argv);

	    idio_error_system_errno ("fdopen", job_stdout, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * NB the temporary file has just been written to so the file
	 * pointer is currently at the end, we need to set it back to
	 * the start to be able to read anything!
	 */
	if (fseek (filep, 0, SEEK_SET) < 0) {
	    /*
	     * Test Case: ??
	     *
	     * See above.
	     */
	    idio_command_free_argv1 (argv);

	    idio_error_system_errno_msg ("fseek", "0 SEEK_SET", job_stdout, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	int done = 0;

	while (! done) {
	    int c = fgetc (filep);

	    switch (c) {
	    case EOF:
		done = 1;
		break;
	    default:
		idio_putb_handle (recover_stdout, c);
		break;
	    }
	}

	if (-1 == fclose (filep)) {
	    /*
	     * Test Case: ??
	     *
	     * See above.
	     */
	    idio_command_free_argv1 (argv);

	    idio_error_system_errno_msg ("fclose", "<job stdout>", job_stdout, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    if (idio_S_false != recover_stderr) {
	FILE *filep = fdopen (IDIO_C_TYPE_int (job_stderr), "r");

	if (NULL == filep) {
	    idio_command_free_argv1 (argv);

	    /*
	     * Test Case: ??
	     *
	     * See above.
	     */
	    idio_error_system_errno ("fdopen", job_stderr, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	/*
	 * NB the temporary file has just been written to so the file
	 * pointer is currently at the end, we need to set it back to
	 * the start to be able to read anything!
	 */
	if (fseek (filep, 0, SEEK_SET) < 0) {
	    idio_command_free_argv1 (argv);

	    /*
	     * Test Case: ??
	     *
	     * See above.
	     */
	    idio_error_system_errno_msg ("fseek", "0 SEEK_SET", job_stderr, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	int done = 0;

	while (! done) {
	    int c = fgetc (filep);

	    switch (c) {
	    case EOF:
		done = 1;
		break;
	    default:
		idio_putb_handle (recover_stderr, c);
		break;
	    }
	}

	if (-1 == fclose (filep)) {
	    idio_command_free_argv1 (argv);

	    /*
	     * Test Case: ??
	     *
	     * See above.
	     */
	    idio_error_system_errno_msg ("fclose", "<job stderr>", job_stdout, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    idio_command_free_argv1 (argv);

    return r;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("%exec", exec, (IDIO command, IDIO args), "command [args]", "\
exec `command` `args`				\n\
						\n\
:param command: command name			\n\
:type command: symbol				\n\
:param args: arguments				\n\
:type args: list, optional			\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_ASSERT (command);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (symbol, command);

    size_t pathname_len = 0;
    char *pathname = idio_command_find_exe (command, &pathname_len);
    if (NULL == pathname) {
	/*
	 * Test Case: command-errors/exec-not-found.idio
	 *
	 * create a temporary dirctory
	 * point PATH at that (and only that)
	 * %find-exe foo
	 */
	idio_command_not_found_error ("command not found", IDIO_LIST2 (command, args), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /*
     * Code coverage:
     *
     * The very point of %exec is to run another process in place of
     * this one which means our test is over and, more particularly,
     * we're not running the code-coverage executable any more so
     * there's no logging that we made it here.
     *
     * I guess some manual testing will have to do:
     *
     * module command
     * %exec true
     */
    char **argv = idio_command_argv (args);

    argv[0] = pathname;

    size_t envp_len = 0;
    char **envp = idio_command_get_envp (&envp_len);

    execve (argv[0], argv, envp);
    perror ("execv");

    /*
     * Test Case: ??
     *
     * Can we provoke execve(2) to fail?
     */

    /* idio_command_exec_error (argv, envp, IDIO_C_FUNC_LOCATION ()); */

    exit (1);
    return idio_S_notreached;
}

void idio_command_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_command_module, find_exe);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_command_module, exec);
}

void idio_init_command ()
{
    idio_module_table_register (idio_command_add_primitives, NULL, NULL);

    idio_command_module = idio_module (IDIO_SYMBOL ("command"));
}

