:var:`*idio-features*` is the list of features that can be tested with
:ref:`cond-expand <cond-expand>`.  They are symbols and strings.

There are no values associated with the list elements, only presence
or absence.

By default, :lname:`Idio` adds the following features:

* from :manpage:`uname(2)` the appropriate :samp:`{value}`'s for:

  * :samp:`"uname/sysname/{value}"`

  * :samp:`"uname/nodename/{value}"`

  * :samp:`"uname/release/{value}"`

  * :samp:`"uname/machine/{value}"`

* :samp:`sizeof/pointer/{value}` -- where `value` is ``sizeof (void
  *) * CHAR_BIT``

* ``F_DUPFD_CLOEXEC`` if :ref:`libc <libc module>` defines
  ``F_DUPFD_CLOEXEC``

* ``/dev/fd`` if this system uses the :file:`/dev/fd` system for
  accessing *all* file descriptors

* ``IDIO_DEBUG`` if this executable was compiled with debug enabled

There are some operating system-specific features:

* for Linux (``"uname/sysname/Linux"``), from :file:`/etc/os-release`:

  * :samp:`"os-release/ID/{value}"` for the value of the ``ID``
    variable

  * :samp:`"os-release/VERSION_ID/{value}"` for the value of the
    ``VERSION_ID`` variable

  * ``virtualisation/WSL`` if running under Windows Subsystem for
    Linux (for WSL1 at any rate)

* for SunOS (``"uname/sysname/SunOS"``), from :file:`/etc/release`:

  * :samp:`"release/{value}"` where :samp:`{value}` is the first word
    from that file

.. seealso:: :ref:`%add-feature <%add-feature>`
