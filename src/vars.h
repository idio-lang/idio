/*
 * Copyright (c) 2021 Ian Fitchet <idf(at)idio-lang.org>
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
 * vars.h
 *
 */

#ifndef VARS_H
#define VARS_H

extern IDIO idio_vars_IFS_sym;
extern IDIO idio_vars_suppress_exit_on_error_sym;
extern IDIO idio_vars_suppress_pipefail_sym;
extern IDIO idio_vars_suppress_async_command_report_sym;

void idio_init_vars ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
