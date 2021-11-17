:default: ``"Real %.3R\nUser %.3U\nSyst %.3S\n"``

:var:`TIMEFORMAT` is a string describing the report for the
:ref:`time-function <time-function>` command (or `time`
:ref:`meta-command <job-control/job meta-commands>`).

It allows for the use of three ``'keyed`` format characters, ``R``,
``U`` and ``S``, for the real, user and system time, respectively.
The precision given is for the fractional seconds.

