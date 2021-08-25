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
 * json5-parser.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "json5-unicode.h"
#include "json5-token.h"
#include "json5-parser.h"
#include "utf8.h"

#define JSON5_CHUNK 1024

static json5_token_t *json5_parse_token (json5_token_t *ct, json5_value_t **valuepp);
static json5_token_t *json5_parse_token_stream (json5_token_t *ct, json5_value_t **valuepp);

static json5_token_t *json5_parse_array (json5_token_t *ct, json5_value_t **valuepp)
{
    enum pending_array_states {
	VALUE,
	COMMA_RBRACKET,		/* , or ] */
    };
    enum pending_array_states pending = VALUE;

    *valuepp = (json5_value_t *) malloc (sizeof (json5_value_t));
    (*valuepp)->type = JSON5_VALUE_ARRAY;
    (*valuepp)->u.a = NULL;

    json5_array_t *ae = NULL;

    int done = 0;
    for (; NULL != ct;) {
	if (JSON5_TOKEN_PUNCTUATOR == ct->type &&
	    JSON5_PUNCTUATOR_RBRACKET == ct->value->u.p) {
	    ct = json5_token_free_next (ct);
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

	    ct = json5_parse_token (ct, &(ae->element));

	    if (NULL == ct) {
		done = 1;
		break;
	    }
	    pending = COMMA_RBRACKET;
	    break;
	case COMMA_RBRACKET:
	    if (JSON5_TOKEN_PUNCTUATOR != ct->type ||
		JSON5_PUNCTUATOR_COMMA != ct->value->u.p) {
		json5_error_printf ("_array: ,|] expected: ");

		/* notreached */
		return NULL;
	    }
	    ct = json5_token_free_next (ct);
	    pending = VALUE;
	    break;
	}

	if (done) {
	    if (NULL != ct) {
		ct = json5_token_free_next (ct);
	    }
	    break;
	}
    }

    return ct;
}

static json5_token_t *json5_parse_object (json5_token_t *ct, json5_value_t **valuepp)
{
    int done = 0;
    enum pending_object_states {
	NAME,
	COLON,
	VALUE,
	COMMA_RBRACE,			/* , or } */
    };
    enum pending_object_states pending = NAME;

    *valuepp = (json5_value_t *) malloc (sizeof (json5_value_t));
    (*valuepp)->type = JSON5_VALUE_OBJECT;
    (*valuepp)->u.o = NULL;

    json5_object_t *object = NULL;

    for (; NULL != ct;) {
	if (JSON5_TOKEN_PUNCTUATOR == ct->type &&
	    JSON5_PUNCTUATOR_RBRACE == ct->value->u.p) {
	    ct = json5_token_free_next (ct);
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
	    object->value = NULL;

	    if (JSON5_TOKEN_IDENTIFIER == ct->type) {
		object->type = JSON5_MEMBER_IDENTIFIER;
		object->name = ct->value->u.s;
		free (ct->value);
	    } else if (JSON5_TOKEN_STRING == ct->type) {
		object->type = JSON5_MEMBER_STRING;
		object->name = ct->value->u.s;
		free (ct->value);
	    } else {
		json5_error_printf ("_object: identifier|string name expected: ct->type=%d ", ct->type);

		/* notreached */
		return NULL;
	    }
	    ct = json5_token_free_next (ct);
	    pending = COLON;
	    break;
	case COLON:
	    if (JSON5_TOKEN_PUNCTUATOR != ct->type ||
		JSON5_PUNCTUATOR_COLON != ct->value->u.p) {
		json5_error_printf ("_object: colon expected: ");

		/* notreached */
		return NULL;
	    }
	    ct = json5_token_free_next (ct);
	    pending = VALUE;
	    break;
	case VALUE:
	    {
		ct = json5_parse_token (ct, &(object->value));

		if (NULL == ct) {
		    return ct;
		}
	    }
	    pending = COMMA_RBRACE;
	    break;
	case COMMA_RBRACE:
	    if (JSON5_TOKEN_PUNCTUATOR != ct->type ||
		(JSON5_PUNCTUATOR_COMMA != ct->value->u.p &&
		 JSON5_PUNCTUATOR_RBRACE != ct->value->u.p)) {
		json5_error_printf ("_object: ,|} expected: ");

		/* notreached */
		return NULL;
	    }
	    ct = json5_token_free_next (ct);
	    pending = NAME;
	    break;
	}

	if (done) {
	    ct = json5_token_free_next (ct);
	    break;
	}
    }

    return ct;
}

