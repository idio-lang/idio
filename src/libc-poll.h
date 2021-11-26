/*
 * Copyright (c) 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * libc-poll.h
 *
 */

#ifndef LIBC_POLL_H
#define LIBC_POLL_H

#define IDIO_LIBC_POLL(n) {						\
	IDIO poll_sym = idio_symbols_C_intern (#n, sizeof (#n) - 1);	\
	IDIO C_int = idio_C_int (n);					\
	idio_libc_export_symbol_value (poll_sym, C_int);		\
	idio_hash_put (idio_libc_poll_names, C_int, poll_sym);		\
    }

#define IDIO_LIBC_POLL_FLAG_NONE	0

typedef struct idio_libc_poller_s {
    IDIO fd_map;		/* hash table */
    int valid;			/* struct pollfd array is OK */
    nfds_t nfds;
    struct pollfd *fds;
    int in_use;			/* from Python, selectmodule.c */
} idio_libc_poller_t;

extern IDIO idio_libc_poll_names;

void idio_libc_poll_register (idio_libc_poller_t *poller, IDIO pollee);
void idio_libc_poll_deregister (idio_libc_poller_t *poller, IDIO fdh);
void idio_libc_poll_finalizer (IDIO poller);
IDIO idio_libc_poll_name (IDIO pollevent);

void idio_init_libc_poll ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
