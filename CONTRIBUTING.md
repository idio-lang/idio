Any contribution you make to this repository will be under the [Apache
2 Licence](https://www.apache.org/licenses/LICENSE-2.0) as dictated by
Section 5 of that licence:

```
   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

```

Contributors must sign-off on each commit by adding a `Signed-off-by:
...` line to commit messages to certify that they have the right to
submit the code they are contributing to the project according to the
[Developer Certificate of Origin
(DCO)](https://developercertificate.org).

Test suites should look to:

* generate every possible error in the *C* code or place a comment in
  *the C* code as to why it can't be generated ("inconceivable!")

  These are generally in `tests/test-X-error.idio` files.

* probe boundary cases

  These are generally in `tests/test-X.idio` files.

* give decent code coverage

  Code coverage doesn't guarantee the code works it just means that at
  least something caused the code to be exercised (and it didn't
  generate an error).

Documentation might be more widespread:

* functions defined in both *C* and *Idio* support a *docstring*

  These should be reStructuredText in the style of
  https://readthedocs.org.

* variables (and more awkward functions) can be documented in the
  `doc/ref` hierarchy

* examples might want to go in the [Idio User
  Guide](https://github.com/idio-lang/guide)

* complex contributions might want to be added to
  [DIPS](https://github.com/idio-lang/DIPS)

