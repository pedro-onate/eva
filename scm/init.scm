(define (caar pair) (car (car pair)))
(define (cadr pair) (car (cdr pair)))
(define (cddr pair) (cdr (cdr pair)))
(define (caddr pair) (car (cddr pair)))
(define (cdddr pair) (cdr (cddr pair)))
(define (cadddr pair) (car (cdddr pair)))

(define (list . args) args)

(define (append lst element)
  (if (null? lst)
    element
    (cons (car lst)
          (append (cdr lst) 
                  element))))

(define (f n)
  (if (= n 0)
    0
    (f (- n 1))))

(define (loop-test)
  (time (lambda () (f 1000000))))

(define (time thunk)
  ((lambda (t0)
    (begin
      (thunk)
      (- (time-ms) t0)
    )) (time-ms)))

(load "scm/compile.scm")
(load "scm/repl.scm")
