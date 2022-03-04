.. _`condition types hierarchy`:

Condition Types
---------------

By convention, condition type names have a ``^`` prefix suggesting
some higher level of intervention.

There is a predicate for each condition type, without the leading
``^``, and any fields have accessors called :samp:`{type}-{field}`.

The base condition type is ``^condition`` and its immediate descendent
is ``^error``.

:lname:`Idio` error types, ``^idio-error``, are descended from
``^error`` and add three fields: ``message``, ``location`` and
``detail``.

A few instances of ``^idio-error`` are still generated, largely from
the :lname:`C` code base, but most conditions are instances of more
specific condition types.

* ``^condition``

  * ``^error``

    * ``^idio-error`` (``message`` ``location`` ``detail``)

      * ``^read-error`` (``line`` ``position``)

      * ``^evaluation-error`` (``expr``)

      * ``^string-error``

      * ``^static-error``

	* ``^static-variable-error`` (``name``)
	   
	  * ``^static-variable-type-error``
	   
	* ``^static-function-error``
	   
	  * ``^static-function-arity-error``
	   
      * ``^i/o-error``

	* ``^i/o-handle-error`` (``handle``)
	   
	  * ``^i/o-read-error``
	   
	  * ``^i/o-write-error``
	   
	  * ``^i/o-closed-error``
	   
	* ``^i/o-filename-error`` (``filename``)
	   
	  * ``^i/o-malformed-filename-error``
	   
	  * ``^i/o-file-protection-error``
	   
	  * ``^i/o-file-is-read-only-error``
	   
	  * ``^i/o-file-already-exists-error``
	   
	  * ``^i/o-no-such-file-error``
	   
	* ``^i/o-mode-error`` (``mode``)
	   
      * ``^system-error`` (``errno`` ``function``)

      * ``^runtime-error``

	* ``^rt-syntax-error``

	* ``^rt-parameter-error``

	  * ``^rt-parameter-type-error``

	  * ``^rt-const-parameter-error``

	  * ``^rt-parameter-value-error``

	  * ``^rt-parameter-nil-error``

	* ``^rt-variable-error`` (``name``)

	  * ``^rt-variable-unbound-error``

	  * ``^rt-dynamic-variable-error``

	    * ``^rt-dynamic--variable-unbound-error``

	  * ``^rt-environ-variable-error``

	    * ``^rt-environ--variable-unbound-error``

	  * ``^rt-computed-variable-error``

	    * ``^rt-computed--variable-no-accessor-error``

	* ``^rt-function-error``

	  * ``^rt-function-arity-error``

	* ``^rt-module-error`` (``module``)

	  * ``^rt-module-unbound-error``

	  * ``^rt-module-symbol-unbound-error`` (``symbol``)

	* ``^rt-path-error`` (``name``)

	* ``^rt-glob-error`` (``pattern``)

	* ``^rt-command-error``

	  * ``^rt-command-argv-type-error`` (``arg``)

	  * ``^rt-command-format-error`` (``name`` ``value``)

	  * ``^rt-command-env-type-error`` (``name`` ``value``)

	  * ``^rt-command-exec-error`` (``errno``)

	  * ``^rt-command-status-error`` (``status``)

	    * ``^rt-async-command-status-error``

	* ``^rt-array-error`` (``index``)

	* ``^rt-hash-error``

	  * ``^rt-hash-key-error`` (``key``)

	* ``^rt-number-error`` (``number``)

	  * ``^rt-divide-by-zero-error``

	  * ``^rt-bignum-error``

	    * ``^rt-bignum-conversion-error``

	  * ``^rt-C-conversion-error``

	  * ``^rt-fixnum-error``

	    * ``^rt-fixnum-conversion-error``

	* ``^rt-bitset-error``

	  * ``^rt-bitset-bounds-error`` (``bit``)

	  * ``^rt-bitset-size-mismatch-error`` (``size1`` ``size2``)

	* ``^rt-keyword-error`` (``keyword``)

	* ``^rt-libc-error``

	  * ``^rt-libc-format-error`` (``name``)

	* ``^rt-load-error`` (``name``)

	* ``^rt-regex-error``

	* ``^rt-struct-error``

	* ``^rt-symbol-error``

	* ``^rt-vtable-unbound-error``

	* ``^rt-vtable-method-unbound-error`` (``name``)

	* ``^rt-instance-error``

	* ``^rt-instance-invocation-error``

	* ``^rt-slot-not-found-error`` (``slot``)

	* ``^rt-signal`` (``signal``)

Defining Condition Types
^^^^^^^^^^^^^^^^^^^^^^^^

Defining condition types is slightly roundabout because most of the
standard condition types are defined in :lname:`C` so that the
:lname:`C` code can :ref:`raise <raise>` instances of the condition
types.

Furthermore, the :lname:`C` code does not need accessor functions as
it can access the internals of the condition types directly.

That said, the :lname:`Idio` code *does* need to have the accessor
functions available otherwise it can't access the condition internals.

From the :lname:`C` perspective, we have defined a condition type and
we only need to have :lname:`Idio` define the predicate and accessors.
This is done in :file:`lib/bootstrap/condition.idio` with calls to
:ref:`define-condition-type-accessors-only
<define-condition-type-accessors-only>`.

From the :lname:`Idio` perspective, we need to define a condition type
and then carry on with what the :lname:`C` condition types do.

So :ref:`define-condition-type <define-condition-type>` takes the
condition type name and creates a standard predicate name and takes
the field names and creates standard
:samp:`{condition-name}-{field-name}` accessor names.

It then calls :ref:`define-condition-type/accessors
<define-condition-type/accessors>` with these new names which creates
the condition type and then calls
:ref:`define-condition-type-accessors-only
<define-condition-type-accessors-only>` as is done for the :lname:`C`
condition types.


