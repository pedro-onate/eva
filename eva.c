#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "eva.h"

struct ScmObj {
  enum ScmType type;
  ScmVal       pfwd;
};

struct ScmPair {
  enum ScmType type;
  ScmVal pfwd;
  ScmVal head;
  ScmVal tail;
};

struct ScmClosure {
  enum ScmType type;
  ScmVal       pfwd;
  ScmVal       formals;
  ScmVal       body;
  ScmVal       env;
};

struct ScmProcedure {
  enum ScmType type;
  ScmVal       pfwd;
  ScmVal       (*fptr)(ScmVal);
};

struct String {
  enum ScmType type;
  ScmVal       pfwd;
  char         value[];
};

struct Port {
  enum ScmType type;
  ScmVal       pfwd;
  FILE*        stream;
};

enum Tag {
  kObjectTag    = 0x0, /* 0b000 */
  kPairTag      = 0x1, /* 0b001 */
  kImmediateTag = 0x2, /* 0b010 */
  kBooleanTag   = 0x3, /* 0b011 */
  kIntegerTag   = 0x4, /* 0b100 */
  kSymbolTag    = 0x5, /* 0b101 */
  kCharacterTag = 0x6  /* 0b110 */
};

struct SymbolTable {char* symbols[65536]; int id;};
struct Heap {char* buffer; size_t size; char* ptr; char* from; char* to;};

#define SCM_TAG_BITS        3
#define SCM_TAG_MASK        0x7
#define SCM_PTR_BITS(ptr)   (intptr_t)(ptr)
#define SCM_GET_TAG(ptr)    (SCM_PTR_BITS(ptr) & SCM_TAG_MASK)
#define SCM_TAGGED(v, t)    (ScmVal)(SCM_PTR_BITS(v) << SCM_TAG_BITS | t)
#define SCM_UNTAG(t, v)     ((t)(SCM_PTR_BITS(v) >> SCM_TAG_BITS))

#define SCM_VAL2PTR(t, val)  ((t)(SCM_PTR_BITS(val) & ~SCM_TAG_MASK))
#define SCM_VAL2PAIR(val)    SCM_VAL2PTR(struct ScmPair*, val)
#define SCM_VAL2OBJ(val)     SCM_VAL2PTR(struct ScmObj*, val)
#define SCM_VAL2CLOSURE(val) SCM_VAL2PTR(struct ScmClosure*, val)
#define SCM_TAG_PTR(ptr, t)  ((ScmVal)(SCM_PTR_BITS(ptr) | t))
#define SCM_PAIR2VAL(pair)   SCM_TAG_PTR(pair, kPairTag)
#define SCM_OBJ2VAL(obj)     SCM_TAG_PTR(obj, kObjectTag)


static ScmVal env;
static struct Heap heap;
static struct SymbolTable symtab;
static ScmVal DEFINE, LAMBDA, IF, BEGIN, QUOTE;
ScmVal        SCM_NIL, SCM_FALSE, SCM_TRUE, SCM_UNBOUND, SCM_UNSPECIFIED, SCM_EOF;

void* (*alloc)(size_t);

static FILE* istream;
static FILE* ostream;

static ScmVal iport;
static ScmVal oport;

enum ScmType Scm_type(ScmVal value) {
  switch(SCM_GET_TAG(value)) {
    case kObjectTag:    return SCM_VAL2OBJ(value)->type;
    case kPairTag:      return PAIR;
    case kImmediateTag: return SCM_UNTAG(enum ScmType, value);
    case kBooleanTag:   return BOOLEAN;
    case kIntegerTag:   return INTEGER;
    case kSymbolTag:    return SYMBOL;
    case kCharacterTag: return CHARACTER;
    default:            return INVALID;
  }
}

ScmVal Scm_Integer_new(long value) {
  return SCM_TAGGED(value, kIntegerTag);
}

