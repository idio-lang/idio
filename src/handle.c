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
 * handle.c
 *
 */

#include "idio.h"

void idio_handle_error_read (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("handle name '", sh);
    idio_display (IDIO_HANDLE_PATHNAME (h), sh);
    idio_display_C ("' read error", sh);
    IDIO c = idio_struct_instance (idio_condition_io_read_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       c_location,
					       idio_S_nil,
					       IDIO_HANDLE_PATHNAME (h)));
    idio_raise_condition (idio_S_true, c);
}

void idio_handle_error_write (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("handle name '", sh);
    idio_display (IDIO_HANDLE_PATHNAME (h), sh);
    idio_display_C ("' write error", sh);
    IDIO c = idio_struct_instance (idio_condition_io_write_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       c_location,
					       idio_S_nil,
					       IDIO_HANDLE_PATHNAME (h)));
    idio_raise_condition (idio_S_true, c);
}

void idio_handle_error_closed (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO sh = idio_open_output_string_handle_C ();
    idio_display_C ("handle name '", sh);
    idio_display (IDIO_HANDLE_PATHNAME (h), sh);
    idio_display_C ("' already closed", sh);
    IDIO c = idio_struct_instance (idio_condition_io_closed_error_type,
				   IDIO_LIST4 (idio_get_output_string (sh),
					       c_location,
					       idio_S_nil,
					       IDIO_HANDLE_PATHNAME (h)));
    idio_raise_condition (idio_S_true, c);
}

static void idio_handle_error_bad (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_param_type ("handle", h, c_location);
}

static void idio_handle_error_bad_input (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_param_type ("input handle", h, c_location);
}

static void idio_handle_error_bad_output (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_param_type ("output handle", h, c_location);
}

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
    IDIO_HANDLE_FILENAME (h) = idio_S_nil;
    IDIO_HANDLE_PATHNAME (h) = idio_S_nil;

    return h;
}

int idio_isa_handle (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_isa (h, IDIO_TYPE_HANDLE);
}

char *idio_handle_name_as_C (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    IDIO hname = IDIO_HANDLE_FILENAME (h);
    if (idio_S_nil == hname) {
	hname = IDIO_HANDLE_PATHNAME (h);
    }
    char *name = "n/a";
    if (hname->type) {
	if (idio_isa_string (hname)) {
	    name = idio_string_as_C (hname);
	} else if (idio_isa_symbol (hname)) {
	    name = IDIO_SYMBOL_S (hname);
	}
    } else {
	/*
	 * I need to stop myself printing things out
	 * during shutdown...
	 */
	name = "n/r";
    }

    return name;
}

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
	IDIO_INPUTP_HANDLE (h)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("output-handle?", output_handlep, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO r = idio_S_false;

    if (idio_isa_handle (h) &&
	IDIO_OUTPUTP_HANDLE (h)) {
	r = idio_S_true;
    }

    return r;
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
    idio_error_printf (IDIO_C_FUNC_LOCATION (), "%s->unget => %#x (!= EOF)", idio_handle_name_as_C (h), c);
}

void idio_handle_finalizer (IDIO handle)
{
    IDIO_ASSERT (handle);

    if (! idio_isa_handle (handle)) {
	idio_error_param_type ("handle", handle, IDIO_C_FUNC_LOCATION ());
    }
}

/*
 * Basic IO on handles
 */

int idio_readyp_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    if (EOF != IDIO_HANDLE_LC (h)) {
	return 1;
    }

    return IDIO_HANDLE_M_READYP (h) (h);
}

IDIO_DEFINE_PRIMITIVE0V ("ready?", readyp, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO r = idio_S_false;

    if (idio_readyp_handle (h)) {
	r = idio_S_true;
    }

    return r;
}

int idio_getc_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int r = IDIO_HANDLE_LC (h);

    if (EOF != r) {
	/* there was a lookahead char so reset the flag */
	IDIO_HANDLE_LC (h) = EOF;
    } else {
	r = IDIO_HANDLE_M_GETC (h) (h);
    }

    /*
     * Only increment the line number if we have a valid line number
     * to start with
     */
    if ('\n' == r &&
	IDIO_HANDLE_LINE (h)) {
	IDIO_HANDLE_LINE (h) += 1;
    }

    IDIO_HANDLE_POS (h) += 1;

    return r;
}

