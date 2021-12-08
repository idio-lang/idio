/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "codegen.h"
#include "condition.h"
#include "error.h"
#include "evaluate.h"
#include "file-handle.h"
#include "fixnum.h"
#include "handle.h"
#include "idio-string.h"
#include "job-control.h"
#include "module.h"
#include "pair.h"
#include "read.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm.h"

void idio_handle_read_error (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("handle read error", msh);

    IDIO lsh = idio_open_output_string_handle_C ();
    idio_display (idio_vm_source_location (), lsh);
    idio_error_func_name (lsh, ":", NULL);

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_read_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       idio_get_output_string (lsh),
					       detail,
					       IDIO_HANDLE_PATHNAME (h)));
    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

void idio_handle_write_error (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("handle write error", msh);

    IDIO lsh = idio_open_output_string_handle_C ();
    idio_display (idio_vm_source_location (), lsh);
    idio_error_func_name (lsh, ":", NULL);

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_write_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       idio_get_output_string (lsh),
					       detail,
					       IDIO_HANDLE_PATHNAME (h)));
    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

void idio_handle_closed_error (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("handle already closed", msh);

    IDIO lsh = idio_open_output_string_handle_C ();
    idio_display (idio_vm_source_location (), lsh);
    idio_error_func_name (lsh, ":", NULL);

    IDIO detail = idio_S_nil;

#ifdef IDIO_DEBUG
    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (c_location, dsh);
    detail = idio_get_output_string (dsh);
#endif

    IDIO c = idio_struct_instance (idio_condition_io_closed_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       idio_get_output_string (lsh),
					       detail,
					       IDIO_HANDLE_PATHNAME (h)));
    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static void idio_handle_type_error (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_param_type ("handle", h, c_location);

    /* notreached */
}

static void idio_handle_bad_input_error (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    if (idio_isa_handle (h)) {
	size_t hn_size;
	char *hn = idio_string_as_C (IDIO_HANDLE_FILENAME (h), &hn_size);

	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "handle '%s' is not a input handle", hn);

	IDIO_GC_FREE (hn, hn_size);

	idio_error_param_type_msg (em, c_location);
    } else {
	idio_error_param_type ("input handle", h, c_location);
    }

    /* notreached */
}

static void idio_handle_bad_output_error (IDIO h, IDIO c_location)
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    if (idio_isa_handle (h)) {
	size_t hn_size;
	char *hn = idio_string_as_C (IDIO_HANDLE_FILENAME (h), &hn_size);

	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "handle '%s' is not a output handle", hn);

	IDIO_GC_FREE (hn, hn_size);

	idio_error_param_type_msg (em, c_location);
    } else {
	idio_error_param_type ("output handle", h, c_location);
    }

    /* notreached */
}

/*
 * Code coverage:
 *
 * This is called from idio_ungetc_handle() and requires some coding
 * error.
 */
void idio_handle_lookahead_error (IDIO h, int c)
{
    size_t size = 0;
    idio_error_printf (IDIO_C_FUNC_LOCATION (), "%s->unget => %#x (!= EOF)", idio_handle_name_as_C (h, &size), c);

    /* notreached */
}

/*
 * lookahead char: getc(3) returns an "(int) unsigned char" or EOF
 * therefore a safe value for "there is no lookahead char available"
 * is EOF.  The lookahead char is thus conceptually different to the
 * result of getc(3) itself as EOF now means "actually call getc(3)".
 *
 * EOF itself is now moot in the face of UTF-8 inputs.  Whilst the
 * byte 0xff -- which will be recast as EOF, -1 -- is invalid UTF-8 it
 * is used extensively (enough) as a test for validating UTF-8
 * decoders!
 *
 * So you can't do a "switch (c) { case EOF: ..." or "(EOF == c)"
 * comparison any more!
 */

static void idio_handle_method_error (char const *func, char const *msg, IDIO c_location)
{
    IDIO_C_ASSERT (func);
    IDIO_C_ASSERT (msg);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_param_value_msg_only (func, "method", msg, c_location);

    /* notreached */
}

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

/*
 * Code coverage:
 *
 * There's no direct user call to here but handle-location does use
 * idio_handle_name_as_C().
 */
char *idio_handle_name_as_C (IDIO h, size_t *sizep)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    IDIO hname = IDIO_HANDLE_FILENAME (h);
    if (idio_S_nil == hname) {
	hname = IDIO_HANDLE_PATHNAME (h);
    }
    char *name;;
    *sizep = 0;
    if (idio_isa_string (hname)) {
	name = idio_string_as_C (hname, sizep);
    } else {
	/*
	 * I need to stop myself printing things out
	 * during shutdown...
	 */
	name = "n/r";
    }

    return name;
}

IDIO_DEFINE_PRIMITIVE1_DS ("handle?", handlep, (IDIO o), "o", "\
test if `o` is a handle				\n\
						\n\
:param o: object to test			\n\
:return: ``#t`` if `o` is a handle, ``#f`` otherwise	\n\
:rtype: boolean					\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_handle (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("input-handle?", input_handlep, (IDIO o), "o", "\
test if `o` is an input handle				\n\
							\n\
:param o: object to test				\n\
:return: ``#t`` if `o` is an input handle, ``#f`` otherwise	\n\
:rtype: boolean						\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_handle (o) &&
	IDIO_INPUTP_HANDLE (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("output-handle?", output_handlep, (IDIO o), "o", "\
test if `o` is an output handle				\n\
							\n\
:param o: object to test				\n\
:return: ``#t`` if `o` is an output handle, ``#f`` otherwise	\n\
:rtype: boolean						\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_handle (o) &&
	IDIO_OUTPUTP_HANDLE (o)) {
	r = idio_S_true;
    }

    return r;
}

void idio_free_handle (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (handle, h);

    IDIO_HANDLE_M_FREE (h) (h);

    IDIO_GC_FREE (h->u.handle, sizeof (idio_handle_t));
}

/*
 * Basic IO on handles
 */

int idio_readyp_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The problem, here, is that ready? has invoked
	 * idio_handle_or_current() which does this test for us.
	 *
	 * Otherwise a coding error?
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    if (EOF != IDIO_HANDLE_LC (h)) {
	/*
	 * Code coverage:
	 *
	 * IDIO_HANDLE_LC() will be set after a peek-char
	 */
	return 1;
    }

    return IDIO_HANDLE_M_READYP (h) (h);
}

