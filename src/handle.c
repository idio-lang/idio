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
 * handle.c
 * 
 */

#include "idio.h"

/*
 * lookahead char: getc(3) returns an "(int) unsigned char" or EOF
 * therefore a safe value for "there is no lookahead char available"
 * is EOF.  The lookahead char is thus conceptually different to the
 * result of getc(3) itself as EOF now means "actually call getc(3)".
 */

static IDIO idio_handles = idio_S_nil;

void idio_init_handle ()
{
    idio_collector_protect (idio_G_frame, idio_handles);

    
}

void idio_final_handle ()
{
    while (idio_S_nil != idio_handles) {
	IDIO h = IDIO_PAIR_H (idio_handles);
	idio_handles = IDIO_PAIR_T (idio_handles);

	IDIO_HANDLE_M_CLOSE (h) (idio_G_frame, h);
    }
}

IDIO idio_handle (IDIO f)
{
    IDIO_ASSERT (f);

    IDIO h = idio_get (f, IDIO_TYPE_HANDLE);

    IDIO_ALLOC (f, h->u.handle, sizeof (idio_handle_t));

    IDIO_HANDLE_FLAGS (h) = IDIO_HANDLE_FLAG_NONE;
    IDIO_HANDLE_LC (h) = EOF;
    IDIO_HANDLE_LINE (h) = 1;
    IDIO_HANDLE_POS (h) = 0;
    IDIO_HANDLE_NAME (h) = NULL;
    
    return h;
}

int idio_isa_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    return idio_isa (f, h, IDIO_TYPE_HANDLE);
}

void idio_free_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    IDIO_C_ASSERT (idio_isa_handle (f, h));

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->stats.nbytes -= sizeof (idio_handle_t);

    free (h->u.handle);
}

static void idio_handle_error (IDIO f, IDIO e, char *format, ...)
{
    va_list fmt_args;
    va_start (fmt_args, format);
    vfprintf (stderr, format, fmt_args);
    va_end (fmt_args);
    idio_raise_exception (f, idio_make_exception (f, e));
}

void idio_handle_lookahead_error (IDIO f, IDIO h, int c)
{
    idio_handle_error (f, idio_io_read_exception, "%s->unget => %#x (!= EOF)", IDIO_HANDLE_NAME (h), c);
}

void idio_handle_finalizer (IDIO handle)
{
    IDIO_ASSERT (handle);

    IDIO_C_ASSERT (idio_isa_opaque (idio_G_frame, handle));
}

void idio_register_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);

    idio_handles = idio_pair (f, h, idio_handles);
}

void idio_deregister_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);

    idio_handles = idio_pair (f, h, idio_handles);
}

void idio_error_bad_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

}

/*
 * Basic IO on handles
 */

int idio_handle_readyp (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    if (EOF != IDIO_HANDLE_LC (h)) {
	return 1;
    }
    
    return IDIO_HANDLE_M_READYP (h) (f, h);
}

int idio_handle_getc (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    int r = IDIO_HANDLE_LC (h);
    
    if (EOF != r) {
	/* there was a lookahead char so reset the flag */
	IDIO_HANDLE_LC (h) = EOF;
    } else {
	r = IDIO_HANDLE_M_GETC (h) (f, h);
    }

    if ('\n' == r) {
	IDIO_HANDLE_LINE (h) += 1;
    }

    IDIO_HANDLE_POS (h) += 1;
    
    return r;
}

int idio_handle_ungetc (IDIO f, IDIO h, int c)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    int r = IDIO_HANDLE_LC (h);
    
    if (EOF != r) {
	/* there already was a lookahead char */
	idio_handle_lookahead_error (f, h, r);
    }

    IDIO_HANDLE_LC (h) = c;
    
    if ('\n' == r) {
	IDIO_HANDLE_LINE (h) -= 1;
    }

    IDIO_HANDLE_POS (h) -= 1;

    return c;
}

int idio_handle_eofp (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    return IDIO_HANDLE_M_EOFP (h) (f, h);
}

int idio_handle_close (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    IDIO_HANDLE_FLAGS (h) |= IDIO_HANDLE_FLAG_CLOSED;
    
    return IDIO_HANDLE_M_CLOSE (h) (f, h);
}

int idio_handle_putc (IDIO f, IDIO h, int c)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    int n = IDIO_HANDLE_M_PUTC (h) (f, h, c);

    if (EOF != n) {
	IDIO_HANDLE_POS (h) += n;
    }

    return n;
}

int idio_handle_puts (IDIO f, IDIO h, char *s, size_t l)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (s);
    
    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    int n = IDIO_HANDLE_M_PUTS (h) (f, h, s, l);

    if (EOF != n) {
	IDIO_HANDLE_POS (h) += 1;
    }

    return n;
}

