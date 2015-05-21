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
 * string-handle.h
 * 
 */

#ifndef STRING_HANDLE_H
#define STRING_HANDLE_H

IDIO idio_open_input_string_handle_C (char *str);
IDIO idio_open_output_string_handle_C ();
void idio_string_handle_free (IDIO sh);
int idio_string_handle_readyp (IDIO sh);
int idio_string_handle_getc (IDIO sh);
int idio_string_handle_eofp (IDIO sh);
int idio_string_handle_close (IDIO sh);
int idio_string_handle_putc (IDIO sh, int c);
size_t idio_string_handle_puts (IDIO sh, char *s, size_t slen);
int idio_string_handle_flush (IDIO sh);
off_t idio_string_handle_seek (IDIO sh, off_t offset, int whence);
void idio_string_handle_print (IDIO sh, IDIO o);

IDIO idio_get_output_string (IDIO sh);

void idio_init_string_handle ();
void idio_string_handle_add_primitives ();
void idio_final_string_handle ();


#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
