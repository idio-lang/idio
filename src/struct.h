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
 * struct.h
 * 
 */

#ifndef STRUCT_H
#define STRUCT_H

IDIO idio_struct_type (IDIO name, IDIO parent, IDIO slots);
int idio_isa_struct_type (IDIO p);
void idio_free_struct_type (IDIO p);
IDIO idio_defprimitive_make_struct_type (IDIO name, IDIO parent, IDIO slots);
IDIO idio_defprimitive_struct_typep (IDIO st);
IDIO idio_defprimitive_struct_type_name (IDIO st);
IDIO idio_defprimitive_struct_type_parent (IDIO st);
IDIO idio_defprimitive_struct_type_slots (IDIO st);
IDIO idio_struct_instance (IDIO st, IDIO slots);
int idio_isa_struct_instance (IDIO p);
void idio_free_struct_instance (IDIO p);
IDIO idio_defprimitive_make_struct (IDIO st, IDIO slots);
IDIO idio_defprimitive_struct_instancep (IDIO si);
IDIO idio_defprimitive_struct_instance_type (IDIO si);
IDIO idio_defprimitive_struct_instance_slots (IDIO si);
IDIO idio_defprimitive_struct_instance_ref (IDIO si, IDIO slot);
IDIO idio_defprimitive_struct_instance_set (IDIO si, IDIO slot, IDIO v);
IDIO idio_defprimitive_struct_instance_isa (IDIO si, IDIO st);


#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
