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
 * file-handle.c
 * 
 */

#include "idio.h"

static IDIO idio_file_handles = idio_S_nil;
static IDIO idio_stdin = idio_S_nil;
static IDIO idio_stdout = idio_S_nil;
static IDIO idio_stderr = idio_S_nil;

#define IDIO_FILE_HANDLE_FLAG_NONE	  0
#define IDIO_FILE_HANDLE_FLAG_EOF	  (1<<0)
#define IDIO_FILE_HANDLE_FLAG_INTERACTIVE (1<<1)
#define IDIO_FILE_HANDLE_FLAG_STDIO	  (1<<2)

typedef struct idio_file_handle_stream_s {
    FILE *filep;		/* or NULL! */
    int fd;
    int flags;			/* IDIO_FILE_HANDLE_FLAG_* */
    char *buf;			/* buffer */
    int bufsiz;
    char *ptr;			/* ptr into buffer */
    int count;			/* bytes in buffer */

} idio_file_handle_stream_t;

#define IDIO_FILE_HANDLE_STREAM_FILEP(S)  ((S)->filep)
#define IDIO_FILE_HANDLE_STREAM_FD(S)     ((S)->fd)
#define IDIO_FILE_HANDLE_STREAM_FLAGS(S)  ((S)->flags)
#define IDIO_FILE_HANDLE_STREAM_BUF(S)    ((S)->buf)
#define IDIO_FILE_HANDLE_STREAM_BUFSIZ(S) ((S)->bufsiz)
#define IDIO_FILE_HANDLE_STREAM_PTR(S)    ((S)->ptr)
#define IDIO_FILE_HANDLE_STREAM_COUNT(S)  ((S)->count)

#define IDIO_FILE_HANDLE_FILEP(H)  IDIO_FILE_HANDLE_STREAM_FILEP((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_FD(H)     IDIO_FILE_HANDLE_STREAM_FD((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_FLAGS(H)  IDIO_FILE_HANDLE_STREAM_FLAGS((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_BUF(H)    IDIO_FILE_HANDLE_STREAM_BUF((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_BUFSIZ(H) IDIO_FILE_HANDLE_STREAM_BUFSIZ((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_PTR(H)    IDIO_FILE_HANDLE_STREAM_PTR((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_COUNT(H)  IDIO_FILE_HANDLE_STREAM_COUNT((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))

static idio_handle_methods_t idio_file_handle_methods = {
    idio_file_handle_free,
    idio_file_handle_readyp,
    idio_file_handle_getc,
    idio_file_handle_eofp,
    idio_file_handle_close,
    idio_file_handle_putc,
    idio_file_handle_puts,
    idio_file_handle_flush,
    idio_file_handle_seek,
    idio_file_handle_print
};

static void idio_error_file_closed (IDIO fh)
{
    idio_error_message ("file-handle %s already closed", IDIO_HANDLE_NAME (fh));
}

static void idio_error_filename_too_long (IDIO filename)
{
    idio_error_message ("filename %s is too long", IDIO_STRING_S (filename));
}

static void idio_error_file_not_found (IDIO filename)
{
    idio_error_message ("filename %s not found", IDIO_STRING_S (filename));
}

static void idio_error_file_delete (IDIO filename)
{
    idio_error_message ("remove (%s): %s", IDIO_STRING_S (filename), strerror (errno));
}

