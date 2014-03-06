#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "eva.h"

#define SCM_TAG_BITS         3
#define SCM_TAG_MASK         0x7
#define SCM_PTR_BITS(ptr)    (intptr_t)(ptr)
#define SCM_TAG(ptr)         (SCM_PTR_BITS(ptr) & SCM_TAG_MASK)
#define SCM_TAGGED(v, t)     (ScmVal)(SCM_PTR_BITS(v) << SCM_TAG_BITS | t)
#define SCM_UNTAG(t, v)      ((t)(SCM_PTR_BITS(v) >> SCM_TAG_BITS))
#define SCM_PTR(t, val)      ((t)(SCM_PTR_BITS(val) & ~SCM_TAG_MASK))
#define SCM_TAG_PTR(ptr, t)  ((ScmVal)(SCM_PTR_BITS(ptr) | t))
#define SCM_ALIGN(p, n)      (char*)((((uintptr_t)p) + (n - 1)) & -n)

enum ScmTag {
  kObjectTag    = 0x0, /* 0b000 */
  kPairTag      = 0x1, /* 0b001 */
  kImmediateTag = 0x2, /* 0b010 */
  kBooleanTag   = 0x3, /* 0b011 */
  kIntegerTag   = 0x4, /* 0b100 */
  kSymbolTag    = 0x5, /* 0b101 */
  kCharacterTag = 0x6  /* 0b110 */
};

enum { kDefaultAlignment = 16 };
enum { kDefaultHeapSize = 512 * 1000000 };

struct ScmObj {
  enum ScmType type;
  ScmVal       pfwd;
};

#define SCM_OBJ(val) SCM_PTR(struct ScmObj*, val)

struct ScmPair {
  enum ScmType type;
  ScmVal       pfwd;
  ScmVal       head;
  ScmVal       tail;
};

#define SCM_PAIR(val) SCM_PTR(struct ScmPair*, val)

struct ScmClosure {
  enum ScmType type;
  ScmVal       pfwd;
  ScmVal       formals;
  ScmVal       body;
  ScmVal       env;
};

#define SCM_CLOSURE(val) SCM_PTR(struct ScmClosure*, val)
 
struct ScmProcedure {
  enum ScmType type;
  ScmVal       pfwd;
  ScmVal       (*fptr)(ScmVal);
};

#define SCM_PROC(val) SCM_PTR(struct ScmProcedure*, val)

struct ScmString {
  enum ScmType type;
  ScmVal       pfwd;
  size_t       length;
  char         value[];
};

#define SCM_STRING(val) SCM_PTR(struct ScmString*, val)

struct ScmPort {
  enum ScmType type;
  ScmVal       pfwd;
  FILE*        stream;
};

#define SCM_PORT(val) SCM_PTR(struct ScmPort*, val)

struct ScmTypeInfo {
  enum ScmType type;
  size_t       size;
  char*        name;
  enum ScmTag  tag;
};

static struct ScmTypeInfo type_info[] = {
  {NIL,         sizeof(ScmVal),              "nil",         kImmediateTag}, //NIL,
  {BOOLEAN,     sizeof(ScmVal),              "boolean",     kBooleanTag}, //BOOLEAN,
  {INTEGER,     sizeof(ScmVal),              "integer",     kIntegerTag}, //INTEGER,
  {SYMBOL,      sizeof(ScmVal),              "symbol",      kSymbolTag},  //SYMBOL,
  {CHARACTER,   sizeof(ScmVal),              "character",   kCharacterTag}, //CHARACTER,
  {STRING,      sizeof(struct ScmString),    "string",      kObjectTag}, //STRING,
  {PAIR,        sizeof(struct ScmPair),      "pair",        kPairTag}, //PAIR,
  {CLOSURE,     sizeof(struct ScmClosure),   "closure",     kObjectTag}, //CLOSURE,
  {PROCEDURE,   sizeof(struct ScmProcedure), "procedure",   kObjectTag}, //PROCEDURE,
  {UNBOUND,     sizeof(ScmVal),              "unbound",     kImmediateTag}, //UNBOUND,
  {UNSPECIFIED, sizeof(ScmVal),              "unspecified", kImmediateTag}, //UNSPECIFIED,
  {EOF_OBJ,     sizeof(ScmVal),              "eof_obj",     kImmediateTag}, //EOF_OBJ,
  {PORT,        sizeof(struct ScmPort),      "port",        kObjectTag}, //PORT,
  {INVALID,     0,                           "invalid",     -1} //PORT,
};

