#include "eva.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <ctype.h>
#include <assert.h>

#ifdef __GNUC__
  #define LABELS_AS_VALUES
#endif

#define ES_TAG_BITS          3
#define ES_TAG_MASK          ((1 << ES_TAG_BITS) - 1)
#define ES_PAYLOAD_MASK      (~(uintptr_t)ES_TAG_MASK)
#define ES_PAYLOAD_BITS      (sizeof(uintptr_t) * 8 - ES_TAG_BITS)
#define ES_FIXNUM_MIN        (-1 << ES_PAYLOAD_BITS)
#define ES_FIXNUM_MAX        (1 << ES_PAYLOAD_BITS)
#define ES_DEFAULT_ALIGNMENT 16
#define ES_DEFAULT_HEAP_SIZE (128 * 1000000)
#define ES_SYMTAB_SIZE       32768
#define ES_GLOBAL_ENV_SIZE   32
#define ES_ROOT_STACK_SIZE   1024
#define ES_CONST_POOL_SIZE   4096
#define ES_STACK_SIZE        4096
#define ES_MAX_FRAMES        4096

#define es_tagged_val(v, tag) ((es_val_t)(((v) << ES_TAG_BITS) | tag))
#define es_obj_to_val(o)      ((es_val_t)(o))
#define es_tag(v)             ((v) & ES_TAG_MASK)
#define es_payload(t, v)      (((t)(v)) >> ES_TAG_BITS)
#define es_obj_to(t, v)       ((t)(v))
#define es_val_to_obj(v)      ((es_obj_t*)(v))

#define gc_root(ctx, v)           ctx->roots.stack[ctx->roots.top++] = (es_val_t*)(&(v))
#define gc_root2(ctx, a, b)       gc_root(ctx, a); gc_root(ctx, b)
#define gc_root3(ctx, a, b, c)    gc_root(ctx, a); gc_root2(ctx, b, c)
#define gc_root4(ctx, a, b, c, d) gc_root(ctx, a); gc_root3(ctx, b, c, d)
#define gc_unroot(ctx, n)         ctx->roots.top -= n

typedef enum es_tag {
  ES_OBJ_TAG    = 0x0, /**< b000 - Heap allocated object */
  ES_FIXNUM_TAG = 0x1, /**< b001 - Fixnums */
  ES_SYMBOL_TAG = 0x2, /**< b010 - Symbols tag */
  ES_VALUE_TAG  = 0x3, /**< b011 - Immediate values: Nil, EOF, Unspecified, Unbound */
  ES_BOOL_TAG   = 0x4, /**< b100 - Symbol tag */
  ES_CHAR_TAG   = 0x5, /**< b101 - Character tag */
} es_tag_t;

/* VM opcodes */
typedef enum es_opcode {
  HALT,          // 0x00
  CONST,         // 0x01
  POP,           // 0x02
  GLOBAL_REF,    // 0x03
  GLOBAL_SET,    // 0x04
  CLOSED_REF,    // 0x05
  CLOSED_SET,    // 0x06
  ARG_REF,       // 0x07
  ARG_SET,       // 0x08
  JMP,           // 0x09
  BF,            // 0x0A
  CALL,          // 0x0B
  TAIL_CALL,     // 0x0C
  RETURN,        // 0x0D
  CLOSURE        // 0x0E
} es_opcode_t;

typedef enum es_vm_mode {
  ES_VM_FETCH_OPCODE,
  ES_VM_DISPATCH,
} es_vm_mode_t;

typedef struct es_inst_info {
  void*       label;
  const char* name;
  int         arity;
} es_inst_info_t;

#ifdef LABELS_AS_VALUES
typedef struct es_inst {
  void*       opcode;
  short       operand1;
  short       operand2;
} es_inst_t;
#else
typedef struct es_inst {
  es_opcode_t opcode;
  short       operand1;
  short       operand2;
} es_inst_t;
#endif

typedef struct es_heap {
  char*  buffer;     /**< Heap pointer */
  size_t size;       /**< Size of heap in bytes */
  char*  next;       /**< Pointer to_space next free memory */
  char*  from_space; /**< From space pointer */
  char*  to_space;   /**< To space pointer */
  char*  end;        /**< End of from space */
  char*  to_end;
  size_t requested;  /**< Requested heap size in bytes */
} es_heap_t;

typedef struct es_symtab {
  char* table[ES_SYMTAB_SIZE];
  int   next_id;
  int   next_gensym;
} es_symtab_t;

typedef struct es_frame {
  es_val_t   args;
  es_inst_t* knt;
} es_frame_t;

typedef struct es_roots {
  es_val_t* stack[ES_ROOT_STACK_SIZE];
  int       top;
} es_roots_t;

struct es_ctx {
  es_heap_t   heap;
  es_roots_t  roots;
  es_symtab_t symtab;
  es_val_t    iport;
  es_val_t    oport;
  es_val_t    bytecode;
  es_inst_t*  ip;
  es_val_t*   sp;
  int         fp;
  es_val_t    env;
  es_val_t    args;
  es_val_t    stack[ES_STACK_SIZE];
  es_frame_t  frames[ES_MAX_FRAMES];
};

typedef struct es_obj {
  es_type_t type;
  void*     reloc;
} es_obj_t;

typedef struct es_string {
  es_obj_t base;
  size_t   length;
  char     value[];
} es_string_t;

typedef struct es_slot {
  es_val_t val;
  es_val_t sym;
} es_slot_t;

typedef struct es_env {
  es_obj_t  base;
  int       count;
  int       size;
  es_slot_t slots[];
} es_env_t;

typedef struct es_args {
  es_obj_t        base;
  struct es_args* parent;
  int             size;
  es_val_t        args[];
} es_args_t;

typedef struct es_pair {
  es_obj_t base;
  es_val_t head;
  es_val_t tail;
} es_pair_t;

typedef struct es_vec {
  es_obj_t base;
  int      length;
  es_val_t array[];
} es_vec_t;

typedef struct es_closure {
  es_obj_t   base;
  es_args_t* env;
  es_val_t   proc;
} es_closure_t;

typedef struct es_error {
  es_obj_t base;
  char*    errstr;
} es_error_t;

typedef struct es_fn {
  es_obj_t base;
  int      arity;
  es_pfn_t pfn;
} es_fn_t;

typedef struct es_proc {
  es_obj_t base;
  int      arity;
  int      rest;
  int      addr;
  int      end;
} es_proc_t;

typedef struct es_macro {
  es_obj_t base;
  es_val_t trans;
} es_macro_t;

typedef struct es_buffer {
  es_obj_t base;
  size_t   size;
  char     buf[];
} es_buffer_t;

typedef struct es_bytecode {
  es_obj_t   base;
  es_val_t   consts[ES_CONST_POOL_SIZE];
  int        cpool_size;
  int        next_const;
  es_inst_t* inst;
  int        inst_size;
  int        next_inst;
} es_bytecode_t;

typedef struct es_cont {
  es_obj_t   base;
  es_inst_t* ip;
  int        sp;
  int        fp;
  es_val_t   env;
  es_val_t   args;
  es_val_t   stack[ES_STACK_SIZE];
  es_frame_t frames[ES_MAX_FRAMES];
} es_cont_t;

typedef struct es_port {
  es_obj_t  base;
  FILE*     stream;
  char*     buf;
  int       size;
  int       cur;
  int       tail;
  int       eof;
  size_t    nbytes;
  int       linum;
  int       colnum;
  struct {
    int top;
    struct {
      int    cur;
      size_t nbytes;
      int    linum;
      int    colnum;
    } state[256];
  };
} es_port_t;

const es_val_t es_nil       = es_tagged_val(ES_NIL_TYPE, ES_VALUE_TAG);
const es_val_t es_true      = es_tagged_val(1, ES_BOOL_TAG);
const es_val_t es_false     = es_tagged_val(0, ES_BOOL_TAG);
const es_val_t es_eof_obj   = es_tagged_val(ES_EOF_OBJ_TYPE, ES_VALUE_TAG);
const es_val_t es_void      = es_tagged_val(ES_VOID_TYPE, ES_VALUE_TAG);
const es_val_t es_unbound   = es_tagged_val(ES_UNBOUND_TYPE, ES_VALUE_TAG);
const es_val_t es_undefined = es_tagged_val(ES_UNDEFINED_TYPE, ES_VALUE_TAG);

static const es_val_t symbol_define          = es_tagged_val(0, ES_SYMBOL_TAG);
static const es_val_t symbol_if              = es_tagged_val(1, ES_SYMBOL_TAG);
static const es_val_t symbol_begin           = es_tagged_val(2, ES_SYMBOL_TAG);
static const es_val_t symbol_set             = es_tagged_val(3, ES_SYMBOL_TAG);
static const es_val_t symbol_lambda          = es_tagged_val(4, ES_SYMBOL_TAG);
static const es_val_t symbol_quote           = es_tagged_val(5, ES_SYMBOL_TAG);
static const es_val_t symbol_quasiquote      = es_tagged_val(6, ES_SYMBOL_TAG);
static const es_val_t symbol_unquote         = es_tagged_val(7, ES_SYMBOL_TAG);
static const es_val_t symbol_unquotesplicing = es_tagged_val(8, ES_SYMBOL_TAG);

