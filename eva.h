/**
 * EvaScheme
 */

#ifndef EVASCHEME_H
#define EVASCHEME_H

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
  EOF_OBJ,
  PORT,
  INVALID
};

struct ScmVal {
  enum ScmType type;
};

struct Pair {
  enum ScmType   type;
  struct ScmVal* head;
  struct ScmVal* tail;
};

struct Closure {
  enum ScmType   type;
  struct ScmVal* formals;
  struct ScmVal* body;
  struct ScmVal* env;
};

struct Procedure {
  enum ScmType   type;
  struct ScmVal* (*fptr)(struct ScmVal*);
};

struct String {
  enum ScmType type;
  char         value[];
};

struct Port {
  enum ScmType type;
  FILE*        stream;
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
struct ScmVal* Scm_Character_new(int c);
struct ScmVal* Scm_Port_new(FILE* stream);
struct ScmVal* Scm_Port_read_char(struct ScmVal* port);
struct ScmVal* Scm_Port_peek_char(struct ScmVal* port);
struct ScmVal* Scm_Port_write_char(struct ScmVal* port, struct ScmVal* c);
struct ScmVal* Scm_Port_write(struct ScmVal* port, struct ScmVal* obj);
struct ScmVal* Scm_Port_read(struct ScmVal* port);
struct ScmVal* Scm_is_eof_obj(struct ScmVal* port);
struct ScmVal* Scm_Closure_new(struct ScmVal* formals, struct ScmVal* body, struct ScmVal* env);
struct ScmVal* Scm_Procedure_new(struct ScmVal* (*fptr)(struct ScmVal*));
struct ScmVal* Scm_parse(FILE* istream);
void Scm_print(FILE* ostream, struct ScmVal* exp);
struct ScmVal* Scm_eval(struct ScmVal* exp, struct ScmVal* env);
struct ScmVal* Scm_Env_new(struct ScmVal* formals, struct ScmVal* args, struct ScmVal* parent);
struct ScmVal* Scm_Env_define_symbol(struct ScmVal* env, struct ScmVal* symbol, struct ScmVal* value);
struct ScmVal* Scm_Env_lookup_symbol(struct ScmVal* env, struct ScmVal* symbol);
int socket_connect(char* host, char* port);
int socket_listen(char* port);
int socket_accept(int fd);

#ifdef __cplusplus
  }
#endif

#endif