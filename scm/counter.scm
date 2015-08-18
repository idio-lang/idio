(letrec ((cter 
            (lambda (n c)
              (if (> n 0)
                  (begin
                    ;(print c)
                    (cter (- n 1) (+ c 1)))
                  c))))
    (cter 4000 0))