static void           ctx_init(es_ctx_t* ctx, size_t heap_size);
static void           ctx_init_env(es_ctx_t* ctx);
static int            is_obj(es_val_t val);
static es_type_t      obj_type_of(es_val_t val);
static es_obj_t*      obj_reloc(es_val_t obj);
static void           obj_init(es_val_t obj, es_type_t type);
static void           es_mark_copy(es_heap_t* heap, es_val_t* pval, char** next);
static int            es_obj_is_reloc(es_val_t val);
static void*          heap_alloc(es_heap_t* heap, size_t size);
static void           heap_init(es_heap_t* heap, size_t size);
static void           symtab_init(es_symtab_t* symtab);
static char*          symtab_find_by_id(es_symtab_t* symtab, int id);
static int            symtab_id_by_string(es_symtab_t* symtab, const char* cstr);
static int            symtab_find_or_create(es_symtab_t* symtab, const char* cstr);
static int            symtab_add_string(es_symtab_t* symtab, const char* cstr);
static es_string_t*   es_string_val(es_val_t val);
static es_pair_t*     es_pair_val(es_val_t val);
static es_vec_t*      es_vector_val(es_val_t val);
static es_closure_t*  es_closure_val(es_val_t val);
static es_port_t*     es_port_val(es_val_t val);
static es_error_t*    es_error_val(es_val_t val);
static es_env_t*      es_env_val(es_val_t val);
static es_bytecode_t* es_bytecode_val(es_val_t val);
static size_t         es_vector_size_of(es_val_t vecval);
static int            timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);
static es_val_t       es_parse(es_ctx_t* ctx, es_val_t port);
static void*          es_alloc(es_ctx_t* ctx, es_type_t type, size_t size);
static void           es_port_mark(es_val_t port);
static es_val_t       compile(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_form(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_call(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_args(es_ctx_t* ctx, es_val_t bc, es_val_t exp, es_val_t scope);
static es_val_t       compile_lambda(es_ctx_t* ctx, es_val_t bc, es_val_t formals, es_val_t body, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_if(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_seq(es_ctx_t* ctx, es_val_t bc, es_val_t seq, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_define(es_ctx_t* ctx, es_val_t bc, es_val_t binding, es_val_t val, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_set(es_ctx_t* ctx, es_val_t bc, es_val_t sym, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_ref(es_ctx_t* ctx, es_val_t bc, es_val_t sym, int tail_pos, int next, es_val_t scope);
static es_val_t       compile_const(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static void           print_inst(es_ctx_t* ctx, es_val_t port, es_inst_t* inst);
static void*          es_vm_run(es_ctx_t* ctx, es_vm_mode_t mode, ...);
static es_val_t       env_ref(es_ctx_t* ctx, es_val_t env, int slot);

es_ctx_t* es_ctx_new(size_t heap_size)
{
  es_ctx_t* ctx = malloc(sizeof(es_ctx_t));
  ctx_init(ctx, heap_size);
  return ctx;
}

static void ctx_init(es_ctx_t* ctx, size_t heap_size)
{
  heap_init(&ctx->heap, heap_size);
  ctx->roots.top = 0;
  ctx->iport = es_void;
  ctx->oport = es_void;
  ctx->sp = ctx->stack;
  ctx->fp = 0;
  ctx->bytecode = es_make_bytecode(ctx);
  symtab_init(&ctx->symtab);
  es_symbol_intern(ctx, "define");
  es_symbol_intern(ctx, "if");
  es_symbol_intern(ctx, "begin");
  es_symbol_intern(ctx, "set!");
  es_symbol_intern(ctx, "lambda");
  es_symbol_intern(ctx, "quote");
  es_symbol_intern(ctx, "quasiquote");
  es_symbol_intern(ctx, "unquote");
  es_symbol_intern(ctx, "unquote-splicing");

  ctx_init_env(ctx);
}

void es_ctx_free(es_ctx_t* ctx)
{
  free(ctx);
}

es_val_t es_ctx_iport(es_ctx_t* ctx)
{
  return ctx->iport;
}

es_val_t es_ctx_oport(es_ctx_t* ctx)
{
  return ctx->oport;
}

void es_ctx_set_iport(es_ctx_t* ctx, es_val_t port)
{
  ctx->iport = port;
}

void es_ctx_set_oport(es_ctx_t* ctx, es_val_t port)
{
  ctx->oport = port;
}

es_val_t es_ctx_env(es_ctx_t* ctx)
{
  return ctx->env;
}

void es_ctx_set_env(es_ctx_t* ctx, es_val_t env)
{
  ctx->env = env;
}

static void* es_alloc(es_ctx_t* ctx, es_type_t type, size_t size)
{
  void* mem;
  while(!(mem = heap_alloc(&ctx->heap, size))) {
    //printf("out of memory\n");
    es_gc(ctx);
  }
  obj_init(es_obj_to_val(mem), type);
  return mem;
}

void es_gc_root_p(es_ctx_t* ctx, es_val_t *pv)
{
  gc_root(ctx, *pv);
}

void es_gc_unroot(es_ctx_t* ctx, int n)
{
  gc_unroot(ctx, n);
}

static long ceil_to(long n, long u)
{
  return ((n + u - 1) / u) * u;
}

#define align(v, n) ((v + n - 1) & ~(n - 1))
#define alignp(p, n) ((void*)align((uintptr_t)p, (uintptr_t)n))

static void* heap_alloc(es_heap_t* heap, size_t size)
{
  char* mem = alignp(heap->next, ES_DEFAULT_ALIGNMENT);
  if (size > (heap->end - mem)) {
    return NULL;
  }
  heap->next = mem + size;
  return mem;
}

static void heap_init(es_heap_t* heap, size_t size)
{
  heap->requested  = size;
  heap->size       = align((size + 1) / 2, ES_DEFAULT_ALIGNMENT);
  heap->buffer     = malloc(heap->size * 2);
  heap->from_space = alignp(heap->buffer, ES_DEFAULT_ALIGNMENT);
  heap->to_space   = alignp(heap->from_space + heap->size, ES_DEFAULT_ALIGNMENT);
  heap->next       = heap->from_space;
  heap->end        = heap->from_space + size;
  heap->to_end     = heap->to_space + size;
}

void es_mark_copy(es_heap_t* heap, es_val_t* ref, char** next)
{
  assert(*next < heap->to_end);
  if (!is_obj(*ref))
    return;

  if (es_obj_is_reloc(*ref)) {
    *ref = es_obj_to_val(es_val_to_obj(*ref)->reloc); // Update stale reference to relocated object
  } else {
    size_t size = es_size_of(*ref);                // Get size of object
    *next = alignp(*next, ES_DEFAULT_ALIGNMENT); // Ensure next pointer is aligned
    memcpy(*next, (void*)*ref, size);                 // Copy object from_space from_space-space into to_space-space
    es_val_to_obj(*ref)->reloc = (es_obj_t*)*next;            // Leave forwarding pointer in old from_space-space object
    *ref = es_obj_to_val(es_val_to_obj(*ref)->reloc);                    // Update current reference to_space point to_space new object in to_space-space
    es_val_to_obj(*ref)->reloc = NULL;                        // Reset tombstone
    *next += size;                                 // Update next pointer
  }
}

es_type_t es_type_of(es_val_t val)
{
  switch(es_tag(val)) {
  case ES_OBJ_TAG:    return obj_type_of(val);
  case ES_VALUE_TAG:  return es_payload(es_type_t, val);
  case ES_BOOL_TAG:   return ES_BOOL_TYPE;
  case ES_FIXNUM_TAG: return ES_FIXNUM_TYPE;
  case ES_SYMBOL_TAG: return ES_SYMBOL_TYPE;
  case ES_CHAR_TAG:   return ES_CHAR_TYPE;
  default:            return ES_INVALID_TYPE;
  }
}

static es_val_t es_port_vprintf(es_ctx_t* ctx, es_val_t port, const char* fmt, va_list args);
static void es_print(es_ctx_t* ctx, es_val_t val, es_val_t oport);

void es_printf(es_ctx_t* ctx, const char * fmt, ...) {
  va_list args;
  va_start(args, fmt);
  es_port_vprintf(ctx, ctx->oport, fmt, args);
  va_end(args);
}

void es_write(es_ctx_t* ctx, es_val_t val)
{
  es_port_write(ctx, ctx->oport, val);
}

int es_is_eq(es_val_t v1, es_val_t v2)
{
  return v1 == v2;
}

es_val_t es_make_bool(int value)
{
  return es_tagged_val(value == 0 ? 0 : 1, ES_BOOL_TAG);
}

int es_is_bool(es_val_t val)
{
  return ES_BOOL_TYPE == es_type_of(val);
}

int es_is_true(es_val_t val)
{
  return es_bool_val(val) != 0;
}

int es_bool_val(es_val_t val)
{
  return es_payload(int, val);
}

void es_bool_print(es_ctx_t* ctx, es_val_t val, es_val_t port)
{
  es_port_printf(ctx, port, "#%c", es_is_true(val) ? 't' : 'f');
}

es_val_t es_make_char(int charcode)
{
  return es_tagged_val(charcode, ES_CHAR_TAG);
}

int es_char_val(es_val_t val)
{
  return es_payload(int, val);
}

es_val_t es_make_char_cstr(char* buf)
{
  if (strcmp("space", buf) == 0) {
    return es_make_char(' ');
  } else if (strcmp("newline", buf) == 0) {
    return es_make_char('\n');
  } else if (strcmp("tab", buf) == 0) {
    return es_make_char('\t');
  } else {
    return es_make_char(*buf);
  }
}

int es_is_char(es_val_t val)
{
  return ES_CHAR_TYPE == es_type_of(val);
}

static void es_char_print(es_ctx_t* ctx, es_val_t val, es_val_t port)
{
  int c = es_char_val(val);
  switch(c) {
    case ' ':  es_port_printf(ctx, port, "#\\space");   break;
    case '\n': es_port_printf(ctx, port, "#\\newline"); break;
    case '\t': es_port_printf(ctx, port, "#\\tab");     break;
    default:   es_port_printf(ctx, port, "#\\%c", c);   break;
  }
}

static int is_obj(es_val_t val)
{
  return ES_OBJ_TAG == es_tag(val) && es_val_to_obj(val) != NULL;
}

es_obj_t* es_obj_val(es_val_t val)
{
  return es_obj_to(es_obj_t*, val);
}

static es_type_t obj_type_of(es_val_t val)
{
  return es_obj_val(val)->type;
}

static es_obj_t* obj_reloc(es_val_t obj)
{
  return es_obj_val(obj)->reloc;
}

static void obj_init(es_val_t self, es_type_t type)
{
  es_obj_t* obj = es_obj_val(self);
  obj->type  = type;
  obj->reloc = NULL;
}

static int es_obj_is_reloc(es_val_t val)
{
  return obj_reloc(val) != NULL;
}

es_val_t es_make_error(es_ctx_t* ctx, char* errstr)
{
  es_error_t* error = es_alloc(ctx, ES_ERROR_TYPE, sizeof(es_error_t));
  error->errstr = errstr;
  return es_obj_to_val(error);
}

int es_is_error(es_val_t val)
{
  return ES_ERROR_TYPE == es_type_of(val);
}

es_error_t* es_error_val(es_val_t val)
{
  return es_obj_to(es_error_t*, val);
}

static void es_error_print(es_ctx_t* ctx, es_val_t val, es_val_t port)
{
  es_port_printf(ctx, port, "#<error: %s>", es_error_val(val)->errstr);
}

es_val_t es_make_fixnum(int value)
{
  return es_tagged_val(value, ES_FIXNUM_TAG);
}

int es_is_fixnum(es_val_t val)
{
  return ES_FIXNUM_TYPE == es_type_of(val);
}

int es_fixnum_val(es_val_t fixnum)
{
  return es_payload(int, fixnum);
}

es_val_t es_number_add(es_ctx_t* ctx, es_val_t v1, es_val_t v2)
{
  int a = es_fixnum_val(v1);
  int b = es_fixnum_val(v2);
  return es_make_fixnum(a + b);
}

es_val_t es_number_sub(es_ctx_t* ctx, es_val_t v1, es_val_t v2)
{
  int a = es_fixnum_val(v1);
  int b = es_fixnum_val(v2);
  return es_make_fixnum(a - b);
}

es_val_t es_number_mul(es_ctx_t* ctx, es_val_t v1, es_val_t v2)
{
  int a = es_fixnum_val(v1);
  int b = es_fixnum_val(v2);
  return es_make_fixnum(a * b);
}

es_val_t es_number_div(es_ctx_t* ctx, es_val_t v1, es_val_t v2)
{
  int a = es_fixnum_val(v1);
  int b = es_fixnum_val(v2);
  return es_make_fixnum(a / b);
}

int es_number_is_eq(es_val_t a, es_val_t b)
{
  return es_is_eq(a, b);
}

static void es_fixnum_print(es_ctx_t* ctx, es_val_t val, es_val_t port)
{
  es_port_printf(ctx, port, "%ld", es_fixnum_val(val));
}

es_val_t es_make_pair(es_ctx_t* ctx, es_val_t head, es_val_t tail)
{
  es_pair_t* pair;
  gc_root2(ctx, head, tail);
  pair = es_alloc(ctx, ES_PAIR_TYPE, sizeof(es_pair_t));
  pair->head = head;
  pair->tail = tail;
  gc_unroot(ctx, 2);
  return es_obj_to_val(pair);
}

int es_is_pair(es_val_t val)
{
  return ES_PAIR_TYPE == es_type_of(val);
}

es_pair_t* es_pair_val(es_val_t val)
{
  return es_obj_to(es_pair_t*, val);
}

es_val_t es_pair_car(es_val_t pair)
{
  return es_pair_val(pair)->head;
}

es_val_t es_pair_cdr(es_val_t pair)
{
  return es_pair_val(pair)->tail;
}

void es_pair_set_head(es_val_t pair, es_val_t value)
{
  es_pair_val(pair)->head = value;
}

void es_pair_set_tail(es_val_t pair, es_val_t value)
{
  es_pair_val(pair)->tail = value;
}

static void es_pair_mark_copy(es_heap_t* heap, es_val_t pval, char** next)
{
  es_pair_t* pair = es_pair_val(pval);
  es_mark_copy(heap, &pair->head, next);
  es_mark_copy(heap, &pair->tail, next);
}

static void es_pair_print(es_ctx_t* ctx, es_val_t val, es_val_t port)
{
  es_val_t list = val;
  es_port_printf(ctx, port, "(");
  while(es_is_pair(es_cdr(list))) {
    es_port_printf(ctx, port, "%@ ", es_car(list));
    list = es_cdr(list);
  }
  es_port_write(ctx, port, es_car(list));
  if (!es_is_nil(es_cdr(list))) {
    es_port_printf(ctx, port, " . %@", es_cdr(list));
  }
  es_port_printf(ctx, port, ")");
}

es_val_t es_make_port(es_ctx_t* ctx, FILE* stream)
{
  es_port_t* port = es_alloc(ctx, ES_PORT_TYPE, sizeof(es_port_t));
  port->stream = stream;
  port->eof    = 0;
  port->top    = 0;
  port->tail   = 0;
  port->cur    = 0;
  port->nbytes = 0;
  port->linum  = 0;
  port->colnum = 0;
  port->size   = 65536;
  port->buf    = malloc(port->size);
  return es_obj_to_val(port);
}

int es_is_port(es_val_t val)
{
  return ES_PORT_TYPE == es_type_of(val);
}

es_port_t* es_port_val(es_val_t val)
{
  return es_obj_to(es_port_t*, val);
}

es_val_t es_port_vprintf(es_ctx_t* ctx, es_val_t port, const char* fmt, va_list args)
{
  if (!fmt || *fmt == '\0') {
    return es_void;
  }

  es_port_t* p = es_port_val(port);
  char buf[1024];
  char* loc = strstr(fmt, "%@");

  if (loc) {
    strncpy(buf, fmt, loc - fmt);
    buf[loc - fmt] = '\0';
    if (*buf != '\0') {
      vfprintf(p->stream, buf, args);
    }
    es_val_t val = va_arg(args, es_val_t);
    es_port_write(ctx, port, val);
    es_port_vprintf(ctx, port, loc + 2, args);
    return es_void;
  } else {
    vfprintf(p->stream, fmt, args);
    return es_void;
  }
}

es_val_t es_port_printf(es_ctx_t* ctx, es_val_t port, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  es_port_vprintf(ctx, port, fmt, args);
  va_end(args);
  return es_void;
}

es_val_t es_read(es_ctx_t* ctx)
{
  return es_port_read(ctx, ctx->iport);
}

es_val_t es_port_read(es_ctx_t* ctx, es_val_t port)
{
  return es_parse(ctx, port);
}

static int port_update_pos(es_val_t port, int c)
{
  es_port_t* p = es_port_val(port);
  p->nbytes++;
  p->colnum++;
  if ('\n' == c) {
    p->linum++;
    p->colnum = 0;
  }
  return c;
}

int es_port_getc(es_val_t port)
{
  es_port_t* p = es_port_val(port);
  if (p->cur < p->tail) {
    int c = p->buf[p->cur++];
    if (p->top == 0 && p->cur == p->tail)
      p->cur = p->tail = 0;
    return port_update_pos(port, c);
  } else if (p->eof) {
    return EOF;
  } else {
    int c = getc(p->stream);
    if (EOF == c) {
      p->eof = 1;
    } else if (p->top > 0) {
      assert(p->tail < p->size);
      p->buf[p->tail] = c;
      p->cur = ++p->tail;
    } else if (p->cur == p->tail) {
      p->cur = p->tail = 0;
    }
    return port_update_pos(port, c);
  }
}

int es_port_peekc(es_val_t port)
{
  es_port_t* p = es_port_val(port);
  if (p->cur < p->tail) {
    return p->buf[p->cur];
  } else {
    es_port_mark(port);
    int c = es_port_getc(port);
    es_port_reset(port);
    return c;
  }
}

void es_port_mark(es_val_t port)
{
  es_port_t* p = es_port_val(port);
  //assert(!p->mark);
  p->state[p->top].cur    = p->cur;
  p->state[p->top].nbytes = p->nbytes;
  p->state[p->top].linum  = p->linum;
  p->state[p->top].colnum = p->colnum;
  p->top++;
}

void es_port_reset(es_val_t port)
{
  es_port_t* p = es_port_val(port);
  assert(p->top > 0);
  --p->top;
  p->cur    = p->state[p->top].cur;
  p->nbytes = p->state[p->top].nbytes;
  p->linum  = p->state[p->top].linum;
  p->colnum = p->state[p->top].colnum;
}

void es_port_resume(es_val_t port)
{
  //es_port_t* p = es_port_val(port);
}

es_val_t es_port_read_char(es_val_t port)
{
  int c = es_port_getc(port);
  return c == EOF ? es_eof_obj : es_make_char(c);
}

es_val_t es_port_peek_char(es_val_t port)
{
  int c = es_port_peekc(port);
  return c == EOF ? es_eof_obj : es_make_char(c);
}

void es_port_close(es_val_t iport)
{
  es_port_t* port = es_port_val(iport);
  fclose(port->stream);
  port->stream = NULL;
}

int es_port_linum(es_val_t port)
{
  return es_port_val(port)->linum;
}

int es_port_colnum(es_val_t port)
{
  return es_port_val(port)->colnum;
}

es_val_t es_port_write_char(es_val_t oport, es_val_t c)
{
  fputc(es_char_val(c), es_port_val(oport)->stream);
  return es_void;
}

es_val_t es_port_write(es_ctx_t* ctx, es_val_t oport, es_val_t obj)
{
  es_print(ctx, obj, oport);
  return es_void;
}

es_val_t es_make_fn(es_ctx_t* ctx, int arity, es_pfn_t pfn)
{
  es_fn_t* fn = es_alloc(ctx, ES_FN_TYPE, sizeof(es_fn_t));
  fn->arity   = arity;
  fn->pfn     = pfn;
  return es_obj_to_val(fn);
}

int es_is_fn(es_val_t val)
{
  return ES_FN_TYPE == es_type_of(val);
}

es_fn_t* es_fn_val(es_val_t val)
{
  return es_obj_to(es_fn_t*, val);
}

static es_val_t es_fn_apply_argv(es_ctx_t* ctx, es_val_t fn, int argc, es_val_t* argv)
{
  return es_fn_val(fn)->pfn(ctx, argc, argv);
}

es_val_t es_make_proc(es_ctx_t* ctx, int arity, int rest, int addr, int end)
{
  es_proc_t* proc = es_alloc(ctx, ES_PROC_TYPE, sizeof(es_proc_t));
  proc->arity = arity;
  proc->rest  = rest;
  proc->addr  = addr;
  proc->end   = end;
  return es_obj_to_val(proc);
}

es_proc_t* es_proc_val(es_val_t val)
{
  return es_obj_to(es_proc_t*, val);
}

static void es_proc_print(es_ctx_t* ctx, es_val_t val, es_val_t port)
{
  es_proc_t* proc = es_proc_val(val);
  es_port_printf(ctx, port,"#<compiled-procedure\n");
  es_bytecode_t* bcode = es_bytecode_val(ctx->bytecode);
  for(int i = proc->addr; i < proc->end; i++) {
    print_inst(ctx, port, bcode->inst + i);
    es_port_printf(ctx, port, "\n");
  }
  es_port_printf(ctx, port, ">");
}

static int es_proc_addr(es_val_t proc)
{
  return es_proc_val(proc)->addr;
}

/*
static int es_proc_arity(es_val_t proc)
{
  return es_proc_val(proc)->arity;
}
*/

es_val_t es_make_cont(es_ctx_t* ctx)
{
  es_cont_t* k = es_alloc(ctx, ES_CONT_TYPE, sizeof(es_cont_t));

  return es_obj_to_val(k);
}

int es_is_cont(es_val_t val)
{
  return ES_CONT_TYPE == es_type_of(val);
}

es_cont_t* es_cont_val(es_val_t val)
{
  return es_obj_to(es_cont_t*, val);
}

es_val_t es_make_string(es_ctx_t* ctx, char* cstr)
{
  es_string_t* string;
  long length = strlen(cstr);
  string = es_alloc(ctx, ES_STRING_TYPE, sizeof(es_string_t) + length + 1);
  string->length = length;
  strcpy(string->value, cstr);
  return es_obj_to_val(string);
}

es_val_t es_string_make(es_ctx_t* ctx, int length, char c)
{
  es_string_t* string;
  string = es_alloc(ctx, ES_STRING_TYPE, sizeof(es_string_t) + length + 1);
  string->length = length;
  for(int i = 0; i < string->length; i++) {
    string->value[i] = c;
  }
  string->value[string->length] = '\0';
  return es_obj_to_val(string);
}

int es_string_ref(es_val_t string, int k)
{
  es_string_t* s = es_string_val(string);
  return s->value[k];
}

void es_string_set(es_val_t string, int k, int chr)
{

}

int es_is_string(es_val_t val)
{
  return ES_STRING_TYPE == es_type_of(val);
}

es_string_t* es_string_val(es_val_t val)
{
  return es_obj_to(es_string_t*, val);
}

es_val_t es_string_to_symbol(es_ctx_t* ctx, es_val_t val)
{
  return es_nil;
}

static size_t es_string_size_of(es_val_t value)
{
  es_string_t* string = es_string_val(value);
  return sizeof(es_string_t) + string->length + 1;
}

static void es_string_print(es_ctx_t* ctx, es_val_t self, es_val_t port)
{
  es_port_printf(ctx, port,"\"%s\"", es_string_val(self)->value);
}

es_val_t es_make_symbol(int value)
{
  return es_tagged_val(value, ES_SYMBOL_TAG);
}

int es_is_symbol(es_val_t val)
{
  return ES_SYMBOL_TYPE == es_type_of(val);
}

int es_symbol_val(es_val_t val)
{
  return es_payload(int, val);
}

es_val_t es_symbol_intern(es_ctx_t* ctx, const char* cstr)
{
  es_symtab_t* symtab = &ctx->symtab;
  int id = symtab_find_or_create(symtab, cstr);
  if (id < 0) {
    return es_make_error(ctx, "symtab count exceeded");
  }
  return es_make_symbol(id);
}

es_val_t es_symbol_to_string(es_ctx_t* ctx, es_val_t val)
{
  es_symtab_t* symtab = &ctx->symtab;
  int          symval = es_symbol_val(val);
  return es_make_string(ctx, symtab_find_by_id(symtab, symval));
}

static void es_symbol_print(es_ctx_t* ctx, es_val_t sym, es_val_t port)
{
  char* string = symtab_find_by_id(&ctx->symtab, es_symbol_val(sym));
  es_port_printf(ctx, port, "%s", string);
}

es_val_t es_make_buffer(es_ctx_t* ctx, size_t size)
{
  es_buffer_t* buffer = es_alloc(ctx, ES_BUFFER_TYPE, sizeof(es_buffer_t) + size);
  return es_obj_to_val(buffer);
}

es_buffer_t* es_buffer_val(es_val_t val)
{
  return es_obj_to(es_buffer_t*, val);
}

static size_t es_buffer_size_of(es_val_t buffer)
{
  return es_buffer_val(buffer)->size;
}

int es_is_unbound(es_val_t val)
{
  return ES_UNBOUND_TYPE == es_type_of(val);
}

int es_is_defined(es_val_t val)
{
  return ES_UNBOUND_TYPE != es_type_of(val);
}

int es_is_undefined(es_val_t val)
{
  return ES_UNDEFINED_TYPE == es_type_of(val);
}

int es_is_void(es_val_t val)
{
  return ES_VOID_TYPE == es_type_of(val);
}

int es_is_nil(es_val_t val)
{
  return ES_NIL_TYPE == es_type_of(val);
}

int es_is_eof_obj(es_val_t val)
{
  return ES_EOF_OBJ_TYPE == es_type_of(val);
}

es_val_t es_make_vector(es_ctx_t* ctx, int size)
{
  assert(size >= 0);
  es_vec_t* vector = es_alloc(ctx, ES_VECTOR_TYPE, sizeof(es_vec_t) + size * sizeof(es_val_t));
  vector->length = size;
  for(int i = 0; i < size; i++)
    vector->array[i] = es_undefined;
  return es_obj_to_val(vector);
}

int es_is_vector(es_val_t val)
{
  return ES_VECTOR_TYPE == es_type_of(val);
}

es_vec_t* es_vector_val(es_val_t val)
{
  return es_obj_to(es_vec_t*, val);
}

int es_vector_len(es_val_t vector)
{
  return es_vector_val(vector)->length;
}

void es_vector_set(es_val_t vector, int idx, es_val_t val)
{
  es_vector_val(vector)->array[idx] = val;
}

es_val_t es_vector_ref(es_val_t vector, int idx)
{
  return es_vector_val(vector)->array[idx];
}

es_val_t es_vector_from_list(es_ctx_t* ctx, es_val_t list)
{
  es_val_t vector;
  int i, length;
  gc_root(ctx, list);
  length = es_list_length(list);
  vector = es_make_vector(ctx, length);
  for(i = 0; i < length; i++, list = es_cdr(list)) {
    es_vector_set(vector, i, es_car(list));
  }
  gc_unroot(ctx, 1);
  return vector;
}

static size_t es_vector_size_of(es_val_t vecval)
{
  return sizeof(es_vec_t) + es_vector_val(vecval)->length * sizeof(es_val_t);
}

static void es_vector_mark_copy(es_heap_t* heap, es_val_t pval, char** next)
{
  es_vec_t* vector = es_vector_val(pval);
  for(int i = 0; i < vector->length; i++) {
    es_mark_copy(heap, &vector->array[i], next);
  }
}

static void es_vector_print(es_ctx_t* ctx, es_val_t self, es_val_t port)
{
  int i, len;
  es_port_printf(ctx, port, "#(%@", es_vector_ref(self, 0));
  for(i = 1, len = es_vector_len(self); i < len; i++) {
    es_port_printf(ctx, port, " %@", es_vector_ref(self, i));
  }
  es_port_printf(ctx, port, ")");
}

es_val_t es_make_env(es_ctx_t* ctx, int size)
{
  size_t sz = sizeof(es_env_t) + sizeof(es_slot_t) * size;
  es_env_t* env = es_alloc(ctx, ES_ENV_TYPE, sz);
  env->count = 0;
  env->size  = size;
  return es_obj_to_val(env);
}

static size_t es_env_size_of(es_val_t val)
{
  return sizeof(es_env_t) + es_env_val(val)->size * sizeof(es_slot_t);
}

static int es_env_size(es_val_t env)
{
  return es_env_val(env)->size;
}

static size_t es_env_count(es_val_t env)
{
  return es_env_val(env)->count;
}

static es_val_t es_env_val_of(es_val_t env, int slot)
{
  return es_env_val(env)->slots[slot].val;
}

static void env_set_val(es_val_t env, int slot, es_val_t val)
{
  es_env_val(env)->slots[slot].val = val;
}

static es_env_t* es_env_val(es_val_t val)
{
  return es_obj_to(es_env_t*, val);
}

static void es_env_mark_copy(es_heap_t* heap, es_val_t pval, char** next)
{
  es_env_t* env = es_env_val(pval);
  for(int i = 0; i < env->count; i++) {
    es_mark_copy(heap, &env->slots[i].val, next);
  }
}

static void es_env_print(es_ctx_t* ctx, es_val_t _env, es_val_t port)
{
  es_env_t* env = es_env_val(_env);
  es_port_printf(ctx, port, "#<env:\n");
  for(int i = 0; i < env->count; i++) {
    es_port_printf(ctx, port, "%4d: [%@: %@]\n",
      i, env->slots[i].sym, env->slots[i].val);
  }
  es_port_printf(ctx, port, ">");
}

/**
 * Returns the slot index for a symbol.
 *
 * @param sym A symbol
 * @return    The index for this symbol, else -1 if not bound.
 */
static int es_env_loc(es_val_t _env, es_val_t sym)
{
  es_env_t* env = es_env_val(_env);
  for(int i = 0; i < env->count; i++) {
    if (es_is_eq(env->slots[i].sym, sym)) {
      return i;
    }
  }
  return -1;
}

static int env_reserve_loc(es_ctx_t* ctx, es_val_t env, es_val_t sym, es_val_t init)
{
  int loc = es_env_loc(env, sym);
  if (loc > -1) {
    if (es_is_unbound(es_env_val_of(env, loc))) {
      env_set_val(env, loc, init);
    }
    return loc;
  }

  int size  = es_env_size(env);
  size_t count = es_env_count(env);

  if (count >= size) {
    int new_size = (size << 1) + size;
    gc_root3(ctx, env, sym, init);

    es_val_t new_env = es_make_env(ctx, new_size);

    for(int i = 0; i < count; i++) {
      es_env_val(new_env)->slots[i].sym = es_env_val(env)->slots[i].sym;
      es_env_val(new_env)->slots[i].val = es_env_val(env)->slots[i].val;
      es_env_val(new_env)->count++;
    }

    gc_unroot(ctx, 3);

    ctx->env = new_env;
    env = new_env;
  }

  es_env_val(env)->slots[es_env_val(env)->count].sym = sym;
  es_env_val(env)->slots[es_env_val(env)->count].val = init;
  return es_env_val(env)->count++;
}

es_val_t es_define_symbol(es_ctx_t* ctx, es_val_t env, es_val_t sym, es_val_t val)
{
  env_reserve_loc(ctx, env, sym, val);
  return es_void;
}

es_val_t es_define(es_ctx_t* ctx, char* symbol, es_val_t val)
{
  return es_define_symbol(ctx, es_ctx_env(ctx), es_symbol_intern(ctx, symbol), val);
}

es_val_t es_define_fn(es_ctx_t* ctx, char* name, es_pfn_t fn, int arity)
{
  return es_define(ctx, name, es_make_fn(ctx, arity, fn));
}

es_val_t es_lookup_symbol(es_ctx_t* ctx, es_val_t env, es_val_t sym)
{
  int loc = es_env_loc(env, sym);
  return loc > -1 ? env_ref(ctx, env, loc) : es_unbound;
}

/**
 * Updates the value bound to the given slot index
 *
 * @param ctx  The context
 * @param env  The environment
 * @param slot The slot index
 * @param val  The new value to bind to this index
 * @return     The bound value
 */
static es_val_t env_set(es_ctx_t* ctx, es_val_t env, int slot, es_val_t val)
{
  es_env_t* e = es_env_val(env);
  if (es_is_unbound(e->slots[slot].val)) {
    return es_make_error(ctx, "unbound symbol");
  }
  e->slots[slot].val = val;
  return es_void;
}

/**
 * Returns the value bound to the given slot index
 *
 * @param ctx  The context
 * @param env  The environment
 * @param slot The slot index
 * @return     The bound value
 */
static es_val_t env_ref(es_ctx_t* ctx, es_val_t env, int slot)
{
  es_env_t* e = es_env_val(env);
  es_val_t val = e->slots[slot].val;
  return !es_is_unbound(val) ? val : es_make_error(ctx, "unbound symbol");
}

/**
 * Creates a new argument structure
 *
 * @param ctx
 * @param parent
 * @param arity
 * @param rest
 * @param argc
 * @param argv
 * @return
 */
static es_val_t es_make_args(es_ctx_t* ctx, es_args_t* parent, int arity, int rest, int argc, es_val_t* argv)
{
  gc_root(ctx, parent);
  es_args_t* env = es_alloc(ctx, ES_ARGS_TYPE, sizeof(es_args_t) + argc * sizeof(es_val_t));
  int i = 0;
  env->parent = parent;
  env->size   = argc;
  for(; i < arity; i++)
    env->args[i] = argv[i];
  if (rest && argc > arity) {
    es_val_t iter = env->args[i] = es_cons(ctx, argv[i], es_nil);
    for(int j = i + 1; j < argc; j++) {
      es_set_cdr(iter, es_cons(ctx, argv[j], es_nil));
      iter = es_cdr(iter);
    }
  } else {
    env->args[i] = es_nil;
  }
  gc_unroot(ctx, 1);
  return es_obj_to_val(env);
}

#define es_args_val(val) ((es_args_t*)(val))

static size_t es_args_size_of(es_val_t val)
{
  es_args_t* args = es_obj_to(es_args_t*, val);
  return sizeof(es_args_t) + args->size * sizeof(es_val_t);
}

static void es_args_mark_copy(es_heap_t* heap, es_val_t pval, char** next)
{
  es_args_t* args = es_obj_to(es_args_t*, pval);
  es_mark_copy(heap, (es_val_t*)&args->parent, next);
  for(int i = 0; i < args->size; i++)
    es_mark_copy(heap, &args->args[i], next);
}

es_val_t es_make_bytecode(es_ctx_t* ctx)
{
  es_bytecode_t* b = es_alloc(ctx, ES_BYTECODE_TYPE, sizeof(es_bytecode_t));
  b->inst       = malloc(1024 * sizeof(es_inst_t));
  b->inst_size  = 1024;
  b->cpool_size = ES_CONST_POOL_SIZE;
  b->next_const = 0;
  b->next_inst  = 0;
  return es_obj_to_val(b);
}

int es_is_bytecode(es_val_t val)
{
  return ES_BYTECODE_TYPE == es_type_of(val);
}

es_bytecode_t* es_bytecode_val(es_val_t val)
{
  return es_obj_to(es_bytecode_t*, val);
}

static void es_bytecode_mark_copy(es_heap_t* heap, es_val_t val, char** next)
{
  es_bytecode_t* bc = es_bytecode_val(val);
  for(int i = 0; i < bc->next_const; i++) {
    es_mark_copy(heap, &bc->consts[i], next);
  }
}

static void print_inst(es_ctx_t* ctx, es_val_t port, es_inst_t* inst)
{
  es_inst_info_t* i = es_vm_run(NULL, ES_VM_FETCH_OPCODE, inst->opcode);
  switch(i->arity) {
  case 0:
    es_port_printf(ctx, port, "%s", i->name);
    break;
  case 1:
    es_port_printf(ctx, port, "%s %d", i->name, inst->operand1);
    break;
  case 2:
    es_port_printf(ctx, port, "%s %d %d", i->name, inst->operand1, inst->operand2);
    break;
  }
}

static void es_bytecode_print(es_ctx_t* ctx, es_val_t val, es_val_t port)
{
  es_bytecode_t* bc = es_bytecode_val(val);
  int i;
  i = 0;
  es_port_printf(ctx, port, "#<bytecode:\n");
  while(i < bc->next_inst) {
    es_port_printf(ctx, port, "%4d: ", i);
    print_inst(ctx, port, bc->inst + i);
    i++;
    es_port_printf(ctx, port, "\n");
  }
  es_port_printf(ctx, port, ",\n");
  es_port_printf(ctx, port,"const pool:\n");
  for(i = 0; i < bc->next_const; i++) {
    es_port_printf(ctx, port,"%4d: ", i);
    es_port_write(ctx, port, bc->consts[i]);
    es_port_printf(ctx, port, "\n");
  }
  es_port_printf(ctx, port, ">");
}

#ifdef LABELS_AS_VALUES
  #define opcode(_o) (((es_inst_info_t*)(es_vm_run(NULL, ES_VM_FETCH_OPCODE, _o)))->label)
#else
  #define opcode(_o) (_o)
#endif

static void emit(es_val_t code, es_inst_t inst)
{
  es_bytecode_t* b = es_bytecode_val(code);
  if (b->next_inst >= b->inst_size) {
    b->inst_size += b->inst_size / 2;
    b->inst = realloc(b->inst, b->inst_size);
  }

  b->inst[b->next_inst++] = inst;
}

static void emit_global_set(es_val_t code, int idx)
{
  emit(code, (es_inst_t){ opcode(GLOBAL_SET), idx });
}

static void emit_const(es_val_t code, int idx)
{
  emit(code, (es_inst_t){ opcode(CONST), idx });
}

static void emit_halt(es_val_t code)
{
  emit(code, (es_inst_t){ opcode(HALT) });
}

static void emit_global_ref(es_val_t code, int idx)
{
  emit(code, (es_inst_t){ opcode(GLOBAL_REF), idx });
}

static void emit_arg_ref(es_val_t code, int idx)
{
  emit(code, (es_inst_t){ opcode(ARG_REF), idx });
}

static void emit_arg_set(es_val_t code, int idx)
{
  emit(code, (es_inst_t){ opcode(ARG_SET), idx });
}

static void emit_pop(es_val_t code)
{
  emit(code, (es_inst_t){ opcode(POP) });
}

static void emit_bf(es_val_t code, int dIp)
{
  emit(code, (es_inst_t){ opcode(BF), dIp });
}

static void emit_jmp(es_val_t code, int dIp)
{
  emit(code, (es_inst_t){ opcode(JMP), dIp });
}

static void emit_closure(es_val_t code, int idx)
{
  emit(code, (es_inst_t){ opcode(CLOSURE), idx });
}

static void emit_call(es_val_t code, int argc)
{
  emit(code, (es_inst_t){ opcode(CALL), argc });
}

static void emit_tail_call(es_val_t code, int argc)
{
  emit(code, (es_inst_t){ opcode(TAIL_CALL), argc });
}

static void emit_closed_ref(es_val_t code, int depth, int idx)
{
  emit(code, (es_inst_t){ opcode(CLOSED_REF), depth, idx });
}

static void emit_closed_set(es_val_t code, int depth, int idx)
{
  emit(code, (es_inst_t){ opcode(CLOSED_SET), depth, idx });
}

static int alloc_const(es_val_t code, es_val_t v)
{
  es_bytecode_t* b = es_bytecode_val(code);
  assert(b->next_const < b->cpool_size);
  for(int i = 0; i < b->next_const; i++) {
    if (es_is_eq(v, b->consts[i]))
      return i;
  }
  b->consts[b->next_const] = v;
  return b->next_const++;
}

static int bytecode_label(es_val_t code)
{
  return es_bytecode_val(code)->next_inst;
}

//=================
// Closures
//=================
es_val_t es_make_closure(es_ctx_t* ctx, es_val_t env, es_val_t proc)
{
  es_closure_t* closure;
  gc_root2(ctx, env, proc);
  closure = es_alloc(ctx, ES_CLOSURE_TYPE, sizeof(es_closure_t));
  closure->proc = proc;
  closure->env  = (es_args_t*)env;
  gc_unroot(ctx, 2);
  return es_obj_to_val(closure);
}

int es_is_closure(es_val_t value)
{
  return ES_CLOSURE_TYPE == es_type_of(value);
}

es_closure_t* es_closure_val(es_val_t val)
{
  return es_obj_to(es_closure_t*, val);
}

es_val_t es_closure_proc(es_val_t val)
{
  return es_closure_val(val)->proc;
}

static void es_closure_mark_copy(es_heap_t* heap, es_val_t pval, char** next)
{
  es_closure_t* closure = es_closure_val(pval);
  es_mark_copy(heap, &closure->proc, next);
  es_mark_copy(heap, (es_val_t*)&closure->env, next);
}

es_val_t es_make_macro(es_ctx_t* ctx, es_val_t trans)
{
  es_macro_t* macro;
  gc_root(ctx, trans);
  macro = es_alloc(ctx, ES_MACRO_TYPE, sizeof(es_macro_t));
  macro->trans = trans;
  gc_unroot(ctx, 1);
  return es_obj_to_val(macro);
}

int es_is_macro(es_val_t val)
{
  return ES_MACRO_TYPE == es_type_of(val);
}

es_macro_t* es_macro_val(es_val_t val)
{
  return es_obj_to(es_macro_t*, val);
}

es_val_t es_macro_transformer(es_val_t macro)
{
  return es_macro_val(macro)->trans;
}

static void es_macro_mark_copy(es_heap_t* heap, es_val_t val, char** next)
{
  es_macro_t* macro = es_macro_val(val);
  es_mark_copy(heap, &macro->trans, next);
}

// PRINTER
void es_print(es_ctx_t* ctx, es_val_t val, es_val_t oport)
{
  switch(es_type_of(val)) {
  case ES_NIL_TYPE:      es_port_printf(ctx, oport, "()");              break;
  case ES_BOOL_TYPE:     es_bool_print(ctx, val, oport);                break;
  case ES_FIXNUM_TYPE:   es_fixnum_print(ctx, val, oport);              break;
  case ES_SYMBOL_TYPE:   es_symbol_print(ctx, val, oport);              break;
  case ES_CHAR_TYPE:     es_char_print(ctx, val, oport);                break;
  case ES_STRING_TYPE:   es_string_print(ctx, val, oport);              break;
  case ES_PAIR_TYPE:     es_pair_print(ctx, val, oport);                break;
  case ES_EOF_OBJ_TYPE:  es_port_printf(ctx, oport, "#<eof-obj>");      break;
  case ES_CLOSURE_TYPE:  es_port_printf(ctx, oport,"#<closure>");       break;
  case ES_FN_TYPE:       es_port_printf(ctx, oport, "#<fn>");           break;
  case ES_UNBOUND_TYPE:  es_port_printf(ctx, oport,"#<unbound>");       break;
  case ES_VOID_TYPE:     es_port_printf(ctx, oport,"#<void>");          break;
  case ES_PORT_TYPE:     es_port_printf(ctx, oport,"#<port>");          break;
  case ES_VECTOR_TYPE:   es_vector_print(ctx, val, oport);              break;
  case ES_ERROR_TYPE:    es_error_print(ctx, val, oport);               break;
  case ES_BYTECODE_TYPE: es_bytecode_print(ctx, val, oport);            break;
  case ES_PROC_TYPE:     es_proc_print(ctx, val, oport);                break;
  case ES_ENV_TYPE:      es_env_print(ctx, val, oport);                 break;
  case ES_CONT_TYPE:     es_port_printf(ctx, oport, "#<continuation>"); break;
  case ES_MACRO_TYPE:    es_port_printf(ctx, oport, "#<macro>");        break;
  case ES_INVALID_TYPE:
  default:
    break;
  }
}

// GC
size_t es_size_of(es_val_t val)
{
  switch(es_type_of(val)) {
  case ES_STRING_TYPE:       return es_string_size_of(val);
  case ES_PAIR_TYPE:         return sizeof(es_pair_t);
  case ES_CLOSURE_TYPE:      return sizeof(es_closure_t);
  case ES_PORT_TYPE:         return sizeof(es_port_t);
  case ES_VECTOR_TYPE:       return es_vector_size_of(val);
  case ES_FN_TYPE:           return sizeof(es_fn_t);
  case ES_BYTECODE_TYPE:     return sizeof(es_bytecode_t);
  case ES_ERROR_TYPE:        return sizeof(es_error_t);
  case ES_ENV_TYPE:          return es_env_size_of(val);
  case ES_PROC_TYPE:         return sizeof(es_proc_t);
  case ES_ARGS_TYPE:         return es_args_size_of(val);
  case ES_MACRO_TYPE:        return sizeof(es_macro_t);
  case ES_BUFFER_TYPE:       return es_buffer_size_of(val);
  case ES_INVALID_TYPE:      return -1;
  case ES_NIL_TYPE:
  case ES_BOOL_TYPE:
  case ES_FIXNUM_TYPE:
  case ES_SYMBOL_TYPE:
  case ES_CHAR_TYPE:
  case ES_EOF_OBJ_TYPE:
  case ES_UNBOUND_TYPE:
  case ES_UNDEFINED_TYPE:
  case ES_VOID_TYPE:
  case ES_CONT_TYPE:
    return sizeof(es_val_t);
  }
}

void es_gc(es_ctx_t* ctx)
{
/* TODO: Ensure scan and next pointers respect alignment */
  //printf("running gc\n");
  //struct timeval t0, t1, dt;
  char* scan, *next, *tmp;

  //gettimeofday(&t0, NULL);

  es_heap_t* heap = &ctx->heap;
  scan = next = heap->to_space;

  es_mark_copy(heap, &ctx->env, &next);
  es_mark_copy(heap, &ctx->iport, &next);
  es_mark_copy(heap, &ctx->oport, &next);
  es_mark_copy(heap, &ctx->bytecode, &next);

  for(int i = 0; i < ctx->roots.top; i++) {
    es_mark_copy(heap, ctx->roots.stack[i], &next);
  }

  es_mark_copy(heap, &ctx->env, &next);
  for(int i = 0; i < ctx->sp - ctx->stack; i++) {
    es_mark_copy(heap, &ctx->stack[i], &next);
  }
  for(int i = 1; i < ctx->fp; i++) {
    es_mark_copy(heap, &ctx->frames[i].args, &next);
  }

  while(scan < next) {
    es_val_t obj = es_obj_to_val(scan);
    switch(es_type_of(obj)) {
    case ES_PAIR_TYPE:      es_pair_mark_copy(heap, obj, &next);     break;
    case ES_CLOSURE_TYPE:   es_closure_mark_copy(heap, obj, &next);  break;
    case ES_VECTOR_TYPE:    es_vector_mark_copy(heap, obj, &next);   break;
    case ES_ENV_TYPE:       es_env_mark_copy(heap, obj, &next);      break;
    case ES_ARGS_TYPE:      es_args_mark_copy(heap, obj, &next);     break;
    case ES_BYTECODE_TYPE:  es_bytecode_mark_copy(heap, obj, &next); break;
    case ES_MACRO_TYPE:     es_macro_mark_copy(heap, obj, &next);    break;
    default:                                                         break;
    }
    scan = alignp(scan + es_size_of(obj), ES_DEFAULT_ALIGNMENT);
  }

  tmp              = heap->from_space;
  heap->from_space = heap->to_space;
  heap->end        = heap->from_space + heap->size;
  heap->to_space   = tmp;
  heap->to_end     = heap->to_space + heap->size;
  heap->next       = next;

  //gettimeofday(&t1, NULL);
  //timeval_subtract(&dt, &t1, &t0);
  //printf("gc time: %f\n", dt.tv_sec * 1000.0 + dt.tv_usec / 1000.0);
}

//=================
// Utils
//=================
es_val_t es_make_list(es_ctx_t* ctx, ...)
{
  es_val_t list, tail, e;
  va_list argp;
  va_start(argp, ctx);
  e = va_arg(argp, es_val_t);
  if (es_is_void(e))
    return es_nil;
  tail = list = es_cons(ctx, e, es_nil);
  while(!es_is_void(e = va_arg(argp, es_val_t))) {
    es_set_cdr(tail, es_cons(ctx, e, es_nil));
    tail = es_cdr(tail);
  }
  va_end(argp);
  return list;
}

int es_list_length(es_val_t list)
{
  int i = 0;
  while(!es_is_nil(list)) {
    list = es_cdr(list);
    i++;
  }
  return i;
}

//================
// Reader
//================
typedef enum es_token {
  tk_eof, tk_lpar, tk_hlpar, tk_rpar, tk_int, tk_tbool,
  tk_fbool, tk_str, tk_char, tk_dot, tk_sym, tk_quot,
  tk_qquot, tk_unquot, tk_unquot_splice, tk_unknown
} es_token_t;

typedef enum lstate {
  lstate_start, lstate_sign, lstate_int, lstate_dot, lstate_sym
} lstate_t;

#define pgetc(port)  es_port_getc(port)
#define ppeekc(port) es_port_peekc(port)
#define eatc(p, c)   (ppeekc(p) == c) ? pgetc(p), 1 : 0
#define eot(c)       (strchr(" \t\n\r\"()'`;", c) || c == EOF)

static int escape(int c)
{
  switch(c) {
  case 'a': return '\a';
  case 'b': return '\b';
  case 'f': return '\f';
  case 'n': return '\n';
  case 'r': return '\r';
  case 't': return '\t';
  case 'v': return '\v';
  default:  return c;
  }
}

static int eat_ws(es_val_t port)
{
  int res = 0;
  while(isspace(ppeekc(port))) {
    pgetc(port);
    res = 1;
  }
  return res;
}

static int eat_cmnt(es_val_t port)
{
  int res;
  if ((res = (ppeekc(port) == ';'))) {
    while(pgetc(port) != '\n') {}
  }
  return res;
}

static void eat_sp(es_val_t port)
{
  while(eat_ws(port) || eat_cmnt(port)) {};
}

static void eat_line(es_val_t port)
{
  while(pgetc(port) != '\n') {}
}

static lstate_t step(lstate_t s, int c)
{
  if (s == lstate_start) {
    if (strchr("+-", c)) s = lstate_sign;
    else if (isdigit(c)) s = lstate_int;
    else if ('.' == c)   s = lstate_dot;
    else                 s = lstate_sym;
  } else if (s == lstate_sign) {
    s = isdigit(c) ? lstate_int : lstate_sym;
  } else if (s == lstate_int) {
    if (!isdigit(c) && !eot(c)) s = lstate_sym;
  } else if (s == lstate_dot && !eot(c)) {
    s = lstate_sym;
  }
  return s;
}

static es_token_t next(es_val_t port, char* pbuf)
{
  es_token_t t;
  eat_sp(port);
  int c = pgetc(port);
  switch(c) {
  case -1:   t = tk_eof;    break;
  case '(':  t = tk_lpar;   break;
  case ')':  t = tk_rpar;   break;
  case '\'': t = tk_quot;   break;
  case '`':  t = tk_qquot;  break;
  case ',':
    t = tk_unquot;
    if (ppeekc(port) == '@') {
      pgetc(port);
      t = tk_unquot_splice;
    }
    break;
  case '"':
    while((c = pgetc(port)) != '"')
      *pbuf++ = '\\' == c ? escape(pgetc(port)) : c;
    t = tk_str;
    break;
  case '#':
    c = pgetc(port);
    if (c == 't')       t = tk_tbool;
    else if (c == 'f')  t = tk_fbool;
    else if (c == '(')  t = tk_hlpar;
    else if (c == '\\') {
      while(!eot(ppeekc(port)))
        *pbuf++ = pgetc(port);
      t = tk_char;
    }
    else                t = tk_unknown;
    break;
  default: {
    lstate_t s = step(lstate_start, c);
    *pbuf++ = c;
    while(!eot(ppeekc(port))) {
      s = step(s, (*pbuf++ = c = pgetc(port)));
    }
    s = step(s, ppeekc(port));
    if (s == lstate_sym)      t = tk_sym;
    else if (s == lstate_int) t = tk_int;
    else if (s == lstate_dot) t = tk_dot;
    else                      t = tk_unknown;
    }
    break;
  }
  *pbuf = '\0';
  while(strchr(" \t", ppeekc(port))) pgetc(port);
  if (ppeekc(port) == '\n') pgetc(port);
  return t;
}

static es_token_t peek(es_val_t port, char* pbuf)
{
  es_port_mark(port);
  es_token_t t = next(port, pbuf);
  es_port_reset(port);
  return t;
}

static es_val_t reverse(es_val_t lst)
{
  es_val_t res = es_nil;
  while(!es_is_nil(lst)) {
    es_val_t next = es_cdr(lst);
    es_pair_set_tail(lst, res);
    res = lst;
    lst = next;
  }
  return res;
}

static es_val_t parse_list(es_ctx_t* ctx, es_val_t port)
{
  char buf[1024];
  if (peek(port, buf) == tk_rpar) {
    next(port, buf);
    return es_nil;
  }
  es_val_t e = es_parse(ctx, port);
  if (es_is_error(e)) return e;
  es_val_t lst = es_cons(ctx, e, es_nil);
  es_val_t node = lst;
  while(peek(port, buf) != tk_rpar) {
    if (peek(port, buf) == tk_dot) {
      next(port, buf);
      es_set_cdr(node, es_parse(ctx, port));
      if (peek(port, buf) != tk_rpar) {
        printf("error syntax dotted list [Line %d, Column: %d]\n", es_port_linum(port), es_port_colnum(port));
        eat_line(port);
        return es_make_error(ctx, "syntax dotted list");
      }
      next(port, buf);
      return lst;
    }
    es_set_cdr(node, es_cons(ctx, es_parse(ctx, port), es_nil));
    node = es_cdr(node);
  }
  next(port, buf);
  return lst;
}

static es_val_t es_parse(es_ctx_t* ctx, es_val_t port)
{
  char buf[1024];
  switch(next(port, buf)) {
  case tk_eof:
    return es_eof_obj;
  case tk_str:
    return es_make_string(ctx, buf);
  case tk_sym:
    return es_symbol_intern(ctx, buf);
  case tk_int:
    return es_make_fixnum(atoi(buf));
  case tk_tbool:
    return es_true;
  case tk_fbool:
    return es_false;
  case tk_char:
    return es_make_char_cstr(buf);
  case tk_quot:
    return es_make_list(ctx, symbol_quote, es_parse(ctx, port), es_void);
  case tk_qquot:
    return es_make_list(ctx, symbol_quasiquote, es_parse(ctx, port), es_void);
  case tk_unquot:
    return es_make_list(ctx, symbol_unquote, es_parse(ctx, port), es_void);
  case tk_unquot_splice:
    return es_make_list(ctx, symbol_unquotesplicing, es_parse(ctx, port), es_void);
  case tk_hlpar: {
      es_val_t lst = es_nil;
      while(peek(port, buf) != tk_rpar) {
        lst = es_cons(ctx, es_parse(ctx, port), lst);
      }
      next(port, buf);
      return es_vector_from_list(ctx, reverse(lst));
    }
  case tk_lpar:
    return parse_list(ctx, port);
  default:
    return es_make_error(ctx, "Invalid syntax");
  }
}

static void symtab_init(es_symtab_t* symtab)
{
  symtab->next_id = 0;
  symtab->next_gensym = 0;
}

static char* symtab_find_by_id(es_symtab_t* symtab, int id)
{
  return symtab->table[id];
}

static int symtab_id_by_string(es_symtab_t* symtab, const char* cstr)
{
  for(int i = 0; i < symtab->next_id; i++) {
    if (strcmp(symtab->table[i], cstr) == 0) {
      return i;
    }
  }
  return -1;
}

es_val_t es_gensym(es_ctx_t* ctx) {
  char buf[32];
  sprintf(buf, "%%s%d", ctx->symtab.next_gensym++);
  return es_symbol_intern(ctx, buf);
}

/*
static int symtab_count(es_symtab_t* symtab)
{
  return symtab->next_id;
}
*/

static int symtab_find_or_create(es_symtab_t* symtab, const char* cstr)
{
  int id = symtab_id_by_string(symtab, cstr);
  if (id < 0) {
    return symtab_add_string(symtab, cstr);
  }
  return id;
}

static int symtab_add_string(es_symtab_t* symtab, const char* cstr)
{
  if (symtab->next_id >= ES_SYMTAB_SIZE) {
    return -1;
  }

  char* newstr = malloc(strlen(cstr) + 1);
  strcpy(newstr, cstr);
  symtab->table[symtab->next_id] = newstr;
  return symtab->next_id++;
}

static es_val_t flatten_args(es_ctx_t* ctx, es_val_t args)
{
  if (es_is_nil(args)) {
    return es_nil;
  } else if (es_is_symbol(args)) {
    return es_cons(ctx, args, es_nil);
  } else {
    return es_cons(ctx, es_car(args), flatten_args(ctx, es_cdr(args)));
  }
}

static int index_of(es_val_t lst, es_val_t e)
{
  for(int i = 0; !es_is_nil(lst); i++, lst = es_cdr(lst)) {
    if (es_is_eq(es_car(lst), e))
      return i;
  }
  return -1;
}

static es_val_t scope_args(es_val_t scope)
{
  return es_car(scope);
}

static es_val_t make_scope(es_ctx_t* ctx, es_val_t args, es_val_t parent)
{
  return es_cons(ctx, flatten_args(ctx, args), parent);
}

static es_val_t scope_parent(es_val_t scope)
{
  return es_cdr(scope);
}

static int arg_idx(es_val_t scope, es_val_t symbol, int* pidx, int* pdepth)
{
  int idx   = -1;
  int depth = 0;

  while(!es_is_nil(scope)
    && (idx = index_of(scope_args(scope), symbol)) == -1
    && !es_is_nil((scope = scope_parent(scope)))
  ) {
    depth++;
  }
  if (idx == -1) return 0;
  *pidx   = idx;
  *pdepth = depth;
  return 1;
}

static int lambda_arity(es_val_t formals, int* rest)
{
  int arity = 0;
  if (es_is_symbol(formals)) {
    *rest = 1;
    return arity;
  } else {
    while(es_is_pair(formals)) {
      arity++;
      formals = es_cdr(formals);
    }
    *rest = es_is_nil(formals) ? 0 : 1;
    return arity;
  }
}

static es_val_t compile(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope)
{
  if (es_is_pair(exp)) {
    compile_form(ctx, bc, exp, tail_pos, next, scope);
  } else if(es_is_symbol(exp)) {
    compile_ref(ctx, bc, exp, tail_pos, next, scope);
  } else {
    compile_const(ctx, bc, exp, tail_pos, next, scope);
  }
  return es_void;
}

static es_val_t compile_form(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope)
{
  es_val_t op   = es_car(exp);
  es_val_t args = es_cdr(exp);
  if (es_is_symbol(op)) {
    if (es_is_eq(op, symbol_define)) {
      compile_define(ctx, bc, es_car(args), es_cdr(args), tail_pos, next, scope);
    } else if (es_is_eq(op, symbol_if)) {
      compile_if(ctx, bc, exp, tail_pos, next, scope);
    } else if (es_is_eq(op, symbol_lambda)) {
      es_val_t formals = es_car(args);
      es_val_t body    = es_cdr(args);
      compile_lambda(ctx, bc, formals, body, tail_pos, next, scope);
    } else if (es_is_eq(op, symbol_begin)) {
      compile_seq(ctx, bc, args, tail_pos, next, scope);
    } else if (es_is_eq(op, symbol_set)) {
      compile_set(ctx, bc, es_car(args), es_cadr(args), tail_pos, next, scope);
    } else if (es_is_eq(op, symbol_quote)) {
      emit_const(bc, alloc_const(bc, es_car(args)));
      if (tail_pos) emit(bc, (es_inst_t){ opcode(next) });
    } else {
      compile_call(ctx, bc, exp, tail_pos, next, scope);
    }
  } else {
    compile_call(ctx, bc, exp, tail_pos, next, scope);
  }
  return es_void;
}

static es_val_t compile_const(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope)
{
  emit_const(bc, alloc_const(bc, exp));
  if (tail_pos) emit(bc, (es_inst_t){ opcode(next) });
  return es_void;
}

static es_val_t compile_ref(es_ctx_t* ctx, es_val_t bc, es_val_t sym, int tail_pos, int next, es_val_t scope)
{
  int idx, depth;
  if (arg_idx(scope, sym, &idx, &depth)) {
    if (depth == 0) {
      emit_arg_ref(bc, idx);
    } else {
      emit_closed_ref(bc, depth, idx);
    }
  } else {
    emit_global_ref(bc, env_reserve_loc(ctx, es_ctx_env(ctx), sym, es_unbound));
  }
  if (tail_pos) emit(bc, (es_inst_t){ opcode(next) });
  return es_void;
}

static es_val_t compile_set(es_ctx_t* ctx, es_val_t bc, es_val_t sym, es_val_t exp, int tail_pos, int next, es_val_t scope)
{
  compile(ctx, bc, exp, 0, 0, scope);
  int idx, depth;
  if (arg_idx(scope, sym, &idx, &depth)) {
    depth == 0 ? emit_arg_set(bc, idx) : emit_closed_set(bc, depth, idx);
  } else {
    emit_global_set(bc, env_reserve_loc(ctx, es_ctx_env(ctx), sym, es_unbound));
  }
  if (tail_pos) emit(bc, (es_inst_t){ opcode(next) });
  return es_void;
}

static es_val_t compile_call(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope)
{
  es_val_t op   = es_car(exp);
  es_val_t args = es_cdr(exp);
  int argc      = es_list_length(args);
  compile_args(ctx, bc, args, scope);
  compile(ctx, bc, op, 0, 0, scope);
  if (tail_pos) {
    emit_tail_call(bc, argc);
  } else {
    emit_call(bc, argc);
  }
  return es_void;
}

static es_val_t compile_lambda(es_ctx_t* ctx, es_val_t bc, es_val_t formals, es_val_t body, int tail_pos, int next, es_val_t scope)
{
  int label1 = bytecode_label(bc);
  emit_jmp(bc, -1);
  int label2 = bytecode_label(bc);
  scope = make_scope(ctx, formals, scope);
  compile_seq(ctx, bc, body, 1, RETURN, scope);
  int label3 = bytecode_label(bc);
  int rest;
  int arity = lambda_arity(formals, &rest);
  es_val_t proc = es_make_proc(ctx, arity, rest, label2, label3);
  emit_closure(bc, alloc_const(bc, proc));
  es_bytecode_val(bc)->inst[label1].operand1 = label3 - label1;
  if (tail_pos) emit(bc, (es_inst_t){ opcode(next) });
  return es_void;
}

static es_val_t compile_if(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope)
{
  es_val_t cond = es_cadr(exp);
  es_val_t bthen = es_caddr(exp);
  compile(ctx, bc, cond, 0, 0, scope);
  int label1 = bytecode_label(bc);
  emit_bf(bc, -1);
  compile(ctx, bc, bthen, tail_pos, next, scope);
  int label2 = bytecode_label(bc);
  if (!tail_pos) emit_jmp(bc, -1);
  int label3 = bytecode_label(bc);
  if (!es_is_nil(es_cdddr(exp))) {
    es_val_t belse = es_cadddr(exp);
    compile(ctx, bc, belse, tail_pos, next, scope);
  } else {
    emit_const(bc, alloc_const(bc, es_undefined));
  }
  int label4 = bytecode_label(bc);
  es_bytecode_val(bc)->inst[label1].operand1 = label3 - label1;
  if (!tail_pos) es_bytecode_val(bc)->inst[label2].operand1 = label4 - label2;
  return es_void;
}

static es_val_t compile_define(es_ctx_t* ctx, es_val_t bc, es_val_t binding, es_val_t val, int tail_pos, int next, es_val_t scope)
{
  if (es_is_symbol(binding)) {
    compile(ctx, bc, es_car(val), 0, 0, scope);
    emit_global_set(bc, env_reserve_loc(ctx, es_ctx_env(ctx), binding, es_undefined));
    if (tail_pos) emit(bc, (es_inst_t){ opcode(next) });
  } else if (es_is_pair(binding)) {
    es_val_t sym     = es_car(binding);
    es_val_t formals = es_cdr(binding);
    es_val_t body    = val;
    compile_lambda(ctx, bc, formals, body, 0, 0, scope);
    emit_global_set(bc, env_reserve_loc(ctx, es_ctx_env(ctx), sym, es_undefined));
    if (tail_pos) emit(bc, (es_inst_t){ opcode(next) });
  } else {
    return es_make_error(ctx, "invalid define syntax");
  }
  return es_void;
}

static es_val_t compile_seq(es_ctx_t* ctx, es_val_t bc, es_val_t seq, int tail_pos, int next, es_val_t scope)
{
  while(!es_is_nil(es_cdr(seq))) {
    compile(ctx, bc, es_car(seq), 0, 0, scope);
    emit_pop(bc);
    seq = es_cdr(seq);
  }
  compile(ctx, bc, es_car(seq), tail_pos, next, scope);
  return es_void;
}

static es_val_t compile_args(es_ctx_t* ctx, es_val_t bc, es_val_t args, es_val_t scope)
{
  if (!es_is_nil(args)) {
    compile(ctx, bc, es_car(args), 0, 0, scope);
    compile_args(ctx, bc, es_cdr(args), scope);
  }
  return es_void;
}

//=============
// VM
//=============
#define pop(ctx)             *--ctx->sp
#define pop_n(ctx, n)        ctx->sp -= n
#define push(ctx, v)         *ctx->sp++ = v
#define restore(ctx)         ctx->fp--; \
                             ctx->args = ctx->frames[ctx->fp].args; \
                             ctx->ip   = ctx->frames[ctx->fp].knt;
#define save(ctx)            ctx->frames[ctx->fp].args = ctx->args; \
                             ctx->frames[ctx->fp].knt  = ctx->ip; \
                             ctx->fp++;

static void* es_vm_run(es_ctx_t* ctx, es_vm_mode_t mode, ...)
{

#ifdef LABELS_AS_VALUES
  static es_inst_info_t inst_info[] = {
    { &&HALT,       "halt",       0 },
    { &&CONST,      "const",      1 },
    { &&POP,        "pop",        0 },
    { &&GLOBAL_REF, "global-ref", 1 },
    { &&GLOBAL_SET, "global-set", 1 },
    { &&CLOSED_REF, "closed-ref", 2 },
    { &&CLOSED_SET, "closed-set", 2 },
    { &&ARG_REF,    "arg-ref",    1 },
    { &&ARG_SET,    "arg-set",    1 },
    { &&JMP,        "jmp",        1 },
    { &&BF,         "bf",         1 },
    { &&CALL,       "call",       1 },
    { &&TAIL_CALL,  "tail-call",  1 },
    { &&RETURN,     "return",     0 },
    { &&CLOSURE,    "closure",    1 }
  };

  if (mode == ES_VM_FETCH_OPCODE) {
    va_list ap;
    va_start(ap, mode);
    es_opcode_t opcode = va_arg(ap, es_opcode_t);
    va_end(ap);
    return &inst_info[opcode];
  }
#endif

  va_list ap;
  va_start(ap, mode);
  es_val_t proc = va_arg(ap, es_val_t);
  va_end(ap);

  es_inst_t* inst  = es_bytecode_val(ctx->bytecode)->inst;
  ctx->ip          = inst + es_proc_addr(proc);
  ctx->args        = es_nil;
  es_val_t* consts = es_bytecode_val(ctx->bytecode)->consts;

  #ifdef LABELS_AS_VALUES
    #define SWITCH(value) goto *(value);
    #define CASE(label)   label
    #define BREAK         goto *(ctx->ip->opcode)
  #else
    #define SWITCH(value) switch(value)
    #define CASE(label)   case label
    #define BREAK         break
  #endif

  while(1) {
    SWITCH(ctx->ip->opcode) {
      CASE(HALT):
        //assert(ctx->sp - ctx->stack == 1);
        return (void*)pop(ctx);
      CASE(CONST): {
        int const_idx = ctx->ip->operand1;
        push(ctx, consts[const_idx]);
        ctx->ip++;
        BREAK;
      }
      CASE(POP):
        ctx->sp--;
        ctx->ip++;
        BREAK;
      CASE(BF): {
        int else_br = ctx->ip->operand1;
        es_val_t val = pop(ctx);
        ctx->ip += es_is_true(val) ? 1 : else_br;
        BREAK;
      }
      CASE(JMP):
        ctx->ip += ctx->ip->operand1;
        BREAK;
      CASE(GLOBAL_REF): {
        int global_idx      = ctx->ip->operand1;
        es_val_t global_val = env_ref(ctx, ctx->env, global_idx);
        push(ctx, global_val);
        ctx->ip++;
        BREAK;
      }
      CASE(GLOBAL_SET): {
        int global_idx = ctx->ip->operand1;
        es_val_t val   = pop(ctx);
        push(ctx, env_set(ctx, ctx->env, global_idx, val));
        ctx->ip++;
        BREAK;
      }
      CASE(CLOSED_REF): {
        int depth = ctx->ip->operand1;
        int idx   = ctx->ip->operand2;
        es_args_t* cenv = es_args_val(ctx->args);
        while(depth-- > 0) {
          cenv = cenv->parent;
        }
        push(ctx, cenv->args[idx]);
        ctx->ip++;
        BREAK;
      }
      CASE(CLOSED_SET): {
        int depth = ctx->ip->operand1;
        int idx   = ctx->ip->operand2;
        es_args_t* cenv = es_args_val(ctx->args);
        while(depth-- > 0) {
          cenv = cenv->parent;
        }
        cenv->args[idx] = pop(ctx);
        push(ctx, es_void);
        ctx->ip++;
        BREAK;
      }
      CASE(ARG_REF): {
        int arg_idx  = ctx->ip->operand1;
        es_val_t arg = es_args_val(ctx->args)->args[arg_idx];
        push(ctx, arg);
        ctx->ip++;
        BREAK;
      }
      CASE(ARG_SET): {
        int arg_idx  = ctx->ip->operand1;
        es_val_t val = pop(ctx);
        es_args_val(ctx->args)->args[arg_idx] = val;
        ctx->ip++;
        BREAK;
      }
      CASE(CLOSURE): {
        int const_idx = ctx->ip->operand1;
        es_val_t proc = consts[const_idx];
        es_val_t closure = es_make_closure(ctx, ctx->args, proc);
        push(ctx, closure);
        ctx->ip++;
        BREAK;
      }
      CASE(RETURN):
        restore(ctx);
        BREAK;
      CASE(CALL): {
        int argc = ctx->ip->operand1;
        ctx->ip++;
        es_val_t proc = pop(ctx);
        if (es_is_fn(proc)) {
          es_val_t* argv = ctx->sp - argc;
          es_val_t res   = es_fn_apply_argv(ctx, proc, argc, argv);
          pop_n(ctx, argc);
          push(ctx, res);
        } else if (es_is_closure(proc)) {
          es_closure_t* closure = es_closure_val(proc);
          es_proc_t* proc = es_obj_to(es_proc_t*, closure->proc);
          save(ctx);
          es_val_t* argv = ctx->sp - argc;
          ctx->args  = es_make_args(ctx, closure->env, proc->arity, proc->rest, argc, argv);
          pop_n(ctx, argc);
          ctx->ip = inst + proc->addr;
        } else if (es_is_cont(proc)) {
          // TODO
          assert(0);
        }
        BREAK;
      }
      CASE(TAIL_CALL): {
        int argc = ctx->ip->operand1;
        ctx->ip++;
        es_val_t proc = pop(ctx);
        if (es_is_fn(proc)) {
          es_val_t* argv = ctx->sp - argc;
          es_val_t res   = es_fn_apply_argv(ctx, proc, argc, argv);
          pop_n(ctx, argc);
          push(ctx, res);
          restore(ctx);
        } else if (es_is_closure(proc)) {
          es_closure_t* closure = es_closure_val(proc);
          es_proc_t* proc = es_obj_to(es_proc_t*, closure->proc);
          es_val_t* argv  = ctx->sp - argc;
          ctx->args   = es_make_args(ctx, closure->env, proc->arity, proc->rest, argc, argv);
          pop_n(ctx, argc);
          ctx->ip = inst + proc->addr;
        } else if (es_is_cont(proc)) {
          // TODO
          assert(0);
        }
        BREAK;
      }
    }
  }
  return (void*)es_void;
}

static int
timeval_subtract (result, x, y)
      struct timeval *result, *x, *y;
 {
   /* Perform the carry for the later subtraction by updating y. */
   if (x->tv_usec < y->tv_usec) {
     int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
     y->tv_usec -= 1000000 * nsec;
     y->tv_sec += nsec;
   }
   if (x->tv_usec - y->tv_usec > 1000000) {
     int nsec = (x->tv_usec - y->tv_usec) / 1000000;
     y->tv_usec += 1000000 * nsec;
     y->tv_sec -= nsec;
   }

   /* Compute the time remaining to wait.
      tv_usec is certainly positive. */
   result->tv_sec = x->tv_sec - y->tv_sec;
   result->tv_usec = x->tv_usec - y->tv_usec;

   /* Return 1 if result is negative. */
   return x->tv_sec < y->tv_sec;
 }

es_val_t es_compile(es_ctx_t* ctx, es_val_t exp)
{
  int start;
  es_val_t b = ctx->bytecode;
  start = bytecode_label(b);
  compile(ctx, b, exp, 0, 0, es_nil);
  emit_halt(b);
  int end = bytecode_label(b);

  return es_make_proc(ctx, 0, 0, start, end);
}

es_val_t es_load(es_ctx_t* ctx, const char* file_name)
{
  es_val_t exp, port;
  port = es_make_port(ctx, fopen(file_name, "r"));
  es_ctx_set_oport(ctx, es_make_port(ctx, stdout));
  while(!es_is_eof_obj(exp = es_port_read(ctx, port))) {
    es_eval(ctx, exp);
  }
  es_port_close(port);
  return es_void;
}

es_val_t es_macro_expand(es_ctx_t* ctx, es_val_t exp, es_val_t env);
es_val_t es_macro_expand_list(es_ctx_t* ctx, es_val_t lst, es_val_t env)
{
  //es_printf(ctx, "macro-expand-list: %@\n", lst);
  if (es_is_nil(lst)) {
    return lst;
  } else if (es_is_pair(es_cdr(lst))) {
    return es_cons(ctx,
      es_macro_expand(ctx, es_car(lst), env),
      es_macro_expand_list(ctx, es_cdr(lst), env));
  } else {
    return es_cons(ctx, es_macro_expand(ctx, es_car(lst), env),
      es_macro_expand(ctx, es_cdr(lst), env));
  }
}

es_val_t es_macro_expand(es_ctx_t* ctx, es_val_t exp, es_val_t env)
{
  //es_printf(ctx, "macro-expand: %@\n", exp);
  if (!es_is_pair(exp)) {
    return exp;
  }
  es_val_t op = es_car(exp);
  if (es_is_symbol(op)) {
    es_val_t val = es_lookup_symbol(ctx, env, op);
    if (es_is_macro(val)) {
      es_val_t trans = es_macro_transformer(val);
      return es_macro_expand(ctx, es_apply(ctx, trans, es_cdr(exp)), env);
    } else {
      return es_macro_expand_list(ctx, exp, env);
    }
  } else {
    return es_macro_expand_list(ctx, exp, env);
  }
}

es_val_t es_eval(es_ctx_t* ctx, es_val_t exp)
{
  struct timeval t0, t1, dt;

  es_val_t env, res;

  env = es_ctx_env(ctx);

  exp = es_macro_expand(ctx, exp, env);

  //es_printf(ctx, "expanded: %@\n", exp);

  es_val_t proc = es_compile(ctx, exp);

  gettimeofday(&t0, NULL);

  res = (es_val_t)es_vm_run(ctx, ES_VM_DISPATCH, proc);

  assert(ctx->fp == 0);

  gettimeofday(&t1, NULL);
  timeval_subtract(&dt, &t1, &t0);
  printf("time: %f\n", dt.tv_sec * 1000.0 + dt.tv_usec / 1000.0);

  return res;
}

es_val_t es_apply(es_ctx_t* ctx, es_val_t proc, es_val_t args)
{
  es_val_t bc = ctx->bytecode;
  int argc = 0;
  while(!es_is_nil(args)) {
    push(ctx, es_car(args));
    args = es_cdr(args);
    argc++;
  }

  push(ctx, proc);

  int start = bytecode_label(bc);
  emit_call(bc, argc);
  emit_halt(bc);

  es_val_t thunk = es_make_proc(ctx, 0, 0, start, bytecode_label(bc));

  es_val_t res = (es_val_t)es_vm_run(ctx, ES_VM_DISPATCH, thunk);

  return res;
}

static es_val_t fn_bytecode(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return ctx->bytecode;
}

static es_val_t fn_env(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_ctx_env(ctx);
}

static es_val_t fn_write(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_port_write(ctx, argc == 1 ? ctx->oport : argv[1], argv[0]);
  return es_void;
}

static es_val_t fn_cons(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_cons(ctx, argv[0], argv[1]);
}

static es_val_t fn_car(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_car(argv[0]);
}

static es_val_t fn_cdr(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_cdr(argv[0]);
}

static es_val_t fn_add(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_number_add(ctx, argv[0], argv[1]);
}

static es_val_t fn_sub(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_number_sub(ctx, argv[0], argv[1]);
}

static es_val_t fn_mul(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_number_mul(ctx, argv[0], argv[1]);
}

static es_val_t fn_div(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_number_div(ctx, argv[0], argv[1]);
}

static es_val_t fn_is_num_eq(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_number_is_eq(argv[0], argv[1]));
}

static es_val_t fn_is_bool(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_bool(argv[0]));
}

static es_val_t fn_is_symbol(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_symbol(argv[0]));
}

static es_val_t fn_is_char(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_char(argv[0]));
}

static es_val_t fn_is_vec(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_vector(argv[0]));
}

static es_val_t fn_is_procedure(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_fn(argv[0])
    || es_is_closure(argv[0])
    || es_is_cont(argv[0]));
}

static es_val_t fn_is_pair(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_pair(argv[0]));
}

static es_val_t fn_is_number(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_fixnum(argv[0]));
}

static es_val_t fn_is_string(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_string(argv[0]));
}

