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

IDIO idio_handle ()
{
    IDIO h = idio_gc_get (IDIO_TYPE_HANDLE);

    IDIO_GC_ALLOC (h->u.handle, sizeof (idio_handle_t));

    IDIO_HANDLE_FLAGS (h) = IDIO_HANDLE_FLAG_NONE;
    IDIO_HANDLE_LC (h) = EOF;
    IDIO_HANDLE_LINE (h) = 1;
    IDIO_HANDLE_POS (h) = 0;
    IDIO_HANDLE_NAME (h) = NULL;
    
    return h;
}

int idio_isa_handle (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_isa (h, IDIO_TYPE_HANDLE);
}

void idio_free_handle (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (handle, h);

    idio_gc_stats_free (sizeof (idio_handle_t));

    IDIO_HANDLE_M_FREE (h) (h);
    
    free (h->u.handle);
}

void idio_handle_lookahead_error (IDIO h, int c)
{
    idio_error_message ("handle lookahead: %s->unget => %#x (!= EOF)", IDIO_HANDLE_NAME (h), c);
}

void idio_handle_finalizer (IDIO handle)
{
    IDIO_ASSERT (handle);

    if (! idio_isa_handle (handle)) {
	idio_error_param_type ("handle", handle);
	IDIO_C_ASSERT (0);
    }
}

static void idio_error_bad_handle (IDIO h)
{
    IDIO_ASSERT (h);

    idio_error_param_type ("handle", h);
}

/*
 * Basic IO on handles
 */

int idio_handle_readyp (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    if (EOF != IDIO_HANDLE_LC (h)) {
	return 1;
    }
    
    return IDIO_HANDLE_M_READYP (h) (h);
}

int idio_handle_getc (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    int r = IDIO_HANDLE_LC (h);
    
    if (EOF != r) {
	/* there was a lookahead char so reset the flag */
	IDIO_HANDLE_LC (h) = EOF;
    } else {
	r = IDIO_HANDLE_M_GETC (h) (h);
    }

    if ('\n' == r) {
	IDIO_HANDLE_LINE (h) += 1;
    }

    IDIO_HANDLE_POS (h) += 1;
    
    return r;
}

int idio_handle_ungetc (IDIO h, int c)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    int r = IDIO_HANDLE_LC (h);
    
    if (EOF != r) {
	/* there already was a lookahead char */
	idio_handle_lookahead_error (h, r);
    }

    IDIO_HANDLE_LC (h) = c;
    
    if ('\n' == c) {
	IDIO_HANDLE_LINE (h) -= 1;
    }

    IDIO_HANDLE_POS (h) -= 1;

    return c;
}

int idio_handle_peek (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    int c = idio_handle_getc (h);
    idio_handle_ungetc (h, c);

    return c;
}

int idio_handle_eofp (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    return IDIO_HANDLE_M_EOFP (h) (h);
}

int idio_handle_close (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    IDIO_HANDLE_FLAGS (h) |= IDIO_HANDLE_FLAG_CLOSED;
    
    return IDIO_HANDLE_M_CLOSE (h) (h);
}

int idio_handle_putc (IDIO h, int c)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    int n = IDIO_HANDLE_M_PUTC (h) (h, c);

    if (EOF != n) {
	IDIO_HANDLE_POS (h) += n;
    }

    return n;
}

int idio_handle_puts (IDIO h, char *s, size_t l)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (s);
    
    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    int n = IDIO_HANDLE_M_PUTS (h) (h, s, l);

    if (EOF != n) {
	IDIO_HANDLE_POS (h) += 1;
    }

    return n;
}

int idio_handle_flush (IDIO h)
{
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    return IDIO_HANDLE_M_FLUSH (h) (h);
}

off_t idio_handle_seek (IDIO h, off_t offset, int whence)
{
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
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

    IDIO_HANDLE_POS (h) = IDIO_HANDLE_M_SEEK (h) (h, offset, whence);

    return IDIO_HANDLE_POS (h);
}

void idio_handle_rewind (IDIO h)
{
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    idio_handle_seek (h, 0, SEEK_SET);
}

off_t idio_handle_tell (IDIO h)
{
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    return IDIO_HANDLE_POS (h);
}

void idio_handle_print (IDIO h, IDIO o)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (o);
    
    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    return IDIO_HANDLE_M_PRINT (h) (h, o);
}

int idio_handle_printf (IDIO h, char *format, ...)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (format);

    char *buf;
    
    va_list fmt_args;
    va_start (fmt_args, format);
    if (-1 == vasprintf (&buf, format, fmt_args)) {
	idio_error_alloc ();
    }
    va_end (fmt_args);

    int n = idio_handle_puts (h, buf, strlen (buf));

    free (buf);

    return n;
}

/*
 * primitives for handles
 */

