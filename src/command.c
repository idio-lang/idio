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

/* MacOS & Solaris doesn't have environ in unistd.h */
extern char **environ;

static IDIO idio_command_pids;
static IDIO idio_command_last_pid;
static IDIO idio_command_status_type;
static IDIO idio_command_status_types;

static IDIO idio_command_env_PATH;

#define IDIO_COMMAND_STATUS_RUNNING	0
#define IDIO_COMMAND_STATUS_EXITED	1
#define IDIO_COMMAND_STATUS_KILLED	2
#define IDIO_COMMAND_STATUS_STOPPED	3
#define IDIO_COMMAND_STATUS_CONTINUED	4

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
	if (idio_S_undef != val) {
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

	symbols = IDIO_PAIR_T (symbols);
	n++;
    }

    return envp;
}

char *idio_command_find_exe (IDIO func)
{
    char *command = IDIO_SYMBOL_S (func);
    size_t cmdlen = strlen (command);
    
    IDIO PATH = idio_module_current_symbol_value_recurse (idio_command_env_PATH);

    char *path;
    char *pathe;
    if (idio_S_undef == PATH) {
	path = "/bin:/usr/bin";
	pathe = path + strlen (path);
    } else {
	path = IDIO_STRING_S (PATH);
	pathe = path + IDIO_STRING_BLEN (PATH);
    }

    /*
     * PATH_MAX varies: POSIX is 256, CentOS 7 is 4096
     */
    char exename[PATH_MAX];
    int done = 0;
    while (! done) {
	size_t pathlen = pathe - path;

	if (0 == pathlen) {
	    idio_error_C ("empty dir in PATH", IDIO_LIST1 (PATH));
	}
	
	char *colon = memchr (path, ':', pathlen);

	if (NULL == colon) {
	    if ((pathlen + 1 + cmdlen + 1) >= PATH_MAX) {
		idio_error_C ("dir+command exename length", IDIO_LIST2 (PATH, func));
	    }
	    
	    memcpy (exename, path, pathlen);
	    exename[pathlen] = '\0';
	} else {
	    size_t dirlen = colon - path;
	    
	    if ((dirlen + 1 + cmdlen + 1) >= PATH_MAX) {
		idio_error_C ("dir+command exename length", IDIO_LIST2 (PATH, func));
	    }
	    
	    memcpy (exename, path, dirlen);
	    exename[dirlen] = '\0';
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

IDIO idio_command_invoke (IDIO func, IDIO thr, char *pathname)
{
    IDIO val = IDIO_THREAD_VAL (thr);
    idio_debug ("invoke: symbol %s ", func); 
    idio_debug ("%s\n", IDIO_FRAME_ARGS (val)); 
    IDIO args_a = IDIO_FRAME_ARGS (val);
    IDIO last = idio_array_pop (args_a);
    IDIO_FRAME_NARGS (val) -= 1;

    if (idio_S_nil != last) {
	char *ls = idio_as_string (last, 1);
	fprintf (stderr, "invoke: last arg != nil: %s\n", ls);
	free (ls);
	IDIO_C_ASSERT (0);
    }

    /*
     * argv[] needs:
     * 1. (nominally) path to command
     * 2+ arg1+
     * 3. NULL (terminator)
     *
     * Here we will flatten any lists and expand filename
     * patterns which means the arg list will grow as we
     * determine what each argument means
     */
    int argc = 1 + IDIO_FRAME_NARGS (val) + 1;
    char **argv = idio_alloc (argc * sizeof (char *));
    int nargs;		/* index into frame args */
    int i = 0;		/* index into argv */

    argv[i++] = pathname;
    for (nargs = 0; nargs < IDIO_FRAME_NARGS (val); nargs++) {
	IDIO o = idio_array_get_index (args_a, nargs);
	idio_as_flat_string (o, argv, &i);
    }
    argv[i++] = NULL;

    char **envp = idio_command_get_envp ();

    return IDIO_FIXNUM (0);
    pid_t cpid;
    cpid = fork ();
    if (-1 == cpid) {
	perror ("fork");
	exit (EXIT_FAILURE);
    }
    
    if (0 == cpid) {
	execv (argv[0], argv);
	perror ("execv");
	exit (1);
    }

    int j; 
    for (j = 0; j < i; j++) { 
    	free (argv[j]); 
    }
    free (argv);

    IDIO fn_cpid = IDIO_FIXNUM ((intptr_t) cpid);
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
	    exit (EXIT_FAILURE);
	}

	IDIO fn_w = IDIO_FIXNUM ((intptr_t) w);
	cstate = idio_hash_get (idio_command_pids, fn_w);
	if (WIFEXITED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_EXITED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     IDIO_FIXNUM ((intptr_t) WEXITSTATUS (status)));
	} else if (WIFSIGNALED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_KILLED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     IDIO_FIXNUM ((intptr_t) WTERMSIG (status)));
	} else if (WIFSTOPPED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_STOPPED));
	    idio_struct_instance_set_direct (cstate,
					     2,
					     IDIO_FIXNUM ((intptr_t) WSTOPSIG (status)));
	} else if (WIFCONTINUED (status)) {
	    idio_struct_instance_set_direct (cstate,
					     1,
					     idio_array_get_index (idio_command_status_types, IDIO_COMMAND_STATUS_CONTINUED));
	}
    } while (!WIFEXITED (status) && !WIFSIGNALED (status));

    return idio_struct_instance_ref_direct (cstate, 2);
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

    idio_command_env_PATH = idio_symbols_C_intern ("PATH");
}

static void idio_command_add_environ ()
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
}

void idio_command_add_primitives ()
{
    idio_command_add_environ ();
}

void idio_final_command ()
{
    idio_gc_expose (idio_command_pids);
    idio_gc_expose (idio_command_status_types);
}