static es_val_t fn_is_port(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_port(argv[0]));
}

static es_val_t fn_is_null(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_nil(argv[0]));
}

static es_val_t fn_is_eq(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_bool(es_is_eq(argv[0], argv[1]));
}

static es_val_t fn_quit(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_eof_obj;
}

static es_val_t fn_gc(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_gc(ctx);
  return es_void;
}

static es_val_t fn_read_char(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_port_read_char(argc == 0 ? ctx->iport : argv[0]);
}

static es_val_t fn_close(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_port_t* port = es_port_val(argv[0]);
  fclose(port->stream);
  port->stream = NULL;
  return es_void;
}

static es_val_t fn_compile(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_val_t b = es_make_bytecode(ctx);
  compile(ctx, b, argv[0], 0, 0, es_nil);
  emit_halt(b);
  return b;
}

static es_val_t fn_apply(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_val_t proc = argv[0];
  es_val_t args = argv[1];

  es_val_t b = ctx->bytecode;
  int start = bytecode_label(b);
  emit_pop(b);

  int nargs = 0;
  while (!es_is_nil(args)) {
    emit_const(b, alloc_const(b, es_car(args)));
    args = es_cdr(args);
    nargs++;
  }

  emit_const(b, alloc_const(b, proc));
  emit_tail_call(b, nargs);
  save(ctx);
  ctx->ip = es_bytecode_val(b)->inst + start;

  return es_void;
}

