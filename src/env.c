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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <errno.h>
#include <ffi.h>
#include <pwd.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "command.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "handle.h"
#include "idio-string.h"
#include "libc-wrap.h"
#include "module.h"
#include "pair.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "util.h"
#include "vm.h"

/*
 * We need sysctl(3) for discovering the FreeBSD exe pathname and
 * _NSGetExecutablePath for Mac OS X
 */
#if defined (BSD)
#include <sys/sysctl.h>
#elif defined (__APPLE__) && defined (__MACH__)
#include <mach-o/dyld.h>
#endif

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

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_rt_environ_variable_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       detail,
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
	    size_t name_len = e - *env;
	    char *name = idio_alloc (name_len + 1);
	    memcpy (name, *env, name_len);
	    name[name_len] = '\0';

	    var = idio_symbols_C_intern (name, name_len);
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
     * internally at the very least and not having one is likely to be
     * described as "an issue" for a shell.
     *
     * So we'll test for some key environment variables we need and
     * set a default if there isn't one.
     *
     * PATH
     * PWD
     * IDIOLIB
     * HOME
     * SHELL
     */

    idio_env_set_default (idio_env_PATH_sym, idio_env_PATH_default);

    /*
     * See comment in libc-wrap.c re: getcwd(3)
     */
    char *cwd = idio_getcwd ("environ/getcwd", NULL, PATH_MAX);
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

    /*
     * From getpwuid(3) on CentOS
     */

    struct passwd pwd;
    struct passwd *pwd_result;
    char *pwd_buf;
    size_t pwd_bufsize;

    pwd_bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
    if (pwd_bufsize == -1) {
	pwd_bufsize = 16384;
    }

    pwd_buf = idio_alloc (pwd_bufsize);

    int pwd_exists = 1;
#if defined (__sun) && defined (__SVR4)
    errno = 0;
    pwd_result = getpwuid_r (getuid (), &pwd, pwd_buf, pwd_bufsize);
    if (pwd_result == NULL) {
	if (errno) {
	    idio_error_warning_message ("user ID %d is not in the passwd database\n", getuid ());
	}
	pwd_exists = 0;
    }
#else
    int pwd_s = getpwuid_r (getuid (), &pwd, pwd_buf, pwd_bufsize, &pwd_result);
    if (pwd_result == NULL) {
	if (pwd_s) {
	    errno = pwd_s;
	    idio_error_warning_message ("user ID %d is not in the passwd database\n", getuid ());
	}
	pwd_exists = 0;
    }
#endif

    /*
     * POSIX is a bit free with environment variables:
     * https://pubs.opengroup.org/onlinepubs/9699919799/ which appears
     * to be someone typing "env | sort" and adding them to the
     * specification as known environment variables.
     *
     * Why would SECONDS or RANDOM be in the environment?
     */
    char *LOGNAME = "";
    char *HOME = "";
    char *SHELL = "";
    if (pwd_exists) {
	LOGNAME = pwd.pw_name;
	HOME = pwd.pw_dir;
	SHELL = pwd.pw_shell;
    }

