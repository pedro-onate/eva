(begin
  (define (caar list) (car (car list)))
  (define (cdar list) (cdr (car list)))
  (define (cadr list) (car (cdr list)))
  (define (cddr list) (cdr (cdr list)))
  (define (caaar list) (car (caar list)))
  (define (cdaar list) (cdr (caar list)))
  (define (cadar list) (car (cdar list)))
  (define (cddar list) (cdr (cdar list)))
  (define (caadr list) (car (cadr list)))
  (define (cdadr list) (cdr (cadr list)))
  (define (caddr list) (car (cddr list)))
  (define (cdddr list) (cdr (cddr list)))
  (define (caaaar list) (car (caaar list)))
  (define (cdaaar list) (cdr (caaar list)))
  (define (cadaar list) (car (cdaar list)))
  (define (cddaar list) (cdr (cdaar list)))
  (define (caadar list) (car (cadar list)))
  (define (cdadar list) (cdr (cadar list)))
  (define (caddar list) (car (cddar list)))
  (define (cdddar list) (cdr (cddar list)))
  (define (caaadr list) (car (caadr list)))
  (define (cdaadr list) (cdr (caadr list)))
  (define (cadadr list) (car (cdadr list)))
  (define (cddadr list) (cdr (cdadr list)))
  (define (caaddr list) (car (caddr list)))
  (define (cdaddr list) (cdr (caddr list)))
  (define (cadddr list) (car (cdddr list)))
  (define (cddddr list) (cdr (cdddr list)))
  
  (define (list . l) l)
  
  (define (foldr fun val lst)
      (if (null? lst)
          val
          (fun (car lst) (foldr fun val (cdr lst)))))
  
  (define (foldl fun acc lst)
    (if (null? lst)
        acc
        (foldl fun (fun acc (car lst)) (cdr lst))))
  
  (define fold foldl)
  
  (define (reduce fun lst)
    (fold fun (car lst) (cdr lst)))
  
  (define (map fun lst)
      (foldr (lambda (a b) (cons (fun a) b)) '() lst))
  
  (define (append lst val)
    (foldr cons val lst))
)