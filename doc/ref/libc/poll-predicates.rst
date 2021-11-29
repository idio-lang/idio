.. _`poll predicates`:

poll predicates
^^^^^^^^^^^^^^^

The `C/int` :lname:`C` macros, ``POLLIN``, ``POLLERR``, etc., are
managed as bitmasks in a `C/short` in :manpage:`poll(2)`'s ``struct
pollfd``.

This is complicated because :lname:`Idio` doesn't use a `C/int` of 0
(zero) as `false` and the :lname:`C` bitwise operators, :ref:`C/&
<C/&>`, etc., only operate on `C/int` types.

So a series of predicates exist which do the right thing, ``POLLIN?``
will test that the bit-field represented by the :lname:`C` macro,
``POLLIN`` is set in the `C/short` argument.

When registering a file descriptor or file handle you can pass a list
of `C/int` values which will be OR-ed together into a :lname:`C`
``short``.  Later, the list of event results from :ref:`poller-poll
<libc/poller-poll>` will have an ``revents`` value which you can test
with these predicates:

.. code-block:: idio

   fh := open-input-file ...
   poller := libc/make-poller (list fh libc/POLLIN)
   r := libc/poller-poll poller 1000

   for p in r {
     fdh := ph p
     revents := pht p
     (cond
      ((libc/POLLERR? revents) {
         libc/poller-deregister poller fdh
       })
      ((libc/POLLIN? revents) {
         ...
       }))
   }
