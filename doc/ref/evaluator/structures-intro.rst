.. idio:currentmodule:: evaluate

evaluate structures
-------------------

The ``evaluate`` module uses an evaluation environment ("eenv") to
manage the data it requires to evaluate source code expressions.

Several parts of the data structure are created directly for and are
*shared with* the *VM*'s execution environment ("xenv").
