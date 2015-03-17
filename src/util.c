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
 * util.c
 * 
 */

#include "idio.h"

const char *idio_type_enum2string (idio_type_e type)
{
    switch (type) {
    case IDIO_TYPE_NONE: return "NONE";
    case IDIO_TYPE_STRING: return "STRING";
    case IDIO_TYPE_SUBSTRING: return "SUBSTRING";
    case IDIO_TYPE_SYMBOL: return "SYMBOL";
    case IDIO_TYPE_PAIR: return "PAIR";
    case IDIO_TYPE_ARRAY: return "ARRAY";
    case IDIO_TYPE_HASH: return "HASH";
    case IDIO_TYPE_CLOSURE: return "FUNCTION";
    case IDIO_TYPE_PRIMITIVE_C: return "FUNCTION_C";
    case IDIO_TYPE_BIGNUM: return "BIGNUM";
	
    case IDIO_TYPE_C_INT8: return "C INT8";
    case IDIO_TYPE_C_UINT8: return "C UINT8";
    case IDIO_TYPE_C_INT16: return "C INT16";
    case IDIO_TYPE_C_UINT16: return "C UINT16";
    case IDIO_TYPE_C_INT32: return "C INT32";
    case IDIO_TYPE_C_UINT32: return "C UINT32";
    case IDIO_TYPE_C_INT64: return "C INT64";
    case IDIO_TYPE_C_UINT64: return "C UINT64";
    case IDIO_TYPE_C_FLOAT: return "C FLOAT";
    case IDIO_TYPE_C_DOUBLE: return "C DOUBLE";
    case IDIO_TYPE_C_POINTER: return "C POINTER";
    case IDIO_TYPE_C_VOID: return "C VOID";
	
    case IDIO_TYPE_C_TYPEDEF: return "TAG";
    case IDIO_TYPE_C_STRUCT: return "C_STRUCT";
    case IDIO_TYPE_C_INSTANCE: return "C_INSTANCE";
    case IDIO_TYPE_C_FFI: return "C_FFI";
    case IDIO_TYPE_FRAME: return "FRAME";
    case IDIO_TYPE_OPAQUE: return "OPAQUE";
    default:
	fprintf (stderr, "IDIO_TYPE_ENUM2STRING: unexpected type %d\n", type);
	IDIO_C_ASSERT (0);
	return "NOT KNOWN";
    }
}

const char *idio_type2string (IDIO o)
{
    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
	return "FIXNUM";
    case IDIO_TYPE_CONSTANT_MARK:
	return "SCONSTANT";
    case IDIO_TYPE_POINTER_MARK:
	return idio_type_enum2string (o->type);
    default:
	fprintf (stderr, "idio_type2string: unexpected type %p\n", o);
	IDIO_C_ASSERT (0);
	return "NOT KNOWN";
    }
}

#define IDIO_EQUAL_EQP		1
#define IDIO_EQUAL_EQVP		2
#define IDIO_EQUAL_EQUALP	3

