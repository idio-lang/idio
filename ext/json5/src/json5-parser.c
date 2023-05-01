/*
 * Copyright (c) 2021, 2023 Ian Fitchet <idf(at)idio-lang.org>
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
 * json5-parser.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "json5-token.h"
#include "json5-unicode.h"
#include "json5-parser.h"
#include "utf8.h"

#define JSON5_CHUNK 1024

/*
 * We've overridden the JSON5 meaning of "value" with punctuator and
 * identifier when a valid JSON5Value is null, boolean, string,
 * number, object or array.
 *
 * In the case of object and array we don't even have those yet as
 * we're still running through the token stream!
 */
int json5_valid_value (json5_token_t *ct) {
    int valid = 0;
    switch (ct->type) {
    case JSON5_TOKEN_IDENTIFIER:
	switch (ct->value->type) {
	case JSON5_VALUE_NULL:
	    valid = 1;
	    break;
	case JSON5_VALUE_BOOLEAN:
	    switch (ct->value->u.l) {
	    case JSON5_LITERAL_TRUE:
	    case JSON5_LITERAL_FALSE:
		valid = 1;
		break;
	    default:
		/* coding error? */
		valid = 0;
		break;
	    }
	    break;
	default:
	    valid = 0;
	}
	break;
    case JSON5_TOKEN_PUNCTUATOR:
	if (JSON5_PUNCTUATOR_LBRACE == ct->value->u.p) {
	    valid = 1;
	} else if (JSON5_PUNCTUATOR_LBRACKET == ct->value->u.p) {
	    valid = 1;
	}
	break;
    case JSON5_TOKEN_STRING:
	valid = 1;
	break;
    case JSON5_TOKEN_NUMBER:
	switch (ct->value->u.n->type) {
	case JSON5_NUMBER_INFINITY:
	case JSON5_NUMBER_NEG_INFINITY:
	case JSON5_NUMBER_NAN:
	case JSON5_NUMBER_NEG_NAN:
	case ECMA_NUMBER_INTEGER:
	case ECMA_NUMBER_FLOAT:
	    valid = 1;
	    break;
	}
	break;
    default:
	valid = 0;
	break;
    }

    return valid;
}

static json5_token_t *json5_parse_token (json5_token_t *ft, json5_token_t *ct, json5_value_t **valuepp, int toplevel);
static json5_token_t *json5_parse_token_stream (json5_token_t *ct, json5_value_t **valuepp);

