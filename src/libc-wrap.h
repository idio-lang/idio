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
 * libc-wrap.h
 * 
 */

#ifndef LIBC_WRAP_H
#define LIBC_WRAP_H

extern IDIO idio_libc_struct_stat;

#define IDIO_STRUCT_STAT_DEV		0
#define IDIO_STRUCT_STAT_INO		1
#define IDIO_STRUCT_STAT_MODE		2
#define IDIO_STRUCT_STAT_NLINK		3
#define IDIO_STRUCT_STAT_UID		4
#define IDIO_STRUCT_STAT_GID		5
#define IDIO_STRUCT_STAT_RDEV		6
#define IDIO_STRUCT_STAT_SIZE		7
#define IDIO_STRUCT_STAT_BLKSIZE	8
#define IDIO_STRUCT_STAT_BLOCKS		9
#define IDIO_STRUCT_STAT_ATIME		10
#define IDIO_STRUCT_STAT_MTIME		11
#define IDIO_STRUCT_STAT_CTIME		12

extern char **idio_libc_signal_names;
char *idio_libc_signal_name (int signum);

void idio_init_libc_wrap ();
void idio_libc_wrap_add_primitives ();
void idio_final_libc_wrap ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
