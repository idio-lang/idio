/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * util.h
 *
 */

#ifndef UTIL_H
#define UTIL_H

typedef enum {
    IDIO_EQUAL_EQP,
    IDIO_EQUAL_EQVP,
    IDIO_EQUAL_EQUALP,
} idio_equal_enum;

#define IDIO_COPY_DEEP		1
#define IDIO_COPY_SHALLOW	2

#define IDIO_PRINT_CONVERSION_FORMAT_X		0x58
#define IDIO_PRINT_CONVERSION_FORMAT_b		0x62
#define IDIO_PRINT_CONVERSION_FORMAT_c		0x63
#define IDIO_PRINT_CONVERSION_FORMAT_d		0x64
#define IDIO_PRINT_CONVERSION_FORMAT_e		0x65
#define IDIO_PRINT_CONVERSION_FORMAT_f		0x66
#define IDIO_PRINT_CONVERSION_FORMAT_g		0x67
#define IDIO_PRINT_CONVERSION_FORMAT_o		0x6F
#define IDIO_PRINT_CONVERSION_FORMAT_p		0x70
#define IDIO_PRINT_CONVERSION_FORMAT_u		0x75
#define IDIO_PRINT_CONVERSION_FORMAT_x		0x78

#define IDIO_PRINT_CONVERSION_FORMAT_s		0x73

extern IDIO idio_S_idio_print_conversion_format;
extern IDIO idio_S_idio_print_conversion_precision;

int idio_type (IDIO o);
char const *idio_type2string (IDIO o);
char const *idio_type_enum2string (idio_type_e type);
IDIO idio_util_method_typename (idio_vtable_method_t *m, IDIO v, ...);
IDIO idio_util_method_members (idio_vtable_method_t *m, IDIO v, ...);
int idio_isa_boolean (IDIO o);
int idio_eqp (void const *o1, void const *o2);
int idio_eqvp (void const *o1, void const *o2);
int idio_equalp (void const *o1, void const *o2);
int idio_equal (IDIO o1, IDIO o2, idio_equal_enum eqp);
IDIO idio_value (IDIO o);
idio_unicode_t idio_util_string_format (idio_unicode_t format);
char *idio_as_string (IDIO o, size_t *sizep, int depth, IDIO seen, int first);
char *idio_as_string_safe (IDIO o, size_t *sizep, int depth, int first);
IDIO idio_util_method_2string (idio_vtable_method_t *m, IDIO v, ...);
char *idio_display_string (IDIO o, size_t *sizep);
char *idio_report_string (IDIO o, size_t *sizep, int depth, IDIO seen, int first);
char const *idio_vm_bytecode2string (int code);
IDIO idio_util_method_value_index (idio_vtable_method_t *m, IDIO v, ...);
IDIO idio_util_method_set_value_index (idio_vtable_method_t *m, IDIO v, ...);
void idio_as_flat_string (IDIO o, char **argv, int *i);
IDIO idio_copy (IDIO o, int depth);
void idio_dump (IDIO o, int detail);
void idio_debug_FILE (FILE *file, char const *fmt, IDIO o);
void idio_debug (char const *fmt, IDIO o);
IDIO idio_add_feature (IDIO f);
IDIO idio_add_feature_ps (char const *p, size_t plen, char const *s, size_t slen);
IDIO idio_add_feature_pi (char const *p, size_t plen, size_t size);
IDIO idio_add_module_feature (IDIO m, IDIO f);

#if ! defined (IDIO_HAVE_STRNLEN)
size_t strnlen (char const *s, size_t maxlen);
#endif

#if ! defined (IDIO_HAVE_MEMRCHR)
void *memrchr (void const *s, int c, size_t n);
#endif

#if ! defined (IDIO_HAVE_CLOCK_GETTIME)
#ifdef __APPLE__
typedef enum {
    CLOCK_REALTIME,
    CLOCK_MONOTONIC,
    CLOCK_PROCESS_CPUTIME_ID,
    CLOCK_THREAD_CPUTIME_ID
} clockid_t;

int clock_gettime (clockid_t clk_id, struct timespec *tp);
#endif
#endif

size_t idio_strnlen (char const *s, size_t maxlen);
int idio_snprintf (char *str, size_t size, char const *format, ...);

char *idio_constant_idio_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
IDIO idio_util_method_run0 (idio_vtable_method_t *m, IDIO v, ...);
IDIO idio_util_method_run (idio_vtable_method_t *m, IDIO v, ...);

void idio_init_util ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
