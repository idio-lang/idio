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
 * string-handle.h
 *
 */

#ifndef STRING_HANDLE_H
#define STRING_HANDLE_H

IDIO idio_reopen_input_string_handle_C (IDIO sh, char *str);
IDIO idio_open_input_string_handle_C (char *str);
IDIO idio_open_output_string_handle_C ();
void idio_free_string_handle (IDIO sh);
int idio_readyp_string_handle (IDIO sh);
int idio_getc_string_handle (IDIO sh);
int idio_eofp_string_handle (IDIO sh);
int idio_close_string_handle (IDIO sh);
int idio_putc_string_handle (IDIO sh, int c);
ptrdiff_t idio_puts_string_handle (IDIO sh, char *s, size_t slen);
int idio_flush_string_handle (IDIO sh);
off_t idio_seek_string_handle (IDIO sh, off_t offset, int whence);
void idio_print_string_handle (IDIO sh, IDIO o);

int idio_isa_string_handle (IDIO o);
IDIO idio_get_output_string (IDIO sh);

void idio_init_string_handle ();
void idio_string_handle_add_primitives ();
void idio_final_string_handle ();


#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