int idio_eqp (IDIO f, void *o1, void *o2)
{
    return idio_equal (f, (IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQP);
}

int idio_eqvp (IDIO f, void *o1, void *o2)
{
    return idio_equal (f, (IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQVP);
}

int idio_equalp (IDIO f, void *o1, void *o2)
{
    return idio_equal (f, (IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQUALP);
}

int idio_equal (IDIO f, IDIO o1, IDIO o2, int eqp)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    if (o1 == o2) {
	return 1;
    }
    
    int m1 = (intptr_t) o1 & 3;
    
    switch (m1) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	/*
	  We already tested for equality above!
	 */
	return 0;
    case IDIO_TYPE_POINTER_MARK:
	{
	    if (o1->type != o2->type) {
		return 0;
	    }
    
	    if (IDIO_FLAG_FREE_SET (o1) ||
		IDIO_FLAG_FREE_SET (o2)) {
		return 0;
	    }

	    size_t i;

	    switch (o1->type) {
	    case IDIO_TYPE_C_INT8:
		return (IDIO_C_TYPE_INT8 (o1) == IDIO_C_TYPE_INT8 (o2));
	    case IDIO_TYPE_C_UINT8:
		return (IDIO_C_TYPE_UINT8 (o1) == IDIO_C_TYPE_UINT8 (o2));
	    case IDIO_TYPE_C_INT16:
		return (IDIO_C_TYPE_INT16 (o1) == IDIO_C_TYPE_INT16 (o2));
	    case IDIO_TYPE_C_UINT16:
		return (IDIO_C_TYPE_UINT16 (o1) == IDIO_C_TYPE_UINT16 (o2));
	    case IDIO_TYPE_C_INT32:
		return (IDIO_C_TYPE_INT32 (o1) == IDIO_C_TYPE_INT32 (o2));
	    case IDIO_TYPE_C_UINT32:
		return (IDIO_C_TYPE_UINT32 (o1) == IDIO_C_TYPE_UINT32 (o2));
	    case IDIO_TYPE_C_INT64:
		return (IDIO_C_TYPE_INT64 (o1) == IDIO_C_TYPE_INT64 (o2));
	    case IDIO_TYPE_C_UINT64:
		return (IDIO_C_TYPE_UINT64 (o1) == IDIO_C_TYPE_UINT64 (o2));
	    case IDIO_TYPE_C_FLOAT:
		return (IDIO_C_TYPE_FLOAT (o1) == IDIO_C_TYPE_FLOAT (o2));
	    case IDIO_TYPE_C_DOUBLE:
		return (IDIO_C_TYPE_DOUBLE (o1) == IDIO_C_TYPE_DOUBLE (o2));
	    case IDIO_TYPE_C_POINTER:
		return (IDIO_C_TYPE_POINTER_P (o1) == IDIO_C_TYPE_POINTER_P (o2));
	    case IDIO_TYPE_STRING:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.string == o2->u.string);
		}
		
		if (IDIO_STRING_BLEN (o1) != IDIO_STRING_BLEN (o2)) {
		    return 0;
		}
		    
		return strncmp (IDIO_STRING_S (o1), IDIO_STRING_S (o2), IDIO_STRING_BLEN (o1) == 0);
	    case IDIO_TYPE_SUBSTRING:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.substring == o2->u.substring);
		}
		    
		if (IDIO_SUBSTRING_BLEN (o1) != IDIO_SUBSTRING_BLEN (o2)) {
		    return 0;
		}
		    
		return strncmp (IDIO_SUBSTRING_S (o1), IDIO_SUBSTRING_S (o2), IDIO_SUBSTRING_BLEN (o1) == 0);
	    case IDIO_TYPE_SYMBOL:
		if (o1 != o2) {
		    return 0;
		}
		
		break;
	    case IDIO_TYPE_PAIR:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.pair == o2->u.pair);
		}
		
		return (idio_equalp (f, IDIO_PAIR_H (o1), IDIO_PAIR_H (o2)) &&
			idio_equalp (f, IDIO_PAIR_T (o1), IDIO_PAIR_T (o2)));
	    case IDIO_TYPE_ARRAY:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.array == o2->u.array);
		}
		
		if (IDIO_ARRAY_USIZE (o1) != IDIO_ARRAY_USIZE (o2)) {
		    return 0;
		}

		for (i = 0; i < IDIO_ARRAY_ASIZE (o1); i++) {
		    if (! idio_equalp (f, IDIO_ARRAY_AE (o1, i), IDIO_ARRAY_AE (o2, i))) {
			return 0;
		    }
		}
		break;
	    case IDIO_TYPE_HASH:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.hash == o2->u.hash);
		}
		
		if (IDIO_HASH_SIZE (o1) != IDIO_HASH_SIZE (o2)) {
		    return 0;
		}
		
		for (i = 0; i < IDIO_HASH_SIZE (o1); i++) {
		    if (! idio_equalp (f, IDIO_HASH_HE_KEY (o1, i), IDIO_HASH_HE_KEY (o2, i)) ||
			! idio_equalp (f, IDIO_HASH_HE_VALUE (o1, i), IDIO_HASH_HE_VALUE (o2, i))) {
			return 0;
		    }
		}
		break;
	    case IDIO_TYPE_CLOSURE:
		return (o1->u.closure == o2->u.closure);
	    case IDIO_TYPE_PRIMITIVE_C:
		return (o1->u.primitive_C == o2->u.primitive_C);
	    case IDIO_TYPE_C_FFI:
		return (o1->u.C_FFI == o2->u.C_FFI);
	    case IDIO_TYPE_C_STRUCT:
		return (o1->u.C_struct == o2->u.C_struct);
	    case IDIO_TYPE_C_INSTANCE:
		return o1->u.C_instance == o2->u.C_instance;
	    case IDIO_TYPE_OPAQUE:
		return (o1->u.opaque == o2->u.opaque);
	    case IDIO_TYPE_C_TYPEDEF:
		return (o1->u.C_typedef == o2->u.C_typedef);
	    case IDIO_TYPE_BIGNUM:
		return idio_bignum_real_equal_p (f, o1, o2);
	    default:
		fprintf (stderr, "idio_equal: IDIO_TYPE_POINTER_MARK: o1->type %d unexpected\n", o1->type);
		IDIO_C_ASSERT (0);
		return 0;
	    }
	}
    default:
	fprintf (stderr, "idio_equal: o1->type %p unexpected\n", o1);
	IDIO_C_ASSERT (0);
	return 0;
    }

    return 1;
}

