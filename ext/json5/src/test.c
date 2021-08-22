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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "json5-unicode.h"
#include "json5-token.h"
#include "json5-parser.h"

#ifdef IDIO_MALLOC
#define JSON5_VASPRINTF idio_malloc_vasprintf
#else
#define JSON5_VASPRINTF vasprintf
#endif

void json5_error_alloc (char *m)
{
    assert (m);

    /*
     * This wants to be a lean'n'mean error "handler" as we've
     * (probably) run out of memory.  The chances are {m} is a static
     * string and has been pushed onto the stack so no allocation
     * there.
     *
     * perror(3) ought to be able to work in this situation and in the
     * end we abort(3).
     */
    perror (m);
    abort ();
}

char *json5_error_string (char *format, va_list argp)
{
    char *s;
    if (-1 == JSON5_VASPRINTF (&s, format, argp)) {
	json5_error_alloc ("asprintf");
    }

    return s;
}

void json5_error_printf (char *format, ...)
{
    assert (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    char *msg = json5_error_string (format, fmt_args);
    va_end (fmt_args);

    fprintf (stderr, "ERROR: %s\n", msg);
    free (msg);
    exit (1);
}

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

	switch (o->type) {
	case JSON5_MEMBER_IDENTIFIER:
	    json5_unicode_string_print (o->name);
	    break;
	case JSON5_MEMBER_STRING:
	    printf ("\"");
	    json5_unicode_string_print (o->name);
	    printf ("\"");
	    break;
	default:
	    json5_error_printf ("?member %d?", o->type);

	    /* notreached */
	    return;
	}
	printf (": ");

	json5_value_print (o->value, depth + 1);

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
	json5_error_printf ("_print: NULL?");

	/* notreached */
	return;
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
	    json5_error_printf ("?literal %d?", v->u.l);

	    /* notreached */
	    return;
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
	json5_error_printf ("?value %d?", v->type);
	/* notreached */
	return;
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

	    if (NULL == v) {
		json5_error_printf ("No JSON5 from %s", argv[i]);

		/* notreached */
		return 1;
	    }

	    json5_value_print (v, 0);
	    printf ("\n");

	    json5_value_free (v);

	    if (close (fd) == -1) {
		perror ("close");
		exit (1);
	    }
	}
    } else {
	json5_value_t *v = json5_parse_fd (0);

	if (NULL == v) {
	    json5_error_printf ("No JSON5 from stdin");

	    /* notreached */
	    return 1;
	}
    }

    return 0;
}