IDIO_DEFINE_PRIMITIVE0V_DS ("ready?", readyp, (IDIO args), "[handle]", "\
test if `handle` has input available or is at end-of-file	\n\
								\n\
:param handle: handle to test, defaults to the current input handle	\n\
:type handle: handle, optional				\n\
:return: ``#t`` if `handle` has input available or is at end-of-file, ``#f`` otherwise	\n\
:rtype: boolean						\n\
")
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO r = idio_S_false;

    if (idio_readyp_handle (h)) {
	r = idio_S_true;
    }

    return r;
}

/*
 * idio_getb_handle() is called from idio_read_character_int(), which
 * constructs a Unicode codepoint from the UTF-8 stream, and
 * idio_read_string(), which constructs a string byte-by-byte (for,
 * uh, $REASONS).
 *
 * Everything else should be calling idio_getc_handle(), which calls
 * idio_read_character_int() in turn, to get an idio_unicode_t
 * (Unicode codepoint).
 */
int idio_getb_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * Coding error
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int r = IDIO_HANDLE_LC (h);

    if (EOF != r) {
	/* there was a lookahead char so reset the flag */
	IDIO_HANDLE_LC (h) = EOF;
    } else {
	r = IDIO_HANDLE_M_GETB (h) (h);
    }

    /*
     * We were bumping the line/pos up even if we'd hit EOF -- which
     * confused the debugger.
     */
    if (idio_eofp_handle (h) == 0) {
	/*
	 * Only increment the line number if we have a valid line number
	 * to start with
	 */
	if ('\n' == r &&
	    IDIO_HANDLE_LINE (h)) {
	    IDIO_HANDLE_LINE (h) += 1;
	}

	IDIO_HANDLE_POS (h) += 1;
    }

    return r;
}

idio_unicode_t idio_getc_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * All user interfaces to this call idio_handle_or_current()
	 * which does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int r = IDIO_HANDLE_LC (h);

    if (EOF != r) {
	/* there was a lookahead char so reset the flag */
	IDIO_HANDLE_LC (h) = EOF;

	/*
	 * Handling LINE/POS would normally have been done in
	 * idio_getb_handle() via idio_read_character_int() unless
	 * we're using LC in which case we need to do it ourselves.
	 */
	if ('\n' == r &&
	    IDIO_HANDLE_LINE (h)) {
	    IDIO_HANDLE_LINE (h) += 1;
	}

	IDIO_HANDLE_POS (h) += 1;
    } else {
	r = idio_read_character_int (h, idio_S_nil, IDIO_READ_CHARACTER_SIMPLE);
    }

    return r;
}

int idio_ungetc_handle (IDIO h, idio_unicode_t c)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The user interface to this calls idio_handle_or_current()
	 * which does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int r = IDIO_HANDLE_LC (h);

    if (EOF != r) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */

	/* there already was a lookahead char */
	idio_handle_lookahead_error (h, r);

	/* notreached */
	return EOF;
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
	/*
	 * Test Case: ??
	 *
	 * The user interface to this calls idio_handle_or_current()
	 * which does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    idio_unicode_t c = idio_getc_handle (h);
    idio_ungetc_handle (h, c);

    return c;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("peek-char", peek_char, (IDIO args), "[handle]", "\
return the next Unicode code point from `handle`	\n\
							\n\
:param handle: handle to observe, defaults to the current input handle	\n\
:type handle: handle, optional				\n\
:return: Unicode code point				\n\
:rtype: unicode						\n\
")
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
	/*
	 * Test Case: ??
	 *
	 * The user interface to this calls idio_handle_or_current()
	 * which does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    return IDIO_HANDLE_M_EOFP (h) (h);
}

IDIO_DEFINE_PRIMITIVE0V_DS ("eof?", eofp, (IDIO args), "[handle]", "\
test if `handle` is not supplied is at end-of-file	\n\
							\n\
:param handle: handle to test, defaults to the current input handle	\n\
:type handle: handle, optional				\n\
:return: ``#t`` if `handle` is at end-of-file, ``#f`` otherwise	\n\
:rtype: boolean						\n\
")
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO r = idio_S_false;

    if (idio_eofp_handle (h)) {
	r = idio_S_true;
    }

    return r;
}

/*
 * Code coverage:
 *
 * This code isn't called but I guess we should leave it in for a
 * C-level requests to close a handle.
 */
