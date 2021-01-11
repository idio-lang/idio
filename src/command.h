/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * command.h
 *
 */

#ifndef COMMAND_H
#define COMMAND_H

extern IDIO idio_command_module;

void idio_command_not_found_error (char *msg, IDIO cmd, IDIO c_location);

char **idio_command_get_envp ();
char *idio_command_find_exe_C (char *command);
char *idio_command_find_exe (IDIO func);
IDIO idio_command_invoke (IDIO func, IDIO thr, char *pathname);

void idio_init_command ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
