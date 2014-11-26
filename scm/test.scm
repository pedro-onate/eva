(begin
  (define f
          (lambda (n)
            (if (= n 0)
                0
                (f (- n 1)))))
  (f 1000000))

(define (f n)
            (if (= n 0)
                0
               (f (- n 1))))

(begin
	(define (f n)
		(if (= n 0) 1
			(* n (f (- n 1)))))
	(f 7))

(begin
	(define f-iter (lambda (n acc)
		(if (= n 0)
			acc
			(f-iter (- n 1) (* n acc)))))
	(f-iter 7 1))


(define factorial
  (lambda (n)
    (if (= n 0)
    	1
		(* n (factorial (- n 1))))))

(factorial 3)