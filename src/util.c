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

int idio_type (IDIO o)
{
    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
	return IDIO_TYPE_FIXNUM;
    case IDIO_TYPE_CONSTANT_MARK:
	return IDIO_TYPE_CONSTANT;
    case IDIO_TYPE_CHARACTER_MARK:
	return IDIO_TYPE_CHARACTER;
    case IDIO_TYPE_POINTER_MARK:
	return o->type;
    default:
	idio_error_message ("type: unexpected object type %x", o);
	return IDIO_TYPE_NONE;
    }
}

const char *idio_type_enum2string (idio_type_e type)
{
    switch (type) {
    case IDIO_TYPE_NONE: return "NONE";
    case IDIO_TYPE_FIXNUM: return "FIXNUM";
    case IDIO_TYPE_CONSTANT: return "CONSTANT";
    case IDIO_TYPE_CHARACTER: return "CHARACTER";
    case IDIO_TYPE_STRING: return "STRING";
    case IDIO_TYPE_SUBSTRING: return "SUBSTRING";
    case IDIO_TYPE_SYMBOL: return "SYMBOL";
    case IDIO_TYPE_PAIR: return "PAIR";
    case IDIO_TYPE_ARRAY: return "ARRAY";
    case IDIO_TYPE_HASH: return "HASH";
    case IDIO_TYPE_CLOSURE: return "CLOSURE";
    case IDIO_TYPE_PRIMITIVE: return "PRIMITIVE";
    case IDIO_TYPE_BIGNUM: return "BIGNUM";
    case IDIO_TYPE_MODULE: return "MODULE";
    case IDIO_TYPE_FRAME: return "FRAME";
    case IDIO_TYPE_HANDLE: return "HANDLE";
    case IDIO_TYPE_STRUCT_TYPE: return "STRUCT_TYPE";
    case IDIO_TYPE_STRUCT_INSTANCE: return "STRUCT_INSTANCE";
    case IDIO_TYPE_THREAD: return "THREAD";
	
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
    case IDIO_TYPE_OPAQUE: return "OPAQUE";
    default:
	IDIO_FPRINTF (stderr, "IDIO_TYPE_ENUM2STRING: unexpected type %d\n", type);
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

IDIO_DEFINE_PRIMITIVE1 ("zero?", zerop, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if ((idio_isa_fixnum (o) &&
	 0 == IDIO_FIXNUM_VAL (o)) ||
	(idio_isa_bignum (o) &&
	 idio_bignum_zero_p (o))) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_nil (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_S_nil == o);
}

IDIO_DEFINE_PRIMITIVE1 ("null?", nullp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_S_nil == o) {
	r = idio_S_true;
    }

    return r;
}

int idio_isa_boolean (IDIO o)
{
    IDIO_ASSERT (o);

    return (idio_S_true == o ||
	    idio_S_false == o);
}

IDIO_DEFINE_PRIMITIVE1 ("boolean?", booleanp, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_boolean (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("not", not, (IDIO e))
{
    IDIO_ASSERT (e);

    IDIO r = idio_S_false;

    if (idio_S_false == e) {
	r = idio_S_true;
    }

    return r;
}

#define IDIO_EQUAL_EQP		1
#define IDIO_EQUAL_EQVP		2
#define IDIO_EQUAL_EQUALP	3

int idio_eqp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQP);
}

IDIO_DEFINE_PRIMITIVE2 ("eq?", eqp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_eqp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2 ("eqv?", eqvp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_eqvp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}

/*
 * s9.scm redefines equal? from eq? and eqv? and recurses on itself --
 * or it will if we do not define a primitive equal? which would be
 * used in its definition

IDIO_DEFINE_PRIMITIVE2 ("equal?", equalp, (IDIO o1, IDIO o2))
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    IDIO r = idio_S_false;

    if (idio_equalp (o1, o2)) {
	r = idio_S_true;
    }

    return r;
}
*/

int idio_eqvp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQVP);
}

int idio_equalp (void *o1, void *o2)
{
    return idio_equal ((IDIO) o1, (IDIO) o2, IDIO_EQUAL_EQUALP);
}