int idio_close_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The user interface to this calls idio_isa_handle() which
	 * does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    int r = IDIO_HANDLE_M_CLOSE (h) (h);
    IDIO_HANDLE_FLAGS (h) |= IDIO_HANDLE_FLAG_CLOSED;

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("close-handle", close_handle, (IDIO h), "handle", "\
Close the handle `handle`				\n\
							\n\
:param handle: handle to close				\n\
:type handle: handle					\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: handle-errors/close-bad-type.idio
	 *
	 * close-handle #t
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("close-input-handle", close_input_handle, (IDIO h), "handle", "\
Close the input handle `handle`				\n\
							\n\
:param handle: handle to close				\n\
:type handle: input handle				\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (h) &&
	   IDIO_INPUTP_HANDLE (h))) {
	/*
	 * Test Case: handle-errors/close-input-bad-type.idio
	 *
	 * close-input-handle (current-output-handle)
	 */
	idio_handle_bad_input_error (h, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("close-output-handle", close_output_handle, (IDIO h), "handle", "\
Close the output handle `handle`				\n\
							\n\
:param handle: handle to close				\n\
:type handle: output handle				\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    if (! (idio_isa_handle (h) &&
	   IDIO_OUTPUTP_HANDLE (h))) {
	/*
	 * Test Case: handle-errors/close-output-bad-type.idio
	 *
	 * close-output-handle (current-input-handle)
	 */
	idio_handle_bad_output_error (h, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("closed-handle?", closedp_handle, (IDIO h), "handle", "\
Is handle `handle` closed?				\n\
							\n\
:param handle: handle to test				\n\
:type handle: handle					\n\
:return: ``#t`` if handle is closed, ``#f`` otherwise		\n\
:rtype: boolean						\n\
")
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: handle-errors/closed-handle-bad-type.idio
	 *
	 * closed-handle? #t
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO r = idio_S_false;

    if (IDIO_CLOSEDP_HANDLE (h)) {
	r = idio_S_true;
    }

    return r;
}

int idio_putb_handle (IDIO h, uint8_t c)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return EOF;
    }

    int n = IDIO_HANDLE_M_PUTB (h) (h, c);

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
	     * A flush-handle here fixes that but it feels wrong.
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

int idio_putc_handle (IDIO h, idio_unicode_t c)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The user interfaces to this call
	 * idio_isa_handle()/idio_handle_or_current() which do this
	 * test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

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
	     * A flush-handle here fixes that but it feels wrong.
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

ptrdiff_t idio_puts_handle (IDIO h, char const *s, size_t const slen)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (s);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The user interface to this calls idio_handle_or_current()
	 * which does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    ptrdiff_t n = IDIO_HANDLE_M_PUTS (h) (h, s, slen);

    if (EOF != n) {
	IDIO_HANDLE_POS (h) += n;
    }

    return n;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("puts", puts, (IDIO s, IDIO args), "s [handle]", "\
Write the printed form of `s` to `handle`		\n\
							\n\
:param s: string to be printed				\n\
:type s: string						\n\
:param handle: handle to print to, defaults to the current output handle	\n\
:type handle: handle, optional				\n\
:return: the number of bytes written			\n\
:rtype: integer						\n\
")
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (string, s)

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    size_t size = 0;
    char *str = idio_string_as_C (s, &size);

    ptrdiff_t n = idio_puts_handle (h, str, size);

    IDIO_GC_FREE (str, size);

    return idio_integer (n);
}

int idio_flush_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The user interface to this calls idio_isa_handle() which
	 * does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    return IDIO_HANDLE_M_FLUSH (h) (h);
}

IDIO_DEFINE_PRIMITIVE1_DS ("flush-handle", flush_handle, (IDIO h), "handle", "\
Invoke the flush method on `handle`			\n\
							\n\
:param handle: handle to flush				\n\
:type handle: handle					\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    /*
     * Test Case: file-handle-errors/flush-handle-bad-type.idio
     *
     * flush-handle #t
     */
    IDIO_USER_TYPE_ASSERT (handle, h);

    idio_flush_handle (h);

    return idio_S_unspec;
}

off_t idio_seek_handle (IDIO h, off_t offset, int whence)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The user interface to this call idio_isa_handle() which
	 * does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    /* line number is invalidated unless we go to pos 0 */
    if (0 == offset &&
	SEEK_SET == whence) {
	IDIO_HANDLE_LINE (h) = 1;
    } else {
	IDIO_HANDLE_LINE (h) = 0;
    }

    if (SEEK_CUR == whence) {
	offset += IDIO_HANDLE_POS (h);
	whence = SEEK_SET;
    }

    if (NULL == IDIO_HANDLE_M_SEEK (h)) {
	idio_handle_method_error ("seek-handle", "seek not available", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    } else {
	IDIO_HANDLE_POS (h) = IDIO_HANDLE_M_SEEK (h) (h, offset, whence);
    }

    /*
     * fseek(3): A successful call to the fseek() function clears the
     * end-of-file indicator for the stream and undoes any effects of
     * the ungetc(3) function on the same stream.
     *
     * EOF is handle implementation specific but we can clear any
     * lookahead character here.
     */
    IDIO_HANDLE_LC (h) = EOF;

    return IDIO_HANDLE_POS (h);
}

