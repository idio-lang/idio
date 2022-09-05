* `desc` is a description of the eenv

* `file` is the file name of the source file associated with this eenv
  (which could be *stdin*)

  Note this is the source file not any (pre-compiled) cache file.

* `aotp` is a flag indicating that the source code is being
  *pre*-compiled

* `chksum` is a SHA256 digest of the source file (this is unlikely to
  be correct for *stdin*)

* `symbols` is an associate list of symbol names and some symbol data
  tuples

  This is the primary evaluation data structure.

* `operators` is a similar association list but for operators which
  need to be managed separately as they don't exist in this module --
  all operators exist in the `operator` module as they are common to
  the reader.

* `predefs` is a simple tuple of the predefs (primitives) used in this
  module

  When reading in a cached byte code file nothing will pre-set the
  predefs into the xenv symbol and value tables.  Most of the time
  this doesn't matter as a reference to the name will find the predef
  but there are corner cases, for example, where a closure shadows a
  predef, that pre-setting the predef is a requirement.

* `ph` is a hash of the `predefs` being added (creating a set rather
  than duplicates)

* `st` is the symbol table for the xenv, it is an array mapping a
  symbol table index, :samp:`{si}`, to constants table index,
  :samp:`{ci}`

  `st` and `vt` will align one-for one in an xenv

* `cs` is the constants table for the xenv, an array mapping a
  constants table index, :samp:`{ci}`, to a constant value

  Any (constant) expression that isn't a small positive integer will
  be in here.

* `ch` is a hash of the constants table for the VM enabling faster
  lookups

* `vt` is the values table for the xenv, an array mapping the xenv's
  value table index, :samp:`vi`, to a global values table index (the
  table the VM actually cares about)

  `st` and `vt` will align one-for one in an xenv

* `module` is a module being modified in this eenv

* `escapes` isn't used

* `ses` is an array of source code expressions

  These are maintained during evaluation but are not saved as it is
  impossible to (sensibly) regenerate without re-parsing the source
  code.

* `sps` is an array of source code properties

  In particular it is a set of :samp:`({fi} {line})` tuples where
  :samp:`{fi}` is an index into the constants table to the source
  code's file name.  :samp:`{line}` is the source code line number.

* `byte-code` is the VM's byte code

  This is saved as a :ref:`byte string <octet string>`.

* `pcs` is the list of initial program counters for the `byte-code`

  These reflect the top-level expressions in the source code.

* `xi` is the xenv's xenv index
