#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "eva.h"

enum ScmTag {
  kObjectTag    = 0x0, /* 0b0000 */
  kPairTag      = 0x1, /* 0b0001 */
  kImmediateTag = 0x2, /* 0b0010 */
  kBooleanTag   = 0x3, /* 0b0011 */
  kIntegerTag   = 0x4, /* 0b0100 */
  kSymbolTag    = 0x5, /* 0b0101 */
  kCharacterTag = 0x6  /* 0b0110 */
};

enum { kDefaultAlignment = 16 };
enum { kTagBits          = 3 };
enum { kTagMask          = (1 << kTagBits) - 1 };
enum { kDefaultHeapSize  = 256 * 1000000 };

#define SCM_PTR_BITS(ptr)    (intptr_t)(ptr)
#define SCM_TAG(ptr)         (SCM_PTR_BITS(ptr) & kTagMask)
#define SCM_TAGGED(v, t)     (ScmVal)(SCM_PTR_BITS(v) << kTagBits | t)
#define SCM_UNTAG(t, v)      ((t)(SCM_PTR_BITS(v) >> kTagBits))
#define SCM_PTR(t, val)      ((t)(SCM_PTR_BITS(val) & ~kTagMask))
#define SCM_TAG_PTR(ptr, t)  ((ScmVal)(SCM_PTR_BITS(ptr) | t))
#define SCM_ALIGN(p, n)      (void*)((((intptr_t)p) + (n - 1)) & -n)

struct ScmHeader {
  enum ScmType type;
  ScmVal       pfwd;
};

struct ScmObj {
  struct ScmHeader header;
};

#define SCM_OBJ(val) SCM_PTR(struct ScmObj*, val)

struct ScmPair {
  struct ScmHeader header;
  ScmVal           head;
  ScmVal           tail;
};

#define SCM_PAIR(val) SCM_PTR(struct ScmPair*, val)

struct ScmClosure {
  struct ScmHeader header;
  ScmVal           formals;
  ScmVal           body;
  ScmVal           env;
};

#define SCM_CLOSURE(val) SCM_PTR(struct ScmClosure*, val)
 
struct ScmProcedure {
  struct ScmHeader header;
  ScmVal           (*fptr)(ScmVal);
};

#define SCM_PROC(val) SCM_PTR(struct ScmProcedure*, val)

struct ScmString {
  struct ScmHeader header;
  size_t           length;
  char             value[];
};

#define SCM_STRING(val) SCM_PTR(struct ScmString*, val)

struct ScmPort {
  struct ScmHeader header;
  FILE*            stream;
};

#define SCM_PORT(val) SCM_PTR(struct ScmPort*, val)

struct ScmVector {
  struct ScmHeader header;
  size_t           length;
  ScmVal           array[];
};

#define SCM_VECTOR(val) SCM_PTR(struct ScmVector*, val)

struct ScmTypeInfo {
  enum ScmType type;
  size_t       size;
  char*        name;
  enum ScmTag  tag;
};

static struct ScmTypeInfo type_info[] = {
  {kScmNilType,         sizeof(ScmVal),              "nil",         kImmediateTag}, //NIL,
  {kScmBooleanType,     sizeof(ScmVal),              "boolean",     kBooleanTag}, //BOOLEAN,
  {kScmIntegerType,     sizeof(ScmVal),              "integer",     kIntegerTag}, //INTEGER,
  {kScmSymbolType,      sizeof(ScmVal),              "symbol",      kSymbolTag},  //SYMBOL,
  {kScmCharacterType,   sizeof(ScmVal),              "character",   kCharacterTag}, //CHARACTER,
  {kScmStringType,      sizeof(struct ScmString),    "string",      kObjectTag}, //STRING,
  {kScmPairType,        sizeof(struct ScmPair),      "pair",        kPairTag}, //PAIR,
  {kScmClosureType,     sizeof(struct ScmClosure),   "closure",     kObjectTag}, //CLOSURE,
  {kScmProcedureType,   sizeof(struct ScmProcedure), "procedure",   kObjectTag}, //PROCEDURE,
  {kScmUnboundType,     sizeof(ScmVal),              "unbound",     kImmediateTag}, //UNBOUND,
  {kScmUnspecifiedType, sizeof(ScmVal),              "unspecified", kImmediateTag}, //UNSPECIFIED,
  {kScmEOFObjType,      sizeof(ScmVal),              "eof_obj",     kImmediateTag}, //EOF_OBJ,
  {kScmPortType,        sizeof(struct ScmPort),      "port",        kObjectTag}, //PORT,
  {kScmVectorType,      sizeof(struct ScmVector),    "vector",      kObjectTag}, //VECTOR,
};

