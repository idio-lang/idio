indexable object types are:

.. csv-table::
   :widths: auto
   :align: left

   list, :samp:`(nth {o} {i})`
   string, :samp:`(string-ref {o} {i})`
   array, :samp:`(array-ref {o} {i})`
   hash, :samp:`(hash-ref {o} {i})`
   struct instance, :samp:`(struct-instance-ref {o} {i})`
   tagged C/pointer, :samp:`({accessor} {o} {i})`

Note that, in particular for struct instance, the symbol used for a
symbolic field name may have been evaluated to a value.

Quote if necessary: :samp:`{o}.'{i}`

``value-index`` is not as efficient as calling the accessor function
directly.
