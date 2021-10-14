.. _`templates`:

**************
Idio Templates
**************

Templates in :lname:`Idio` are like macros in many other
:lname:`Lisp`-ish languages.  They allow the user to "generate code".

Their creation and usage is fraught with issues from hygiene, scope,
arguments, evaluation (timing, context), ... well, you get the
picture.  It looks easy and obvious but very quickly becomes
complicated and possibly intractable.

To help for those less familiar with this sort of thing, consider
:lname:`C` pre-processing of a :lname:`C` source file where you've
defined some complex macros which affect code generation (a bit more
complex than ``#ifdef``, say).  Pre-processing, whilst part of
compilation, is run before the compiler proper gets a look in and uses
a different set of variables that, subsequently, the compiler (or you)
have no visibility of.  You only see the *results* of the :lname:`C`
macro expansion and nothing of the macros or their variables at all.

Templates are much the same in that they are run before the (final)
draft of the code is evaluated and use a context (namespace and
"thread" of control) separate from regular user contexts except the
language of templates is regular :lname:`Idio` and not some special
pre-processor language and, to make matters more complicated, you can
use any :lname:`Idio` function including those you've just defined
(including if they were just defined by a template).

.. toctree::
   :maxdepth: 1

