The :ref:`unicode/describe <unicode/describe>` reports a pseudo
*Unicode Character Database* entry plus the Categories and Properties
associated with the code point and indications of any Lowercase,
Uppercase or Titlecase variants and any possible *Numeric_Value*.

It will do the same for each code point in a string (which may, of
course, be more than the number of "characters").

.. code-block:: idio

   Idio> unicode/describe "é"
   00E9;;Ll;;;;;;;;;;00C9;;00C9 # Letter Lowercase Alphabetic Uppercase=00C9 Titlecase=00C9 
   Idio> unicode/describe "é"
   0065;;Ll;;;;;;;;;;0045;;0045 # Letter Lowercase Alphabetic ASCII_Hex_Digit Uppercase=0045 Titlecase=0045 
   0301;;Mn;;;;;;;;;;;; # Mark Extend 
