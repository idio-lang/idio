Associate `name` with a syntax transformer using `rules`.

`rules` is a list:

    :samp:`(syntax-rules ({keywords}) (({pattern} {expansion}) ...))`

* the symbol ``syntax-rules`` as the first element

* a list of `keywords` to be matched for as syntactic structure then
  discarded

  This is often an empty list.

* a list of :samp:`({pattern} {expansion})` rules.  If the `pattern`
  matches then apply the `expansion`

  The first element of the pattern match will always be the name of
  the syntax transformer -- otherwise we wouldn't be executing this
  expander -- and is ignored.

  The pattern must have a placeholder for the function name, though,
  which is often represented as ``_``.

.. note::

   The `expansion` can produce an instance of the same syntax
   transformer where one `pattern` might have an `expansion` that,
   say, fills in a default parameter for another `pattern` to match.

   As the first word of the resultant expression of the first
   `expansion` is a syntax transformer, the evaluator will loop round
   again, this time matching against the other `pattern` and producing
   its `expansion` in turn.

   Obviously, you should avoid circular loops.

Both `pattern` and `expansion` support ellipses.  Here, if a term in
`pattern` is followed by ellipses then multiple alike elements are
gathered together and spliced in *en masse* in the corresponding
term-ellipses part of `expansion`.  There is an example in
:ref:`define-syntax-rule <define-syntax-rule>`.

:Example:

Define a "wordy" conditional expression which makes use of keywords
``then`` and ``else`` which are simply syntactic fluff and are
discarded:

.. code-block:: idio

   (define-syntax wordy-if
		  (syntax-rules (then else)
		    ((_ condition then consequent)
		     (and condition consequent))
		    ((_ condition then consequent else alternative)
		     (cond
		      (condition consequent)
		      (else alternative)))))

   wordy-if (frob a) then b
   ;; (and (frob a) b)

   wordy-if z then #n else (call it)
   ;; (cond
   ;;   (z #n)
   ;;   (else (call it)))

Also note how the expansions are not required to be similar.  Here,
``wordy-if`` uses two different expansions, one with :ref:`and <and
special form>`, the other with :ref:`cond <cond special form>`.
