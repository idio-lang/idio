.. _`pipe handles`:

Pipe Handles
------------

Pipe handles are the entities through which we perform command
orchestration pipelines.  They are one underlying implementation of
:ref:`handles <handle type>`.

Pipe handles only really differ from file handles in that you cannot
*seek* on a pipe handle.

You can open pipe handles directly and there are command orchestration
meta-commands like ``pipe-into`` and ``pipe-from`` which will return
pipe handles.

