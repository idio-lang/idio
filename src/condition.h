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
 * condition.h
 * 
 */

#ifndef CONDITION_H
#define CONDITION_H

extern IDIO idio_condition_root_type;
extern IDIO idio_condition_message_type;
extern IDIO idio_condition_error_type;
extern IDIO idio_condition_io_error_type;
extern IDIO idio_condition_io_handle_error_type;
extern IDIO idio_condition_io_read_error_type;
extern IDIO idio_condition_io_write_error_type;
extern IDIO idio_condition_io_closed_error_type;
extern IDIO idio_condition_io_filename_error_type;
extern IDIO idio_condition_io_malformed_filename_error_type;
extern IDIO idio_condition_io_file_protection_error_type;
extern IDIO idio_condition_io_file_is_read_only_error_type;
extern IDIO idio_condition_io_file_already_exists_error_type;
extern IDIO idio_condition_io_no_such_file_error_type;
extern IDIO idio_condition_read_error_type;
extern IDIO idio_condition_idio_error_type;
extern IDIO idio_condition_system_error_type;

int idio_isa_condition_type (IDIO o);
int idio_isa_condition (IDIO o);

IDIO idio_condition_idio_error (IDIO message, IDIO location, IDIO detail);

void idio_init_condition ();
void idio_condition_add_primitives ();
void idio_final_condition ();

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */