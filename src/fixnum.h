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
 * fixnum.h
 * 
 */

#ifndef FIXNUM_H
#define FIXNUM_H

IDIO idio_integer (intmax_t i);
IDIO idio_fixnum (intmax_t i);
IDIO idio_fixnum_C (char *str, int base);
int idio_isa_integer (IDIO o);
IDIO idio_fixnum_primitive_add (IDIO args);
IDIO idio_fixnum_primitive_subtract (IDIO args);
IDIO idio_fixnum_primitive_multiply (IDIO args);
IDIO idio_fixnum_primitive_divide (IDIO args);
IDIO idio_fixnum_primitive_le (IDIO args);
IDIO idio_fixnum_primitive_lt (IDIO args);
IDIO idio_fixnum_primitive_eq (IDIO args);
IDIO idio_fixnum_primitive_ge (IDIO args);
IDIO idio_fixnum_primitive_gt (IDIO args);
IDIO idio_fixnum_primitive_floor (IDIO a);
IDIO idio_fixnum_primitive_quotient (IDIO a, IDIO b);
IDIO idio_fixnum_primitive_remainder (IDIO a, IDIO b);

IDIO idio_defprimitive_add ();
IDIO idio_defprimitive_subtract ();
IDIO idio_defprimitive_multiply ();
IDIO idio_defprimitive_divide ();
IDIO idio_defprimitive_lt ();
IDIO idio_defprimitive_le ();
IDIO idio_defprimitive_eq ();
IDIO idio_defprimitive_ge ();
IDIO idio_defprimitive_gt ();
IDIO idio_defprimitive_remainder ();

void idio_init_fixnum ();
void idio_fixnum_add_primitives ();
void idio_final_fixnum ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
