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
 * libc-wrap.h
 *
 */

#ifndef LIBC_WRAP_H
#define LIBC_WRAP_H

extern IDIO idio_libc_wrap_module;
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

#define IDIO_LIBC_SIGNAL_NAME_ONLY(n,i)						\
    sig_sym = idio_symbols_C_intern (#n);					\
    idio_libc_export_symbol_value (sig_sym, idio_C_int (i));			\
    sprintf (idio_libc_signal_names[i], "%s", IDIO_SYMBOL_S (sig_sym) + 3);	\

#define IDIO_LIBC_SIGNAL_CONDITION_ONLY(n,i)										\
    sig_cond = idio_struct_instance (idio_condition_rt_signal_type, IDIO_LIST1 (IDIO_FIXNUM (n)));	\
    idio_array_insert_index (idio_vm_signal_handler_conditions, sig_cond, n);		\

/*
 * We create a signal-specific condition type, ^rt-signal-SIGxxx, for
 * each signum then build a single instance of it.  That condition
 * carries no other information than the signal number so the
 * condition can be persistent and re-used.
 */

#define IDIO_LIBC_SIGNAL(n) {										\
    sig_sym = idio_symbols_C_intern (#n);								\
    idio_libc_export_symbol_value (sig_sym, idio_C_int (n));						\
    sprintf (idio_libc_signal_names[n], "%s", IDIO_SYMBOL_S (sig_sym) + 3);				\
    IDIO sig_ct;											\
    IDIO_DEFINE_CONDITION0_DYNAMIC (sig_ct, "^rt-signal-" #n, idio_condition_rt_signal_type);		\
    sig_cond = idio_struct_instance (sig_ct, IDIO_LIST1 (idio_C_int (n)));				\
    idio_array_insert_index (idio_vm_signal_handler_conditions, sig_cond, n);				\
    }

#define IDIO_LIBC_SIGNAL_(n) IDIO_LIBC_SIGNAL_NAME_CONDITION(n,n)

extern IDIO idio_vm_signal_handler_conditions;
extern char **idio_libc_signal_names;
char *idio_libc_signal_name (int signum);
extern char **idio_libc_errno_names;
char *idio_libc_errno_name (int errnum);
extern char **idio_libc_rlimit_names;
char *idio_libc_rlimit_name (int errnum);

void idio_init_libc_wrap ();
void idio_libc_wrap_add_primitives ();
void idio_final_libc_wrap ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
