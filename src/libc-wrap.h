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

/*
 * XXX I can't figure out the macro expansion rules where macros might
 * embed calls to other macros.  I think it's something to do with
 * pre-scanning and stringization.  Or something else.
 *
 * The result is ^rt-signal-3 rather than ^rt-signal-SIGINT.
 *
 * So these macros are functional duplicates (bad coder!) until
 * someone who knows what they're doing figures it out.
 */

/*
 * IDIO_LIBC_SIGNAL_NAME_ONLY(n,i) exists because there are some
 * signal numbers < IDIO_LIBC_NSIG but which have no (known) C macro
 * name.
 *
 * To keep you on your toes, some of them *do* have entries for
 * strsignal(3).  They are printed out on startup by a debug build.
 *
 * We can handle those by inventing a name (SIGJUNKnn) but of course
 * we can't use that name as a C macro value, we only have an integer,
 * i.
 *
 * Having special-cased those we can reuse the code in
 * IDIO_LIBC_SIGNAL_NAME(n) and IDIO_LIBC_SIGNAL(n) for known signal
 * names.  Or would do if I understood C macro expansion.
 */
#define IDIO_LIBC_SIGNAL_NAME_ONLY(n,i) {				\
	IDIO sig_sym = idio_symbols_C_intern (n);			\
	idio_libc_export_symbol_value (sig_sym, idio_C_int (i));	\
	sprintf (idio_libc_signal_names[i], "%s", #n); \
    }

/*
 * IDIO_LIBC_SIGNAL_NAME(n) really exists because we have two signals,
 * SIGKILL and SIGSTOP, which cannot be caught.  So there's no point
 * in setting an action for them (and attempting to do so will result
 * in EINVAL) nor a condition (as we can't catch one to raise a
 * condition!).
 */
#define IDIO_LIBC_SIGNAL_NAME(n) IDIO_LIBC_SIGNAL_NAME_ONLY(#n,n)

/*
 * We create a signal-specific condition type, ^rt-signal-SIGxxx, for
 * each signum then build a single instance of it.  That condition
 * carries no other information than the signal number so the
 * condition can be persistent and re-used.
 */

#define IDIO_LIBC_SIGNAL_NAME_AND_CONDITION(n) {			\
	IDIO sig_sym = idio_symbols_C_intern (#n);			\
	idio_libc_export_symbol_value (sig_sym, idio_C_int (i));	\
	sprintf (idio_libc_signal_names[n], "%s", #n); \
	IDIO sig_ct;							\
	IDIO_DEFINE_CONDITION0_DYNAMIC (sig_ct, "^rt-signal-" #n, idio_condition_rt_signal_type); \
	IDIO sig_cond = idio_struct_instance (sig_ct, IDIO_LIST1 (idio_C_int (n))); \
	idio_array_insert_index (idio_vm_signal_handler_conditions, sig_cond, n); \
    }

#define IDIO_LIBC_SIGNAL(n) {						\
	IDIO sig_sym = idio_symbols_C_intern (#n);			\
	idio_libc_export_symbol_value (sig_sym, idio_C_int (i));	\
	sprintf (idio_libc_signal_names[n], "%s", #n); \
	IDIO sig_ct;							\
	IDIO_DEFINE_CONDITION0_DYNAMIC (sig_ct, "^rt-signal-" #n, idio_condition_rt_signal_type); \
	IDIO sig_cond = idio_struct_instance (sig_ct, IDIO_LIST1 (idio_C_int (n))); \
	idio_array_insert_index (idio_vm_signal_handler_conditions, sig_cond, n); \
    }

#define IDIO_LIBC_ERRNO(n) {						\
	IDIO err_sym = idio_symbols_C_intern (#n);			\
	idio_libc_export_symbol_value (err_sym, idio_C_int (n));	\
	sprintf (idio_libc_errno_names[n], "%s", IDIO_SYMBOL_S (err_sym)); \
	IDIO err_ct;							\
	IDIO_DEFINE_CONDITION0_DYNAMIC (err_ct, "^system-error-" #n, idio_condition_system_error_type);	\
	IDIO err_cond = idio_struct_instance (err_ct, IDIO_LIST4 (idio_S_nil, idio_S_nil, idio_S_nil, idio_C_int (n))); \
	idio_array_insert_index (idio_vm_errno_conditions, err_cond, n); \
    }

#define IDIO_LIBC_RLIMIT(n) {						\
	IDIO rlimit_sym = idio_symbols_C_intern (#n);			\
	idio_libc_export_symbol_value (rlimit_sym, idio_C_int (n));	\
	sprintf (idio_libc_rlimit_names[n], "%s", IDIO_SYMBOL_S (rlimit_sym)); \
    }

extern IDIO idio_vm_signal_handler_conditions;
extern char **idio_libc_signal_names;
char *idio_libc_signal_name (int signum);
extern IDIO idio_vm_errno_conditions;
extern char **idio_libc_errno_names;
char *idio_libc_errno_name (int errnum);
extern char **idio_libc_rlimit_names;
char *idio_libc_rlimit_name (int errnum);

void idio_init_libc_wrap ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
