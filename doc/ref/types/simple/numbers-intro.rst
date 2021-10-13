.. _`number types`:

Number Types
============

Numbers in :lname:`Idio` are either "small" integer :ref:`fixnums
<fixnum type>` or "arbitrary" precision :ref:`bignums <bignum type>`
which can be integers, floating point numbers and support a notion of
exactness.

You can also use the :lname:`C` base types, ``C/char``, ``C/int``,
etc. and supported ``typedef``\ s thereof, although they are usually
passed verbatim within :lname:`C`-oriented modules like :ref:`libc
<libc module>`.

Reader Forms
------------

Reading numbers or, rather, identifying numbers as distinct from
"words", is complicated as we need to differentiate between, say,
``3e3``, a number, and ``2pi``, a symbol (almost certainly referencing
a number).

Broadly, a number looks like:

* :samp:`[+-]?[0-9]+`

* :samp:`[+-]?[0-9]+.[0-9]*`
  
* :samp:`[+-]?[0-9]+.[0-9]*{E}[+-]?[0-9]+`

(There must be a numeric part before any ``.`` otherwise it will be
interpreted as the :ref:`value-index <value-index>` operator.)

The exponent character, :samp:`{E}` in the last example, can be one of
several characters:

* for base-16 numbers (see below): ``e`` and ``E`` quite obviously
  clash with the possible digits of hexadecimal numbers so for base-16
  numbers you can use the exponent characters ``s``/``S`` or
  ``l``/``L``

* other exponent-able numbers can use ``d``/``D``, ``e``/``E``,
  ``f``/``F`` -- and ``s``/``S`` and ``l``/``L``

Hence, ``+10``, ``1.``, ``-20.1``, ``0.3e1``, ``-4e-5``, ``6L7`` are
all valid number formats and a number will be constructed by the
reader.

Non-base-10 Numbers
^^^^^^^^^^^^^^^^^^^

There are several reader input forms for non-base-10 numbers all of
which effectively call :ref:`read-number <read-number>` with an
appropriate `radix`:

.. csv-table:: Non-base-10 reader number formats
   :header: "form", "radix", "example", "decimal equivalent"
   :widths: auto
   :align: left

   ``#b``, 2,  ``#b101``, 5
   ``#o``, 8,  ``#o101``, 65
   ``#d``, 10, ``#d101``, 101
   ``#x``, 16, ``#x101``, 257

``read-number`` supports bases up to 36 although there are no
dedicated reader input forms.
