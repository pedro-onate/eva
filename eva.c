#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include "eva.h"

struct ScmObj {
  enum ScmType type;
};

struct ScmPair {
  ScmVal head;
  ScmVal tail;
};

struct ScmClosure {
  enum ScmType   type;
  ScmVal formals;
  ScmVal body;
  ScmVal env;
};

struct ScmProcedure {
  enum ScmType   type;
  ScmVal (*fptr)(ScmVal);
};

enum Tag {
  kObjectTag    = 0x0, /* 0b000 */
  kPairTag      = 0x1, /* 0b001 */
  kImmediateTag = 0x2, /* 0b010 */
  kBooleanTag   = 0x3, /* 0b011 */
  kIntegerTag   = 0x4, /* 0b100 */
  kSymbolTag    = 0x5, /* 0b101 */
  kCharacter    = 0x6  /* 0b110 */
};

struct SymbolTable {char* symbols[65536]; int id;};
struct Heap {char* buffer; int size; char* ptr;};

#define SCM_TAG_BITS        3
#define SCM_TAG_MASK        0x7
#define SCM_PTR_BITS(ptr)   (intptr_t)(ptr)
#define SCM_GET_TAG(ptr)    SCM_PTR_BITS(ptr) & SCM_TAG_MASK
#define SCM_TAGGED(v, t)    (ScmVal)(SCM_PTR_BITS(v) << SCM_TAG_BITS | t)
#define SCM_UNTAG(t, v)     ((t)(SCM_PTR_BITS(v) >> SCM_TAG_BITS))

#define SCM_PAIR2VAL(pair)  ((ScmVal)((uintptr_t)pair | kPairTag))
#define SCM_VAL2PAIR(val)   ((struct ScmPair*)(val & ~SCM_TAG_MASK))
#define SCM_OBJ2VAL(obj)    ((ScmVal)((uintptr_t)obj | kObjectTag))
#define SCM_VAL2OBJ(val)    ((struct ScmObj*)val)

static ScmVal env;
static struct Heap heap;
static struct SymbolTable symtab;
static ScmVal DEFINE, LAMBDA, IF, BEGIN, QUOTE;
ScmVal        SCM_NIL, SCM_FALSE, SCM_TRUE, SCM_UNBOUND, SCM_UNSPECIFIED, SCM_EOF;

void* (*alloc)(size_t);

static enum ScmType Scm_Obj_type(struct ScmObj* obj) {
  return INVALID;
}

enum ScmType Scm_type(ScmVal value) {
  switch(SCM_GET_TAG(value)) {
    case kObjectTag:    return SCM_VAL2OBJ(value)->type;
    case kPairTag:      return PAIR;
    case kImmediateTag: return SCM_UNTAG(enum ScmType, value);
    case kBooleanTag:   return BOOLEAN;
    case kIntegerTag:   return INTEGER;
    case kSymbolTag:    return SYMBOL;
    case kCharacter:    return CHARACTER;
    default:            return INVALID;
  }
}

ScmVal Scm_Integer_new(long value) {
  return SCM_TAGGED(value, kIntegerTag);
}