#define IDIO_ENV_EXPORT(name)						\
    if (getenv (#name) == NULL) {					\
	IDIO sym = idio_symbols_C_intern (#name, sizeof (#name) - 1);	\
	if (idio_env_set_default (sym, name) == 0) {			\
	    idio_module_env_set_symbol_value (sym, idio_string_C (name)); \
	}								\
    }

    IDIO_ENV_EXPORT (LOGNAME);
    IDIO_ENV_EXPORT (HOME);
    IDIO_ENV_EXPORT (SHELL);

    IDIO_GC_FREE (pwd_buf);
}

/*
 * Figure out the pathname of the currently running executable.
 *
 * We should prefer to use kernel interfaces -- which are, of course,
 * operating system-bespoke -- before falling back to figuring
 * something out from argv[0].
 *
 * argv[0] has issues in that no-one is obliged to use the executable
 * pathname for argv[0] in exec*(2) calls.  Hence our preference for
 * kernel interfaces which, presumably, are not fooled by such, er,
 * tomfoolery.
 *
 * - argv0 is argv[0]
 *
 * - a0rp (argv0_realpath) is a buffer of PATH_MAX characters which we
 *   copy the result into
 */
void idio_env_exe_pathname (const char *argv0, const size_t argv0_len, char *a0rp, size_t a0rp_len)
{
    IDIO_C_ASSERT (argv0);
    IDIO_C_ASSERT (argv0_len > 0);
    IDIO_C_ASSERT (a0rp);
    IDIO_C_ASSERT (a0rp_len > 0);

    /*
     * Annoyingly, we want to replace argv0 and argv0_len in the SunOS
     * section to reuse the generic argv0 code which means we drop the
     * const or use local non-const references
     */
    char *a0 = (char *) argv0;
    size_t a0_len = (size_t) argv0_len;

    /*
     * These operating system-bespoke sections are from
     * https://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe.
     * I guess there are more.
     */

    a0rp[0] = '\0';
#if defined (BSD)
#if defined (KERN_PROC_PATHNAME)
    /*
     * FreeBSD may or may not have procfs, the alternative is
     * sysctl(3)
     */
    int names[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t pm = PATH_MAX;
    int r = sysctl (names, 4, a0rp, &pm, NULL, 0);
    if (0 == r) {
	return;
    } else {
	perror ("sysctl CTL_KERN KERN_PROC KERN_PROC_PATHNAME");
    }
#endif
#elif defined (__linux__)
    ssize_t r = readlink ("/proc/self/exe", a0rp, a0rp_len);
    if (r > 0) {
	/*
	 * 1. Can a link be zero length?
	 *
	 * 2. readlink(2) hasn't added a NUL byte and could have
	 * truncated the symlink's value.
	 */
	if (PATH_MAX == r) {
	    /*
	     * Hmm, we're truncating our own name, which gives the
	     * feeling we should be failing.
	     *
	     * Quite how the kernel managed to run a command whose
	     * name is longer than PATH_MAX is the right question.
	     */
	    a0rp[r] = '\0';
	} else {
	    a0rp[r] = '\0';
	}
	return;
    } else {
	perror ("readlink /proc/self/exe");
    }
#elif defined (__APPLE__) && defined (__MACH__)
    uint32_t bufsiz = a0rp_len;
    int r = _NSGetExecutablePath (a0rp, &bufsiz);
    if (0 == r) {
	return;
    } else {
	perror ("_NSGetExecutablePath");
    }
#elif defined (__sun) && defined (__SVR4)
    const char *r = getexecname ();
    if (NULL != r) {
	if ('/' == r[0]) {
	    /* absolute pathname */
	    memcpy (a0rp, r, a0rp_len);
	    a0rp[a0rp_len] = '\0'; /* just in case */

	    return;
	} else {
	    /*
	     * relative
	     *
	     * We're about to do the right thing with argv0, below,
	     * but have a better value (hopefully) in r, now.  Rather
	     * than duplicate code have a0 be r.
	     */
	    a0 = (char *) r;
	    a0_len = idio_strnlen (r, PATH_MAX);
	}
    }
#else
#error No OS-bespoke exec pathname variant
#endif

    /*
     * Fallback to looking for a0 either relative to us or on the PATH
     */
    char *dir = rindex (a0, '/');

    if (NULL == dir) {
	/*
	 * Test Case: ??
	 *
	 * Actually, the problem with the test case isn't that we
	 * can't get here, we simply need to invoke idio from a
	 * directory on the PATH but that we can't invoke *both* an
	 * explicit exec, ".../idio", and an implicit one,
	 * "PATH=... idio", in the same test suite.
	 */
	a0 = idio_command_find_exe_C (a0, a0_len);
    }

    char *rp = realpath (a0, a0rp);

    if (NULL == rp) {
	/*
	 * Test Case: ??
	 *
	 * Like the getcwd(3) case, above, this is hard to emulate
	 * during bootstrap.
	 */
	idio_error_system_errno_msg ("realpath", "=> NULL", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    a0rp[a0rp_len] = '\0';	/* just in case */
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
void idio_env_init_idiolib (const char *argv0, const size_t argv0_len)
{
    IDIO_C_ASSERT (argv0);

    char a0rp[PATH_MAX];
    idio_env_exe_pathname (argv0, argv0_len, a0rp, PATH_MAX);

    /*
     * While we are here, set IDIO_CMD and IDIO_EXE.
     */
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_CMD"), idio_string_C (argv0), idio_Idio_module_instance ());
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_EXE"), idio_string_C (a0rp), idio_Idio_module_instance ());

    /*
     * If there's no existing IDIOLIB environment variable then set
     * one now
     */
    idio_env_set_default (idio_env_IDIOLIB_sym, idio_env_IDIOLIB_default);

    /*
     * Were we launched with a specific executable name (absolute or
     * relative)?  We use argv0, here, rather than the computed a0rp.
     */
    char *rel = rindex (argv0, '/');

    if (NULL == rel) {
	/*
	 * Found on the PATH so we are happy with IDIOLIB
	 * (user-supplied or system default).
	 */
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
    char *dir = rindex (a0rp, '/');

    if (NULL == dir) {
	/*
	 * Possible if idio_env_exe_pathname() dun goofed.
	 */
	return;
    }

    char *pdir = dir - 1;
    while (pdir > a0rp &&
	   '/' != *pdir) {
	pdir--;
    }

    if (pdir >= a0rp) {
	if (strncmp (pdir, "/bin", 4) == 0) {
	    /*
	     * From .../bin we can derive .../lib
	     *
	     * This is the implicit idio-exe IDIOLIB directory, ieId
	     */
	    size_t ddd_len = pdir - a0rp;
	    size_t ieId_len = ddd_len + 4;
	    char *ieId = idio_alloc (ieId_len + 1);
	    memcpy (ieId, a0rp, ddd_len);
	    memcpy (ieId + ddd_len, "/lib", 4);
	    ieId[ddd_len + 4] = '\0';

	    IDIO idiolib = idio_module_env_symbol_value (idio_env_IDIOLIB_sym, IDIO_LIST1 (idio_S_false));

	    size_t idiolib_len = 0;
	    char *idiolib_C = idio_string_as_C (idiolib, &idiolib_len);

	    /*
	     * Use idiolib_len + 1 to avoid a truncation warning --
	     * we're just seeing if idiolib_C includes a NUL
	     */
	    size_t C_size = idio_strnlen (idiolib_C, idiolib_len + 1);
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
		IDIO_GC_FREE (idiolib_C);

		idio_env_format_error ("bootstrap", "contains an ASCII NUL", idio_env_IDIOLIB_sym, idiolib, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return;
	    }

	    char *index = strstr (idiolib_C, ieId);
	    int prepend = 0;
	    if (index) {
		/*
		 * Code coverage:
		 *
		 * These should be picked up if a pre-existing IDIOLIB
		 * is already one of:
		 *
		 * .../lib
		 * .../lib:...
		 *
		 * Both of which will fall into the manual test
		 * category.
		 *
		 * We do these extra checks in case someone has added
		 * .../libs -- which index will also match.
		 */
		if (! ('\0' == index[ieId_len] ||
		       ':' == index[ieId_len])) {
		    prepend = 1;
		}
	    } else {
		/*
		 * Code coverage:
		 *
		 * IDIOLIB is set but doesn't have .../lib
		 */
		prepend = 1;
	    }

	    if (prepend) {
		size_t ni_len = ieId_len + 1 + idiolib_len + 1;
		if (0 == idiolib_len) {
		    ni_len = ieId_len + 1;
		}
		char *ni = idio_alloc (ni_len + 1);
		memcpy (ni, ieId, ieId_len);
		memcpy (ni + ieId_len, ":", 1);
		if (idiolib_len) {
		    memcpy (ni + ieId_len + 1, idiolib_C, idiolib_len);
		}
		ni[ni_len] = '\0';

		idio_module_env_set_symbol_value (idio_env_IDIOLIB_sym, idio_string_C (ni));
		IDIO_GC_FREE (ni);
	    }

	    IDIO_GC_FREE (idiolib_C);
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
    idio_module_table_register (idio_env_add_primitives, idio_final_env, NULL);

    idio_env_IDIOLIB_sym = IDIO_SYMBOLS_C_INTERN ("IDIOLIB");
    idio_env_PATH_sym = IDIO_SYMBOLS_C_INTERN ("PATH");
    idio_env_PWD_sym = IDIO_SYMBOLS_C_INTERN ("PWD");

    /*
     * /usr/lib/{pkg} seems pretty universal -- but it might not be.
     * Hence the use of idiolib_default which might be different on
     * some systems before copying that into idio_env_IDIOLIB_default.
     */
    char *idiolib_default = "/usr/lib/idio";
    size_t id_len = sizeof (idiolib_default) - 1;
    idio_env_IDIOLIB_default = idio_alloc (id_len + 1);
    memcpy (idio_env_IDIOLIB_default, idiolib_default, id_len);
    idio_env_IDIOLIB_default[id_len] = '\0';
}

