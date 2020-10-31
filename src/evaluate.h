/*
 * Copyright (c) 2015, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * evaluate.h
 *
 */

#ifndef EVALUATE_H
#define EVALUATE_H

#define IDIO_MEANING_FLAG_NONE			0
#define IDIO_MEANING_FLAG_TAILP			(1<<0)
#define IDIO_MEANING_FLAG_TOPLEVEL_SCOPE	(1<<1)
#define IDIO_MEANING_FLAG_DYNAMIC_SCOPE		(1<<2)
#define IDIO_MEANING_FLAG_ENVIRON_SCOPE		(1<<3)
#define IDIO_MEANING_FLAG_COMPUTED_SCOPE	(1<<4)
#define IDIO_MEANING_FLAG_DEFINE		(1<<5)
#define IDIO_MEANING_FLAG_FRAME_REUSE		(1<<6)

#define IDIO_MEANING_IS_TAILP(f)		((f) & IDIO_MEANING_FLAG_TAILP)
#define IDIO_MEANING_SET_TAILP(f)		((f) | IDIO_MEANING_FLAG_TAILP)
#define IDIO_MEANING_NOT_TAILP(f)		((f) & (~ IDIO_MEANING_FLAG_TAILP))

#define IDIO_MEANING_SCOPE_MASK			(IDIO_MEANING_FLAG_TOPLEVEL_SCOPE | \
						 IDIO_MEANING_FLAG_DYNAMIC_SCOPE | \
						 IDIO_MEANING_FLAG_ENVIRON_SCOPE | \
						 IDIO_MEANING_FLAG_COMPUTED_SCOPE)

#define IDIO_MEANING_SCOPE(f)			((f) & IDIO_MEANING_SCOPE_MASK)
#define IDIO_MEANING_TOPLEVEL_SCOPE(f)		(((f) & (~ IDIO_MEANING_SCOPE_MASK)) | IDIO_MEANING_FLAG_TOPLEVEL_SCOPE)
#define IDIO_MEANING_DYNAMIC_SCOPE(f)		(((f) & (~ IDIO_MEANING_SCOPE_MASK)) | IDIO_MEANING_FLAG_DYNAMIC_SCOPE)
#define IDIO_MEANING_ENVIRON_SCOPE(f)		(((f) & (~ IDIO_MEANING_SCOPE_MASK)) | IDIO_MEANING_FLAG_ENVIRON_SCOPE)
#define IDIO_MEANING_COMPUTED_SCOPE(f)		(((f) & (~ IDIO_MEANING_SCOPE_MASK)) | IDIO_MEANING_FLAG_COMPUTED_SCOPE)

#define IDIO_MEANING_IS_DEFINE(f)		((f) & IDIO_MEANING_FLAG_DEFINE)
#define IDIO_MEANING_DEFINE(f)			((f) | IDIO_MEANING_FLAG_DEFINE)
#define IDIO_MEANING_NO_DEFINE(f)		((f) & (~ IDIO_MEANING_FLAG_DEFINE))

#define IDIO_MEANING_IS_FRAME_REUSE(f)		((f) & IDIO_MEANING_FLAG_FRAME_REUSE)
#define IDIO_MEANING_FRAME_REUSE(f)		((f) | IDIO_MEANING_FLAG_FRAME_REUSE)
#define IDIO_MEANING_NO_FRAME_REUSE(f)		((f) & (~ IDIO_MEANING_FLAG_FRAME_REUSE))

extern IDIO idio_evaluation_module;

void idio_meaning_dump_src_properties (const char *prefix, const char*name, IDIO e);
void idio_meaning_error_param_type (IDIO src, IDIO c_location, char *msg, IDIO expr);
void idio_meaning_error_static_redefine (IDIO lo, IDIO c_location, char *msg, IDIO name, IDIO cv, IDIO new);
void idio_meaning_error_static_arity (IDIO lo, IDIO c_location, char *msg, IDIO args);

void idio_meaning_add_description (IDIO sym, IDIO desc);
IDIO idio_meaning_get_description (IDIO sym);
IDIO idio_add_module_primitive (IDIO module, idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__);
IDIO idio_export_module_primitive (IDIO module, idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__);
IDIO idio_add_primitive (idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__);

IDIO idio_toplevel_extend (IDIO lo, IDIO name, int variant, IDIO cs, IDIO cm);
IDIO idio_environ_extend (IDIO lo, IDIO name, IDIO val, IDIO cs);

void idio_meaning_copy_src_properties (IDIO src, IDIO dst);
void idio_meaning_copy_src_properties_r (IDIO src, IDIO dst);
void idio_meaning_copy_src_properties_f (IDIO src, IDIO dst);

void idio_install_expander (IDIO id, IDIO proc);

void idio_install_infix_operator (IDIO id, IDIO proc, int pri);
IDIO idio_infix_operatorp (IDIO name);
IDIO idio_infix_operator_expand (IDIO e, int depth);

void idio_install_postfix_operator (IDIO id, IDIO proc, int pri);
IDIO idio_postfix_operatorp (IDIO name);
IDIO idio_postfix_operator_expand (IDIO e, int depth);

IDIO idio_operator_expand (IDIO e, int depth);

IDIO idio_evaluate (IDIO e, IDIO cs);
void idio_init_evaluate ();
void idio_evaluate_add_primitives ();
void idio_final_evaluate ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
