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

int idio_isa_file_handle (IDIO fh);
void idio_file_handle_finalizer (IDIO fh);
void idio_gc_register_file_handle (IDIO fh);
void idio_gc_deregister_file_handle (IDIO fh);
void idio_file_handle_free (IDIO fh);
IDIO idio_open_file_handle_C (char *name, char *mode);
IDIO idio_stdin_file_handle ();
IDIO idio_stdout_file_handle ();
IDIO idio_stderr_file_handle ();
int idio_file_handle_readyp (IDIO fh);
int idio_file_handle_getc (IDIO fh);
int idio_file_handle_eofp (IDIO fh);
int idio_file_handle_close (IDIO fh);
int idio_file_handle_putc (IDIO fh, int c);
size_t idio_file_handle_puts (IDIO fh, char *s, size_t slen);
int idio_file_handle_flush (IDIO fh);
off_t idio_file_handle_seek (IDIO fh, off_t offset, int whence);
void idio_file_handle_print (IDIO fh, IDIO o);
IDIO idio_defprimitive_open_file_handle (IDIO name, IDIO mode);
IDIO idio_load_filehandle (IDIO fh, IDIO (*reader) (IDIO fh), IDIO (*evaluator) (IDIO fh));
IDIO idio_load_file (IDIO filename);
IDIO idio_defprimitive_load_file (IDIO filename);

void idio_init_file_handle ();
void idio_file_handle_add_primitives ();
void idio_final_file_handle ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
