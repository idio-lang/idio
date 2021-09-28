``struct-timeval-as-string`` is set to be the printer for a
``libc/struct-timeval``.

The code prints the structure in a pseudo-floating point format:
:samp:`{tv_sec}.{tv_usec}` with the :samp:`{tv_usec}` part formatted
to 6 digits with leading 0 (zero) padding.