int idio_handle_flush (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    return IDIO_HANDLE_M_FLUSH (h) (f, h);
}

off_t idio_handle_seek (IDIO f, IDIO h, off_t offset, int whence)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    /* line number is invalidated unless we go to pos 0 */
    if (0 == offset && SEEK_SET == whence) {
	IDIO_HANDLE_LINE (h) = 1;
    } else {
	IDIO_HANDLE_LINE (h) = -1;
    }

    if (SEEK_CUR == whence) {
	offset += IDIO_HANDLE_POS (h);
	whence = SEEK_SET;
    }

    IDIO_HANDLE_LC (h) = EOF;

    IDIO_HANDLE_POS (h) = IDIO_HANDLE_M_SEEK (h) (f, h, offset, whence);

    return IDIO_HANDLE_POS (h);
}

void idio_handle_rewind (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    idio_handle_seek (f, h, 0, SEEK_SET);
}

off_t idio_handle_tell (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    return IDIO_HANDLE_POS (h);
}

void idio_handle_print (IDIO f, IDIO h, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    IDIO_ASSERT (o);
    
    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    return IDIO_HANDLE_M_PRINT (h) (f, h, o);
}

int idio_handle_printf (IDIO f, IDIO h, char *format, ...)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (format);

    char *buf;
    
    va_list fmt_args;
    va_start (fmt_args, format);
    if (-1 == vasprintf (&buf, format, fmt_args)) {
	idio_error_alloc (f);
    }
    va_end (fmt_args);

    int n = idio_handle_puts (f, h, buf, strlen (buf));

    free (buf);

    return n;
}

/*
 * primitives for handles
 */

/* handle? */
IDIO idio_handlep (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    IDIO r = idio_S_false;
    
    if (idio_isa_handle (f, h)) {
	r = idio_S_true;
    }

    return r;
}

/* input-handle? */
IDIO idio_input_handlep (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    IDIO r = idio_S_false;

    if (idio_isa_handle (f, h) &&
	IDIO_HANDLE_INPUTP (h)) {
	r = idio_S_true;
    }

    return r;
}

/* output-handle? */
IDIO idio_output_handlep (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    IDIO r = idio_S_false;

    if (idio_isa_handle (f, h) &&
	IDIO_HANDLE_OUTPUTP (h)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_current_input_handle (IDIO f)
{
    IDIO_ASSERT (f);

    IDIO_C_ASSERT (0);
}

IDIO idio_current_output_handle (IDIO f)
{
    IDIO_ASSERT (f);

    IDIO_C_ASSERT (0);
}

IDIO idio_set_input_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    IDIO_C_ASSERT (0);
}

IDIO idio_set_output_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    IDIO_C_ASSERT (0);
}

IDIO idio_close_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    IDIO_HANDLE_M_CLOSE (h) (f, h);

    return idio_S_unspec;
}

IDIO idio_close_input_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (f, h) &&
	   IDIO_HANDLE_INPUTP (h))) {
	idio_error_bad_handle (f, h);
    }

    IDIO_HANDLE_M_CLOSE (h) (f, h);

    return idio_S_unspec;
}

IDIO idio_close_output_handle (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (f, h) &&
	   IDIO_HANDLE_OUTPUTP (h))) {
	idio_error_bad_handle (f, h);
    }

    IDIO_HANDLE_M_CLOSE (h) (f, h);

    return idio_S_unspec;
}

/* handle-closed? */
IDIO idio_handle_closedp (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (f, h)) {
	idio_error_bad_handle (f, h);
    }

    IDIO r = idio_S_false;

    if (IDIO_HANDLE_CLOSEDP (h)) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_handle_or_current (IDIO f, IDIO h, unsigned mode)
{
    switch (mode) {
    case IDIO_HANDLE_FLAG_READ:
	if (idio_S_nil == h) {
	    return idio_current_input_handle (f);
	} else {
	    if (! IDIO_HANDLE_INPUTP (h)) {
		idio_error_bad_handle (f, h);
	    } else {
		return h;
	    }
	}
	break;
    case IDIO_HANDLE_FLAG_WRITE:
	if (idio_S_nil == h) {
	    return idio_current_output_handle (f);
	} else {
	    if (! IDIO_HANDLE_OUTPUTP (h)) {
		idio_error_bad_handle (f, h);
	    } else {
		return h;
	    }
	}
	break;
    default:
	IDIO_C_ASSERT (0);
    }

    IDIO_C_ASSERT (0);
}

IDIO idio_primitive_C_read (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    h = idio_handle_or_current (f, h, IDIO_HANDLE_FLAG_READ);

    return idio_read (f, h);
}

IDIO idio_primitive_C_read_char (IDIO f, IDIO h)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (h);
    
    h = idio_handle_or_current (f, h, IDIO_HANDLE_FLAG_READ);

    return idio_read (f, h);
}

