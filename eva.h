/**
 * EvaScheme
 */

#ifndef EVA_H
#define EVA_H

#ifdef __cplusplus
  extern "C" {
#endif

typedef void* ScmVal;

enum ScmType {
  kScmInvalidType = -1,
  kScmNilType,
  kScmBooleanType,
  kScmIntegerType,
  kScmSymbolType,
  kScmCharacterType,
  kScmStringType,
  kScmPairType,
  kScmClosureType,
  kScmProcedureType,
  kScmUnboundType,
  kScmUnspecifiedType,
  kScmEOFObjType,
  kScmPortType,
  kScmVectorType
};

extern ScmVal SCM_NIL;
extern ScmVal SCM_TRUE;
extern ScmVal SCM_FALSE;
extern ScmVal SCM_UNBOUND;
extern ScmVal SCM_UNSPECIFIED;
extern ScmVal SCM_EOF;

#define Scm_cons(a, b)    Scm_Pair_new(a, b)
#define Scm_car(e)        Scm_Pair_car(e)
#define Scm_cdr(e)        Scm_Pair_cdr(e)
#define Scm_caar(e)       Scm_car(Scm_car(e))
#define Scm_cadr(e)       Scm_car(Scm_cdr(e))
#define Scm_cdar(e)       Scm_cdr(Scm_car(e))
#define Scm_cddr(e)       Scm_cdr(Scm_cdr(e))
#define Scm_caadr(e)      Scm_car(Scm_cadr(e))
#define Scm_caddr(e)      Scm_car(Scm_cddr(e))
#define Scm_cdadr(e)      Scm_cdr(Scm_cadr(e))
#define Scm_cdddr(e)      Scm_cdr(Scm_cddr(e))
#define Scm_cadddr(e)     Scm_car(Scm_cdddr(e))
#define Scm_set_car(e, v) Scm_Pair_set_head(e, v)
#define Scm_set_cdr(e, v) Scm_Pair_set_tail(e, v)

void Scm_init(size_t heap_size);
enum ScmType Scm_type(ScmVal exp);

ScmVal Scm_Nil_new();
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
ScmVal Scm_Vector_new(size_t size);
void Scm_Vector_set(ScmVal vector, size_t idx, ScmVal value);
ScmVal Scm_Vector_ref(ScmVal vector, size_t idx);
void Scm_set_input_port(ScmVal iport);
void Scm_set_output_port(ScmVal oport);
ScmVal Scm_Env_new(ScmVal formals, ScmVal args, ScmVal parent);
void Scm_define(ScmVal env, char*, ScmVal value);
ScmVal Scm_lookup_symbol(ScmVal env, ScmVal symbol);
ScmVal Scm_is_eof_obj(ScmVal port);
ScmVal Scm_Closure_new(ScmVal formals, ScmVal body, ScmVal env);
ScmVal Scm_Procedure_new(ScmVal(*fptr)(ScmVal), int arity);
ScmVal Scm_parse(FILE* istream);
void Scm_print(FILE* ostream, ScmVal exp);
ScmVal Scm_eval(ScmVal exp, ScmVal env);
ScmVal Scm_top_level_env();
void Scm_gc();
void Scm_print_mem_stats();
#ifdef __cplusplus
  }
#endif

#endif