static es_val_t fn_eval(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  /*
  es_val_t b = ctx->bytecode;
  int start = bytecode_label(b);
  emit_pop(b);
  compile(ctx, b, argv[0], 1, RETURN, es_nil);
  save(ctx);
  ctx->ip = start;
  */
  return es_void;
}

static es_val_t fn_vec_ref(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_val_t vec = argv[0];
  int idx      = es_fixnum_val(argv[1]);
  return es_vector_ref(vec, idx);
}

static es_val_t fn_make_string(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_string_make(ctx, es_fixnum_val(argv[0]), argc == 1 ? 0 : es_char_val(argv[1]));
}

static es_val_t fn_string_ref(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_val_t str = argv[0];
  int k = es_fixnum_val(argv[1]);
  int c = es_string_ref(str, k);
  return es_make_char(c);
}

static es_val_t fn_mem_stats(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  size_t used = ctx->heap.next - ctx->heap.from_space;
  printf("heap size: %ld\n", ctx->heap.size);
  printf("allocated: %ld\n", used);

  return es_void;
}

static es_val_t fn_current_input_port(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_ctx_iport(ctx);
}

static es_val_t fn_current_output_port(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_ctx_oport(ctx);
}

static es_val_t fn_get_proc(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_closure_val(argv[0])->proc;
}