ScmVal Scm_Boolean_new(int value) {
  return SCM_TAGGED(value, kBooleanTag);
}

ScmVal Scm_Character_new(int value) {
  return SCM_TAGGED(value, kCharacterTag);
}

ScmVal Scm_Symbol_new(char* value) {
  int i;
  for(i = 0; i < symtab.id; i++) {
    if (strcmp(symtab.symbols[i], value) == 0) {
      return SCM_TAGGED(i, kSymbolTag);
    }
  }
  symtab.symbols[symtab.id] = malloc(strlen(value) + 1);
  strcpy(symtab.symbols[symtab.id], value);
  return SCM_TAGGED(symtab.id++, kSymbolTag);
}

ScmVal Scm_String_new(char* value) {
  struct String* string = alloc(sizeof(struct String) + strlen(value) + 1);
  string->type = STRING;
  string->pfwd = NULL;
  strcpy(string->value, value);
  return SCM_OBJ2VAL(string);
}

ScmVal Scm_Pair_new(ScmVal head, ScmVal tail) {
  struct ScmPair* pair = alloc(sizeof(struct ScmPair));
  pair->type = PAIR;
  pair->pfwd = NULL;
  pair->head = head;
  pair->tail = tail;
  return SCM_PAIR2VAL(pair);
}

ScmVal Scm_Pair_car(ScmVal pair) {
  return SCM_VAL2PAIR(pair)->head;
}

ScmVal Scm_Pair_cdr(ScmVal pair) {
  return SCM_VAL2PAIR(pair)->tail;
}

void Scm_Pair_set_head(ScmVal pair, ScmVal value) {
  SCM_VAL2PAIR(pair)->head = value;
}

void Scm_Pair_set_tail(ScmVal pair, ScmVal value) {
  SCM_VAL2PAIR(pair)->tail = value;
}

ScmVal Scm_Procedure_new(ScmVal (*fptr)(ScmVal)) {
  struct ScmProcedure* proc = alloc(sizeof(struct ScmProcedure));
  proc->type  = PROCEDURE;
  proc->pfwd  = NULL;
  proc->fptr  = fptr;
  return SCM_OBJ2VAL(proc);
}

ScmVal Scm_Closure_new(ScmVal formals, ScmVal body, ScmVal env) {
  struct ScmClosure* closure = alloc(sizeof(struct ScmClosure));
  closure->type    = CLOSURE;
  closure->pfwd    = NULL;
  closure->formals = formals;
  closure->body    = body;
  closure->env     = env;
  return SCM_OBJ2VAL(closure);
}

static int peekc(FILE* stream) {
  return ungetc(getc(stream), stream);
}

ScmVal Scm_Port_new(FILE* stream) {
  struct Port* port = alloc(sizeof(struct Port));
  port->type   = PORT;
  port->pfwd   = NULL;
  port->stream = stream;
  return SCM_OBJ2VAL(port);
}

ScmVal Scm_Port_read(ScmVal value) {
  struct Port* port = (struct Port*)value;
  return Scm_parse(port->stream);
}

ScmVal Scm_Port_read_char(ScmVal iport) {
  struct Port* port;
  int          c;
  port = (struct Port*)iport;
  c    = getc(port->stream);
  return c == EOF ? SCM_EOF : Scm_Character_new(c);
}

ScmVal Scm_Port_peek_char(ScmVal iport) {
  struct Port* port = (struct Port*)iport;
  int c = peekc(port->stream);
  return c == EOF ? SCM_EOF : Scm_Character_new(c);
}

ScmVal Scm_Port_write_char(ScmVal oport, ScmVal c) {
  struct Port* port = (struct Port*)oport;
  fputc(SCM_UNTAG(int, c), port->stream);
  return SCM_UNSPECIFIED;
}

ScmVal Scm_Port_write(ScmVal oport, ScmVal obj) {
  struct Port* port = (struct Port*)oport;
  Scm_print(port->stream, obj);
  return SCM_UNSPECIFIED;
}