enum ScmType Scm_type(ScmVal value) {
  switch(SCM_TAG(value)) {
    case kObjectTag:    return SCM_OBJ(value)->type;
    case kPairTag:      return PAIR;
    case kImmediateTag: return SCM_UNTAG(enum ScmType, value);
    case kBooleanTag:   return BOOLEAN;
    case kIntegerTag:   return INTEGER;
    case kSymbolTag:    return SYMBOL;
    case kCharacterTag: return CHARACTER;
    default:            return INVALID;
  }
}

size_t Scm_size_of(ScmVal val) {
  enum ScmType type = Scm_type(val);
  if (type == STRING) {
    return sizeof(struct ScmString) + ((struct ScmString*)val)->length + 1;
  } else {
    return type_info[type].size;
  }
}

struct Heap {
  void*  (*alloc)(size_t);
  char*  buffer;
  size_t size;
  char*  next;
  char*  from;
  char*  to;
  size_t requested;
};

struct Stack {
  ScmVal* roots[65536];
  int     top;
};

#define gc_pop(n)            ctx.stack.top -= n
#define gc_push(v)           ctx.stack.roots[ctx.stack.top++] = &v
#define gc_push2(a,b)        gc_push(a); gc_push(b)
#define gc_push3(a,b,c)      gc_push(a); gc_push2(b,c)
#define gc_push4(a,b,c,d)    gc_push(a); gc_push3(b,c,d)

struct SymbolTable {
  char* symbols[65536];
  int   id;
};

struct ScmContext {
  struct Heap        heap;
  struct Stack       stack;
  struct SymbolTable symtab;
  ScmVal             iport;
  ScmVal             oport;
  ScmVal             env;
};

ScmVal SCM_NIL, SCM_FALSE, SCM_TRUE, SCM_UNBOUND, SCM_UNSPECIFIED, SCM_EOF;
static struct ScmContext ctx;

static void* bump_allocator(size_t size) {
  void* mem;
  retry:
  ctx.heap.next = SCM_ALIGN(ctx.heap.next, kDefaultAlignment);
  if (ctx.heap.next + size > (ctx.heap.from + ctx.heap.size)) {
    Scm_gc();
    goto retry;
  }
  mem = ctx.heap.next;
  ctx.heap.next += size;
  return mem;
}

int Scm_Heap_contains(ScmVal val) {
  return SCM_TAG(val) < kImmediateTag;
}

int Scm_Heap_from_contains(ScmVal val) {
  char* address = SCM_PTR(char*, val);
  return address >= ctx.heap.from && address < ctx.heap.from + ctx.heap.size;
}

int Scm_Heap_to_contains(ScmVal val) {
  char* address = SCM_PTR(char*, val);
  return address >= ctx.heap.to && address < ctx.heap.to + ctx.heap.size;
}

int is_forwarded(ScmVal val) {
  switch(Scm_type(val)) {
    case PAIR:
    case PROCEDURE:
    case CLOSURE:
    case STRING:
    case PORT:
    return SCM_OBJ(val)->pfwd != NULL;
    default:
    return 0;
  }
}

void mark_copy(ScmVal* pval, char** next) {
  enum ScmTag  tag;
  size_t       size;
  
  if(*pval == NULL) {
    printf("found null ref, aborting\n"); exit(1); return;
  }
  
  if (Scm_Heap_contains(*pval) && !Scm_Heap_to_contains(*pval)) {
    if (is_forwarded(*pval)) {
      *pval = SCM_OBJ(*pval)->pfwd; // This reference points to a object in from space that has been moved, so update reference
    } else {
      tag   = SCM_TAG(*pval);                                    // Save Tag
      size  = Scm_size_of(*pval);                                // Get size of object
      *next = SCM_ALIGN(*next, kDefaultAlignment);               // Ensure next pointer is aligned
      memcpy(*next, SCM_OBJ(*pval), size);                       // Copy object from from-space into to-space
      SCM_OBJ(*pval)->pfwd = SCM_TAG_PTR(*next, tag);            // Leave forwarding pointer in old from-space object
      *pval = SCM_OBJ(*pval)->pfwd;                              // Update current reference to point to new object in to-space
      *next += size;                                             // Update next pointer
    }
  }
}

