.. _`array type`:

Array Type
==========

Arrays are integer indexed collections of references to values.

The supplied index, :samp:`{index}`, can be negative in which case the
calculated index is :samp:`{array-length} + {index}`.

Reader Form
-----------

Arrays of simple types can be constructed with the reader form

    :samp:`#[ [{value} ...] ]`

degenerating to ``#[]``.

Any :samp:`{value}` will not be evaluated, only simple type
construction (numbers, strings, symbols etc.) will occur.