ScmVal Scm_Boolean_new(int value) {
  return SCM_TAGGED(value, kBooleanTag);
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

ScmVal Scm_Pair_new(ScmVal head, ScmVal tail) {
  struct ScmPair* pair = alloc(sizeof(struct ScmPair));
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
  proc->fptr  = fptr;
  return SCM_OBJ2VAL(proc);
}

ScmVal Scm_Closure_new(ScmVal formals, ScmVal body, ScmVal env) {
  struct ScmClosure* closure = alloc(sizeof(struct ScmClosure));
  closure->type    = CLOSURE;
  closure->formals = formals;
  closure->body    = body;
  closure->env     = env;
  return SCM_OBJ2VAL(closure);
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

static int peekc(FILE* stream) {
  int c;
  c = getc(stream);
  ungetc(c, stream); 
  return c;
}

static ScmVal parse_atom(FILE* stream) {
  char  buf[1028] = "\0";
  char  *pbuf     = buf;
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == '#') {
    *pbuf++ = getc(stream);
    if (strchr("tf", peekc(stream))) {
      return Scm_Boolean_new(getc(stream) == 't');
    } else {
      goto SYMBOL;
    }
  } else if (isdigit(peekc(stream))) {
    while(isdigit(peekc(stream))) *pbuf++ = getc(stream);
    *pbuf = '\0';
    return Scm_Integer_new(atoi(buf));
  } else {
    SYMBOL: while(!strchr(" \t\n)", peekc(stream))) *pbuf++ = getc(stream);
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
  if (!stream || peekc(stream) == EOF) {return SCM_EOF;}
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == '(' && getc(stream)) {
    return parse_list(stream);
  } else if (peekc(stream) == '\'' && getc(stream)) {
    return cons(Scm_Symbol_new("quote"), cons(Scm_parse(stream), SCM_NIL));
  } else {
    return parse_atom(stream);
  }
}

static void print_list(ScmVal exp) {
  ScmVal tail;
  Scm_print(car(exp));
  tail = cdr(exp);
  if (tail != SCM_NIL) {
    if (Scm_type(tail) == PAIR) {
      printf(" "); print_list(tail);
    } else {
      printf(" . "); Scm_print(tail);
    }
  }
}

void Scm_print(ScmVal exp) {
  if (!exp) {
    return;
  }
  switch(Scm_type(exp)) {
    case NIL:         printf("()");                                      break;
    case INTEGER:     printf("%ld", SCM_UNTAG(long, exp));               break;
    case BOOLEAN:     printf("#%c", exp == SCM_TRUE ? 't' : 'f');        break;
    case PAIR:        printf("("); print_list(exp); printf(")");         break;
    case SYMBOL:      printf("%s", symtab.symbols[SCM_UNTAG(int, exp)]); break;
    case CLOSURE:     printf("#<closure>");                              break;
    case PROCEDURE:   printf("#<procedure>");                            break;
    case UNSPECIFIED:                                                    break;
    case UNBOUND:     printf("#<unbound>");                              break;
    case EOFILE:      printf("#<eof>");                                  break;
    default:          printf("undefined type");                          break;
  }
}

static ScmVal eval_args(ScmVal args, ScmVal env) {
  if (Scm_type(args) == NIL) {
    return SCM_NIL;
  } else {
    return cons(Scm_eval(car(args), env), eval_args(cdr(args), env));
  }
}

ScmVal Scm_eval(ScmVal exp, ScmVal env) {
  int               type;
  ScmVal    op;
  ScmVal    res;
  struct ScmProcedure* cfunc;
  struct ScmClosure*   closure;

  EVAL:;
  type = Scm_type(exp);
  if (type == PAIR){
    op = car(exp);
    if (op == DEFINE) {
      return Scm_define_symbol(env, cadr(exp), Scm_eval(caddr(exp), env));
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
        return cfunc->fptr(eval_args(cdr(exp), env));
      } else if (type == CLOSURE) {
        closure = (struct ScmClosure*)op;
        env = Scm_Env_new(closure->formals, eval_args(cdr(exp), env), closure->env);
        exp = closure->body;
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
  return Scm_eval(car(args), env);
}

static ScmVal procedure_read(ScmVal args) {
  return Scm_parse(stdin);
}

static ScmVal procedure_quit(ScmVal args) {
  exit(0);
  return SCM_EOF;
}

static ScmVal procedure_time_ms(ScmVal args) {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return Scm_Integer_new(tp.tv_sec * 1000 + tp.tv_usec / 10000);
}


//#define ALIGN_N(p, n)     (p) % (n) == 0 ? (p) : (p) + ((n) - (p) % (n))
#define ALIGN_N(p, n)     ((((uintptr_t)p) + (n - 1)) & -n)
#define DEFAULT_ALIGNMENT 8

static void* bump_allocator(size_t size) {
  uintptr_t block;
  block    = ALIGN_N(heap.ptr, 8);
  heap.ptr = (char*)(block + size);

  //if (heap.ptr >= (char*)heap.buffer + heap.size) {
  //  fprintf(stderr, "OUT OF MEMORY\n");
  //  exit(1);
  //}
  return (void*)block;
}

void Scm_init(size_t heap_size) {
  heap.size       = heap_size;
  heap.buffer     = malloc(heap_size);
  heap.ptr        = heap.buffer;

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
  SCM_EOF         = SCM_TAGGED(EOFILE, kImmediateTag);
  env             = Scm_Env_new(SCM_NIL, SCM_NIL, SCM_NIL);

  Scm_define_symbol(env, Scm_Symbol_new("+"), Scm_Procedure_new(procedure_add));
  Scm_define_symbol(env, Scm_Symbol_new("-"), Scm_Procedure_new(procedure_sub));
  Scm_define_symbol(env, Scm_Symbol_new("*"), Scm_Procedure_new(procedure_mult));
  Scm_define_symbol(env, Scm_Symbol_new("="), Scm_Procedure_new(procedure_eq));
  Scm_define_symbol(env, Scm_Symbol_new("car"), Scm_Procedure_new(procedure_car));
  Scm_define_symbol(env, Scm_Symbol_new("cdr"), Scm_Procedure_new(procedure_cdr));
  Scm_define_symbol(env, Scm_Symbol_new("eval"), Scm_Procedure_new(procedure_eval));
  Scm_define_symbol(env, Scm_Symbol_new("read"), Scm_Procedure_new(procedure_read));
  Scm_define_symbol(env, Scm_Symbol_new("quit"), Scm_Procedure_new(procedure_quit));
  Scm_define_symbol(env, Scm_Symbol_new("time-ms"), Scm_Procedure_new(procedure_time_ms));
}

int main() {
  printf(".------------------.\n");
  printf("|  EvaScheme v0.1  |\n");
  printf("'------------------'\n\n");

  Scm_init(2 << 29);
  while (true) {
    printf("eva> ");
    Scm_print(Scm_eval(Scm_parse(stdin), env));
    printf("\n");
  }
}