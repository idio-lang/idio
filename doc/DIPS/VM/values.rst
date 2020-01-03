
Constants and Values in the VM
==============================

Overview
--------

When we compile Idio source code for the VM we want to minimise the
decoding required by the VM.  There's also the problem of what we can
successfully encode for the VM.

By and large the `evaluator` cares about symbols and the `VM` cares
about values.  Although there's plenty of cross-over.

One of the greatest speedups in writing a programming language is
eliminating the lookup of variables to values.  If we can pre-map a
variable name to a location in memory and then just use that location
in memory in subsequent operations then we can save a huge amount of
time.

Our variable names are symbols and as variables these refer to
particular memory locations.

Constants
---------

Numbers (in general), strings, quoted values etc. don't have a simple
encoding.  By and large we don't know how big any of those objects are
so every object would require a prefix indicating its length.  We're
starting to add unnecessary complexity even if we've figured out some
means to encode a hash table as bytecode.

Instead, every constant is put into a global array in the VM and the
index into that array, a `constant index`, is encoded in the bytecode.

When the VM is required to access a constant the bytecode instruction
will be followed by an encoded integer and the decoded integer used to
index into the constants table.

Symbols
+++++++

Symbols are also kept in the constants table.  A *reference* to a
symbol is then a reference into the constants table.

Values
------

Values become *far* more complex!  Broadly, every variable that is not
a local variable (that is, a procedure parameter or locally defined
variable) is a toplevel variable and the value it refers to is kept in
an array in the VM.  To access the value we need to encode in the
bytecode the index into this table.

That's the idea but it falls apart pretty quickly when you introduce
both modules and loadable code.

We can run through some history to see how.

Version 1
+++++++++

* No modules

* No loadable code

Here, whenever we see a symbol being used we can look that symbol up
in a table of previously seen symbols, get an index (or create a new
one) and use that symbol index as a one-for-one index into a table of
values.

.. note:: We could have used the symbol's index into the constants
          table directly but you can imagine there might be many more
          constants than there are symbols and so the values table
          would have many more entries than it needs.

Example
-------

Suppose we've previously seen six symbols then we look to encode::

 B := "foo"
 display B

``B`` will become the seventh symbol with a `symbol index`, ``si``,
of 7.  ``"foo"`` will be stored against `value index`, ``vi``, of 7 as
well. In a first pass we might say::

 si#7 := "foo"
 display si#7

and then a second might rewrite that as::

 vi#7 := "foo"
 display vi#7

With a one-to-one mapping we can do that in one pass.

In a pure programming language, if, at the point of first seeing the
symbol we put ``#<undef>`` into the values table then we can use
``#<undef>`` as a check should the code try to reference a variable
before it has been set.  Hence having a ``CHECKED-GLOBAL-REF``
instruction over and above a ``GLOBAL-REF``.

In the case of Idio, we choose to interpret ``#<undef>`` as meaning it
is to be used as part of an external command.

Version 2
+++++++++

* Modules

* No loadable code

Modules make the previous mapping impossible.  If two modules, ``foo``
and ``bar`` both define and export a variable ``A`` then, while they
both use the symbol ``A`` we expect that behind the scenes they are
referring to a different value.

If another module chose to import ``foo`` then ``bar`` you would
expect any references it made to ``A`` to use the value associated
with the module ``foo``.  If, in another instance of Idio, you changed
the import order then you'd expect it to use ``bar`` 's value for
``A``.

If the `evaluator` knew which module it was in when generating code
then it could directly encode the correct index into the values
tables.

However, the `evaluator` does not know which module is the current one
as the expression to set the current module is arbitrary and the
`evaluator` cannot determine the result until the expression is
compiled and run.  It would also have to know the current sets of
imports.

That means we can't encode a `value index` directly.  What we can do
is encode a `symbol index` which this time should be the index into
the constants table (rather than a previously-seen symbols table which
is now redundant).

When the VM tries to reference a variable it will decode the following
integer as the symbol index which it can then use to get the symbol
from the constants table.  It can then call the symbol lookup code to
find out which variable is being referenced (which is dependent on
which modules have been imported and what order).

That requires that associated with each module is a local
symbol-index-to-value-index mapping.  In our example, both of the
modules ``foo`` and ``bar`` will have a mapping for the symbol index
for ``A`` and the mapping will return the correct value index.

The correct value index will have been arranged when the symbol was
used in the definition::

 module foo

 # using the existing global A
 A = 10

 # create our own B for module foo with a new value index
 B := 99

