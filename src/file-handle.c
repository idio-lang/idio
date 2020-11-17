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
    idio_getb_file_handle,
    idio_eofp_file_handle,
    idio_close_file_handle,
    idio_putb_file_handle,
    idio_putc_file_handle,
    idio_puts_file_handle,
    idio_flush_file_handle,
    idio_seek_file_handle,
    idio_print_file_handle
};

static void idio_file_handle_error_filename (IDIO filename, IDIO c_location)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("generic filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' error", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_get_output_string (dsh),
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_error_filename_C (char *name, IDIO c_location)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_file_handle_error_filename (idio_string_C (name), c_location);

    /* notreached */
}

static void idio_file_handle_error_filename_delete (IDIO filename, IDIO c_location)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("remove '", msh);
    idio_display (filename, msh);
    idio_display_C ("'", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_get_output_string (dsh),
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_error_filename_delete_C (char *name, IDIO c_location)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_file_handle_error_filename_delete (idio_string_C (name), c_location);

    /* notreached */
}

static void idio_file_handle_error_malformed_filename (IDIO filename, IDIO c_location)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("bad filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("'", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_malformed_filename_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_get_output_string (dsh),
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_error_malformed_filename_C (char *name, IDIO c_location)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_file_handle_error_malformed_filename (idio_string_C (name), c_location);

    /* notreached */
}

static void idio_file_handle_error_file_protection (IDIO filename, IDIO c_location)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' access", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_file_protection_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_get_output_string (dsh),
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_error_file_protection_C (char *name, IDIO c_location)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_file_handle_error_file_protection (idio_string_C (name), c_location);

    /* notreached */
}

static void idio_file_handle_error_filename_already_exists (IDIO filename, IDIO c_location)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' already exists", msh);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display_C (strerror (errno), dsh);

    IDIO c = idio_struct_instance (idio_condition_io_file_already_exists_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_get_output_string (dsh),
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_error_filename_already_exists_C (char *name, IDIO c_location)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_file_handle_error_filename_already_exists (idio_string_C (name), c_location);

    /* notreached */
}

static void idio_file_handle_error_filename_not_found (IDIO filename, IDIO c_location)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, filename);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("filename '", msh);
    idio_display (filename, msh);
    idio_display_C ("' not found", msh);

    IDIO c = idio_struct_instance (idio_condition_io_no_such_file_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       c_location,
					       idio_S_nil,
					       filename));

    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_file_handle_error_filename_not_found_C (char *name, IDIO c_location)
{
    IDIO_C_ASSERT (name);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_file_handle_error_filename_not_found (idio_string_C (name), c_location);

    /* notreached */
}

void idio_file_handle_error_format (char *m, IDIO s, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (s);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_C (m, s, c_location);

    /* notreached */
}

