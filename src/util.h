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
 * util.h
 * 
 */

#ifndef UTIL_H
#define UTIL_H

const char *idio_type2string (IDIO o);
const char *idio_type_enum2string (idio_type_e type);
IDIO idio_type (IDIO f, IDIO o);
int idio_eqp (IDIO f, void *o1, void *o2);
int idio_eqvp (IDIO f, void *o1, void *o2);
int idio_equalp (IDIO f, void *o1, void *o2);
int idio_equal (IDIO f, IDIO o1, IDIO o2, int eqp);
IDIO idio_value (IDIO f, IDIO o);
char *idio_as_string (IDIO f, IDIO o, int depth);
char *idio_display_string (IDIO f, IDIO o);
IDIO idio_apply (IDIO f, IDIO func, IDIO args);
IDIO idio_apply1 (IDIO f, IDIO func, IDIO arg);
IDIO idio_apply2 (IDIO f, IDIO func, IDIO arg1, IDIO arg2);

/*
  XXX delete me
 */
#define idio_expr_dump(f,e)	(idio_expr_dump_ ((f), (e), (#e), 1))
#define idio_expr_dumpn(f,e,d)	(idio_expr_dump_ ((f), (e), (#e), (d)))
void idio_expr_dump_ (IDIO f, IDIO e, const char *en, int depth);

#define idio_frame_dump(f)	{idio_frame_dump_((f),(#f),1);}
#define idio_frame_dump2(f)	{idio_frame_dump_((f),(#f),2);}
#define idio_frame_trace(f)	{idio_frame_trace_((f),(#f),1);}
#define idio_frame_trace2(f)	{idio_frame_trace_((f),(#f),2);}

void idio_frame_trace_ (IDIO f, const char *sname, int detail);
void idio_dump (IDIO f, IDIO o, int detail);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
