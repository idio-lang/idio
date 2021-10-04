:var:`IDIO_SYSTEM_ARCH` is used in the construction of actual
directory search paths from :ref:`IDIOLIB <IDIOLIB>`.

It takes the value of either:

* ``${CC} -print-multiarch`` -- the Debian `Multiarch Architecture
  Specifiers (Tuples) <https://wiki.debian.org/Multiarch/Tuples>`_

* ``${CC} -dumpmachine`` -- the GNU triplet


