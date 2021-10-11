Modify path `var` in various ways (*append*, *prepend*, *remove*,
*replace*, etc.).  It handles paths with whitespace and various kinds
of separators (Unix's ``:``, Windows' ``;``).

:param var: the name of the path to be manipulated, eg. PATH, CLASSPATH
:type var: symbol
:param val: the path element(s) to be added, `sep` separated
:type val: string
:param act: the action to be performed: 
:param wrt: the element in the path to be operated on
:type wrt: string

keyword arguments:

:param \:sep: the element separator, defaults to ``:``
:type \:sep: string, optional
:param \:once: do the operation once (see below), defaults to ``#f``
:type \:once: boolean, optional
:param \:test: apply the predicate on the path segment
:type \:test: predicate (function), optional

`act` can be:

.. csv-table::
   :widths: auto
   :align: left
	   
   ``first`` ``start``, prepend `val`
   ``last`` ``end``, append `val`
   ``verify``, apply the conditional operator
   ``after`` ``before``, insert `val` after/before `wrt`
   ``replace``, replace `wrt` with `val`
   ``remove``, remove `wrt`

If `:once` is `true` then

* if `act` is ``replace`` or ``remove`` perform the action once

* if `act` is one of ``first``, ``last``, ``before``, ``after`` ensure
  that `val` appears once in the result

  In particular, subsequent instances of `val` may be removed or `val`
  may not be appended depending on the current elements of `var`.

The `:test` flag might be one of the standard predicates, :ref:`d?
<d?>`, :ref:`e? <e?>` or :ref:`f? <f?>`, or something bespoke.
