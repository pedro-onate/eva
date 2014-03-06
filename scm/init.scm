(define (caar pair) (car (car pair)))
(define (cadr pair) (car (cdr pair)))

(define (time thunk)
  ((lambda (t0)
    (begin
      (thunk)
      (- (time-ms) t0)
    )) (time-ms)))

(load "scm/repl.scm")
