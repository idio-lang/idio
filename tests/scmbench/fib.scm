;;; FIB -- A classic benchmark, computes fib(30) inefficiently.

(define (fib n)
  (if (< n 2)
    n
    (+ (fib (- n 1))
       (fib (- n 2)))))

(fib 30)
