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

IDIO idio_struct_type (IDIO name, IDIO parent, IDIO fields);
int idio_isa_struct_type (IDIO p);
void idio_free_struct_type (IDIO p);
IDIO idio_struct_instance (IDIO st, IDIO fields);
int idio_isa_struct_instance (IDIO p);
void idio_free_struct_instance (IDIO p);


#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
