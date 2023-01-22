*Idio* is a programming language for orchestrating commands like a
shell.  It is asking the question, can we write better shell scripts
by integrating shell-like constructions into a programming language?

The motivation behind *Idio* is to be able to write shell scripts and
to not have to change languages to perform some, ostensibly, simple
task.  Other languages which are not predicated on orchestrating
commands.  Do we go all-in with the other language and lose the
shell's expressibility, do we fight with quoting to get results back?
It doesn't seem right.

We should be able to manage complex data structures, we should be able
to interact with terminal based programs whose prompts are
context-dependent, we should be able to invoke functionality in shared
libraries.

*Idio* is a programming language first and foremost, though, so not
all traditional shell command syntax translates directly.

*Careful, now!*

*Idio* is new and hasn't seen enough of the world to avoid what are
relatively obvious faults and omissions.  It is also far from ready
for prime-time.  Please take that into consideration.

That's not to say it isn't functional, after all it uses thousands of
lines of *Idio*-script to create its own source code.

It is very much a work in progress.

To get started, please read [Getting
Idio](https://idio-lang.org/get.html).

For further information you can read [Idio By
Example](https://idio-lang.org/docs/idio-by-example) and the [Idio
Language Reference](https://idio-lang.org/docs/ref/).

For those interested in the Design and Implementation of *Idio*,
please read [DIPS](https://idio-lang.org/docs/DIPS/) which is a
lengthy treatise on the hows and whys and wherefores.
