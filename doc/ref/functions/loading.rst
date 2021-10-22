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

If a module exports a name you can always use it directly with
:samp:`{module}/{name}` without incurring the name clashing costs of
``import``.  Unexported names cannot be accessed in this way.

