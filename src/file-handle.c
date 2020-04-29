/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * file-handle.c
 *
 */

#include "idio.h"

static IDIO idio_file_handles = idio_S_nil;
static IDIO idio_stdin = idio_S_nil;
static IDIO idio_stdout = idio_S_nil;
static IDIO idio_stderr = idio_S_nil;

static idio_handle_methods_t idio_file_handle_methods = {
    idio_free_file_handle,
    idio_readyp_file_handle,
    idio_getc_file_handle,
    idio_eofp_file_handle,
    idio_close_file_handle,
    idio_putc_file_handle,
    idio_puts_file_handle,
    idio_flush_file_handle,
    idio_seek_file_handle,
    idio_print_file_handle
};

static void idio_file_handle_error_filename (IDIO filename, IDIO loc)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("generic filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' error", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh),
					       filename));
    idio_raise_condition (idio_S_true, c);
}

static void idio_file_handle_error_filename_C (char *name, IDIO loc)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_file_handle_error_filename (idio_string_C (name), loc);
}

static void idio_file_handle_error_filename_delete (IDIO filename, IDIO loc)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("remove '", msh);
    idio_display (filename, msh);
    idio_display_C ("'", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh),
					       filename));
    idio_raise_condition (idio_S_true, c);
}

static void idio_file_handle_error_filename_delete_C (char *name, IDIO loc)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_file_handle_error_filename_delete (idio_string_C (name), loc);
}

static void idio_file_handle_error_malformed_filename (IDIO filename, IDIO loc)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("bad filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("'", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_malformed_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh),
					       filename));
    idio_raise_condition (idio_S_true, c);
}

static void idio_file_handle_error_malformed_filename_C (char *name, IDIO loc)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_file_handle_error_malformed_filename (idio_string_C (name), loc);
}

static void idio_file_handle_error_file_protection (IDIO filename, IDIO loc)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' access", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_file_protection_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh),
					       filename));
    idio_raise_condition (idio_S_true, c);
}

static void idio_file_handle_error_file_protection_C (char *name, IDIO loc)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_file_handle_error_file_protection (idio_string_C (name), loc);
}

static void idio_file_handle_error_filename_already_exists (IDIO filename, IDIO loc)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' already exists", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_file_already_exists_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_get_output_string (dsh),
					       filename));
    idio_raise_condition (idio_S_true, c);
}

static void idio_file_handle_error_filename_already_exists_C (char *name, IDIO loc)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_file_handle_error_filename_already_exists (idio_string_C (name), loc);
}

static void idio_file_handle_error_filename_not_found (IDIO filename, IDIO loc)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, loc);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' not found", msh);

    IDIO c = idio_struct_instance (idio_condition_io_no_such_file_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_S_nil,
					       filename));
    idio_raise_condition (idio_S_true, c);
}

static void idio_file_handle_error_filename_not_found_C (char *name, IDIO loc)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);

    idio_file_handle_error_filename_not_found (idio_string_C (name), loc);
}

static IDIO idio_open_file_handle (char *name, FILE *filep, int h_flags, int s_flags)
{
    IDIO_C_ASSERT (filep);

    idio_file_handle_stream_t *fhsp = idio_alloc (sizeof (idio_file_handle_stream_t));
    int bufsiz = BUFSIZ;

    int fd = fileno (filep);

    if (isatty (fd)) {
	s_flags |= IDIO_FILE_HANDLE_FLAG_INTERACTIVE;
    }

    IDIO_FILE_HANDLE_STREAM_FILEP (fhsp) = filep;
    IDIO_FILE_HANDLE_STREAM_FD (fhsp) = fd;
    IDIO_FILE_HANDLE_STREAM_FLAGS (fhsp) = s_flags;
    IDIO_FILE_HANDLE_STREAM_BUF (fhsp) = idio_alloc (bufsiz);
    IDIO_FILE_HANDLE_STREAM_BUFSIZ (fhsp) = bufsiz;
    IDIO_FILE_HANDLE_STREAM_PTR (fhsp) = IDIO_FILE_HANDLE_STREAM_BUF (fhsp);
    IDIO_FILE_HANDLE_STREAM_COUNT (fhsp) = 0;

    IDIO fh = idio_handle ();

    char *name_copy = idio_alloc (strlen (name) + 1);
    strcpy (name_copy, name);

    IDIO_HANDLE_FLAGS (fh) |= h_flags | IDIO_HANDLE_FLAG_FILE;
    IDIO_HANDLE_NAME (fh) = name_copy;
    IDIO_HANDLE_STREAM (fh) = fhsp;
    IDIO_HANDLE_METHODS (fh) = &idio_file_handle_methods;

    if ((s_flags & IDIO_FILE_HANDLE_FLAG_STDIO) == 0) {
	idio_gc_register_finalizer (fh, idio_file_handle_finalizer);
	/* idio_gc_register_file_handle (fh); */
    }

    return fh;
}

IDIO_DEFINE_PRIMITIVE1V ("open-file-from-fd", open_file_handle_from_fd, (IDIO ifd, IDIO args))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    int fd = IDIO_C_TYPE_INT (ifd);

    char name[PATH_MAX];
    sprintf (name, "/dev/fd/%d", fd);

    if (idio_S_nil != args) {
	IDIO iname = IDIO_PAIR_H (args);
	if (idio_isa_string (iname)) {
	    size_t blen = idio_string_blen (iname);

	    if (blen >= PATH_MAX) {
		idio_error_C ("name too long", iname, IDIO_C_LOCATION ("open-file-from-fd"));

		/* notreached */
		return idio_S_notreached;
	    }

	    sprintf (name, "%.*s", (int) blen, idio_string_s (iname));

	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", iname, IDIO_C_LOCATION ("open-file-from-fd"));
	}
    }

    int free_mode = 0;
    char *mode = "re";

    if (idio_S_nil != args) {
	IDIO imode = IDIO_PAIR_H (args);
	if (idio_isa_string (imode)) {
	    mode = idio_string_as_C (imode);
	    free_mode = 1;
	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", imode, IDIO_C_LOCATION ("open-file-from-fd"));
	}
    }

    FILE *filep = fdopen (fd, mode);
    if (NULL == filep) {
	idio_error_system_errno ("fdopen", IDIO_LIST2 (idio_string_C (name), idio_string_C (mode)), IDIO_C_LOCATION ("open-file-from-fd"));
    }

    int mflag = IDIO_HANDLE_FLAG_READ;
    if (strchr (mode, '+') != NULL) {
	mflag |= IDIO_HANDLE_FLAG_WRITE;
    }

    if (free_mode) {
	free (mode);
    }

    return idio_open_file_handle (name, filep, mflag, IDIO_FILE_HANDLE_FLAG_NONE);
}

