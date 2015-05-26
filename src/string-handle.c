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
 * string-handle.c
 * 
 */

#include "idio.h"

typedef struct idio_string_handle_stream_s {
    char *buf;			/* buffer */
    size_t blen;
    char *ptr;			/* ptr into buffer */
    char *end;			/* end of buffer */
    char eof;			/* EOF flag */
} idio_string_handle_stream_t;

#define IDIO_STRING_HANDLE_STREAM_BUF(S)  ((S)->buf)
#define IDIO_STRING_HANDLE_STREAM_BLEN(S) ((S)->blen)
#define IDIO_STRING_HANDLE_STREAM_PTR(S)  ((S)->ptr)
#define IDIO_STRING_HANDLE_STREAM_END(S)  ((S)->end)
#define IDIO_STRING_HANDLE_STREAM_EOF(S)  ((S)->eof)

#define IDIO_STRING_HANDLE_BUF(H)  IDIO_STRING_HANDLE_STREAM_BUF((idio_string_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_STRING_HANDLE_BLEN(H) IDIO_STRING_HANDLE_STREAM_BLEN((idio_string_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_STRING_HANDLE_PTR(H)  IDIO_STRING_HANDLE_STREAM_PTR((idio_string_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_STRING_HANDLE_END(H)  IDIO_STRING_HANDLE_STREAM_END((idio_string_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_STRING_HANDLE_EOF(H)  IDIO_STRING_HANDLE_STREAM_EOF((idio_string_handle_stream_t *) IDIO_HANDLE_STREAM(H))

#define IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE	256

static idio_handle_methods_t idio_string_handle_methods = {
    idio_string_handle_free,
    idio_string_handle_readyp,
    idio_string_handle_getc,
    idio_string_handle_eofp,
    idio_string_handle_close,
    idio_string_handle_putc,
    idio_string_handle_puts,
    idio_string_handle_flush,
    idio_string_handle_seek,
    idio_string_handle_print
};

static IDIO idio_open_string_handle (char *str, size_t blen, int sflags)
{
    IDIO_C_ASSERT (str);

    /*
     * We could open an input-string-handle from "" so blen can be 0
     */
    
    idio_string_handle_stream_t *shsp = idio_alloc (sizeof (idio_string_handle_stream_t));

    IDIO_STRING_HANDLE_STREAM_BUF (shsp) = str;
    IDIO_STRING_HANDLE_STREAM_BLEN (shsp) = blen;
    IDIO_STRING_HANDLE_STREAM_PTR (shsp) = IDIO_STRING_HANDLE_STREAM_BUF (shsp);

    if (sflags & IDIO_HANDLE_FLAG_WRITE) {
	IDIO_STRING_HANDLE_STREAM_END (shsp) = str;
    } else {
	IDIO_STRING_HANDLE_STREAM_END (shsp) = str + blen;
    }

    IDIO_STRING_HANDLE_STREAM_EOF (shsp) = 0;
    
    IDIO sh = idio_handle ();

    IDIO_HANDLE_FLAGS (sh) |= sflags | IDIO_HANDLE_FLAG_STRING;
    IDIO_HANDLE_NAME (sh) = "string handle";
    IDIO_HANDLE_STREAM (sh) = shsp;
    IDIO_HANDLE_METHODS (sh) = &idio_string_handle_methods;

    return sh;
}

IDIO idio_open_input_string_handle_C (char *str)
{
    IDIO_C_ASSERT (str);

    size_t blen = strlen (str);
    char *str_copy = idio_alloc (blen);
    strncpy (str_copy, str, blen);

    return idio_open_string_handle (str_copy, blen, IDIO_HANDLE_FLAG_READ);
}

IDIO idio_open_output_string_handle_C ()
{
    char *str_C = idio_alloc (IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE);

    return idio_open_string_handle (str_C, IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE, IDIO_HANDLE_FLAG_WRITE);
}

