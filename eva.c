#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "eva.h"

enum Tag {
  kReferenceTag = 0x0, /* 0b000 */
  kPairTag      = 0x1, /* 0b001 */
  kImmediateTag = 0x2, /* 0b010 */
  kBooleanTag   = 0x3, /* 0b011 */
  kIntegerTag   = 0x4, /* 0b100 */
  kSymbolTag    = 0x5, /* 0b101 */
  kCharacterTag = 0x6  /* 0b110 */
};

struct SymbolTable {char* symbols[65536]; int id;};
struct Heap {char* buffer; int size; char* ptr;};

#define SCM_TAG_BITS        3
#define SCM_TAG_MASK        0x7
#define SCM_PTR_BITS(ptr)   (intptr_t)(ptr)
#define SCM_GET_TAG(ptr)    SCM_PTR_BITS(ptr) & SCM_TAG_MASK
#define SCM_TAGGED(v, t)    (struct ScmVal*)(SCM_PTR_BITS(v) << SCM_TAG_BITS | t)
#define SCM_UNTAG(t, v)     ((t)(SCM_PTR_BITS(v) >> SCM_TAG_BITS))

static struct ScmVal* env;
static struct Heap heap;
static struct SymbolTable symtab;
static struct ScmVal* DEFINE, *LAMBDA, *IF, *BEGIN, *QUOTE;
struct ScmVal*        SCM_NIL, *SCM_FALSE, *SCM_TRUE, *SCM_UNBOUND, *SCM_UNSPECIFIED, *SCM_EOF;

void* (*alloc)(size_t);

static FILE* istream;
static FILE* ostream;

static struct ScmVal* iport;
static struct ScmVal* oport;

enum ScmType Scm_type(struct ScmVal* exp) {
  if (!exp) {
    return INVALID;
  }
  switch(SCM_GET_TAG(exp)) {
    case kReferenceTag: return exp->type;
    case kPairTag:      return PAIR;
    case kImmediateTag: return SCM_UNTAG(int, exp);
    case kBooleanTag:   return BOOLEAN;
    case kIntegerTag:   return INTEGER;
    case kSymbolTag:    return SYMBOL;
    case kCharacterTag: return CHARACTER;
    default:            return INVALID;
  }
}

struct ScmVal* Scm_Integer_new(long value) {
  return SCM_TAGGED(value, kIntegerTag);
}

struct ScmVal* Scm_Boolean_new(int value) {
  return SCM_TAGGED(value, kBooleanTag);
}

struct ScmVal* Scm_Character_new(int value) {
  return SCM_TAGGED(value, kCharacterTag);
}

