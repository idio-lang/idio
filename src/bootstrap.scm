
(define (map* fn . l) 		; A map which accepts dotted lists (arg lists
  (cond 			; must be "isomorph"
   ((null? (car l)) '())
   ((pair? (car l)) (cons (apply fn      (map car l))
			  (apply map* fn (map cdr l))))
   (else            (apply fn l))))

(define (application-expander x e)
  (map* (lambda (y) (e y e)) x))


(define (initial-expander x e)
 (cond
   ((not (pair? x))   	      x)
   ((not (symbol? (car x)))  (application-expander x e))
   (else  (let ((functor (car x)))
	    (cond
	       ((expander? functor) ((cdr (assq functor *expander-list*)) x e))
	       (else (application-expander x e)))))))


(define (macro-expand x)
  (initial-expander x initial-expander))

(load-file "s9.scm")
(load-file "test.scm")