IDIO_DEFINE_PRIMITIVE2V_DS ("seek-handle", seek_handle, (IDIO h, IDIO pos, IDIO args), "handle pos [whence]", "\
seek to the given `pos` in `handle`			\n\
							\n\
if one of the optional ``'set``, ``'end`` or ``'cur``	\n\
symbols is supplied for `whence` use the appropriate	\n\
*whence* flag						\n\
							\n\
:param handle: handle to seek in (file or string)	\n\
:type handle: handle					\n\
:param pos: position to seek to				\n\
:type pos: integer					\n\
:param whence: whence flag, defaults to ``'set``	\n\
:type whence: symbol, optional				\n\
:return: position actually sought to			\n\
:rtype: integer						\n\
							\n\
A successful seek will clear the end-of-file status	\n\
of the handle.						\n\
							\n\
The handle's concept of a line number is invalidated	\n\
unless `whence` is ``'set`` and position is ``0`` (zero)	\n\
")
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (pos);
    IDIO_ASSERT (args);

    /*
     * Test Case: file-handle-errors/seek-handle-bad-handle-type.idio
     *
     * seek-handle #t #t
     */
    IDIO_USER_TYPE_ASSERT (handle, h);
    /*
     * Test Case: file-handle-errors/seek-handle-bad-pos-type.idio
     *
     * seek-handle (current-input-handle) #t
     */
    IDIO_USER_TYPE_ASSERT (integer, pos);

    int whence = -1;

    if (idio_S_nil != args) {
	IDIO w = idio_list_head (args);
	/*
	 * Test Case: file-handle-errors/seek-handle-bad-whence-type.idio
	 *
	 * seek-handle (current-input-handle) 0 #t
	 */
	IDIO_USER_TYPE_ASSERT (symbol, w);

	if (3 == IDIO_SYMBOL_BLEN (w)) {
	    if (strncmp (IDIO_SYMBOL_S (w), "set", 3) == 0) {
		whence = SEEK_SET;
	    } else if (strncmp (IDIO_SYMBOL_S (w), "end", 3) == 0) {
		whence = SEEK_END;
	    } else if (strncmp (IDIO_SYMBOL_S (w), "cur", 3) == 0) {
		whence = SEEK_CUR;
	    }
	}
	if (-1 == whence) {
	    /*
	     * Test Case: handle-errors/seek-handle-bad-whence-value.idio
	     *
	     * seek-handle (current-input-handle) 0 'maybe
	     */
	    char em[BUFSIZ];
	    idio_snprintf (em, BUFSIZ, "'%s' is invalid: 'set, 'end or 'cur", IDIO_SYMBOL_S (w));

	    idio_error_param_value_msg ("seek-handle", "whence", w, em, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else {
	whence = SEEK_SET;
    }

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
	offset = idio_bignum_intmax_t_value (pos);
    }
    off_t n = idio_seek_handle (h, offset, whence);

    IDIO r = idio_integer (n);

    return r;
}

void idio_rewind_handle (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * The user interface to this call idio_isa_handle() which
	 * does this test for us.
	 *
	 * Otherwise a coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    idio_seek_handle (h, 0, SEEK_SET);
}

IDIO_DEFINE_PRIMITIVE1_DS ("rewind-handle", rewind_handle, (IDIO h), "handle", "\
Seek to position zero of `handle`			\n\
							\n\
This will reset the handle's position to zero and	\n\
the handle's line number to 1				\n\
							\n\
:param handle: handle to rewind				\n\
:type handle: handle					\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    /*
     * Test Case: file-handle-errors/rewind-handle-bad-type.idio
     *
     * rewind-handle #t
     */
    IDIO_USER_TYPE_ASSERT (handle, h);

    idio_rewind_handle (h);

    return idio_S_unspec;
}

/*
 * idio_handle_tell() is called from the bitset reader format code to
 * verify range markers
 */
off_t idio_handle_tell (IDIO h)
{
    IDIO_ASSERT (h);

    if (! idio_isa_handle (h)) {
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_handle_type_error (h, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    return IDIO_HANDLE_POS (h);
}

/*
 * primitives for handles
 */

IDIO_DEFINE_PRIMITIVE0_DS ("current-input-handle", current_input_handle, (), "", "\
Return the current input handle		\n\
					\n\
:return: current input handle		\n\
:rtype: handle				\n\
")
{
    return idio_thread_current_input_handle ();
}

IDIO_DEFINE_PRIMITIVE0_DS ("current-output-handle", current_output_handle, (), "", "\
Return the current output handle	\n\
					\n\
:return: current output handle		\n\
:rtype: handle				\n\
")
{
    return idio_thread_current_output_handle ();
}

IDIO_DEFINE_PRIMITIVE0_DS ("current-error-handle", current_error_handle, (), "", "\
Return the current error handle		\n\
					\n\
:return: current error handle		\n\
:rtype: handle				\n\
")
{
    return idio_thread_current_error_handle ();
}

IDIO_DEFINE_PRIMITIVE1_DS ("set-input-handle!", set_input_handle, (IDIO h), "handle", "\
Set the current input handle to `handle`		\n\
							\n\
:param handle: handle to use				\n\
:type handle: handle					\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    /*
     * Test Case: file-handle-errors/set-input-handle-bad-type.idio
     *
     * set-input-handle! #t
     */
    IDIO_USER_TYPE_ASSERT (handle, h);

    idio_thread_set_current_input_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("set-output-handle!", set_output_handle, (IDIO h), "handle", "\
Set the current output handle to `handle`		\n\
							\n\
:param handle: handle to use				\n\
:type handle: handle					\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    /*
     * Test Case: file-handle-errors/set-output-handle-bad-type.idio
     *
     * set-output-handle! #t
     */
    IDIO_USER_TYPE_ASSERT (handle, h);

    idio_thread_set_current_output_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("set-error-handle!", set_error_handle, (IDIO h), "handle", "\
Set the current error handle to `handle`		\n\
							\n\
:param handle: handle to use				\n\
:type handle: handle					\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);

    /*
     * Test Case: file-handle-errors/set-error-handle-bad-type.idio
     *
     * set-error-handle! #t
     */
    IDIO_USER_TYPE_ASSERT (handle, h);

    idio_thread_set_current_error_handle (h);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("eof-object?", eof_objectp, (IDIO o), "o", "\
Is `o` the end-of-file value?				\n\
							\n\
:param o: value to test					\n\
:return: ``#t`` if o is the end-of-file value, ``#f`` otherwise	\n\
:rtype: boolean						\n\
")
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
	} else if (idio_isa_handle (h)) {
	    if (! IDIO_INPUTP_HANDLE (h)) {
		/*
		 * Test Case: handle-errors/read-char-bad-handle.idio
		 *
		 * read-char (current-output-handle)
		 */
		idio_handle_read_error (h, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		return h;
	    }
	} else {
	    /*
	     * Test Case: handle-errors/read-char-bad-type.idio
	     *
	     * read-char #t
	     */
	    idio_error_param_type ("input handle", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    case IDIO_HANDLE_FLAG_WRITE:
	if (idio_S_nil == h) {
	    return idio_thread_current_output_handle ();
	} else if (idio_isa_handle (h)) {
	    if (! IDIO_OUTPUTP_HANDLE (h)) {
		/*
		 * Test Case: handle-errors/display-bad-handle.idio
		 *
		 * display "foo" (current-input-handle)
		 */
		idio_handle_write_error (h, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    } else {
		return h;
	    }
	} else {
	    /*
	     * Test Case: handle-errors/display-bad-type.idio
	     *
	     * display "foo" #t
	     */
	    idio_error_param_type ("output handle", h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	break;
    default:
	/*
	 * Test Case: ??
	 *
	 * Coding error.
	 */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected mode %d", mode);

	return idio_S_notreached;
	break;
    }

    return idio_S_notreached;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("read", read, (IDIO args), "[handle]", "\
read an Idio expression from `handle`			\n\
							\n\
:param handle: handle to read from, defaults to the current input handle	\n\
:type handle: handle, optional				\n\
:return: object						\n\
")
{
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_READ);

    IDIO expr = idio_read (h);

    return expr;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("read-expr", read_expr, (IDIO args), "[handle]", "\
read a single Idio expression from `handle`		\n\
							\n\
:param handle: handle to read from, defaults to the current input handle	\n\
:type handle: handle, optional					\n\
:return: object						\n\
")
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
	idio_unicode_t c = idio_getc_handle (h);
	if (idio_eofp_handle (h) ||
	    '\n' == c) {
	    return idio_get_output_string (osh);
	} else {
	    idio_putc_string_handle (osh, c);
	}
    }

}

IDIO_DEFINE_PRIMITIVE0V_DS ("read-line", read_line, (IDIO args), "[handle]", "\
read a string from `handle` up to a ``#\\{newline}`` character	\n\
								\n\
:param handle: handle to read from, defaults to the current input handle	\n\
:type handle: handle, optional					\n\
:return: string excluding the newline				\n\
:rtype: string							\n\
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
	idio_unicode_t c = idio_getc_handle (h);
	if (idio_eofp_handle (h)) {
	    return idio_get_output_string (osh);
	} else {
	    idio_putc_string_handle (osh, c);
	}
    }

}

IDIO_DEFINE_PRIMITIVE0V_DS ("read-lines", read_lines, (IDIO args), "[handle]", "\
read from `handle` up to the end of file		\n\
							\n\
:param handle: handle to read from, defaults to the current input handle	\n\
:type handle: handle, optional				\n\
:return: string						\n\
:rtype: string						\n\
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

IDIO_DEFINE_PRIMITIVE0V_DS ("read-char", read_char, (IDIO args), "[handle]", "\
read a UTF-8 encoded character from `handle`		\n\
							\n\
:param handle: handle to read from, defaults to the current input handle	\n\
:type handle: handle, optional				\n\
:return: Unicode code point				\n\
:rtype: unicode						\n\
")
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

    size_t size = 0;
    char *os = idio_as_string_safe (o, &size, 10, 1);

    idio_puts_handle (h, os, size);

    IDIO_GC_FREE (os, size);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("write", write, (IDIO o, IDIO args), "o [handle]", "\
write the printed representation of `o` to `handle`	\n\
							\n\
:param o: object					\n\
:param handle: handle to write to, defaults to the current output handle	\n\
:type handle: handle, optional				\n\
:return: ``#<unspec>``					\n\
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

IDIO_DEFINE_PRIMITIVE1V_DS ("write-char", write_char, (IDIO c, IDIO args), "c [handle]", "\
write a UTF-8 encoded character to `handle`		\n\
							\n\
:param c: code point to write				\n\
:type c: unicode					\n\
:param handle: handle to write to, defaults to the current output handle	\n\
:type handle: handle, optional				\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (c);
    IDIO_ASSERT (args);

    /*
     * Test Case: file-handle-errors/write-char-bad-type.idio
     *
     * write-char #t
     */
    IDIO_USER_TYPE_ASSERT (unicode, c);

    /*
     * If you want to write out noncharacter Unicode code points use a
     * string with a \xhh escape.
     */
    idio_unicode_t c_C = IDIO_UNICODE_VAL (c);
    if (idio_unicode_character_code_point (c_C)) {
	IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

	return idio_write_char (c, h);
    } else {
	/*
	 * Test Case: handle-errors/write-char-noncharacter.idio
	 *
	 * write-char (integer->unicode #xFDD0)
	 */
	char em[BUFSIZ];
	idio_snprintf (em, BUFSIZ, "U+%04" PRIX32 " is not a character", c_C);

	idio_error_param_value_msg_only ("write-char", "Unicode code point", em, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }
}

IDIO_DEFINE_PRIMITIVE0V_DS ("newline", newline, (IDIO args), "[handle]", "\
write a UTF-8 encoded U+000A character to `handle`	\n\
							\n\
:param handle: handle to write to, defaults to the current output handle	\n\
:type handle: handle, optional				\n\
:return: ``#<unspec>``					\n\
")
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

    size_t size = 0;
    char *s = idio_display_string (o, &size);

    idio_puts_handle (h, s, size);

    IDIO_GC_FREE (s, size);

    return idio_S_unspec;
}

IDIO idio_display_C_len (char const *s, size_t const blen, IDIO h)
{
    IDIO_C_ASSERT (s);
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    idio_puts_handle (h, s, blen);

    return idio_S_unspec;
}

IDIO idio_display_C (char const *s, IDIO h)
{
    IDIO_C_ASSERT (s);
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    return idio_display_C_len (s, strlen (s), h);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("display", display, (IDIO o, IDIO args), "o [handle]", "\
display `o` to `handle`					\n\
							\n\
:param o: object					\n\
:param handle: handle to write to, defaults to the current output handle	\n\
:type handle: handle, optional				\n\
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (o);
    IDIO_ASSERT (args);

    IDIO h = idio_handle_or_current (idio_list_head (args), IDIO_HANDLE_FLAG_WRITE);

    return idio_display (o, h);
}

/*
 * Code coverage:
 *
 * %printf is deprecated in favour of printf/eprintf/hprintf/sprintf
 * and friends
 */
IDIO_DEFINE_PRIMITIVE2V_DS ("%printf", printf, (IDIO h, IDIO fmt, IDIO args), "handle fmt [args]", "\
printf `fmt` to `handle` using `args` as required	\n\
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
:return: ``#<unspec>``					\n\
")
{
    IDIO_ASSERT (h);
    IDIO_ASSERT (fmt);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (handle, h);
    IDIO_USER_TYPE_ASSERT (string, fmt);

    if (! (idio_S_nil == args ||
	   idio_isa_pair (args))) {
	idio_error_param_type ("list", args, IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    size_t blen = 0;
    char *fmt_C = idio_string_as_C (fmt, &blen);
    /*
     * fmt could have contained ASCII NULs.  So we care?  It means the
     * format string has been truncated?
     *
     * Should we, Idio, print the NULs?
     */

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
		int left_aligned = 0;
		if ((si + 1) < blen) {
		    char *c = ss + 1;
		    switch (*c) {
		    case '-':
			left_aligned = 1;
			ss++;
			si++;
			break;
		    }
		}
		/*
		 * width
		 */
		char width_c[BUFSIZ];
		int width_len = 0;
		while ((si + 1) < blen) {
		    char *c = ss + 1;
		    if (isdigit (*c)) {
			width_c[width_len++] = *c;
			ss++;
			si++;
		    } else {
			break;
		    }
		}
		long int width = 0;
		if (width_len) {
		    width_c[width_len] = '\0';
		    char *end;
		    width = strtol (width_c, &end, 10);
		    if (0 == width &&
			end == width_c) {
			fprintf (stderr, "%%printf: bad width? '%s'\n", width_c);
		    }
		}
		/*
		 * precision
		 *
		 * preceded by a dot
		 */
		char prec_c[BUFSIZ];
		int prec_len = 0;
		if ((si + 1) < blen) {
		    char *c = ss + 1;
		    switch (*c) {
		    case '.':
			ss++;
			si++;
			while ((si + 1) < blen) {
			    char *c = ss + 1;
			    if (isdigit (*c)) {
				prec_c[prec_len++] = *c;
				ss++;
				si++;
			    } else {
				break;
			    }
			}
			break;
		    }
		}
		long int prec = 0;
		if (prec_len) {
		    prec_c[prec_len] = '\0';
		    char *end;
		    prec = strtol (prec_c, &end, 10);
		    if (0 == prec &&
			end == prec_c) {
			fprintf (stderr, "%%printf: bad prec? '%s'\n", prec_c);
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
			    memcpy (fmt, s, ss - s);
			    fmt[ss - s] = '\0';

			    if (idio_S_nil != args) {
				IDIO arg = IDIO_PAIR_H (args);
				args = IDIO_PAIR_T (args);
				if (idio_isa_fixnum (arg)) {
				    intmax_t n = IDIO_FIXNUM_VAL (arg);
				    char str[BUFSIZ];
				    size_t str_len = idio_snprintf (str, BUFSIZ, fmt, n);
				    idio_puts_handle (h, str, str_len);
				} else if (idio_isa_bignum (arg)) {
				    size_t s_size = 0;
				    s = idio_bignum_as_string (arg, &s_size);
				    idio_puts_handle (h, s, s_size);
				    IDIO_GC_FREE (s, s_size);
				} else {
				    /* ?? */
				    idio_puts_handle (h, c, ss - s);
				}
			    } else {
				c = "<no-arg>";
				idio_puts_handle (h, c, sizeof (c) - 1);
			    }
			    s = ss;
			    i = si;
			}
			break;
		    case 's':
			{
			    if (idio_S_nil != args) {
				IDIO arg = IDIO_PAIR_H (args);
				args = IDIO_PAIR_T (args);

				size_t c_size = 0;
				c = idio_display_string (arg, &c_size);

				size_t c_prec_width = c_size;
				if (prec &&
				    prec < c_size) {
				    c_prec_width = prec;
				}

				if (0 == left_aligned &&
				    c_prec_width < width) {
				    size_t i = width - c_prec_width;
				    while (i) {
					idio_putc_handle (h, ' ');
					i--;
				    }
				}
				idio_puts_handle (h, c, c_prec_width);
				if (left_aligned &&
				    c_prec_width < width) {
				    size_t i = width - c_prec_width;
				    while (i) {
					idio_putc_handle (h, ' ');
					i--;
				    }
				}

				IDIO_GC_FREE (c, c_size);
			    } else {
				c = "<no-arg>";
				idio_puts_handle (h, c, sizeof (c) - 1);
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

    IDIO_GC_FREE (fmt_C, blen);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("handle-line", handle_line, (IDIO handle), "handle", "\
Return the current line number of handle `handle`	\n\
							\n\
:param handle: handle to report on			\n\
:type handle: handle					\n\
:return: line number					\n\
:rtype: integer						\n\
							\n\
The handle's `line` can be invalidated by :ref:`seek-handle <seek-handle>`.	\n\
")
{
    IDIO_ASSERT (handle);

    IDIO_USER_TYPE_ASSERT (handle, handle);

    IDIO r;
    off_t line = IDIO_HANDLE_LINE (handle);
    r = idio_integer (line);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("handle-pos", handle_pos, (IDIO handle), "handle", "\
Return the current position of handle `handle`		\n\
							\n\
:param handle: handle to report on			\n\
:type handle: handle					\n\
:return: position					\n\
:rtype: integer						\n\
")
{
    IDIO_ASSERT (handle);

    IDIO_USER_TYPE_ASSERT (handle, handle);

    IDIO r;
    off_t pos = IDIO_HANDLE_POS (handle);
    r = idio_integer (pos);

    return r;
}

IDIO idio_handle_location (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (handle, h);

    size_t size = 0;
    char *sname = idio_handle_name_as_C (h, &size);
    char buf[BUFSIZ];
    size_t buflen = idio_snprintf (buf, BUFSIZ, "%s:line %jd", sname, (intmax_t) IDIO_HANDLE_LINE (h));

    IDIO_GC_FREE (sname, size);

    return idio_string_C_len (buf, buflen);
}

IDIO_DEFINE_PRIMITIVE1_DS ("handle-location", handle_location, (IDIO h), "handle", "\
Return a string representation of the current location	\n\
in handle `handle`					\n\
							\n\
:samp:`{name}:line {number}`				\n\
							\n\
:param handle: handle to report on			\n\
:type handle: handle					\n\
:return: handle location				\n\
:rtype: string						\n\
							\n\
The handle's `line` can be invalidated by :ref:`seek-handle <seek-handle>`.	\n\
")
{
    IDIO_ASSERT (h);

    /*
     * Test Case: file-handle-errors/handle-location-bad-type.idio
     *
     * handle-location #t
     */
    IDIO_USER_TYPE_ASSERT (handle, h);

    return idio_handle_location (h);
}

IDIO_DEFINE_PRIMITIVE1_DS ("handle-name", handle_name, (IDIO handle), "handle", "\
return the name associated with handle `handle`	\n\
						\n\
:param handle: handle to query			\n\
:type handle: handle				\n\
:return: name					\n\
:rtype: string					\n\
")
{
    IDIO_ASSERT (handle);

    /*
     * Test Case: handle-errors/handle-name-bad-type.idio
     *
     * handle-name #t
     */
    IDIO_USER_TYPE_ASSERT (handle, handle);

    return IDIO_HANDLE_FILENAME (handle);
}

IDIO idio_load_handle (IDIO h, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs, int preserve)
{
    IDIO_ASSERT (h);
    IDIO_C_ASSERT (reader);
    IDIO_C_ASSERT (evaluator);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (handle, h);
    IDIO_TYPE_ASSERT (array, cs);

    if (idio_isa_file_handle (h) &&
	IDIO_FILE_HANDLE_FLAGS (h) & IDIO_FILE_HANDLE_FLAG_INTERACTIVE) {
	/*
	 * Code coverage:
	 *
	 * Requires an interactive session.
	 */
	return idio_load_handle_interactive (h, reader, evaluator, cs);
    }

    /*
     * Explicitly disable interactive for the duration of a load
     *
     * It will be reset just prior to the prompt, if required.
     */
    idio_job_control_set_interactive (0);

    IDIO thr = idio_thread_current_thread ();
    IDIO stack = IDIO_THREAD_STACK (thr);

    /*
     * When we call idio_vm_run() we are at risk of the garbage
     * collector being called so we need to save the current file
     * handle and any lists we're walking over
     *
     * XXX This only affects calls from C -- load-handle, below, is
     * actually perturbed by stashing the handle on the stack.
     */
    if (preserve) {
	idio_array_push (stack, h);
    }

    idio_ai_t ss0 = idio_array_size (IDIO_THREAD_STACK (thr));

    IDIO e = idio_S_nil;
    IDIO r = idio_S_nil;

    for (;;) {
#ifdef IDIO_LOAD_TIMING
	struct timeval t0;
	if (gettimeofday (&t0, NULL) == -1) {
	    perror ("gettimeofday");
	}
#endif
	e = (*reader) (h);

	if (idio_S_eof == e) {
	    break;
	} else {
#ifdef IDIO_LOAD_TIMING
	    struct timeval te;
	    struct timeval td;
	    fprintf (stderr, "  load %4d", i++);
#endif
	    IDIO m = (*evaluator) (e, cs);

#ifdef IDIO_LOAD_TIMING
	    if (gettimeofday (&te, NULL) == -1) {
		perror ("gettimeofday");
	    }
	    td.tv_sec = te.tv_sec - t0.tv_sec;
	    td.tv_usec = te.tv_usec - t0.tv_usec;
	    if (td.tv_usec < 0) {
		td.tv_usec += 1000000;
		td.tv_sec -= 1;
	    }
	    fprintf (stderr, " e %ld.%06ld", td.tv_sec, td.tv_usec);
#endif

	    idio_ai_t pc = idio_codegen (thr, m, cs);

	    r = idio_vm_run_C (thr, pc);

	    idio_ai_t ss = idio_array_size (stack);

	    if (ss != ss0) {
		size_t size = 0;
		char *sname = idio_handle_name_as_C (h, &size);
		fprintf (stderr, "load-handle: %s: SS %td != %td\n", sname, ss, ss0);

		IDIO_GC_FREE (sname, size);

		idio_vm_thread_state (thr);
	    }

#ifdef IDIO_LOAD_TIMING
	    if (gettimeofday (&te, NULL) == -1) {
		perror ("gettimeofday");
	    }
	    td.tv_sec = te.tv_sec - t0.tv_sec;
	    td.tv_usec = te.tv_usec - t0.tv_usec;
	    if (td.tv_usec < 0) {
		td.tv_usec += 1000000;
		td.tv_sec -= 1;
	    }
	    fprintf (stderr, " r %ld.%06ld", td.tv_sec, td.tv_usec);
	    if (td.tv_sec > 0 ||
		td.tv_usec > 400000) {
		if (idio_S_define == IDIO_PAIR_H (e) &&
		    idio_isa_pair (IDIO_PAIR_HTT (e))) {
		    fprintf (stderr, " %zu", idio_list_length (IDIO_PAIR_HTT (e)));
		    idio_debug (" %s", IDIO_PAIR_HTT (e));
		} else {
		    idio_debug (" %s", e);
		}
	    }
	    fprintf (stderr, "\n");
#endif
	}
    }

    IDIO_HANDLE_M_CLOSE (h) (h);

    if (preserve) {
	idio_ai_t ss = idio_array_size (stack);
	if (ss == ss0) {
	    idio_array_pop (stack);
	} else {
	    fprintf (stderr, "load-handle: SS %td != %td\n", ss, ss0);
	}
    }
    return r;
}

/*
 * A dumb wrapper to idio_load_handle()
 */
IDIO idio_load_handle_C (IDIO h, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs)
{
    return idio_load_handle (h, reader, evaluator, cs, 1);
}

IDIO_DEFINE_PRIMITIVE1_DS ("load-handle", load_handle, (IDIO h), "handle", "\
load expressions from `handle` expression by expression		\n\
								\n\
:param handle: the handle to load from				\n\
:type handle: handle						\n\
								\n\
This is the `load-handle` primitive.				\n\
")
{
    IDIO_ASSERT (h);

    /*
     * Test Case: ??
     *
     * load-handle is overriden elsewhere
     */
    IDIO_USER_TYPE_ASSERT (handle, h);

    IDIO thr = idio_thread_current_thread ();
    idio_ai_t pc0 = IDIO_THREAD_PC (thr);

    IDIO r = idio_load_handle (h, idio_read, idio_evaluate, idio_vm_constants, 0);

    idio_ai_t pc = IDIO_THREAD_PC (thr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (thr) = pc0;
    }

    return r;
}

/*
 * Code coverage:
 *
 * idio_load_handle_interactive() isn't tested by the automation
 * scripts
 */
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
	 * Throw out some messages about any recently failed jobs
	 */
	idio_vm_invoke_C (idio_thread_current_thread (),
			  idio_module_symbol_value (IDIO_SYMBOLS_C_INTERN ("do-job-notification"),
						    idio_job_control_module,
						    idio_S_nil));

	/*
	 * idio_command_interactive will have been set to 0 by {load}
	 * so we need to reset it, just in case.
	 */
	idio_job_control_set_interactive (idio_job_control_tty_isatty);

	idio_flush_handle (oh);

	IDIO eh = idio_thread_current_error_handle ();
#ifdef IDIO_DEBUG
	char pbuf[BUFSIZ];
	idio_snprintf (pbuf, BUFSIZ, "[%d] ", getpid ());

	idio_display_C (pbuf, eh);
#endif
	idio_display (IDIO_MODULE_NAME (cm), eh);
	idio_display_C ("> ", eh);

	IDIO e = (*reader) (fh);

	if (idio_S_eof == e) {
	    break;
	}

	IDIO m = (*evaluator) (e, cs);
	idio_ai_t pc = idio_codegen (thr, m, cs);

	IDIO r = idio_vm_run_C (thr, pc);
	/*
	 * NB.  We must deliberately call idio_as_string() because the
	 * idio_print_handle (oh, r) method will call
	 * idio_display_string(), ie. strings will not be
	 * double-quoted which is *precisely* what we want here.
	 */
	size_t r_size = 0;
	char *rs = idio_as_string_safe (r, &r_size, 40, 1);
	idio_puts_handle (oh, rs, r_size);

	IDIO_GC_FREE (rs, r_size);

	idio_display_C ("\n", oh);
    }

    IDIO_HANDLE_M_CLOSE (fh) (fh);

    if (idio_vm_exit) {
	fprintf (stderr, "load-filehandle-interactive/exit (%d)\n", idio_exit_status);
	idio_vm_restore_exit (idio_k_exit, idio_S_unspec);

	return idio_S_notreached;
    }

    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (thr));

    if (sp != sp0) {
	size_t size = 0;
	char *sname = idio_handle_name_as_C (fh, &size);
	fprintf (stderr, "load-file-handle-interactive: %s: SP %td != SP0 %td\n", sname, sp, sp0);

	IDIO_GC_FREE (sname, size);

	idio_debug ("THR %s\n", thr);
	idio_debug ("STK %s\n", IDIO_THREAD_STACK (thr));
    }

    idio_forget_file_handle (fh);

    return idio_S_unspec;
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
    IDIO_ADD_PRIMITIVE (handle_name);
    IDIO_ADD_PRIMITIVE (puts);
    IDIO_ADD_PRIMITIVE (flush_handle);
    IDIO_ADD_PRIMITIVE (seek_handle);
    IDIO_ADD_PRIMITIVE (rewind_handle);
    IDIO_ADD_PRIMITIVE (load_handle);
}

void idio_init_handle ()
{
    idio_module_table_register (idio_handle_add_primitives, NULL, NULL);
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