struct ScmVal* Scm_Symbol_new(char* value) {
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

struct ScmVal* Scm_String_new(char* value) {
  struct String* string = alloc(sizeof(struct String) + strlen(value) + 1);
  string->type = STRING;
  strcpy(string->value, value);
  return (struct ScmVal*)string;
}

struct ScmVal* Scm_Pair_new(struct ScmVal* head, struct ScmVal* tail) {
  struct Pair* pair = alloc(sizeof(struct Pair));
  pair->head = head;
  pair->tail = tail;
  return SCM_TAGGED(pair, kPairTag);
}

struct ScmVal* Scm_Pair_car(struct ScmVal* pair) {
  return SCM_UNTAG(struct Pair*, pair)->head;
}

struct ScmVal* Scm_Pair_cdr(struct ScmVal* pair) {
  return SCM_UNTAG(struct Pair*, pair)->tail;
}

void Scm_Pair_set_head(struct ScmVal* pair, struct ScmVal* value) {
  SCM_UNTAG(struct Pair*, pair)->head = value;
}

void Scm_Pair_set_tail(struct ScmVal* pair, struct ScmVal* value) {
  SCM_UNTAG(struct Pair*, pair)->tail = value;
}

struct ScmVal* Scm_Procedure_new(struct ScmVal* (*fptr)(struct ScmVal*)) {
  struct Procedure* cfunc = alloc(sizeof(struct Procedure));
  cfunc->type  = PROCEDURE;
  cfunc->fptr  = fptr;
  return (struct ScmVal*)cfunc;
}

struct ScmVal* Scm_Closure_new(struct ScmVal* formals, struct ScmVal* body, struct ScmVal* env) {
  struct Closure* closure = alloc(sizeof(struct Closure));
  closure->type    = CLOSURE;
  closure->formals = formals;
  closure->body    = body;
  closure->env     = env;
  return (struct ScmVal*)closure;
}

static int peekc(FILE* stream) {
  return ungetc(getc(stream), stream);
}

struct ScmVal* Scm_Port_new(FILE* stream) {
  struct Port* port = alloc(sizeof(struct Port));
  port->type   = PORT;
  port->stream = stream;
  return (struct ScmVal*)port;
}

struct ScmVal* Scm_Port_read(struct ScmVal* value) {
  struct Port* port = (struct Port*)value;
  return Scm_parse(port->stream);
}

struct ScmVal* Scm_Port_read_char(struct ScmVal* iport) {
  struct Port* port;
  int          c;
  port = (struct Port*)iport;
  c    = getc(port->stream);
  return c == EOF ? SCM_EOF : Scm_Character_new(c);
}

struct ScmVal* Scm_Port_peek_char(struct ScmVal* iport) {
  struct Port* port = (struct Port*)iport;
  int c = peekc(port->stream);
  return c == EOF ? SCM_EOF : Scm_Character_new(c);
}

struct ScmVal* Scm_Port_write_char(struct ScmVal* oport, struct ScmVal* c) {
  struct Port* port = (struct Port*)oport;
  fputc(SCM_UNTAG(int, c), port->stream);
  return SCM_UNSPECIFIED;
}

struct ScmVal* Scm_Port_write(struct ScmVal* oport, struct ScmVal* obj) {
  struct Port* port = (struct Port*)oport;
  Scm_print(port->stream, obj);
  return SCM_UNSPECIFIED;
}

struct ScmVal* Scm_is_eof_obj(struct ScmVal* obj) {
  return Scm_Boolean_new(obj == SCM_EOF);
}

static struct ScmVal* bind_args(struct ScmVal* formals, struct ScmVal* args) {
  if (formals == SCM_NIL || args == SCM_NIL) {
    return SCM_NIL;
  } else if (Scm_type(formals) == PAIR) {
    return cons(cons(car(formals), car(args)), 
      bind_args(cdr(formals), cdr(args)));
  } else {
    return cons(cons(formals, args), SCM_NIL);
  }
}

struct ScmVal* Scm_Env_new(struct ScmVal* formals, struct ScmVal* args, struct ScmVal* parent){
  struct ScmVal* bindings;
  if (Scm_type(formals) == SYMBOL) {
    bindings = cons(cons(formals, args), SCM_NIL);  
  } else {
    bindings = bind_args(formals, args);
  }
  return cons(bindings, parent);
}

struct ScmVal* assq(struct ScmVal* lst, struct ScmVal* key){
  if (lst == SCM_NIL) { 
    return SCM_NIL;
  } else if (caar(lst) == key) {
    return car(lst);
  } else {
    return assq(cdr(lst), key);
  }
}

static struct ScmVal* lookup_binding(struct ScmVal* env, struct ScmVal* symbol) {
  struct ScmVal* binding;
  binding  = assq(car(env), symbol);
  if (binding != SCM_NIL) {
    return binding;
  } else if (cdr(env) != SCM_NIL) {
    return lookup_binding(cdr(env), symbol);
  } else {
    return NULL;
  }
}

struct ScmVal* Scm_define_symbol(struct ScmVal* env, struct ScmVal* symbol, struct ScmVal* value) {
  struct ScmVal* binding;
  binding = lookup_binding(env, symbol);
  if (binding) {
    set_cdr(binding, value);
  } else {
    set_car(env, cons(cons(symbol, value), car(env)));
  }
  return symbol;
}

struct ScmVal* Scm_lookup_symbol(struct ScmVal* env, struct ScmVal* symbol) {
  struct ScmVal* binding = lookup_binding(env, symbol);
  return binding ? cdr(binding) : SCM_UNBOUND;
}

static struct ScmVal* parse_atom(FILE* stream) {
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

static struct ScmVal* parse_list(FILE* stream) {
  struct ScmVal* head, *tail;
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

struct ScmVal* Scm_parse(FILE* stream) {
  while(isspace(peekc(stream))) getc(stream);
  if (peekc(stream) == '(' && getc(stream)) {
    return parse_list(stream);
  } else if (peekc(stream) == '\'' && getc(stream)) {
    return cons(Scm_Symbol_new("quote"), cons(Scm_parse(stream), SCM_NIL));
  } else {
    return parse_atom(stream);
  }
}

static void print_list(FILE* ostream, struct ScmVal* exp) {
  struct ScmVal* tail;
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

void Scm_print(FILE* ostream, struct ScmVal* exp) {
  if (!exp) {
    return;
  }
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

static struct ScmVal* eval_args(struct ScmVal* args, struct ScmVal* env) {
  if (Scm_type(args) == NIL) {
    return SCM_NIL;
  } else {
    return cons(Scm_eval(car(args), env), eval_args(cdr(args), env));
  }
}

struct ScmVal* Scm_eval(struct ScmVal* exp, struct ScmVal* env) {
  int               type;
  struct ScmVal*    op      = NULL;
  struct ScmVal*    res     = NULL;
  struct Procedure* cfunc   = NULL;
  struct Closure*   closure = NULL;

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
        cfunc = (struct Procedure*)op;
        return cfunc->fptr(eval_args(cdr(exp), env));
      } else if (type == CLOSURE) {
        closure = (struct Closure*)op;
        env = Scm_Env_new(closure->formals, eval_args(cdr(exp), env), closure->env);
        exp = closure->body;
        goto EVAL_SEQ;
      }
    }
    return NULL;
  } else if(type == SYMBOL) {
    return Scm_lookup_symbol(env, exp);
  } else {
    return exp;
  }
}

static struct ScmVal* procedure_add(struct ScmVal* args) {
  long a = SCM_UNTAG(long, car(args));
  long b = SCM_UNTAG(long, cadr(args));
  return Scm_Integer_new(a + b);
}

static struct ScmVal* procedure_sub(struct ScmVal* args) {
  long a = SCM_UNTAG(long, car(args));
  long b = SCM_UNTAG(long, cadr(args));
  return Scm_Integer_new(a - b);
}

static struct ScmVal* procedure_mult(struct ScmVal* args) {
  long a = SCM_UNTAG(long, car(args));
  long b = SCM_UNTAG(long, cadr(args));
  return Scm_Integer_new(a * b);
}

static struct ScmVal* procedure_eq(struct ScmVal* args) {
  return Scm_Boolean_new(car(args) == cadr(args)); 
}

static struct ScmVal* procedure_car(struct ScmVal* args) {
  return car(car(args));
}

static struct ScmVal* procedure_cdr(struct ScmVal* args) {
  return cdr(car(args));
}

static struct ScmVal* procedure_eval(struct ScmVal* args) {
  return Scm_eval(car(args), env);
}

static struct ScmVal* procedure_read(struct ScmVal* args) {
  return Scm_Port_read(args == SCM_NIL ? iport : car(args));
}

static struct ScmVal* procedure_read_char(struct ScmVal* args) {
  return Scm_Port_read_char(args == SCM_NIL ? iport : car(args));
}

static struct ScmVal* procedure_peek_char(struct ScmVal* args) {
  return Scm_Port_peek_char(args == SCM_NIL ? iport : car(args));
}

static struct ScmVal* procedure_write(struct ScmVal* args) {
  return Scm_Port_write(cdr(args) == SCM_NIL ? oport : cadr(args), car(args));
}

static struct ScmVal* procedure_write_char(struct ScmVal* args) {
  return Scm_Port_write_char(cdr(args) == SCM_NIL ? oport : cadr(args), car(args));
}

static struct ScmVal* procedure_quit(struct ScmVal* args) {
  exit(0);
  return SCM_EOF;
}

static struct ScmVal* procedure_socket_listen(struct ScmVal* args) {
  struct String* port = (struct String*)car(args);
  return Scm_Integer_new(socket_listen(port->value));
}

static struct ScmVal* procedure_socket_accept(struct ScmVal* args) {
  return Scm_Port_new(fdopen(socket_accept(SCM_UNTAG(int, car(args))), "r+"));
}

static struct ScmVal* procedure_socket_connect(struct ScmVal* args) {
  struct String* host = (struct String*)car(args);
  struct String* port = (struct String*)cadr(args);
  int sock_fd = socket_connect(host->value, port->value);
  return Scm_Port_new(fdopen(sock_fd, "r+"));
}

static struct ScmVal* procedure_is_eof_obj(struct ScmVal* args) {
  return Scm_is_eof_obj(car(args));
}

static struct ScmVal* procedure_close(struct ScmVal* args) {
  struct Port* port = (struct Port*)car(args);
  fclose(port->stream);
  port->stream = NULL;
  return SCM_UNSPECIFIED;
}

static struct ScmVal* procedure_not(struct ScmVal* args) {
  return Scm_Boolean_new(car(args) == SCM_FALSE);
}

static struct ScmVal* procedure_time_ms(struct ScmVal* args) {
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
  SCM_EOF         = SCM_TAGGED(EOF_OBJ, kImmediateTag);
  env             = Scm_Env_new(SCM_NIL, SCM_NIL, SCM_NIL);

