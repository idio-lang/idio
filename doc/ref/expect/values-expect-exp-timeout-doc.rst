:default: ``-1``

``exp-timeout`` is the timeout used by :ref:`exp-case
<expect/exp-case>` measured in seconds.

A value of 0 represents a poll of the device.

A value of -1, the default, represents an infinite timeout.

.. note::

   :manpage:`expect(1)` defaults to a timeout of 10 (seconds).  Don
   Libes has indicated some regret over this value and that
   it should be off by default (see `Writing a Tcl
   Extention in only... 7 Years` in the Proceedings of the Fifth
   Annual Tcl/Tk Workshop Boston, Massachusetts, July 1997).