ScmVal Scm_is_eof_obj(ScmVal obj) {
  return Scm_Boolean_new(obj == SCM_EOF);
}

static ScmVal bind_args(ScmVal formals, ScmVal args) {
  if (formals == SCM_NIL || args == SCM_NIL) {
    return SCM_NIL;
  } else if (Scm_type(formals) == PAIR) {
    return cons(cons(car(formals), car(args)), 
      bind_args(cdr(formals), cdr(args)));
  } else {
    return cons(cons(formals, args), SCM_NIL);
  }
}

ScmVal Scm_Env_new(ScmVal formals, ScmVal args, ScmVal parent){
  ScmVal bindings;
  if (Scm_type(formals) == SYMBOL) {
    bindings = cons(cons(formals, args), SCM_NIL);  
  } else {
    bindings = bind_args(formals, args);
  }
  return cons(bindings, parent);
}

ScmVal assq(ScmVal lst, ScmVal key){
  if (lst == SCM_NIL) { 
    return SCM_NIL;
  } else if (caar(lst) == key) {
    return car(lst);
  } else {
    return assq(cdr(lst), key);
  }
}

static ScmVal lookup_binding(ScmVal env, ScmVal symbol) {
  ScmVal binding;
  binding  = assq(car(env), symbol);
  if (binding != SCM_NIL) {
    return binding;
  } else if (cdr(env) != SCM_NIL) {
    return lookup_binding(cdr(env), symbol);
  } else {
    return SCM_UNBOUND;
  }
}

ScmVal Scm_define_symbol(ScmVal env, ScmVal symbol, ScmVal value) {
  ScmVal binding;
  binding = lookup_binding(env, symbol);
  if (binding != SCM_UNBOUND) {
    set_cdr(binding, value);
  } else {
    set_car(env, cons(cons(symbol, value), car(env)));
  }
  return symbol;
}

ScmVal Scm_lookup_symbol(ScmVal env, ScmVal symbol) {
  ScmVal binding = lookup_binding(env, symbol);
  return binding != SCM_UNBOUND ? cdr(binding) : SCM_UNBOUND;
}

static ScmVal parse_atom(FILE* stream) {
  char  buf[1028] = "\0";
  char  *pbuf     = buf;
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == EOF) {
    return SCM_EOF;
  } else if (peekc(stream) == '#') {
    *pbuf++ = getc(stream);
    if (strchr("tf", peekc(stream))) {
      return Scm_Boolean_new(getc(stream) == 't');
    } else if (peekc(stream) == '\\') {
      getc(stream);
      while(!strchr(" \t\n\r)", peekc(stream))) *pbuf++ = getc(stream);
      *pbuf = '\0';
      if (strcmp("space", buf+1) == 0) {
        return Scm_Character_new(' ');
      } else if (strcmp("newline", buf+1) == 0) {
        return Scm_Character_new('\n');
      } else {
        return Scm_Character_new(buf[1]);
      }
    } else {
      goto SYMBOL;
    }
  } else if(peekc(stream) == '"') {
    getc(stream);
    while(peekc(stream) != '"'){
      if (peekc(stream) == '\\') {
        getc(stream);
        int c = getc(stream);
        switch(c) {
          case 'n': *pbuf++ = '\n';continue;
          case 'r': *pbuf++ = '\r';continue;
          case 't': *pbuf++ = '\t';continue;
          default:  *pbuf++ = c;continue;
        }
      }
      *pbuf++ = getc(stream);
    }
    getc(stream);
    *pbuf = '\0';
    return Scm_String_new(buf);
  }else if (isdigit(peekc(stream))) {
    while(isdigit(peekc(stream))) *pbuf++ = getc(stream);
    *pbuf = '\0';
    return Scm_Integer_new(atoi(buf));
  } else {
    SYMBOL: while(!strchr(" \t\n\r)", peekc(stream))) *pbuf++ = getc(stream);
    *pbuf = '\0';
    return Scm_Symbol_new(buf);
  };
}