static json5_token_t *json5_parse_array (json5_token_t *ft, json5_token_t *ct, json5_value_t **valuepp, int toplevel)
{
    enum pending_array_states {
	VALUE,
	COMMA_RBRACKET,		/* , or ] */
    };
    enum pending_array_states pending = VALUE;

    size_t a_start = ct->start;
    ct = ct->next;

    *valuepp = (json5_value_t *) malloc (sizeof (json5_value_t));
    (*valuepp)->type = JSON5_VALUE_ARRAY;
    (*valuepp)->u.a = NULL;

    json5_array_t *ae = NULL;

    int done = 0;
    for (; NULL != ct;) {
	if (JSON5_TOKEN_PUNCTUATOR == ct->type &&
	    JSON5_PUNCTUATOR_RBRACKET == ct->value->u.p) {
	    done = 1;
	    ct = ct->next;
	    break;
	}

	switch (pending) {
	case VALUE:
	    if (NULL == ae) {
		(*valuepp)->u.a = ae = (json5_array_t *) malloc (sizeof (json5_array_t));
	    } else {
		ae->next = (json5_array_t *) malloc (sizeof (json5_array_t));
		ae = ae->next;
	    }
	    ae->next = NULL;
	    ae->element = NULL;

	    if (! json5_valid_value (ct)) {
		size_t ct_start = ct->start;
		if (toplevel) {
		    json5_free_value (*valuepp);
		}
		json5_token_free_remaining (ft);

		/*
		 * Test Case: json5-errors/parse-array-invalid-value.idio
		 *
		 * json5/parse-string "[ while ]"
		 */
		json5_error_printf ("json5/parse array at %zd: invalid value at %zd", a_start, ct_start);

		/* notreached */
		return NULL;
	    }

	    ct = json5_parse_token (ft, ct, &(ae->element), 0);

	    if (NULL == ct) {
		if (toplevel) {
		    json5_free_value (*valuepp);
		}
		json5_token_free_remaining (ft);

		/*
		 * Test Case: json5-errors/parse-unterminated-array-1.idio
		 *
		 * json5/parse-string "[ true"
		 */
		json5_error_printf ("json5/parse array at %zd: no more tokens: expected ']'", a_start);

		/* notreached */
		return NULL;
	    }

	    pending = COMMA_RBRACKET;
	    break;
	case COMMA_RBRACKET:
	    if (JSON5_TOKEN_PUNCTUATOR != ct->type ||
		JSON5_PUNCTUATOR_COMMA != ct->value->u.p) {
		size_t ct_start = ct->start;
		if (toplevel) {
		    json5_free_value (*valuepp);
		}
		json5_token_free_remaining (ft);

		/*
		 * Test Cases:
		 *
		 *   json5-errors/parse-array-bad-sequence-1.idio
		 *   json5-errors/parse-array-bad-sequence-2.idio
		 *
		 * json5/parse-string "[ true false ]"
		 * json5/parse-string "[ true [ false ] ]"
		 */
		json5_error_printf ("json5/parse array at %zd: expected ',' or ']' at %zd", a_start, ct_start);

		/* notreached */
		return NULL;
	    }
	    ct = ct->next;
	    pending = VALUE;
	    break;
	}
    }

    if (0 == done) {
	/*
	 * Test Case: json5-errors/parse-unterminated-array-2.idio
	 *
	 * json5/parse-string "["
	 */
	if (toplevel) {
	    json5_free_value (*valuepp);
	}
	if (NULL == ct) {
	    json5_token_free_remaining (ft);

	    json5_error_printf ("json5/parse array at %zd: expected ']' (no more tokens)", a_start);
	} else {
	    size_t ct_start = ct->start;
	    json5_token_free_remaining (ft);

	    json5_error_printf ("json5/parse array at %zd: expected ']' at %zd", a_start, ct_start);
	}

	/* notreached */
	return NULL;
    }

    return ct;
}