IDIO_DEFINE_PRIMITIVE1 ("open-input-string", open_input_string_handle, (IDIO str))
{
    IDIO_ASSERT (str);

    char *str_C = NULL;

    switch (idio_type (str)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	str_C = idio_string_as_C (str);
	break;
    default:
	idio_error_param_type ("string", str);
	break;
    }
    
    IDIO r = idio_open_string_handle (str_C, strlen (str_C), IDIO_HANDLE_FLAG_READ);

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("open-output-string", open_output_string_handle, (void))
{
    char *str_C = idio_alloc (IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE);
    
    return idio_open_string_handle (str_C, IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE, IDIO_HANDLE_FLAG_WRITE);
}

int idio_isa_string_handle (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_handle (o) &&
	    IDIO_HANDLE_FLAGS (o) & IDIO_HANDLE_FLAG_STRING);
}

IDIO_DEFINE_PRIMITIVE1 ("string-handle?", string_handlep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_string_handle (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_input_string_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_string_handle (o) &&
	    IDIO_HANDLE_INPUTP (o));
}

IDIO_DEFINE_PRIMITIVE1 ("input-string-handle?", input_string_handlep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_input_string_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

int idio_output_string_handlep (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_isa_string_handle (o) &&
	    IDIO_HANDLE_OUTPUTP (o));
}

IDIO_DEFINE_PRIMITIVE1 ("output-string-handle?", output_string_handlep, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_output_string_handlep (o)) {
	r = idio_S_true;
    }

    return r;
}

void idio_string_handle_free (IDIO sh)
{
    IDIO_ASSERT (sh);

    free (IDIO_STRING_HANDLE_BUF (sh));
    free (IDIO_HANDLE_STREAM (sh));
}

int idio_string_handle_readyp (IDIO sh)
{
    IDIO_ASSERT (sh);

    return (idio_string_handle_eofp (sh) == 0);
}

int idio_string_handle_getc (IDIO sh)
{
    IDIO_ASSERT (sh);

    if (! idio_input_string_handlep (sh)) {
	idio_error_param_type ("input string-handle", sh);
	IDIO_C_ASSERT (0);
    }

    if (IDIO_STRING_HANDLE_PTR (sh) < IDIO_STRING_HANDLE_END (sh)) {
	int c = (int) *(IDIO_STRING_HANDLE_PTR (sh));
	IDIO_STRING_HANDLE_PTR (sh) += 1;
	return c;
    } else {
	IDIO_STRING_HANDLE_EOF (sh) = 1;
	return EOF;
    }
}

int idio_string_handle_eofp (IDIO sh)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    return IDIO_STRING_HANDLE_EOF (sh);
}

int idio_string_handle_close (IDIO sh)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    return 0;
}

int idio_string_handle_putc (IDIO sh, int c)
{
    IDIO_ASSERT (sh);

    if (! idio_output_string_handlep (sh)) {
	idio_error_param_type ("output string-handle", sh);
	IDIO_C_ASSERT (0);
    }
    
    if (IDIO_STRING_HANDLE_PTR (sh) >= IDIO_STRING_HANDLE_END (sh)) {
	if (IDIO_STRING_HANDLE_END (sh) == (IDIO_STRING_HANDLE_BUF (sh) + IDIO_STRING_HANDLE_BLEN (sh))) {
	    size_t blen = IDIO_STRING_HANDLE_BLEN (sh);
	    char *buf = IDIO_STRING_HANDLE_BUF (sh);
	    buf = idio_realloc (buf, blen + blen / 2);

	    /*
	     * realloc can relocate data in memory!
	     */
	    IDIO_STRING_HANDLE_BUF (sh) = buf;
	    IDIO_STRING_HANDLE_PTR (sh) = buf + IDIO_STRING_HANDLE_BLEN (sh);
	    IDIO_STRING_HANDLE_BLEN (sh) = blen;
	}

	IDIO_STRING_HANDLE_END (sh) = IDIO_STRING_HANDLE_PTR (sh) + 1;
    }
    
    *(IDIO_STRING_HANDLE_PTR (sh)) = (char) c;
    IDIO_STRING_HANDLE_PTR (sh) += 1;

    return c;
}

