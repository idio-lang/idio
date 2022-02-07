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
   Idio> describe "🏴󠁧󠁢󠁳󠁣󠁴󠁿"
   1F3F4;;So;;;;;;;;;;;; # Symbol
   E0067;;Cf;;;;;;;;;;;; # Extend
   E0062;;Cf;;;;;;;;;;;; # Extend
   E0073;;Cf;;;;;;;;;;;; # Extend
   E0063;;Cf;;;;;;;;;;;; # Extend
   E0074;;Cf;;;;;;;;;;;; # Extend
   E007F;;Cf;;;;;;;;;;;; # Extend


The third string is an example of an emoji, in this case, ``flag:
Scotland``, 🏴󠁧󠁢󠁳󠁣󠁴󠁿, in the *Subdivision Flags* Category.  Don't worry if
you can't actually see a Saltire, maybe just a black flag, as desktop
browser support is poor, mobile phone support is better.  The point
being that a single (double-width) "character" is, in this case,
constructed from seven Unicode code points:

.. csv-table::
   :widths: auto
   :align: left

   1F3F4, WAVING BLACK FLAG
   E0067, TAG LATIN SMALL LETTER G
   E0062, TAG LATIN SMALL LETTER B
   E0073, TAG LATIN SMALL LETTER S
   E0063, TAG LATIN SMALL LETTER C
   E0074, TAG LATIN SMALL LETTER T
   E007F, CANCEL TAG

revealing the GB then SCT elements.  There are corresponding WLS and
ENG variants for the flags for Wales and England, respectively.  These
abbreviations appear to be derived from `ISO 3166-2:GB
<https://en.wikipedia.org/wiki/ISO_3166-2:GB>`_.
