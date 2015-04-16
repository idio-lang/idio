
(load "s9.scm")
(load "test.scm")

;; (define (map* fn . l) 		; A map which accepts dotted lists (arg lists
;;   (cond 			; must be "isomorph"
;;    ((null? (car l)) '())
;;    ((pair? (car l)) (cons (apply fn      (map car l))
;; 			  (apply map* fn (map cdr l))))
;;    (else            (apply fn l))))

;; (define (application-expander x e)
;;   (map* (lambda (y) (e y e)) x))


;; (define (initial-expander x e)
;;  (cond
;;    ((not (pair? x))   	      x)
;;    ((not (symbol? (car x)))  (application-expander x e))
;;    (else  (let ((functor (car x)))
;; 	    (cond
;; 	       ((expander? functor) ((cdr (assq functor *expander-list*)) x e))
;; 	       (else (application-expander x e)))))))

;; ;; will be redefined but macro-expand requires a placeholder
;; (define (syntax-expand x) x)

;; (define (macro-expand x)
;;   (initial-expander (syntax-expand x) initial-expander))

;; (define (macro-expand* exp)
;;   (let ((new (macro-expand exp)))
;;     (if (equal? new exp)
;;         new
;;         (macro-expand* new))))

;; (define %macro-expand*
;;   (let ((expand (lambda (x)
;; 		  ;; as macro-expand without syntax expand (used by full-syntax)
;; 		  (initial-expander x (lambda (x e) x)))))
;;     (lambda (exp)
;;       (let ((new (expand exp)))
;; 	(if (equal? new exp)
;; 	    new
;; 	    (%macro-expand* new))))))
