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
 * file-handle.h
 *
 */

#ifndef FILE_HANDLE_H
#define FILE_HANDLE_H

#define IDIO_FILE_HANDLE_FLAG_NONE	  0
#define IDIO_FILE_HANDLE_FLAG_EOF	  (1<<0)
#define IDIO_FILE_HANDLE_FLAG_INTERACTIVE (1<<1)
#define IDIO_FILE_HANDLE_FLAG_STDIO	  (1<<2)
#define IDIO_FILE_HANDLE_FLAG_CLOEXEC	  (1<<3)

typedef struct idio_file_handle_stream_s {
    int fd;
    IDIO_FLAGS_T flags;		/* IDIO_FILE_HANDLE_FLAG_* */
    char *buf;			/* buffer */
    int bufsiz;
    char *ptr;			/* ptr into buffer */
    int count;			/* bytes in buffer */
} idio_file_handle_stream_t;

#define IDIO_FILE_HANDLE_STREAM_FD(S)     ((S)->fd)
#define IDIO_FILE_HANDLE_STREAM_FLAGS(S)  ((S)->flags)
#define IDIO_FILE_HANDLE_STREAM_BUF(S)    ((S)->buf)
#define IDIO_FILE_HANDLE_STREAM_BUFSIZ(S) ((S)->bufsiz)
#define IDIO_FILE_HANDLE_STREAM_PTR(S)    ((S)->ptr)
#define IDIO_FILE_HANDLE_STREAM_COUNT(S)  ((S)->count)

#define IDIO_FILE_HANDLE_FD(H)     IDIO_FILE_HANDLE_STREAM_FD((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_FLAGS(H)  IDIO_FILE_HANDLE_STREAM_FLAGS((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_BUF(H)    IDIO_FILE_HANDLE_STREAM_BUF((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_BUFSIZ(H) IDIO_FILE_HANDLE_STREAM_BUFSIZ((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_PTR(H)    IDIO_FILE_HANDLE_STREAM_PTR((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))
#define IDIO_FILE_HANDLE_COUNT(H)  IDIO_FILE_HANDLE_STREAM_COUNT((idio_file_handle_stream_t *) IDIO_HANDLE_STREAM(H))

int idio_isa_file_handle (IDIO fh);
int idio_file_handle_fd (IDIO fh);
void idio_file_handle_finalizer (IDIO fh);
void idio_gc_register_file_handle (IDIO fh);
void idio_gc_deregister_file_handle (IDIO fh);
void idio_remember_file_handle (IDIO fh);
void idio_forget_file_handle (IDIO fh);
void idio_free_file_handle (IDIO fh);
IDIO idio_open_file_handle_C (char *func, IDIO filename, char *pathname, int free_pathname, char *mode, int free_mode);
IDIO idio_stdin_file_handle ();
IDIO idio_stdout_file_handle ();
IDIO idio_stderr_file_handle ();
int idio_readyp_file_handle (IDIO fh);
int idio_getb_file_handle (IDIO fh);
int idio_eofp_file_handle (IDIO fh);
int idio_close_file_handle (IDIO fh);
int idio_putb_file_handle (IDIO fh, uint8_t c);
int idio_putc_file_handle (IDIO fh, idio_unicode_t c);
ptrdiff_t idio_puts_file_handle (IDIO fh, char *s, size_t slen);
int idio_flush_file_handle (IDIO fh);
off_t idio_seek_file_handle (IDIO fh, off_t offset, int whence);
void idio_print_file_handle (IDIO fh, IDIO o);
IDIO idio_defprimitive_open_file_handle (IDIO name, IDIO mode);
IDIO idio_load_file_name (IDIO filename, IDIO cs);
IDIO idio_defprimitive_load_file (IDIO filename);

void idio_init_file_handle ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