IDIO_DEFINE_PRIMITIVE1V ("open-input-file-from-fd", open_input_file_handle_from_fd, (IDIO ifd, IDIO args))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    int fd = IDIO_C_TYPE_INT (ifd);

    char name[PATH_MAX];
    sprintf (name, "/dev/fd/%d", fd);

    if (idio_S_nil != args) {
	IDIO iname = IDIO_PAIR_H (args);
	if (idio_isa_string (iname)) {
	    size_t blen = idio_string_blen (iname);

	    if (blen >= PATH_MAX) {
		idio_error_C ("name too long", iname, IDIO_C_LOCATION ("open-input-file-from-fd"));

		/* notreached */
		return idio_S_notreached;
	    }

	    sprintf (name, "%.*s", (int) blen, idio_string_s (iname));

	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", iname, IDIO_C_LOCATION ("open-input-file-from-fd"));
	}
    }

    int free_mode = 0;
    char *mode = "re";

    if (idio_S_nil != args) {
	IDIO imode = IDIO_PAIR_H (args);
	if (idio_isa_string (imode)) {
	    mode = idio_string_as_C (imode);
	    free_mode = 1;
	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", imode, IDIO_C_LOCATION ("open-input-file-from-fd"));
	}
    }

    FILE *filep = fdopen (fd, mode);
    if (NULL == filep) {
	idio_error_system_errno ("fdopen", IDIO_LIST2 (idio_string_C (name), idio_string_C (mode)), IDIO_C_LOCATION ("open-input-file-from-fd"));
    }

    int mflag = IDIO_HANDLE_FLAG_READ;
    if (strchr (mode, '+') != NULL) {
	mflag |= IDIO_HANDLE_FLAG_WRITE;
    }

    if (free_mode) {
	free (mode);
    }

    return idio_open_file_handle (name, filep, mflag, IDIO_FILE_HANDLE_FLAG_NONE);
}

IDIO_DEFINE_PRIMITIVE1V ("open-output-file-from-fd", open_output_file_handle_from_fd, (IDIO ifd, IDIO args))
{
    IDIO_ASSERT (ifd);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (C_int, ifd);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    int fd = IDIO_C_TYPE_INT (ifd);

    char name[PATH_MAX];
    sprintf (name, "/dev/fd/%d", fd);

    if (idio_S_nil != args) {
	IDIO iname = IDIO_PAIR_H (args);
	if (idio_isa_string (iname)) {
	    size_t blen = idio_string_blen (iname);

	    if (blen >= PATH_MAX) {
		idio_error_C ("name too long", iname, IDIO_C_LOCATION ("open-output-file-from-fd"));

		/* notreached */
		return idio_S_notreached;
	    }

	    sprintf (name, "%.*s", (int) blen, idio_string_s (iname));

	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", iname, IDIO_C_LOCATION ("open-output-file-from-fd"));
	}
    }

    int free_mode = 0;
    char *mode = "we";

    if (idio_S_nil != args) {
	IDIO imode = IDIO_PAIR_H (args);
	if (idio_isa_string (imode)) {
	    mode = idio_string_as_C (imode);
	    free_mode = 1;
	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", imode, IDIO_C_LOCATION ("open-output-file-from-fd"));
	}
    }

    FILE *filep = fdopen (fd, mode);
    if (NULL == filep) {
	idio_error_system_errno ("fdopen", IDIO_LIST2 (idio_string_C (name), idio_string_C (mode)), IDIO_C_LOCATION ("open-output-file-from-fd"));
    }

    int mflag = IDIO_HANDLE_FLAG_WRITE;
    if (strchr (mode, '+') != NULL) {
	mflag |= IDIO_HANDLE_FLAG_READ;
    }

    if (free_mode) {
	free (mode);
    }

    return idio_open_file_handle (name, filep, mflag, IDIO_FILE_HANDLE_FLAG_NONE);
}

IDIO idio_open_file_handle_C (char *name, char *mode)
{
    IDIO_C_ASSERT (name);
    IDIO_C_ASSERT (mode);

    int h_flags = 0;

    switch (mode[0]) {
    case 'r':
	h_flags = IDIO_HANDLE_FLAG_READ;
	if (strchr (mode, '+') != NULL) {
	    h_flags |= IDIO_HANDLE_FLAG_WRITE;
	}
	break;
    case 'a':
    case 'w':
	h_flags = IDIO_HANDLE_FLAG_WRITE;
	if (strchr (mode, '+') != NULL) {
	    h_flags |= IDIO_HANDLE_FLAG_READ;
	}
	break;
    default:
	idio_error_printf (IDIO_C_LOCATION ("idio_open_file_handle_C"), "unexpected mode", mode);
	break;
    }

    /*
     * XXX
     *
     * Solaris (OpenIndiana 0.151.1.8) does not support the "x" (ie.
     * O_EXCL) mode flag.
     *
     * To fix that would require we change fopen() to open() changing
     * all the character string mode flags to fcntl O_* flags (and
     * with "x" mapping to O_EXCL) followed by fdopen() to get the
     * FILE*.
     */

    FILE *filep;
    int tries;
    for (tries = 2; tries > 0 ; tries--) {
	filep = fopen (name, mode);
	if (NULL == filep) {
	    switch (errno) {
	    case EMFILE:	/* process max */
	    case ENFILE:	/* system max */
		idio_gc_collect ();
		break;
	    case EACCES:
		idio_file_handle_error_file_protection_C (name, IDIO_C_LOCATION ("idio_open_file_handle_C"));
	    case EEXIST:
		idio_file_handle_error_filename_already_exists_C (name, IDIO_C_LOCATION ("idio_open_file_handle_C"));
	    case ENAMETOOLONG:
		idio_file_handle_error_malformed_filename_C (name, IDIO_C_LOCATION ("idio_open_file_handle_C"));
	    case ENOENT:
		idio_file_handle_error_filename_not_found_C (name, IDIO_C_LOCATION ("idio_open_file_handle_C"));
	    case ENOTDIR:
		idio_file_handle_error_filename_C (name, IDIO_C_LOCATION ("idio_open_file_handle_C"));
	    default:
		idio_error_system_errno ("fopen", IDIO_LIST1 (idio_string_C (name)), IDIO_C_LOCATION ("idio_open_file_handle_C"));
	    }
	} else {
	    break;
	}
    }

    if (NULL == filep) {
	idio_error_system_errno ("fopen (final)", IDIO_LIST2 (idio_string_C (name), idio_string_C (mode)), IDIO_C_LOCATION ("idio_open_file_handle_C"));
    }

    int s_flags = IDIO_FILE_HANDLE_FLAG_NONE;
    if (strchr (mode, 'e') != NULL) {
	s_flags |= IDIO_FILE_HANDLE_FLAG_CLOEXEC;

	/*
	 * Some systems don't support the "e" (close-on-exec) mode
	 * character for fopen(3) -- on the plus side they don't
	 * complain either!
	 *
	 * We'll have to set the close-on-exec flag ourselves, if
	 * required
	 */
#if (defined (__APPLE__) && defined (__MACH__))
	int fd = fileno (filep);
	if (fcntl (fd, F_SETFD, FD_CLOEXEC) == -1) {
	    idio_error_system_errno ("fcntl F_SETFD FD_CLOEXEC", IDIO_LIST3 (idio_string_C (name), idio_string_C (mode), idio_C_int (fd)), IDIO_C_LOCATION ("idio_open_file_handle_C"));
	}
#endif
    }

    return idio_open_file_handle (name, filep, h_flags, s_flags);
}

