.. _`loading`:

Loading
^^^^^^^

You can load more source code with :ref:`load <load>`.

``load`` will search for the file to be loaded on :ref:`IDIOLIB
<IDIOLIB>` although *extensions* will be given priority over simple
files.  You should not use a filename extension, eg. :file:`.idio`.

require / provide
"""""""""""""""""

``load`` is naïve and will blindly run everything in the source file
again.

You can prevent that happening by requiring a *feature* with
:ref:`require <require>` which, if it hasn't already seen the feature,
will call ``load`` and check that the feature has been :ref:`provide
<provide>`'d afterwards.

Importing
"""""""""

Finally, you can :ref:`import <import>` a *module* (which uses
``require`` in turn).  This changes the current module's list of
imported modules and therefore the list of names it is able to see.

Some names clash, :ref:`libc <libc module>` is an egregious example,
so import wisely.

If a module exports a name you can commonly use it directly with
:samp:`{module}/{name}` without incurring the name clashing costs of
``import``.  Unexported names cannot be accessed in this way.

.. attention::

   This use of direct references to names is an *artefact* and
   requires that someone somewhere has previously imported
   :samp:`{module}` so that its list of exported names can be
   consulted.

   One of the beneficiaries of this is ``libc`` which is imported by
   ``job-control`` which is, in turn, imported during bootstrap.  Now
   all ``libc`` exported names are accessible as :samp:`libc/{name}`
   and we can avoid parameter-type conflicts between calling, say, the
   external command, :program:`mkdir` and the ``libc`` function
   :ref:`libc/mkdir <ref:libc/mkdir>`.
