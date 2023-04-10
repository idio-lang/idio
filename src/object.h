/*
 * Copyright (c) 2022-2023 Ian Fitchet <idf(at)idio-lang.org>
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
 * object.h
 *
 */

#ifndef OBJECT_H
#define OBJECT_H

extern IDIO idio_object_module;
extern IDIO idio_class_struct_type;

/*
 * Indexes into structures for direct references
 */
typedef enum {
    IDIO_CLASS_ST_CLASS,
    IDIO_CLASS_ST_PROC,
    /* actual class/instance slots follow on in the struct-instance */

    IDIO_CLASS_ST_MAX,		/* used to initialise the following enums */
} idio_class_st_enum;

typedef enum {
    IDIO_CLASS_SLOT_NAME = IDIO_CLASS_ST_MAX,
    IDIO_CLASS_SLOT_DIRECT_SUPERS,
    IDIO_CLASS_SLOT_DIRECT_SLOTS,
    IDIO_CLASS_SLOT_CPL,
    IDIO_CLASS_SLOT_SLOTS,
    IDIO_CLASS_SLOT_NFIELDS,
    IDIO_CLASS_SLOT_GETTERS_N_SETTERS,	/* slot descriptions: (... (name init-function getter) ...) */

    IDIO_CLASS_SLOT_MAX,
} idio_class_slot_enum;

typedef enum {
    IDIO_GENERIC_SLOT_NAME = IDIO_CLASS_ST_MAX,
    IDIO_GENERIC_SLOT_DOCUMENTATION,
    IDIO_GENERIC_SLOT_METHODS,

    IDIO_GENERIC_SLOT_MAX,
} idio_generic_slot_enum;

typedef enum {
    IDIO_METHOD_SLOT_GENERIC_FUNCTION = IDIO_CLASS_ST_MAX,
    IDIO_METHOD_SLOT_SPECIALIZERS,
    IDIO_METHOD_SLOT_PROCEDURE,

    IDIO_METHOD_SLOT_MAX,
} idio_method_slot_enum;

int idio_isa_instance (IDIO o);
static int idio_isa_class (IDIO o);
int idio_isa_generic (IDIO o);
IDIO idio_object_class_of (IDIO o);
IDIO idio_object_slot_ref (IDIO obj, IDIO slot_name);
IDIO idio_object_slot_set (IDIO obj, IDIO slot, IDIO val);

char *idio_instance_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
char *idio_instance_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
IDIO idio_instance_method_2string (idio_vtable_method_t *m, IDIO v, ...);
char *idio_class_struct_type_report_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);
char *idio_class_struct_type_as_C_string (IDIO v, size_t *sizep, idio_unicode_t format, IDIO seen, int depth);

void idio_init_object ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