To avoid repeating the above search process every time we look a
variable up we should make a local mapping in the current module's
symbol-index-to-value-index mapping table for ``A`` to whichever value
index was found.

Example
-------

::

 module foo
 A := 10
 
 module bar
 A := 20
 
 module Idio
 import foo bar
 display* (A + 1) (A + 2)

The first reference to ``A`` in the ``Idio`` module (probably in
``(A + 1)`` but possibly ``(A + 2)`` depending on the implementation)
will:

#. decode the symbol index, say, ``si#m``

#. check the local symbol-index-to-value-index mapping table for
   ``si#m`` and find no entry

#. access the symbol from the constants table using ``si#m`` which
   will give us ``A`` again -- the VM never knew about ``A``
   previously, the compiled code only referenced ``si#m``.

#. call the symbol lookup code for the symbol which will find the
   symbol in the hierarchy of modules, ``foo``

#. access the value index from the module-specific
   symbol-index-to-value-index, say, ``vi#n``

#. update the local symbol-to-value index mapping (``si#m`` -> ``vi#n``)

#. access the value from the values table using ``vi#n``

Subsequent references to ``A`` in the ``Idio`` module will:

#. decode the symbol index, say, ``si#m``

#. check the local symbol-to-value index mapping table for ``si#m``
   and get ``vi#n``

#. access the value from the values table using ``vi#n``


Version 3
+++++++++

* Modules

* Loadable code

The loadable code is the problem here -- modules merely add fuel to
the fire.

The problems for us here are the same as faced by loadable objects
used by the operating system.  Given a previously created object file,
when we load it in how do we map its references to global values to
where the global values are in the running instance of the program?

Traditionally there have been two solutions:

