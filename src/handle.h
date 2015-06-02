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
 * handle.h
 * 
 */

#ifndef HANDLE_H
#define HANDLE_H

IDIO idio_handle_error_read (IDIO h);
IDIO idio_handle_error_read_C (char *name);
IDIO idio_handle_error_write (IDIO h);
IDIO idio_handle_error_write_C (char *name);
IDIO idio_handle_error_closed (IDIO h);
IDIO idio_handle_error_closed_C (char *name);

IDIO idio_handle ();
int idio_isa_handle (IDIO d);
void idio_free_handle (IDIO d);
void idio_handle_finalizer (IDIO handle);

void idio_handle_free (IDIO h);
int idio_handle_readyp (IDIO h);
int idio_handle_getc (IDIO h);
int idio_handle_ungetc (IDIO h, int c);
int idio_handle_peek (IDIO h);
int idio_handle_eofp (IDIO h);
int idio_handle_close (IDIO h);
int idio_handle_putc (IDIO h, int c);
size_t idio_handle_puts (IDIO h, char *s, size_t slen);
int idio_handle_flush (IDIO h);
off_t idio_handle_seek (IDIO h, off_t offset, int whence);
void idio_handle_print (IDIO h, IDIO o);

IDIO idio_handle_or_current (IDIO h, unsigned mode);
IDIO idio_close_input_handle (IDIO h);
IDIO idio_close_output_handle (IDIO h);
IDIO idio_current_input_handle ();
IDIO idio_current_output_handle ();

IDIO idio_open_handle (IDIO pathname, char *mode);
IDIO idio_write (IDIO o, IDIO args);
IDIO idio_write_char (IDIO c, IDIO args);
IDIO idio_display (IDIO o, IDIO args);
IDIO idio_display_C (char *s, IDIO h);

void idio_init_handle ();
void idio_handle_add_primitives ();
void idio_final_handle ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
