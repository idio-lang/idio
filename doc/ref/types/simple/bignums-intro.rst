.. _`bignum type`:

Bignum Type
===========

Bignums are "arbitrary" precision integers or floating point numbers
with a concept of exactness.

You can create and store a number with arbitrary precision but most
calculations involving bignums will "normalize" the result to a value
with up to 18 significant digits and flag the number as *inexact* to
indicate that rounding has taken place.

