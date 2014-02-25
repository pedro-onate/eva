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

extern struct ScmVal* SCM_NIL;
extern struct ScmVal* SCM_TRUE;
extern struct ScmVal* SCM_FALSE;
extern struct ScmVal* SCM_UNBOUND;
extern struct ScmVal* SCM_UNSPECIFIED;
extern struct ScmVal* SCM_EOF;

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

struct ScmVal {
  enum ScmType type;
};

struct ScmObj {
  enum ScmType type;
};

struct ScmPair {
  struct ScmVal* head;
  struct ScmVal* tail;
};

struct ScmClosure {
  enum ScmType   type;
  struct ScmVal* formals;
  struct ScmVal* body;
  struct ScmVal* env;
};

struct ScmProcedure {
  enum ScmType   type;
  struct ScmVal* (*fptr)(struct ScmVal*);
};


void Scm_init(size_t heap_size);
enum ScmType Scm_type(struct ScmVal* exp);
struct ScmVal* Scm_Boolean_new(int value);
struct ScmVal* Scm_Pair_new(struct ScmVal* head, struct ScmVal* tail);

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

struct ScmVal* Scm_Pair_head(struct ScmVal* pair);
struct ScmVal* Scm_Pair_tail(struct ScmVal* pair);
void Scm_Pair_set_head(struct ScmVal* cons, struct ScmVal* value);
void Scm_Pair_set_tail(struct ScmVal* cons, struct ScmVal* value);
struct ScmVal* Scm_Integer_new(long value);
struct ScmVal* Scm_Symbol_new(char* symbol);
struct ScmVal* Scm_String_new(char* value);
struct ScmVal* Scm_Closure_new(struct ScmVal* formals, struct ScmVal* body, struct ScmVal* env);
struct ScmVal* Scm_Procedure_new(struct ScmVal* (*fptr)(struct ScmVal*));
struct ScmVal* Scm_parse(FILE* stream);
void Scm_print(struct ScmVal* exp);
struct ScmVal* Scm_eval(struct ScmVal* exp, struct ScmVal* env);
struct ScmVal* Scm_Env_new(struct ScmVal* formals, struct ScmVal* args, struct ScmVal* parent);
struct ScmVal* Scm_Env_define_symbol(struct ScmVal* env, struct ScmVal* symbol, struct ScmVal* value);
struct ScmVal* Scm_Env_lookup_symbol(struct ScmVal* env, struct ScmVal* symbol);

#ifdef __cplusplus
  }
#endif

#endif