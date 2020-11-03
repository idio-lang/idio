/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

static size_t idio_string_handle_instance = 1;

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
    idio_free_string_handle,
    idio_readyp_string_handle,
    idio_getb_string_handle,
    idio_eofp_string_handle,
    idio_close_string_handle,
    idio_putb_string_handle,
    idio_putc_string_handle,
    idio_puts_string_handle,
    idio_flush_string_handle,
    idio_seek_string_handle,
    idio_print_string_handle
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

    char name[BUFSIZ];
    name[0] = '\0';
    if (sflags & IDIO_HANDLE_FLAG_READ) {
	strcat (name, "input");
	if (sflags & IDIO_HANDLE_FLAG_WRITE) {
	    strcat (name, "/");
	} else {
	    strcat (name, " ");
	}
    }
    if (sflags & IDIO_HANDLE_FLAG_WRITE) {
	strcat (name, "output ");
    }
    strcat (name, "string-handle #");
    char inst[BUFSIZ];
    sprintf (inst, "%zu", idio_string_handle_instance++);
    strcat (name, inst);

    IDIO_HANDLE_FILENAME (sh) = idio_string_C (name);
    IDIO_HANDLE_PATHNAME (sh) = IDIO_HANDLE_FILENAME (sh);
    IDIO_HANDLE_STREAM (sh) = shsp;
    IDIO_HANDLE_METHODS (sh) = &idio_string_handle_methods;

    return sh;
}

IDIO idio_open_input_string_handle_C (char *str)
{
    IDIO_C_ASSERT (str);

    size_t blen = strlen (str);
    char *str_copy = idio_alloc (blen + 1);
    /*
     * gcc warns "output truncated before terminating nul copying as
     * many bytes from a string as its length [-Wstringop-truncation]"
     * for just strncpy(..., blen)
     */
    strncpy (str_copy, str, blen + 1);
    str_copy[blen] = '\0';

    /*
     * str_copy will be freed when the handle is freed
     */

    return idio_open_string_handle (str_copy, blen, IDIO_HANDLE_FLAG_READ);
}

IDIO idio_open_output_string_handle_C ()
{
    char *str_C = idio_alloc (IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE);

    /*
     * str_C will be freed when the handle is freed
     */

    return idio_open_string_handle (str_C, IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE, IDIO_HANDLE_FLAG_WRITE);
}

/*
 * Dirty hack for bitsets.  We open an input string for each offset
 * and two for each range description (to be able to call read-number
 * with a handle) which, for the Unicode set adds up to 13,000 input
 * string handles each of which has attendent strings (for their
 * names)!
 *
 * Given that those do not recurse we could save a lot of allocations
 * by re-using an existing input string handle...
 */
IDIO idio_reopen_input_string_handle_C (IDIO sh, char *str)
{
    IDIO_ASSERT (sh);
    IDIO_C_ASSERT (str);

    IDIO_TYPE_ASSERT (string_handle, sh);

    /*
     * Out with the old
     */
    free (IDIO_STRING_HANDLE_BUF (sh));

    size_t blen = strlen (str);
    char *str_copy = idio_alloc (blen + 1);
    /*
     * gcc warns "output truncated before terminating nul copying as
     * many bytes from a string as its length [-Wstringop-truncation]"
     * for just strncpy(..., blen)
     */
    strncpy (str_copy, str, blen + 1);
    str_copy[blen] = '\0';

    /*
     * We could open an input-string-handle from "" so blen can be 0
     */

    idio_string_handle_stream_t *shsp = IDIO_HANDLE_STREAM (sh);
    IDIO_STRING_HANDLE_STREAM_PTR (shsp) = IDIO_STRING_HANDLE_STREAM_BUF (shsp);
    IDIO_STRING_HANDLE_STREAM_EOF (shsp) = 0;

    IDIO_STRING_HANDLE_STREAM_BUF (shsp) = str_copy;
    IDIO_STRING_HANDLE_STREAM_BLEN (shsp) = blen;
    IDIO_STRING_HANDLE_STREAM_PTR (shsp) = IDIO_STRING_HANDLE_STREAM_BUF (shsp);

    int sflags = IDIO_HANDLE_FLAGS (sh);
    if (sflags & IDIO_HANDLE_FLAG_WRITE) {
	IDIO_STRING_HANDLE_STREAM_END (shsp) = str_copy;
    } else {
	IDIO_STRING_HANDLE_STREAM_END (shsp) = str_copy + blen;
    }

    IDIO_STRING_HANDLE_STREAM_EOF (shsp) = 0;

    /*
     * Don't forget the parent handle object
     */
    IDIO_HANDLE_LC (sh) = EOF;
    IDIO_HANDLE_LINE (sh) = 1;
    IDIO_HANDLE_POS (sh) = 0;

    return sh;
}

IDIO_DEFINE_PRIMITIVE1 ("open-input-string", open_input_string_handle, (IDIO str))
{
    IDIO_ASSERT (str);

    size_t size = 0;
    char *str_C = NULL;

    switch (idio_type (str)) {
    case IDIO_TYPE_STRING:
    case IDIO_TYPE_SUBSTRING:
	str_C = idio_string_as_C (str, &size);
	break;
    default:
	idio_error_param_type ("string", str, IDIO_C_FUNC_LOCATION ());
	break;
    }

    IDIO r = idio_open_string_handle (str_C, size, IDIO_HANDLE_FLAG_READ);

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
	    IDIO_INPUTP_HANDLE (o));
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
	    IDIO_OUTPUTP_HANDLE (o));
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

