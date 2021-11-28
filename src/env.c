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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "idio-system.h"

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
#include "path.h"
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

#define IDIO_ENV_LIB_BASE	"/lib/idio"

char *idio_env_PATH_default = "/bin:/usr/bin";
char *idio_env_IDIOLIB_default = NULL;
size_t idio_env_IDIOLIB_default_len;
IDIO idio_env_IDIOLIB_sym;
IDIO idio_env_PATH_sym;
IDIO idio_env_PWD_sym;

/*
 * Code coverage:
 *
 * This code will only get called if IDIOLIB has an ASCII NULL in it
 * which...is unlikely.
 */
void idio_env_format_error (char const *circumstance, char const *msg, IDIO name, IDIO val, IDIO c_location)
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

static int idio_env_set_default (IDIO name, char const *val)
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
	idio_module_env_set_symbol_value (idio_env_PWD_sym, idio_pathname_C (cwd));
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
 * Following in the guise of
 * https://stackoverflow.com/questions/4774116/realpath-without-resolving-symlinks
 *
 * Here the result is always allocated and we'll set the returned
 * value's length in rlenp (which may be less than the allocated
 * length).
 */
char *idio_env_normalize_path (char const *path, size_t const path_len, size_t *rlenp)
{
    IDIO_C_ASSERT (path);
    IDIO_C_ASSERT (path_len > 0);
    IDIO_C_ASSERT (rlenp);

    char *r;
    size_t rlen;

    char const *p;
    char const *pe = path + path_len;
    char const *slash;

    if ('/' != path[0]) {
	char *cwd = getcwd (NULL, PATH_MAX);

	if (NULL == cwd) {
	    *rlenp = 0;
	    return NULL;
	}

	size_t cwd_len = strnlen (cwd, PATH_MAX);
	r = idio_alloc (cwd_len + 1 + path_len + 1);
	memcpy (r, cwd, cwd_len);
	rlen = cwd_len;
	free (cwd);
    } else {
	r = idio_alloc (path_len + 1);
	rlen = 0;		/* nothing so far */
    }

    for (p = path; p < pe; p = slash + 1) {
	slash = memchr (p, '/', pe - p);
	if (NULL == slash) {
	    slash = pe;
	}

	size_t plen = slash - p;
	switch (plen) {
	case 2:			/* /../ ? */
	    if ('.' == p[0] &&
		'.' == p[1]) {
		char const *pslash = memrchr (r, '/', rlen);
		if (NULL != pslash) {
		    rlen = pslash - r;
		}
		continue;
	    }
	    break;
	case 1:			/* /./ ? */
	    if ('.' == p[0]) {
		continue;
	    }
	    break;
	case 0:			/* // */
	    continue;
	}

	r[rlen++] = '/';
	memcpy (r + rlen, p, plen);
	rlen += plen;
    }

    if (0 == rlen) {
	r[rlen++] = '/';
    }
    r[rlen] = '\0';
    *rlenp = rlen;
    return r;
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
 * However, we want, if we can, to realpath (argv[0]) anyway in case
 * it is a symlink as in a virtualenv.
 *
 * - argv0 is argv[0]
 *
 * - a0rp (argv0_realpath) is a buffer of a0rp_len characters which we
 *   copy the realpath(argv0) result into
 *
 * - erp (executable_realpath) is a buffer of erp_len characters which
 *   we copy the system-specific running executable name into
 *
 * - linkp is whether a0rp is a symlink
 *
 * Note that we can't (won't) (re-)calculate the a0rp_len and erp_len.
 * For some OS' we can them via system calls which means someone has
 * to strnlen() them.
 */
void idio_env_exe_pathname (char const *argv0, size_t const argv0_len, char *a0rp, size_t a0rp_len, char *erp, size_t erp_len, int *linkp)
{
    IDIO_C_ASSERT (argv0);
    IDIO_C_ASSERT (argv0_len > 0);
    IDIO_C_ASSERT (a0rp);
    IDIO_C_ASSERT (a0rp_len > 0);
    IDIO_C_ASSERT (erp);
    IDIO_C_ASSERT (erp_len > 0);
    IDIO_C_ASSERT (linkp);

    a0rp[0] = '\0';

    if ('/' == argv0[0]) {
	memcpy (a0rp, argv0, argv0_len);
	a0rp[argv0_len] = '\0';
    }

    /*
     * Annoyingly, we want to replace argv0 and argv0_len in the SunOS
     * section to reuse the generic realpath (argv0) with a better
     * argv0, if you like.
     *
     * That means we drop the const for argv0 or use local non-const
     * references.
     *
     * We'll use e0 for exe0, our better argv0 (in SunOS, possibly).
     */
    char *e0 = (char *) argv0;
    size_t e0_len = (size_t) argv0_len;

    /*
     * These operating system-bespoke sections are from
     * https://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe.
     * I guess there are more.
     */

    erp[0] = '\0';
#if defined (BSD)
#if defined (KERN_PROC_PATHNAME)
    /*
     * FreeBSD may or may not have procfs, the alternative is
     * sysctl(3)
     */
    int names[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t pm = erp_len;
    int r = sysctl (names, 4, erp, &pm, NULL, 0);
    if (0 != r) {
	perror ("sysctl CTL_KERN KERN_PROC KERN_PROC_PATHNAME");
    }
#endif
#elif defined (__linux__)
    ssize_t r = readlink ("/proc/self/exe", erp, erp_len);
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
	    erp[r-1] = '\0';
	} else {
	    erp[r] = '\0';
	}
    } else {
	perror ("readlink /proc/self/exe");
    }