static ScmVal parse_list(FILE* stream) {
  ScmVal head, tail;
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == ')' && getc(stream)) {
    return SCM_NIL;
  } else if (peekc(stream) == '.' && getc(stream)) {
    tail = parse_atom(stream);
    parse_list(stream);
    return tail;
  } else {
    SYMBOL:;
    head = Scm_parse(stream);
    return cons(head, parse_list(stream));
  }  
}

ScmVal Scm_parse(FILE* stream) {
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == ';') while(getc(stream) != '\n'){}
  if (peekc(stream) == '(' && getc(stream)) {
    return parse_list(stream);
  } else if (peekc(stream) == '\'' && getc(stream)) {
    return cons(Scm_Symbol_new("quote"), cons(Scm_parse(stream), SCM_NIL));
  } else {
    return parse_atom(stream);
  }
}

static void print_list(FILE* ostream, ScmVal exp) {
  ScmVal tail;
  Scm_print(ostream, car(exp));
  tail = cdr(exp);
  if (tail != SCM_NIL) {
    if (Scm_type(tail) == PAIR) {
      fprintf(ostream, " "); print_list(ostream, tail);
    } else {
      fprintf(ostream, " . "); Scm_print(ostream, tail);
    }
  }
}

void Scm_print(FILE* ostream, ScmVal exp) {
  switch(Scm_type(exp)) {
    case NIL:         fprintf(ostream, "()");                                                 break;
    case INTEGER:     fprintf(ostream, "%ld", SCM_UNTAG(long, exp));                          break;
    case BOOLEAN:     fprintf(ostream, "#%c", exp == SCM_TRUE ? 't' : 'f');                   break;
    case SYMBOL:      fprintf(ostream, "%s", symtab.symbols[SCM_UNTAG(int, exp)]);            break;
    case CLOSURE:     fprintf(ostream, "#<closure>");                                         break;
    case PROCEDURE:   fprintf(ostream, "#<procedure>");                                       break;
    case STRING:      fprintf(ostream, "%s", ((struct String*)exp)->value);                   break;
    case CHARACTER:   fprintf(ostream, "#\\%c", SCM_UNTAG(char, exp));                        break;
    case UNSPECIFIED:                                                                         break;
    case UNBOUND:     fprintf(ostream, "#<unbound>");                                         break;
    case EOF_OBJ:     fprintf(ostream, "#<eof>");                                             break;
    case PAIR:        fprintf(ostream, "("); print_list(ostream, exp); fprintf(ostream, ")"); break;
    default:          fprintf(ostream, "undefined type");                                     break;
  }
}

ScmVal Scm_top_level_env() {
  return env;
}

static ScmVal eval_args(ScmVal args, ScmVal env) {
  if (Scm_type(args) == NIL) {
    return SCM_NIL;
  } else {
    return cons(Scm_eval(car(args), env), eval_args(cdr(args), env));
  }
}