int idio_ungetc_handle (IDIO h, int c)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int r = IDIO_HANDLE_LC (h);

    if (EOF != r) {
	/* there already was a lookahead char */
	idio_handle_lookahead_error (h, r);
    }

    IDIO_HANDLE_LC (h) = c;

    /*
     * Only decrement the line number if we have a valid line number
     * to start with
     */
    if ('\n' == c &&
	IDIO_HANDLE_LINE (h)) {
	IDIO_HANDLE_LINE (h) -= 1;
    }

    if (IDIO_HANDLE_POS (h)) {
	IDIO_HANDLE_POS (h) -= 1;
    }

    return c;
}

int idio_peek_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int c = idio_getc_handle (h);
    idio_ungetc_handle (h, c);

    return c;
}

IDIO_DEFINE_PRIMITIVE0V ("peek-char", peek_char, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    int c = idio_peek_handle (h);

    return IDIO_UNICODE (c);
}

int idio_eofp_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    return IDIO_HANDLE_M_EOFP (h) (h);
}

IDIO_DEFINE_PRIMITIVE0V ("eof?", eofp, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO r = idio_S_false;

    if (idio_eofp_handle (h)) {
	r = idio_S_true;
    }

    return r;
}

int idio_close_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    int r = IDIO_HANDLE_M_CLOSE (h) (h);
    IDIO_HANDLE_FLAGS (h) |= IDIO_HANDLE_FLAG_CLOSED;

    return r;
}

int idio_putc_handle (IDIO h, int c)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int n = IDIO_HANDLE_M_PUTC (h) (h, c);

    if (EOF != n) {
	IDIO_HANDLE_POS (h) += n;
	if ('\n' == c &&
	    IDIO_HANDLE_LINE (h)) {
	    IDIO_HANDLE_LINE (h) += 1;

	    /*
	     * Hmm.  Idio has a per-handle buffer and if the handle is
	     * a FILE* then it too has a buffer (probably).  There's
	     * the odd edge-case where trailing newlines don't get
	     * flushed.
	     *
	     * A handle-flush here fixes that but it feels wrong.
	     *
	     * This was picked up by:
	     *
	     * display* "first" | auto-exit -r 1
	     *
	     * which, it transpires, has Bash's {read} fail if the
	     * output is not terminated by a newline (who knew?).
	     * Indeed, the pipeline was only seeing "first" and not
	     * the trailing newline that display* should have been
	     * emitting.
	     */
	    IDIO_HANDLE_M_FLUSH (h) (h);
	}
    }

    return n;
}

size_t idio_puts_handle (IDIO h, char *s, size_t slen)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (s);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    size_t n = IDIO_HANDLE_M_PUTS (h) (h, s, slen);

    if (EOF != n) {
	IDIO_HANDLE_POS (h) += n;
    }

    return n;
}

int idio_flush_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    return IDIO_HANDLE_M_FLUSH (h) (h);
}

IDIO_DEFINE_PRIMITIVE1 ("flush-handle", flush_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO_VERIFY_PARAM_TYPE (handle, h);

    idio_flush_handle (h);

    return idio_S_unspec;
}

off_t idio_seek_handle (IDIO h, off_t offset, int whence)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    /* line number is invalidated unless we go to pos 0 */
    if (0 == offset && SEEK_SET == whence) {
	IDIO_HANDLE_LINE (h) = 1;
    } else {
	IDIO_HANDLE_LINE (h) = 0;
    }

    if (SEEK_CUR == whence) {
	offset += IDIO_HANDLE_POS (h);
	whence = SEEK_SET;
    }

    IDIO_HANDLE_LC (h) = EOF;

    IDIO_HANDLE_POS (h) = IDIO_HANDLE_M_SEEK (h) (h, offset, whence);

    return IDIO_HANDLE_POS (h);
}