static json5_token_t *json5_parse_object (json5_token_t *ft, json5_token_t *ct, json5_value_t **valuepp, int toplevel)
{
    enum pending_object_states {
	NAME,
	COLON,
	VALUE,
	COMMA_RBRACE,			/* , or } */
    };
    enum pending_object_states pending = NAME;

    size_t o_start = ct->start;
    ct = ct->next;

    *valuepp = (json5_value_t *) malloc (sizeof (json5_value_t));
    (*valuepp)->type = JSON5_VALUE_OBJECT;
    (*valuepp)->u.o = NULL;

    json5_object_t *object = NULL;

    int done = 0;
    for (; NULL != ct;) {
	if (JSON5_TOKEN_PUNCTUATOR == ct->type &&
	    JSON5_PUNCTUATOR_RBRACE == ct->value->u.p) {
	    /*
	     * Have we closed too soon?
	     */
	    switch (pending) {
	    case NAME:
	    case COMMA_RBRACE:
		break;
	    default:
		{
		    size_t ct_start = ct->start;
		    if (toplevel) {
			json5_free_value (*valuepp);
		    }
		    json5_token_free_remaining (ft);

		    /*
		     * Test Cases:
		     *
		     *   json5-errors/parse-object-MemberName-only.idio
		     *   json5-errors/parse-object-MemberName-colon-only.idio
		     *
		     * json5/parse-string "{ true }"
		     * json5/parse-string "{ true: }"
		     */
		    json5_error_printf ("json5/parse object at %zd: expected more tokens at %zd", o_start, ct_start);

		    /* notreached */
		    return NULL;
		}
		break;
	    }
	    done = 1;
	    ct = ct->next;
	    break;
	}

	switch (pending) {
	case NAME:
	    if (NULL == object) {
		(*valuepp)->u.o = object = (json5_object_t *) malloc (sizeof (json5_object_t));
	    } else {
		object->next = (json5_object_t *) malloc (sizeof (json5_object_t));
		object = object->next;
	    }
	    object->next = NULL;
	    object->name = NULL;
	    object->value = NULL;

	    if (JSON5_TOKEN_IDENTIFIER == ct->type) {
		switch (ct->value->type) {
		case JSON5_VALUE_NULL:
		case JSON5_VALUE_BOOLEAN:
		    object->type = JSON5_MEMBER_LITERAL;
		    break;
		case JSON5_VALUE_IDENTIFIER:
		    object->type = JSON5_MEMBER_IDENTIFIER;
		    break;
		default:
		    /*
		     * Test Case: coding error?
		     */
		    json5_error_printf ("json5/parse object at %zd: MemberName: expected ??", o_start);

		    /* notreached */
		    return NULL;
		    break;
		}
		object->name = ct->value;
		ct->value = NULL;
	    } else if (JSON5_TOKEN_STRING == ct->type) {
		object->type = JSON5_MEMBER_STRING;
		object->name = ct->value;
		ct->value = NULL;
	    } else {
		if (toplevel) {
		    json5_free_value (*valuepp);
		}
		json5_token_free_remaining (ft);

		/*
		 * Test Case: json5-errors/parse-object-bad-MemberName-type.idio
		 *
		 * json5/parse-string "{ 10: false }"
		 */
		json5_error_printf ("json5/parse object at %zd: MemberName: expected ECMAIdentifier|string", o_start);

		/* notreached */
		return NULL;
	    }
	    ct = ct->next;
	    pending = COLON;
	    break;
	case COLON:
	    if (JSON5_TOKEN_PUNCTUATOR != ct->type ||
		JSON5_PUNCTUATOR_COLON != ct->value->u.p) {
		if (toplevel) {
		    json5_free_value (*valuepp);
		}
		json5_token_free_remaining (ft);

		/*
		 * Test Case: json5-errors/parse-object-no-colon.idio
		 *
		 * json5/parse-string "{ true false }"
		 */
		json5_error_printf ("json5/parse object at %zd: expected ':'", o_start);

		/* notreached */
		return NULL;
	    }
	    ct = ct->next;
	    pending = VALUE;
	    break;
	case VALUE:
	    {
		if (! json5_valid_value (ct)) {
		    size_t ct_start = ct->start;
		    if (toplevel) {
			json5_free_value (*valuepp);
		    }
		    json5_token_free_remaining (ft);

		    /*
		     * Test Case: json5-errors/parse-object-invalid-value.idio
		     *
		     * json5/parse-string "{ true: while }"
		     */
		    json5_error_printf ("json5/parse object at %zd: invalid value at %zd", o_start, ct_start);

		    /* notreached */
		    return NULL;
		}

		ct = json5_parse_token (ft, ct, &(object->value), 0);

		if (NULL == ct) {
		    if (toplevel) {
			json5_free_value (*valuepp);
		    }
		    json5_token_free_remaining (ft);

		    /*
		     * Test Case: json5-errors/parse-unterminated-object-1.idio
		     *
		     * json5/parse-string "{ true: false"
		     */
		    json5_error_printf ("json5/parse object at %zd: expected '}'", o_start);

		    /* notreached */
		    return NULL;
		}
	    }
	    pending = COMMA_RBRACE;
	    break;
	case COMMA_RBRACE:
	    if (JSON5_TOKEN_PUNCTUATOR != ct->type ||
		(JSON5_PUNCTUATOR_COMMA != ct->value->u.p &&
		 JSON5_PUNCTUATOR_RBRACE != ct->value->u.p)) {
		if (toplevel) {
		    json5_free_value (*valuepp);
		}
		json5_token_free_remaining (ft);

		/*
		 * Test Case: json5-errors/parse-object-bad-sequence.idio
		 *
		 * json5/parse-string "{ true: false true: false }"
		 */
		json5_error_printf ("json5/parse object at %zd: expected ',' or '}'", o_start);

		/* notreached */
		return NULL;
	    }
	    ct = ct->next;
	    pending = NAME;
	    break;
	}
    }

    if (0 == done) {
	/*
	 * Test Case: json5-errors/parse-unterminated-object-2.idio
	 *
	 * json5/parse-string "{"
	 */
	if (toplevel) {
	    json5_free_value (*valuepp);
	}
	if (NULL == ct) {
	    json5_token_free_remaining (ft);

	    json5_error_printf ("json5/parse object at %zd: expected '}' (no more tokens)", o_start);
	} else {
	    size_t ct_start = ct->start;
	    json5_token_free_remaining (ft);

	    json5_error_printf ("json5/parse object at %zd: expected '}' at %zd", o_start, ct_start);
	}

	/* notreached */
	return NULL;
    }

    return ct;
}