ScmVal Scm_eval(ScmVal exp, ScmVal env) {
  enum ScmType         type;
  ScmVal               op, args;
  struct ScmProcedure* cfunc;
  struct ScmClosure*   closure;

  EVAL:;
  type = Scm_type(exp);
  if (type == PAIR){
    op = car(exp);
    if (op == DEFINE) {
      if (Scm_type(cadr(exp)) == PAIR) {
        return Scm_define_symbol(env, caadr(exp), Scm_Closure_new(cdadr(exp), cddr(exp), env));
      } else {
        return Scm_define_symbol(env, cadr(exp), Scm_eval(caddr(exp), env));
      }
    } else if (op == IF) {
      if (Scm_eval(cadr(exp), env) != SCM_FALSE) {
        exp = caddr(exp);
        goto EVAL;
      } else if (cdddr(exp) != SCM_NIL){
        exp = cadddr(exp);
        goto EVAL;
      } else {
        return SCM_UNSPECIFIED;
      }
    } else if (op == QUOTE) {
      return cadr(exp);
    } else if (op == LAMBDA) {
      return Scm_Closure_new(cadr(exp), cddr(exp), env);
    } else if (op == BEGIN) {
      exp = cdr(exp);
      EVAL_SEQ:;
      while(cdr(exp) != SCM_NIL) {
        Scm_eval(car(exp), env);
        exp = cdr(exp);
      }
      exp = car(exp);
      goto EVAL;
    } else {
      op = Scm_eval(op, env);
      type = Scm_type(op);
      if (type == PROCEDURE) {
        cfunc = (struct ScmProcedure*)op;
        args  = eval_args(cdr(exp), env);
        return cfunc->fptr(args);
      } else if (type == CLOSURE) {
        closure = (struct ScmClosure*)op;
        args = eval_args(cdr(exp), env);
        env  = Scm_Env_new(closure->formals, args, closure->env);
        exp  = closure->body;
        goto EVAL_SEQ;
      }
    }
    return SCM_UNSPECIFIED;
  } else if(type == SYMBOL) {
    return Scm_lookup_symbol(env, exp);
  } else {
    return exp;
  }
}

static ScmVal procedure_add(ScmVal args) {
  long a = SCM_UNTAG(long, car(args));
  long b = SCM_UNTAG(long, cadr(args));
  return Scm_Integer_new(a + b);
}

static ScmVal procedure_sub(ScmVal args) {
  long a = SCM_UNTAG(long, car(args));
  long b = SCM_UNTAG(long, cadr(args));
  return Scm_Integer_new(a - b);
}

static ScmVal procedure_mult(ScmVal args) {
  long a = SCM_UNTAG(long, car(args));
  long b = SCM_UNTAG(long, cadr(args));
  return Scm_Integer_new(a * b);
}

static ScmVal procedure_eq(ScmVal args) {
  return Scm_Boolean_new(car(args) == cadr(args)); 
}

static ScmVal procedure_car(ScmVal args) {
  return car(car(args));
}

static ScmVal procedure_cdr(ScmVal args) {
  return cdr(car(args));
}

static ScmVal procedure_eval(ScmVal args) {
  return Scm_eval(car(args), cadr(args));
}

static ScmVal procedure_read(ScmVal args) {
  return Scm_Port_read(args == SCM_NIL ? iport : car(args));
}

static ScmVal procedure_read_char(ScmVal args) {
  return Scm_Port_read_char(args == SCM_NIL ? iport : car(args));
}

static ScmVal procedure_peek_char(ScmVal args) {
  return Scm_Port_peek_char(args == SCM_NIL ? iport : car(args));
}

static ScmVal procedure_write(ScmVal args) {
  return Scm_Port_write(cdr(args) == SCM_NIL ? oport : cadr(args), car(args));
}

static ScmVal procedure_write_char(ScmVal args) {
  return Scm_Port_write_char(cdr(args) == SCM_NIL ? oport : cadr(args), car(args));
}

static ScmVal procedure_quit(ScmVal args) {
  exit(0);
  return SCM_EOF;
}

int socket_listen(char* port);
int socket_accept(int sockfd);
int socket_connect(char* host, char* port);

static ScmVal procedure_socket_listen(ScmVal args) {
  struct String* port = (struct String*)car(args);
  return Scm_Integer_new(socket_listen(port->value));
}

static ScmVal procedure_socket_accept(ScmVal args) {
  return Scm_Port_new(fdopen(socket_accept(SCM_UNTAG(int, car(args))), "r+"));
}

static ScmVal procedure_socket_connect(ScmVal args) {
  struct String* host = (struct String*)car(args);
  struct String* port = (struct String*)cadr(args);
  int sock_fd = socket_connect(host->value, port->value);
  return Scm_Port_new(fdopen(sock_fd, "r+"));
}

