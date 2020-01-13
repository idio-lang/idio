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
 * keyword.h
 *
 */

#ifndef KEYWORD_H
#define KEYWORD_H

#define IDIO_KEYWORD_DECL(n)		IDIO idio_KW_ ## n
#define IDIO_KEYWORD_DEF(iname,cname)	idio_KW_ ## cname = idio_keywords_C_intern (iname);

extern IDIO_KEYWORD_DECL (docstr);
extern IDIO_KEYWORD_DECL (docstr_raw);
extern IDIO_KEYWORD_DECL (handle);
extern IDIO_KEYWORD_DECL (line);
extern IDIO_KEYWORD_DECL (name);
extern IDIO_KEYWORD_DECL (setter);
extern IDIO_KEYWORD_DECL (sigstr);
extern IDIO_KEYWORD_DECL (source);

IDIO idio_keyword_C (const char *s_C);
IDIO idio_tag_C (const char *s_C);
void idio_free_keyword (IDIO s);
int idio_isa_keyword (IDIO s);
IDIO idio_keywords_C_intern (char *s);
IDIO idio_keywords_string_intern (IDIO str);
IDIO idio_hash_make_keyword_table (IDIO args);
IDIO idio_keyword_get (IDIO ht, IDIO kw, IDIO args);
IDIO idio_keyword_set (IDIO ht, IDIO kw, IDIO v);

void idio_init_keyword (void);
void idio_keyword_add_primitives (void);
void idio_final_keyword (void);

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