#elif defined (__APPLE__) && defined (__MACH__)
    uint32_t bufsiz = erp_len;
    int r = _NSGetExecutablePath (erp, &bufsiz);
    if (0 != r) {
	perror ("_NSGetExecutablePath");
    }
#elif defined (__sun) && defined (__SVR4)
    char const *r = getexecname ();
    if (NULL != r) {
	if ('/' == r[0]) {
	    /*
	     * absolute pathname
	     *
	     * strnlen rather than idio_strnlen during bootstrap
	     */
	    size_t rlen = strnlen (r, erp_len);
	    memcpy (erp, r, rlen);
	    erp[rlen] = '\0';
	} else {
	    /*
	     * relative
	     *
	     * We're about to do the right thing with e0, below, but
	     * have a better value (hopefully) in r, now.  Rather than
	     * duplicate code have e be r.
	     */
	    e0 = (char *) r;
	    e0_len = idio_strnlen (r, erp_len);
	}
    }
#else
#error No OS-bespoke exec pathname variant
#endif

    /*
     * Fallback to looking for e0 either relative to us or on the PATH
     */
    char *dir = memrchr (e0, '/', e0_len);

    size_t elen = e0_len;
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
	e0 = idio_command_find_exe_C (e0, e0_len, &elen);
    }

    /*
     * If argv0 is not an absolute path then we'll copy this
     * found-on-the-PATH value
     */
    if ('/' != argv0[0]) {
	if (elen >= a0rp_len) {
	    elen = a0rp_len - 1;
	}
	memcpy (a0rp, e0, elen);
	a0rp[elen] = '\0';
    }

    /*
     * We have a slight problem, here, in that PATH could have had
     * a relative element, PATH=$PATH:./bin in which case
     * a0rp is ./bin/idio.
     *
     * Worse, is that realpath(3) resolves symlinks
     */
    if ('/' != a0rp[0]) {
	size_t n_len = 0;
	char *n_a0rp = idio_env_normalize_path (a0rp, elen, &n_len);
	if (n_len < a0rp_len) {
	    memcpy (a0rp, n_a0rp, n_len);
	    a0rp[n_len] = '\0';
	}
	IDIO_GC_FREE (n_a0rp);
    }

    char *e0_rp = realpath (e0, erp);

    if (NULL == e0_rp) {
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

    erp[erp_len - 1] = '\0';	/* just in case */

    /*
     * Finally, we should have an a0rp, now, either from realpath() or
     * from being found on PATH, so let's see if it was a symlink
     */
    struct stat sb;

    *linkp = 0;

    if (lstat (a0rp, &sb) == 0 &&
	S_ISLNK (sb.st_mode)) {
	*linkp = 1;
    }

    if (NULL == dir) {
	IDIO_GC_FREE (e0);
    }
}

int idio_env_valid_directory (char const *dir, int const verbose)
{
    int r = 0;

    if (access (dir, R_OK) == 0) {
	struct stat sb;

	if (stat (dir, &sb) == -1) {
	    /*
	     * Can this fail if access(2) just above has succeeded?
	     * Other than the obvious race condition of substituting
	     * an alternative dir between the two calls.
	     */
	    perror ("stat");
	}

	if (S_ISDIR (sb.st_mode)) {
	    r = 1;
	} else {
	    if (verbose) {
		fprintf (stderr, "WARNING: extend-IDIOLIB: %s is not a directory\n", dir);
	    }
	}
    } else {
	if (verbose) {
	    fprintf (stderr, "WARNING: extend-IDIOLIB: %s is not accessible\n", dir);
	}
    }

    return r;
}