IDIO_DEFINE_PRIMITIVE2V_DS ("seek-handle", seek_handle, (IDIO h, IDIO pos, IDIO args), "handle pos [whence]", "\
seek to the given ``pos`` in ``handle``			\n\
							\n\
if one of the optional 'set, 'end or 'cur symbols is	\n\
supplied for ``whence`` use the appropriate *whence* flag	\n\
							\n\
:param handle: handle to seek in (file or string)	\n\
:type handle: handle					\n\
:param pos: position to seek to				\n\
:type pos: integer					\n\
:param whence: whence flag, defaults to 'set		\n\
:type whence: symbol					\n\
:return: position actually sought to			\n\
:rtype: integer						\n\
							\n\
A successful seek will clear the end-of-file status	\n\
of the handle.						\n\
							\n\
The handle's concept of a line number is invalidated	\n\
unless ``whence`` is 'set and position is 0 (zero)	\n\
")
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (pos);
    IDIO_ASSERT (args);

    IDIO_VERIFY_PARAM_TYPE (handle, h);
    IDIO_VERIFY_PARAM_TYPE (integer, pos);

    int whence = -1;

    if (idio_S_nil != args) {
	IDIO w = idio_list_head (args);
	IDIO_VERIFY_PARAM_TYPE (symbol, w);

	if (IDIO_STREQP (IDIO_SYMBOL_S (w), "set")) {
	    whence = SEEK_SET;
	} else if (IDIO_STREQP (IDIO_SYMBOL_S (w), "end")) {
	    whence = SEEK_END;
	} else if (IDIO_STREQP (IDIO_SYMBOL_S (w), "cur")) {
	    whence = SEEK_CUR;
	} else {
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "bad seek request: %s", IDIO_SYMBOL_S (w));

	    return idio_S_notreached;
	}
    } else {
	whence = SEEK_SET;
    }

    idio_flush_handle (h);

    off_t offset;
    if (idio_isa_fixnum (pos)) {
	offset = IDIO_FIXNUM_VAL (pos);
    } else {
	/*
	 * XXX intmax_t should be at least as large as off_t but we
	 * might still get into trouble.
	 *
	 * intmax_t is in the CXX (C99?) standards meaning the largest
	 * integral type whereas off_t is POSIX and relates to file
	 * offsets.  But whether off_t == off64_t may depend on
	 * whether you've compiled with _FILE_OFFSET_BITS=64
	 *
	 * That means there's a risk our intmax_t could overflow off_t
	 * with "hilarious" results.
	 */
	offset = idio_bignum_intmax_value (pos);
    }
    off_t n = idio_seek_handle (h, offset, whence);

    if (n < 0) {
	idio_debug ("seek-handle %s", h);
	idio_debug (" %s", pos);
	if (idio_S_nil != args) {
	    idio_debug (" %s", IDIO_PAIR_H (args));
	}
	fprintf (stderr, "\n");

	char *ws;
	switch (whence) {
	case SEEK_SET: ws = "SEEK_SET"; break;
	case SEEK_END: ws = "SEEK_END"; break;
	case SEEK_CUR: ws = "SEEK_CUR"; break;
	default: ws = "u/k"; break;
	}
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "cannot seek (%" PRId64 ", %s)", offset, ws);

	return idio_S_notreached;
    }

    IDIO_HANDLE_POS (h) = n;

    IDIO r = idio_integer (n);

    return r;
}

void idio_handle_rewind (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_seek_handle (h, 0, SEEK_SET);
}

IDIO_DEFINE_PRIMITIVE1 ("handle-rewind", handle_rewind, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO_VERIFY_PARAM_TYPE (handle, h);

    idio_handle_rewind (h);

    return idio_S_unspec;
}

off_t idio_handle_tell (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    return IDIO_HANDLE_POS (h);
}

void idio_print_handle (IDIO h, IDIO o)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (o);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    return IDIO_HANDLE_M_PRINT (h) (h, o);
}

int idio_print_handlef (IDIO h, char *format, ...)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (format);

    char *buf;

    va_list fmt_args;
    va_start (fmt_args, format);
    if (-1 == vasprintf (&buf, format, fmt_args)) {
	idio_error_alloc ("vasprintf");

	/* notreached */
	return 0;
    }
    va_end (fmt_args);

    int n = idio_puts_handle (h, buf, strlen (buf));

    free (buf);

    return n;
}

/*
 * primitives for handles
 */

IDIO_DEFINE_PRIMITIVE0 ("current-input-handle", current_input_handle, ())
{
    return idio_thread_current_input_handle ();
}

IDIO_DEFINE_PRIMITIVE0 ("current-output-handle", current_output_handle, ())
{
    return idio_thread_current_output_handle ();
}

IDIO_DEFINE_PRIMITIVE0 ("current-error-handle", current_error_handle, ())
{
    return idio_thread_current_error_handle ();
}

