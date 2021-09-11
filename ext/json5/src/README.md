
# JSON5 Standalone Library

Whilst this code exists as an Idio extension it also works as a
standalone library with a simple test capability.

To keep the standalone test in line with the extension, it copies the
Unicode Summary Information source code, `usi.[ch]`, from Idio and
generates a slightly different shared library, the unhelpfully named
`libjson5-bare.so`, with the USI code baked in (like the Idio
executable has the USI code baked in).

## Build

make test

## Run

The standalone test, `./test`, uses the shared library,
`libjson5-bare.so`, but doesn't know where it is so you'll need to set
`LD_LIBRARY_PATH`:

```
% LD_LIBRARY_PATH=. ./test [file ...]
```

The standalone test will read in the JSON5, issue an ERROR and exit if
it finds fault, and otherwise print out the JSON5 it read in.

```
% LD_LIBRARY_PATH=. ./test examples/spec.json5.org/1.2.json5 
File: examples/spec.json5.org/1.2.json5
{
  unquoted: "and you can quote me on that",
  singleQuotes: "I can use \"double quotes\" here",
  lineBreaks: "Look, Mom! No \\n's!",
  hexadecimal: 912559,
  leadingDecimalPoint: 8.675309e-01,
  andTrailing: 8.675309e+06,
  positiveSign: 1,
  trailingComma: "in objects",
  andIn: [
      "arrays"
    ],
  "backwardsCompatible": "with JSON"
}
```

## Validating

You will not get back out what you put in, in many circumstances.
There are several reasons for that:

### Numbers

Numbers are constrained to C `int64_t` and `double`s and, in addition,
highly exponentiated numbers are lost.

There is no requirements for a bignums library and the specification
notes that using numbers beyond those of C are likely to be less
portable.

The `int64_t` and `double` are printed as `"%" PRId64` and `"%e"`,
respectively, meaning the formatting of the JSON5 number and the
printed form are likely to be different.

A more obvious case being that hexadecimal numbers are printed as
decimal.

In addition, the code doesn't handle exponentiated numbers very
sensibly.  In `examples/spec.json5.org/6.1.json5`, the value
`123e-456` has the significant digits, 123, "shifted right" 456 times.
Even if the code handled exponentiated numbers better, 10^-456 is
beyond the capabilities of a double to store.

### Strings (and IdentifierNames)

JSON5 allows a plethora of ECMAScript EscapeSequences which this JSON5
library supports.  However, their existence is lost when it comes time
to print the values back out.

You will get the UTF-8 representation of the EscapeSequence.

This is also a difference with with LineContinuation sequences, the
shell-like trailing-\ to continue a string on the next line.
LineContinuation sequences are dropped, obviously, and it is
impossible to say where they were when it comes time to print the
string back out.

### Objects and Arrays

These are printed one item per line with limited attention paid to
ideal indentation of complex values.

### Comments

Comments obviously disappear.

## Test Suites

The examples in the text of https://spec.json5.org are collected in
the `examples/spec.json5.org` directory.

You can clone the https://github.com/json5/json5-tests repository and
run all the expected pass and expected fail tests with:

```
% ./run-json5-tests -d [git-clone-dir]
```