void idio_env_extend_IDIOLIB (char const *path, size_t const path_len, int prepend)
{
    IDIO_C_ASSERT (path);
    IDIO_C_ASSERT (path_len > 0);

    /*
     * path	~ /a/b/c/idio
     *
     * dir	~ /idio
     *
     * pdir	~ /c/idio		parent dir (of dir)
     *
     * If pdir is actually /bin/idio, ie. starts with /bin, then we
     * can make an assumption that there is a parallel
     * /lib/idio/{IDIO_VER} containing Idio stuff unless path is
     * /bin/idio or /usr/bin/idio in which case use
     * idio_env_IDIOLIB_default.
     */
    char *dir = memrchr (path, '/', path_len);

    if (NULL == dir) {
	/*
	 * Possible if idio_env_exe_pathname() dun goofed.
	 */
	char em[301];
	snprintf (em, 300, "WARNING: extend-IDIOLIB: no / in directory: '%s' (%zd chars)", path, path_len);
	fprintf (stderr, "%s\n", em);
	return;
    }

    char *pdir = dir - 1;
    while (pdir >= path &&
	   '/' != *pdir) {
	pdir--;
    }

    if (pdir >= path) {
	if (strncmp (pdir, "/bin", 4) == 0) {
	    /*
	     * From .../bin we can derive .../lib/idio/{IDIO_VER}
	     *
	     * This is the implicit idio-exe IDIOLIB directory, ieId
	     */
	    size_t ddd_len = pdir - path;
	    size_t ieId_len = ddd_len + sizeof (IDIO_ENV_LIB_BASE) - 1 + 1 + sizeof (IDIO_SYSTEM_VERSION_MM) - 1;
	    char *ieId;

	    /*
	     * A quick check for /bin/{idio} or /usr/bin/{idio} -- we
	     * can't guarantee the actual string "idio" but here we
	     * are identifiying an element in a system executable
	     * directory therefore we want a system library path
	     */
	    if (pdir == path ||
		(4 == (pdir - path) &&
		 strncmp (path, "/usr", 4) == 0)) {
		ieId_len = idio_env_IDIOLIB_default_len;
		ieId = idio_alloc (ieId_len + 1);
		memcpy (ieId, idio_env_IDIOLIB_default, ieId_len);
		ieId[ieId_len] = '\0';

		/*
		 * Noisily complain if the system directory is not
		 * available
		 */
		idio_env_valid_directory (ieId, 1);
	    } else {
		ieId = idio_alloc (ieId_len + 1);
		size_t i = 0;
		memcpy (ieId + i, path, ddd_len);
		i += ddd_len;
		memcpy (ieId + i, IDIO_ENV_LIB_BASE, sizeof (IDIO_ENV_LIB_BASE) - 1);
		i += sizeof (IDIO_ENV_LIB_BASE) - 1;
		ieId[i++] = '/';
		memcpy (ieId + i, IDIO_SYSTEM_VERSION_MM, sizeof (IDIO_SYSTEM_VERSION_MM) - 1);
		ieId[ieId_len] = '\0';

		idio_env_valid_directory (ieId, 1);
		if (0 &&
		    idio_env_valid_directory (ieId, 0) == 0) {
		    /*
		     * .../lib/idio/{IDIO_VER} wants to be .../lib/idio
		     */
		    i = sizeof (IDIO_SYSTEM_VERSION_MM);
		    ieId_len -= i;
		    ieId[ieId_len] = '\0';

		    if (idio_env_valid_directory (ieId, 1) == 0) {
			/*
			 * Bah!  Go back to the .../lib/idio variant
			 */
			ieId[ieId_len] = '/';
			ieId_len += i;
		    }
		}
	    }

	    /*
	     * Has what we are about to do already been done: if we're
	     * about to add /a, does IDIOLIB already have /a:... or
	     * ...:/a?
	     */
	    int in_place = 0;

	    IDIO idiolib = idio_module_env_symbol_value (idio_env_IDIOLIB_sym, IDIO_LIST1 (idio_S_false));

	    size_t idiolib_C_len = 0;
	    char *idiolib_C = NULL;

	    if (idio_S_false != idiolib) {
		idiolib_C = idio_string_as_C (idiolib, &idiolib_C_len);

		/*
		 * Use idiolib_C_len + 1 to avoid a truncation warning
		 * -- we're just seeing if idiolib_C includes a NUL
		 */
		size_t C_size = idio_strnlen (idiolib_C, idiolib_C_len + 1);
		if (C_size != idiolib_C_len) {
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
		     * environment variable into idio's environment
		     * with an ASCII NUL in it?
		     */
		    IDIO_GC_FREE (idiolib_C);

		    idio_env_format_error ("bootstrap", "contains an ASCII NUL", idio_env_IDIOLIB_sym, idiolib, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return;
		}
	    }

	    size_t dlen;

	    if (NULL != idiolib_C) {
		if (prepend) {
		    char *colon = memchr (idiolib_C, ':', idiolib_C_len);
		    if (NULL == colon) {
			if (ieId_len == idiolib_C_len &&
			    strncmp (ieId, idiolib_C, ieId_len) == 0) {
			    in_place = 1;
			}
		    } else {
			dlen = colon - idiolib_C;
			if (ieId_len == dlen &&
			    strncmp (ieId, colon + 1, ieId_len) == 0) {
			    in_place = 1;
			}
		    }
		} else {
		    char *colon = memrchr (idiolib_C, ':', idiolib_C_len);
		    if (NULL == colon) {
			if (ieId_len == idiolib_C_len &&
			    strncmp (ieId, idiolib_C, ieId_len) == 0) {
			    in_place = 1;
			}
		    } else {
			dlen = idiolib_C_len - (colon - idiolib_C) + 1;
			if (ieId_len == dlen &&
			    strncmp (ieId, colon + 1, ieId_len) == 0) {
			    in_place = 1;
			}
		    }
		}
	    }

	    if (0 == in_place) {
		size_t ni_len = ieId_len;

		if (idiolib_C_len) {
		    ni_len += 1 + idiolib_C_len;
		}

		char *ni = idio_alloc (ni_len + 1);
		ni_len = 0;
		if (prepend) {
		    memcpy (ni, ieId, ieId_len);
		    ni_len = ieId_len;
		    if (idiolib_C_len) {
			memcpy (ni + ni_len++, ":", 1);
		    }
		}
		if (idiolib_C_len) {
		    memcpy (ni + ni_len, idiolib_C, idiolib_C_len);
		    ni_len += idiolib_C_len;
		    if (0 == prepend) {
			memcpy (ni + ni_len++, ":", 1);
		    }
		}
		if (0 == prepend) {
		    memcpy (ni + ni_len, ieId, ieId_len);
		    ni_len += ieId_len;
		}
		ni[ni_len] = '\0';

		if (idio_S_false == idiolib) {
		    idio_env_set_default (idio_env_IDIOLIB_sym, ni);
		} else {
		    idio_module_env_set_symbol_value (idio_env_IDIOLIB_sym, idio_string_C_len (ni, ni_len));
		}
		IDIO_GC_FREE (ni);
	    }

	    IDIO_GC_FREE (idiolib_C);
	}
    }
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
void idio_env_init_idiolib (char const *argv0, size_t const argv0_len)
{
    IDIO_C_ASSERT (argv0);

    char a0rp[PATH_MAX];
    char erp[PATH_MAX];
    int link = 0;
    idio_env_exe_pathname (argv0, argv0_len, a0rp, PATH_MAX, erp, PATH_MAX, &link);

    /*
     * XXX strnlen in bootstrap
     */
    size_t a0rp_len = strnlen (a0rp, PATH_MAX);
    size_t erp_len = strnlen (erp, PATH_MAX);

    /*
     * While we are here, set IDIO_CMD and IDIO_EXE.
     */
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_CMD"), idio_string_C_len (argv0, argv0_len), idio_Idio_module_instance ());
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_CMD_PATH"), idio_pathname_C_len (a0rp, a0rp_len), idio_Idio_module_instance ());
    idio_module_set_symbol_value (IDIO_SYMBOLS_C_INTERN ("IDIO_EXE"), idio_pathname_C_len (erp, erp_len), idio_Idio_module_instance ());

    if (erp_len > 0) {
	idio_env_extend_IDIOLIB (erp, erp_len, 1);
    } else {
	fprintf (stderr, "WARNING: IDIO_EXE is zero length\n");
    }

    /*
     * a0rp *may* have been copied from erp if argv0 had to be found
     * on the PATH.  However, a0rp will only have been normalized (if
     * the PATH element was not absolute) whereas erp will have been
     * realpath(3)'d meaning symbolic links resolved.
     *
     * The point being they could now be quite different, notably, if
     * used in a virtualenv-type setup.
     */
    if (link ||
	!(a0rp_len == erp_len &&
	  strncmp (a0rp, erp, a0rp_len) == 0)) {
	if (a0rp_len > 0) {
	    idio_env_extend_IDIOLIB (a0rp, a0rp_len, 1);
	} else {
	    fprintf (stderr, "WARNING: IDIO_CMD_PATH is zero length\n");
	}
    }

    return;
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
     * /usr/lib/{pkg} was pretty universal -- now, not so much.
     *
     * Hence the use of the system-specific IDIO_SYSTEM_LIBDIR which
     * varies across systems before copying that into
     * idio_env_IDIOLIB_default.
     */
    size_t isl_len = sizeof (IDIO_SYSTEM_LIBDIR) - 1;
    idio_env_IDIOLIB_default_len = isl_len + 5;
    idio_env_IDIOLIB_default = idio_alloc (idio_env_IDIOLIB_default_len + 1);
    memcpy (idio_env_IDIOLIB_default, IDIO_SYSTEM_LIBDIR, isl_len);
    memcpy (idio_env_IDIOLIB_default + isl_len, "/idio", 5);
    idio_env_IDIOLIB_default[idio_env_IDIOLIB_default_len] = '\0';
}