static IDIO idio_open_file_handle (IDIO filename, char *pathname, FILE *filep, int h_flags, int s_flags)
{
    IDIO_ASSERT (filename);
    IDIO_C_ASSERT (pathname);
    IDIO_C_ASSERT (filep);

    IDIO_TYPE_ASSERT (string, filename);

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

    IDIO_HANDLE_FLAGS (fh) |= h_flags | IDIO_HANDLE_FLAG_FILE;
    IDIO_HANDLE_FILENAME (fh) = filename;
    IDIO_HANDLE_PATHNAME (fh) = idio_string_C (pathname);
    IDIO_HANDLE_STREAM (fh) = fhsp;
    IDIO_HANDLE_METHODS (fh) = &idio_file_handle_methods;

    if ((s_flags & IDIO_FILE_HANDLE_FLAG_STDIO) == 0) {
	idio_gc_register_finalizer (fh, idio_file_handle_finalizer);
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
	    size_t size = 0;
	    char *s = idio_string_as_C (iname, &size);
	    size_t C_size = strlen (s);
	    if (C_size != size) {
		IDIO_GC_FREE (s);

		idio_file_handle_error_format ("open-file-from-fd: name contains an ASCII NUL", iname, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    if (size >= PATH_MAX) {
		IDIO_GC_FREE (s);

		idio_error_C ("name too long", iname, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    sprintf (name, "%s", s);

	    IDIO_GC_FREE (s);

	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", iname, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    int free_mode = 0;
    char *mode = "re";

    if (idio_S_nil != args) {
	IDIO imode = IDIO_PAIR_H (args);
	if (idio_isa_string (imode)) {
	    size_t size = 0;
	    mode = idio_string_as_C (imode, &size);
	    size_t C_size = strlen (mode);
	    if (C_size != size) {
		IDIO_GC_FREE (mode);

		idio_file_handle_error_format ("open-file-from-fd: mode contains an ASCII NUL", imode, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    free_mode = 1;
	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", imode, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    FILE *filep = fdopen (fd, mode);
    if (NULL == filep) {
	if (free_mode) {
	    IDIO_GC_FREE (mode);
	}

	idio_error_system_errno ("fdopen", IDIO_LIST2 (idio_string_C (name), idio_string_C (mode)), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int mflag = IDIO_HANDLE_FLAG_READ;
    if (strchr (mode, '+') != NULL) {
	mflag |= IDIO_HANDLE_FLAG_WRITE;
    }

    if (free_mode) {
	IDIO_GC_FREE (mode);
    }

    return idio_open_file_handle (idio_string_C (name), name, filep, mflag, IDIO_FILE_HANDLE_FLAG_NONE);
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
	    size_t size = 0;
	    char *s = idio_string_as_C (iname, &size);
	    size_t C_size = strlen (s);
	    if (C_size != size) {
		IDIO_GC_FREE (s);

		idio_file_handle_error_format ("open-input-file-from-fd: name contains an ASCII NUL", iname, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    if (size >= PATH_MAX) {
		IDIO_GC_FREE (s);

		idio_error_C ("name too long", iname, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    sprintf (name, "%s", s);
	    IDIO_GC_FREE (s);

	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", iname, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    int free_mode = 0;
    char *mode = "re";

    if (idio_S_nil != args) {
	IDIO imode = IDIO_PAIR_H (args);
	if (idio_isa_string (imode)) {
	    size_t size = 0;
	    mode = idio_string_as_C (imode, &size);
	    size_t C_size = strlen (mode);
	    if (C_size != size) {
		IDIO_GC_FREE (mode);

		idio_file_handle_error_format ("open-input-file-from-fd: mode contains an ASCII NUL", imode, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    free_mode = 1;
	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", imode, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    FILE *filep = fdopen (fd, mode);
    if (NULL == filep) {
	if (free_mode) {
	    IDIO_GC_FREE (mode);
	}

	idio_error_system_errno ("fdopen", IDIO_LIST2 (idio_string_C (name), idio_string_C (mode)), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int mflag = IDIO_HANDLE_FLAG_READ;
    if (strchr (mode, '+') != NULL) {
	mflag |= IDIO_HANDLE_FLAG_WRITE;
    }

    if (free_mode) {
	IDIO_GC_FREE (mode);
    }

    return idio_open_file_handle (idio_string_C (name), name, filep, mflag, IDIO_FILE_HANDLE_FLAG_NONE);
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
	    size_t size = 0;
	    char *s = idio_string_as_C (iname, &size);
	    size_t C_size = strlen (s);
	    if (C_size != size) {
		IDIO_GC_FREE (s);

		idio_file_handle_error_format ("open-output-file-from-fd: name contains an ASCII NUL", iname, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    if (size >= PATH_MAX) {
		IDIO_GC_FREE (s);

		idio_error_C ("name too long", iname, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    sprintf (name, "%s", s);
	    IDIO_GC_FREE (s);

	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", iname, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    int free_mode = 0;
    char *mode = "we";

    if (idio_S_nil != args) {
	IDIO imode = IDIO_PAIR_H (args);
	if (idio_isa_string (imode)) {
	    size_t size = 0;
	    mode = idio_string_as_C (imode, &size);
	    size_t C_size = strlen (mode);
	    if (C_size != size) {
		IDIO_GC_FREE (mode);

		idio_file_handle_error_format ("open-output-file-from-fd: mode contains an ASCII NUL", imode, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }

	    free_mode = 1;
	    args = IDIO_PAIR_T (args);
	} else {
	    idio_error_param_type ("string", imode, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    FILE *filep = fdopen (fd, mode);
    if (NULL == filep) {
	if (free_mode) {
	    IDIO_GC_FREE (mode);
	}

	idio_error_system_errno ("fdopen", IDIO_LIST2 (idio_string_C (name), idio_string_C (mode)), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    int mflag = IDIO_HANDLE_FLAG_WRITE;
    if (strchr (mode, '+') != NULL) {
	mflag |= IDIO_HANDLE_FLAG_READ;
    }

    if (free_mode) {
	IDIO_GC_FREE (mode);
    }

    return idio_open_file_handle (idio_string_C (name), name, filep, mflag, IDIO_FILE_HANDLE_FLAG_NONE);
}

IDIO idio_open_file_handle_C (IDIO filename, char *pathname, int free_pathname, char *mode, int free_mode)
{
    IDIO_ASSERT (filename);	/* the user supplied name */
    IDIO_C_ASSERT (pathname);
    IDIO_C_ASSERT (mode);

    IDIO_TYPE_ASSERT (string, filename);

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
	if (free_pathname) {
	    IDIO_GC_FREE (pathname);
	}
	if (free_mode) {
	    IDIO_GC_FREE (mode);
	}

	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected mode", mode);

	return idio_S_notreached;
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
	filep = fopen (pathname, mode);
	if (NULL == filep) {
	    switch (errno) {
	    case EMFILE:	/* process max */
	    case ENFILE:	/* system max */
		idio_gc_collect ("idio_open_file_handle_C");
		break;
	    case EACCES:
		{
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode) {
			IDIO_GC_FREE (mode);
		    }

		    idio_file_handle_error_file_protection (pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case EEXIST:
		{
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode) {
			IDIO_GC_FREE (mode);
		    }

		    idio_file_handle_error_filename_already_exists (pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case ENAMETOOLONG:
		{
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode) {
			IDIO_GC_FREE (mode);
		    }

		    idio_file_handle_error_malformed_filename (pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case ENOENT:
		{
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode) {
			IDIO_GC_FREE (mode);
		    }

		    idio_file_handle_error_filename_not_found (pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    case ENOTDIR:
		{
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode) {
			IDIO_GC_FREE (mode);
		    }

		    idio_file_handle_error_filename (pn, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    default:
		{
		    IDIO pn = idio_string_C (pathname);

		    if (free_pathname) {
			IDIO_GC_FREE (pathname);
		    }
		    if (free_mode) {
			IDIO_GC_FREE (mode);
		    }

		    idio_error_system_errno ("fopen", IDIO_LIST1 (pn), IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}
	    }
	} else {
	    break;
	}
    }

    if (NULL == filep) {
	IDIO pn = idio_string_C (pathname);
	IDIO m = idio_string_C (mode);

	if (free_pathname) {
	    IDIO_GC_FREE (pathname);
	}
	if (free_mode) {
	    IDIO_GC_FREE (mode);
	}

	idio_error_system_errno ("fopen (final)", IDIO_LIST2 (pn, m), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
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
	    IDIO pn = idio_string_C (pathname);
	    IDIO m = idio_string_C (mode);

	    if (free_pathname) {
		IDIO_GC_FREE (pathname);
	    }
	    if (free_mode) {
		IDIO_GC_FREE (mode);
	    }

	    idio_error_system_errno ("fcntl F_SETFD FD_CLOEXEC", IDIO_LIST3 (pn, m, idio_C_int (fd)), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
#endif
    }

    return idio_open_file_handle (filename, pathname, filep, h_flags, s_flags);
}

IDIO_DEFINE_PRIMITIVE2 ("open-file", open_file_handle, (IDIO name, IDIO mode))
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (mode);

    size_t name_size = 0;
    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name, &name_size);
	size_t C_size = strlen (name_C);
	if (C_size != name_size) {
	    IDIO_GC_FREE (name_C);

	    idio_file_handle_error_format ("open-file: name contains an ASCII NUL", name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    default:
	idio_error_param_type ("string", name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
	break;
    }

    size_t mode_size = 0;
    char *mode_C = NULL;

    switch (idio_type (mode)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	mode_C = idio_string_as_C (mode, &mode_size);
	size_t C_size = strlen (mode_C);
	if (C_size != mode_size) {
	    IDIO_GC_FREE (name_C);
	    IDIO_GC_FREE (mode_C);

	    idio_file_handle_error_format ("open-file: mode contains an ASCII NUL", mode, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    default:
	IDIO_GC_FREE (name_C);

	idio_error_param_type ("string", mode, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
	break;
    }

    IDIO r = idio_open_file_handle_C (name, name_C, 1, mode_C, 1);

    IDIO_GC_FREE (mode_C);
    IDIO_GC_FREE (name_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("open-input-file", open_input_file_handle, (IDIO name))
{
    IDIO_ASSERT (name);

    size_t name_size = 0;
    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name, &name_size);
	size_t C_size = strlen (name_C);
	if (C_size != name_size) {
	    IDIO_GC_FREE (name_C);

	    idio_file_handle_error_format ("open-input-file: name contains an ASCII NUL", name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    default:
	idio_error_param_type ("string", name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
	break;
    }

    IDIO r = idio_open_file_handle_C (name, name_C, 1, "re", 0);

    IDIO_GC_FREE (name_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("open-output-file", open_output_file_handle, (IDIO name))
{
    IDIO_ASSERT (name);

    size_t name_size = 0;
    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name, &name_size);
	size_t C_size = strlen (name_C);
	if (C_size != name_size) {
	    IDIO_GC_FREE (name_C);

	    idio_file_handle_error_format ("open-output-file: name contains an ASCII NUL", name, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    default:
	idio_error_param_type ("string", name, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
	break;
    }

    IDIO r = idio_open_file_handle_C (name, name_C, 1, "we", 0);

    IDIO_GC_FREE (name_C);

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
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected standard IO stream");

	return idio_S_notreached;
    }

    return idio_open_file_handle (idio_string_C (name), name, filep, mflag, IDIO_FILE_HANDLE_FLAG_STDIO);
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

    if (idio_isa_handle (o) &&
	IDIO_HANDLE_FLAGS (o) & IDIO_HANDLE_FLAG_FILE) {
	return 1;
    }

    return 0;
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

    IDIO_GC_FREE (IDIO_FILE_HANDLE_BUF (fh));
    IDIO_GC_FREE (IDIO_HANDLE_STREAM (fh));
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

    ssize_t nread = read (IDIO_FILE_HANDLE_FD (fh), IDIO_FILE_HANDLE_BUF (fh), IDIO_FILE_HANDLE_BUFSIZ (fh));
    if (-1 == nread) {
	perror ("read");
    } else if (0 == nread) {
	IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_EOF;
    } else {
	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = nread;
    }
}

int idio_getb_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (! idio_input_file_handlep (fh)) {
	idio_handle_error_read (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
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
	idio_handle_error_closed (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
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

int idio_putb_file_handle (IDIO fh, uint8_t c)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_handle_error_write (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
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

    return 1;
}

int idio_putc_file_handle (IDIO fh, idio_unicode_t c)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_handle_error_write (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    char buf[4];
    int size;
    idio_utf8_code_point (c, buf, &size);

    for (int n = 0;n < size;n++) {
	if (IDIO_FILE_HANDLE_COUNT (fh) < IDIO_FILE_HANDLE_BUFSIZ (fh)) {
	    *(IDIO_FILE_HANDLE_PTR (fh)) = buf[n];
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

    return size;
}

ptrdiff_t idio_puts_file_handle (IDIO fh, char *s, size_t slen)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_handle_error_write (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
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
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "fwrite (%s) => %zd / %zd", idio_handle_name_as_C (fh), r, slen);

	    /* notreached */
	    return EOF;
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
	idio_handle_error_write (fh, IDIO_C_FUNC_LOCATION ());

	/* notreached */
    }

    size_t size = 0;
    char *os = idio_display_string (o, &size);
    IDIO_HANDLE_M_PUTS (fh) (fh, os, size);
    IDIO_HANDLE_M_PUTS (fh) (fh, "\n", 1);
    IDIO_GC_FREE (os);
}

IDIO_DEFINE_PRIMITIVE1 ("close-file-handle-on-exec", close_file_handle_on_exec, (IDIO fh))
{
    IDIO_ASSERT (fh);

    IDIO_VERIFY_PARAM_TYPE (file_handle, fh);

    int fd = IDIO_FILE_HANDLE_FD (fh);

    int r = fcntl (fd, F_SETFD, FD_CLOEXEC);

    if (-1 == r) {
	idio_error_system_errno ("fcntl F_SETFD FD_CLOEXEC", IDIO_LIST1 (fh), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_FILE_HANDLE_FLAGS (fh) |= IDIO_FILE_HANDLE_FLAG_CLOEXEC;

    return idio_C_int (r);
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
    char *idiolib_copy = NULL;
    char *idiolib;
    char *idiolibe;
    if (idio_S_undef == IDIOLIB ||
	! idio_isa_string (IDIOLIB)) {
	idiolib = idio_env_IDIOLIB_default;
	idiolibe = idiolib + strlen (idiolib);
    } else {
	size_t size = 0;
	idiolib_copy = idio_string_as_C (IDIOLIB, &size);
	size_t C_size = strlen (idiolib_copy);
	if (C_size != size) {
	    IDIO_GC_FREE (idiolib_copy);

	    idio_file_handle_error_format ("find-C: IDIOLIB contains an ASCII NUL", IDIOLIB, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	idiolib = idiolib_copy;
	idiolibe = idiolib + idio_string_len (IDIOLIB);
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
	if (idiolib_copy) {
	    IDIO_GC_FREE (idiolib_copy);
	}

	idio_error_system_errno ("getcwd", idio_S_nil, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }
    size_t cwdlen = strlen (cwd);

    int done = 0;
    while (! done) {
	size_t idioliblen = idiolibe - idiolib;
	char * colon = NULL;

	if (0 == idioliblen) {
	    if (idiolib_copy) {
		IDIO_GC_FREE (idiolib_copy);
	    }

	    return NULL;
	}

	colon = memchr (idiolib, ':', idioliblen);

	if (NULL == colon) {
	    if ((idioliblen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
		if (idiolib_copy) {
		    IDIO_GC_FREE (idiolib_copy);
		}

		idio_error_system ("dir+file.idio libname length", IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		/* notreached */
		return NULL;
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
		    if (idiolib_copy) {
			IDIO_GC_FREE (idiolib_copy);
		    }

		    idio_error_system ("cwd+file.idio libname length", IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
		}

		strcpy (libname, cwd);
	    } else {
		if ((dirlen + 1 + filelen + max_ext_len + 1) >= PATH_MAX) {
		    if (idiolib_copy) {
			IDIO_GC_FREE (idiolib_copy);
		    }

		    idio_error_system ("dir+file.idio libname length", IDIO_LIST2 (IDIOLIB, idio_string_C (file)), ENAMETOOLONG, IDIO_C_FUNC_LOCATION ());

		    /* notreached */
		    return NULL;
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
		    if (idiolib_copy) {
			IDIO_GC_FREE (idiolib_copy);
		    }

		    idio_file_handle_error_malformed_filename (idio_string_C (libname), IDIO_C_FUNC_LOCATION ());

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

    if (idiolib_copy) {
	IDIO_GC_FREE (idiolib_copy);
    }

    return idiolibname;
}

char *idio_libfile_find (IDIO file)
{
    IDIO_ASSERT (file);
    IDIO_TYPE_ASSERT (string, file);

    size_t size = 0;
    char *file_C = idio_string_as_C (file, &size);
    size_t C_size = strlen (file_C);
    if (C_size != size) {
	IDIO_GC_FREE (file_C);

	idio_file_handle_error_format ("find-lib: file contains an ASCII NUL", file, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    char *r = idio_libfile_find_C (file_C);

    IDIO_GC_FREE (file_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("find-lib", find_lib, (IDIO file))
{
    IDIO_ASSERT (file);
    IDIO_VERIFY_PARAM_TYPE (string, file);

    char *r_C = idio_libfile_find (file);

    IDIO r = idio_S_nil;

    if (NULL != r_C) {
	r = idio_string_C (r_C);
	IDIO_GC_FREE (r_C);
    }

    return  r;
}

IDIO idio_load_file_name (IDIO filename, IDIO cs)
{
    IDIO_ASSERT (filename);
    IDIO_ASSERT (cs);

    if (! idio_isa_string (filename)) {
	idio_error_param_type ("string", filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    IDIO_TYPE_ASSERT (array, cs);

    size_t size = 0;
    char *filename_C = idio_string_as_C (filename, &size);
    size_t C_size = strlen (filename_C);
    if (C_size != size) {
	IDIO_GC_FREE (filename_C);

	idio_file_handle_error_format ("load-file: filename contains an ASCII NUL", filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    char lfn[PATH_MAX];
    size_t l;

    char *libfile = idio_libfile_find_C (filename_C);

    if (NULL == libfile) {
	IDIO_GC_FREE (filename_C);

	idio_file_handle_error_filename_not_found (filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    strncpy (lfn, libfile, PATH_MAX - 1);
    l = strlen (lfn);
    IDIO_GC_FREE (libfile);

    char *filename_slash = strrchr (filename_C, '/');
    if (NULL == filename_slash) {
	filename_slash = filename_C;
    }

    char *filename_dot = strrchr (filename_slash, '.');

    char *lfn_slash = strrchr (lfn, '/');
    if (NULL == lfn_slash) {
	lfn_slash = lfn;
    }

    char *lfn_dot = strrchr (lfn_slash, '.');

    if (NULL == lfn_dot) {
	lfn_dot = strrchr (lfn_slash, '\0'); /* end of string */

	idio_file_extension_t *fe = idio_file_extensions;

	for (;NULL != fe->reader;fe++) {
	    IDIO filename_ext = filename;

	    if (NULL != fe->ext) {

		if ((l + strlen (fe->ext)) >= PATH_MAX) {
		    IDIO_GC_FREE (filename_C);

		    idio_file_handle_error_malformed_filename (filename, IDIO_C_FUNC_LOCATION ());

		    return idio_S_notreached;
		}

		strncpy (lfn_dot, fe->ext, PATH_MAX - l - 1);

		char *ss[] = { filename_C, fe->ext };

		filename_ext = idio_string_C_array (2, ss);

		idio_gc_protect (filename_ext);
	    }

	    if (access (lfn, R_OK) == 0) {
		IDIO fh = idio_open_file_handle_C (filename_ext, lfn, 0, "r", 0);

		IDIO_GC_FREE (filename_C);

		if (filename_ext != filename) {
		    idio_gc_expose (filename_ext);
		}

		/* idio_thread_set_current_module ((*fe->modulep) ()); */
		return idio_load_handle (fh, fe->reader, fe->evaluator, cs);
	    }

	    if (filename_ext != filename) {
		idio_gc_expose (filename_ext);
	    }

	    /* reset lfn without ext */
	    *lfn_dot = '\0';
	}
    } else {
	IDIO (*reader) (IDIO h) = idio_read;
	IDIO (*evaluator) (IDIO e, IDIO cs) = idio_evaluate;

	idio_file_extension_t *fe = idio_file_extensions;
	IDIO filename_ext = filename;

	for (;NULL != fe->reader;fe++) {
	    if (NULL != fe->ext) {
		if (strncmp (lfn_dot, fe->ext, strlen (fe->ext)) == 0) {
		    reader = fe->reader;
		    evaluator = fe->evaluator;

		    /*
		     * If it's not the same extension as the user gave
		     * us then tack it on the end
		     */
		    if (NULL == filename_dot ||
			strncmp (filename_dot, fe->ext, strlen (fe->ext))) {

			char *ss[] = { filename_C, fe->ext };

			filename_ext = idio_string_C_array (2, ss);

			idio_gc_protect (filename_ext);
		    }
		    break;
		}
	    }
	}

	if (access (lfn, R_OK) == 0) {
	    IDIO fh = idio_open_file_handle_C (filename_ext, lfn, 0, "r", 0);

	    IDIO_GC_FREE (filename_C);

	    if (filename_ext != filename) {
		idio_gc_expose (filename_ext);
	    }

	    /* idio_thread_set_current_module ((*fe->modulep) ()); */
	    return idio_load_handle (fh, reader, evaluator, cs);
	}

	if (filename_ext != filename) {
	    idio_gc_expose (filename_ext);
	}
    }

    IDIO_GC_FREE (filename_C);

    idio_file_handle_error_filename_not_found (filename, IDIO_C_FUNC_LOCATION ());

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE1_DS ("load", load, (IDIO filename), "filename", "\
load ``filename`` expression by expression			\n\
								\n\
:param filename: the file to load				\n\
:type filename: string						\n\
								\n\
The system will use the environment variable ``IDIOLIB`` to	\n\
find ``filename``.						\n\
								\n\
This is the ``load`` primitive.					\n\
")
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t pc0 = IDIO_THREAD_PC (thr);

    /*
     * Explicitly disable interactive for the duration of a load
     *
     * It will be reset just prior to the prompt.
     */
    idio_command_interactive = 0;
    IDIO r = idio_load_file_name (filename, idio_vm_constants);

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("file-exists?", file_exists_p, (IDIO filename))
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    size_t size = 0;
    char *filename_C = idio_string_as_C (filename, &size);
    size_t C_size = strlen (filename_C);
    if (C_size != size) {
	IDIO_GC_FREE (filename_C);

	idio_file_handle_error_format ("file-exists?: filename contains an ASCII NUL", filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (access (filename_C, R_OK) == 0) {
	r = idio_S_true;
    }

    IDIO_GC_FREE (filename_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("delete-file", delete_file, (IDIO filename))
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    size_t size = 0;
    char *filename_C = idio_string_as_C (filename, &size);
    size_t C_size = strlen (filename_C);
    if (C_size != size) {
	IDIO_GC_FREE (filename_C);

	idio_file_handle_error_format ("delete-file: filename contains an ASCII NUL", filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (remove (filename_C)) {
	IDIO_GC_FREE (filename_C);

	idio_file_handle_error_filename_delete (filename, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    } else {
	r = idio_S_true;
    }

    IDIO_GC_FREE (filename_C);

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