static json5_token_t *json5_parse_token (json5_token_t *ct, json5_value_t **valuepp)
{
    switch (ct->type) {
    case JSON5_TOKEN_ROOT:
	json5_error_printf ("unexpected root token");

	/* notreached */
	return NULL;
    case JSON5_TOKEN_IDENTIFIER:
	switch (ct->value->type) {
	case JSON5_VALUE_NULL:
	    *valuepp = ct->value;
	    break;
	case JSON5_VALUE_BOOLEAN:
	    switch (ct->value->u.l) {
	    case JSON5_LITERAL_TRUE:
		*valuepp = ct->value;
		break;
	    case JSON5_LITERAL_FALSE:
		*valuepp = ct->value;
		break;
	    default:
		json5_error_printf ("?literal %d?", ct->value->u.l);

		/* notreached */
		return NULL;
	    }
	    break;
	case JSON5_VALUE_IDENTIFIER:
	    *valuepp = ct->value;
	    break;
	default:
	    json5_error_printf ("?identifier %d?", ct->value->type);

	    /* notreached */
	    return NULL;
	}
	ct = json5_token_free_next (ct);
	break;
    case JSON5_TOKEN_PUNCTUATOR:
	if (JSON5_PUNCTUATOR_LBRACE == ct->value->u.p) {
	    ct = json5_token_free_next (ct);
	    ct = json5_parse_object (ct, valuepp);
	} else if (JSON5_PUNCTUATOR_LBRACKET == ct->value->u.p) {
	    ct = json5_token_free_next (ct);
	    ct = json5_parse_array (ct, valuepp);
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
		json5_error_printf ("?punctuator? %d ", ct->value->u.p);

		/* notreached */
		return NULL;
	    }
	    json5_error_printf ("unexpected punctuation: '%s'", p);

	    /* notreached */
	    return NULL;
	}
	break;
    case JSON5_TOKEN_STRING:
	*valuepp = ct->value;
	ct = json5_token_free_next (ct);
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
	    json5_error_printf ("?number %d?", ct->value->u.n->type);

	    /* notreached */
	    return NULL;
	}
	*valuepp = ct->value;
	ct = json5_token_free_next (ct);
	break;
    default:
	json5_error_printf ("?token %d?", ct->type);

	/* notreached */
	return NULL;
    }

    return ct;
}

static json5_token_t *json5_parse_token_stream (json5_token_t *ct, json5_value_t **valuepp)
{
    if (NULL == ct ||
	JSON5_TOKEN_ROOT != ct->type) {
	json5_error_printf ("invalid token stream");

	/* notreached */
	return NULL;
    }

    ct = json5_token_free_next (ct);

    if (NULL == ct) {
	json5_error_printf ("empty token stream");

	/* notreached */
	return NULL;
    }

    ct = json5_parse_token (ct, valuepp);

    return ct;
}

json5_value_t *json5_parse (json5_token_t *root)
{
    json5_value_t *valuep;

    json5_token_t *ct = json5_parse_token_stream (root, &valuep);

    while (NULL != ct) {
	json5_error_printf ("_parse: not all parsed");

	/* notreached */
	return NULL;
    }

    return valuep;
}

json5_value_t *json5_parse_string (json5_unicode_string_t *so)
{
    json5_token_t *root = json5_tokenize_string (so);

    json5_value_t *valuep = json5_parse (root);

    return valuep;
}

json5_value_t *json5_parse_string_C (char *s_C, size_t s_Clen)
{
    json5_token_t *root = json5_tokenize_string_C (s_C, s_Clen);

    json5_value_t *valuep = json5_parse (root);

    return valuep;
}

json5_value_t *json5_parse_fd (int fd)
{
    size_t slen = JSON5_CHUNK;
    size_t buf_rem = slen;
    char *s = malloc (buf_rem);

    size_t buf_off = 0;
    char *buf = s + buf_off;

    for (;;) {
	ssize_t read_r = read (fd, buf, buf_rem);

	if (-1 == read_r) {
	    perror ("read");
	    return NULL;
	}

	if (0 == read_r) {
	    *buf = '\0';
	    break;
	}

	buf_rem -= read_r;
	buf_off += read_r;

	if (0 == buf_rem) {
	    buf_rem = JSON5_CHUNK;
	    slen += buf_rem;
	    char *p = realloc (s, slen);

	    if (NULL == p) {
		perror ("json5_parse_fd: realloc");
		exit (1);
	    }
	    s = p;
	}

	buf = s + buf_off;
    }

    json5_value_t *v = json5_parse_string_C (s, strlen (s));

    free (s);

    return v;
}

/* Local Variables: */
/* mode: C */
/* buffer-file-coding-system: utf-8-unix */
/* End: */