int idio_equal (IDIO o1, IDIO o2, int eqp)
{
    IDIO_ASSERT (o1);
    IDIO_ASSERT (o2);

    if (o1 == o2) {
	return 1;
    }
    
    int m1 = (intptr_t) o1 & 3;
    
    switch (m1) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_CHARACTER_MARK:
	/*
	  We already tested for equality above!
	 */
	return 0;
    case IDIO_TYPE_POINTER_MARK:
	{
	    int m2 = (intptr_t) o2 & 3;
    
	    switch (m2) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
	    case IDIO_TYPE_CHARACTER_MARK:
		/* we would have matched at the top */
		return 0;
	    default:
		break;
	    }
	    
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
		    return (o1 == o2);
		    /* return (o1->u.string == o2->u.string); */
		}
		
		if (IDIO_STRING_BLEN (o1) != IDIO_STRING_BLEN (o2)) {
		    return 0;
		}
		    
		return (strncmp (IDIO_STRING_S (o1), IDIO_STRING_S (o2), IDIO_STRING_BLEN (o1)) == 0);
	    case IDIO_TYPE_SUBSTRING:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1 == o2);
		    /* return (o1->u.substring == o2->u.substring); */
		}
		    
		if (IDIO_SUBSTRING_BLEN (o1) != IDIO_SUBSTRING_BLEN (o2)) {
		    return 0;
		}
		    
		return (strncmp (IDIO_SUBSTRING_S (o1), IDIO_SUBSTRING_S (o2), IDIO_SUBSTRING_BLEN (o1)) == 0);
	    case IDIO_TYPE_SYMBOL:
		return (o1 == o2);
		
		break;
	    case IDIO_TYPE_PAIR:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1 == o2);
		    /* return (o1->u.pair == o2->u.pair); */
		}
		
		return (idio_equalp (IDIO_PAIR_H (o1), IDIO_PAIR_H (o2)) &&
			idio_equalp (IDIO_PAIR_T (o1), IDIO_PAIR_T (o2)));
	    case IDIO_TYPE_ARRAY:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.array == o2->u.array);
		}
		
		if (IDIO_ARRAY_USIZE (o1) != IDIO_ARRAY_USIZE (o2)) {
		    return 0;
		}

		for (i = 0; i < IDIO_ARRAY_ASIZE (o1); i++) {
		    if (! idio_equalp (IDIO_ARRAY_AE (o1, i), IDIO_ARRAY_AE (o2, i))) {
			return 0;
		    }
		}
		return 1;
	    case IDIO_TYPE_HASH:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.hash == o2->u.hash);
		}
		
		if (IDIO_HASH_SIZE (o1) != IDIO_HASH_SIZE (o2)) {
		    return 0;
		}
		
		for (i = 0; i < IDIO_HASH_SIZE (o1); i++) {
		    if (! idio_equalp (IDIO_HASH_HE_KEY (o1, i), IDIO_HASH_HE_KEY (o2, i)) ||
			! idio_equalp (IDIO_HASH_HE_VALUE (o1, i), IDIO_HASH_HE_VALUE (o2, i))) {
			return 0;
		    }
		}
		return 1;
	    case IDIO_TYPE_CLOSURE:
		return (o1 == o2);
		/* return (o1->u.closure == o2->u.closure); */
	    case IDIO_TYPE_PRIMITIVE:
		return (o1 == o2);
		/* return (o1->u.primitive == o2->u.primitive); */
	    case IDIO_TYPE_BIGNUM:
		return idio_bignum_real_equal_p (o1, o2);
	    case IDIO_TYPE_HANDLE:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.handle == o2->u.handle);
		}
		
		if (! idio_equalp (IDIO_HANDLE_NAME (o1), IDIO_HANDLE_NAME (o2))) {
		    return 0;
		}
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.struct_type == o2->u.struct_type);
		}
		
		if (! idio_equalp (IDIO_STRUCT_TYPE_NAME (o1), IDIO_STRUCT_TYPE_NAME (o2)) ||
		    ! idio_equalp (IDIO_STRUCT_TYPE_PARENT (o1), IDIO_STRUCT_TYPE_PARENT (o2)) ||
		    ! idio_equalp (IDIO_STRUCT_TYPE_FIELDS (o1), IDIO_STRUCT_TYPE_FIELDS (o2))) {
		    return 0;
		}
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		if (IDIO_EQUAL_EQP == eqp) {
		    return (o1->u.struct_instance == o2->u.struct_instance);
		}
		
		if (! idio_equalp (IDIO_STRUCT_INSTANCE_TYPE (o1), IDIO_STRUCT_INSTANCE_TYPE (o2)) ||
		    ! idio_equalp (IDIO_STRUCT_INSTANCE_FIELDS (o1), IDIO_STRUCT_INSTANCE_FIELDS (o2))) {
		    return 0;
		}
		break;
	    case IDIO_TYPE_THREAD:
		return (o1->u.thread == o2->u.thread);
	    case IDIO_TYPE_C_TYPEDEF:
		return (o1->u.C_typedef == o2->u.C_typedef);
	    case IDIO_TYPE_C_STRUCT:
		return (o1->u.C_struct == o2->u.C_struct);
	    case IDIO_TYPE_C_INSTANCE:
		return o1->u.C_instance == o2->u.C_instance;
	    case IDIO_TYPE_C_FFI:
		return (o1->u.C_FFI == o2->u.C_FFI);
	    case IDIO_TYPE_OPAQUE:
		return (o1->u.opaque == o2->u.opaque);
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
 * reconstruct C escapes in s
 */
char *idio_escape_string (size_t blen, char *s)
{
    size_t i;
    size_t n = 0;
    for (i = 0; i < blen; i++) {
	n++;
	switch (s[i]) {
	case '\a': n++; break;
	case '\b': n++; break;
	case '\f': n++; break;
	case '\n': n++; break;
	case '\r': n++; break;
	case '\t': n++; break;
	case '\v': n++; break;
	}
    }

    /* 2 for "s and 1 for \0 */
    char *r = idio_alloc (1 + n + 1 + 1);

    n = 0;
    r[n++] = '"';
    for (i = 0; i < blen; i++) {
	char c = 0;
	switch (s[i]) {
	case '\a': c = 'a'; break;
	case '\b': c = 'b'; break;
	case '\f': c = 'f'; break;
	case '\n': c = 'n'; break;
	case '\r': c = 'r'; break;
	case '\t': c = 't'; break;
	case '\v': c = 'v'; break;
	}

	if (c) {
	    r[n++] = '\\';
	    r[n++] = c;
	} else {
	    r[n++] = s[i];
	}
    }

    r[n++] = '"';
    r[n] = '\0';
    
    return r;
}