static ScmVal procedure_is_eof_obj(ScmVal args) {
  return Scm_is_eof_obj(car(args));
}

static ScmVal procedure_close(ScmVal args) {
  struct Port* port = (struct Port*)car(args);
  fclose(port->stream);
  port->stream = NULL;
  return SCM_UNSPECIFIED;
}

static ScmVal procedure_not(ScmVal args) {
  return Scm_Boolean_new(car(args) == SCM_FALSE);
}

static ScmVal procedure_time_ms(ScmVal args) {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return Scm_Integer_new(tp.tv_sec * 1000 + tp.tv_usec / 10000);
}

ScmVal Scm_load(char* filename) {
  FILE* istream = fopen(filename, "r");
  if (istream) {
    ScmVal exp = Scm_parse(istream);
    while(exp != SCM_EOF) {
      Scm_eval(exp, env);
      exp = Scm_parse(istream);
    }
    fclose(istream);
  }
  return SCM_UNSPECIFIED;
}

static ScmVal procedure_load(ScmVal args) {
  struct String* string = (struct String*)SCM_VAL2OBJ(car(args));
  return Scm_load(string->value);
}

static ScmVal procedure_interaction_env(ScmVal args) {
  return Scm_top_level_env();
}

void Scm_define(ScmVal env, char* symbol, ScmVal val) {
  Scm_define_symbol(env, Scm_Symbol_new(symbol), val);
}

//#define ALIGN_N(p, n)     (p) % (n) == 0 ? (p) : (p) + ((n) - (p) % (n))
#define ALIGN_N(p, n)     ((((uintptr_t)p) + (n - 1)) & -n)
#define DEFAULT_ALIGNMENT 8

static void* bump_allocator(size_t size) {
  uintptr_t block;
  block    = ALIGN_N(heap.ptr, 8);
  heap.ptr = (char*)(block + size);

  if (heap.ptr >= (char*)heap.buffer + heap.size) {
    fprintf(stderr, "OUT OF MEMORY\n");
    exit(1);
  }
  return (void*)block;
}

int is_heap_alloc(ScmVal val) {
  return SCM_GET_TAG(val) < kImmediateTag;
}

int is_to(ScmVal val) {
  char* address = SCM_VAL2PTR(char*, val);
  return address >= heap.to && address < heap.to + heap.size / 2;
}

int is_forwarded(ScmVal val) {
  switch(Scm_type(val)) {
    case PAIR:
    case PROCEDURE:
    case CLOSURE:
    case STRING:
    case PORT:
      return SCM_VAL2OBJ(val)->pfwd != NULL;
    default:
      return 0;
  }
}

void mark_copy(ScmVal* pval, char** next) {
  struct ScmObj* obj_from;
  size_t         size;
  enum Tag       tag;

  // Return if this reference does not point to a value on the heap
  if (!is_heap_alloc(*pval)) {
    return;
  }

  obj_from = SCM_VAL2OBJ(*pval);

  // Return if this reference already points to an object in to space
  if (is_to(*pval)) {
    return;
  }

  // This reference points to a object in from space that has been moved
  if (is_forwarded(*pval)) {
    *pval = obj_from->pfwd;
    return;
  }

  // This reference points to an unmoved object in from space
  switch(Scm_type(*pval)) {
    case PAIR:
    size = sizeof(struct ScmPair);
    break;

    case PROCEDURE:
    size = sizeof(struct ScmProcedure);
    break;

    case CLOSURE:
    size = sizeof(struct ScmClosure);
    break;

    case STRING:
    size = sizeof(struct String);
    break;

    case PORT:
    size = sizeof(struct Port);
    break;

    default:
      // NOOP
      printf("should not happen\n");
      exit(1);
    break;
  }


  tag = SCM_GET_TAG(*pval);
  memcpy(*next, obj_from, size);
  obj_from->pfwd = *pval;
  *pval = SCM_TAG_PTR(*next, tag);
  *next += size;
  //printf("next: %p\n", *next);
}