static IDIO idio_open_file_handle (char *name, FILE *filep, int mflag, int sflags)
{
    IDIO_C_ASSERT (filep);
    
    idio_file_handle_stream_t *fhsp = idio_alloc (sizeof (idio_file_handle_stream_t));
    int bufsiz = BUFSIZ;

    int fd = fileno (filep);

    if (isatty (fd)) {
	sflags |= IDIO_FILE_HANDLE_FLAG_INTERACTIVE;
    }
    
    IDIO_FILE_HANDLE_STREAM_FILEP (fhsp) = filep;
    IDIO_FILE_HANDLE_STREAM_FD (fhsp) = fd;
    IDIO_FILE_HANDLE_STREAM_FLAGS (fhsp) = sflags;
    IDIO_FILE_HANDLE_STREAM_BUF (fhsp) = idio_alloc (bufsiz);
    IDIO_FILE_HANDLE_STREAM_BUFSIZ (fhsp) = bufsiz;
    IDIO_FILE_HANDLE_STREAM_PTR (fhsp) = IDIO_FILE_HANDLE_STREAM_BUF (fhsp);
    IDIO_FILE_HANDLE_STREAM_COUNT (fhsp) = 0;

    IDIO fh = idio_handle ();

    char *name_copy = idio_alloc (strlen (name) + 1);
    strcpy (name_copy, name);

    IDIO_HANDLE_FLAGS (fh) |= mflag | IDIO_HANDLE_FLAG_FILE;
    IDIO_HANDLE_NAME (fh) = name_copy;
    IDIO_HANDLE_STREAM (fh) = fhsp;
    IDIO_HANDLE_METHODS (fh) = &idio_file_handle_methods;

    if ((sflags & IDIO_FILE_HANDLE_FLAG_STDIO) == 0) {
	idio_register_finalizer (fh, idio_file_handle_finalizer);
	/* idio_register_file_handle (fh); */
    }
    
    return fh;
}

IDIO idio_open_file_handle_C (char *name, char *mode)
{
    IDIO_C_ASSERT (name);
    IDIO_C_ASSERT (mode);

    int mflag = 0;
    
    switch (mode[0]) {
    case 'r':
	mflag = IDIO_HANDLE_FLAG_READ;
	if ('+' == mode[1]) {
	    mflag |= IDIO_HANDLE_FLAG_WRITE;
	}
	break;
    case 'a':
    case'w':
	mflag = IDIO_HANDLE_FLAG_WRITE;
	if ('+' == mode[1]) {
	    mflag |= IDIO_HANDLE_FLAG_READ;
	}
	break;
    default:
	idio_error_message ("unexpected mode", mode);
	break;
    }

    FILE *filep;
    int tries;
    for (tries = 2; tries > 0 ; tries--) {
	filep = fopen (name, mode);
	if (NULL == filep) {
	    idio_gc_collect ();
	} else {
	    break;
	}
    }

    if (NULL == filep) {
	idio_error_message ("fopen (\"%s\", \"%s\"): %s", name, mode, strerror (errno));
    }

    return idio_open_file_handle (name, filep, mflag, IDIO_FILE_HANDLE_FLAG_NONE);
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
	idio_error_message ("unexpected standard IO stream");
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

int idio_isa_file_handle (IDIO fh)
{
    IDIO_ASSERT (fh);

    return (idio_isa_handle (fh) &&
	    IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_FILE);
}

int idio_input_file_handlep (IDIO fh)
{
    IDIO_ASSERT (fh);

    return (idio_isa_handle (fh) &&
	    IDIO_HANDLE_INPUTP (fh) &&
	    IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_FILE);
}

int idio_output_file_handlep (IDIO fh)
{
    IDIO_ASSERT (fh);

    return (idio_isa_handle (fh) &&
	    IDIO_HANDLE_OUTPUTP (fh) &&
	    IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_FILE);
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

void idio_file_handle_free (IDIO fh)
{
    IDIO_ASSERT (fh);

    free (IDIO_FILE_HANDLE_BUF (fh));
    free (IDIO_HANDLE_STREAM (fh));
    free (IDIO_HANDLE_NAME (fh));
}

int idio_file_handle_readyp (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (IDIO_FILE_HANDLE_COUNT (fh) > 0) {
	return 1;
    }
    
    return feof (IDIO_FILE_HANDLE_FILEP (fh));
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
	if (idio_file_handle_eofp (fh)) {
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
	if (idio_file_handle_eofp (fh)) {
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

int idio_file_handle_getc (IDIO fh)
{
    IDIO_ASSERT (fh);

    if (! idio_input_file_handlep (fh)) {
	idio_error_param_type ("input file-handle", fh);
	IDIO_C_ASSERT (0);
    }

    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) >= 1) {
	    IDIO_FILE_HANDLE_COUNT (fh) -= 1;
	    int c = (int) *(IDIO_FILE_HANDLE_PTR (fh));
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    return c;
	} else {
	    idio_file_handle_read_more (fh);
	    if (idio_file_handle_eofp (fh)) {
		return EOF;
	    }
	}
    }
}

int idio_file_handle_eofp (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    return (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_EOF);
}

int idio_file_handle_close (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    /* idio_debug ("file-handle-close: %s\n", fh); */

    if (IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_CLOSED) {
	errno = EBADF;
	return EOF;
    } else {
	if (EOF == idio_file_handle_flush (fh)) {
	    return EOF;
	}

	IDIO_HANDLE_FLAGS (fh) |= IDIO_HANDLE_FLAG_CLOSED;
	idio_deregister_finalizer (fh);
	/* idio_deregister_file_handle (fh); */
	return fclose (IDIO_FILE_HANDLE_FILEP (fh));
    }
}

int idio_file_handle_putc (IDIO fh, int c)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_error_param_type ("output file-handle", fh);
	IDIO_C_ASSERT (0);
    }
    
    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) < IDIO_FILE_HANDLE_BUFSIZ (fh)) {
	    *(IDIO_FILE_HANDLE_PTR (fh)) = (char) c;
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    IDIO_FILE_HANDLE_COUNT (fh) += 1;

	    if ('\n' == c &&
		IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
		if (EOF == idio_file_handle_flush (fh)) {
		    return EOF;
		}
	    }
	    break;
	} else {
	    if (EOF == idio_file_handle_flush (fh)) {
		return EOF;
	    }	    
	}
    }

    return c;
}