IDIO_DEFINE_PRIMITIVE1 ("set-input-handle!", set_input_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO_VERIFY_PARAM_TYPE (handle, h);

    idio_thread_set_current_input_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("set-output-handle!", set_output_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO_VERIFY_PARAM_TYPE (handle, h);

    idio_thread_set_current_output_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("set-error-handle!", set_error_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    IDIO_VERIFY_PARAM_TYPE (handle, h);

    idio_thread_set_current_error_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("close-handle", close_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("close-input-handle", close_input_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (h) &&
	   IDIO_INPUTP_HANDLE (h))) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("close-output-handle", close_output_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (h) &&
	   IDIO_OUTPUTP_HANDLE (h))) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("closed-handle?", closedp_handle, (IDIO h))
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	idio_handle_error_bad (h, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (IDIO_CLOSEDP_HANDLE (h)) {
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
	    return idio_thread_current_input_handle ();
	} else {
	    if (! IDIO_INPUTP_HANDLE (h)) {
		idio_handle_error_read (h, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		return h;
	    }
	}
	break;
    case IDIO_HANDLE_FLAG_WRITE:
	if (idio_S_nil == h) {
	    return idio_thread_current_output_handle ();
	} else {
	    if (! IDIO_OUTPUTP_HANDLE (h)) {
		idio_handle_error_write (h, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		return h;
	    }
	}
	break;
    default:
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected mode %d", mode);

	return idio_S_notreached;
	break;
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("read", read, (IDIO args), "[handle]", "\
read an Idio expression from ``handle`` or the current input handle	\n\
							\n\
:param handle: handle to read from			\n\
:type handle: handle					\n\
:return: object						\n\
")
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO expr = idio_read (h);

    return expr;
}

IDIO_DEFINE_PRIMITIVE0V ("read-expr", read_expr, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    return idio_read_expr (h);
}

IDIO idio_read_line (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    IDIO osh = idio_open_output_string_handle_C ();

    for (;;) {
	int c = idio_getc_handle (h);
	switch (c) {
	case EOF:
	case '\n':
	    return idio_get_output_string (osh);
	default:
	    idio_putc_string_handle (osh, c);
	    break;
	}
    }

}

IDIO_DEFINE_PRIMITIVE0V_DS ("read-line", read_line, (IDIO args), "[handle]", "\
read a string from ``handle`` or the current intput handle	\n\
up to a #\{newline} character					\n\
								\n\
:param handle: handle to read from				\n\
:type handle: handle						\n\
:return: object							\n\
")
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    return idio_read_line (h);
}

IDIO idio_read_lines (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    IDIO osh = idio_open_output_string_handle_C ();

    for (;;) {
	int c = idio_getc_handle (h);
	switch (c) {
	case EOF:
	    return idio_get_output_string (osh);
	default:
	    idio_putc_string_handle (osh, c);
	    break;
	}
    }

}

IDIO_DEFINE_PRIMITIVE0V_DS ("read-lines", read_lines, (IDIO args), "[handle]", "\
read from ``handle`` or the current intput handle	\n\
up to the end of file					\n\
							\n\
:param handle: handle to read from			\n\
:type handle: handle					\n\
:return: object						\n\
")
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    return idio_read_lines (h);
}

/* IDIO_DEFINE_PRIMITIVE0V ("scm-read", scm_read, (IDIO args)) */
/* { */
/*     IDIO_ASSERT (args); */

/*     IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ); */

/*     return idio_scm_read (h); */
/* } */

IDIO_DEFINE_PRIMITIVE0V ("read-char", read_char, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    return idio_read_character (h, idio_S_nil, IDIO_READ_CHARACTER_SIMPLE);
}

IDIO idio_write (IDIO o, IDIO h)
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    char *os = idio_as_string (o, 10);

    idio_puts_handle (h, os, strlen (os));

    free (os);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("write", write, (IDIO o, IDIO args), "o [handle]", "\
write ``o`` to ``handle`` or the current output handle	\n\
							\n\
:param o: object					\n\
:param handle: handle to write to			\n\
:type handle: handle					\n\
:return: <unspec>					\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    return idio_write (o, h);
}

IDIO idio_write_char (IDIO c, IDIO h)
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    IDIO_TYPE_ASSERT (unicode, c);

    idio_putc_handle (h, IDIO_UNICODE_VAL (c));

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1V ("write-char", write_char, (IDIO c, IDIO args))
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (args);

    IDIO_VERIFY_PARAM_TYPE (unicode, c);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    return idio_write_char (c, h);
}

IDIO_DEFINE_PRIMITIVE0V ("newline", newline, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    idio_putc_handle (h, '\n');

    return idio_S_unspec;
}

IDIO idio_display (IDIO o, IDIO h)
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    char *s = idio_display_string (o);

    idio_puts_handle (h, s, strlen (s));
    free (s);

    return idio_S_unspec;
}