enum ScmType Scm_type(ScmVal value) {
  switch(SCM_TAG(value)) {
    case kObjectTag:    return SCM_OBJ(value)->header.type;
    case kPairTag:      return kScmPairType;
    case kImmediateTag: return SCM_UNTAG(enum ScmType, value);
    case kBooleanTag:   return kScmBooleanType;
    case kIntegerTag:   return kScmIntegerType;
    case kSymbolTag:    return kScmSymbolType;
    case kCharacterTag: return kScmCharacterType;
    default:            return kScmInvalidType;
  }
}

size_t Scm_size_of(ScmVal val) {
  enum ScmType type = Scm_type(val);
  if (type == kScmStringType) {
    return sizeof(struct ScmString) + SCM_STRING(val)->length + 1;
  } else if (type == kScmVectorType) {
    return sizeof(struct ScmVector) + SCM_VECTOR(val)->length * sizeof(ScmVal);
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

int Scm_is_forwarded(ScmVal val) {
  switch(Scm_type(val)) {
    case kScmPairType:
    case kScmProcedureType:
    case kScmClosureType:
    case kScmStringType:
    case kScmPortType:
    return SCM_OBJ(val)->header.pfwd != NULL;
    default:
    return 0;
  }
}

void Scm_mark_copy(ScmVal* pval, char** next) {
  enum ScmTag  tag;
  size_t       size;
  
  if(*pval == NULL) {
    printf("found null ref, aborting\n"); exit(1); return;
  }
  
  if (Scm_Heap_contains(*pval) && !Scm_Heap_to_contains(*pval)) {
    if (Scm_is_forwarded(*pval)) {
      *pval = SCM_OBJ(*pval)->header.pfwd; // This reference points to a object in from space that has been moved, so update reference
    } else {
      tag   = SCM_TAG(*pval);                                    // Save Tag
      size  = Scm_size_of(*pval);                                // Get size of object
      *next = SCM_ALIGN(*next, kDefaultAlignment);               // Ensure next pointer is aligned
      memcpy(*next, SCM_OBJ(*pval), size);                       // Copy object from from-space into to-space
      SCM_OBJ(*pval)->header.pfwd = SCM_TAG_PTR(*next, tag);     // Leave forwarding pointer in old from-space object
      *pval = SCM_OBJ(*pval)->header.pfwd;                       // Update current reference to point to new object in to-space
      *next += size;                                             // Update next pointer
    }
  }
}

void Scm_gc() {
/* TODO: Ensure scan and next pointers respect alignment */
  char* scan, *next, *tmp;
  int i;

  scan = next = ctx.heap.to;

  Scm_mark_copy(&ctx.env, &next);
  Scm_mark_copy(&ctx.iport, &next);
  Scm_mark_copy(&ctx.oport, &next);

  for(int i = 0; i < ctx.stack.top; i++) {
    Scm_mark_copy(ctx.stack.roots[i], &next);
  }

  while(scan < next) {
    switch(Scm_type(scan)) {
      case kScmPairType:
        Scm_mark_copy(&SCM_PAIR(scan)->head, &next);
        Scm_mark_copy(&SCM_PAIR(scan)->tail, &next);
        break;
      case kScmClosureType:
        Scm_mark_copy(&SCM_CLOSURE(scan)->formals, &next);
        Scm_mark_copy(&SCM_CLOSURE(scan)->body, &next);
        Scm_mark_copy(&SCM_CLOSURE(scan)->env, &next);
        break;
      case kScmVectorType:
        for(i = 0; i < SCM_VECTOR(scan)->length; i++) {
          Scm_mark_copy(&SCM_VECTOR(scan)->array[i], &next);
        }
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
  return SCM_TAGGED(kScmNilType, kImmediateTag);
}

ScmVal Scm_Integer_new(long value) {
  return SCM_TAGGED(value, kIntegerTag);
}

#define SCM_INTEGER(val) SCM_UNTAG(intptr_t, val)

ScmVal Scm_Boolean_new(int value) {
  return SCM_TAGGED(value, kBooleanTag);
}

ScmVal Scm_Character_new(int value) {
  return SCM_TAGGED(value, kCharacterTag);
}

#define SCM_CHAR(val) SCM_UNTAG(char, val)

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
  string->header.type = kScmStringType;
  string->header.pfwd = NULL;
  string->length = len;
  strcpy(string->value, value);
  return (string);
}

ScmVal Scm_Pair_new(ScmVal head, ScmVal tail) {
  struct ScmPair* pair;
  gc_push2(head, tail);
  pair = ctx.heap.alloc(sizeof(struct ScmPair));
  pair->header.type = kScmPairType;
  pair->header.pfwd = NULL;
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
  proc->header.type  = kScmProcedureType;
  proc->header.pfwd  = NULL;
  proc->fptr  = fptr;
  return SCM_TAG_PTR(proc, kObjectTag);
}

ScmVal Scm_Closure_new(ScmVal formals, ScmVal body, ScmVal env) {
  struct ScmClosure* closure;
  gc_push3(formals, body, env);
  closure = ctx.heap.alloc(sizeof(struct ScmClosure));
  closure->header.type = kScmClosureType;
  closure->header.pfwd = NULL;
  closure->formals     = formals;
  closure->body        = body;
  closure->env         = env;
  gc_pop(3);
  return SCM_TAG_PTR(closure, kObjectTag);
}

static int peekc(FILE* stream) {
  return ungetc(getc(stream), stream);
}

ScmVal Scm_Port_new(FILE* stream) {
  struct ScmPort* port;
  port = ctx.heap.alloc(sizeof(struct ScmPort));
  port->header.type   = kScmPortType;
  port->header.pfwd   = NULL;
  port->stream        = stream;
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

ScmVal Scm_Vector_new(size_t size) {
  int i;
  struct ScmVector* vector;
  vector = ctx.heap.alloc(sizeof(struct ScmVector) + size * sizeof(ScmVal));
  vector->header.type   = kScmVectorType;
  vector->header.pfwd   = NULL;
  vector->length = size;
  for(i = 0; i < size; i++) {
    vector->array[i] = SCM_UNBOUND;
  }
  return SCM_TAG_PTR(vector, kObjectTag);
}

void Scm_Vector_set(ScmVal vector, size_t idx, ScmVal element) {
  SCM_VECTOR(vector)->array[idx] = element;
}

ScmVal Scm_Vector_ref(ScmVal vector, size_t idx) {
  return SCM_VECTOR(vector)->array[idx];
}

int list_length(ScmVal list) {
  int i = 0;
  while(list != SCM_NIL) {
   i++;
   list = Scm_cdr(list);
  }
  return i;
}

ScmVal Scm_List2Vector(ScmVal list) {
  ScmVal vector;
  int i, length;
  gc_push(list);
  length = list_length(list);
  vector = Scm_Vector_new(length);
  for(i = 0; i < length; i++, list = Scm_cdr(list)) {
    Scm_Vector_set(vector, i, Scm_car(list));
  }
  gc_pop(1);
  return vector;
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
  } else if (Scm_type(formals) == kScmPairType) {
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
  if (Scm_type(formals) == kScmSymbolType) {
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

ScmVal Scm_assq(ScmVal lst, ScmVal key){
  if (lst == SCM_NIL) {
    return SCM_NIL;
  } else if (Scm_caar(lst) == key) {
    return Scm_car(lst);
  } else {
    return Scm_assq(Scm_cdr(lst), key);
  }
}

static ScmVal lookup_binding(ScmVal env, ScmVal symbol) {
  ScmVal binding;
  binding  = Scm_assq(Scm_car(env), symbol);
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
  return SCM_UNSPECIFIED;
}

ScmVal Scm_lookup_symbol(ScmVal env, ScmVal symbol) {
  ScmVal binding = lookup_binding(env, symbol);
  return binding != SCM_UNBOUND ? Scm_cdr(binding) : SCM_UNBOUND;
}

static ScmVal Scm_parse_list(FILE* stream);
static ScmVal Scm_parse_atom(FILE* stream) {
  char  buf[1028] = "\0";
  char  *pbuf     = buf;
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == EOF) {
    return SCM_EOF;
  } else if (peekc(stream) == '#') {
    *pbuf++ = getc(stream);
    if (peekc(stream) == '(') {
      getc(stream);
      return Scm_List2Vector(Scm_parse_list(stream));
    } else if (strchr("tf", peekc(stream))) {
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

static ScmVal Scm_parse_list(FILE* stream) {
  ScmVal head, tail, res;
  head = tail = SCM_UNBOUND;
  gc_push2(head, tail);
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == ')' && getc(stream)) {
    res = SCM_NIL;
  } else if (peekc(stream) == '.' && getc(stream)) {
    tail = Scm_parse_atom(stream);
    Scm_parse_list(stream);
    res = tail;
  } else {
    head = Scm_parse(stream);
    res  = Scm_parse_list(stream);
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
    res = Scm_parse_list(stream);
  } else if (peekc(stream) == '\'' && getc(stream)) {
    res = Scm_parse(stream);
    res = Scm_cons(res, SCM_NIL);
    res = Scm_cons(Scm_Symbol_new("quote"), res);
  } else {
    res = Scm_parse_atom(stream);
  }
  return res;
}

static void Scm_print_list(FILE* ostream, ScmVal exp) {
  ScmVal tail;
  Scm_print(ostream, Scm_car(exp));
  tail = Scm_cdr(exp);
  if (tail != SCM_NIL) {
    if (Scm_type(tail) == kScmPairType) {
      fprintf(ostream, " "); Scm_print_list(ostream, tail);
    } else {
      fprintf(ostream, " . "); Scm_print(ostream, tail);
    }
  }
}

static void Scm_print_vector(FILE* ostream, ScmVal vec) {
  int i;
  Scm_print(ostream, Scm_Vector_ref(vec, 0));
  for(i = 1; i < SCM_VECTOR(vec)->length; i++) {
    fprintf(ostream, " ");
    Scm_print(ostream, Scm_Vector_ref(vec, i));
  }
}

void Scm_print(FILE* ostream, ScmVal exp) {
  switch(Scm_type(exp)) {
    case kScmNilType:         fprintf(ostream, "()");                                                 break;
    case kScmIntegerType:     fprintf(ostream, "%ld", SCM_UNTAG(long, exp));                          break;
    case kScmBooleanType:     fprintf(ostream, "#%c", exp == SCM_TRUE ? 't' : 'f');                   break;
    case kScmSymbolType:      fprintf(ostream, "%s", ctx.symtab.symbols[SCM_UNTAG(int, exp)]);        break;
    case kScmClosureType:     fprintf(ostream, "#<closure>");                                         break;
    case kScmProcedureType:   fprintf(ostream, "#<procedure>");                                       break;
    case kScmStringType:      fprintf(ostream, "%s", ((struct ScmString*)exp)->value);                break;
    case kScmCharacterType:   fprintf(ostream, "#\\%c", SCM_UNTAG(char, exp));                        break;
    case kScmUnspecifiedType:                                                                         break;
    case kScmUnboundType:     fprintf(ostream, "#<unbound>");                                         break;
    case kScmEOFObjType:      fprintf(ostream, "#<eof>");                                             break;
    case kScmPairType:        fprintf(ostream, "("); Scm_print_list(ostream, exp); fprintf(ostream, ")"); break;
    case kScmVectorType:      fprintf(ostream, "#("); Scm_print_vector(ostream, exp); fprintf(ostream, ")"); break;
    case kScmPortType:        fprintf(ostream, "#<port>");                                            break;
    default:                  fprintf(ostream, "undefined type");                                     break;
  }
}

ScmVal Scm_top_level_env() {
  return ctx.env;
}

static ScmVal Scm_eval_args(ScmVal args, ScmVal env) {
  ScmVal arg, res;
  arg = SCM_UNBOUND;
  gc_push3(args, env, arg);
  if (args == SCM_NIL) {
    res = SCM_NIL;
  } else {
    arg = Scm_eval(Scm_car(args), env);
    res = Scm_eval_args(Scm_cdr(args), env);
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
  if (type == kScmPairType){
    op = Scm_car(exp);
    if (op == DEFINE) {
      if (Scm_type(Scm_cadr(exp)) == kScmPairType) {
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
      if (type == kScmProcedureType) {
        args = Scm_eval_args(Scm_cdr(exp), env);
        res  = SCM_PROC(op)->fptr(args);
        gc_pop(4);
        return res;
      } else if (type == kScmClosureType) {
        args = Scm_eval_args(Scm_cdr(exp), env);
        env  = Scm_Env_new(SCM_CLOSURE(op)->formals, args, SCM_CLOSURE(op)->env);
        exp  = SCM_CLOSURE(op)->body;
        goto eval_seq;
      }
    }
    printf("shouldnt be here\n");
    exit(1);
    gc_pop(4);
    return SCM_UNSPECIFIED;
  } else if(type == kScmSymbolType) {
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
  char* port = SCM_STRING(Scm_car(args))->value;
  return Scm_Integer_new(socket_listen(port));
}

static ScmVal procedure_socket_accept(ScmVal args) {
  int fd = SCM_UNTAG(int, Scm_car(args));
  return Scm_Integer_new(socket_accept(fd));
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
  return Scm_Integer_new(tp.tv_sec * 1000 + tp.tv_usec / 1000);
}

static ScmVal procedure_vector_ref(ScmVal args) {
  return Scm_Vector_ref(Scm_car(args), SCM_UNTAG(long, Scm_cadr(args)));
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

static ScmVal procedure_is_eq(ScmVal args) { 
  return Scm_Boolean_new(Scm_car(args) == Scm_cadr(args));
}

static ScmVal procedure_current_input_port(ScmVal args) {
  return ctx.iport;
}

static ScmVal procedure_current_output_port(ScmVal args) {
  return ctx.oport;
}

void Scm_set_input_port(ScmVal iport) {
  ctx.iport = iport;
}

void Scm_set_output_port(ScmVal oport) {
  ctx.oport = oport;
}

static ScmVal procedure_fdopen(ScmVal args) {
  int fd    = SCM_UNTAG(int, Scm_car(args));
  char* opt = SCM_STRING(Scm_cadr(args))->value;
  return Scm_Port_new(fdopen(fd, opt));
}

static ScmVal procedure_is_pair(ScmVal args) {
  return Scm_Boolean_new(Scm_type(Scm_car(args)) == kScmPairType); 
}

static ScmVal procedure_is_symbol(ScmVal args) {
  return Scm_Boolean_new(Scm_type(Scm_car(args)) == kScmSymbolType); 
}

static ScmVal procedure_is_null(ScmVal args) {
  return Scm_Boolean_new(Scm_car(args) == SCM_NIL); 
}

static ScmVal procedure_cons(ScmVal args) {
  return Scm_cons(Scm_car(args), Scm_cadr(args));
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

static void Scm_heap_init(size_t size) {
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

  Scm_heap_init(heap_size);

  DEFINE          = Scm_Symbol_new("define");
  LAMBDA          = Scm_Symbol_new("lambda");
  IF              = Scm_Symbol_new("if");
  BEGIN           = Scm_Symbol_new("begin");
  QUOTE           = Scm_Symbol_new("quote");
  SCM_NIL         = Scm_Nil_new();
  SCM_FALSE       = Scm_Boolean_new(0);
  SCM_TRUE        = Scm_Boolean_new(1);
  SCM_UNBOUND     = SCM_TAGGED(kScmUnboundType, kImmediateTag);
  SCM_UNSPECIFIED = SCM_TAGGED(kScmUnspecifiedType, kImmediateTag);
  SCM_EOF         = SCM_TAGGED(kScmEOFObjType, kImmediateTag);
  env             = Scm_Env_new(SCM_NIL, SCM_NIL, SCM_NIL);

  Scm_define(env, "quit", Scm_Procedure_new(procedure_quit));
  Scm_define(env, "connect", Scm_Procedure_new(procedure_socket_connect));
  Scm_define(env, "listen", Scm_Procedure_new(procedure_socket_listen));
  Scm_define(env, "gc", Scm_Procedure_new(procedure_gc));
  Scm_define(env, "peek-char", Scm_Procedure_new(procedure_peek_char));
  Scm_define(env, "eof-obj?", Scm_Procedure_new(procedure_is_eof_obj));
  Scm_define(env, "current-input-port", Scm_Procedure_new(procedure_current_input_port));
  Scm_define(env, "current-output-port", Scm_Procedure_new(procedure_current_output_port));
  Scm_define(env, "vector-ref", Scm_Procedure_new(procedure_vector_ref));
  Scm_define(env, "accept", Scm_Procedure_new(procedure_socket_accept));
  Scm_define(env, "read-char", Scm_Procedure_new(procedure_read_char));
  Scm_define(env, "close", Scm_Procedure_new(procedure_close));
  Scm_define(env, "fdopen", Scm_Procedure_new(procedure_fdopen));
  Scm_define(env, "write-char", Scm_Procedure_new(procedure_write_char));  
  Scm_define(env, "eq?", Scm_Procedure_new(procedure_is_eq));
  Scm_define(env, "time-ms", Scm_Procedure_new(procedure_time_ms));
  Scm_define(env, "load", Scm_Procedure_new(procedure_load));
  Scm_define(env, "interaction-environment", Scm_Procedure_new(procedure_interaction_env));
  Scm_define(env, "write", Scm_Procedure_new(procedure_write));
  Scm_define(env, "eval", Scm_Procedure_new(procedure_eval));
  Scm_define(env, "read", Scm_Procedure_new(procedure_read));
  Scm_define(env, "car", Scm_Procedure_new(procedure_car));
  Scm_define(env, "cdr", Scm_Procedure_new(procedure_cdr));
  Scm_define(env, "cons", Scm_Procedure_new(procedure_cons));
  Scm_define(env, "symbol?", Scm_Procedure_new(procedure_is_symbol));
  Scm_define(env, "pair?", Scm_Procedure_new(procedure_is_pair));
  Scm_define(env, "null?", Scm_Procedure_new(procedure_is_null));
  Scm_define(env, "+", Scm_Procedure_new(procedure_add));
  Scm_define(env, "*", Scm_Procedure_new(procedure_mult));
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