/*
 * ft	- first token (used for cleanup)
 * ct	- current token
 */
static json5_token_t *json5_parse_token (json5_token_t *ft, json5_token_t *ct, json5_value_t **valuepp, int toplevel)
{
    switch (ct->type) {
    case JSON5_TOKEN_ROOT:
	/*
	 * Test Case: coding error?
	 */
	json5_error_printf ("json5/parse: unexpected root token");

	/* notreached */
	return NULL;
    case JSON5_TOKEN_IDENTIFIER:
	switch (ct->value->type) {
	case JSON5_VALUE_NULL:
	    *valuepp = ct->value;
	    ct->value = NULL;
	    break;
	case JSON5_VALUE_BOOLEAN:
	    switch (ct->value->u.l) {
	    case JSON5_LITERAL_TRUE:
	    case JSON5_LITERAL_FALSE:
		*valuepp = ct->value;
		ct->value = NULL;
		break;
	    default:
		/*
		 * Test Case: coding error?
		 */
		json5_error_printf ("json5/parse: unexpected literal type %d", ct->value->u.l);

		/* notreached */
		return NULL;
	    }
	    break;
	case JSON5_VALUE_IDENTIFIER:
	    /*
	     * Test Case: ??
	     */
	    json5_error_printf ("json5/parse at %zd: unexpected identifier", ct->start);

	    /* notreached */
	    return NULL;
	    break;
	default:
	    /*
	     * Test Case: coding error?
	     */
	    json5_error_printf ("json5/parse: unexpected identifier type %d", ct->value->type);

	    /* notreached */
	    return NULL;
	}
	ct = ct->next;
	break;
    case JSON5_TOKEN_PUNCTUATOR:
	if (JSON5_PUNCTUATOR_LBRACE == ct->value->u.p) {
	    ct = json5_parse_object (ft, ct, valuepp, toplevel);
	} else if (JSON5_PUNCTUATOR_LBRACKET == ct->value->u.p) {
	    ct = json5_parse_array (ft, ct, valuepp, toplevel);
	} else {
	    char *p;
	    switch (ct->value->u.p) {
	    case JSON5_PUNCTUATOR_RBRACE:
		p = "}";
		break;
	    case JSON5_PUNCTUATOR_RBRACKET:
		p = "]";
		break;
	    case JSON5_PUNCTUATOR_COLON:
		p = ":";
		break;
	    case JSON5_PUNCTUATOR_COMMA:
		p = ",";
		break;
	    default:
		/*
		 * Test Case: coding error?
		 */
		json5_error_printf ("json5/parse: unexpected punctuator type %d ", ct->value->u.p);

		/* notreached */
		return NULL;
	    }

	    size_t ct_start = ct->start;
	    json5_token_free_remaining (ft);

	    /*
	     * Test Cases:
	     *
	     *   json5-errors/parse-bad-punctuation-rbrace.idio
	     *   json5-errors/parse-bad-punctuation-rbracket.idio
	     *   json5-errors/parse-bad-punctuation-colon.idio
	     *   json5-errors/parse-bad-punctuation-comma.idio
	     *
	     * json5/parse-string ", true"
	     */
	    json5_error_printf ("json5/parse: unexpected punctuation at %zd: '%s'", ct_start, p);

	    /* notreached */
	    return NULL;
	}
	break;
    case JSON5_TOKEN_STRING:
	*valuepp = ct->value;
	ct->value = NULL;
	ct = ct->next;
	break;
    case JSON5_TOKEN_NUMBER:
	switch (ct->value->u.n->type) {
	case JSON5_NUMBER_INFINITY:
	    break;
	case JSON5_NUMBER_NEG_INFINITY:
	    break;
	case JSON5_NUMBER_NAN:
	    break;
	case JSON5_NUMBER_NEG_NAN:
	    break;
	case ECMA_NUMBER_INTEGER:
	    break;
	case ECMA_NUMBER_FLOAT:
	    break;
	default:
	    /*
	     * Test Case: coding error?
	     */
	    json5_error_printf ("json5/parse: unexpected number type %d", ct->value->u.n->type);

	    /* notreached */
	    return NULL;
	}
	*valuepp = ct->value;
	ct->value = NULL;
	ct = ct->next;
	break;
    default:
	/*
	 * Test Case: coding error?
	 */
	json5_error_printf ("json5/parse: unexpected token type %d", ct->type);

	/* notreached */
	return NULL;
    }

    return ct;
}

