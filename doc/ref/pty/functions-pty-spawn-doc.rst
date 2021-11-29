Spawn a process defined by `argv`

:param argv: command and argument list
:type argv: list
:param mreader: function to read from master device, defaults to simple internal function
:type mreader: function, optional
:param inreader: function to read from stdin, defaults to simple internal function
:type inreader: function, optional
:return: result from :ref:`waitpid`
:rtype: list
:raises ^system-error:

This function invokes a simple copy of data to and from the
sub-process.

