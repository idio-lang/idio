/*
 * Copyright (c) 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * Parts of this code are a straight copy of Bjoern Hoehrmann’s
 * DFA-based decoder, http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
 * for which the licence reads:

License

Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

/*
 * unicode.c
 *
 */

#include "idio.h"

/**
 * static idio_unicode_hash - table of known unicode names to values
 *
 */
static IDIO idio_unicode_hash = idio_S_nil;

static const uint8_t idio_utf8d[] = {
    // The first part of the table maps bytes to character classes that
    // to reduce the size of the transition table and create bitmasks.
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

    // The second part is a transition table that maps a combination
    // of a state of the automaton and a character class to a state.
     0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
};

idio_unicode_t inline idio_utf8_decode (idio_unicode_t* state, idio_unicode_t* codep, idio_unicode_t byte)
{
    idio_unicode_t type = idio_utf8d[byte];

    *codep = (*state != IDIO_UTF8_ACCEPT) ?
	(byte & 0x3fu) | (*codep << 6) :
	(0xff >> type) & (byte);

    *state = idio_utf8d[256 + *state + type];
    return *state;
}

/*
 *

if (countCodePoints(s, &count)) {
  printf("The string is malformed\n");
} else {
  printf("The string is %u characters long\n", count);
}

 *
 */
int idio_utf8_countCodePoints (uint8_t* s, size_t* count)
{
    idio_unicode_t codepoint;
    idio_unicode_t state = 0;

    for (*count = 0; *s; ++s)
	if (0 == idio_utf8_decode (&state, &codepoint, *s))
	    *count += 1;

    return state != IDIO_UTF8_ACCEPT;
}

void idio_utf8_printCodePoints (uint8_t* s)
{
    idio_unicode_t codepoint;
    idio_unicode_t state = 0;

    for (; *s; ++s)
	if (0 == idio_utf8_decode (&state, &codepoint, *s))
	    printf("U+%04X\n", codepoint);

    if (state != IDIO_UTF8_ACCEPT)
	printf("The string is not well-formed\n");
}

/*
  This loop prints out UTF-16 code units for the characters in a null-terminated UTF-8 encoded string.

for (; *s; ++s) {

  if (idio_utf8_decode (&state, &codepoint, *s))
    continue;

  if (codepoint <= 0xFFFF) {
    printf("0x%04X\n", codepoint);
    continue;
  }

  // Encode code points above U+FFFF as surrogate pair.
  printf("0x%04X\n", (0xD7C0 + (codepoint >> 10)));
  printf("0x%04X\n", (0xDC00 + (codepoint & 0x3FF)));
}

*/

/*

  The following code implements one such recovery strategy. When an
  unexpected byte is encountered, the sequence up to that point will
  be replaced and, if the error occured in the middle of a sequence,
  will retry the byte as if it occured at the beginning of a
  string. Note that the idio_utf8_decode function detects errors as
  early as possible, so the sequence 0xED 0xA0 0x80 would result in
  three replacement characters.

for (prev = 0, current = 0; *s; prev = current, ++s) {

  switch (idio_utf8_decode(&current, &codepoint, *s)) {
  case IDIO_UTF8_ACCEPT:
    // A properly encoded character has been found.
    printf("U+%04X\n", codepoint);
    break;

  case IDIO_UTF8_REJECT:
    // The byte is invalid, replace it and restart.
    printf("U+FFFD (Bad UTF-8 sequence)\n");
    current = IDIO_UTF8_ACCEPT;
    if (prev != IDIO_UTF8_ACCEPT)
      s--;
    break;
  ...

  For some recovery strategies it may be useful to determine the
  number of bytes expected. The states in the automaton are numbered
  such that, assuming C's division operator, state / 3 + 1 is that
  number. Of course, this will only work for states other than
  IDIO_UTF8_ACCEPT and IDIO_UTF8_REJECT. This number could then be used, for
  instance, to skip the continuation octets in the illegal sequence
  0xED 0xA0 0x80 so it will be replaced by a single replacement
  character.

  idf: presumably state / 12 + 1 with the Rich Felker tweak.

*/