int idio_file_handle_puts (IDIO fh, char *s, size_t l)
{
    IDIO_ASSERT (fh);

    size_t r;
    
    /*
     * If the string is going to cause a flush then flush and write
     * the string directly out
     */
    if (l > IDIO_FILE_HANDLE_BUFSIZ (fh) ||
	l > (IDIO_FILE_HANDLE_BUFSIZ (fh) - IDIO_FILE_HANDLE_COUNT (fh))) {
	if (EOF == idio_file_handle_flush (fh)) {
	    return EOF;
	}
	r = fwrite (s, 1, l, IDIO_FILE_HANDLE_FILEP (fh));
	if (r < l) {
	    fprintf (stderr, "puts: fwrite (%s) => %zd / %zd\n", IDIO_HANDLE_NAME (fh), r, l);
	    IDIO_C_ASSERT (0);
	}

	IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
	IDIO_FILE_HANDLE_COUNT (fh) = 0;
    } else {
	memcpy (IDIO_FILE_HANDLE_PTR (fh), s, l);
	IDIO_FILE_HANDLE_PTR (fh) += l;
	IDIO_FILE_HANDLE_COUNT (fh) += l;
	r = l;
    }

    idio_file_handle_flush (fh);
    return r;
}

int idio_file_handle_flush (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    /*
     * What does it mean to flush a file open for reading?  fflush(3)
     * "discards any buffered data that has been fetched from the
     * underlying file, but has not been consumed by the application."
     *
     * ??
     */
    if (IDIO_HANDLE_INPUTP (fh) &&
	! IDIO_HANDLE_OUTPUTP (fh)) {
	/* fprintf (stderr, "WARNING: flush (%s) open for reading\n", IDIO_HANDLE_NAME (fh)); */
    }

    int r = fwrite (IDIO_FILE_HANDLE_BUF (fh), 1, IDIO_FILE_HANDLE_COUNT (fh), IDIO_FILE_HANDLE_FILEP (fh));
    IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
    IDIO_FILE_HANDLE_COUNT (fh) = 0;

    return r;
}

off_t idio_file_handle_seek (IDIO fh, off_t offset, int whence)
{
    IDIO_ASSERT (fh);

    IDIO_TYPE_ASSERT (file_handle, fh);

    return lseek (IDIO_FILE_HANDLE_FD (fh), offset, whence);
}