size_t idio_string_handle_puts (IDIO sh, char *s, size_t slen)
{
    IDIO_ASSERT (sh);

    if (! idio_output_string_handlep (sh)) {
	idio_error_param_type ("output string-handle", sh);
	IDIO_C_ASSERT (0);
    }
    
    if ((IDIO_STRING_HANDLE_PTR (sh) + slen) >= (IDIO_STRING_HANDLE_BUF (sh) + IDIO_STRING_HANDLE_BLEN (sh))) {
	size_t blen = IDIO_STRING_HANDLE_BLEN (sh);
	char *buf = IDIO_STRING_HANDLE_BUF (sh);
	size_t offset = IDIO_STRING_HANDLE_PTR (sh) - buf;
	buf = idio_realloc (buf, blen + slen + IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE);

	/*
	 * realloc can relocate data in memory!
	 */
	IDIO_STRING_HANDLE_BUF (sh) = buf;
	IDIO_STRING_HANDLE_PTR (sh) = buf + offset;
	IDIO_STRING_HANDLE_BLEN (sh) = blen;
	IDIO_STRING_HANDLE_END (sh) = IDIO_STRING_HANDLE_PTR (sh) + slen;
    }
    memcpy (IDIO_STRING_HANDLE_PTR (sh), s, slen);
    IDIO_STRING_HANDLE_PTR (sh) += slen;
    if (IDIO_STRING_HANDLE_PTR (sh) > IDIO_STRING_HANDLE_END (sh)) {
	IDIO_STRING_HANDLE_END (sh) = IDIO_STRING_HANDLE_PTR (sh);
    }

    return slen;
}

int idio_string_handle_flush (IDIO sh)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    return 0;
}

off_t idio_string_handle_seek (IDIO sh, off_t offset, int whence)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    char *ptr;

    switch (whence) {
    case SEEK_SET:
	ptr = IDIO_STRING_HANDLE_BUF (sh) + offset;
	break;
    case SEEK_CUR:
	ptr = IDIO_STRING_HANDLE_PTR (sh) + offset;
	break;
    case SEEK_END:
	ptr = IDIO_STRING_HANDLE_END (sh) + offset;
	break;
    default:
	idio_error_message ("idio_string_handle_seek: unexpected whence %d", whence);
	return -1;
    }

    /*
     * This allows you to seek to one beyond the end of the string
     */
    if (IDIO_STRING_HANDLE_BUF (sh) <= ptr &&
	ptr <= IDIO_STRING_HANDLE_END (sh)) {
	IDIO_STRING_HANDLE_EOF (sh) = 0;
	IDIO_STRING_HANDLE_PTR (sh) = ptr;
	return (ptr - IDIO_STRING_HANDLE_BUF (sh));
    } else {
	return -1;
    }
}

void idio_string_handle_print (IDIO sh, IDIO o)
{
    IDIO_ASSERT (sh);

    if (! idio_output_string_handlep (sh)) {
	idio_error_param_type ("output string-handle", sh);
	IDIO_C_ASSERT (0);
    }

    char *os = idio_display_string (o);
    IDIO_HANDLE_M_PUTS (sh) (sh, os, strlen (os));
    IDIO_HANDLE_M_PUTS (sh) (sh, "\n", 1);
    free (os);
}

IDIO idio_get_output_string (IDIO sh)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    return idio_string_C_len (IDIO_STRING_HANDLE_BUF (sh), IDIO_STRING_HANDLE_END (sh) - IDIO_STRING_HANDLE_BUF (sh));
}

IDIO_DEFINE_PRIMITIVE1 ("get-output-string", get_output_string, (IDIO sh))
{
    IDIO_ASSERT (sh);

    IDIO_VERIFY_PARAM_TYPE (string_handle, sh);

    return idio_get_output_string (sh);
}

void idio_init_string_handle ()
{
}

void idio_string_handle_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (open_input_string_handle);
    IDIO_ADD_PRIMITIVE (open_output_string_handle);
    IDIO_ADD_PRIMITIVE (get_output_string);
    IDIO_ADD_PRIMITIVE (string_handlep);
    IDIO_ADD_PRIMITIVE (input_string_handlep);
    IDIO_ADD_PRIMITIVE (output_string_handlep);
}

void idio_final_string_handle ()
{
}

