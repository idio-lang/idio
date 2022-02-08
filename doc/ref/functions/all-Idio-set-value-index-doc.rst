Indexable object types are those with a defined setter or with a
``set-value-index!`` vtable method associated with them.  Standard
vtable methods and setters are:

.. csv-table::
   :widths: auto
   :align: left

   string, :samp:`(string-set! {o} {i} {v})`
   array, :samp:`(array-set! {o} {i} {v})`
   hash, :samp:`(hash-set! {o} {i} {v})`
   struct instance, :samp:`(struct-instance-set! {o} {i} {v})`
   tagged C/pointer, :samp:`({setter} {o} {i})`

Note that, in particular for struct instance, the symbol used for a
symbolic field name may have been evaluated to a value.

Quote if necessary: :samp:`{o}.'{i} = {v}`

.. note::

   Not all tagged C/pointer types have an associated `setter`.
   :ref:`struct-stat <libc/struct-stat>`, for example, does not have
   an associated setter.

``set-value-index!`` is not as efficient as calling the setting
function directly.
