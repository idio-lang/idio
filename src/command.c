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
 * command.c
 * 
 */

#include "idio.h"

static IDIO idio_command_pids;
static IDIO idio_command_last_pid;
static IDIO idio_command_status_type;
static IDIO idio_command_status_types;

#define IDIO_COMMAND_STATUS_RUNNING	0
#define IDIO_COMMAND_STATUS_EXITED	1
#define IDIO_COMMAND_STATUS_KILLED	2
#define IDIO_COMMAND_STATUS_STOPPED	3
#define IDIO_COMMAND_STATUS_CONTINUED	4

static void idio_command_error_glob (IDIO pattern)
{
    IDIO_ASSERT (pattern);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("pattern glob failed", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_glob_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       pattern));
    idio_signal_exception (idio_S_true, c);
}

static void idio_command_error_exec ()
{
    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("exec", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_command_exec_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       idio_fixnum ((intptr_t) errno)));
    idio_signal_exception (idio_S_true, c);
}

static void idio_command_error_status (IDIO status)
{
    IDIO_ASSERT (status);
    IDIO_TYPE_ASSERT (struct_instance, status)

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("command status", sh);
    IDIO c = idio_struct_instance (idio_condition_rt_command_status_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       idio_S_nil,
					       idio_S_nil,
					       status));
    idio_signal_exception (idio_S_true, c);
}

static char **idio_command_get_envp ()
{
    IDIO symbols = idio_module_visible_symbols (idio_current_module (), idio_S_environ);
    size_t n = idio_list_length (symbols);

    char **envp = idio_alloc ((n + 1) * sizeof (char *));

    n = 0;
    while (idio_S_nil != symbols) {
	IDIO symbol = IDIO_PAIR_H (symbols);
	size_t slen = strlen (IDIO_SYMBOL_S (symbol));
	IDIO val = idio_module_current_symbol_value_recurse (symbol);

	size_t vlen = 0;
	if (idio_S_unset != val) {
	    IDIO_TYPE_ASSERT (string, val);
	    vlen = IDIO_STRING_BLEN (val);
	}

	envp[n] = idio_alloc (slen + 1 + vlen + 1);
	strcpy (envp[n], IDIO_SYMBOL_S (symbol));
	strcat (envp[n], "=");
	if (idio_S_undef != val) {
	    strncat (envp[n], IDIO_STRING_S (val), vlen);
	}
	envp[n][slen + 1 + vlen] = '\0';

	/* fprintf (stderr, "E: %s\n", envp[n]); */

	symbols = IDIO_PAIR_T (symbols);
	n++;
    }
    envp[n] = NULL;

    return envp;
}