void Scm_gc() {
  /* TODO: Ensure scan and next pointers respect alignment */
  char* scan, *next, *tmp;

  scan = next = heap.to;

  mark_copy(&env, &next);
  mark_copy(&iport, &next);
  mark_copy(&oport, &next);

  while(scan < next) {
    //printf("scanning\n");
    switch(Scm_type(scan)) {
      case PAIR:
        mark_copy(&SCM_VAL2PAIR(scan)->head, &next);
        mark_copy(&SCM_VAL2PAIR(scan)->tail, &next);
        scan += sizeof(struct ScmPair);
        break;
      case CLOSURE:
        mark_copy(&SCM_VAL2CLOSURE(scan)->formals, &next);
        mark_copy(&SCM_VAL2CLOSURE(scan)->body, &next);
        mark_copy(&SCM_VAL2CLOSURE(scan)->env, &next);
        scan += sizeof(struct ScmClosure);
        break;
      case STRING:
        scan += sizeof(struct String);
        break;
      case PROCEDURE:
        scan += sizeof(struct ScmProcedure);
        break;
      case PORT:
        scan += sizeof(struct Port);
        break;
      default:
        printf("should never happen\n");
        exit(1);
    }
  }

  tmp = heap.from;
  heap.from = heap.to;
  heap.to = tmp;
  heap.ptr = next;
}

void Scm_init(size_t heap_size) {
  heap.size       = heap_size;
  heap.buffer     = malloc(heap_size);
  heap.ptr        = heap.buffer;
  heap.from       = heap.ptr;
  heap.to         = heap.from + heap_size / 2;

  alloc           = bump_allocator;
  DEFINE          = Scm_Symbol_new("define");
  LAMBDA          = Scm_Symbol_new("lambda");
  IF              = Scm_Symbol_new("if");
  BEGIN           = Scm_Symbol_new("begin");
  QUOTE           = Scm_Symbol_new("quote");
  SCM_NIL         = SCM_TAGGED(NIL, kImmediateTag);
  SCM_FALSE       = Scm_Boolean_new(0);
  SCM_TRUE        = Scm_Boolean_new(1);
  SCM_UNBOUND     = SCM_TAGGED(UNBOUND, kImmediateTag);
  SCM_UNSPECIFIED = SCM_TAGGED(UNSPECIFIED, kImmediateTag);
  SCM_EOF         = SCM_TAGGED(EOF_OBJ, kImmediateTag);
  env             = Scm_Env_new(SCM_NIL, SCM_NIL, SCM_NIL);

  Scm_define(env, "eval", Scm_Procedure_new(procedure_eval));
  Scm_define(env, "read", Scm_Procedure_new(procedure_read));
  Scm_define(env, "read-char", Scm_Procedure_new(procedure_read_char));
  Scm_define(env, "peek-char", Scm_Procedure_new(procedure_peek_char));
  Scm_define(env, "eof-obj?", Scm_Procedure_new(procedure_is_eof_obj));
  Scm_define(env, "write", Scm_Procedure_new(procedure_write));
  Scm_define(env, "write-char", Scm_Procedure_new(procedure_write_char));
  Scm_define(env, "quit", Scm_Procedure_new(procedure_quit));
  Scm_define(env, "connect", Scm_Procedure_new(procedure_socket_connect));
  Scm_define(env, "listen", Scm_Procedure_new(procedure_socket_listen));
  Scm_define(env, "accept", Scm_Procedure_new(procedure_socket_accept));
  Scm_define(env, "close", Scm_Procedure_new(procedure_close));
  Scm_define(env, "not", Scm_Procedure_new(procedure_not));
  Scm_define(env, "time-ms", Scm_Procedure_new(procedure_time_ms));
  Scm_define(env, "load", Scm_Procedure_new(procedure_load));
  Scm_define(env, "interaction-environment", Scm_Procedure_new(procedure_interaction_env));
  Scm_define(env, "eval", Scm_Procedure_new(procedure_eval));
  Scm_define(env, "+", Scm_Procedure_new(procedure_add));
  Scm_define(env, "*", Scm_Procedure_new(procedure_mult));
  Scm_define(env, "car", Scm_Procedure_new(procedure_car));
  Scm_define(env, "cdr", Scm_Procedure_new(procedure_cdr));
  Scm_define(env, "-", Scm_Procedure_new(procedure_sub));
  Scm_define(env, "=", Scm_Procedure_new(procedure_eq));
}

