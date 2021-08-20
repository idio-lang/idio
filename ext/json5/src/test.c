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
 * test.c
 *
 * Read from any supplied filename or stdin.
 *
 * Write results to stdout.
 */

#define _GNU_SOURCE

#include <sys/types.h>

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "json5-unicode.h"
#include "json5-token.h"
#include "json5-parser.h"

void json5_value_print (json5_value_t *v, int depth);

void json5_value_print_indent (int depth)
{
    if (depth < 1) {
	return;
    }

    for (int i = 0; i < depth; i++) {
	printf ("  ");
    }
}

void json5_value_array_print (json5_array_t *a, int depth)
{
    if (NULL == a) {
	printf ("[]");
	return;
    }

    int printed = 0;
    printf ("[");
    for (; NULL != a;) {
	if (printed) {
	    printf (",");
	} else {
	    printed = 1;
	}
	printf ("\n");

	json5_value_print_indent (depth);

	json5_value_print (a->element, depth);

	a = a->next;
    }
    if (printed) {
	printf ("\n");
	json5_value_print_indent (depth - 1);
    }
    printf ("]");
}

void json5_value_object_print (json5_object_t *o, int depth)
{
    if (NULL == o) {
	printf ("{}");
	return;
    }

    int printed = 0;
    printf ("{");
    for (; NULL != o;) {
	if (printed) {
	    printf (",");
	} else {
	    printed = 1;
	}
	printf ("\n");
	json5_value_print_indent (depth);

	json5_member_t *m = o->member;

	switch (m->type) {
	case JSON5_MEMBER_IDENTIFIER:
	    json5_unicode_string_print (m->name);
	    break;
	case JSON5_MEMBER_STRING:
	    printf ("\"");
	    json5_unicode_string_print (m->name);
	    printf ("\"");
	    break;
	default:
	    fprintf (stderr, "?member %d?\n", m->type);
	    exit (1);
	    break;
	}
	printf (": ");

	json5_value_print (m->value, depth + 1);

	o = o->next;
    }
    if (printed) {
	printf ("\n");
	json5_value_print_indent (depth - 1);
    }
    printf ("}");
}

void json5_value_print (json5_value_t *v, int depth)
{
    if (NULL == v) {
	fprintf (stderr, "_print: NULL?\n");
	assert (0);
	exit (1);
    }

    switch (v->type) {
    case JSON5_VALUE_NULL:
	printf ("null");
	break;
    case JSON5_VALUE_BOOLEAN:
	switch (v->u.l) {
	case JSON5_LITERAL_NULL:
	    /* shouldn't get here */
	    printf ("null?");
	    break;
	case JSON5_LITERAL_TRUE:
	    printf ("true");
	    break;
	case JSON5_LITERAL_FALSE:
	    printf ("false");
	    break;
	default:
	    assert (0);
	    fprintf (stderr, "?literal %d?\n", v->u.l);
	    exit (1);
	    break;
	}
	break;
    case JSON5_VALUE_STRING:
	printf ("\"");
	json5_unicode_string_print (v->u.s);
	printf ("\"");
	break;
    case JSON5_VALUE_NUMBER:
	switch (v->u.n->type) {
	case JSON5_NUMBER_INFINITY:
	    printf ("Infinity");
	    break;
	case JSON5_NUMBER_NEG_INFINITY:
	    printf ("-Infinity");
	    break;
	case JSON5_NUMBER_NAN:
	    printf ("NaN");
	    break;
	case JSON5_NUMBER_NEG_NAN:
	    printf ("-NaN ");
	    break;
	case ECMA_NUMBER_INTEGER:
	    printf ("%" PRId64, v->u.n->u.i);
	    break;
	case ECMA_NUMBER_FLOAT:
	    printf ("%e", v->u.n->u.f);
	    break;
	default:
	    printf ("?? ");
	    break;
	}
	break;
    case JSON5_VALUE_OBJECT:
	json5_value_object_print (v->u.o, depth + 1);
	break;
    case JSON5_VALUE_ARRAY:
	json5_value_array_print (v->u.a, depth + 1);
	break;
    default:
	fprintf (stderr, "?value %d?\n", v->type);
	exit (1);
	break;
    }
}

int main (int argc, char **argv)
{
    if (argc > 1) {
	for (int i = 1; i < argc; i++) {
	    printf ("File: %s\n", argv[i]);
	    int fd = open (argv[i], O_RDONLY);
	    if (-1 == fd) {
		perror ("open");
		exit (1);
	    }

	    json5_value_t *v = json5_parse_fd (fd);

	    json5_value_print (v, 0);
	    printf ("\n");

	    if (close (fd) == -1) {
		perror ("close");
		exit (1);
	    }

	    if (NULL == v) {
		fprintf (stderr, "No JSON5 from %s\n", argv[i]);
	    }
	}
    } else {
	json5_value_t *v = json5_parse_fd (0);

	if (NULL == v) {
	    fprintf (stderr, "No JSON5 from stdin\n");
	}
    }

    return 0;
}