/*
  Scheme-ish write -- internal representation (where appropriate)
  suitable for (read).  Primarily:

  CHARACTER #\a:	#\a
  STRING "foo":		"foo"
 */
char *idio_as_string (IDIO o, int depth)
{
    char *r;
    size_t i;
    
    IDIO_C_ASSERT (depth >= -10000);

    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
	{
	    if (asprintf (&r, "%" PRIdPTR, IDIO_FIXNUM_VAL (o)) == -1) {
		return NULL;
	    }
	    break;
	}
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    char *t = NULL;
	    
	    intptr_t v = IDIO_CONSTANT_VAL (o);
	    
	    switch (v) {
	    case IDIO_CONSTANT_NIL:                  t = "#n";                   break;
	    case IDIO_CONSTANT_UNDEF:                t = "#undef";                break;
	    case IDIO_CONSTANT_UNSPEC:               t = "#unspec";               break;
	    case IDIO_CONSTANT_EOF:                  t = "#eof";                  break;
	    case IDIO_CONSTANT_TRUE:                 t = "#t";                    break;
	    case IDIO_CONSTANT_FALSE:                t = "#f";                    break;
	    case IDIO_CONSTANT_VOID:                 t = "#void";                 break;
	    case IDIO_CONSTANT_NAN:                  t = "#NaN";                  break;

	    case IDIO_TOKEN_DOT:                     t = "T/.";                     break;
	    case IDIO_TOKEN_LPAREN:                  t = "T/(";                     break;
	    case IDIO_TOKEN_RPAREN:                  t = "T/)";                     break;
	    case IDIO_TOKEN_LBRACE:                  t = "T/{";                     break;
	    case IDIO_TOKEN_RBRACE:                  t = "T/}";                     break;
	    case IDIO_TOKEN_LBRACKET:                t = "T/[";                     break;
	    case IDIO_TOKEN_RBRACKET:                t = "T/]";                     break;
	    case IDIO_TOKEN_LANGLE:                  t = "T/<";                     break;
	    case IDIO_TOKEN_RANGLE:                  t = "T/>";                     break;
	    case IDIO_TOKEN_EOL:                     t = "T/EOL";                   break;
	    case IDIO_TOKEN_AMPERSAND:               t = "T/&";                     break;


	    case IDIO_VM_CODE_SHALLOW_ARGUMENT_REF:  t = "SHALLOW-ARGUMENT-REF";  break;
	    case IDIO_VM_CODE_PREDEFINED:            t = "PREDEFINED";            break;
	    case IDIO_VM_CODE_DEEP_ARGUMENT_REF:     t = "DEEP-ARGUMENT-REF";     break;
	    case IDIO_VM_CODE_SHALLOW_ARGUMENT_SET:  t = "SHALLOW-ARGUMENT-SET";  break;
	    case IDIO_VM_CODE_DEEP_ARGUMENT_SET:     t = "DEEP-ARGUMENT-SET";     break;
	    case IDIO_VM_CODE_GLOBAL_REF:            t = "GLOBAL-REF";            break;
	    case IDIO_VM_CODE_CHECKED_GLOBAL_REF:    t = "CHECKED-GLOBAL-REF";    break;
	    case IDIO_VM_CODE_CHECKED_GLOBAL_FUNCTION_REF:    t = "CHECKED-GLOBAL-FUNCTION-REF";    break;
	    case IDIO_VM_CODE_GLOBAL_SET:            t = "GLOBAL-SET";            break;
	    case IDIO_VM_CODE_CONSTANT:              t = "CONSTANT";              break;
	    case IDIO_VM_CODE_ALTERNATIVE:           t = "ALTERNATIVE";           break;
	    case IDIO_VM_CODE_SEQUENCE:              t = "SEQUENCE";              break;
	    case IDIO_VM_CODE_TR_FIX_LET:            t = "TR-FIX-LET";            break;
	    case IDIO_VM_CODE_FIX_LET:               t = "FIX-LET";               break;
	    case IDIO_VM_CODE_PRIMCALL0:             t = "PRIMCALL0";             break;
	    case IDIO_VM_CODE_PRIMCALL1:             t = "PRIMCALL1";             break;
	    case IDIO_VM_CODE_PRIMCALL2:             t = "PRIMCALL2";             break;
	    case IDIO_VM_CODE_PRIMCALL3:             t = "PRIMCALL3";             break;
	    case IDIO_VM_CODE_FIX_CLOSURE:           t = "FIX-CLOSURE";           break;
	    case IDIO_VM_CODE_NARY_CLOSURE:          t = "NARY-CLOSURE";          break;
	    case IDIO_VM_CODE_TR_REGULAR_CALL:       t = "TR-REGULAR-CALL";       break;
	    case IDIO_VM_CODE_REGULAR_CALL:          t = "REGULAR-CALL";          break;
	    case IDIO_VM_CODE_STORE_ARGUMENT:        t = "STORE-ARGUMENT";        break;
	    case IDIO_VM_CODE_CONS_ARGUMENT:         t = "CONS-ARGUMENT";         break;
	    case IDIO_VM_CODE_ALLOCATE_FRAME:        t = "ALLOCATE-FRAME";        break;
	    case IDIO_VM_CODE_ALLOCATE_DOTTED_FRAME: t = "ALLOCATE-DOTTED-FRAME"; break;
	    case IDIO_VM_CODE_FINISH:                t = "FINISH";                break;
	    case IDIO_VM_CODE_PUSH_DYNAMIC:          t = "PUSH-DYNAMIC";          break;
	    case IDIO_VM_CODE_POP_DYNAMIC:           t = "POP-DYNAMIC";           break;
	    case IDIO_VM_CODE_DYNAMIC_REF:           t = "DYNAMIC-REF";           break;
	    case IDIO_VM_CODE_PUSH_HANDLER:          t = "PUSH-HANDLER";          break;
	    case IDIO_VM_CODE_POP_HANDLER:           t = "POP-HANDLER";           break;
	    case IDIO_VM_CODE_AND:                   t = "AND";                   break;
	    case IDIO_VM_CODE_OR:                    t = "OR";                    break;
	    case IDIO_VM_CODE_BEGIN:                 t = "BEGIN";                 break;
	    case IDIO_VM_CODE_EXPANDER:              t = "EXPANDER";              break;
	    case IDIO_VM_CODE_NOP:		     t = "NOP";			  break;
	    default:
		break;
	    }

	    if (NULL == t) {
		if (asprintf (&r, "C=%" PRIdPTR, v) == -1) {
		    return NULL;
		}
	    } else {
		if (asprintf (&r, "%s", t) == -1) {
		    return NULL;
		}
	    }
	}
	break;
    case IDIO_TYPE_CHARACTER_MARK:
	{
	    int c = IDIO_CHARACTER_VAL (o);
	    switch (c) {
	    case ' ':
		if (asprintf (&r, "#\\space") == -1) {
		    return NULL;
		}
		break;
	    case '\n':
		if (asprintf (&r, "#\\newline") == -1) {
		    return NULL;
		}
		break;
	    default:
		if (isprint (c)) {
		    if (asprintf (&r, "#\\%c", c) == -1) {
			return NULL;
		    }
		} else {
		    if (asprintf (&r, "#\\%#x", c) == -1) {
			return NULL;
		    }
		}
		break;
	    }
	    break;
	}
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (idio_type (o)) {
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
		if (asprintf (&r, "%" PRId64, IDIO_C_TYPE_INT64 (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_C_UINT64:
		if (asprintf (&r, "%" PRId64, IDIO_C_TYPE_UINT64 (o)) == -1) {
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
		if (asprintf (&r, "#C_p-%p{%p free=%d}", o, IDIO_C_TYPE_POINTER_P (o), IDIO_C_TYPE_POINTER_FREEP (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_STRING:
		r = idio_escape_string (IDIO_STRING_BLEN (o), IDIO_STRING_S (o));
		break;
	    case IDIO_TYPE_SUBSTRING:
		r = idio_escape_string (IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_S (o));
		break;
	    case IDIO_TYPE_SYMBOL:
		if (asprintf (&r, "%s", IDIO_SYMBOL_S (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_PAIR:
		/*
		  Technically a list (of pairs) should look like:

		  "(a . (b . (c . (d . nil))))"

		  but tradition dictates that we should flatten
		  the list to:

		  "(a b c d)"

		  hence the while loop which continues if the tail is
		  itself a pair
		*/
		{
		    if (idio_isa_symbol (IDIO_PAIR_H (o))) {
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
			    if (idio_isa_pair (IDIO_PAIR_T (o))) {
				IDIO_STRCAT_FREE (r, idio_as_string (idio_list_head (IDIO_PAIR_T (o)), depth - 1));
			    } else {
				IDIO_STRCAT_FREE (r, idio_as_string (IDIO_PAIR_T (o), depth - 1));
			    }
			    break;
			}
		    }
	    
		    if (asprintf (&r, "(") == -1) {
			return NULL;
		    }
		    while (1) {
			IDIO_STRCAT_FREE (r, idio_as_string (IDIO_PAIR_H (o), depth - 1));

			o = IDIO_PAIR_T (o);
			if (idio_type (o) != IDIO_TYPE_PAIR) {
			    if (idio_S_nil != o) {
				char *t = idio_as_string (o, depth - 1);
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
		if (depth > 0) {
		    for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			if (idio_S_nil != IDIO_ARRAY_AE (o, i) || 1) {
			    char *t = idio_as_string (IDIO_ARRAY_AE (o, i), depth - 1);
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
		    IDIO_STRCAT (r, "... ");
		}
		IDIO_STRCAT (r, ")");
		break;
	    case IDIO_TYPE_HASH:
		if (asprintf (&r, "{ ") == -1) {
		    return NULL;
		}
		if (depth > 0) {
		    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
			    char *t;
			    if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) {
				t = (char *) IDIO_HASH_HE_KEY (o, i);
			    } else {
				t = idio_as_string (IDIO_HASH_HE_KEY (o, i), depth - 1);
			    }
			    char *hes;
			    if (asprintf (&hes, "%s:", t) == -1) {
				if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
				    free (t);
				}
				free (r);
				return NULL;
			    }
			    if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
				free (t);
			    }
			    IDIO_STRCAT_FREE (r, hes);
			    if (IDIO_HASH_HE_VALUE (o, i)) {
				t = idio_as_string (IDIO_HASH_HE_VALUE (o, i), depth - 1);
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
		    if (asprintf (&r, "#CLOS{@%zd/%p}", IDIO_CLOSURE_CODE (o), IDIO_CLOSURE_ENV (o)) == -1) {
			return NULL;
		    }
		    break;
		}
	    case IDIO_TYPE_PRIMITIVE:
		if (asprintf (&r, "#PRIM{%s}", IDIO_PRIMITIVE_NAME (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_BIGNUM:
		{
		    r = idio_bignum_as_string (o);
		    break;
		}
	    case IDIO_TYPE_MODULE:
		{
		    if (asprintf (&r, "{module %10p", o) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT (r, " name=");
		    if (idio_S_nil == IDIO_MODULE_NAME (o)) {
			IDIO_STRCAT (r, "(nil)");
		    } else {
			IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_NAME (o), depth - 1));
		    }
		    if (depth > 0) {
			IDIO_STRCAT (r, " exports=");
			if (idio_S_nil == IDIO_MODULE_EXPORTS (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_EXPORTS (o), depth - 1));
			}
			IDIO_STRCAT (r, " imports=");
			if (idio_S_nil == IDIO_MODULE_IMPORTS (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_IMPORTS (o), 0));
			}
			IDIO_STRCAT (r, " symbols=");
			if (idio_S_nil == IDIO_MODULE_SYMBOLS (o)) {
			    IDIO_STRCAT (r, "(nil)");
			} else {
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_MODULE_SYMBOLS (o), depth - 1));
			}
		    }
		    IDIO_STRCAT (r, " }");
		    break;
		}
	    case IDIO_TYPE_FRAME:
		{
		    if (asprintf (&r, "{frame %10p f=%02x next=%p nargs=%zd", o, IDIO_FRAME_FLAGS (o), IDIO_FRAME_NEXT (o), IDIO_FRAME_NARGS (o)) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT (r, " args=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_FRAME_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, " }");
		    break;
		}
	    case IDIO_TYPE_HANDLE:
		if (asprintf (&r, "#H{%x\"%s\":%lld:%lld}", IDIO_HANDLE_FLAGS (o), IDIO_HANDLE_NAME (o), (unsigned long long) IDIO_HANDLE_LINE (o), (unsigned long long) IDIO_HANDLE_POS (o)) == -1) {
		    return NULL;
		}
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		{
		    if (asprintf (&r, "#ST{%p ", o) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_STRUCT_TYPE_NAME (o), 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_STRUCT_TYPE_PARENT (o), 1));
		    
		    IDIO stf = IDIO_STRUCT_TYPE_FIELDS (o);

		    idio_ai_t al = idio_array_size (stf);
		    idio_ai_t ai;
		    for (ai = 0; ai < al; ai++) {
			IDIO_STRCAT (r, " ");
			IDIO_STRCAT_FREE (r, idio_as_string (idio_array_get_index (stf, ai), 1));
		    }

		    IDIO_STRCAT (r, "}");
		}
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		{
		    if (asprintf (&r, "#SI{%p", o) == -1) {
			return NULL;
		    }

		    IDIO st = IDIO_STRUCT_INSTANCE_TYPE (o);
		    IDIO stf = IDIO_STRUCT_TYPE_FIELDS (st);
		    IDIO sif = IDIO_STRUCT_INSTANCE_FIELDS (o);

		    idio_ai_t al = idio_array_size (stf);
		    idio_ai_t ai;
		    for (ai = 0; ai < al; ai++) {
			IDIO_STRCAT (r, " ");
			IDIO_STRCAT_FREE (r, idio_as_string (idio_array_get_index (stf, ai), 1));
			IDIO_STRCAT (r, ":");
			IDIO_STRCAT_FREE (r, idio_as_string (idio_array_get_index (sif, ai), 1));
		    }

		    IDIO_STRCAT (r, "}");
		}
		break;
	    case IDIO_TYPE_THREAD:
		{
		    idio_ai_t sp = idio_array_size (IDIO_THREAD_STACK (o));
		    if (asprintf (&r, "#T{%p pc=%4zd sp/top=%2zd/",
				  o,
				  IDIO_THREAD_PC (o),
				  sp) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (idio_array_top (IDIO_THREAD_STACK (o)), 1));
		    IDIO_STRCAT (r, " val=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_VAL (o), 2));
		    IDIO_STRCAT (r, " func=");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_FUNC (o), 1));
		    if (1 == depth) {
			IDIO env = IDIO_THREAD_ENV (o);

			if (idio_S_nil == env) {
			    IDIO_STRCAT (r, " env=nil");
			} else {
			    char *es;
			    if (asprintf (&es, " env=%p ", env) == -1) {
				return NULL;
			    }
			    IDIO_STRCAT_FREE (r, es);
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_FRAME_ARGS (env), 1));
			}
		    }
		    if (depth > 1) {
			IDIO_STRCAT (r, " env=");
			IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_ENV (o), 1));
			if (depth > 2) {
			    IDIO_STRCAT (r, " reg1=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_REG1 (o), 1));
			    IDIO_STRCAT (r, " reg2=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_REG2 (o), 1));
			    IDIO_STRCAT (r, " input_handle=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_INPUT_HANDLE (o), 1));
			    IDIO_STRCAT (r, " output_handle=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_OUTPUT_HANDLE (o), 1));
			    IDIO_STRCAT (r, " error_handle=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_ERROR_HANDLE (o), 1));
			    IDIO_STRCAT (r, " module=");
			    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_THREAD_MODULE (o), 1));
			}
		    }
		    IDIO_STRCAT (r, "}");
		}
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		{
		    if (asprintf (&r, "#CTD{%10p}", IDIO_C_TYPEDEF_SYM (o)) == -1) {
			return NULL;
		    }
		    break;
		}
	    case IDIO_TYPE_C_STRUCT:
		{
		    if (asprintf (&r, "c_struct %10p { ", o) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT (r, "\n\tfields: ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_STRUCT_FIELDS (o), depth - 1));

		    IDIO mh = IDIO_C_STRUCT_METHODS (o);
	    
		    IDIO_STRCAT (r, "\n\tmethods: ");
		    if (idio_S_nil != mh) {
			for (i = 0; i < IDIO_HASH_SIZE (mh); i++) {
			    if (idio_S_nil != IDIO_HASH_HE_KEY (mh, i)) {
				char *t = idio_as_string (IDIO_HASH_HE_KEY (mh, i), depth - 1);
				char *hes;
				if (asprintf (&hes, "\n\t%10s:", t) == -1) {
				    free (t);
				    free (r);
				    return NULL;
				}
				free (t);
				IDIO_STRCAT_FREE (r, hes);
				if (IDIO_HASH_HE_VALUE (mh, i)) {
				    t = idio_as_string (IDIO_HASH_HE_VALUE (mh, i), depth - 1);
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
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_STRUCT_FRAME (o), depth - 1));
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
	    case IDIO_TYPE_C_FFI:
		{
		    char *t = idio_as_string (IDIO_C_FFI_NAME (o), depth - 1);
		    if (asprintf (&r, "#F_CFFI{%s ", t) == -1) {
			free (t);
			return NULL;
		    }
		    free (t);
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_SYMBOL (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_RESULT (o), depth - 1));
		    IDIO_STRCAT (r, " ");
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_C_FFI_NAME (o), depth - 1));
		    IDIO_STRCAT (r, " }");
		    break;
		}
	    case IDIO_TYPE_OPAQUE:
		{
		    if (asprintf (&r, "#O{%10p ", IDIO_OPAQUE_P (o)) == -1) {
			return NULL;
		    }
		    IDIO_STRCAT_FREE (r, idio_as_string (IDIO_OPAQUE_ARGS (o), depth - 1));
		    IDIO_STRCAT (r, "}");
		    break;
		}
	    default:
		{
		    size_t n = strnlen ((char *) o, 40);
		    if (40 == n) {
			if (asprintf (&r, "#?{%10p}", o) == -1) {
			    return NULL;
			}
		    } else {
			if (asprintf (&r, "#?s{%s}", (char *) o) == -1) {
			    return NULL;
			}
		    }
		}
		break;
	    }
	}
	break;
    default:
	if (asprintf (&r, "#??{%10p}", o) == -1) {
	    return NULL;
	}
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
char *idio_display_string (IDIO o)
{
    char *r;
    
    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	r = idio_as_string (o, 4);
	break;
    case IDIO_TYPE_CHARACTER_MARK:
	if (asprintf (&r, "%c", (char) IDIO_CHARACTER_VAL (o)) == -1) {
	    return NULL;
	}
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
		r = idio_as_string (o, 4);
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
	    case IDIO_TYPE_PRIMITIVE:
	    case IDIO_TYPE_BIGNUM:
	    case IDIO_TYPE_MODULE:
	    case IDIO_TYPE_FRAME:
	    case IDIO_TYPE_HANDLE:
	    case IDIO_TYPE_STRUCT_TYPE:
	    case IDIO_TYPE_STRUCT_INSTANCE:
	    case IDIO_TYPE_THREAD:
	    case IDIO_TYPE_C_TYPEDEF:
	    case IDIO_TYPE_C_STRUCT:
	    case IDIO_TYPE_C_INSTANCE:
	    case IDIO_TYPE_C_FFI:
	    case IDIO_TYPE_OPAQUE:
		r = idio_as_string (o, 4);
		break;
	    default:
		if (asprintf (&r, "#?{%10p}", o) == -1) {
		    return NULL;
		}
		break;
	    }
	}
	break;
    default:
	if (asprintf (&r, "type %d n/k", o->type) == -1) {
	    return NULL;
	}
	break;
    }
    
    return r;
}

void idio_as_flat_string (IDIO o, char **argv, int *i)
{
    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_CHARACTER_MARK:
	{
	    argv[(*i)++] = idio_as_string (o, 1);
	}
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    switch (idio_type (o)) {
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
		{
		    argv[(*i)++] = idio_as_string (o, 1);
		}
		break;
	    case IDIO_TYPE_STRING:
		if (asprintf (&argv[(*i)++], "%.*s", (int) IDIO_STRING_BLEN (o), IDIO_STRING_S (o)) == -1) {
		    return;
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (asprintf (&argv[(*i)++], "%.*s", (int) IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_S (o)) == -1) {
		    return;
		}
		break;
	    case IDIO_TYPE_SYMBOL:
		if (asprintf (&argv[(*i)++], "%s", IDIO_SYMBOL_S (o)) == -1) {
		    return;
		}
		break;
	    case IDIO_TYPE_PAIR:
	    case IDIO_TYPE_ARRAY:
	    case IDIO_TYPE_HASH:
	    case IDIO_TYPE_BIGNUM:
		{
		    argv[(*i)++] = idio_as_string (o, 1);
		}
		break;
	    case IDIO_TYPE_C_POINTER:
	    case IDIO_TYPE_CLOSURE:
	    case IDIO_TYPE_PRIMITIVE:
	    case IDIO_TYPE_MODULE:
	    case IDIO_TYPE_FRAME:
	    case IDIO_TYPE_HANDLE:
	    case IDIO_TYPE_STRUCT_TYPE:
	    case IDIO_TYPE_STRUCT_INSTANCE:
	    case IDIO_TYPE_THREAD:
	    case IDIO_TYPE_C_TYPEDEF:
	    case IDIO_TYPE_C_STRUCT:
	    case IDIO_TYPE_C_INSTANCE:
	    case IDIO_TYPE_C_FFI:
	    case IDIO_TYPE_OPAQUE:
	    default:
		idio_warning_message ("unexpected object type: %s", idio_type2string (o));
		break;
	    }
	}
	break;
    default:
	idio_warning_message ("unexpected object type: %s", idio_type2string (o));
	break;
    }
}

/* 
 * Basic map -- only one list and the function must be a primitive so
 * we can call it directly.  We can't apply a closure as apply only
 * changes the PC, it doesn't actually do anything!
 *
 * The real map is defined early on in bootstrap.
 */
IDIO_DEFINE_PRIMITIVE2 ("%map1", map1, (IDIO fn, IDIO list))
{
    IDIO_ASSERT (fn);
    IDIO_ASSERT (list);

    IDIO_VERIFY_PARAM_TYPE (primitive, fn);
    IDIO_VERIFY_PARAM_TYPE (list, list);

    IDIO r = idio_S_nil;

    /* idio_debug ("map: in: %s\n", list); */

    while (idio_S_nil != list) {
	r = idio_pair (IDIO_PRIMITIVE_F (fn) (IDIO_PAIR_H (list)), r);
	list = IDIO_PAIR_T (list);
    }

    r = idio_list_reverse (r);
    /* idio_debug ("map => %s\n", r); */
    return r;
}

IDIO idio_list_mapcar (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_H (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_mapcdr (IDIO l)
{
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    IDIO r = idio_S_nil;

    while (idio_S_nil != l) {
	IDIO e = IDIO_PAIR_H (l);
	if (idio_isa_pair (e)) {
	    r = idio_pair (IDIO_PAIR_T (e), r);
	} else {
	    r = idio_pair (idio_S_nil, r);
	}
	l = IDIO_PAIR_T (l);
	IDIO_TYPE_ASSERT (list, l);
    }

    return idio_list_reverse (r);
}

IDIO idio_list_memq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    /* fprintf (stderr, "memq: k=%s in l=%s\n", idio_as_string (k, 1), idio_as_string (l, 4));   */

    if (idio_S_true == k &&
	idio_S_nil == l) {
	sleep (0);
    }
    while (idio_S_nil != l) {
	if (idio_eqp (k, IDIO_PAIR_H (l))) {
	    return l;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2 ("memq", memq, (IDIO k, IDIO l))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_memq (k, l);
}

IDIO idio_list_assq (IDIO k, IDIO l)
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);
    IDIO_TYPE_ASSERT (list, l);

    /* fprintf (stderr, "assq: k=%s in l=%s\n", idio_as_string (k, 1), idio_as_string (idio_list_mapcar (l), 4)); */
    
    while (idio_S_nil != l) {
	IDIO p = IDIO_PAIR_H (l);

	if (idio_S_nil == p) {
	    return idio_S_false;
	}

	if (! idio_isa_pair (p)) {
	    fprintf (stderr, "assq: p %s is not a pair in l %s\n", idio_as_string (p, 1), idio_as_string (l, 2));
	}

	if (idio_eqp (k, IDIO_PAIR_H (p))) {
	    return p;
	}
	l = IDIO_PAIR_T (l);
    }

    return idio_S_false;
}

IDIO_DEFINE_PRIMITIVE2 ("assq", assq, (IDIO k, IDIO l))
{
    IDIO_ASSERT (k);
    IDIO_ASSERT (l);

    IDIO_VERIFY_PARAM_TYPE (list, l);

    return idio_list_assq (k, l);
}

IDIO idio_list_set_difference (IDIO set1, IDIO set2)
{
    if (idio_isa_pair (set1)) {
	if (idio_S_false != idio_list_memq (IDIO_PAIR_H (set1), set2)) {
	    return idio_list_set_difference (IDIO_PAIR_T (set1), set2);
	} else {
	    return idio_pair (IDIO_PAIR_H (set1),
			      idio_list_set_difference (IDIO_PAIR_T (set1), set2));
	}
    } else {
	if (idio_S_nil != set1) {
	    fprintf (stderr, "set1=%s\n", idio_as_string (set1, 1));
	    IDIO_C_ASSERT (0);
	}
	return idio_S_nil;
    }
}

void idio_dump (IDIO o, int detail)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_CHARACTER_MARK:
	break;
    case IDIO_TYPE_POINTER_MARK:
	{
	    if (detail > 0) {
		IDIO_FPRINTF (stderr, "%10p ", o);
		if (detail > 1) {
		    IDIO_FPRINTF (stderr, "-> %10p ", o->next);
		}
		IDIO_FPRINTF (stderr, "t=%2d/%4.4s f=%2x ", o->type, idio_type2string (o), o->flags);
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
		IDIO_FPRINTF (stderr, "n=");
		break;
	    case IDIO_TYPE_C_POINTER:
		IDIO_FPRINTF (stderr, "p=");
		break;
	    case IDIO_TYPE_STRING:
		if (detail) {
		    IDIO_FPRINTF (stderr, "blen=%d s=", IDIO_STRING_BLEN (o));
		}
		break;
	    case IDIO_TYPE_SUBSTRING:
		if (detail) {
		    IDIO_FPRINTF (stderr, "blen=%d parent=%10p subs=", IDIO_SUBSTRING_BLEN (o), IDIO_SUBSTRING_PARENT (o));
		}
		break;
	    case IDIO_TYPE_SYMBOL:
		IDIO_FPRINTF (stderr, "sym=");
		break;
	    case IDIO_TYPE_PAIR:
		if (detail > 1) {
		    IDIO_FPRINTF (stderr, "head=%10p tail=%10p p=", IDIO_PAIR_H (o), IDIO_PAIR_T (o));
		}
		break;
	    case IDIO_TYPE_ARRAY:
		if (detail) {
		    IDIO_FPRINTF (stderr, "size=%d/%d \n", IDIO_ARRAY_USIZE (o), IDIO_ARRAY_ASIZE (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
			    if (idio_S_nil != IDIO_ARRAY_AE (o, i) ||
				detail > 3) {
				char *s = idio_as_string (IDIO_ARRAY_AE (o, i), 4);
				IDIO_FPRINTF (stderr, "\t%3d: %10p %10s\n", i, IDIO_ARRAY_AE (o, i), s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_HASH:
		if (detail) {
		    IDIO_FPRINTF (stderr, "hsize=%d hmask=%x\n", IDIO_HASH_SIZE (o), IDIO_HASH_MASK (o));
		    if (detail > 1) {
			size_t i;
			for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			    if (idio_S_nil == IDIO_HASH_HE_KEY (o, i)) {
				continue;
			    } else {
				char *s;
				if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) {
				    s = (char *) IDIO_HASH_HE_KEY (o, i);
				} else {
				    s = idio_as_string (IDIO_HASH_HE_KEY (o, i), 4);
				}
				if (detail & 0x4) {
				    IDIO_FPRINTF (stderr, "\t%30s : ", s);
				} else {
				    IDIO_FPRINTF (stderr, "\t%3d: k=%10p v=%10p n=%3d %10s : ",
							i,
							IDIO_HASH_HE_KEY (o, i),
							IDIO_HASH_HE_VALUE (o, i),
							IDIO_HASH_HE_NEXT (o, i),
							s);
				}
				if (! (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
				    free (s);
				}
				if (IDIO_HASH_HE_VALUE (o, i)) {
				    s = idio_as_string (IDIO_HASH_HE_VALUE (o, i), 4);
				} else {
				    if (asprintf (&s, "-") == -1) {
					break;
				    }
				}
				IDIO_FPRINTF (stderr, "%-10s\n", s);
				free (s);
			    }
			}
		    }
		}
		break;
	    case IDIO_TYPE_CLOSURE:
		break;
	    case IDIO_TYPE_PRIMITIVE:
		break;
	    case IDIO_TYPE_BIGNUM:
		break;
	    case IDIO_TYPE_MODULE:
		break;
	    case IDIO_TYPE_FRAME:
		break;
	    case IDIO_TYPE_HANDLE:
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		break;
	    case IDIO_TYPE_THREAD:
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		break;
	    case IDIO_TYPE_C_STRUCT:
		break;
	    case IDIO_TYPE_C_INSTANCE:
		break;
	    case IDIO_TYPE_C_FFI:
		break;
	    case IDIO_TYPE_OPAQUE:
		break;
	    default:
		IDIO_FPRINTF (stderr, "o=%#p\n", o);
		break;
	    }
	}
	break;
    default:
	IDIO_FPRINTF (stderr, "v=n/k o=%#p o&3=%x F=%x C=%x P=%x\n", o, (intptr_t) o & 3, IDIO_TYPE_FIXNUM_MARK, IDIO_TYPE_CONSTANT_MARK, IDIO_TYPE_POINTER_MARK);
	IDIO_C_ASSERT (0);
	break;
    }	

    if (detail &&
	!(detail & 0x4)) {
	char *s = idio_as_string (o, detail);
	fprintf (stderr, "%s", s);
	free (s);
    }

    fprintf (stderr, "\n");
}

void idio_debug (const char *fmt, IDIO o)
{
    IDIO_C_ASSERT (fmt);
    IDIO_ASSERT (o);

    char *os = idio_as_string (o, 1);
    fprintf (stderr, fmt, os);
    free (os);
}

void idio_init_util ()
{
}

void idio_util_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (nullp);
    IDIO_ADD_PRIMITIVE (booleanp);
    IDIO_ADD_PRIMITIVE (not);
    IDIO_ADD_PRIMITIVE (eqp);
    IDIO_ADD_PRIMITIVE (eqvp);
    /* IDIO_ADD_PRIMITIVE (equalp); */
    IDIO_ADD_PRIMITIVE (zerop);
    IDIO_ADD_PRIMITIVE (map1);
    IDIO_ADD_PRIMITIVE (memq);
    IDIO_ADD_PRIMITIVE (assq);
}

void idio_final_util ()
{
}

/* Local Variables: */
/* mode: C/l */
/* buffer-file-coding-system: undecided-unix */
/* End: */
