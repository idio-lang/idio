* `location` is set to be the job that failed such that you might:

  .. code-block:: idio

     job := idio-error-location c
     format-job-detail job

* `status` is the Unix *status* value

  You will need to use the `libc` functions :ref:`libc/WIFEXITED
  <libc/WIFEXITED>`, :ref:`libc/WEXITSTATUS <libc/WEXITSTATUS>`
  etc. to decode it.

  