IDIO_DEFINE_PRIMITIVE1 ("handle?", handlep, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO r = idio_S_false;
    
    if (idio_isa_handle (h)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("input-handle?", input_handlep, (IDIO h))
{
    IDIO_ASSERT (h);
    
    IDIO r = idio_S_false;

    if (idio_isa_handle (h) &&
	IDIO_HANDLE_INPUTP (h)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("output-handle?", output_handlep, (IDIO h))
{
    IDIO_ASSERT (h);
    
    IDIO r = idio_S_false;

    if (idio_isa_handle (h) &&
	IDIO_HANDLE_OUTPUTP (h)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE0 ("current-input-handle", current_input_handle, ())
{
    return idio_current_input_handle ();
}

IDIO_DEFINE_PRIMITIVE0 ("current-output-handle", current_output_handle, ())
{
    return idio_current_output_handle ();
}

IDIO_DEFINE_PRIMITIVE1 ("set-input-handle!", set_input_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO_VERIFY_PARAM_TYPE (handle, h);
    
    idio_set_current_input_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("set-output-handle!", set_output_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO_VERIFY_PARAM_TYPE (handle, h);
    
    idio_set_current_output_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("close-handle", close_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("close-input-handle", close_input_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (h) &&
	   IDIO_HANDLE_INPUTP (h))) {
	idio_error_bad_handle (h);
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("close-output-handle", close_output_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (h) &&
	   IDIO_HANDLE_OUTPUTP (h))) {
	idio_error_bad_handle (h);
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

/* handle-closed? */
IDIO_DEFINE_PRIMITIVE1 ("handle-closed?", handle_closedp, (IDIO h))
{
    IDIO_ASSERT (h);
    
    if (! idio_isa_handle (h)) {
	idio_error_bad_handle (h);
    }

    IDIO r = idio_S_false;

    if (IDIO_HANDLE_CLOSEDP (h)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("eof-object?", eof_objectp, (IDIO o))
{
    IDIO_ASSERT (o);
    
    IDIO r = idio_S_false;

    if (idio_S_eof == o) {
	r = idio_S_true;
    }

    return r;
}

IDIO idio_handle_or_current (IDIO h, unsigned mode)
{
    switch (mode) {
    case IDIO_HANDLE_FLAG_READ:
	if (idio_S_nil == h) {
	    return idio_current_input_handle ();
	} else {
	    if (! IDIO_HANDLE_INPUTP (h)) {
		idio_error_bad_handle (h);
	    } else {
		return h;
	    }
	}
	break;
    case IDIO_HANDLE_FLAG_WRITE:
	if (idio_S_nil == h) {
	    return idio_current_output_handle ();
	} else {
	    if (! IDIO_HANDLE_OUTPUTP (h)) {
		idio_error_bad_handle (h);
	    } else {
		return h;
	    }
	}
	break;
    default:
	IDIO_C_ASSERT (0);
	break;
    }

    IDIO_C_ASSERT (0);
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V ("read", read, (IDIO args))
{
    IDIO_ASSERT (args);
    
    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    return idio_scm_read (h);
}

IDIO_DEFINE_PRIMITIVE0V ("read-char", read_char, (IDIO args))
{
    IDIO_ASSERT (args);
    
    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    return idio_scm_read_char (h);
}

IDIO_DEFINE_PRIMITIVE0V ("peek-char", peek_char, (IDIO args))
{
    IDIO_ASSERT (args);
    
    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    int c = idio_handle_peek (h);

    return IDIO_CHARACTER (c);
}

IDIO_DEFINE_PRIMITIVE1V ("write", write, (IDIO o, IDIO args))
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (args);
    
    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    char *os = idio_as_string (o, 10);
    
    IDIO_HANDLE_M_PUTS (h) (h, os, strlen (os));

    free (os);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1V ("write-char", write_char, (IDIO c, IDIO args))
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (args);

    IDIO_VERIFY_PARAM_TYPE (character, c);
    
    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    IDIO_HANDLE_M_PUTC (h) (h, IDIO_CHARACTER_VAL (c));

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V ("newline", newline, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    IDIO_HANDLE_M_PUTC (h) (h, '\n');

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1V ("display", display, (IDIO o, IDIO args))
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (args);
    
    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    char *s = idio_display_string (o);
    
    IDIO_HANDLE_M_PUTS (h) (h, s, strlen (s));
    free (s);

    return idio_S_unspec;
}

void idio_init_handle ()
{
}

void idio_handle_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (handlep);
    IDIO_ADD_PRIMITIVE (input_handlep);
    IDIO_ADD_PRIMITIVE (output_handlep);
    IDIO_ADD_PRIMITIVE (current_input_handle);
    IDIO_ADD_PRIMITIVE (current_output_handle);
    IDIO_ADD_PRIMITIVE (set_input_handle);
    IDIO_ADD_PRIMITIVE (set_output_handle);
    IDIO_ADD_PRIMITIVE (close_handle);
    IDIO_ADD_PRIMITIVE (close_input_handle);
    IDIO_ADD_PRIMITIVE (close_output_handle);
    IDIO_ADD_PRIMITIVE (handle_closedp);
    IDIO_ADD_PRIMITIVE (eof_objectp);
    IDIO_ADD_PRIMITIVE (read);
    IDIO_ADD_PRIMITIVE (read_char);
    IDIO_ADD_PRIMITIVE (peek_char);
    IDIO_ADD_PRIMITIVE (write);
    IDIO_ADD_PRIMITIVE (write_char);
    IDIO_ADD_PRIMITIVE (newline);
    IDIO_ADD_PRIMITIVE (display);
}

void idio_final_handle ()
{
}