IDIO_DEFINE_PRIMITIVE2 ("open-file", open_file_handle, (IDIO name, IDIO mode))
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (mode);

    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name);
	break;
    default:
	idio_error_param_type ("string", name, IDIO_C_LOCATION ("open-file"));
	break;
    }

    char *mode_C = NULL;

    switch (idio_type (mode)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	mode_C = idio_string_as_C (mode);
	break;
    default:
	idio_error_param_type ("string", mode, IDIO_C_LOCATION ("open-file"));
	break;
    }

    IDIO r = idio_open_file_handle_C (name_C, mode_C);

    free (mode_C);
    free (name_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("open-input-file", open_input_file_handle, (IDIO name))
{
    IDIO_ASSERT (name);

    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name);
	break;
    default:
	idio_error_param_type ("string", name, IDIO_C_LOCATION ("open-input-file"));
	break;
    }

    IDIO r = idio_open_file_handle_C (name_C, "re");

    free (name_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("open-output-file", open_output_file_handle, (IDIO name))
{
    IDIO_ASSERT (name);

    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name);
	break;
    default:
	idio_error_param_type ("string", name, IDIO_C_LOCATION ("open-output-file"));
	break;
    }

    IDIO r = idio_open_file_handle_C (name_C, "we");

    free (name_C);

    return r;
}

static IDIO idio_open_std_file_handle (FILE *filep)
{
    IDIO_C_ASSERT (filep);

    int mflag = IDIO_HANDLE_FLAG_NONE;
    char *name = NULL;

    if (filep == stdin) {
	mflag = IDIO_HANDLE_FLAG_READ;
	name = "*stdin*";
    } else if (filep == stdout) {
	mflag = IDIO_HANDLE_FLAG_WRITE;
	name = "*stdout*";
    } else if (filep == stderr) {
	mflag = IDIO_HANDLE_FLAG_WRITE;
	name = "*stderr*";
    } else {
	idio_error_printf (IDIO_C_LOCATION ("idio_open_std_file_handle"), "unexpected standard IO stream");
	return idio_S_unspec;
    }

    return idio_open_file_handle (name, filep, mflag, IDIO_FILE_HANDLE_FLAG_STDIO);
}

IDIO idio_stdin_file_handle ()
{
    return idio_stdin;
}

IDIO idio_stdout_file_handle ()
{
    return idio_stdout;
}

IDIO idio_stderr_file_handle ()
{
    return idio_stderr;
}

int idio_isa_file_handle (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_handle (o) &&
	    IDIO_HANDLE_FLAGS (o) & IDIO_HANDLE_FLAG_FILE);
}

IDIO_DEFINE_PRIMITIVE1 ("file-handle?", file_handlep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_file_handle (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_input_file_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_file_handle (o) &&
	    IDIO_INPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1 ("input-file-handle?", input_file_handlep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_input_file_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_output_file_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_file_handle (o) &&
	    IDIO_OUTPUTP_HANDLE (o));
}

IDIO_DEFINE_PRIMITIVE1 ("output-file-handle?", output_file_handlep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_output_file_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_file_handle_fd (IDIO fh)
{
    IDIO_ASSERT (fh);
    IDIO_TYPE_ASSERT (file_handle, fh);

    return IDIO_FILE_HANDLE_FD (fh);
}

IDIO_DEFINE_PRIMITIVE1 ("file-handle-fd", file_handle_fd, (IDIO fh))
{
    IDIO_ASSERT (fh);
    IDIO_VERIFY_PARAM_TYPE (file_handle, fh);

    return idio_fixnum (IDIO_FILE_HANDLE_FD (fh));
}

void idio_file_handle_finalizer (IDIO fh)
{
    IDIO_ASSERT (fh);
    IDIO_TYPE_ASSERT (handle, fh);

    if (! (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED)) {
	IDIO_HANDLE_M_CLOSE (fh) (fh);
    }
}

void idio_remember_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    idio_hash_put (idio_file_handles, fh, idio_S_nil);
}

void idio_forget_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    idio_hash_delete (idio_file_handles, fh);
}

void idio_free_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    free (IDIO_FILE_HANDLE_BUF (fh));
    free (IDIO_HANDLE_STREAM (fh));
    free (IDIO_HANDLE_NAME (fh));
}

int idio_readyp_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (IDIO_FILE_HANDLE_COUNT (fh) > 0) {
	return 1;
    }

    return (feof (IDIO_FILE_HANDLE_FILEP (fh)) == 0);
}

