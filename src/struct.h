/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * struct.h
 *
 */

#ifndef STRUCT_H
#define STRUCT_H

#define IDIO_DEFINE_STRUCT0(v,n,p) {			\
	IDIO sym = idio_symbols_C_intern (n);		\
	v = idio_struct_type (sym, p, idio_S_nil);	\
	idio_gc_protect (v);				\
	idio_module_toplevel_set_symbol_value (sym, v);	\
    }

#define IDIO_DEFINE_STRUCT1(v,n,p,f1) {					\
	IDIO sym = idio_symbols_C_intern (n);				\
	v = idio_struct_type (sym, p, IDIO_LIST1 (idio_symbols_C_intern (f1)));	\
	idio_gc_protect (v);						\
	idio_module_toplevel_set_symbol_value (sym, v);			\
    }

#define IDIO_DEFINE_STRUCT2(v,n,p,f1,f2) {				\
	IDIO sym = idio_symbols_C_intern (n);				\
	v = idio_struct_type (sym, p, IDIO_LIST2 (idio_symbols_C_intern (f1), idio_symbols_C_intern (f2))); \
	idio_gc_protect (v);						\
	idio_module_toplevel_set_symbol_value (sym, v);			\
    }

#define IDIO_DEFINE_STRUCT3(v,n,p,f1,f2,f3) {				\
	IDIO sym = idio_symbols_C_intern (n);				\
	v = idio_struct_type (sym, p, IDIO_LIST3 (idio_symbols_C_intern (f1), idio_symbols_C_intern (f2), idio_symbols_C_intern (f3))); \
	idio_gc_protect (v);						\
	idio_module_toplevel_set_symbol_value (sym, v);			\
    }

    /*
     * The following is the IDIO_DEFINE_STRUCTn convention for but a
     * large n.
     *
     * field_names[] must be a NULL-terminated array of char* in
     * scope.
     *
     * We want to walk backwards over field_names[] to generate fields
     * in order -- to avoid an unecessary call to idio_list_reverse().
     * So go to the end first.
     */
#define IDIO_DEFINE_MODULE_STRUCTn(m,v,n,p) {				\
	IDIO sym = idio_symbols_C_intern (n);				\
	ptrdiff_t i;							\
	for (i = 0; NULL != field_names[i]; i++) {			\
	}								\
	IDIO fields = idio_S_nil;					\
	for (i-- ; i >= 0; i--) {					\
	    IDIO field = idio_symbols_C_intern (field_names[i]);	\
	    fields = idio_pair (field, fields);				\
	}								\
	v = idio_struct_type (sym, p, fields);				\
	idio_gc_protect (v);						\
	idio_module_set_symbol_value (sym, v, m);			\
    }

#define IDIO_DEFINE_STRUCTn(v,n,p)	IDIO_DEFINE_MODULE_STRUCTn(idio_Idio_module_instance(),v,n,p)

IDIO idio_struct_type (IDIO name, IDIO parent, IDIO fields);
int idio_isa_struct_type (IDIO p);
int idio_struct_type_isa (IDIO st, IDIO type);
void idio_free_struct_type (IDIO p);
IDIO idio_allocate_struct_instance (IDIO st, int fill);
IDIO idio_struct_instance (IDIO st, IDIO fields);
int idio_isa_struct_instance (IDIO p);
int idio_struct_instance_isa (IDIO si, IDIO st);
void idio_free_struct_instance (IDIO p);

IDIO idio_struct_instance_ref (IDIO si, IDIO field);
IDIO idio_struct_instance_ref_direct (IDIO si, idio_ai_t index);
IDIO idio_struct_instance_set (IDIO si, IDIO field, IDIO v);
IDIO idio_struct_instance_set_direct (IDIO si, idio_ai_t index, IDIO v);

void idio_init_struct ();
void idio_struct_add_primitives ();
void idio_final_struct ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
