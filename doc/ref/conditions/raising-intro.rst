Raising Conditions
------------------

You can raise conditions in user-code.

You can raise standard :lname:`Idio` errors with :ref:`error <error>`
for a generic :ref:`^idio-error <^idio-error>` or :ref:`error/type
<error/type>` for a more specific condition type.

You can raise a specific condition with :ref:`raise <raise>` which
supports any kind of condition type, including user-defined ones.

Finally, :ref:`reraise <reraise>` supports a niche problem where you
recognise that, as a handler potentially several layers down in the
trap-handling stack, you are raising a new condition for which the
user might have defined a handler some layers above where you are now.
What ``reraise`` does is look for the highest numbered trap-handler on
the stack and start (again) with it.