void idio_file_handle_read_more (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
	/*
	 * fgets sets EOF if it saw EOF even if it read something.  In
	 * practice you need to check for EOF before calling fgets the
	 * next time round...
	 */
	if (idio_eofp_file_handle (fh)) {
	    IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_EOF;
	    return;
	}

	char *s = fgets (IDIO_FILE_HANDLE_BUF (fh), IDIO_FILE_HANDLE_BUFSIZ (fh), IDIO_FILE_HANDLE_FILEP (fh));
	if (NULL == s) {
	    IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_EOF;
	    return;
	}
	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = strlen (IDIO_FILE_HANDLE_BUF (fh));
    } else {
	/*
	 * fread sets EOF if it saw EOF even if it read something.  In
	 * practice you need to check for EOF before calling fread the
	 * next time round...
	 */
	if (idio_eofp_file_handle (fh)) {
	    IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_EOF;
	    return;
	}

	int nread = fread (IDIO_FILE_HANDLE_BUF (fh), 1, IDIO_FILE_HANDLE_BUFSIZ (fh), IDIO_FILE_HANDLE_FILEP (fh));
	if (0 == nread) {
	    IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_EOF;
	    return;
	}
	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = nread;
    }
}

int idio_getc_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (! idio_input_file_handlep (fh)) {
	idio_handle_error_read (fh, IDIO_C_LOCATION ("idio_getc_file_handle"));
    }

    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) >= 1) {
	    IDIO_FILE_HANDLE_COUNT (fh) -= 1;
	    int c = (int) *(IDIO_FILE_HANDLE_PTR (fh));
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    return c;
	} else {
	    idio_file_handle_read_more (fh);
	    if (idio_eofp_file_handle (fh)) {
		return EOF;
	    }
	    if (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
		IDIO_FILE_HANDLE_COUNT (fh) -= 1;
		int c = (int) *(IDIO_FILE_HANDLE_PTR (fh));
		IDIO_FILE_HANDLE_PTR (fh) += 1;
		return c;
	    }
	}
    }
}

int idio_eofp_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    return (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_EOF);
}

int idio_close_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    if (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED) {
	idio_handle_error_closed (fh, IDIO_C_LOCATION ("idio_close_file_handle"));
	errno = EBADF;
	return EOF;
    } else {
	if (EOF == idio_flush_file_handle (fh)) {
	    return EOF;
	}

	IDIO_HANDLE_FLAGS (fh) |= IDIO_HANDLE_FLAG_CLOSED;
	idio_gc_deregister_finalizer (fh);
	return fclose (IDIO_FILE_HANDLE_FILEP (fh));
    }
}

int idio_putc_file_handle (IDIO fh, int c)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_handle_error_write (fh, IDIO_C_LOCATION ("idio_putc_file_handle"));
    }

    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) < IDIO_FILE_HANDLE_BUFSIZ (fh)) {
	    *(IDIO_FILE_HANDLE_PTR (fh)) = (char) c;
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    IDIO_FILE_HANDLE_COUNT (fh) += 1;

	    if ('\n' == c &&
		IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
		if (EOF == idio_flush_file_handle (fh)) {
		    return EOF;
		}
	    }
	    break;
	} else {
	    if (EOF == idio_flush_file_handle (fh)) {
		return EOF;
	    }
	}
    }

    return c;
}

size_t idio_puts_file_handle (IDIO fh, char *s, size_t slen)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_handle_error_write (fh, IDIO_C_LOCATION ("idio_puts_file_handle"));
    }

    size_t r;

    /*
     * If the string is going to cause a flush then flush and write
     * the string directly out
     */
    if (slen > IDIO_FILE_HANDLE_BUFSIZ (fh) ||
	slen > (IDIO_FILE_HANDLE_BUFSIZ (fh) - IDIO_FILE_HANDLE_COUNT (fh))) {
	if (EOF == idio_flush_file_handle (fh)) {
	    return EOF;
	}
	r = fwrite (s, 1, slen, IDIO_FILE_HANDLE_FILEP (fh));
	if (r < slen) {
	    idio_error_printf (IDIO_C_LOCATION ("idio_puts_file_handle"), "fwrite (%s) => %zd / %zd", IDIO_HANDLE_NAME (fh), r, slen);
	}

	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = 0;
    } else {
	memcpy (IDIO_FILE_HANDLE_PTR (fh), s, slen);
	IDIO_FILE_HANDLE_PTR (fh) += slen;
	IDIO_FILE_HANDLE_COUNT (fh) += slen;
	r = slen;
    }

    if (EOF == idio_flush_file_handle (fh)) {
	return EOF;
    }

    size_t nl = 0;
    size_t i;
    for (i = 0; i < slen; i++) {
	if ('\n' == s[i]) {
	    nl++;
	}
    }
    IDIO_HANDLE_LINE (fh) += nl;

    return r;
}

int idio_flush_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    /*
     * What does it mean to flush a file open for reading?  fflush(3)
     * "discards any buffered data that has been fetched from the
     * underlying file, but has not been consumed by the application."
     *
     * ??
     *
     * Anyway, all we do here is fwrite(3) the contents of *our*
     * buffer to the FILE* stream.
     *
     * Of course the FILE* stream is itself buffered (probably) so we
     * may not have achieved very much as far as a third party
     * process/observer is concerned.
     *
     * There's the file-handle-specific file-handle-fflush, below, if
     * needed.
     */
    if (IDIO_INPUTP_HANDLE (fh) &&
	! IDIO_OUTPUTP_HANDLE (fh)) {
	/* fprintf (stderr, "WARNING: flush (%s) open for reading\n", IDIO_HANDLE_NAME (fh)); */
    }

    int r = fwrite (IDIO_FILE_HANDLE_BUF (fh), 1, IDIO_FILE_HANDLE_COUNT (fh), IDIO_FILE_HANDLE_FILEP (fh));
    IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
    IDIO_FILE_HANDLE_COUNT (fh) = 0;

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("file-handle-fflush", file_handle_fflush, (IDIO fh))
{
    IDIO_ASSERT (fh);
    IDIO_VERIFY_PARAM_TYPE (file_handle, fh);

    idio_flush_file_handle (fh);
    int r = fflush (IDIO_FILE_HANDLE_FILEP (fh));

    return idio_C_int (r);
}

off_t idio_seek_file_handle (IDIO fh, off_t offset, int whence)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    if (feof (IDIO_FILE_HANDLE_FILEP (fh))) {
	clearerr (IDIO_FILE_HANDLE_FILEP (fh));
    }

    IDIO_FILE_HANDLE_FLAGS (fh) &= ~IDIO_FILE_HANDLE_FLAG_EOF;

    return lseek (IDIO_FILE_HANDLE_FD (fh), offset, whence);
}

