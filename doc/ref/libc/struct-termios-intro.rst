struct termios
--------------

:lname:`Idio` supports tagged references to :lname:`C` ``struct
termios``:

.. code-block:: c

   struct termios
   {
     tcflag_t             c_iflag;
     tcflag_t             c_oflag;
     tcflag_t             c_cflag;
     tcflag_t             c_lflag;
     cc_t[]               c_cc;
     speed_t              c_ispeed;
     speed_t              c_ospeed;
   };

using the structure member names ``c_iflag``, ``c_oflag`` etc..

Note that SunOS does not define either of ``c_ispeed`` or ``c_ospeed``
and OpenBSD defines ``speed_t`` as an ``int``.

Linux's ``c_line`` is not supported.

See also :ref:`tcgetattr <libc/tcgetattr>` and :ref:`tcsetattr
<libc/tcsetattr>`.

