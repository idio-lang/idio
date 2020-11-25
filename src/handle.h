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
 * handle.h
 *
 */

#ifndef HANDLE_H
#define HANDLE_H

void idio_handle_error_read (IDIO h, IDIO c_location);
void idio_handle_error_write (IDIO h, IDIO c_location);
void idio_handle_error_closed (IDIO h, IDIO c_location);

IDIO idio_handle ();
int idio_isa_handle (IDIO d);
char *idio_handle_name_as_C (IDIO h);
void idio_free_handle (IDIO d);
void idio_handle_finalizer (IDIO handle);

void idio_free_handle (IDIO h);
int idio_readyp_handle (IDIO h);
int idio_getb_handle (IDIO h);
idio_unicode_t idio_getc_handle (IDIO h);
int idio_ungetc_handle (IDIO h, idio_unicode_t c);
int idio_peek_handle (IDIO h);
int idio_eofp_handle (IDIO h);
int idio_close_handle (IDIO h);
int idio_putb_handle (IDIO h, uint8_t c);
int idio_putc_handle (IDIO h, idio_unicode_t c);
ptrdiff_t idio_puts_handle (IDIO h, char *s, size_t slen);
int idio_flush_handle (IDIO h);
off_t idio_seek_handle (IDIO h, off_t offset, int whence);
off_t idio_handle_tell (IDIO h);
void idio_print_handle (IDIO h, IDIO o);

IDIO idio_handle_or_current (IDIO h, unsigned mode);
IDIO idio_close_input_handle (IDIO h);
IDIO idio_close_output_handle (IDIO h);
IDIO idio_thread_current_input_handle ();
IDIO idio_thread_current_output_handle ();

IDIO idio_open_handle (IDIO pathname, char *mode);
IDIO idio_write (IDIO o, IDIO args);
IDIO idio_write_char (IDIO c, IDIO args);
IDIO idio_display (IDIO o, IDIO args);
IDIO idio_display_C_len (char *s, size_t blen, IDIO h);
IDIO idio_display_C (char *s, IDIO h);
IDIO idio_handle_location (IDIO h);

IDIO idio_load_handle (IDIO h, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs);
IDIO idio_load_handle_interactive (IDIO fh, IDIO (*reader) (IDIO h), IDIO (*evaluator) (IDIO e, IDIO cs), IDIO cs);

void idio_init_handle ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
