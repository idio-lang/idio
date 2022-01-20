:default: ``(180 240 1 45 360)``

``exp-human`` is a tuple of:

* the average gap between in-word code points

* the average gap transitioning from an in-word code point to a
  non-word code point

* a moderating factor, K:

  "tiredness" might be represented by a K < 1 and preternaturally
  consistent typing by a K > 1

* the minimum inter-code point gap, defaulting to a quarter of the
  in-word gap

* the maximum inter-code point gap, defaulting to twice the in-word
  gap

The default is roughly equivalent to a 60 wpm typist.

The algorithm used to calculate the inter-code point gap is the
inverse cumulative distribution function of the Weibull distribution.
The same as Don Libes' :manpage:`expect(1)`.
