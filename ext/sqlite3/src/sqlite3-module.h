/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * sqlite3-module.h
 *
 */

#ifndef SQLITE3_MODULE_H
#define SQLITE3_MODULE_H

/*
 * Indexes into structures for direct references
 */
typedef enum {
    IDIO_SQLITE3_DB_ST_NAME,
    IDIO_SQLITE3_DB_ST_DB,
    IDIO_SQLITE3_DB_ST_STMTS,
    IDIO_SQLITE3_DB_ST_SIZE,
} idio_sqlite3_db_st_enum;

extern IDIO idio_sqlite3_module;

extern IDIO idio_condition_rt_sqlite3_error_type;

void idio_sqlite3_db_finalizer (IDIO C_p);
void idio_sqlite3_stmt_finalizer (IDIO C_p);

void idio_init_sqlite3 ();

#endif
