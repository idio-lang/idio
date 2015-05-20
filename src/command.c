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

char *idio_find_command (IDIO func)
{
    char *command = IDIO_SYMBOL_S (func);
    fprintf (stderr, "idio_find_command: looking for %s\n", command);

    char *cmd_dir = "/usr/bin";
    
    char *pathname = idio_alloc (strlen (cmd_dir) + 1 + strlen (command) + 1);
    strcpy (pathname, cmd_dir);
    strcat (pathname, "/");
    strcat (pathname, command);
    
    return pathname;
}

IDIO idio_invoke_command (IDIO func, IDIO thr, char *pathname)
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

    /* int j; */
    /* for (j = 0; j < i; j++) { */
    /* 	fprintf (stderr, "argv[%d] = %s\n", j, argv[j]); */
    /* } */

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
}

void idio_command_add_primitives ()
{
}

void idio_final_command ()
{
    idio_gc_expose (idio_command_pids);
    idio_gc_expose (idio_command_status_types);
}

