Indexable object types are those with a ``value-index`` vtable method
associated with them.  Standard indexable types with the
implementation function call are:

.. csv-table::
   :widths: auto
   :align: left

   list, :samp:`(nth {o} {i})`, :ref:`nth <nth>` indexes start at 1
   string, :samp:`(string-ref {o} {i})`, :ref:`string-ref <string-ref>`
   array, :samp:`(array-ref {o} {i})`, :ref:`array-ref <array-ref>` indexes can be negative
   hash, :samp:`(hash-ref {o} {i})`, :ref:`hash-ref <hash-ref>`
   struct instance, :samp:`(struct-instance-ref {o} {i})`, :ref:`struct-instance-ref <struct-instance-ref>`
   tagged C/pointer, :samp:`({accessor} {o} {i})`

Note that, in particular for struct instance, the symbol used for a
symbolic field name may have been evaluated to a value.

Quote if necessary: :samp:`{o}.'{i}`

``value-index`` is not as efficient as calling the accessor function
directly.

:Example:

An example where `i` is a function is using :ref:`fields <fields>` to
split a string (on :ref:`IFS <IFS>`):

.. code-block:: idio-console

   Idio> "here and there" . fields
   #[ "here and there" "here" "and" "there" ]

An array example which can use a negative index:

.. code-block:: idio-console

   Idio> a := #[ 'a 'b 'c ]
   #[ 'a 'b 'c ]
   Idio> a.2
   'c
   Idio> a.-1
   'c