void idio_free_string_handle (IDIO sh)
{
    IDIO_ASSERT (sh);

    free (IDIO_STRING_HANDLE_BUF (sh));
    free (IDIO_HANDLE_STREAM (sh));
}

int idio_readyp_string_handle (IDIO sh)
{
    IDIO_ASSERT (sh);

    return (idio_eofp_string_handle (sh) == 0);
}

int idio_getb_string_handle (IDIO sh)
{
    IDIO_ASSERT (sh);

    if (! idio_input_string_handlep (sh)) {
	idio_handle_error_read (sh, IDIO_C_FUNC_LOCATION ());
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

int idio_eofp_string_handle (IDIO sh)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    return IDIO_STRING_HANDLE_EOF (sh);
}

int idio_close_string_handle (IDIO sh)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    IDIO_HANDLE_FLAGS (sh) |= IDIO_HANDLE_FLAG_CLOSED;

    return 0;
}

int idio_putb_string_handle (IDIO sh, uint8_t c)
{
    IDIO_ASSERT (sh);

    if (! idio_output_string_handlep (sh)) {
	idio_handle_error_write (sh, IDIO_C_FUNC_LOCATION ());
    }

    if (IDIO_STRING_HANDLE_PTR (sh) >= IDIO_STRING_HANDLE_END (sh)) {
	if (IDIO_STRING_HANDLE_END (sh) == (IDIO_STRING_HANDLE_BUF (sh) + IDIO_STRING_HANDLE_BLEN (sh))) {
	    size_t blen = IDIO_STRING_HANDLE_BLEN (sh);
	    char *buf = IDIO_STRING_HANDLE_BUF (sh);
	    blen += blen / 2;	/* 50% more */
	    buf = idio_realloc (buf, blen);

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

    return 1;
}

int idio_putc_string_handle (IDIO sh, idio_unicode_t c)
{
    IDIO_ASSERT (sh);

    if (! idio_output_string_handlep (sh)) {
	idio_handle_error_write (sh, IDIO_C_FUNC_LOCATION ());
    }

    char buf[4];
    int size;
    idio_utf8_code_point (c, buf, &size);

    for (int n = 0;n < size;n++) {
	if (IDIO_STRING_HANDLE_PTR (sh) >= IDIO_STRING_HANDLE_END (sh)) {
	    if (IDIO_STRING_HANDLE_END (sh) == (IDIO_STRING_HANDLE_BUF (sh) + IDIO_STRING_HANDLE_BLEN (sh))) {
		size_t blen = IDIO_STRING_HANDLE_BLEN (sh);
		char *buf = IDIO_STRING_HANDLE_BUF (sh);
		blen += blen / 2;	/* 50% more */
		buf = idio_realloc (buf, blen);

		/*
		 * realloc can relocate data in memory!
		 */
		IDIO_STRING_HANDLE_BUF (sh) = buf;
		IDIO_STRING_HANDLE_PTR (sh) = buf + IDIO_STRING_HANDLE_BLEN (sh);
		IDIO_STRING_HANDLE_BLEN (sh) = blen;
	    }

	    IDIO_STRING_HANDLE_END (sh) = IDIO_STRING_HANDLE_PTR (sh) + 1;
	}

	*(IDIO_STRING_HANDLE_PTR (sh)) = buf[n];
	IDIO_STRING_HANDLE_PTR (sh) += 1;
    }

    return size;
}

ptrdiff_t idio_puts_string_handle (IDIO sh, char *s, size_t slen)
{
    IDIO_ASSERT (sh);

    if (! idio_output_string_handlep (sh)) {
	idio_handle_error_write (sh, IDIO_C_FUNC_LOCATION ());
    }

    if ((IDIO_STRING_HANDLE_PTR (sh) + slen) >= (IDIO_STRING_HANDLE_BUF (sh) + IDIO_STRING_HANDLE_BLEN (sh))) {
	size_t blen = IDIO_STRING_HANDLE_BLEN (sh);
	char *buf = IDIO_STRING_HANDLE_BUF (sh);
	size_t offset = IDIO_STRING_HANDLE_PTR (sh) - buf;

	blen = blen + slen + IDIO_STRING_HANDLE_DEFAULT_OUTPUT_SIZE;
	buf = idio_realloc (buf, blen);

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

int idio_flush_string_handle (IDIO sh)
{
    IDIO_ASSERT (sh);

    IDIO_TYPE_ASSERT (string_handle, sh);

    /*
     * There is nowhere for a string-handle to flush to -- it makes no
     * sense...
     */

    return 0;
}

off_t idio_seek_string_handle (IDIO sh, off_t offset, int whence)
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
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "idio_seek_string_handle: unexpected whence %d", whence);
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

void idio_print_string_handle (IDIO sh, IDIO o)
{
    IDIO_ASSERT (sh);

    if (! idio_output_string_handlep (sh)) {
	idio_handle_error_write (sh, IDIO_C_FUNC_LOCATION ());
    }

    size_t size = 0;
    char *os = idio_display_string (o, &size);
    IDIO_HANDLE_M_PUTS (sh) (sh, os, size);
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

