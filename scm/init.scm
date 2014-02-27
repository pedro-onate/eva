(define caar (lambda (list) (car (car list))))
(define cdar (lambda (list) (cdr (car list))))
(define cadr (lambda (list) (car (cdr list))))
(define cddr (lambda (list) (cdr (cdr list))))
(define caaar (lambda (list) (car (caar list))))
(define cdaar (lambda (list) (cdr (caar list))))
(define cadar (lambda (list) (car (cdar list))))
(define cddar (lambda (list) (cdr (cdar list))))
(define caadr (lambda (list) (car (cadr list))))
(define cdadr (lambda (list) (cdr (cadr list))))
(define caddr (lambda (list) (car (cddr list))))
(define cdddr (lambda (list) (cdr (cddr list))))

(define list (lambda exp exp))

(define foldr (lambda (fun val lst)
    (if (null? lst)
        val
        (fun (car lst) (foldr fun val (cdr lst))))))

(define foldl (lambda (fun acc lst)
  (if (null? lst)
      acc
      (foldl fun (fun acc (car lst)) (cdr lst)))))

(define fold foldl)

(define reduce (lambda (fun lst)
  (fold fun (car lst) (cdr lst))))

(define map (lambda (fun lst)
    (foldr (lambda (a b) (cons (fun a) b)) '() lst)))

(define append (lambda (lst val)
  (foldr cons val lst)))

(define quasiquote
  (macro (lambda (exp)
    (if (pair? exp)
      (if (eq? (car exp) 'unquote)
        (cadr exp)
        (if (pair? (car exp))
          (if (eq? (caar exp) 'unquote-splice)
            (list 'append
                 (cadar exp)
                 (list 'quasiquote (cdr exp)))
            (list 'cons
                  (list 'quasiquote (car exp))
                  (list 'quasiquote (cdr exp))))
          (list 'cons
                  (list 'quasiquote (car exp))
                  (list 'quasiquote (cdr exp)))))
        (list 'quote exp)))))

(load "scm/repl.scm")