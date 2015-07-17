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

(define (zero? n)
  (= n 0))

(define (not val)
  (if val #f #t))

(define (f n)
  (if (= n 0)
    0
    (f (- n 1))))

;;;======================================================================
;;; foldr (a b -> a) c [lst] -> val
;;;======================================================================
(define foldr (lambda (fun val lst)
    (if (null? lst)
        val
        (fun (car lst) (foldr fun val (cdr lst))))))

;;;======================================================================
;;; foldl
;;;======================================================================
(define foldl (lambda (fun acc lst)
  (if (null? lst)
      acc
      (foldl fun (fun acc (car lst)) (cdr lst)))))

(define fold foldl)

(define reduce (lambda (fun lst)
  (fold fun (car lst) (cdr lst))))

;;;======================================================================
;;; map
;;; Usage (map <proc> '(v1 v2 ...)) => ((<proc> v1) (<proc> v2) ...)
;;;======================================================================
(define map (lambda (fun lst)
    (foldr (lambda (a b) (cons (fun a) b)) '() lst)))

(define append (lambda (lst val)
  (foldr cons val lst)))

;;;======================================================================
;;; quasiquote
;;;======================================================================
(define quasiquote
  (macro (lambda (exp)
    (if (pair? exp)
      (if (eq? (car exp) 'unquote)
        (cadr exp)
        (if (pair? (car exp))
          (if (eq? (caar exp) 'unquote-splicing)
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

;;;======================================================================
;;; for-each
;;; Usage (map <proc> '(v1 v2 ...)) => ((<proc> v1) (<proc> v2) ...)
;;;======================================================================
(define for-each (lambda (fun lst)
    (if (not (null? lst))
        (begin (fun (car lst))
               (for-each fun (cdr lst))))))

;;;======================================================================
;;; unzip-list
;;; Usage: (unzip-list '((k1 v1) (k2 v2) ...)) => ((k1 k2 ...) (v1 v2 ...))
;;;======================================================================
(define unzip-list (lambda (lst)
    (if (null? lst)
        (list '() '())
        ((lambda (unzipped-rest)
            (list (cons (caar lst) (car unzipped-rest))
                  (cons (cadar lst) (cadr unzipped-rest))))
         (unzip-list (cdr lst))))))

;;;======================================================================
;;; let
;;; Syntax (let [tag] ([(<var> <init>)]*) <body>)
;;;======================================================================
(define let
  (macro (lambda args
    (if (symbol? (car args))
        (let ((tag (car args))        ;Look Ma - using macro before its defined :)!
              (bindings (cadr args))
              (body (cddr args)))
            (let* ((unzipped (unzip-list bindings))
                   (variables (car unzipped))
                   (values (cadr unzipped)))
            `((letrec ((,tag (lambda ,variables ,@body))) ,tag) ,@values)))
        ((lambda (unzipped-bindings body)
            `((lambda ,(car unzipped-bindings) ,@body) ,@(cadr unzipped-bindings)))
         (unzip-list (car args)) (cdr args))))))

;;;======================================================================
;;; let*
;;; Syntax (let* ([(<var> <init>)]*) <body>)
;;;======================================================================
(define let*
  (macro (lambda (bindings . body)
   (if (null? bindings)
       `(let () ,@body)
       (if (null? (cdr bindings))
           `(let ,bindings ,@body)
           `(let (,(car bindings)) (let* ,(cdr bindings) ,@body)))))))

;;;======================================================================
;;; letrec
;;; Syntax (let [tag] ([(var init)]*) )
;;;======================================================================
(define letrec
  (macro (lambda (bindings . body)
    (if (null? bindings)
        `(let () ,@body)
        (let ((unspecified '(if #f #f #f)))
            `(let ,(map (lambda (binding) (list (car binding) unspecified)) bindings)
                  ,@(map (lambda (binding) (list 'set! (car binding) (cadr binding))) bindings)
                  ,@body))))))

;;;======================================================================
;;; and
;;; Syntax (and [arg]*)
;;;======================================================================
(define and
  (macro (lambda args
    (if (null? args)
        #t
        (if (null? (cdr args))
            (car args)
            `(if ,(car args)
              (and ,@(cdr args))
              #f))))))

;;;======================================================================
;;; or
;;; Syntax (or [arg]*)
;;;======================================================================
(define or
  (macro (lambda args
    (if (null? args)
        #f
        (if (null? (cdr args))
            (car args)
            (let ((x (gensym)))
              `(let ((,x ,(car args)))
                (if ,x
                    ,x
                    (or ,@(cdr args))))))))))