define a new method for the generic function, `name`.  The generic
function `name` will be created if it does not already exist.

The `specialized-formals` take the form :samp:`[{specialized-formal}
...]` and :samp:`{specialized-formal}` can take either the form
:samp:`({name} {class})` or the form :samp:`{name}` which implies a
class of ``<object>``.

If a documentation string is supplied then it will be used for the
documentation for the generic function `name` unless `name` already
exists.

:Example:

These two declarations are equivalent:

.. code-block:: idio

   define-method (foo/1  arg) ...

   define-method (foo/1 (arg <object>)) ...

Similarly for multiple arguments:

.. code-block:: idio

   define-method (foo/2  arg1            arg2) ...

   define-method (foo/2 (arg1 <object>)  arg2) ...

   define-method (foo/2 (arg1 <object>) (arg2 <object>)) ...

   define-method (foo/2  arg1           (arg2 <object>)) ...
