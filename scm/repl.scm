(begin
  (define (repl)
    (write "eva> ")
    (write (eval (read) (interaction-environment)))
    (write "\n")
    (repl))

  (write ".------------------.\n")
  (write "|  EvaScheme v0.1  |\n")
  (write "'------------------'\n\n")

  (repl)
)