#define BACKLOG 128     // how many pending connections queue will hold

/*
void sigchld_handler(int s) {
  while(waitpid(-1, NULL, WNOHANG) > 0);
}
*/

// get sockaddr, IPv4 or IPv6:
void* get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int socket_listen(char* port) {
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  struct sigaction sa;
  int yes=1;
  int rv;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // use my IP
  if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return -1;
  }
  for(p = servinfo; p != NULL; p = p->ai_next) {
      if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
          perror("server: socket");
          continue;
      }
      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
          perror("setsockopt");
          return -1;
      }
      if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
          close(sockfd);
          perror("server: bind");
          continue;
      }
      break;
  }
  if (p == NULL)  {
      fprintf(stderr, "server: failed to bind\n");
      return -1;
  }
  freeaddrinfo(servinfo);
  if (listen(sockfd, BACKLOG) == -1) {
      perror("listen");
      return -1;
  }
  /*sa.sa_handler = sigchld_handler; // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
      perror("sigaction");
      return -1;
  }*/  
  return sockfd;
}

int socket_accept(int sockfd) {
  int new_fd;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  char s[INET6_ADDRSTRLEN];
  sin_size = sizeof their_addr;
  new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
  if (new_fd == -1) {
    return -1;
  }
  inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
  return new_fd;
}

int socket_connect(char* host, char* port) {
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  char s[INET6_ADDRSTRLEN];
  memset(&hints, 0, sizeof hints);
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return -1;
  }
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("client: connect");
      continue;
    }
    break;
  }
  if (p == NULL) {
    fprintf(stderr, "client: failed to connect\n");
    return -1;
  }
  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
  freeaddrinfo(servinfo); // all done with this structure
  return sockfd;
}

//#define SOCKET
int main(int argc, char* argv[]) {
#ifdef SOCKET
  int listen_fd = socket_listen("1234");
  int client_fd = socket_accept(listen_fd);

  FILE* iostream = fdopen(client_fd, "r+");

  ostream = iostream;
  istream = iostream;
#else
  ostream = stdout;
  istream = stdin;
#endif

  Scm_init(2 << 27);

  iport = Scm_Port_new(istream);
  oport = Scm_Port_new(ostream);

  Scm_load("scm/init.scm");

  //printf("heap.from: %p, heap.to: %p, heap.ptr: %p, env: %p, iport: %p, oport: %p\n", heap.from, heap.to, heap.ptr, SCM_VAL2OBJ(env), SCM_VAL2OBJ(iport), SCM_VAL2OBJ(oport));

  while(1) {
    printf("eva> ");
    Scm_Port_write(oport, Scm_eval(Scm_Port_read(iport), env));
    printf("\n");
    Scm_gc();
    //printf("heap.from: %p, heap.to: %p, heap.ptr: %p, env: %p, iport: %p, oport: %p\n", heap.from, heap.to, heap.ptr, SCM_VAL2OBJ(env), SCM_VAL2OBJ(iport), SCM_VAL2OBJ(oport));
  }

  

  //Scm_load("scm/init.scm");
}