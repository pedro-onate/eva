/**
 * EvaScheme
 */

#ifndef EVASCHEME_H
#define EVASCHEME_H

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
  EOF_OBJ,
  PORT,
  MACRO,
  INVALID
};

typedef void* ScmVal;

extern ScmVal SCM_NIL;
extern ScmVal SCM_TRUE;
extern ScmVal SCM_FALSE;
extern ScmVal SCM_UNBOUND;
extern ScmVal SCM_UNSPECIFIED;
extern ScmVal SCM_EOF;

#define cons(a, b)    Scm_Pair_new(a, b)
#define car(e)        Scm_Pair_car(e)
#define cdr(e)        Scm_Pair_cdr(e)
#define caar(e)       car(car(e))
#define cadr(e)       car(cdr(e))
#define cddr(e)       cdr(cdr(e))
#define caadr(e)      car(cadr(e))
#define caddr(e)      car(cddr(e))
#define cdadr(e)      cdr(cadr(e))
#define cdddr(e)      cdr(cddr(e))
#define cadddr(e)     car(cdddr(e))
#define set_car(e, v) Scm_Pair_set_head(e, v)
#define set_cdr(e, v) Scm_Pair_set_tail(e, v)

void Scm_init(size_t heap_size);
enum ScmType Scm_type(ScmVal exp);
ScmVal Scm_Boolean_new(int value);
ScmVal Scm_Pair_new(ScmVal head, ScmVal tail);
ScmVal Scm_Pair_head(ScmVal pair);
ScmVal Scm_Pair_tail(ScmVal pair);
void Scm_Pair_set_head(ScmVal cons, ScmVal value);
void Scm_Pair_set_tail(ScmVal cons, ScmVal value);
ScmVal Scm_Integer_new(long value);
ScmVal Scm_Symbol_new(char* symbol);
ScmVal Scm_String_new(char* value);
ScmVal Scm_Character_new(int c);
ScmVal Scm_Port_new(FILE* stream);
ScmVal Scm_Port_read_char(ScmVal port);
ScmVal Scm_Port_peek_char(ScmVal port);
ScmVal Scm_Port_write_char(ScmVal port, ScmVal c);
ScmVal Scm_Port_write(ScmVal port, ScmVal obj);
ScmVal Scm_Port_read(ScmVal port);
ScmVal Scm_is_eof_obj(ScmVal port);
ScmVal Scm_Closure_new(ScmVal formals, ScmVal body, ScmVal env);
ScmVal Scm_Procedure_new(ScmVal(*fptr)(ScmVal));
ScmVal Scm_parse(FILE* istream);
void Scm_print(FILE* ostream, ScmVal exp);
ScmVal Scm_eval(ScmVal exp, ScmVal env);
ScmVal Scm_Env_new(ScmVal formals, ScmVal args, ScmVal parent);
ScmVal Scm_Env_define_symbol(ScmVal env, ScmVal symbol, ScmVal value);
ScmVal Scm_top_level_env();
void Scm_define(ScmVal env, char*, ScmVal value);
ScmVal Scm_Env_lookup_symbol(ScmVal env, ScmVal symbol);
ScmVal Scm_Macro_new(ScmVal transformer);

#ifdef __cplusplus
  }
#endif

#endif