void Scm_gc() {
/* TODO: Ensure scan and next pointers respect alignment */
  char* scan, *next, *tmp;

  scan = next = ctx.heap.to;

  mark_copy(&ctx.env, &next);
  mark_copy(&ctx.iport, &next);
  mark_copy(&ctx.oport, &next);

  for(int i = 0; i < ctx.stack.top; i++) {
    mark_copy(ctx.stack.roots[i], &next);
  }

  while(scan < next) {
    switch(Scm_type(scan)) {
      case PAIR:
      mark_copy(&SCM_PAIR(scan)->head, &next);
      mark_copy(&SCM_PAIR(scan)->tail, &next);
      break;
      case CLOSURE:
      mark_copy(&SCM_CLOSURE(scan)->formals, &next);
      mark_copy(&SCM_CLOSURE(scan)->body, &next);
      mark_copy(&SCM_CLOSURE(scan)->env, &next);
      break;
      default:
      break;
    }
    scan += Scm_size_of(scan);
    scan = SCM_ALIGN(scan, kDefaultAlignment);
  }

  tmp           = ctx.heap.from;
  ctx.heap.from = ctx.heap.to;
  ctx.heap.to   = tmp;
  ctx.heap.next = next;
}

ScmVal Scm_Nil_new() {
  return SCM_TAGGED(NIL, kImmediateTag);
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
  for(i = 0; i < ctx.symtab.id; i++) {
    if (strcmp(ctx.symtab.symbols[i], value) == 0) {
      return SCM_TAGGED(i, kSymbolTag);
    }
  }
  ctx.symtab.symbols[ctx.symtab.id] = malloc(strlen(value) + 1);
  strcpy(ctx.symtab.symbols[ctx.symtab.id], value);
  return SCM_TAGGED(ctx.symtab.id++, kSymbolTag);
}

ScmVal Scm_String_new(char* value) {
  size_t len = strlen(value);
  struct ScmString* string = ctx.heap.alloc(sizeof(struct ScmString) + len + 1);
  string->type = STRING;
  string->pfwd = NULL;
  string->length = len;
  strcpy(string->value, value);
  return (string);
}

ScmVal Scm_Pair_new(ScmVal head, ScmVal tail) {
  struct ScmPair* pair;
  gc_push2(head, tail);
  pair = ctx.heap.alloc(sizeof(struct ScmPair));
  pair->type = PAIR;
  pair->pfwd = NULL;
  pair->head = head;
  pair->tail = tail;
  gc_pop(2);
  return SCM_TAG_PTR(pair, kPairTag);
}

ScmVal Scm_Pair_car(ScmVal pair) {
  return SCM_PAIR(pair)->head;
}

ScmVal Scm_Pair_cdr(ScmVal pair) {
  return SCM_PAIR(pair)->tail;
}

void Scm_Pair_set_head(ScmVal pair, ScmVal value) {
  SCM_PAIR(pair)->head = value;
}

void Scm_Pair_set_tail(ScmVal pair, ScmVal value) {
  SCM_PAIR(pair)->tail = value;
}

ScmVal Scm_Procedure_new(ScmVal (*fptr)(ScmVal)) {
  struct ScmProcedure* proc;
  proc = ctx.heap.alloc(sizeof(struct ScmProcedure));
  proc->type  = PROCEDURE;
  proc->pfwd  = NULL;
  proc->fptr  = fptr;
  return SCM_TAG_PTR(proc, kObjectTag);
}

ScmVal Scm_Closure_new(ScmVal formals, ScmVal body, ScmVal env) {
  struct ScmClosure* closure;
  gc_push3(formals, body, env);
  closure = ctx.heap.alloc(sizeof(struct ScmClosure));
  closure->type    = CLOSURE;
  closure->pfwd    = NULL;
  closure->formals = formals;
  closure->body    = body;
  closure->env     = env;
  gc_pop(3);
  return SCM_TAG_PTR(closure, kObjectTag);
}