void idio_file_handle_print (IDIO fh, IDIO o)
{
    IDIO_ASSERT (fh);

    if (! idio_output_file_handlep (fh)) {
	idio_error_param_type ("output file-handle", fh);
	IDIO_C_ASSERT (0);
    }

    char *os = idio_display_string (o);
    IDIO_HANDLE_M_PUTS (fh) (fh, os, strlen (os));
    IDIO_HANDLE_M_PUTS (fh) (fh, "\n", 1);
    free (os);
}

IDIO_DEFINE_PRIMITIVE2 ("open-file", open_file, (IDIO name, IDIO mode))
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
	idio_error_param_type ("string", name);
	break;
    }
    
    char *mode_C = NULL;

    switch (idio_type (mode)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	mode_C = idio_string_as_C (mode);
	break;
    default:
	idio_error_param_type ("string", mode);
	break;
    }
    
    IDIO r = idio_open_file_handle_C (name_C, mode_C);

    free (mode_C);
    free (name_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("open-input-file", open_input_file, (IDIO name))
{
    IDIO_ASSERT (name);

    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name);
	break;
    default:
	idio_error_param_type ("string", name);
	break;
    }
    
    IDIO r = idio_open_file_handle_C (name_C, "r");

    free (name_C);

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("open-output-file", open_output_file, (IDIO name))
{
    IDIO_ASSERT (name);

    char *name_C = NULL;

    switch (idio_type (name)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (name);
	break;
    default:
	idio_error_param_type ("string", name);
	break;
    }
    
    IDIO r = idio_open_file_handle_C (name_C, "w");

    free (name_C);

    return r;
}

IDIO idio_load_filehandle (IDIO fh, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO h))
{
    IDIO_ASSERT (fh);
    IDIO_C_ASSERT (reader);

    fprintf (stderr, "load-file-handle: %s: start\n", IDIO_HANDLE_NAME (fh)); 
    /* idio_debug ("%s\n", idio_current_thread ()); */
    idio_ai_t sp0 = idio_array_size (IDIO_THREAD_STACK (idio_current_thread ()));

    /*
     * When we call (*evaluator) (e) we are at risk of the garbage
     * collector being called so we need to save the current file
     * handle so it isn't collected
     */
    idio_remember_file_handle (fh);
    
    struct timeval t0;
    gettimeofday (&t0, NULL);

    IDIO m = idio_pair (idio_S_nil, idio_S_nil);
    idio_gc_protect (m);

    IDIO e = idio_S_nil;
    
    for (;;) {
	IDIO en = (*reader) (fh);
	
	if (idio_S_eof == en) {
	    fprintf (stderr, "load-file-handle: %s: EOF\n", IDIO_HANDLE_NAME (fh));
	    break;
	} else {
	    e = idio_pair (en, e);
	}
    }
    
    IDIO_HANDLE_M_CLOSE (fh) (fh);

    struct timeval t_read;
    gettimeofday (&t_read, NULL);

    time_t s = t_read.tv_sec - t0.tv_sec;
    suseconds_t us = t_read.tv_usec - t0.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }
	
    fprintf (stderr, "load-file-handle: %s: read time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);

    e = idio_list_append2 (IDIO_LIST1 (idio_S_begin), idio_list_reverse (e));
    /* idio_debug ("load-file-handle: e %s\n", e); */
    IDIO_PAIR_H (m) = (*evaluator) (e);

    struct timeval te;
    gettimeofday (&te, NULL);

    s = te.tv_sec - t_read.tv_sec;
    us = te.tv_usec - t_read.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }
	
    fprintf (stderr, "load-file-handle: %s: evaluation time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);

    IDIO thr = idio_current_thread ();
    idio_vm_codegen (thr, IDIO_PAIR_H (m));

    idio_gc_expose (m);

    struct timeval tc;
    gettimeofday (&tc, NULL);
	
    s = tc.tv_sec - te.tv_sec;
    us = tc.tv_usec - te.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }
	
    fprintf (stderr, "load-file-handle: %s: compile time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);

    /* idio_debug ("load-file-handle: THR %s\n", idio_current_thread ()); */
    IDIO r = idio_vm_run (idio_current_thread (), 1);

    struct timeval tr;
    gettimeofday (&tr, NULL);
	
    s = tr.tv_sec - tc.tv_sec;
    us = tr.tv_usec - tc.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }
	
    fprintf (stderr, "load-file-handle: %s: run time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);

    s = tr.tv_sec - t0.tv_sec;
    us = tr.tv_usec - t0.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }
	
    fprintf (stderr, "load-file-handle: %s: elapsed time %ld.%03ld\n", IDIO_HANDLE_NAME (fh), s, (long) us / 1000);
    idio_debug (" => %s\n", r);
    
    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (idio_current_thread ()));
    /* fprintf (stderr, "load-file-handle: %-20s: end   ", IDIO_HANDLE_NAME (fh)); */
    /* idio_debug ("%s\n", idio_current_thread ()); */

    if (sp != sp0) {
	fprintf (stderr, "load-file-handle: %s: SP %zd != %zd: ", IDIO_HANDLE_NAME (fh), sp, sp0);
	idio_debug ("%s\n", IDIO_THREAD_STACK (idio_current_thread ()));
    } else {
	/* fprintf (stderr, "load-file-handle: SP %zd == %zd: ", sp, sp0); */
	/* idio_debug ("%s\n", IDIO_THREAD_STACK (idio_current_thread ())); */
    }
    idio_forget_file_handle (fh);
    
    return idio_S_true;
}

