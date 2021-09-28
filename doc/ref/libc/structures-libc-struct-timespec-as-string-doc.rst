``struct-timespec-as-string`` is set to be the printer for a
``libc/struct-timespec``.

The code prints the structure in a pseudo-floating point format:
:samp:`{tv_sec}.{tv_nsec}` with the :samp:`{tv_nsec}` part formatted
to 9 digits with leading 0 (zero) padding.