static int peekc(FILE* stream) {
  return ungetc(getc(stream), stream);
}

ScmVal Scm_Port_new(FILE* stream) {
  struct ScmPort* port;
  port = ctx.heap.alloc(sizeof(struct ScmPort));
  port->type   = PORT;
  port->pfwd   = NULL;
  port->stream = stream;
  return SCM_TAG_PTR(port, kObjectTag);
}

ScmVal Scm_Port_read(ScmVal port) {
  return Scm_parse(SCM_PORT(port)->stream);
}

ScmVal Scm_Port_read_char(ScmVal iport) {
  int c = getc(SCM_PORT(iport)->stream);
  return c == EOF ? SCM_EOF : Scm_Character_new(c);
}

ScmVal Scm_Port_peek_char(ScmVal iport) {
  int c = peekc(SCM_PORT(iport)->stream);
  return c == EOF ? SCM_EOF : Scm_Character_new(c);
}

ScmVal Scm_Port_write_char(ScmVal oport, ScmVal c) {
  fputc(SCM_UNTAG(int, c), SCM_PORT(oport)->stream);
  return SCM_UNSPECIFIED;
}

ScmVal Scm_Port_write(ScmVal oport, ScmVal obj) {
  Scm_print(SCM_PORT(oport)->stream, obj);
  return SCM_UNSPECIFIED;
}

ScmVal Scm_is_eof_obj(ScmVal obj) {
  return Scm_Boolean_new(obj == SCM_EOF);
}

static ScmVal bind_args(ScmVal formals, ScmVal args) {
  ScmVal binding, res;
  binding = SCM_UNBOUND;
  gc_push3(formals, args, binding);
  if (formals == SCM_NIL || args == SCM_NIL) {
    res = SCM_NIL;
  } else if (Scm_type(formals) == PAIR) {
    binding = Scm_cons(Scm_car(formals), Scm_car(args));
    res = bind_args(Scm_cdr(formals), Scm_cdr(args));
    res = Scm_cons(binding, res);
  } else {
    res = Scm_cons(formals, args);
    res = Scm_cons(res, SCM_NIL);
  }
  gc_pop(3);
  return res;
}

ScmVal Scm_Env_new(ScmVal formals, ScmVal args, ScmVal parent){
  ScmVal res;
  gc_push3(formals, args, parent);
  if (Scm_type(formals) == SYMBOL) {
    res = Scm_cons(formals, args);
    res = Scm_cons(res, SCM_NIL);
    res = Scm_cons(res, parent);
  } else {
    res = bind_args(formals, args);
    res = Scm_cons(res, parent);
  }
  gc_pop(3);
  return res;
}

ScmVal assq(ScmVal lst, ScmVal key){
  if (lst == SCM_NIL) {
    return SCM_NIL;
  } else if (Scm_caar(lst) == key) {
    return Scm_car(lst);
  } else {
    return assq(Scm_cdr(lst), key);
  }
}

static ScmVal lookup_binding(ScmVal env, ScmVal symbol) {
  ScmVal binding;
  binding  = assq(Scm_car(env), symbol);
  if (binding != SCM_NIL) {
    return binding;
  } else if (Scm_cdr(env) != SCM_NIL) {
    return lookup_binding(Scm_cdr(env), symbol);
  } else {
    return SCM_UNBOUND;
  }
}

ScmVal Scm_define_symbol(ScmVal env, ScmVal symbol, ScmVal value) {
  ScmVal res;
  gc_push3(env, symbol, value);
  res = lookup_binding(env, symbol);
  if (res != SCM_UNBOUND) {
    Scm_set_cdr(res, value);
  } else {
    res = Scm_cons(symbol, value);
    res = Scm_cons(res, Scm_car(env));
    Scm_set_car(env, res);
  }
  gc_pop(3);
  return symbol;
}

ScmVal Scm_lookup_symbol(ScmVal env, ScmVal symbol) {
  ScmVal binding = lookup_binding(env, symbol);
  return binding != SCM_UNBOUND ? Scm_cdr(binding) : SCM_UNBOUND;
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
  ScmVal head, tail, res;
  head = tail = SCM_UNBOUND;
  gc_push2(head, tail);
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == ')' && getc(stream)) {
    res = SCM_NIL;
  } else if (peekc(stream) == '.' && getc(stream)) {
    tail = parse_atom(stream);
    parse_list(stream);
    res = tail;
  } else {
    head = Scm_parse(stream);
    res  = parse_list(stream);
    res  = Scm_cons(head, res);
  }
  gc_pop(2);
  return res;
}