  Scm_define_symbol(env, Scm_Symbol_new("+"), Scm_Procedure_new(procedure_add));
  Scm_define_symbol(env, Scm_Symbol_new("-"), Scm_Procedure_new(procedure_sub));
  Scm_define_symbol(env, Scm_Symbol_new("*"), Scm_Procedure_new(procedure_mult));
  Scm_define_symbol(env, Scm_Symbol_new("="), Scm_Procedure_new(procedure_eq));
  Scm_define_symbol(env, Scm_Symbol_new("car"), Scm_Procedure_new(procedure_car));
  Scm_define_symbol(env, Scm_Symbol_new("cdr"), Scm_Procedure_new(procedure_cdr));
  Scm_define_symbol(env, Scm_Symbol_new("eval"), Scm_Procedure_new(procedure_eval));
  Scm_define_symbol(env, Scm_Symbol_new("read"), Scm_Procedure_new(procedure_read));
  Scm_define_symbol(env, Scm_Symbol_new("read-char"), Scm_Procedure_new(procedure_read_char));
  Scm_define_symbol(env, Scm_Symbol_new("peek-char"), Scm_Procedure_new(procedure_peek_char));
  Scm_define_symbol(env, Scm_Symbol_new("eof-obj?"), Scm_Procedure_new(procedure_is_eof_obj));
  Scm_define_symbol(env, Scm_Symbol_new("write"), Scm_Procedure_new(procedure_write));
  Scm_define_symbol(env, Scm_Symbol_new("write-char"), Scm_Procedure_new(procedure_write_char));
  Scm_define_symbol(env, Scm_Symbol_new("quit"), Scm_Procedure_new(procedure_quit));
  Scm_define_symbol(env, Scm_Symbol_new("connect"), Scm_Procedure_new(procedure_socket_connect));
  Scm_define_symbol(env, Scm_Symbol_new("listen"), Scm_Procedure_new(procedure_socket_listen));
  Scm_define_symbol(env, Scm_Symbol_new("accept"), Scm_Procedure_new(procedure_socket_accept));
  Scm_define_symbol(env, Scm_Symbol_new("close"), Scm_Procedure_new(procedure_close));
  Scm_define_symbol(env, Scm_Symbol_new("not"), Scm_Procedure_new(procedure_not));
  Scm_define_symbol(env, Scm_Symbol_new("time-ms"), Scm_Procedure_new(procedure_time_ms));
}

#define BACKLOG 10     // how many pending connections queue will hold

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
  // loop through all the results and bind to the first we can
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
  freeaddrinfo(servinfo); // all done with this structure
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
  struct sockaddr_storage their_addr; // connector's address information
  socklen_t sin_size;
  char s[INET6_ADDRSTRLEN];
  printf("server: waiting for connections...\n");
  sin_size = sizeof their_addr;
  new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
  if (new_fd == -1) {
    return -1;
  }
  inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
  printf("server: got connection from %s\n", s);
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
  // loop through all the results and connect to the first we can
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
  printf("client: connecting to %s\n", s);
  freeaddrinfo(servinfo); // all done with this structure
  return sockfd;
}

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

  fprintf(ostream, ".------------------.\n");
  fprintf(ostream, "|  EvaScheme v0.1  |\n");
  fprintf(ostream, "'------------------'\n\n");

  struct ScmVal* exp;
  struct ScmVal* res;

  do {
    fprintf(ostream, "eva> ");
    //Scm_print(ostream, Scm_eval(Scm_parse(istream), env));

    exp = Scm_Port_read(iport);

    if (exp != SCM_EOF) {
      res = Scm_eval(exp, env);
      Scm_Port_write(oport, res);
      fprintf(ostream, "\n"); 
    }
  } while(exp != SCM_EOF);
}