typedef struct idio_file_extension_s {
    char *ext;
    IDIO (*reader) (IDIO h);
    IDIO (*evaluator) (IDIO h);
} idio_file_extension_t;

static idio_file_extension_t idio_file_extensions[] = {
    { NULL, idio_scm_read, idio_scm_evaluate },
    { ".idio", idio_read, idio_evaluate },
    { ".scm", idio_scm_read, idio_scm_evaluate },
    { NULL, NULL }
};

IDIO idio_load_file (IDIO filename)
{
    IDIO_ASSERT (filename);

    if (! idio_isa_string (filename)) {
	idio_error_param_type ("string", filename);
	return idio_S_unspec;
    }

    char *filename_C = idio_string_as_C (filename);
    char lfn[PATH_MAX];
    size_t l;
    
    strncpy (lfn, filename_C, PATH_MAX - 1);
    l = strlen (lfn);

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
		    idio_error_filename_too_long (filename);
		    return idio_S_unspec;
		}
	    
		strncpy (dot, fe->ext, PATH_MAX - l - 1);
	    }

	    if (access (lfn, R_OK) == 0) {
		IDIO fh = idio_open_file_handle_C (lfn, "r");

		free (filename_C);

		return idio_load_filehandle (fh, fe->reader, fe->evaluator);
	    }

	    /* reset lfn without ext */
	    *dot = '\0';
	}
    } else {
	IDIO (*reader) (IDIO h) = idio_read;
	IDIO (*evaluator) (IDIO h) = idio_evaluate;
	
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

	    return idio_load_filehandle (fh, reader, evaluator);
	}	
    }

    idio_error_file_not_found (filename);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("load", load, (IDIO filename))
{
    IDIO_ASSERT (filename);

    IDIO_VERIFY_PARAM_TYPE (string, filename);

    idio_thread_save_state (idio_current_thread ());
    IDIO r = idio_load_file (filename);
    idio_thread_restore_state (idio_current_thread ());
    
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
	idio_error_file_delete (filename);
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
    IDIO_ADD_PRIMITIVE (open_file);
    IDIO_ADD_PRIMITIVE (open_input_file);
    IDIO_ADD_PRIMITIVE (open_output_file);
    IDIO_ADD_PRIMITIVE (load);
    IDIO_ADD_PRIMITIVE (file_exists_p);
    IDIO_ADD_PRIMITIVE (delete_file);
}

void idio_final_file_handle ()
{
    IDIO fhl = idio_hash_keys_to_list (idio_file_handles);
    
    while (idio_S_nil != fhl) {
	IDIO fh = IDIO_PAIR_H (fhl);

	IDIO_HANDLE_M_CLOSE (fh) (fh);

	fhl = IDIO_PAIR_T (fhl);
    }

    idio_gc_expose (idio_file_handles);
}