ScmVal Scm_parse(FILE* stream) {
  ScmVal res;
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == ';') while(getc(stream) != '\n'){}
  if (peekc(stream) == '(' && getc(stream)) {
    res = parse_list(stream);
  } else if (peekc(stream) == '\'' && getc(stream)) {
    res = Scm_parse(stream);
    res = Scm_cons(res, SCM_NIL);
    res = Scm_cons(Scm_Symbol_new("quote"), res);
  } else {
    res = parse_atom(stream);
  }
  return res;
}

static void print_list(FILE* ostream, ScmVal exp) {
  ScmVal tail;
  Scm_print(ostream, Scm_car(exp));
  tail = Scm_cdr(exp);
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
    case SYMBOL:      fprintf(ostream, "%s", ctx.symtab.symbols[SCM_UNTAG(int, exp)]);            break;
    case CLOSURE:     fprintf(ostream, "#<closure>");                                         break;
    case PROCEDURE:   fprintf(ostream, "#<procedure>");                                       break;
    case STRING:      fprintf(ostream, "%s", ((struct ScmString*)exp)->value);                   break;
    case CHARACTER:   fprintf(ostream, "#\\%c", SCM_UNTAG(char, exp));                        break;
    case UNSPECIFIED:                                                                         break;
    case UNBOUND:     fprintf(ostream, "#<unbound>");                                         break;
    case EOF_OBJ:     fprintf(ostream, "#<eof>");                                             break;
    case PAIR:        fprintf(ostream, "("); print_list(ostream, exp); fprintf(ostream, ")"); break;
    default:          fprintf(ostream, "undefined type");                                     break;
  }
}

ScmVal Scm_top_level_env() {
  return ctx.env;
}

static ScmVal eval_args(ScmVal args, ScmVal env) {
  ScmVal arg, res;
  arg = SCM_UNBOUND;
  gc_push3(args, env, arg);
  if (Scm_type(args) == NIL) {
    res = SCM_NIL;
  } else {
    arg = Scm_eval(Scm_car(args), env);
    res = eval_args(Scm_cdr(args), env);
    res = Scm_cons(arg, res);
  }
  gc_pop(3);
  return res;
}

static ScmVal DEFINE, IF, QUOTE, LAMBDA, BEGIN;

ScmVal Scm_eval(ScmVal exp, ScmVal env) {
  enum ScmType type;
  ScmVal       op   = SCM_UNBOUND;
  ScmVal       args = SCM_UNBOUND;
  ScmVal       res  = SCM_UNBOUND;

  gc_push4(exp, env, op, args);
  
eval:
  type = Scm_type(exp);
  if (type == PAIR){
    op = Scm_car(exp);
    if (op == DEFINE) {
      if (Scm_type(Scm_cadr(exp)) == PAIR) {
        res = Scm_Closure_new(Scm_cdadr(exp), Scm_cddr(exp), env);
        res = Scm_define_symbol(env, Scm_caadr(exp), res);
        gc_pop(4);
        return res;
      } else {
        res = Scm_eval(Scm_caddr(exp), env);
        res = Scm_define_symbol(env, Scm_cadr(exp), res);
        gc_pop(4);
        return res;
      }
    } else if (op == IF) {
      if (Scm_eval(Scm_cadr(exp), env) != SCM_FALSE) {
        exp = Scm_caddr(exp);
        goto eval;
      } else if (Scm_cdddr(exp) != SCM_NIL){
        exp = Scm_cadddr(exp);
        goto eval;
      } else {
        gc_pop(4);
        return SCM_UNSPECIFIED;
      }
    } else if (op == QUOTE) {
      gc_pop(4);
      return Scm_cadr(exp);
    } else if (op == LAMBDA) {
      res = Scm_Closure_new(Scm_cadr(exp), Scm_cddr(exp), env);
      gc_pop(4);
      return res;
    } else if (op == BEGIN) {
      exp = Scm_cdr(exp);
eval_seq:
      while(Scm_cdr(exp) != SCM_NIL) {
        Scm_eval(Scm_car(exp), env);
        exp = Scm_cdr(exp);
      }
      exp = Scm_car(exp);
      goto eval;
    } else {
      op = Scm_eval(op, env);
      type = Scm_type(op);
      if (type == PROCEDURE) {
        args = eval_args(Scm_cdr(exp), env);
        res  = SCM_PROC(op)->fptr(args);
        gc_pop(4);
        return res;
      } else if (type == CLOSURE) {
        args = eval_args(Scm_cdr(exp), env);
        env  = Scm_Env_new(SCM_CLOSURE(op)->formals, args, SCM_CLOSURE(op)->env);
        exp  = SCM_CLOSURE(op)->body;
        goto eval_seq;
      }
    }
    printf("shouldnt be here\n");
    exit(1);
    gc_pop(4);
    return SCM_UNSPECIFIED;
  } else if(type == SYMBOL) {
    res = Scm_lookup_symbol(env, exp);
    gc_pop(4);
    return res;
  } else {
    res = exp;
    gc_pop(4);
    return res;
  }
}