void idio_print_file_handle (IDIO fh, IDIO o)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_handle_error_write (fh, IDIO_C_LOCATION ("idio_print_file_handle"));
    }

    char *os = idio_display_string (o);
    IDIO_HANDLE_M_PUTS (fh) (fh, os, strlen (os));
    IDIO_HANDLE_M_PUTS (fh) (fh, "\n", 1);
    free (os);
}

IDIO_DEFINE_PRIMITIVE1 ("close-file-handle-on-exec", close_file_handle_on_exec, (IDIO fh))
{
    IDIO_ASSERT (fh);

    IDIO_VERIFY_PARAM_TYPE (file_handle, fh);

    int fd = IDIO_FILE_HANDLE_FD (fh);

    int r = fcntl (fd, F_SETFD, FD_CLOEXEC);

    if (-1 == r) {
	idio_error_system_errno ("fcntl F_SETFD FD_CLOEXEC", IDIO_LIST1 (fh), IDIO_C_LOCATION ("close-file-handle-on-exec"));
    }

    IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_CLOEXEC;

    return idio_C_int (r);
}

IDIO idio_load_file_handle_interactive (IDIO fh, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs)
{
    IDIO_ASSERT (fh);
    IDIO_C_ASSERT (reader);
    IDIO_C_ASSERT (evaluator);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (file_handle, fh);
    IDIO_TYPE_ASSERT (array, cs);

    if (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED) {
	IDIO eh = idio_thread_current_error_handle ();
	idio_display_C ("ERROR: load-file-handle-interactive: ", eh);
	idio_display (fh, eh);
	idio_display_C (": handle already closed?\n", eh);
	return idio_S_false;
    }

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t sp0 = idio_array_size (IDIO_THREAD_STACK (thr));

    /*
     * When we call idio_vm_run() we are at risk of the garbage
     * collector being called so we need to save the current file
     * handle and any lists we're walking over
     */
    idio_remember_file_handle (fh);

    for (;;) {
	IDIO cm = idio_thread_current_module ();

	/*
	 * As we're interactive, make an attempt to flush stdout --
 	 * noting that stdout might no longer be a file-handle and
 	 * that a regular flush-handle merely shuffles our handle's
 	 * buffer into what is probably a FILE* buffer.
	 */
	IDIO oh = idio_thread_current_output_handle ();
	if (idio_isa_file_handle (oh)) {
	    fflush (IDIO_FILE_HANDLE_FILEP (oh));
	} else {
	    idio_flush_handle (oh);
	}
	IDIO eh = idio_thread_current_error_handle ();
	idio_display (IDIO_MODULE_NAME (cm), eh);
	idio_display_C ("> ", eh);

	IDIO e = (*reader) (fh);
	if (idio_S_eof == e) {
	    break;
	}

	IDIO m = (*evaluator) (e, cs);
	idio_codegen (thr, m, cs);
	IDIO r = idio_vm_run (thr);
	idio_debug ("%s\n", r);
    }

    IDIO_HANDLE_M_CLOSE (fh) (fh);

    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (thr));

    if (sp != sp0) {
	fprintf (stderr, "load-file-handle-interactive: %s: SP %td != SP0 %td\n", IDIO_HANDLE_NAME (fh), sp, sp0);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
    }

    idio_forget_file_handle (fh);

    return idio_S_unspec;
}

IDIO idio_load_file_handle_lbl (IDIO fh, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs)
{
    IDIO_ASSERT (fh);
    IDIO_C_ASSERT (reader);
    IDIO_C_ASSERT (evaluator);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (file_handle, fh);
    IDIO_TYPE_ASSERT (array, cs);

    if (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
	return idio_load_file_handle_interactive (fh, reader, evaluator, cs);
    }

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t ss0 = idio_array_size (IDIO_THREAD_STACK (thr));
    /* fprintf (stderr, "load-file-handle: %s\n", IDIO_HANDLE_NAME (fh)); */
    /* idio_debug ("THR %s\n", thr); */
    /* idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr)); */

    /*
     * When we call idio_vm_run() we are at risk of the garbage
     * collector being called so we need to save the current file
     * handle and any lists we're walking over
     */
    idio_remember_file_handle (fh);

    IDIO r = idio_S_unspec;

    for (;;) {
	IDIO e = (*reader) (fh);

	if (idio_S_eof == e) {
	    break;
	} else {
	    IDIO m = (*evaluator) (e, cs);

	    idio_ai_t lfh_pc = -1;

	    idio_codegen (thr, m, cs);

	    if (-1 == lfh_pc) {
		lfh_pc = IDIO_THREAD_PC (thr);
		/* fprintf (stderr, "\n\n%s lfh_pc == %jd\n", IDIO_HANDLE_NAME (fh), lfh_pc); */
	    }

	    IDIO_THREAD_PC (thr) = lfh_pc;
	    r = idio_vm_run (thr);
	}
    }

    idio_forget_file_handle (fh);

    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

    if (ss != ss0) {
	fprintf (stderr, "load-file-handle: %s: stack size %td != initial ss %td\n", IDIO_HANDLE_NAME (fh), ss, ss0);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
    }

    return r;
}

