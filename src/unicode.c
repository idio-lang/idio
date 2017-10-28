/*
 * Copyright (c) 2017 Ian Fitchet <idf(at)idio-lang.org>
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

idio_utf8_t inline idio_utf8_decode (idio_utf8_t* state, idio_utf8_t* codep, idio_utf8_t byte)
{
    idio_utf8_t type = idio_utf8d[byte];

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
    idio_utf8_t codepoint;
    idio_utf8_t state = 0;

    for (*count = 0; *s; ++s)
	if (0 == idio_utf8_decode (&state, &codepoint, *s))
	    *count += 1;

    return state != IDIO_UTF8_ACCEPT;
}

void idio_utf8_printCodePoints (uint8_t* s)
{
    idio_utf8_t codepoint;
    idio_utf8_t state = 0;

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

void idio_init_unicode ()
{
}

void idio_unicode_add_primitives ()
{
}

void idio_final_unicode ()
{
}