int idio_isa_unicode (IDIO o)
{
    IDIO_ASSERT (o);

    if (((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_UNICODE_MARK) {
	return 1;
    }

    return 0;
}

IDIO_DEFINE_PRIMITIVE1_DS ("unicode?",  unicode_p, (IDIO o), "o", "\
test if `o` is a unicode value				\n\
							\n\
:param o: object to test				\n\
							\n\
:return: #t if `o` is a unicode value, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_unicode (o)) {
	r = idio_S_true;
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("unicode->integer",  unicode2integer, (IDIO c), "c", "\
convert `c` to an integer				\n\
						\n\
:param c: unicode to convert			\n\
						\n\
:return: integer (fixnum) conversion of `c`	\n\
")
{
    IDIO_ASSERT (c);

    IDIO_VERIFY_PARAM_TYPE (unicode, c);

    return idio_fixnum (IDIO_UNICODE_VAL (c));
}

/*
 * reconstruct C escapes in s
 */
char *idio_utf8_string (IDIO str, size_t *sizep, int escapes, int quoted)
{
    IDIO_ASSERT (str);

    IDIO_TYPE_ASSERT (string, str);

    char *s;
    size_t len;
    IDIO_FLAGS_T flags;
    if (idio_isa_substring (str)) {
	s = IDIO_SUBSTRING_S (str);
	len = IDIO_SUBSTRING_LEN (str);
	flags = IDIO_STRING_FLAGS (IDIO_SUBSTRING_PARENT (str));
    } else {
	s = IDIO_STRING_S (str);
	len = IDIO_STRING_LEN (str);
	flags = IDIO_STRING_FLAGS (str);
    }

    uint8_t *s8 = (uint8_t *) s;
    uint16_t *s16 = (uint16_t *) s;
    uint32_t *s32 = (uint32_t *) s;

    /*
     * Figure out the number of bytes required including escape
     * sequences
     */
    size_t i;
    size_t n = 0;
    for (i = 0; i < len; i++) {
	uint32_t c = 0;
	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    n += 1;
	    c = s8[i];
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    c = s16[i];
	    if (c >= 0x0800) {
		n += 3;
	    } else if (c >= 0x0080) {
		n += 2;
	    } else {
		n += 1;
	    }
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    c = s32[i];
	    if (c >= 0x10000) {
		n += 4;
	    } else if (c >= 0x0800) {
		n += 3;
	    } else if (c >= 0x0080) {
		n += 2;
	    } else {
		n += 1;
	    }
	    break;
	}

	if (escapes) {
	    switch (c) {
	    case '\a': n++; break;
	    case '\b': n++; break;
	    case '\f': n++; break;
	    case '\n': n++; break;
	    case '\r': n++; break;
	    case '\t': n++; break;
	    case '\v': n++; break;
	    case '"': n++; break;
	    }
	}
    }

    size_t bytes = n + 1;
    if (IDIO_UTF8_STRING_QUOTED == quoted) {
	bytes += 2;
    }
    char *r = idio_alloc (bytes);

    n = 0;
    if (IDIO_UTF8_STRING_QUOTED == quoted) {
	r[n++] = '"';
    }
    for (i = 0; i < len; i++) {
	uint32_t c = 0;
	switch (flags) {
	case IDIO_STRING_FLAG_1BYTE:
	    c = s8[i];
	    break;
	case IDIO_STRING_FLAG_2BYTE:
	    c = s16[i];
	    break;
	case IDIO_STRING_FLAG_4BYTE:
	    c = s32[i];
	    break;
	}

	char ec = 0;
	if (escapes) {
	    switch (c) {
	    case '\a': ec = 'a'; break;
	    case '\b': ec = 'b'; break;
	    case '\f': ec = 'f'; break;
	    case '\n': ec = 'n'; break;
	    case '\r': ec = 'r'; break;
	    case '\t': ec = 't'; break;
	    case '\v': ec = 'v'; break;
	    case '"': ec = '"'; break;
	    }
	}

	if (ec) {
	    r[n++] = '\\';
	    r[n++] = ec;
	} else {
	    switch (flags) {
	    case IDIO_STRING_FLAG_1BYTE:
		r[n++] = c;
		break;
	    case IDIO_STRING_FLAG_2BYTE:
	    case IDIO_STRING_FLAG_4BYTE:
		if (c > 0x10ffff) {
		    fprintf (stderr, "utf8-string: oops c=%x > 0x10ffff\n", c);
		} else if (c >= 0x10000) {
		    r[n++] = 0xf0 | ((c & (0x07 << 18)) >> 18);
		    r[n++] = 0x80 | ((c & (0x3f << 12)) >> 12);
		    r[n++] = 0x80 | ((c & (0x3f << 6)) >> 6);
		    r[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
		} else if (c >= 0x0800) {
		    r[n++] = 0xe0 | ((c & (0x0f << 12)) >> 12);
		    r[n++] = 0x80 | ((c & (0x3f << 6)) >> 6);
		    r[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
		} else if (c >= 0x0080) {
		    r[n++] = 0xc0 | ((c & (0x1f << 6)) >> 6);
		    r[n++] = 0x80 | ((c & (0x3f << 0)) >> 0);
		} else {
		    r[n++] = c & 0x7f;
		}
		break;
	    }
	}
    }

    if (IDIO_UTF8_STRING_QUOTED == quoted) {
	r[n++] = '"';
    }
    r[n] = '\0';
    *sizep = n;

    return r;
}

int idio_unicode_C_eqp (void *s1, void *s2)
{
    /*
     * We should only be here for idio_unicode_hash key comparisons
     * but hash keys default to idio_S_nil
     */
    if (idio_S_nil == s1 ||
	idio_S_nil == s2) {
	return 0;
    }

    return (0 == strcmp ((const char *) s1, (const char *) s2));
}

idio_hi_t idio_unicode_C_hash (IDIO h, void *s)
{
    size_t hvalue = (uintptr_t) s;

    if (idio_S_nil != s) {
	hvalue = idio_hash_default_hash_C_string_C (strlen ((char *) s), s);
    }

    return (hvalue & IDIO_HASH_MASK (h));
}

IDIO idio_unicode_C_intern (char *s, IDIO v)
{
    IDIO_C_ASSERT (s);

    IDIO c = idio_hash_get (idio_unicode_hash, s);

    if (idio_S_unspec == c) {
	size_t blen = strlen (s);
	char *copy = idio_alloc (blen + 1);
	strcpy (copy, s);
	idio_hash_put (idio_unicode_hash, copy, v);
    }

    return v;
}

/**
 * idio_unicode_lookup - return the code point of a named unicode code
 * point
 *
 * @s: unicode name (a C string)
 *
 * Return:
 * The Idio value of the code point or %idio_S_unspec.
 */
IDIO idio_unicode_lookup (char *s)
{
    IDIO_C_ASSERT (s);

    return idio_hash_get (idio_unicode_hash, s);
}

/*
 * All the unicode-*? are essentially identical
 */
#define IDIO_DEFINE_UNICODE_PRIMITIVE2V(name,cname,cmp,accessor)	\
    IDIO_DEFINE_PRIMITIVE2V (name, cname, (IDIO c1, IDIO c2, IDIO args)) \
    {									\
	IDIO_ASSERT (c1);						\
	IDIO_ASSERT (c2);						\
	IDIO_ASSERT (args);						\
									\
	IDIO_VERIFY_PARAM_TYPE (unicode, c1);				\
	IDIO_VERIFY_PARAM_TYPE (unicode, c2);				\
	IDIO_VERIFY_PARAM_TYPE (list, args);				\
									\
	args = idio_pair (c2, args);					\
									\
	IDIO r = idio_S_true;						\
									\
	while (idio_S_nil != args) {					\
	    c2 = IDIO_PAIR_H (args);					\
	    IDIO_VERIFY_PARAM_TYPE (unicode, c2);			\
	    if (! (accessor (c1) cmp accessor (c2))) {			\
		r = idio_S_false;					\
		break;							\
	    }								\
	    c1 = c2;							\
	    args = IDIO_PAIR_T (args);					\
	}								\
									\
	return r;							\
    }

#define IDIO_DEFINE_UNICODE_CS_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_UNICODE_PRIMITIVE2V (name, unicode_ci_ ## cname ## _p, cmp, IDIO_UNICODE_VAL)

#define IDIO_DEFINE_UNICODE_CI_PRIMITIVE2V(name,cname,cmp)		\
    IDIO_DEFINE_UNICODE_PRIMITIVE2V (name, unicode_ ## cname ## _p, cmp, idio_unicode_ival)

IDIO_DEFINE_UNICODE_CS_PRIMITIVE2V ("unicode=?", eq, ==)

#define IDIO_UNICODE_INTERN_C(name,c)	(idio_unicode_C_intern (name, IDIO_UNICODE (c)))

void idio_init_unicode ()
{
    idio_unicode_hash = idio_hash (1<<7, idio_unicode_C_eqp, idio_unicode_C_hash, idio_S_nil, idio_S_nil);
    idio_gc_protect (idio_unicode_hash);
    IDIO_HASH_FLAGS (idio_unicode_hash) |= IDIO_HASH_FLAG_STRING_KEYS;

    /* ASCII C0 control characters */
    IDIO_UNICODE_INTERN_C ("nul", 0);
    IDIO_UNICODE_INTERN_C ("soh", 1);
    IDIO_UNICODE_INTERN_C ("stx", 2);
    IDIO_UNICODE_INTERN_C ("etx", 3);
    IDIO_UNICODE_INTERN_C ("eot", 4);
    IDIO_UNICODE_INTERN_C ("enq", 5);
    IDIO_UNICODE_INTERN_C ("ack", 6);
    IDIO_UNICODE_INTERN_C ("bel", 7);
    IDIO_UNICODE_INTERN_C ("bs", 8);
    IDIO_UNICODE_INTERN_C ("ht", 9);
    IDIO_UNICODE_INTERN_C ("lf", 10);
    IDIO_UNICODE_INTERN_C ("vt", 11);
    IDIO_UNICODE_INTERN_C ("ff", 12);
    IDIO_UNICODE_INTERN_C ("cr", 13);
    IDIO_UNICODE_INTERN_C ("so", 14);
    IDIO_UNICODE_INTERN_C ("si", 15);
    IDIO_UNICODE_INTERN_C ("dle", 16);
    IDIO_UNICODE_INTERN_C ("dcl", 17);
    IDIO_UNICODE_INTERN_C ("dc2", 18);
    IDIO_UNICODE_INTERN_C ("dc3", 19);
    IDIO_UNICODE_INTERN_C ("dc4", 20);
    IDIO_UNICODE_INTERN_C ("nak", 21);
    IDIO_UNICODE_INTERN_C ("syn", 22);
    IDIO_UNICODE_INTERN_C ("etb", 23);
    IDIO_UNICODE_INTERN_C ("can", 24);
    IDIO_UNICODE_INTERN_C ("em", 25);
    IDIO_UNICODE_INTERN_C ("sub", 26);
    IDIO_UNICODE_INTERN_C ("esc", 27);
    IDIO_UNICODE_INTERN_C ("fs", 28);
    IDIO_UNICODE_INTERN_C ("gs", 29);
    IDIO_UNICODE_INTERN_C ("rs", 30);
    IDIO_UNICODE_INTERN_C ("us", 31);
    IDIO_UNICODE_INTERN_C ("sp", 32);

    /* C and other common names */
    /* nul as above */
    IDIO_UNICODE_INTERN_C ("alarm",		'\a'); /* 0x07 */
    IDIO_UNICODE_INTERN_C ("backspace",		'\b'); /* 0x08 */
    IDIO_UNICODE_INTERN_C ("tab",		'\t'); /* 0x09 */
    IDIO_UNICODE_INTERN_C ("linefeed",		'\n'); /* 0x0a */
    IDIO_UNICODE_INTERN_C ("newline",		'\n'); /* 0x0a */
    IDIO_UNICODE_INTERN_C ("vtab",		'\v'); /* 0x0b */
    IDIO_UNICODE_INTERN_C ("page",		'\f'); /* 0x0c */
    IDIO_UNICODE_INTERN_C ("return",		'\r'); /* 0x0d */
    IDIO_UNICODE_INTERN_C ("carriage-return",	'\r'); /* 0x0d */
    IDIO_UNICODE_INTERN_C ("esc",		0x1b); /* 0x1b */
    IDIO_UNICODE_INTERN_C ("escape",		0x1b); /* 0x1b */
    IDIO_UNICODE_INTERN_C ("space",		' ');  /* 0x20 */
    IDIO_UNICODE_INTERN_C ("del",		0x7f); /* 0x7f */
    IDIO_UNICODE_INTERN_C ("delete",		0x7f); /* 0x7f */

    /*
     * As #\{ is the start of a named character we need to be able to
     * "characterise" a left brace itself.  Lets have a couple of
     * ways:
     *
     * #\{lbrace}
     * #\{{}
     */
    IDIO_UNICODE_INTERN_C ("lbrace",		'{');
    IDIO_UNICODE_INTERN_C ("{",			'{');

    /* Unicode code points... */
}

void idio_unicode_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (unicode_p);
    IDIO_ADD_PRIMITIVE (unicode2integer);

    /*
     * The unicode_* functions were autogenerated but we still need to
     * add the sigstr and docstr
     */
    IDIO fvi = IDIO_ADD_PRIMITIVE (unicode_ci_eq_p);
    IDIO p = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));
    idio_primitive_set_property_C (p, idio_KW_sigstr, "c1 c2 [...]");
    idio_primitive_set_property_C (p, idio_KW_docstr_raw, "\
test if unicode arguments are equal case-insensitively		\n\
									\n\
:param c1: char								\n\
:param c2: char								\n\
:param ...: chars							\n\
									\n\
This implementation uses libc tolower()					\n\
									\n\
:return: #t if arguments are equal case-insensitively, #f otherwise	\n\
");
}

void idio_final_unicode ()
{
    idio_gc_expose (idio_unicode_hash);
}