IDIO idio_display_C_len (char *s, size_t blen, IDIO h)
{
    IDIO_C_ASSERT (s);
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    idio_puts_handle (h, s, blen);

    return idio_S_unspec;
}

IDIO idio_display_C (char *s, IDIO h)
{
    IDIO_C_ASSERT (s);
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    return idio_display_C_len (s, strlen (s), h);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("display", display, (IDIO o, IDIO args), "o [handle]", "\
display ``o`` to ``handle`` or the current output handle	\n\
							\n\
:param o: object					\n\
:param handle: handle to write to			\n\
:type handle: handle					\n\
:return: <unspec>					\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    return idio_display (o, h);
}

IDIO_DEFINE_PRIMITIVE2V_DS ("%printf", printf, (IDIO h, IDIO fmt, IDIO args), "handle fmt [args]", "\
printf ``fmt`` to ``handle`` using ``args`` as required	\n\
							\n\
All Idio objects will become strings so the only useful	\n\
printf conversion is %s					\n\
							\n\
There is rudimentary support for the following flags:	\n\
%[flags][width][.precision]s:				\n\
							\n\
[flags]							\n\
-	left-align the output				\n\
[width]							\n\
num	specifies a minimum number of characters to output\n\
[precision]						\n\
num	specifies a maximum limit on the output		\n\
							\n\
%% is also functional					\n\
							\n\
:param handle: handle to write to			\n\
:type handle: handle					\n\
:param fmt: format string				\n\
:type fmt: string					\n\
:param args: optional arguments to printf		\n\
:return: <unspec>					\n\
")
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (fmt);
    IDIO_ASSERT (args);

    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (string, fmt);

    if (! (idio_S_nil == args ||
	   idio_isa_pair (args))) {
	idio_error_param_type ("list", args, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    char *fmt_C = idio_string_as_C (fmt);
    size_t blen = strlen (fmt_C);

    char *s = fmt_C;
    size_t i = 0;
    while (i < blen &&
	   *s) {
	switch (*s) {
	case '%':
	    {
		char *ss = s;
		size_t si = i;

		/*
		 * flags
		 *
		 * only '-' is meaningful
		 */
		if ((si + 1) < blen) {
		    char *c = ss + 1;
		    switch (*c) {
		    case '-':
			ss++;
			si++;
			break;
		    }
		}
		/*
		 * width
		 */
		while ((si + 1) < blen) {
		    char *c = ss + 1;
		    if (isdigit (*c)) {
			ss++;
			si++;
		    } else {
			break;
		    }
		}
		/*
		 * precision
		 *
		 * preceded by a dot
		 */
		if ((si + 1) < blen) {
		    char *c = ss + 1;
		    switch (*c) {
		    case '.':
			ss++;
			si++;
			while ((si + 1) < blen) {
			    char *c = ss + 1;
			    if (isdigit (*c)) {
				ss++;
				si++;
			    } else {
				break;
			    }
			}
			break;
		    }
		}
		if ((si + 1) < blen) {
		    ss += 1;
		    si += 1;
		    char *c = ss;
		    switch (*c) {
		    case 'd':
		    case 'x':
		    case 'X':
			{
			    char fmt[BUFSIZ];
			    strncpy (fmt, s, ss - s + 1);
			    fmt[ss - s + 1] = '\0';
			    if (idio_S_nil != args) {
				IDIO arg = IDIO_PAIR_H (args);
				args = IDIO_PAIR_T (args);
				if (idio_isa_fixnum (arg)) {
				    intmax_t n = IDIO_FIXNUM_VAL (arg);
				    char str[BUFSIZ];
				    sprintf (str, fmt, n);
				    idio_puts_handle (h, str, strlen (str));
				} else if (idio_isa_bignum (arg)) {
				    s = idio_bignum_as_string (arg);
				    idio_puts_handle (h, s, strlen (s));
				    free (s);
				} else {
				    /* ?? */
				    idio_puts_handle (h, c, strlen (c));
				}
			    } else {
				c = "<no-arg>";
				idio_puts_handle (h, c, strlen (c));
			    }
			    s = ss;
			    i = si;
			}
			break;
		    case 's':
			{
			    char fmt[BUFSIZ];
			    strncpy (fmt, s, ss - s + 1);
			    fmt[ss - s + 1] = '\0';
			    if (idio_S_nil != args) {
				IDIO arg = IDIO_PAIR_H (args);
				args = IDIO_PAIR_T (args);
				c = idio_display_string (arg);
				if (strlen (c) > BUFSIZ) {
				    /* hmm, let's pretend there was no
				     * length, precision etc. */
				    idio_puts_handle (h, c, strlen (c));
				} else {
				    char str[BUFSIZ];
				    sprintf (str, fmt, c);
				    idio_puts_handle (h, str, strlen (str));
				}
				free (c);
			    } else {
				c = "<no-arg>";
				idio_puts_handle (h, c, strlen (c));
			    }
			    s = ss;
			    i = si;
			}
			break;
		    case '%':
			s = ss;
			i = si;
			idio_putc_handle (h, *ss);
			break;
		    }
		}
	    }
	    break;
	default:
	    idio_putc_handle (h, *s);
	    break;
	}
	s++;
	i++;
    }

    free (fmt_C);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE0V ("handle-line", handle_line, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO r;
    off_t line = IDIO_HANDLE_LINE (h);
    r = idio_integer (line);

    return r;
}

IDIO_DEFINE_PRIMITIVE0V ("handle-pos", handle_pos, (IDIO args))
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO r;
    off_t pos = IDIO_HANDLE_POS (h);
    r = idio_integer (pos);

    return r;
}

IDIO idio_handle_location (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    char buf[BUFSIZ];
    char *sname = idio_handle_name_as_C (h);
    sprintf (buf, "%s:line %jd", sname, (intmax_t) IDIO_HANDLE_LINE (h));
    free (sname);

    return idio_string_C (buf);
}

IDIO_DEFINE_PRIMITIVE1 ("handle-location", handle_location, (IDIO h))
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    return idio_handle_location (h);
}

IDIO idio_load_handle_ebe (IDIO h, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (reader);
    IDIO_C_ASSERT (evaluator);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (array, cs);

    if (IDIO_FILE_HANDLE_FLAGS (h) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
	return idio_load_handle_interactive (h, reader, evaluator, cs);
    }

    int timing = 0;

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t ss0 = idio_array_size (IDIO_THREAD_STACK (thr));

    /*
     * When we call idio_vm_run() we are at risk of the garbage
     * collector being called so we need to save the current file
     * handle and any lists we're walking over
     */
    idio_remember_file_handle (h);

    IDIO e = idio_S_nil;
    IDIO r;

    for (;;) {
	e = (*reader) (h);

	if (idio_S_eof == e) {
	    break;
	} else {
	    IDIO m = (*evaluator) (e, cs);

	    idio_ai_t lh_pc = -1;
	    idio_codegen (thr, m, cs);
	    if (-1 == lh_pc) {
		lh_pc = IDIO_THREAD_PC (thr);
	    }

	    IDIO_THREAD_PC (thr) = lh_pc;
	    r = idio_vm_run (thr);

	    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

	    if (ss != ss0) {
		char *sname = idio_handle_name_as_C (h);
		fprintf (stderr, "load-handle-ebe: %s: SS %td != %td\n", sname, ss, ss0);
		free (sname);
		idio_debug ("THR %s\n", thr);
		idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
	    }
	}
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    idio_forget_file_handle (h);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("load-handle-ebe", load_handle_ebe, (IDIO h), "handle", "\
load expressions from ``handle`` expression by expression	\n\
								\n\
:param handle: the handle to load from				\n\
:type handle: handle						\n\
								\n\
This is the ``load-handle-ebe`` primitive.			\n\
")
{
    IDIO_ASSERT (h);
    IDIO_VERIFY_PARAM_TYPE (handle, h);

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t pc0 = IDIO_THREAD_PC (thr);

    IDIO r = idio_load_handle_ebe (h, idio_read, idio_evaluate, idio_vm_constants);

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }

    return r;
}

IDIO idio_load_handle_aio (IDIO h, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (reader);
    IDIO_C_ASSERT (evaluator);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (array, cs);

    if (IDIO_FILE_HANDLE_FLAGS (h) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
	return idio_load_handle_interactive (h, reader, evaluator, cs);
    }

    int timing = 0;

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t ss0 = idio_array_size (IDIO_THREAD_STACK (thr));

    time_t s;
    suseconds_t us;
    struct timeval t0;
    gettimeofday (&t0, NULL);

    IDIO es = idio_S_nil;

    for (;;) {
	IDIO expr = (*reader) (h);

	if (idio_S_eof == expr) {
	    break;
	} else {
	    es = idio_pair (expr, es);
	}
    }

    es = idio_list_reverse (es);

    IDIO_HANDLE_M_CLOSE (h) (h);

    struct timeval t_read;
    if (timing) {
	gettimeofday (&t_read, NULL);

	s = t_read.tv_sec - t0.tv_sec;
	us = t_read.tv_usec - t0.tv_usec;

	if (us < 0) {
	    us += 1000000;
	    s -= 1;
	}

	char *sname = idio_handle_name_as_C (h);
	fprintf (stderr, "load-handle-aio: %s: read time %ld.%03ld\n", sname, s, (long) us / 1000);
	free (sname);
    }

    IDIO ms = idio_S_nil;
    while (es != idio_S_nil) {
	ms = idio_pair ((*evaluator) (IDIO_PAIR_H (es), cs), ms);
	es = IDIO_PAIR_T (es);
    }
    ms = idio_list_reverse (ms);
    /* idio_debug ("load-handle-aio: ms %s\n", ms);    */

    struct timeval te;
    if (timing) {
	gettimeofday (&te, NULL);

	s = te.tv_sec - t_read.tv_sec;
	us = te.tv_usec - t_read.tv_usec;

	if (us < 0) {
	    us += 1000000;
	    s -= 1;
	}

	char *sname = idio_handle_name_as_C (h);
	fprintf (stderr, "load-handle-aio: %s: evaluation time %ld.%03ld\n", sname, s, (long) us / 1000);
	free (sname);
    }

    /*
     * When we call idio_vm_run() we are at risk of the garbage
     * collector being called so we need to save the current file
     * handle and any lists we're walking over
     */
    idio_remember_file_handle (h);
    /*
     * We might have called idio_gc_protect (and later idio_gc_expose)
     * to safeguard {ms} however we know (because we wrote the code)
     * that "load" might call a continuation (to a state before we
     * were called) which will unwind the stack and call
     * siglongjmp(3).  That means we'll never reach the
     * idio_gc_expose() call and stuff starts to accumulate in the GC
     * never to be released.
     *
     * However, invoking that continuation will clear the stack
     * including anything we stick on it here.  Very convenient.
     *
     * If you dump the stack you will find an enormous list, {ms},
     * representing the loaded file.  Which is annoying.
     */
    idio_array_push (IDIO_THREAD_STACK (thr), ms);

    idio_ai_t lh_pc = -1;
    IDIO r;
    while (idio_S_nil != ms) {
	idio_codegen (thr, IDIO_PAIR_H (ms), cs);
	if (-1 == lh_pc) {
	    lh_pc = IDIO_THREAD_PC (thr);
	    /* fprintf (stderr, "\n\n%s lh_pc == %jd\n", idio_handle_name_as_C (h), lh_pc); */
	}
	/* r = idio_vm_run (thr); */
	ms = IDIO_PAIR_T (ms);
    }

    IDIO_THREAD_PC (thr) = lh_pc;
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

	char *sname = idio_handle_name_as_C (h);
	fprintf (stderr, "load-handle-aio: %s: compile/run time %ld.%03ld\n", sname, s, (long) us / 1000);
	free (sname);
    }

    s = tr.tv_sec - t0.tv_sec;
    us = tr.tv_usec - t0.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }

#if IDIO_DEBUG
    /* fprintf (stderr, "load-handle-aio: %s: elapsed time %ld.%03ld\n", idio_handle_name_as_C (h), s, (long) us / 1000); */
    /* idio_debug (" => %s\n", r); */
#endif

    idio_ai_t ss = idio_array_size (IDIO_THREAD_STACK (thr));

    if (ss != ss0) {
	char *sname = idio_handle_name_as_C (h);
	fprintf (stderr, "load-handle-aio: %s: SS %td != %td\n", sname, ss, ss0);
	free (sname);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
    }

    idio_forget_file_handle (h);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("load-handle-aio", load_handle_aio, (IDIO h), "handle", "\
load expressions from ``handle`` all in one			\n\
								\n\
:param handle: the handle to load from				\n\
:type handle: handle						\n\
								\n\
This is the ``load-handle-aio`` primitive.			\n\
")
{
    IDIO_ASSERT (h);
    IDIO_VERIFY_PARAM_TYPE (handle, h);

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t pc0 = IDIO_THREAD_PC (thr);

    IDIO r = idio_load_handle_aio (h, idio_read, idio_evaluate, idio_vm_constants);

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }

    return r;
}

