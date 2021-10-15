.. idio:module:: SRFI-115

.. _`SRFI-115 module`:

***************
SRFI-115 Module
***************

:lname:`Scheme`'s `SRFI-115`_ defines a Regular Expressions library.

This is native :lname:`Idio` code and so is relatively slow compared
to the standard library's :ref:`POSIX regex <POSIX regex>`
functionality.  ``SRFI-115`` offers more functionality.

This library uses sexp regular expressions (SREs) rather than the more
common string based regular expression:

     This format offers many advantages, including being easier to
     read and write (notably with structured editors), easier to
     compose (with no escaping issues), and faster and simpler to
     compile.

The `SRE syntax
<https://srfi.schemers.org/srfi-115/srfi-115.html#SRE-Syntax>`_ is
immediately different, being a list, but also because the iteration
key, ``+``, ``*``, ``?``, etc., lead the term(s) they affect.

`SRFI-115`_ has plenty of documentation and examples but some
approximate [#]_ comparisons, noting ``:`` for a sequence or regexps
and ``$`` for submatches, are:

.. csv-table::
   :header: SRE, :manpage:`regex(7)`
   :align: left

   ``'(+ space)``, ``"[[:space:]]+"``
   ``'(* "abc")``, ``"(abc)*"`` [#]_
   ``'(: bol (* "ab") "c")``, ``"^(ab)*c"``
   ``'(: ($ (* "ab")) "c" eol)``, ``"((ab)*)c$"`` [#]_

.. [#] As the definitions of *classes* of words may differ.

.. [#] The :manpage:`regex(7)` match will produce a submatch.

.. [#] The :manpage:`regex(7)` match will produce two submatches, the
       SRE, one.

.. toctree::
   :maxdepth: 2

