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
 * c-struct.h
 * 
 */

#ifndef C_STRUCT_H
#define C_STRUCT_H

#define IDIO_C_TYPEDEF_ADD_VALUE(f,t,v)	{				\
	IDIO sym = idio_symbols_C_intern ((f), (#t));			\
	IDIO val = idio_symbols_C_intern ((f), (#v));			\
	idio_CTD_##t = idio_C_typedefs_add_value ((f), sym, val);	\
    }

#define IDIO_C_TYPEDEF_ADD(f,t)	{					\
	IDIO sym = idio_symbols_C_intern ((f), (#t));			\
	idio_CTD_##t = idio_C_typedefs_add_value ((f), sym, idio_S_nil); \
    }
    
#define IDIO_C_SLOT_DATA_TAG       0
#define IDIO_C_SLOT_DATA_ALIGNMENT 1
#define IDIO_C_SLOT_DATA_TYPE      2
#define IDIO_C_SLOT_DATA_OFFSET    3
#define IDIO_C_SLOT_DATA_SIZE      4
#define IDIO_C_SLOT_DATA_NELEM     5
#define IDIO_C_SLOT_DATA_MAX       6

extern IDIO idio_CTD_int8;
extern IDIO idio_CTD_uint8;
extern IDIO idio_CTD_int16;
extern IDIO idio_CTD_uint16;
extern IDIO idio_CTD_int32;
extern IDIO idio_CTD_uint32;
extern IDIO idio_CTD_int64;
extern IDIO idio_CTD_uint64;
extern IDIO idio_CTD_float;
extern IDIO idio_CTD_double;
extern IDIO idio_CTD_asterisk;	/* pointer */
extern IDIO idio_CTD_string;	/* C string */

extern IDIO idio_CTD_short;
extern IDIO idio_CTD_ushort;
extern IDIO idio_CTD_char;
extern IDIO idio_CTD_uchar;
extern IDIO idio_CTD_int;
extern IDIO idio_CTD_uint;
extern IDIO idio_CTD_long;
extern IDIO idio_CTD_ulong;

void idio_init_C_struct ();
IDIO idio_C_typedef_C (IDIO f, const char *s_C);
int idio_isa_C_typedef (IDIO f, IDIO o);
void idio_free_C_typedef (IDIO f, IDIO o);
IDIO idio_C_typedefs_exists (IDIO f, IDIO s);
IDIO idio_C_typedefs_get (IDIO f, IDIO s);
IDIO idio_C_typedefs_add_value (IDIO f, IDIO s, IDIO v);
IDIO idio_C_typedefs_add (IDIO f, IDIO s);
IDIO idio_C_slots_array (IDIO f, IDIO slots);
IDIO idio_C_struct (IDIO f, IDIO slots_array, IDIO methods, IDIO frame);
void idio_free_C_struct (IDIO f, IDIO s);
IDIO idio_C_instance (IDIO f, IDIO c_struct, IDIO frame);
void idio_free_C_instance (IDIO f, IDIO s);
IDIO idio_opaque (IDIO f, void *opaque);
int idio_isa_opaque (IDIO f, IDIO o);
void idio_free_opaque (IDIO f, IDIO o);
IDIO idio_opaque_args (IDIO f, void *p, IDIO args);
IDIO idio_opaque_final (IDIO f, void *p, void (*func) (IDIO o), IDIO args);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
