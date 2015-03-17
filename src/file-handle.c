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

#define IDIO_FILE_HANDLE_FLAG_NONE	  0
#define IDIO_FILE_HANDLE_FLAG_EOF	  (1<<0)
#define IDIO_FILE_HANDLE_FLAG_INTERACTIVE (1<<1)

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

void idio_init_file_handle ()
{
    idio_collector_protect (idio_G_frame, idio_file_handles);

    
}

void idio_final_file_handle ()
{
    while (idio_S_nil != idio_file_handles) {
	IDIO fh = IDIO_PAIR_H (idio_file_handles);
	idio_file_handles = IDIO_PAIR_T (idio_file_handles);

	IDIO_HANDLE_M_CLOSE (fh) (idio_G_frame, fh);
    }
}

IDIO idio_open_file_handle_C (IDIO f, char *name, char *mode)
{
    IDIO_ASSERT (f);
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
	idio_error_file_handle_mode (f, name, mode);
	break;
    }

    FILE *filep = fopen (name, mode);
    if (NULL == filep) {
	idio_error_file_handle_open (f, name, mode);
    }

    idio_file_handle_stream_t *fhsp = idio_alloc (sizeof (idio_file_handle_stream_t));
    int bufsiz = BUFSIZ;

    int fd = fileno (filep);

    int sflags = IDIO_FILE_HANDLE_FLAG_NONE;
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

    IDIO fh = idio_handle (f);

    IDIO_HANDLE_FLAGS (fh) |= mflag | IDIO_HANDLE_FLAG_FILE;
    IDIO_HANDLE_NAME (fh) = name;
    IDIO_HANDLE_STREAM (fh) = fhsp;
    IDIO_HANDLE_METHODS (fh) = &idio_file_handle_methods;
    
    return fh;
}

int idio_isa_file_handle (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    return (idio_isa_handle (f, fh) &&
	    IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_FILE);
}

int idio_input_file_handlep (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    return (idio_isa_handle (f, fh) &&
	    IDIO_HANDLE_INPUTP (fh) &&
	    IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_FILE);
}

int idio_output_file_handlep (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    return (idio_isa_handle (f, fh) &&
	    IDIO_HANDLE_OUTPUTP (fh) &&
	    IDIO_HANDLE_FLAGS (fh) & IDIO_HANDLE_FLAG_FILE);
}

void idio_file_handle_finalizer (IDIO fh)
{
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_isa_opaque (idio_G_frame, fh));
}

void idio_register_file_handle (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    idio_file_handles = idio_pair (f, fh, idio_file_handles);
}

void idio_deregister_file_handle (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    idio_file_handles = idio_pair (f, fh, idio_file_handles);
}

int idio_file_handle_readyp (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    if (IDIO_FILE_HANDLE_COUNT (fh) > 0) {
	return 1;
    }
    
    return feof (IDIO_FILE_HANDLE_FILEP (fh));
}

void idio_file_handle_read_more (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    if (IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
	/*
	 * fgets sets EOF if it saw EOF even if it read something.  In
	 * practice you need to check for EOF before calling fgets the
	 * next time round...
	 */
	if (idio_file_handle_eofp (f, fh)) {
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
	if (idio_file_handle_eofp (f, fh)) {
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

int idio_file_handle_getc (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_input_file_handlep (f, fh));

    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) >= 1) {
	    IDIO_FILE_HANDLE_COUNT (fh) -= 1;
	    int c = (int) *(IDIO_FILE_HANDLE_PTR (fh));
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    return c;
	} else {
	    idio_file_handle_read_more (f, fh);
	    if (idio_file_handle_eofp (f, fh)) {
		return EOF;
	    }
	}
    }
}

int idio_file_handle_eofp (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_isa_file_handle (f, fh));

    return (IDIO_FILE_HANDLE_STREAM_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_EOF);
}

int idio_file_handle_close (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_isa_file_handle (f, fh));

    idio_file_handle_flush (f, fh);

    return fclose (IDIO_FILE_HANDLE_FILEP (fh));
}