IDIO idio_load_handle_interactive (IDIO fh, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs)
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

	/*
	 * idio_command_interactive will have been set to 0 by {load}
	 * so we need to reset it, just in case.
	 */
	idio_command_set_interactive ();

	/*
	 * Throw out some messages about any recently failed jobs
	 */
	idio_command_SIGCHLD_signal_handler ();

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

    if (idio_vm_exit) {
	fprintf (stderr, "load-filehandle-interactive/exit (%d)\n", idio_exit_status);
	idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

	return idio_S_notreached;
    }

    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (thr));

    if (sp != sp0) {
	char *sname = idio_handle_name_as_C (fh);
	fprintf (stderr, "load-file-handle-interactive: %s: SP %td != SP0 %td\n", sname, sp, sp0);
	free (sname);
	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
    }

    idio_forget_file_handle (fh);

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
    IDIO_ADD_PRIMITIVE (current_error_handle);
    IDIO_ADD_PRIMITIVE (set_input_handle);
    IDIO_ADD_PRIMITIVE (set_output_handle);
    IDIO_ADD_PRIMITIVE (set_error_handle);
    IDIO_ADD_PRIMITIVE (close_handle);
    IDIO_ADD_PRIMITIVE (close_input_handle);
    IDIO_ADD_PRIMITIVE (close_output_handle);
    IDIO_ADD_PRIMITIVE (closedp_handle);
    IDIO_ADD_PRIMITIVE (eof_objectp);
    IDIO_ADD_PRIMITIVE (readyp);
    IDIO_ADD_PRIMITIVE (read);
    IDIO_ADD_PRIMITIVE (read_expr);
    IDIO_ADD_PRIMITIVE (read_line);
    IDIO_ADD_PRIMITIVE (read_lines);
    /* IDIO_ADD_PRIMITIVE (scm_read); */
    IDIO_ADD_PRIMITIVE (read_char);
    IDIO_ADD_PRIMITIVE (peek_char);
    IDIO_ADD_PRIMITIVE (eofp);
    IDIO_ADD_PRIMITIVE (write);
    IDIO_ADD_PRIMITIVE (write_char);
    IDIO_ADD_PRIMITIVE (newline);
    IDIO_ADD_PRIMITIVE (display);
    IDIO_ADD_PRIMITIVE (printf);
    IDIO_ADD_PRIMITIVE (handle_line);
    IDIO_ADD_PRIMITIVE (handle_pos);
    IDIO_ADD_PRIMITIVE (handle_location);
    IDIO_ADD_PRIMITIVE (flush_handle);
    IDIO_ADD_PRIMITIVE (seek_handle);
    IDIO_ADD_PRIMITIVE (handle_rewind);
    IDIO_ADD_PRIMITIVE (load_handle_aio);
    IDIO_ADD_PRIMITIVE (load_handle_ebe);

    IDIO load_handle_sym = idio_symbols_C_intern ("load-handle");
    IDIO load_handle_ebe_sym = idio_symbols_C_intern ("load-handle-ebe");
    idio_module_export_symbol_value (load_handle_sym,
				     idio_module_primitive_symbol_value (load_handle_ebe_sym, idio_S_nil),
				     idio_Idio_module_instance ());

    IDIO load_handle_ebe = idio_module_primitive_symbol_value (load_handle_ebe_sym, idio_S_nil);
    IDIO load_handle = idio_module_toplevel_symbol_value (load_handle_sym, idio_S_nil);
    idio_set_property (load_handle, idio_KW_sigstr, idio_get_property (load_handle_ebe, idio_KW_sigstr, idio_S_nil));
    idio_set_property (load_handle, idio_KW_docstr_raw, idio_get_property (load_handle_ebe, idio_KW_docstr_raw, idio_S_nil));
}

void idio_final_handle ()
{
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
