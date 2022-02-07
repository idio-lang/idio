/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * vtable.h
 *
 */

#ifndef VTABLE_H
#define VTABLE_H

extern int idio_vtable_generation;
extern idio_vtable_t *idio_fixnum_vtable;
extern idio_vtable_t *idio_constant_idio_vtable;
extern idio_vtable_t *idio_constant_token_vtable;
extern idio_vtable_t *idio_constant_i_code_vtable;
extern idio_vtable_t *idio_constant_unicode_vtable;
extern idio_vtable_t *idio_placeholder_vtable;

void idio_vtable_method_unbound_error (IDIO v, IDIO name, IDIO c_location);

idio_vtable_method_t *idio_vtable_create_method_simple (IDIO (*func) (idio_vtable_method_t *m, IDIO v, ...));
idio_vtable_method_t *idio_vtable_create_method_static_C (IDIO (*func) (idio_vtable_method_t *m, IDIO v, ...), void *data, size_t size);
idio_vtable_method_t *idio_vtable_create_method_value (IDIO (*func) (idio_vtable_method_t *m, IDIO v, ...), IDIO value);

idio_vtable_t *idio_vtable (int type);
void idio_free_vtable (idio_vtable_t *vt);
idio_vtable_t *idio_value_vtable (IDIO o);
void idio_vtable_add_method (idio_vtable_t *vt, IDIO name, idio_vtable_method_t *m);
void idio_vtable_inherit_method (idio_vtable_t *vt, IDIO name, idio_vtable_method_t *m);
idio_vtable_method_t *idio_vtable_lookup_method (idio_vtable_t *vt, IDIO v, IDIO name, int throw);
idio_vtable_method_t *idio_vtable_flat_lookup_method (idio_vtable_t *vt, IDIO v, IDIO name, int throw);

void idio_dump_vtable (idio_vtable_t *vt);

void idio_final_vtable ();
void idio_init_vtable ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
