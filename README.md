EvaScheme v0.1
=========

Build Instructions
------------------
```bash
make
```

Run
-------------------
```bash
./eva
```

REPL (Read-evaluate-print-loop)
-------------------
```scheme
;;; Booleans

eva> #t ; True
#t
eva> #f ; False
#f


;;; Integers

eva> 1
1

eva> (+ 3 4)
7

;;; Symbols

eva> (define x 3)
x

eva> x
3

;;; Closures

; Define factorial function

eva> (define factorial
       (lambda (n)
         (if (= n 0)
           1
           (* n (factorial (- n 1))))))

; Evaluate factorial

eva> (factorial 7)
5040
```