#. relocatable code

   Here, when the linker loads the code it rummages through the
   entirety of the loaded code and rewrites any nominal references to
   absolute references.  

   This makes for fast code but requires that the text segment be
   writeable.

   Several Scheme implementation perform this sort of code rewrite, in
   fact, doing it on the fly!  This requires that the existing
   instruction plus index consume the same amount of space in the
   bytecode as the replacement instruction plus index.  That might
   seem obvious if, at least, all indexes are a fixed size.  A fixed
   index size has several associated considerations:

   * using the maximum possible index every time: inefficient for
     small indexes (a full machine word for a potential index when
     you're only using one or two bytes)

   * using a smaller (than a word) fixed size index: limits the
     maximum number of indexes.  Someone will hit the limit,
     guaranteed!

   * variable widths: if the original encoding was maximum width (just
     in case) and the rewrite used less space then what pads the gap?
     A byte-compiler VM will start to consume those padded bytes as
     instructions unless you re-wrote the remainder of the bytecode to
     compensate for the reduced space which would mean any intra-block
     jumps that stepped over the reference will need re-calculating

#. position independent code (PIC)

   Here, the compiler generates a fixed bytecode which contains
   references to a Global Offset Table (GOT) which the linker arranges
   to be in a fixed position relative to the code.  The linker adjusts
   all references to use offsets into the GOT depending on which
   global is being referenced and fills the GOT with the correct
   address of the global.

   (It's not clear how the address of the GOT is advertised as a piece
   of code is run!)

   PIC has the benefits of being read-only and therefore shareable by
   the OS.

   The GOT is writeable and in a data section.

Position Independent Code seems more honest even if it requires a bit
more indirection when running.

Symbols
-------

Suppose we compile a standalone piece of code (whatever `compiling`
means at this point) and write it out with the expectation of loading
it back in::

 A := 10
 C := "foo"
 X := #f

Now we re-run Idio and compile another piece of code and write it out
with the expectation of loading it back in::

 B := 10
 Y := #t
 C := "bar"

What did we generate for symbol indexes in the two sets of bytecode?
As we re-ran Idio then almost certainly the same numbers for a start!
We'll have both ``A`` and ``B`` using the same symbol index as they
were the first symbols seen and we'll have two different symbol
indexes for ``C`` (as ``C`` appears in a different order in each
file).

When we come to load either of these files into another instance of
Idio, it will have three symbol indexes in its hands from either of
these files but what symbols do they represent?  In a worse case, if
the instance of Idio loading the file has done more work before
loading either of these files then it may already have used these
three symbol indexes for other symbols.

Clearly we need some mapping for the loaded files' symbols to the
running Idio's global list and for the loaded file to supply the list
of symbols it is using.

Given that there will be *some* mapping done for loaded files' symbols
and indexes to global symbols and indexes then the loaded file might
as well use the smallest indexes it can to help keep its size down.

So, each of the two files includes a symbol table (``A C X`` and ``B Y
C``) and the symbol indexes used in the bytecode will both be 0, 1
and 2.

When the file is loaded, the symbol table is read and the symbols
looked up in the constants table, that will give existing (or new)
*global* symbol indexes and the loaded file will have an associated
mapping table.

File symbol index map:

======= =========
file si global si
======= =========
0	si#a
1	si#b
2	si#c
======= =========

Hold the thought about where that table lives for a moment as clearly
it must be associated with the arbitrary block of code loaded from the
file.  Any other loaded file will have to have its own mapping table
as clearly our second loadable file uses the same symbol indexes to
mean different symbols.

Values
------

Values have much the same problem.  Not because the value indexes are
encoded in the bytecode (they are (mostly) not) but because the
mapping from (now) global symbol index to value index is associated
with the code segment.

Why?  Why *modules*, of course!  If one code segment is manipulating
symbol ``A`` in module ``foo`` and another code segment is
manipulating symbol ``A`` in module ``bar`` then they must be
manipulating different variables, ie. different values.  That means
each code segment must have its own symbol index to global value index
mapping table.

.. note:: The symbol index used in this mapping table *can* be the
          file-specific symbol index, ie. a relatively small integer
          value (potentially saving space).

File symbol index to value index map:

======= =========== =========
file si bytecode vi global vi
======= =========== =========
0	0	    TBD
1	1	    TBD
2	2	    TBD
======= =========== =========

On a pragmatic front we will want to indicate that no mapping exists
for which we may as well use value index 0 as a marker.  Real values
will have an index 1 or higher.  We'll be wasting an entry in the
values table.

PIC and GOT
-----------

Both the code-segment-specific symbol index to global symbol index
table and code-segment-specific symbol index to global value index
tables must live somewhere but somewhere where the code segment has
access to them.

In x86/x64 shared libraries magic is performed by the dynamic linker
to present a value for the global offset table when code for a shared
library is run.  We need to do something similar.

For shared libraries the GOT is in the data segment at the end of the
code segment.  Could we do that?

Possibly, but it requires multiple passes over the code.  Imagine the
instructions to reference a variable, we might encode::

 ...
 GLOBAL-REF GOT SYMBOL-INDEX
 ...

where ``GOT`` is the end of the text segment (the end of the
bytecode).

What is the *value* of ``GOT``?  Well, we don't know that yet because
we haven't generated all the code.

How much space should we leave for the value of ``GOT`` (now to be
inserted later when we know how much code there is)?  Hmm, we're back
to our fixed/variable values.  We can either waste space on a (large)
fixed width value or we can use a variable width value but that
requires that any intra-block jumps over every reference to ``GOT`` be
adjusted depending on how far it is to the end of the code.

As an alternative, we could put some indication of the GOT at the
start of the code::

 ADDR-OF-GOT
 ...
 GLOBAL-REF offset-to-#0 SYMBOL-INDEX
 ...

where ``offset-to-#0`` is a calculation of how many bytes it is back
to the first bytes of the code segment.  That's much more workable and
fits with the existing variable length relative jumps in the bytecode.

What is the value of ``ADDR-OF-GOT`` though?  Clearly, when we
generate the code we've no idea who is going to be loading the
bytecode back in so we must leave a blank to be filled in.

Filled in with what?  We can't use a real address as we don't know if
a 32 or 64 bit system is going to use this bytecode.  We could put an
index in -- an index into the table of loaded-codes' mapping tables!

How much space to leave?  This is the tricky part.  We're really
asking how many files can we load in before we hit a problem with
indexing?

256 (one byte) loads?  Doesn't sound like enough.  65536 (two bytes)
loads?  More plausible.  16777216 (three bytes) loads?  Plenty.
4294967296 (four bytes) loads?  Enough.

By the time we've loaded 4 billion code segments something else will
surely have broken.  We'll have allocated several orders more memory
for all the various tables etc..  

That is feasible on 64 bit machines but for the sake of four bytes at
the start of every code segment we can state a limitation on Idio that
it cannot load more than 4 billion code segments.  Let's play with
that and see how it goes -- see if anyone tries to load more than 4
billion code segments and complains!

So, where has this left us?  The change is how we find our local
symbol index to global symbol or value index mapping tables.

For our previous code segment::

 A := 10
 C := "foo"
 X := #f

we'll generate some code to access ``C``, say::

 0 0 0 0	;; four blank bytes for index-to-GOT
 ...
 GLOBAL-REF offset-to-#0 1
 ...

with a symbol table of ``(A C X)``.

When we load the file we'll need to allocate new tables in the global
mapping tables table for which we have the value, ``mi``.  We'll write
a fixed four-byte index representing ``mi`` at the start of the code
segment over the four zeroes.

We can generate a symbol-index-to-global-symbol-index table (in the
global mappings table) which has three entries for the three symbols
in the code segment.  We'll have checked whether they already exist,
of course.

We can create an empty symbol-index-to-value-index table too.

When we reach the ``GLOBAL-REF`` instruction we'll read
``offset-to-#0`` (the number of bytes back to where ``index-to-GOT``
has been written and read that value.  We can now access index 1 (the
local symbol index of ``C``) of that table to get the either the value
index or the global symbol reference of ``C`` depending on which use
we need.

More Efficiency
***************

Could we avoid the extra indirection and have the
symbol-index-to-value-index table in the GOT directly?

The existing parser is a single pass which prevents us knowing how
many symbols there will be in the symbol table and by extension in any
mapping table.  However, we could build the mapping table in reverse.
Consider that the bytecode is position independent and therefore can
live anywhere.  In particular, we might start a temporary bytecode
buffer at index 0 but could shift it forward by tens or hundreds of
bytes at no cost to the bytecode because it only ever refers back to
the start of itself which is always the same distance wherever we put
it in memory.

The bytecode references ``offset-to-#0``.  Suppose we build the GOT in
reverse starting at #0?  Symbol index 0 will be at #0, symbol index 1
will be at -1, symbol index `n` will be at `-n`.

Of course, ``GLOBAL-REF offset-to-#0 n`` can be reduced to
``GLOBAL-REF m`` where ``m`` is interpreted as a negative offset.
("Interpreted as" because we can only encode positive integers!)

The final combination will be a "reversed" data segment followed by
text segment.

That works only if the entries in the GOT are a fixed size to allow us
to index them randomly.

Being a fixed size is against our gut instinct but any kind of array
(including an Idio array) will be indexed by a sizeof (size_t)
integral type so by indexing by the same sized objects we neither gain
nor lose anything.

We can further eliminate the symbol-index-to-value-index mapping
indirection by interleaving those values into the GOT as well.  Any
reference, ``m``, into the GOT would be a multiple of two with the
symbol index mapping and value index mapping side-by-side.

The overall result being::

 word 0       : symbol index n mapping to global symbol index
 word 1       : symbol index n mapping to value index
 word 2       : symbol index n-1 mapping to global symbol index
 word 3       : symbol index n-1 mapping to value index
 ...
 word 2(n-1)  : symbol index 1 mapping to global symbol index
 word 2(n-1)+1: symbol index 1 mapping to value index
 word 2n      : symbol index 0 mapping to global symbol index
 word 2n+1    : symbol index 0 mapping to value index
 word 2(n+1)  : start of bytecode
	      ...
              GLOBAL-REF m
              ...

When we come to load an object file we can read and count the symbol
table and allocate twice as many words at the start of the resultant
byte array arranging any symbol index mapping as required at the time.

Note that we now require to indicate the entry point of the bytecode
-- it used to be at position zero but is now some ``2n`` words into
the array.

Direct or Indirect References
*****************************

Loaded bytecode, then, looks like it might have references to symbols
in a "reversed" table at the start of the bytecode.  The non-loaded
bytecode doesn't fit that model and it probably won't fit that model
as every new symbol reference would require that the entirety of the
existing data plus text segments be reallocated two words forward to
accommodate the new symbol index maps.

We have a choice, then.  We can introduce as many more new
instructions into the VM to handle the indirect reference variants of
the existing symbol index referencing instructions (about ten) or we
can flag that the symbol index is direct or indirect.  VM instructions
are more precious!

A flag clearly requires an extra bit.  Can we afford that?  Well,
empirical study suggests that a 32 bit system is limited to allocating
a 29 bit array so we have two or three bits to play with.

An obvious solution, then, is to encode all symbols indexes with the
bottom bit a flag to indicate direct or indirect references.

Dynamic/Environ Values
----------------------

value index on the stack

Interactive Session
-------------------

Hiding in plain sight is any interactive session.  What mapping table
does it use?  Every snippet of code is evaluated, compiled and run.
Doe we really want it to be embedding GOTs?