IDIO idio_load_file_handle_aio (IDIO fh, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs)
{
    IDIO_ASSERT (fh);
    IDIO_C_ASSERT (reader);
    IDIO_C_ASSERT (evaluator);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (file_handle, fh);
    IDIO_TYPE_ASSERT (array, cs);

    if (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
	return idio_load_file_handle_interactive (fh, reader, evaluator, cs);
    }

    int timing = 0;

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t ss0 = idio_array_size (IDIO_THREAD_STACK (thr));
    /* fprintf (stderr, "load-file-handle: %s\n", IDIO_HANDLE_NAME (fh)); */
    /* idio_debug ("THR %s\n", thr); */
    /* idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr)); */

    time_t s;
    suseconds_t us;
    struct timeval t0;
    gettimeofday (&t0, NULL);

    IDIO es = idio_S_nil;

    for (;;) {
	IDIO en = (*reader) (fh);

	if (idio_S_eof == en) {
	    break;
	} else {
	    es = idio_pair (en, es);
	}
    }
    /* e = idio_list_append2 (IDIO_LIST1 (idio_S_begin), idio_list_reverse (e)); */
    /* es = idio_list_reverse (es); */
    es = IDIO_LIST1 (idio_pair (idio_S_begin, idio_list_reverse (es)));
    /* idio_debug ("load-file-handle: es %s\n", es);    */

    IDIO_HANDLE_M_CLOSE (fh) (fh);

    struct timeval t_read;
    if (timing) {
	gettimeofday (&t_read, NULL);

	s = t_read.tv_sec - t0.tv_sec;
	us = t_read.tv_usec - t0.tv_usec;

	if (us < 0) {
	    us += 1000000;
	    s -= 1;
	}

	fprintf (stderr, "load-file-handle: %s: read time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);
    }

    IDIO ms = idio_S_nil;
    while (es != idio_S_nil) {
	ms = idio_pair ((*evaluator) (IDIO_PAIR_H (es), cs), ms);
	es = IDIO_PAIR_T (es);
    }
    ms = idio_list_reverse (ms);
    /* idio_debug ("load-file-handle: ms %s\n", ms);    */

    struct timeval te;
    if (timing) {
	gettimeofday (&te, NULL);

	s = te.tv_sec - t_read.tv_sec;
	us = te.tv_usec - t_read.tv_usec;

	if (us < 0) {
	    us += 1000000;
	    s -= 1;
	}

	fprintf (stderr, "load-file-handle: %s: evaluation time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);
    }

    /*
     * When we call idio_vm_run() we are at risk of the garbage
     * collector being called so we need to save the current file
     * handle and any lists we're walking over
     */
    idio_remember_file_handle (fh);
    /*
     * We might have called idio_gc_protect (and later idio_gc_expose)
     * to safeguard {ms} however we know (because we wrote the code)
     * that "load" might call a continuation (to a state before we
     * were called) which will unwind the stack and call longjmp(3).
     * That means we'll never reach the idio_gc_expose() call and
     * stuff starts to accumulate in the GC never to be released.
     *
     * However, invoking that continuation will clear the stack
     * including anything we stick on it here.  Very convenient.
     *
     * If you dump the stack you will find an enormous list, {ms},
     * representing the loaded file.  Which is annoying.
     */
    idio_array_push (IDIO_THREAD_STACK (thr), ms);

    idio_ai_t lfh_pc = -1;
    IDIO r;
    while (idio_S_nil != ms) {
	idio_codegen (thr, IDIO_PAIR_H (ms), cs);
	if (-1 == lfh_pc) {
	    lfh_pc = IDIO_THREAD_PC (thr);
	    /* fprintf (stderr, "\n\n%s lfh_pc == %jd\n", IDIO_HANDLE_NAME (fh), lfh_pc); */
	}
	/* r = idio_vm_run (thr); */
	ms = IDIO_PAIR_T (ms);
    }
    IDIO_THREAD_PC (thr) = lfh_pc;
    r = idio_vm_run (thr);

    /* ms */
    idio_array_pop (IDIO_THREAD_STACK (thr));

    struct timeval tr;
    gettimeofday (&tr, NULL);

    if (timing) {
	s = tr.tv_sec - te.tv_sec;
	us = tr.tv_usec - te.tv_usec;

	if (us < 0) {
	    us += 1000000;
	    s -= 1;
	}

	fprintf (stderr, "load-file-handle: %s: compile/run time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);
    }

    s = tr.tv_sec - t0.tv_sec;
    us = tr.tv_usec - t0.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }

#if IDIO_DEBUG
    /* fprintf (stderr, "load-file-handle: %s: elapsed time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000); */
    /* idio_debug (" => %s\n", r); */
#endif

    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

    if (ss != ss0) {
	fprintf (stderr, "load-file-handle: %s: stack size %td != initial ss %td\n", IDIO_HANDLE_NAME (fh), ss, ss0);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
    }

    idio_forget_file_handle (fh);

    return r;
}

typedef struct idio_file_extension_s {
    char *ext;
    IDIO (*reader) (IDIO h);
    IDIO (*evaluator) (IDIO e, IDIO cs);
    IDIO (*modulep) (void);
} idio_file_extension_t;

static idio_file_extension_t idio_file_extensions[] = {
    { NULL, idio_read, idio_evaluate, idio_Idio_module_instance },
    { ".idio", idio_read, idio_evaluate, idio_Idio_module_instance },
    /* { ".scm", idio_scm_read, idio_scm_evaluate, idio_main_scm_module_instance }, */
    { NULL, NULL, NULL }
};

char *idio_libfile_find_C (char *file)
{
    IDIO_C_ASSERT (file);

    size_t filelen = strlen (file);

    IDIO IDIOLIB = idio_module_current_symbol_value_recurse (idio_env_IDIOLIB_sym, idio_S_nil);

    /*
     * idiolib is the start of the current IDIOLIB pathname element --
     * colon will mark the end
     *
     * idiolibe is the end of the whole IDIOLIB string, used to
     * calculate when we've tried all parts
     */
    char *idiolib;
    char *idiolibe;
    if (idio_S_undef == IDIOLIB ||
	! idio_isa_string (IDIOLIB)) {
	idiolib = idio_env_IDIOLIB_default;
	idiolibe = idiolib + strlen (idiolib);
    } else {
	idiolib = idio_string_s (IDIOLIB);
	idiolibe = idiolib + idio_string_blen (IDIOLIB);
    }

    /*
     * find the longest filename extension -- so we don't overrun
     * PATH_MAX
     */
    size_t max_ext_len = 0;
    idio_file_extension_t *fe = idio_file_extensions;

    for (;NULL != fe->reader;fe++) {
	if (NULL != fe->ext) {
	    size_t el = strlen (fe->ext);
	    if (el > max_ext_len) {
		max_ext_len = el;
	    }
	}
    }

    /*
     * See comment in libc-wrap.c re: getcwd(3)
     */
    char libname[PATH_MAX];
    char cwd[PATH_MAX];
    if (getcwd (cwd, PATH_MAX) == NULL) {
	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_LOCATION ("idio_command_find_exe_C"));
    }
    size_t cwdlen = strlen (cwd);

    int done = 0;
    while (! done) {
	size_t idioliblen = idiolibe - idiolib;
	char * colon = NULL;

	if (0 == idioliblen) {
	    return NULL;
	}

	colon = memchr (idiolib, ':', idioliblen);

	if (NULL == colon) {
	    if ((idioliblen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
		idio_error_system ("dir+file.idio libname length", IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_LOCATION ("idio_libfile_find_C"));
	    }

	    memcpy (libname, idiolib, idioliblen);
	    libname[idioliblen] = '\0';
	} else {
	    size_t dirlen = colon - idiolib;

	    if (0 == dirlen) {
		/*
		 * An empty value, eg. ::, in IDIOLIB means PWD.  This
		 * is a hangover behaviour from the shell's PATH.
		 *
		 * Is that a good thing?
		 */
		if ((cwdlen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
		    idio_error_system ("cwd+file.idio libname length", IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_LOCATION ("idio_libfile_find_C"));
		}

		strcpy (libname, cwd);
	    } else {
		if ((dirlen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
		    idio_error_system ("dir+file.idio libname length", IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_LOCATION ("idio_libfile_find_C"));
		}

		memcpy (libname, idiolib, dirlen);
		libname[dirlen] = '\0';
	    }
	}

	strcat (libname, "/");
	strcat (libname, file);

	/*
	 * libname now contains "/path/to/file".
	 *
	 * We now try each extension in turn by maintaining lne which
	 * points at the end of the current value of libname.  That
	 * is, it points at the '\0' at the end of "/path/to/file".
	 */
	size_t lnlen = strlen (libname);
	char *lne = strrchr (libname, '\0');

	fe = idio_file_extensions;

	for (;NULL != fe->reader;fe++) {
	    if (NULL != fe->ext) {

		if ((lnlen + strlen (fe->ext)) >= PATH_MAX) {
		    idio_file_handle_error_malformed_filename (idio_string_C (libname), IDIO_C_LOCATION ("idio_libfile_find_C"));

		    /* notreached */
		    return NULL;
		}

		strncpy (lne, fe->ext, PATH_MAX - lnlen - 1);
	    }

	    if (access (libname, R_OK) == 0) {
		done = 1;
		break;
	    }

	    /* reset libname without ext */
	    *lne = '\0';
	}

	/*
	 * Sadly we can't do a double break without a GOTO -- which is
	 * considered harmful.  So we need to check and break again,
	 * here.
	 *
	 * No GOTOs in C, even though we've implemented continuations
	 * in Idio...
	 */
	if (done) {
	    break;
	}

	if (NULL == colon) {
	    /*
	     * Nothing left to try in IDIOLIB so as a final throw of
	     * the dice, let's try idio_env_IDIOLIB_default.
	     *
	     * Unless that was the last thing we tried.
	     */
	    size_t dl = strlen (idio_env_IDIOLIB_default);
	    if (strncmp (libname, idio_env_IDIOLIB_default, dl) == 0 &&
		'/' == libname[dl]) {
		done = 1;
		libname[0] = '\0';
		break;
	    } else {
		idiolib = idio_env_IDIOLIB_default;
		idiolibe = idiolib + strlen (idiolib);
		colon = idiolib - 1;
	    }
	}

	idiolib = colon + 1;
    }

    char *idiolibname = NULL;

    if (0 != libname[0]) {
	idiolibname = idio_alloc (strlen (libname) + 1);
	strcpy (idiolibname, libname);
    }

    return idiolibname;
}

char *idio_libfile_find (IDIO file)
{
    IDIO_ASSERT (file);
    IDIO_TYPE_ASSERT (string, file);

    char *file_C = idio_string_as_C (file);

    char *r = idio_libfile_find_C (file_C);

    free (file_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("find-lib", find_lib, (IDIO file))
{
    IDIO_ASSERT (file);
    IDIO_VERIFY_PARAM_TYPE (string, file);

    char *file_C = idio_string_as_C (file);

    char *r_C = idio_libfile_find_C (file_C);

    free (file_C);

    IDIO r = idio_S_nil;

    if (NULL != r_C) {
	r = idio_string_C (r_C);
	free (r_C);
    }

    return  r;
}

IDIO idio_load_file_name_lbl (IDIO filename, IDIO cs)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (cs);

    if (! idio_isa_string (filename)) {
	idio_error_param_type ("string", filename, IDIO_C_LOCATION ("idio_load_file_name"));
	return idio_S_unspec;
    }
    IDIO_TYPE_ASSERT (array, cs);

    char *filename_C = idio_string_as_C (filename);
    char lfn[PATH_MAX];
    size_t l;

    char *libfile = idio_libfile_find_C (filename_C);

    if (NULL == libfile) {
	idio_file_handle_error_filename_not_found (filename, IDIO_C_LOCATION ("idio_load_file_name"));

	/* notreached */
	return idio_S_notreached;
    }

    strncpy (lfn, libfile, PATH_MAX - 1);
    l = strlen (lfn);
    free (libfile);

    char *slash = strrchr (lfn, '/');
    if (NULL == slash) {
	slash = lfn;
    }

    char *dot = strrchr (slash, '.');

    if (NULL == dot) {
	dot = strrchr (slash, '\0'); /* end of string */

	idio_file_extension_t *fe = idio_file_extensions;

	for (;NULL != fe->reader;fe++) {
	    if (NULL != fe->ext) {

		if ((l + strlen (fe->ext)) >= PATH_MAX) {
		    idio_file_handle_error_malformed_filename (filename, IDIO_C_LOCATION ("idio_load_file_name"));
		    return idio_S_unspec;
		}

		strncpy (dot, fe->ext, PATH_MAX - l - 1);
	    }

	    if (access (lfn, R_OK) == 0) {
		IDIO fh = idio_open_file_handle_C (lfn, "r");

		free (filename_C);

		idio_thread_set_current_module ((*fe->modulep) ());
		return idio_load_file_handle_lbl (fh, fe->reader, fe->evaluator, cs);
	    }

	    /* reset lfn without ext */
	    *dot = '\0';
	}
    } else {
	IDIO (*reader) (IDIO h) = idio_read;
	IDIO (*evaluator) (IDIO e, IDIO cs) = idio_evaluate;

	idio_file_extension_t *fe = idio_file_extensions;

	for (;NULL != fe->reader;fe++) {
	    if (NULL != fe->ext) {
		if (strncmp (dot, fe->ext, strlen (fe->ext)) == 0) {
		    reader = fe->reader;
		    evaluator = fe->evaluator;
		    break;
		}
	    }
	}

	if (access (lfn, R_OK) == 0) {
	    IDIO fh = idio_open_file_handle_C (lfn, "r");

	    free (filename_C);

	    idio_thread_set_current_module ((*fe->modulep) ());
	    return idio_load_file_handle_lbl (fh, reader, evaluator, cs);
	}
    }

    idio_file_handle_error_filename_not_found (filename, IDIO_C_LOCATION ("idio_load_file_name"));
    return idio_S_unspec;
}

IDIO idio_load_file_name_aio (IDIO filename, IDIO cs)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (cs);

    if (! idio_isa_string (filename)) {
	idio_error_param_type ("string", filename, IDIO_C_LOCATION ("idio_load_file_name"));
	return idio_S_unspec;
    }
    IDIO_TYPE_ASSERT (array, cs);

    char *filename_C = idio_string_as_C (filename);
    char lfn[PATH_MAX];
    size_t l;

    char *libfile = idio_libfile_find_C (filename_C);

    if (NULL == libfile) {
	idio_file_handle_error_filename_not_found (filename, IDIO_C_LOCATION ("idio_load_file_name"));

	/* notreached */
	return idio_S_notreached;
    }

    strncpy (lfn, libfile, PATH_MAX - 1);
    l = strlen (lfn);
    free (libfile);

    char *slash = strrchr (lfn, '/');
    if (NULL == slash) {
	slash = lfn;
    }

    char *dot = strrchr (slash, '.');

    if (NULL == dot) {
	dot = strrchr (slash, '\0'); /* end of string */

	idio_file_extension_t *fe = idio_file_extensions;

	for (;NULL != fe->reader;fe++) {
	    if (NULL != fe->ext) {

		if ((l + strlen (fe->ext)) >= PATH_MAX) {
		    idio_file_handle_error_malformed_filename (filename, IDIO_C_LOCATION ("idio_load_file_name"));
		    return idio_S_unspec;
		}

		strncpy (dot, fe->ext, PATH_MAX - l - 1);
	    }

	    if (access (lfn, R_OK) == 0) {
		IDIO fh = idio_open_file_handle_C (lfn, "r");

		free (filename_C);

		idio_thread_set_current_module ((*fe->modulep) ());
		return idio_load_file_handle_aio (fh, fe->reader, fe->evaluator, cs);
	    }

	    /* reset lfn without ext */
	    *dot = '\0';
	}
    } else {
	IDIO (*reader) (IDIO h) = idio_read;
	IDIO (*evaluator) (IDIO e, IDIO cs) = idio_evaluate;

	idio_file_extension_t *fe = idio_file_extensions;

	for (;NULL != fe->reader;fe++) {
	    if (NULL != fe->ext) {
		if (strncmp (dot, fe->ext, strlen (fe->ext)) == 0) {
		    reader = fe->reader;
		    evaluator = fe->evaluator;
		    break;
		}
	    }
	}

	if (access (lfn, R_OK) == 0) {
	    IDIO fh = idio_open_file_handle_C (lfn, "r");

	    free (filename_C);

	    idio_thread_set_current_module ((*fe->modulep) ());
	    return idio_load_file_handle_aio (fh, reader, evaluator, cs);
	}
    }

    idio_file_handle_error_filename_not_found (filename, IDIO_C_LOCATION ("idio_load_file_name"));
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("load", load, (IDIO filename))
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    idio_thread_save_state (idio_thread_current_thread ());
    IDIO r = idio_load_file_name_aio (filename, idio_vm_constants);
    idio_thread_restore_state (idio_thread_current_thread ());

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("load-lbl", load_lbl, (IDIO filename))
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    idio_thread_save_state (idio_thread_current_thread ());
    IDIO r = idio_load_file_name_lbl (filename, idio_vm_constants);
    idio_thread_restore_state (idio_thread_current_thread ());

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("file-exists?", file_exists_p, (IDIO filename))
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    char *Cfn = idio_string_as_C (filename);

    IDIO r = idio_S_false;

    if (access (Cfn, R_OK) == 0) {
	r = idio_S_true;
    }

    free (Cfn);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("delete-file", delete_file, (IDIO filename))
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    char *Cfn = idio_string_as_C (filename);

    IDIO r = idio_S_false;

    if (remove (Cfn)) {
	free (Cfn);
	idio_file_handle_error_filename_delete (filename, IDIO_C_LOCATION ("delete-file"));
	return idio_S_unspec;
    } else {
	r = idio_S_true;
    }

    free (Cfn);

    return r;
}

void idio_init_file_handle ()
{
    idio_file_handles = IDIO_HASH_EQP (1<<3);
    idio_gc_protect (idio_file_handles);

    idio_stdin = idio_open_std_file_handle (stdin);
    idio_stdout = idio_open_std_file_handle (stdout);
    idio_stderr = idio_open_std_file_handle (stderr);
}

void idio_file_handle_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (open_file_handle_from_fd);
    IDIO_ADD_PRIMITIVE (open_input_file_handle_from_fd);
    IDIO_ADD_PRIMITIVE (open_output_file_handle_from_fd);
    IDIO_ADD_PRIMITIVE (open_file_handle);
    IDIO_ADD_PRIMITIVE (open_input_file_handle);
    IDIO_ADD_PRIMITIVE (open_output_file_handle);
    IDIO_ADD_PRIMITIVE (file_handlep);
    IDIO_ADD_PRIMITIVE (input_file_handlep);
    IDIO_ADD_PRIMITIVE (output_file_handlep);
    IDIO_ADD_PRIMITIVE (file_handle_fflush);
    IDIO_ADD_PRIMITIVE (file_handle_fd);
    IDIO_ADD_PRIMITIVE (find_lib);
    IDIO_ADD_PRIMITIVE (load);
    IDIO_ADD_PRIMITIVE (load_lbl);
    IDIO_ADD_PRIMITIVE (file_exists_p);
    IDIO_ADD_PRIMITIVE (delete_file);
    IDIO_ADD_PRIMITIVE (close_file_handle_on_exec);
}

void idio_final_file_handle ()
{
    IDIO fhl = idio_hash_keys_to_list (idio_file_handles);

    while (idio_S_nil != fhl) {
	IDIO fh = IDIO_PAIR_H (fhl);

	if (0 == (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED)) {
	    IDIO_HANDLE_M_CLOSE (fh) (fh);
	}

	fhl = IDIO_PAIR_T (fhl);
    }

    idio_gc_expose (idio_file_handles);
}

