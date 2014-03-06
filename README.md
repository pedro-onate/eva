#EvaScheme v0.2.0

##Build

```bash
make
```

##Run

```bash
./eva
```

##REPL (read-evaluate-print-loop)

Booleans

```scheme
; Comments begin with ';' and continue until the end of the line
eva> #t ; True
#t

eva> #f ; False
#f
```

Integers

```scheme
eva> 1 ; Integers evaluate to themselves
1
```

Definitions

```scheme
eva> (define x 3) ; The value 3 will be assigned to the symbol x

```

Symbols

```scheme
eva> x ; Evaluates to 3 since we have bound the value 3 to it above
3

eva> 'x ; Quoting a value makes it evaluate to itself
x
```

Functions

```scheme
; Defines a function named 'factorial' that takes a single argument 'n'
eva> (define (factorial n) 
        (if (= n 0)
           1
           (* n (factorial (- n 1)))))

eva> (factorial 7) ; Applies the function 'factorial' to its argument '7'
5040
```