int idio_file_handle_putc (IDIO f, IDIO fh, int c)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_output_file_handlep (f, fh));
    
    for (;;) {
	if (IDIO_FILE_HANDLE_COUNT (fh) < IDIO_FILE_HANDLE_BUFSIZ (fh)) {
	    *(IDIO_FILE_HANDLE_PTR (fh)) = (char) c;
	    IDIO_FILE_HANDLE_PTR (fh) += 1;
	    IDIO_FILE_HANDLE_COUNT (fh) += 1;

	    if ('\n' == c &&
		IDIO_FILE_HANDLE_FLAGS (fh) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
		if (EOF == idio_file_handle_flush (f, fh)) {
		    return EOF;
		}
	    }
	    break;
	} else {
	    if (EOF == idio_file_handle_flush (f, fh)) {
		return EOF;
	    }	    
	}
    }

    return c;
}

int idio_file_handle_puts (IDIO f, IDIO fh, char *s, size_t l)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    size_t r;
    
    /*
     * If the string is going to cause a flush then flush and write
     * the string directly out
     */
    if (l > IDIO_FILE_HANDLE_BUFSIZ (fh) ||
	l > (IDIO_FILE_HANDLE_BUFSIZ (fh) - IDIO_FILE_HANDLE_COUNT (fh))) {
	if (EOF == idio_file_handle_flush (f, fh)) {
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
	memcpy (IDIO_FILE_HANDLE_BUF (fh), s, l);
	IDIO_FILE_HANDLE_PTR (fh) += l;
	IDIO_FILE_HANDLE_COUNT (fh) += l;
	r = l;
    }

    return r;
}

int idio_file_handle_flush (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_isa_file_handle (f, fh));

    /*
     * What does it mean to flush a file open for reading?  fflush(3)
     * "discards any buffered data that has been fetched from the
     * underlying file, but has not been consumed by the application."
     *
     * ??
     */
    if (IDIO_HANDLE_INPUTP (fh)) {
	fprintf (stderr, "WARNING: flush (%s) open for reading\n", IDIO_HANDLE_NAME (fh));
    }

    int r = fwrite (IDIO_FILE_HANDLE_BUF (fh), 1, IDIO_FILE_HANDLE_COUNT (fh), IDIO_FILE_HANDLE_FILEP (fh));
    IDIO_FILE_HANDLE_PTR (fh) = IDIO_FILE_HANDLE_BUF (fh);
    IDIO_FILE_HANDLE_COUNT (fh) = 0;

    return r;
}

off_t idio_file_handle_seek (IDIO f, IDIO fh, off_t offset, int whence)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_isa_file_handle (f, fh));

    return lseek (IDIO_FILE_HANDLE_FD (fh), offset, whence);
}

void idio_file_handle_print (IDIO f, IDIO fh, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (idio_output_file_handlep (f, fh));

    IDIO_C_ASSERT (0);
    
}

IDIO idio_defprimitive_open_file_handle (IDIO f, IDIO name, IDIO mode)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (name);
    IDIO_ASSERT (mode);

    char *name_C = NULL;

    switch (name->type) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	name_C = idio_string_as_C (f, name);
	break;
    default:
	idio_error_bad_argument (f, name);
	break;
    }
    
    char *mode_C = NULL;

    switch (mode->type) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	mode_C = idio_string_as_C (f, mode);
	break;
    default:
	idio_error_bad_argument (f, mode);
	break;
    }
    
    IDIO r = idio_open_file_handle_C (f, name_C, mode_C);

    free (mode_C);
    free (name_C);

    return r;
}

IDIO idio_load_file (IDIO f, IDIO fh)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (fh);

    IDIO_C_ASSERT (0);
    return idio_S_unspec;
}

IDIO idio_defprimitive_load_file (IDIO f, IDIO filename)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (filename);

    if (! idio_isa_string (f, filename)) {
	fprintf (stderr, "ERROR: load-file string\n");
	IDIO_C_ASSERT (0);
    }

    char *filename_C = idio_string_as_C (f, filename);
    
    IDIO fh = idio_open_file_handle_C (f, filename_C, "r");

    free (filename_C);

    return idio_load_file (f, fh);
}
