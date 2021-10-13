``cond-expand`` reduces `clauses` to the first
:token:`~SRFI-0:cond_expand_clause` where the clause's
:token:`~SRFI-0:feature_requirement` is `true`

``cond-expand`` is a template and so, in effect, it reduces the set of
`clauses` supplied to just the first that is `true`.

The :token:`~SRFI-0:feature_requirement` for each clause in `clauses`
is constructed from the presence of
:token:`~SRFI-0:feature_identifier`\ s or logical combinations of
those features.  The features themselves are set by :program:`idio` or
can be augmented by users.

The `clauses` for ``cond-expand`` are interpreted as the (SRFI-0_)
grammar :token:`~SRFI-0:conditional_expansion_form`:

.. productionlist:: SRFI-0
   conditional_expansion_form :   (cond-expand `cond_expand_clause`+)
			      : | (cond-expand `cond_expand_clause`* (else expression*))
   cond_expand_clause : (`feature_requirement` expression*)
   feature_requirement :   `feature_identifier`
		       : | (and `feature_requirement`*)
		       : | (or  `feature_requirement`*)
		       : | (not `feature_requirement`)
   feature_identifier : a symbol or string, see below

:Example:

Several tests of standard library calls in :ref:`libc <libc module>`
differ between operating systems.  We can specialize as appropriate
and ``cond-expand`` results in just a single ``libc-wrap-error-load``
expression in the byte code.

.. code-block:: idio

   (cond-expand
    ((or "uname/sysname/FreeBSD"
         "uname/sysname/OpenBSD"
         "uname/sysname/Darwin") {
      libc-wrap-error-load "libc-wrap-errors/EGID-set-invalid-gid.idio" "Operation not permitted" EPERM "setegid"
    })
    (else {
      libc-wrap-error-load "libc-wrap-errors/EGID-set-invalid-gid.idio" "Invalid argument" EINVAL "setegid"
    })
   )

.. seealso:: :ref:`Idio features` and :ref:`%add-feature <%add-feature>`