static json5_token_t *json5_parse_token_stream (json5_token_t *root, json5_value_t **valuepp)
{
    json5_token_t *ct = root;

    if (NULL == ct ||
	JSON5_TOKEN_ROOT != ct->type) {
	/*
	 * Test Case: coding error?
	 */
	json5_error_printf ("json5/parse: invalid token stream");

	/* notreached */
	return NULL;
    }

    ct = ct->next;

    if (NULL == ct) {
	json5_free_value (*valuepp);
	json5_token_free_remaining (root);

	/*
	 * Test Case: json5-errors/parse-empty-string.idio
	 *
	 * json5/parse-string ""
	 */
	json5_error_printf ("json5/parse: empty token stream");

	/* notreached */
	return NULL;
    }

    ct = json5_parse_token (root, ct, valuepp, 1);

    return ct;
}

json5_value_t *json5_parse (json5_token_t *root)
{
    /*
     * XXX assign with NULL so cover the implied json5_free_value
     * (*valuepp) on error
     */
    json5_value_t *valuep = NULL;

    json5_token_t *ct = json5_parse_token_stream (root, &valuep);

    if (NULL != ct) {
	size_t ct_start = ct->start;
	json5_free_value (valuep);
	json5_token_free_remaining (root);

	/*
	 * Test Case: json5-errors/parse-extra-tokens.idio
	 *
	 * json5/parse-string "true false"
	 */
	json5_error_printf ("json5/parse: extra tokens at %zd", ct_start);

	/* notreached */
	return NULL;
    }

    return valuep;
}

json5_value_t *json5_parse_string (json5_unicode_string_t *so)
{
    json5_token_t *root = json5_tokenize_string (so);

    /*
     * We free {so} here, on behalf of the caller, because
     * json5_parse() can exit and doesn't know about {so} (to free
     * it).
     */
    free (so->s);
    free (so);

    json5_value_t *valuep = json5_parse (root);

    json5_token_free_remaining (root);

    return valuep;
}

json5_value_t *json5_parse_string_C (char *s_C, size_t s_Clen)
{
    json5_token_t *root = json5_tokenize_string_C (s_C, s_Clen);

    json5_value_t *valuep = json5_parse (root);

    json5_token_free_remaining (root);

    return valuep;
}

json5_value_t *json5_parse_fd (int fd)
{
    size_t alen = JSON5_CHUNK;
    size_t buf_rem = alen;
    char *s = malloc (buf_rem);

    size_t slen = 0;
    char *buf = s + slen;

    for (;;) {
	ssize_t read_r = read (fd, buf, buf_rem);

	if (-1 == read_r) {
	    if (EINTR == errno) {
		continue;
	    }

	    perror ("read");
	    return NULL;
	}

	if (0 == read_r) {
	    *buf = '\0';
	    break;
	}

	buf_rem -= read_r;
	slen += read_r;

	if (0 == buf_rem) {
	    buf_rem = JSON5_CHUNK;
	    alen += buf_rem;
	    char *p = realloc (s, alen);

	    if (NULL == p) {
		perror ("json5_parse_fd: realloc");
		exit (1);
	    }
	    s = p;
	}

	buf = s + slen;
    }

    json5_value_t *v = json5_parse_string_C (s, slen);

    free (s);

    return v;
}

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