static ScmVal procedure_add(ScmVal args) {
  long a = SCM_UNTAG(long, Scm_car(args));
  long b = SCM_UNTAG(long, Scm_cadr(args));
  return Scm_Integer_new(a + b);
}

static ScmVal procedure_sub(ScmVal args) {
  long a = SCM_UNTAG(long, Scm_car(args));
  long b = SCM_UNTAG(long, Scm_cadr(args));
  return Scm_Integer_new(a - b);
}

static ScmVal procedure_mult(ScmVal args) {
  long a = SCM_UNTAG(long, Scm_car(args));
  long b = SCM_UNTAG(long, Scm_cadr(args));
  return Scm_Integer_new(a * b);
}

static ScmVal procedure_eq(ScmVal args) {
  return Scm_Boolean_new(Scm_car(args) == Scm_cadr(args));
}

static ScmVal procedure_car(ScmVal args) {
  return Scm_caar(args);
}

static ScmVal procedure_cdr(ScmVal args) {
  return Scm_cdar(args);
}

static ScmVal procedure_eval(ScmVal args) {
  return Scm_eval(Scm_car(args), Scm_cadr(args));
}

static ScmVal procedure_read(ScmVal args) {
  return Scm_Port_read(args == SCM_NIL ? ctx.iport : Scm_car(args));
}

static ScmVal procedure_read_char(ScmVal args) {
  return Scm_Port_read_char(args == SCM_NIL ? ctx.iport : Scm_car(args));
}

static ScmVal procedure_peek_char(ScmVal args) {
  return Scm_Port_peek_char(args == SCM_NIL ? ctx.iport : Scm_car(args));
}

static ScmVal procedure_write(ScmVal args) {
  return Scm_Port_write(Scm_cdr(args) == SCM_NIL ? ctx.oport : Scm_cadr(args), Scm_car(args));
}

static ScmVal procedure_write_char(ScmVal args) {
  return Scm_Port_write_char(Scm_cdr(args) == SCM_NIL ? ctx.oport : Scm_cadr(args), Scm_car(args));
}

static ScmVal procedure_quit(ScmVal args) {
  exit(0);
  return SCM_EOF;
}

int socket_listen(char* port);
int socket_accept(int sockfd);
int socket_connect(char* host, char* port);

static ScmVal procedure_socket_listen(ScmVal args) {
  return Scm_Integer_new(socket_listen(SCM_STRING(Scm_car(args))->value));
}

static ScmVal procedure_socket_accept(ScmVal args) {
  return Scm_Port_new(fdopen(socket_accept(SCM_UNTAG(int, Scm_car(args))), "r+"));
}

static ScmVal procedure_socket_connect(ScmVal args) {
  struct ScmString* host = SCM_STRING(Scm_car(args));
  struct ScmString* port = SCM_STRING(Scm_cadr(args));
  int sock_fd = socket_connect(host->value, port->value);
  return Scm_Port_new(fdopen(sock_fd, "r+"));
}

static ScmVal procedure_is_eof_obj(ScmVal args) {
  return Scm_is_eof_obj(Scm_car(args));
}

