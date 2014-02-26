/**
 * EvaScheme
 */

#ifndef EVASCHEME_H
#define EVASCHEME_H

#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <inttypes.h>

#ifdef __cplusplus
  extern "C" {
#endif

enum ScmType {
  NIL,
  BOOLEAN,
  INTEGER,
  SYMBOL,
  CHARACTER,
  STRING,
  PAIR,
  CLOSURE,
  PROCEDURE,
  UNBOUND,
  UNSPECIFIED,
  EOFILE,
  INVALID
};

typedef uintptr_t ScmVal;

extern ScmVal SCM_NIL;
extern ScmVal SCM_TRUE;
extern ScmVal SCM_FALSE;
extern ScmVal SCM_UNBOUND;
extern ScmVal SCM_UNSPECIFIED;
extern ScmVal SCM_EOF;

void Scm_init(size_t heap_size);
enum ScmType Scm_type(ScmVal exp);
ScmVal Scm_Boolean_new(int value);
ScmVal Scm_Pair_new(ScmVal head, ScmVal tail);

#define cons(a, b)    Scm_Pair_new(a, b)
#define car(e)        Scm_Pair_car(e)
#define cdr(e)        Scm_Pair_cdr(e)
#define caar(e)       car(car(e))
#define cadr(e)       car(cdr(e))
#define cddr(e)       cdr(cdr(e))
#define caddr(e)      car(cddr(e))
#define cdddr(e)      cdr(cddr(e))
#define cadddr(e)     car(cdddr(e))
#define set_car(e, v) Scm_Pair_set_head(e, v)
#define set_cdr(e, v) Scm_Pair_set_tail(e, v)

ScmVal Scm_Pair_head(ScmVal pair);
ScmVal Scm_Pair_tail(ScmVal pair);
void Scm_Pair_set_head(ScmVal cons, ScmVal value);
void Scm_Pair_set_tail(ScmVal cons, ScmVal value);
ScmVal Scm_Integer_new(long value);
ScmVal Scm_Symbol_new(char* symbol);
ScmVal Scm_String_new(char* value);
ScmVal Scm_Closure_new(ScmVal formals, ScmVal body, ScmVal env);
ScmVal Scm_Procedure_new(ScmVal (*fptr)(ScmVal));
ScmVal Scm_parse(FILE* stream);
void Scm_print(ScmVal exp);
ScmVal Scm_eval(ScmVal exp, ScmVal env);
ScmVal Scm_Env_new(ScmVal formals, ScmVal args, ScmVal parent);
ScmVal Scm_Env_define_symbol(ScmVal env, ScmVal symbol, ScmVal value);
ScmVal Scm_Env_lookup_symbol(ScmVal env, ScmVal symbol);

#ifdef __cplusplus
  }
#endif

#endif