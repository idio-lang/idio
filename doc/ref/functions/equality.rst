Equality
^^^^^^^^

How *much* are two values "the same"?

:ref:`eq? <eq?>` is the most ruthless.  The two values must reference
the same memory or be otherwise indistinguishable in memory.

Numbers might fail the ``eq?`` test.  The fixnum ``1`` and the bignum
``1e0`` both represent 1 (one) but they are different types so they
can't be *that* similar.

Here we can use :ref:`eqv? <eqv?>` where we want to let some values be
the same.  Numbers and strings might be ``eqv?``.

Finally, for compound values (pairs, arrays, has tables etc.) we need
:ref:`equal? <equal?>` to walk around the structures comparing element
by element.  Here, R5RS_ suggests:

    A rule of thumb is that objects are generally `equal?` if they
    print the same.

Even this might not be enough in the general case.  For example, some
encryption algorithms will *always* produce a different digest every
time they are invoked.  These require a separate action to verify the
digest is correct.

