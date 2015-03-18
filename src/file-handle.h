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
 * file-handle.h
 * 
 */

#ifndef FILE_HANDLE_H
#define FILE_HANDLE_H

void idio_init_file_handle ();
void idio_final_file_handle ();
IDIO idio_open_file_handle_C (IDIO f, char *name, char *mode);
int idio_isa_file_handle (IDIO f, IDIO fh);
void idio_file_handle_finalizer (IDIO fh);
void idio_register_file_handle (IDIO f, IDIO fh);
void idio_deregister_file_handle (IDIO f, IDIO fh);
int idio_file_handle_readyp (IDIO f, IDIO fh);
int idio_file_handle_getc (IDIO f, IDIO fh);
int idio_file_handle_eofp (IDIO f, IDIO fh);
int idio_file_handle_close (IDIO f, IDIO fh);
int idio_file_handle_putc (IDIO f, IDIO fh, int c);
int idio_file_handle_puts (IDIO f, IDIO fh, char *s, size_t l);
int idio_file_handle_flush (IDIO f, IDIO fh);
off_t idio_file_handle_seek (IDIO f, IDIO fh, off_t offset, int whence);
void idio_file_handle_print (IDIO f, IDIO fh, IDIO o);
IDIO idio_defprimitive_open_file_handle (IDIO f, IDIO name, IDIO mode);
IDIO idio_load_file (IDIO f, IDIO fh);
IDIO idio_defprimitive_load_file (IDIO f, IDIO filename);


#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
