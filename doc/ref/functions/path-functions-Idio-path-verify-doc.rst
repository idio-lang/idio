A wrapper to :ref:`path-modify <path-modify>`'s ``verify`` function.

:param var: the name of the path to be manipulated, eg. PATH, CLASSPATH
:type var: symbol

keyword arguments:

:param \:sep: the element separator, defaults to ``:``
:type \:sep: string, optional
:param \:test: apply the predicate on the path segment
:type \:test: predicate (function), **required**

.. note::

   The `:test` keyword argument is required.

:Example:

Reduce :envvar:`PATH` to those elements that are directories:

.. code-block:: idio

   path-verify 'PATH :test d?

