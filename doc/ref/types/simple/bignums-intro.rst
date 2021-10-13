.. _`bignum type`:

Bignum Type
===========

Bignums are "arbitrary" precision integers or floating point numbers
with a concept of exactness.

In principle, bignums allow arbitrary precision but most operations
involving bignums (including reading them in!) will "normalize" the
result to a value with up to 18 significant digits and flag the number
as *inexact* to indicate that rounding has taken place.

A bignum exponent is a signed 32-bit entity.

There is no support for special values such as "not a number" and
infinities.
