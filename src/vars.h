/*
 * Copyright (c) 2021-2022 Ian Fitchet <idf(at)idio-lang.org>
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

extern IDIO idio_S_IFS;
extern IDIO idio_S_suppress_exit_on_error;
extern IDIO idio_S_suppress_pipefail;
extern IDIO idio_S_suppress_async_command_report;

void idio_init_vars ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