static es_val_t fn_call_cc(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  es_val_t proc = argv[0];
  es_val_t k    = es_make_cont(ctx);
  return k;
}

static es_val_t fn_load(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_void;
}

static es_val_t fn_make_macro(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_make_macro(ctx, argv[0]);
}

static es_val_t fn_macro_transformer(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_macro_transformer(argv[0]);
}

static es_val_t fn_gensym(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_gensym(ctx);
}

static es_val_t fn_macro_expand(es_ctx_t* ctx, int argc, es_val_t argv[])
{
  return es_macro_expand(ctx, argv[0], ctx->env);
}

static void ctx_init_env(es_ctx_t* ctx)
{
  ctx->env = es_make_env(ctx, ES_GLOBAL_ENV_SIZE);

  es_define_fn(ctx, "mem-stats",           fn_mem_stats,           0);
  es_define_fn(ctx, "bytecode",            fn_bytecode,            0);
  es_define_fn(ctx, "global-env",          fn_env,                 0);
  es_define_fn(ctx, "cons",                fn_cons,                2);
  es_define_fn(ctx, "car",                 fn_car,                 1);
  es_define_fn(ctx, "cdr",                 fn_cdr,                 1);
  es_define_fn(ctx, "+",                   fn_add,                 2);
  es_define_fn(ctx, "-",                   fn_sub,                 2);
  es_define_fn(ctx, "*",                   fn_mul,                 2);
  es_define_fn(ctx, "/",                   fn_div,                 2);
  es_define_fn(ctx, "call/cc",             fn_call_cc,             1);
  es_define_fn(ctx, "compile",             fn_compile,             1);
  es_define_fn(ctx, "boolean?",            fn_is_bool,             1);
  es_define_fn(ctx, "symbol?",             fn_is_symbol,           1);
  es_define_fn(ctx, "char?",               fn_is_char,             1);
  es_define_fn(ctx, "vector?",             fn_is_vec,              1);
  es_define_fn(ctx, "procedure?",          fn_is_procedure,        1);
  es_define_fn(ctx, "pair?",               fn_is_pair,             1);
  es_define_fn(ctx, "number?",             fn_is_number,           1);
  es_define_fn(ctx, "string?",             fn_is_string,           1);
  es_define_fn(ctx, "port?",               fn_is_port,             1);
  es_define_fn(ctx, "null?",               fn_is_null,             2);
  es_define_fn(ctx, "=",                   fn_is_num_eq,           2);
  es_define_fn(ctx, "eq?",                 fn_is_eq,               2);
  es_define_fn(ctx, "quit",                fn_quit,                2);
  es_define_fn(ctx, "gc",                  fn_gc,                  0);
  es_define_fn(ctx, "write",               fn_write,               2);
  es_define_fn(ctx, "read-char",           fn_read_char,           2);
  es_define_fn(ctx, "close",               fn_close,               1);
  es_define_fn(ctx, "eval",                fn_eval,                1);
  es_define_fn(ctx, "apply",               fn_apply,               2);
  es_define_fn(ctx, "vector-ref",          fn_vec_ref,             2);
  es_define_fn(ctx, "make-string",         fn_make_string,         2);
  es_define_fn(ctx, "string-ref",          fn_string_ref,          2);
  es_define_fn(ctx, "current-input-port",  fn_current_input_port,  0);
  es_define_fn(ctx, "current-output-port", fn_current_output_port, 0);
  es_define_fn(ctx, "get-proc",            fn_get_proc,            1);
  es_define_fn(ctx, "macro",               fn_make_macro,          1);
  es_define_fn(ctx, "macro-transformer",   fn_macro_transformer,   1);
  es_define_fn(ctx, "gensym",              fn_gensym,              0);
  es_define_fn(ctx, "macro-expand",        fn_macro_expand,        1);

  es_load(ctx, "eva.scm");
}

#ifdef ENABLE_REPL

enum { MB = 1000000 };

int main()
{
  es_ctx_t* ctx = es_ctx_new(64 * MB);

  es_ctx_set_iport(ctx, es_make_port(ctx, stdin));
  es_ctx_set_oport(ctx, es_make_port(ctx, stdout));

  es_printf(ctx, ".--------------.\n");
  es_printf(ctx, "|  Eva v%s  |\n", ES_VERSION_STR);
  es_printf(ctx, "'--------------'\n\n");

  es_val_t val = es_undefined;

  es_gc(ctx);

  do {
    es_printf(ctx, "eva> ");
    val = es_read(ctx);
    val = es_eval(ctx, val);
    es_printf(ctx, "%@\n", val);
    es_gc(ctx);
  } while (!es_is_eof_obj(val));

  es_ctx_free(ctx);

  return 0;
}

#endif