static ScmVal procedure_close(ScmVal args) {
  struct ScmPort* port = SCM_PORT(Scm_car(args));
  fclose(port->stream);
  port->stream = NULL;
  return SCM_UNSPECIFIED;
}

static ScmVal procedure_time_ms(ScmVal args) {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return Scm_Integer_new(tp.tv_sec * 1000 + tp.tv_usec / 10000);
}

ScmVal Scm_load(char* filename) {
  ScmVal exp = SCM_NIL;
  gc_push(exp);
  FILE* istream = fopen(filename, "r");
  if (istream) {
    exp = Scm_parse(istream);
    while(exp != SCM_EOF) {
      Scm_eval(exp, ctx.env);
      exp = Scm_parse(istream);
    }
    fclose(istream);
  }
  gc_pop(1);
  return SCM_UNSPECIFIED;
}

static ScmVal procedure_load(ScmVal args) {
  struct ScmString* string = SCM_STRING(Scm_car(args));
  return Scm_load(string->value);
}

static ScmVal procedure_interaction_env(ScmVal args) {
  return Scm_top_level_env();
}

void Scm_define(ScmVal env, char* symbol, ScmVal val) {
  Scm_define_symbol(env, Scm_Symbol_new(symbol), val);
}

static ScmVal procedure_gc(ScmVal args) {
  Scm_gc();
  return SCM_UNSPECIFIED;
}

void Scm_set_input_port(ScmVal iport) {
  ctx.iport = iport;
}

void Scm_set_output_port(ScmVal oport) {
  ctx.oport = oport;
}

void Scm_print_mem_stats() {
  printf("Heap stats:\n");
  printf("bytes requested: %ld\n", ctx.heap.requested);
  printf("bytes allocated: %ld\n", ctx.heap.size * 2);
  printf("address: %p\n", ctx.heap.buffer);
  printf("from-space address: %p, size: %ld bytes\n", ctx.heap.from, ctx.heap.size);
  printf("to-space address: %p, size: %ld bytes\n", ctx.heap.to, ctx.heap.size);
  printf("next: %p\n", ctx.heap.next);
  printf("bytes used: %ld\n", ctx.heap.next - ctx.heap.from);
}

static void heap_init(size_t size) {
  ctx.heap.requested = size;
  ctx.heap.size      = ((((size + 1)/ 2) + kDefaultAlignment - 1) / kDefaultAlignment) * kDefaultAlignment;
  ctx.heap.buffer    = malloc(ctx.heap.size * 2);
  ctx.heap.from      = SCM_ALIGN(ctx.heap.buffer, kDefaultAlignment);
  ctx.heap.to        = SCM_ALIGN(ctx.heap.from + ctx.heap.size, kDefaultAlignment);
  ctx.heap.next      = ctx.heap.from;
  ctx.heap.alloc     = bump_allocator;
}

void Scm_init(size_t heap_size) {
  ScmVal env;

  heap_init(heap_size);

  DEFINE          = Scm_Symbol_new("define");
  LAMBDA          = Scm_Symbol_new("lambda");
  IF              = Scm_Symbol_new("if");
  BEGIN           = Scm_Symbol_new("begin");
  QUOTE           = Scm_Symbol_new("quote");
  SCM_NIL         = Scm_Nil_new();
  SCM_FALSE       = Scm_Boolean_new(0);
  SCM_TRUE        = Scm_Boolean_new(1);
  SCM_UNBOUND     = SCM_TAGGED(UNBOUND, kImmediateTag);
  SCM_UNSPECIFIED = SCM_TAGGED(UNSPECIFIED, kImmediateTag);
  SCM_EOF         = SCM_TAGGED(EOF_OBJ, kImmediateTag);
  env             = Scm_Env_new(SCM_NIL, SCM_NIL, SCM_NIL);

  Scm_define(env, "gc", Scm_Procedure_new(procedure_gc));
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
  Scm_print_mem_stats();

  ctx.env       = env;
  ctx.stack.top = 0;
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

int main(int argc, char* argv[]) {
  Scm_init(kDefaultHeapSize);
  Scm_set_input_port(Scm_Port_new(stdin));
  Scm_set_output_port(Scm_Port_new(stdout));
  Scm_load("scm/init.scm");
}