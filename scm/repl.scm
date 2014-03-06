(begin
  (define (repl)
    (write "eva> ")
    (write (eval (read) (interaction-environment)))
    (write "\n")
    (repl))

  (write ".--------------------.\n")
  (write "|  EvaScheme v0.2.0  |\n")
  (write "'--------------------'\n\n")

  (repl)
)


