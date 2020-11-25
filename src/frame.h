/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * frame.h
 *
 */

#ifndef FRAME_H
#define FRAME_H

extern IDIO idio_G_frame;

IDIO idio_frame_allocate (idio_ai_t nargs);
IDIO idio_frame (IDIO next, IDIO args);
int idio_isa_frame (IDIO fo);
void idio_free_frame (IDIO fo);

IDIO idio_frame_fetch (IDIO fo, size_t d, size_t i);
void idio_frame_update (IDIO fo, size_t d, size_t i, IDIO v);
IDIO idio_frame_extend (IDIO f1, IDIO f2);

IDIO idio_frame_args_as_list_from (IDIO frame, idio_ai_t from);
IDIO idio_frame_args_as_list (IDIO frame);
IDIO idio_frame_params_as_list (IDIO frame);

void idio_init_frame (void);

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