/*
  Scheme-ish write -- internal representation (where appropriate)
  suitable for (read).  Primarily:

  CHARACTER #\a:	#\a
  STRING "foo":		"foo"
 */
char *idio_as_string (IDIO f, IDIO o, int depth)
{
    IDIO_ASSERT (f);

    if ((o->flags & IDIO_FLAG_FREE_MASK) == IDIO_FLAG_FREE) {
	return NULL;
    }

    IDIO_ASSERT (o);

    char *r;
    size_t i;
    
    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
	{
	    if (asprintf (&r, "%ld", ((intptr_t) o) >> 2) == -1) {
		return NULL;
	    }
	    break;
	}
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    char *t = NULL;
	    
	    long v = ((intptr_t) o) >> 2;
	    
	    switch (v) {
	    case IDIO_CONSTANT_NIL:             t = "#nil";            break;
	    case IDIO_CONSTANT_UNDEF:           t = "#undef";          break;
	    case IDIO_CONSTANT_UNSPEC:          t = "#unspec";         break;
	    case IDIO_CONSTANT_EOF:             t = "#eof";            break;
	    case IDIO_CONSTANT_TRUE:            t = "#true";           break;
	    case IDIO_CONSTANT_FALSE:           t = "#false";          break;
	    case IDIO_CONSTANT_NAN:             t = "#NaN";            break;
	    case IDIO_CONSTANT_ELSE:            t = "else";            break;
	    case IDIO_CONSTANT_EQ_GT:           t = "eq_gt";           break;
	    case IDIO_CONSTANT_QUOTE:           t = "quote";           break;
	    case IDIO_CONSTANT_UNQUOTE:         t = "unquote";         break;
	    case IDIO_CONSTANT_UNQUOTESPLICING: t = "unquotesplicing"; break;
	    case IDIO_CONSTANT_QUASIQUOTE:      t = "quasiquote";      break;
	    case IDIO_CONSTANT_LAMBDA:          t = "lambda";          break;
	    case IDIO_CONSTANT_MACRO:           t = "macro";           break;
	    case IDIO_CONSTANT_BEGIN:           t = "begin";           break;
	    case IDIO_CONSTANT_AND:             t = "and";             break;
	    case IDIO_CONSTANT_OR:              t = "or";              break;
	    case IDIO_CONSTANT_DEFINE:          t = "define";          break;
	    case IDIO_CONSTANT_LETREC:          t = "letrec";          break;
	    case IDIO_CONSTANT_BLOCK:           t = "block";           break;
	    case IDIO_CONSTANT_TEMPLATE:        t = "template";        break;
	    case IDIO_CONSTANT_FIXED_TEMPLATE:  t = "fixed_template";  break;
	    case IDIO_CONSTANT_CLASS:           t = "class";           break;
	    case IDIO_CONSTANT_SUPER:           t = "super";           break;
	    case IDIO_CONSTANT_C_STRUCT:        t = "c_struct";        break;
	    case IDIO_CONSTANT_ROOT:            t = "root";            break;
	    case IDIO_CONSTANT_INIT:            t = "init";            break;
	    case IDIO_CONSTANT_THIS:            t = "this";            break;
	    case IDIO_CONSTANT_ERROR:           t = "error";           break;
	    case IDIO_CONSTANT_PROFILE:         t = "profile";         break;
	    case IDIO_CONSTANT_DLOADS:          t = "dloads";          break;
	    case IDIO_CONSTANT_AMPERSAND:       t = "ampersand";       break;
	    case IDIO_CONSTANT_ASTERISK:        t = "asterisk";        break;
	    case IDIO_CONSTANT_NAMESPACE:       t = "namespace";       break;
	    default:
		IDIO_C_ASSERT (0);
		break;
	    }

	    if (asprintf (&r, "%s", t) == -1) {
		return NULL;
	    }
	}
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_C_INT8:
		if (asprintf (&r, "%hhd", IDIO_C_TYPE_INT8 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_UINT8:
		if (asprintf (&r, "%hhu", IDIO_C_TYPE_UINT8 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_INT16:
		if (asprintf (&r, "%hd", IDIO_C_TYPE_INT16 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_UINT16:
		if (asprintf (&r, "%hu", IDIO_C_TYPE_UINT16 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_INT32:
		if (asprintf (&r, "%d", IDIO_C_TYPE_INT32 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_UINT32:
		if (asprintf (&r, "%u", IDIO_C_TYPE_UINT32 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_INT64:
		if (asprintf (&r, "%ld", IDIO_C_TYPE_INT64 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_UINT64:
		if (asprintf (&r, "%lu", IDIO_C_TYPE_UINT64 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_FLOAT:
		if (asprintf (&r, "%g", IDIO_C_TYPE_FLOAT (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_DOUBLE:
		if (asprintf (&r, "%g", IDIO_C_TYPE_DOUBLE (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_POINTER:
		if (asprintf (&r, "%p free=%d", IDIO_C_TYPE_POINTER_P (o), IDIO_C_TYPE_POINTER_FREEP (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_STRING:
		if (asprintf (&r, "\"%.*s\"", (int) IDIO_STRING_BLEN (o), IDIO_STRING_S (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (asprintf (&r, "\"%.*s\"", (int) IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_S (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_SYMBOL:
		IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_SYMBOL_STRING (o), depth - 1));
		break;
	    case IDIO_TYPE_PAIR:
		/*
		  Technically a list (of pairs) should look like:

		  "(a (b (c (d . nil))))"

		  but tradition dictates that we should flatten
		  the list to:

		  "(a b c d)"

		  hence the while loop which continues if the tail is
		  itself a pair
		*/
		{
		    if (idio_isa_symbol (f, IDIO_PAIR_H (o))) {
			int special = 0;
		
			if (idio_S_quote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, "'") == -1) {
				return NULL;
			    }
			} else if (idio_S_unquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, ",") == -1) {
				return NULL;
			    }
			} else if (idio_S_unquotesplicing == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, ",@") == -1) {
				return NULL;
			    }
			} else if (idio_S_quasiquote == IDIO_PAIR_H (o)) {
			    special = 1;
			    if (asprintf (&r, "`") == -1) {
				return NULL;
			    }
			}

			if (special) {
			    if (idio_isa_pair (f, IDIO_PAIR_T (o))) {
				IDIO_STRCAT_FREE (r, idio_as_string (f, idio_pair_head (f, IDIO_PAIR_T (o)), depth - 1));
			    } else {
				IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_PAIR_T (o), depth - 1));
			    }
			    break;
			}
		    }
	    
		    if (asprintf (&r, "(") == -1) {
			return NULL;
		    }
		    while (1) {
			IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_PAIR_H (o), depth - 1));

			o = IDIO_PAIR_T (o);
			if (o->type != IDIO_TYPE_PAIR) {
			    if (idio_S_nil != o) {
				char *t = idio_as_string (f, o, depth - 1);
				char *ps;
				if (asprintf (&ps, " . %s", t) == -1) {
				    free (t);
				    free (r);
				    return NULL;
				}
				free (t);
				IDIO_STRCAT_FREE (r, ps);
			    }
			    break;
			} else {
			    IDIO_STRCAT (r, " ");
			}
		    }
		    IDIO_STRCAT (r, ")");
		}
		break;
	    case IDIO_TYPE_ARRAY:
		if (asprintf (&r, "#( ") == -1) {
		    return NULL;
		}
		if (depth >= 0) {
		    for (i = 0; i < IDIO_ARRAY_ASIZE (o); i++) {
			if (idio_S_nil != IDIO_ARRAY_AE (o, i)) {
			    char *t = idio_as_string (f, IDIO_ARRAY_AE (o, i), depth - 1);
			    char *aes;
			    if (asprintf (&aes, "%s ", t) == -1) {
				free (t);
				free (r);
				return NULL;
			    }
			    free (t);
			    IDIO_STRCAT_FREE (r, aes);
			}
		    }
		} else {
		    IDIO_STRCAT (r, "...");
		}
		IDIO_STRCAT (r, ")");
		break;
	    case IDIO_TYPE_HASH:
		if (asprintf (&r, "{ ") == -1) {
		    return NULL;
		}
		if (depth >= 0) {
		    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
			    char *t = idio_as_string (f, IDIO_HASH_HE_KEY (o, i), depth - 1);
			    char *hes;
			    if (asprintf (&hes, "%s:", t) == -1) {
				free (t);
				free (r);
				return NULL;
			    }
			    free (t);
			    IDIO_STRCAT_FREE (r, hes);
			    if (IDIO_HASH_HE_VALUE (o, i)) {
				t = idio_as_string (f, IDIO_HASH_HE_VALUE (o, i), depth - 1);
			    } else {
				if (asprintf (&t, "-") == -1) {
				    free (r);
				    return NULL;
				}
			    }
			    if (asprintf (&hes, "%s ", t) == -1) {
				free (t);
				free (r);
				return NULL;
			    }
			    free (t);
			    IDIO_STRCAT_FREE (r, hes);
			}
		    }
		} else {
		    IDIO_STRCAT (r, "...");
		}
		IDIO_STRCAT (r, "}");
		break;
	    case IDIO_TYPE_CLOSURE:
		{
		    if (asprintf (&r, "(lambda ") == -1) {
			return NULL;
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_CLOSURE_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_CLOSURE_BODY (o), depth - 1));
		    /* IDIO_STRCAT (r, " "); */
		    /* IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_CLOSURE_FRAME (o), depth + 1, depth + 1)); */
		    IDIO_STRCAT (r, ")");
		    break;
		}
	    case IDIO_TYPE_PRIMITIVE_C:
		if (asprintf (&r, "#F_C{%s}", IDIO_PRIMITIVE_C_NAME (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_FFI:
		{
		    char *t = idio_as_string (f, IDIO_C_FFI_NAME (o), depth - 1);
		    if (asprintf (&r, "#F_CFFI{%s ", t) == -1) {
			free (t);
			return NULL;
		    }
		    free (t);
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_C_FFI_SYMBOL (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_C_FFI_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_C_FFI_RESULT (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_C_FFI_NAME (o), depth - 1));
		    IDIO_STRCAT (r, " }");
		    break;
		}
	    case IDIO_TYPE_C_STRUCT:
		{
		    if (asprintf (&r, "c_struct %10p { ", o) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT (r, "\n\tslots: ");
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_C_STRUCT_SLOTS (o), depth - 1));

		    IDIO mh = IDIO_C_STRUCT_METHODS (o);
	    
		    IDIO_STRCAT (r, "\n\tmethods: ");
		    if (idio_S_nil != mh) {
			for (i = 0; i < IDIO_HASH_SIZE (mh); i++) {
			    if (idio_S_nil != IDIO_HASH_HE_KEY (mh, i)) {
				char *t = idio_as_string (f, IDIO_HASH_HE_KEY (mh, i), depth - 1);
				char *hes;
				if (asprintf (&hes, "\n\t%10s:", t) == -1) {
				    free (t);
				    free (r);
				    return NULL;
				}
				free (t);
				IDIO_STRCAT_FREE (r, hes);
				if (IDIO_HASH_HE_VALUE (mh, i)) {
				    t = idio_as_string (f, IDIO_HASH_HE_VALUE (mh, i), depth - 1);
				} else {
				    if (asprintf (&t, "-") == -1) {
					free (r);
					return NULL;
				    }
				}
				if (asprintf (&hes, "%s ", t) == -1) {
				    free (t);
				    free (r);
				    return NULL;
				}
				free (t);
				IDIO_STRCAT_FREE (r, hes);
			    }
			}
		    }
		    IDIO_STRCAT (r, "\n\tframe: ");
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_C_STRUCT_FRAME (o), depth - 1));
		    IDIO_STRCAT (r, "\n}");
		    break;
		}
	    case IDIO_TYPE_C_INSTANCE:
		{
		    if (asprintf (&r, "c_instance %10p { C_ptr=%10p c-struct=%10p}", o, IDIO_C_INSTANCE_P (o), IDIO_C_INSTANCE_C_STRUCT (o)) == -1) {
			return NULL;
		    }
		    break;
		}
	    case IDIO_TYPE_FRAME:
		{
		    if (asprintf (&r, "{frame %10p f=%02x", o, IDIO_FRAME_FLAGS (o)) == -1) {
			return NULL;
		    }
		    if (idio_G_frame == o) {
			IDIO_STRCAT (r, " == idio_G_frame");
		    } else {
			IDIO_STRCAT (r, " form=");
			if (NULL == IDIO_FRAME_FORM (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_FRAME_FORM (o), depth - 1));
			}
			IDIO_STRCAT (r, " namespace=");
			if (NULL == IDIO_FRAME_NAMESPACE (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_FRAME_NAMESPACE (o), depth - 1));
			}
			IDIO_STRCAT (r, " env=");
			if (NULL == IDIO_FRAME_ENV (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else if (IDIO_FRAME_ENV (idio_G_frame) == IDIO_FRAME_ENV (o)) {
			    IDIO_STRCAT (r, "(idio_G_frame)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_FRAME_ENV (o), depth - 1));
			}
			IDIO_STRCAT (r, " pframe=");
			if (NULL == IDIO_FRAME_PFRAME (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else if (idio_G_frame == IDIO_FRAME_PFRAME (o)) {
			    IDIO_STRCAT (r, "(idio_G_frame)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_FRAME_PFRAME (o), depth - 1));
			}
			IDIO_STRCAT (r, " stack=");
			if (NULL == IDIO_FRAME_STACK (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_FRAME_STACK (o), depth - 1));
			}
			IDIO_STRCAT (r, " threads=");
			if (NULL == IDIO_FRAME_THREADS (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_FRAME_THREADS (o), depth - 1));
			}
			IDIO_STRCAT (r, " error=");
			if (NULL == IDIO_FRAME_ERROR (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_FRAME_ERROR (o), depth - 1));
			}
		    }
		    IDIO_STRCAT (r, " }");
		    break;
		}
	    case IDIO_TYPE_OPAQUE:
		{
		    if (asprintf (&r, "#O{%10p ", IDIO_OPAQUE_P (o)) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (f, IDIO_OPAQUE_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, "}");
		    break;
		}
	    case IDIO_TYPE_C_TYPEDEF:
		{
		    if (asprintf (&r, "#T{%10p}", IDIO_C_TYPEDEF_SYM (o)) == -1) {
			return NULL;
		    }
		    break;
		}
	    case IDIO_TYPE_BIGNUM:
		{
		    r = idio_bignum_as_string (f, o);
		    break;
		}
	    default:
		IDIO_C_ASSERT (0);
		break;
	    }
	}
    default:
	IDIO_C_ASSERT (0);
	break;
    }

    return r;
}

/*
  Scheme-ish display -- no internal representation (where
  appropriate).  Unsuitable for (read).  Primarily:

  CHARACTER #\a:	a
  STRING "foo":		foo

  Most non-data types will still come out as some internal
  representation.  (Still unsuitable for (read) as it doesn't know
  about them.)
 */
char *idio_display_string (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    char *r;
    
    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	r = idio_as_string (f, o, 4);
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (o->type) {
	    case IDIO_TYPE_C_INT8:
	    case IDIO_TYPE_C_UINT8:
	    case IDIO_TYPE_C_INT16:
	    case IDIO_TYPE_C_UINT16:
	    case IDIO_TYPE_C_INT32:
	    case IDIO_TYPE_C_UINT32:
	    case IDIO_TYPE_C_INT64:
	    case IDIO_TYPE_C_UINT64:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
	    case IDIO_TYPE_C_POINTER:
		r = idio_as_string (f, o, 4);
		break;
	    case IDIO_TYPE_STRING:
		if (asprintf (&r, "%.*s", (int) IDIO_STRING_BLEN (o), IDIO_STRING_S (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (asprintf (&r, "%.*s", (int) IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_S (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_SYMBOL:
	    case IDIO_TYPE_PAIR:
	    case IDIO_TYPE_ARRAY:
	    case IDIO_TYPE_HASH:
	    case IDIO_TYPE_CLOSURE:
	    case IDIO_TYPE_PRIMITIVE_C:
	    case IDIO_TYPE_C_FFI:
	    case IDIO_TYPE_C_STRUCT:
	    case IDIO_TYPE_C_INSTANCE:
	    case IDIO_TYPE_FRAME:
	    case IDIO_TYPE_OPAQUE:
	    case IDIO_TYPE_C_TYPEDEF:
	    case IDIO_TYPE_BIGNUM:
		r = idio_as_string (f, o, 4);
		break;
	    default:
		if (asprintf (&r, "type %d n/k", o->type) == -1) {
		    return NULL;
		}
		break;
	    }
	}
    default:
	if (asprintf (&r, "type %d n/k", o->type) == -1) {
	    return NULL;
	}
	break;
    }
    
    return r;
}

/*
 * invoke Idio code from C
 */
IDIO idio_apply (IDIO f, IDIO func, IDIO args)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (func);
    IDIO_ASSERT (args);
    
    switch (func->type) {
    case IDIO_TYPE_CLOSURE:
    case IDIO_TYPE_PRIMITIVE_C:
    default:
	fprintf (stderr, "idio_apply: unexpected function type %s %d\n", idio_type_enum2string (func->type), func->type);
    }

    return idio_S_nil;
}

IDIO idio_apply1 (IDIO f, IDIO func, IDIO arg)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (func);
    IDIO_ASSERT (arg);
    
    return idio_apply (f, func, idio_pair (f, arg, idio_S_nil));
}

IDIO idio_apply2 (IDIO f, IDIO func, IDIO arg1, IDIO arg2)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (func);
    IDIO_ASSERT (arg1);
    IDIO_ASSERT (arg2);
    
    return idio_apply (f,
		       func,
		       idio_pair (f,
				  arg1,
				  idio_pair (f,
					     arg2,
					     idio_S_nil)));
}

void idio_dump (IDIO f, IDIO o, int detail)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    if (detail > 0) {
		IDIO_FRAME_FPRINTF (f, stderr, "%10p ", o);
		if (detail > 1) {
		    IDIO_FRAME_FPRINTF (f, stderr, "-> %10p ", o->next);
		}
		IDIO_FRAME_FPRINTF (f, stderr, "t=%2d/%4.4s f=%2x ", o->type, idio_type2string (o), o->flags);
	    }
    
	    switch (o->type) {
	    case IDIO_TYPE_C_INT8:
	    case IDIO_TYPE_C_UINT8:
	    case IDIO_TYPE_C_INT16:
	    case IDIO_TYPE_C_UINT16:
	    case IDIO_TYPE_C_INT32:
	    case IDIO_TYPE_C_UINT32:
	    case IDIO_TYPE_C_INT64:
	    case IDIO_TYPE_C_UINT64:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
		IDIO_FRAME_FPRINTF (f, stderr, "n=");
		break;
	    case IDIO_TYPE_C_POINTER:
		IDIO_FRAME_FPRINTF (f, stderr, "p=");
		break;
	    case IDIO_TYPE_STRING:
		if (detail) {
		    IDIO_FRAME_FPRINTF (f, stderr, "blen=%d s=", IDIO_STRING_BLEN (o));
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (detail) {
		    IDIO_FRAME_FPRINTF (f, stderr, "blen=%d parent=%10p subs=", IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_PARENT (o));
		}
		break;
	    case IDIO_TYPE_SYMBOL:
		IDIO_FRAME_FPRINTF (f, stderr, "sym=");
		break;
	    case IDIO_TYPE_PAIR:
		if (detail > 1) {
		    IDIO_FRAME_FPRINTF (f, stderr, "head=%10p tail=%10p p=", IDIO_PAIR_H (o), IDIO_PAIR_T (o));
		}
		break;
	    case IDIO_TYPE_ARRAY:
		if (detail) {
		    IDIO_FRAME_FPRINTF (f, stderr, "size=%d/%d \n", IDIO_ARRAY_USIZE (o), IDIO_ARRAY_ASIZE (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_ARRAY_ASIZE (o); i++) {
			    if (idio_S_nil != IDIO_ARRAY_AE (o, i)) {
				char *s = idio_as_string (f, IDIO_ARRAY_AE (o, i), 4);
				IDIO_FRAME_FPRINTF (f, stderr, "\t%3d: %10p %10s\n", i, IDIO_ARRAY_AE (o, i), s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_HASH:
		if (detail) {
		    IDIO_FRAME_FPRINTF (f, stderr, "hsize=%d hmask=%x\n", IDIO_HASH_SIZE (o), IDIO_HASH_MASK (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			    if (idio_S_nil == IDIO_HASH_HE_KEY (o, i)) {
				continue;
			    } else {
				char *s = idio_as_string (f, IDIO_HASH_HE_KEY (o, i), 4);
				if (detail & 0x4) {
				    IDIO_FRAME_FPRINTF (f, stderr, "\t%30s : ", s);
				} else {
				    IDIO_FRAME_FPRINTF (f, stderr, "\t%3d: k=%10p v=%10p n=%3d %10s : ",
							i,
							IDIO_HASH_HE_KEY (o, i),
							IDIO_HASH_HE_VALUE (o, i),
							IDIO_HASH_HE_NEXT (o, i),
							s);
				}
				free (s);
				if (IDIO_HASH_HE_VALUE (o, i)) {
				    s = idio_as_string (f, IDIO_HASH_HE_VALUE (o, i), 4);
				} else {
				    if (asprintf (&s, "-") == -1) {
					break;
				    }
				}
				IDIO_FRAME_FPRINTF (f, stderr, "%-10s\n", s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_CLOSURE:
		break;
	    case IDIO_TYPE_PRIMITIVE_C:
		break;
	    case IDIO_TYPE_C_FFI:
		break;
	    case IDIO_TYPE_C_STRUCT:
		break;
	    case IDIO_TYPE_C_INSTANCE:
		break;
	    case IDIO_TYPE_FRAME:
		break;
	    case IDIO_TYPE_OPAQUE:
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		break;
	    case IDIO_TYPE_BIGNUM:
		break;
	    default:
		IDIO_C_ASSERT (0);
		IDIO_FRAME_FPRINTF (f, stderr, "v=n/k");
	    }
	}
    default:
	IDIO_C_ASSERT (0);
	IDIO_FRAME_FPRINTF (f, stderr, "v=n/k");
    }	

    if (detail &&
	!(detail & 0x4)) {
	char *s = idio_as_string (f, o, detail);
	IDIO_FRAME_FPRINTF (f, stderr, "%s", s);
	free (s);
    }

    IDIO_FRAME_FPRINTF (f, stderr, "\n");
}

/*
  XXX delete me
 */

void idio_expr_dump_ (IDIO f, IDIO e, const char *en, int detail)
{
    fprintf (stderr, "%20s=", en);
    IDIO_FRAME_COLLECTOR (f)->verbose++;
    idio_dump (f, e, detail);
    IDIO_FRAME_COLLECTOR (f)->verbose--;
}

void idio_frame_env_dump_ (IDIO f, int detail)
{
    fprintf (stderr, "ENV: ");
    if (f == idio_G_frame) {
	fprintf (stderr, " (idio_G_frame)\n");
	return;
    }
    IDIO_FRAME_COLLECTOR (f)->verbose++;
    idio_dump (f, IDIO_FRAME_ENV (f), detail | 0x4);
    if (NULL != IDIO_FRAME_PFRAME (f)) { 
     	fprintf (stderr, "+");
	idio_frame_env_dump_ (IDIO_FRAME_PFRAME (f), detail);
    } 
    fprintf (stderr, "\n"); 
    IDIO_FRAME_COLLECTOR (f)->verbose--;
}

void idio_frame_dump_ (IDIO f, const char *sname, int detail)
{
    fprintf (stderr, "%20s: {frame %10p f=%02x}\n", sname, f, IDIO_FRAME_FLAGS (f));
    idio_expr_dump (f, IDIO_FRAME_NAMESPACE (f));
    idio_expr_dump (f, IDIO_FRAME_FORM (f));
    fprintf (stderr, "ERROR:");
    if (idio_array_size (f, IDIO_FRAME_ERROR (f))) {
	IDIO_FRAME_COLLECTOR (f)->verbose++;
	idio_dump (f, IDIO_FRAME_ERROR (f), detail);
	IDIO_FRAME_COLLECTOR (f)->verbose--;
    } else {
	fprintf (stderr, "\n");
    }
    fprintf (stderr, "STACK:");
    if (idio_array_size (f, IDIO_FRAME_STACK (f))) {
	IDIO_FRAME_COLLECTOR (f)->verbose++;
	idio_dump (f, IDIO_FRAME_STACK (f), detail);
	IDIO_FRAME_COLLECTOR (f)->verbose--;
    } else {
	fprintf (stderr, "\n");
    }
    idio_frame_env_dump_ (f, detail);
    fprintf (stderr, "THREADS:");
    if (idio_array_size (f, IDIO_FRAME_THREADS (f))) {
	IDIO_FRAME_COLLECTOR (f)->verbose++;
	idio_dump (f, IDIO_FRAME_THREADS (f), detail);
	IDIO_FRAME_COLLECTOR (f)->verbose--;
    } else {
	fprintf (stderr, "\n");
    }
}

void idio_frame_trace_ (IDIO f, const char *sname, int detail)
{
    if (f == idio_G_frame) {
	fprintf (stderr, "f == idio_G_frame\n");
	return;
    }
    
    idio_frame_dump_ (f, sname, detail);
    idio_expr_dump (f, IDIO_FRAME_FORM (f));
    if (IDIO_FRAME_PFRAME (f)) {
	idio_frame_trace_ (IDIO_FRAME_PFRAME (f), ">>", detail);
    }
}


/* Local Variables: */
/* mode: C/l */
/* buffer-file-coding-system: undecided-unix */
/* End: */
