.. idio:currentmodule:: libc

libc values
-----------

Several large groups of ``libc`` values are not listed explicitly as
they are, to a large degree, operating system-specific.

Within these groups it is possible that not all possible values exist
as they have are almost certainly single-system and have been
overlooked.  It is trivial to create such flags in their absence as
they are all ``C/int`` types:

.. code-block:: idio

   O_ODDNUMBER := C/integer-> 37

Signal Values
^^^^^^^^^^^^^

All :manpage:`signal(7)` signal names: ``SIGHUP``, ``SIGINT``,
``SIGQUIT`` etc..

You can get a list of the defined ``SIG*`` signal names with
:ref:`signal-names <libc/signal-names>`.

Errno Values
^^^^^^^^^^^^

All :manpage:`errno(3)` error names: ``EPERM``, ``EBADF`` etc..

You can get a list of the defined ``E*`` error names with
:ref:`errno-names <libc/errno-names>`.

RLIMIT Values
^^^^^^^^^^^^^

All :manpage:`getrlimit(2)` RLIMIT names: ``RLIMIT_CORE``,
``RLIMIT_NOFILE`` etc..

You can get a list of the defined ``RLIMIT_*`` names with
:ref:`rlimit-names <libc/rlimit-names>`.

Helper functions wrapper the ``getrlimit``/``setrlimit`` calls and
will translate keyword nicknames, eg. ``:NOFILE`` for
``RLIMIT_NOFILE``.

Open Flag Values
^^^^^^^^^^^^^^^^

All :manpage:`open(2)` flag names: ``O_APPEND``, ``O_EXCL`` etc..

You can get a list of the defined ``O_*`` open flag names with
:ref:`open-flag-names <libc/open-flag-names>`.

POLL Values
^^^^^^^^^^^^^

All :manpage:`poll(2)` POLL names: ``POLLIN``, ``POLLERR`` etc..

You can get a list of the defined ``POLL*`` names with
:ref:`poll-names <libc/poll-names>`.

A suite of :ref:`poll predicates`, ``POLLIN?``, ``POLLERR?`` have been
defined for convenient testing.

Other Values
^^^^^^^^^^^^