char *idio_command_find_exe_C (char *command)
{
    IDIO_C_ASSERT (command);
    
    size_t cmdlen = strlen (command);
    
    IDIO PATH = idio_module_current_symbol_value_recurse (idio_env_PATH_sym);

    char *path;
    char *pathe;
    if (idio_S_undef == PATH ||
	! idio_isa_string (PATH)) {
	path = idio_env_PATH_default;
	pathe = path + strlen (path);
    } else {
	path = IDIO_STRING_S (PATH);
	pathe = path + IDIO_STRING_BLEN (PATH);
    }

    /*
     * PATH_MAX varies: POSIX is 256, CentOS 7 is 4096
     *
     * The Linux man page for realpath(3) suggests that calling
     * pathconf(3) doesn't improve matters a whole bunch.
     */
    char exename[PATH_MAX];
    char cwd[PATH_MAX];
    if (getcwd (cwd, PATH_MAX) == NULL) {
	idio_error_system ("cannot access CWD", idio_S_nil, errno);
    }
    size_t cwdlen = strlen (cwd);
    
    int done = 0;
    while (! done) {
	size_t pathlen = pathe - path;
	char * colon = NULL;
	
	if (0 == pathlen) {
	    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
		idio_error_system ("cwd+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
	    }
	    
	    strcpy (exename, cwd);
	} else {
	    colon = memchr (path, ':', pathlen);

	    if (NULL == colon) {
		if ((pathlen + 1 + cmdlen + 1) >= PATH_MAX) {
		    idio_error_system ("dir+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
		}
	    
		memcpy (exename, path, pathlen);
		exename[pathlen] = '\0';
	    } else {
		size_t dirlen = colon - path;
	    
		if (0 == dirlen) {
		    if ((cwdlen + 1 + cmdlen + 1) >= PATH_MAX) {
			idio_error_system ("cwd+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
		    }
	    
		    strcpy (exename, cwd);
		} else {
		    if ((dirlen + 1 + cmdlen + 1) >= PATH_MAX) {
			idio_error_system ("dir+command exename length", IDIO_LIST2 (PATH, idio_string_C (command)), ENAMETOOLONG);
		    }
	    
		    memcpy (exename, path, dirlen);
		    exename[dirlen] = '\0';
		}
	    }
	}

	strcat (exename, "/");
	strcat (exename, command);

	if (access (exename, X_OK) == 0) {
	    done = 1;
	    break;
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
	pathname = idio_alloc (strlen (exename) + 1);
	strcpy (pathname, exename);
    }
    
    return pathname;
}

char *idio_command_find_exe (IDIO func)
{
    IDIO_ASSERT (func);
    IDIO_TYPE_ASSERT (symbol, func);
    
    return idio_command_find_exe_C (IDIO_SYMBOL_S (func));
}

static char *idio_command_glob_charp (char *src)
{
    IDIO_C_ASSERT (src);

    while (*src) {
	switch (*src) {
	case '*':
	case '?':
	case '[':
	    return src;
	default:
	    break;
	}
	src++;
    }

    return NULL;
}

static size_t idio_command_possible_filename_glob (IDIO arg, glob_t *gp)
{
    IDIO_ASSERT (arg);
    IDIO_TYPE_ASSERT (symbol, arg);

    char *match = idio_command_glob_charp (IDIO_SYMBOL_S (arg));

    if (NULL == match) {
	return 0;
    } else {
	if (glob (IDIO_SYMBOL_S (arg), GLOB_NOCHECK, NULL, gp) == 0) {
	    return gp->gl_pathc;
	} else {
	    idio_command_error_glob (arg);
	}
    }
    
    idio_error_C ("dropped out", IDIO_LIST1 (arg));
    return -1;
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
	
	switch ((intptr_t) arg & 3) {
	case IDIO_TYPE_FIXNUM_MARK:
	case IDIO_TYPE_CONSTANT_MARK:
	case IDIO_TYPE_CHARACTER_MARK:
	    {
		argv[i++] = idio_as_string (arg, 1);
	    }
	    break;
	case IDIO_TYPE_POINTER_MARK:
	    {
		switch (idio_type (arg)) {
		case IDIO_TYPE_C_INT8:
		case IDIO_TYPE_C_UINT8:
		case IDIO_TYPE_C_INT16:
		case IDIO_TYPE_C_UINT16:
		case IDIO_TYPE_C_INT32:
		case IDIO_TYPE_C_UINT32:
		case IDIO_TYPE_C_INT64:
		case IDIO_TYPE_C_UINT64:
		case IDIO_TYPE_C_FLOAT:
		case IDIO_TYPE_C_DOUBLE:
		    {
			argv[i++] = idio_as_string (arg, 1);
		    }
		    break;
		case IDIO_TYPE_STRING:
		    if (asprintf (&argv[i++], "%.*s", (int) IDIO_STRING_BLEN (arg), IDIO_STRING_S (arg)) == -1) {
			return NULL;
		    }
		    break;
		case IDIO_TYPE_SUBSTRING:
		    if (asprintf (&argv[i++], "%.*s", (int) IDIO_SUBSTRING_BLEN (arg), IDIO_SUBSTRING_S (arg)) == -1) {
			return NULL;
		    }
		    break;
		case IDIO_TYPE_SYMBOL:
		    {
			glob_t g;
			size_t n = idio_command_possible_filename_glob (arg, &g);

			if (0 == n) {
			    if (asprintf (&argv[i++], "%s", IDIO_SYMBOL_S (arg)) == -1) {
				return NULL;
			    }
			} else {
			    /*
			     * NB "gl_pathc - 1" as we reserved a slot
			     * for the original pattern so the
			     * increment is one less
			     */
			    argc += g.gl_pathc  - 1;

			    argv = idio_realloc (argv, argc * sizeof (char *));
			    
			    size_t n;
			    for (n = 0; n < g.gl_pathc; n++) {
				size_t plen = strlen (g.gl_pathv[n]);
				argv[i] = idio_alloc (plen + 1);
				strcpy (argv[i++], g.gl_pathv[n]);
			    }

			    globfree (&g);
			}
		    }
		    break;
		case IDIO_TYPE_PAIR:
		case IDIO_TYPE_ARRAY:
		case IDIO_TYPE_HASH:
		case IDIO_TYPE_BIGNUM:
		    {
			argv[i++] = idio_as_string (arg, 1);
		    }
		    break;
		case IDIO_TYPE_C_POINTER:
		case IDIO_TYPE_CLOSURE:
		case IDIO_TYPE_PRIMITIVE:
		case IDIO_TYPE_MODULE:
		case IDIO_TYPE_FRAME:
		case IDIO_TYPE_HANDLE:
		case IDIO_TYPE_STRUCT_TYPE:
		case IDIO_TYPE_STRUCT_INSTANCE:
		case IDIO_TYPE_THREAD:
		case IDIO_TYPE_C_TYPEDEF:
		case IDIO_TYPE_C_STRUCT:
		case IDIO_TYPE_C_INSTANCE:
		case IDIO_TYPE_C_FFI:
		case IDIO_TYPE_OPAQUE:
		default:
		    idio_warning_message ("unexpected object type: %s", idio_type2string (arg));
		    break;
		}
	    }
	    break;
	default:
	    idio_warning_message ("unexpected object type: %s", idio_type2string (arg));
	    break;
	}

	args = IDIO_PAIR_T (args);
    }
    argv[i] = NULL;

    return argv;
}

IDIO idio_command_invoke (IDIO func, IDIO thr, char *pathname)
{
    IDIO val = IDIO_THREAD_VAL (thr);
    IDIO args_a = IDIO_FRAME_ARGS (val);
    IDIO last = idio_array_pop (args_a);
    IDIO_FRAME_NARGS (val) -= 1;

    if (idio_S_nil != last) {
	char *ls = idio_as_string (last, 1);
	fprintf (stderr, "invoke: last arg != nil: %s\n", ls);
	free (ls);
	IDIO_C_ASSERT (0);
    }

    IDIO args = idio_array_to_list (IDIO_FRAME_ARGS (val));

    char **argv = idio_command_argv (args);

    if (NULL == argv) {
	idio_error_C ("bad argv", IDIO_LIST2 (func, args));
    }
    argv[0] = pathname;

    pid_t cpid;
    cpid = fork ();
    if (-1 == cpid) {
	perror ("fork");
	idio_error_system ("fork failed", IDIO_LIST2 (func, args), errno);
	exit (EXIT_FAILURE);
    }
    
    if (0 == cpid) {
	char **envp = idio_command_get_envp ();

	execve (argv[0], argv, envp);
	perror ("execv");
	idio_command_error_exec ();
	exit (1);
    }

    /*
     * NB don't free pathname, argv[0] -- we didn't allocate it
     */
    int j; 
    for (j = 1; NULL != argv[j]; j++) { 
    	free (argv[j]); 
    }
    free (argv);

    IDIO fn_cpid = idio_fixnum ((intptr_t) cpid);
    IDIO cstate = idio_struct_instance (idio_command_status_type,
					IDIO_LIST3 (fn_cpid,
						    idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_RUNNING),
						    idio_S_nil));
    idio_hash_put (idio_command_pids, fn_cpid, cstate);

    int status;
    pid_t w;
    do {
	w = waitpid (cpid, &status, WUNTRACED | WCONTINUED);
	if (1 == -w) {
	    perror ("waitpid");
	    idio_error_system ("waitpid failed", IDIO_LIST2 (func, args), errno);
	    exit (EXIT_FAILURE);
	}

	IDIO fn_w = idio_fixnum ((intptr_t) w);
	cstate = idio_hash_get (idio_command_pids, fn_w);
	if (WIFEXITED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_EXITED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     idio_fixnum ((intptr_t) WEXITSTATUS (status)));
	} else if (WIFSIGNALED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_KILLED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     idio_fixnum ((intptr_t) WTERMSIG (status)));
	} else if (WIFSTOPPED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_STOPPED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     idio_fixnum ((intptr_t) WSTOPSIG (status)));
	} else if (WIFCONTINUED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_CONTINUED));
	}
    } while (!WIFEXITED (status) && !WIFSIGNALED (status));

    if (WIFEXITED (status) &&
	0 == WEXITSTATUS (status)) {
	return idio_struct_instance_ref_direct (cstate, 2);
    }

    idio_command_error_status (cstate);

    /* notreached */
    return idio_S_unspec;
}

void idio_init_command ()
{
    idio_command_pids = IDIO_HASH_EQP (8);
    idio_gc_protect (idio_command_pids);

    idio_module_set_symbol_value (idio_symbols_C_intern ("pids"), idio_command_pids, idio_main_module ());

    IDIO status_name = idio_symbols_C_intern ("%command-status-type");
    
    idio_command_status_type = idio_struct_type (status_name,
						 idio_S_nil,
						 IDIO_LIST3 (idio_symbols_C_intern ("pid"),
							     idio_symbols_C_intern ("state"),
							     idio_symbols_C_intern ("wait")));

    idio_module_set_symbol_value (status_name, idio_command_status_type, idio_main_module ());

    idio_command_status_types = idio_array (5);
    idio_gc_protect (idio_command_status_types);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("running"), 0);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("exited"), 1);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("killed"), 2);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("stopped"), 3);
    idio_array_insert_index (idio_command_status_types, idio_symbols_C_intern ("continued"), 4);
}

void idio_command_add_primitives ()
{
}

void idio_final_command ()
{
    idio_gc_expose (idio_command_pids);
    idio_gc_expose (idio_command_status_types);
}

