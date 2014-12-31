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

#define ES_API

typedef enum   es_tag          es_tag_t;
typedef struct es_heap         es_heap_t;
typedef struct es_roots        es_roots_t;
typedef struct es_frame        es_frame_t;
typedef struct es_symtab       es_symtab_t;
typedef enum   es_opcode       es_opcode_t;
typedef struct es_bytecode     es_bytecode_t;
typedef struct es_state        es_state_t;
typedef struct stream          stream_t;
typedef struct es_inst         es_inst_t;
typedef struct es_opcode_info  es_opcode_info_t;

enum es_tag {
  es_obj_tag     = 0x0, /**< b000 - Heap allocated object */
  es_fixnum_tag  = 0x1, /**< b001 - Fixnums */
  es_symbol_tag  = 0x2, /**< b010 - Symbols tag */
  es_value_tag   = 0x3, /**< b011 - Immediate values: Nil, EOF, Unspecified, Unbound */
  es_boolean_tag = 0x4, /**< b100 - Symbol tag */
  es_char_tag    = 0x5, /**< b101 - Character tag */
  es_max_tag
};

enum { es_tag_bits = 3 };
enum { es_tag_mask = (1 << es_tag_bits) - 1 };
enum { es_val_payload_mask = ~(uintptr_t)es_tag_mask };
enum { es_val_payload_bits = sizeof(uintptr_t) * 8 - es_tag_bits };
enum { es_default_alignment = 16 };
enum { es_default_heap_size = 256 * 1000000 };
enum { es_symtab_size = 65536 };

enum { 
  es_fixnum_min = -1 << es_val_payload_bits, 
  es_fixnum_max = 1 << es_val_payload_bits 
};

/* VM opcodes */
enum es_opcode { HALT, CONST, POP, GLOBAL_REF, GLOBAL_SET, CLOSED_REF, 
  CLOSED_SET, ARG_REF, ARG_SET, JMP, BF, CALL, TAIL_CALL, RETURN, CLOSURE };

struct es_opcode_info {
  es_opcode_t opcode;
  const char* name;
  int         arity;
};

static es_opcode_info_t opcode_info[] = {
  { HALT,       "halt",       0 },
  { CONST,      "const",      1 },
  { POP,        "pop",        0 },  
  { GLOBAL_REF, "global-ref", 1 },
  { GLOBAL_SET, "global-set", 1 },
  { CLOSED_REF, "closed-ref", 2 },
  { CLOSED_SET, "closed-set", 2 },
  { ARG_REF,    "arg-ref",    1 },
  { ARG_SET,    "arg-set",    1 },
  { JMP,        "jmp",        1 },
  { BF,         "bf",         1 },
  { CALL,       "call",       1 },
  { TAIL_CALL,  "tail-call",  1 },
  { RETURN,     "return",     0 },
  { CLOSURE,    "closure",    1 }
};

#ifdef LABELS_AS_VALUES
  typedef void* es_op_t;
  static void** inst_addrs;
  #define get_op(opcode) inst_addrs[opcode]
#else
  typedef char es_op_t;
  #define get_op(opcode) (char)op
#endif

struct es_inst {
  es_op_t op;
  short   op1;
  short   op2;
};

#define inst_op(inst)  inst->op
#define inst_op1(inst) inst->op1
#define inst_op2(inst) inst->op2

struct es_obj {
  es_type_t type;    /**< The type of this object */
  es_obj_t* reloc;   /**< Address of relocated object in to-space */
};

struct es_heap {
  char*  buffer;     /**< Heap pointer */
  size_t size;       /**< Size of heap in bytes */
  char*  next;       /**< Pointer to_space next free memory */
  char*  from_space; /**< From space pointer */
  char*  to_space;   /**< To space pointer */
  char*  end;        /**< End of from space */
  size_t requested;  /**< Requested heap size in bytes */
};

struct es_symtab {
  char* table[es_symtab_size];
  int   next_id;
};

struct es_frame {
  es_val_t   args;
  es_inst_t* knt;
};

struct es_state {
  es_val_t   env;
  es_val_t   args;
  int        sp;
  es_val_t   stack[256];
  int        fp;
  es_frame_t frames[256];
};

struct es_roots {
  es_val_t* stack[1024];
  int       top;
};

struct es_ctx {
  es_heap_t   heap;
  es_roots_t  roots;
  es_symtab_t symtab;
  es_val_t    iport;
  es_val_t    oport;
  es_val_t    env;
  es_val_t    bytecode;
  es_state_t  state;
};

struct es_string {
  es_obj_t base;
  size_t   length;
  char     value[];
};

typedef struct es_slot {
  es_val_t val;
  es_val_t sym;
} es_slot_t;

struct es_env {
  es_obj_t  base;
  int       count;
  int       size;
  es_slot_t slots[65536];
};

struct es_args {
  es_obj_t   base;
  es_val_t   parent;
  int        size;
  es_val_t   args[];
};

struct es_pair {
  es_obj_t base;
  es_val_t head;
  es_val_t tail;
};

struct es_vec {
  es_obj_t base;
  size_t   length;
  es_val_t array[];
};

struct es_closure {
  es_obj_t   base;
  es_val_t   env;
  es_val_t   proc;
};

struct es_port {
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
      int cur;
      int nbytes;
      int linum;
      int colnum;
    } state[256];
  };
};

struct es_error {
  es_obj_t base;
  char*    errstr;
};

struct es_fn {
  es_obj_t  base;
  int       arity;
  es_pfn_t  pfn;
};

struct es_proc {
  es_obj_t base;
  int      arity;
  int      rest;
  int      addr;
};

struct es_bytecode {
  es_obj_t   base;
  es_val_t   cpool[256];
  int        cpool_size;
  int        cpi;
  es_inst_t* inst;
  int        inst_size;
  int        ip;
};

typedef struct state {
  int mark;
  int nbytes;
  int linum;
  int colnum;
} state_t;

struct stream {
  FILE*   handle; /* stream */
  char*   srec;   /* stream record */
  size_t  len;    /* length of stream record */
  int     mark;   /* true if stream record active */
  int     tail;   /* last recorded char in stream record */
  int     cur;    /* stream record cursor */
  int     eof;    /* eof flag */
  int     error;
  int     stack[256];
  int     sp;
  int     nbytes;
  int     linum;
  int     colnum;
  state_t sss[256];
};

#define es_tagged_val(v, tag) ((es_val_t){ .val = (((v) << es_tag_bits) | tag) })
#define es_tagged_obj(o)      ((es_val_t){ .obj = (es_obj_t*)(o) })
#define es_tag(v)             ((v).val & es_tag_mask)
#define es_payload(t, v)      (((t)((v).val)) >> es_tag_bits)
#define es_obj_to(t, v)       ((t)(v).obj)
#define es_obj_to_val(o)      (es_val_t*)(o)

#define gc_root(ctx, v)           ctx->roots.stack[ctx->roots.top++] = &v
#define gc_root2(ctx, a, b)       gc_root(ctx, a); gc_root(ctx, b)
#define gc_root3(ctx, a, b, c)    gc_root(ctx, a); gc_root2(ctx, b, c)
#define gc_root4(ctx, a, b, c, d) gc_root(ctx, a); gc_root3(ctx, b, c, d)
#define gc_unroot(ctx, n)         ctx->roots.top -= n

static void      es_ctx_init(es_ctx_t* ctx, size_t heap_size);
static void      ctx_init_env(es_ctx_t* ctx);
static es_type_t es_obj_type_of(es_val_t val);
static es_obj_t* es_obj_reloc(es_val_t obj);
static void      es_obj_init(es_val_t obj, enum es_type type);
static void      es_mark_copy(es_heap_t* heap, es_val_t* pval, char** next);
static int       es_obj_is_reloc(es_val_t val);
static void*     es_align(void* bc, int align) { return (void*)((((uintptr_t)bc / align) + 1) * align); }
static void*     es_heap_alloc(es_heap_t* heap, size_t size);
static int       es_ceil_to(int n, int u);
static void      es_heap_init(es_heap_t* heap, size_t size);
static void      es_symtab_init(es_symtab_t* symtab);
static char*     es_symtab_find_by_id(es_symtab_t* symtab, int id);
static int       es_symtab_id_by_string(es_symtab_t* symtab, char* cstr);
static int       es_symtab_find_or_create(es_symtab_t* symtab, char* cstr);
static int       es_symtab_add_string(es_symtab_t* symtab, char* cstr);
static size_t    es_vec_size_of(es_val_t vecval);
static es_val_t  lookup_binding(es_val_t _env, es_val_t symbol);
static stream_t* stream_open(FILE* handle, char* buf, size_t len);
static void      stream_mark(stream_t* s);
static void      stream_resume(stream_t* s);
static int       stream_stack(stream_t* s);
static void      stream_reset(stream_t* s);
static int       stream_getc(stream_t* s);
static char*     stream_record(stream_t* s);
static int       stream_reclen(stream_t* s);
static int       stream_copybuf(stream_t*s, char* buf, size_t len);
static int       stream_peekc(stream_t* s);
static void      stream_free(stream_t* s);
static int       stream_linum(stream_t* s);
static int       stream_colnum(stream_t* s);
static int       matchc(es_val_t port, int c);
static int       matchstr(es_val_t port, char* cstr);
static int       acceptc(es_val_t port, int c);
static int       acceptfn(es_val_t port, int (*fn)(int));
static int       timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);
static es_val_t  es_parse(es_ctx_t* ctx, es_val_t port);
static es_val_t  parse_exp(es_ctx_t* ctx, es_val_t port, int depth, int qq);
static es_val_t  parse_list(es_ctx_t* ctx, es_val_t port, int depth, int dot, int qq);
static es_val_t  parse_vector(es_ctx_t* ctx, es_val_t port, int depth, int qq);
static es_val_t  parse_atom(es_ctx_t* ctx, es_val_t port, int depth);
static es_val_t  parse_hashform(es_ctx_t* ctx, es_val_t port, int depth, int qq);
static es_val_t  parse_string(es_ctx_t* ctx, es_val_t port, int depth);
static es_val_t  parse_symnum(es_ctx_t* ctx, es_val_t port, int depth);
static es_val_t  parse_char(es_val_t port, int depth);
static void      accept_space(es_val_t port);
static int       accept_whitespace(es_val_t port);
static int       accept_comment(es_val_t port);
static void      es_fixnum_print(es_ctx_t* ctx, es_val_t fixnum, es_val_t oport);
static void      es_char_print(es_ctx_t* ctx, es_val_t clo, es_val_t oport);
static void      es_string_print(es_ctx_t* ctx, es_val_t self, es_val_t oport);
static void      es_pair_print(es_ctx_t* ctx, es_val_t val, es_val_t oport);
static void      es_vec_print(es_ctx_t* ctx, es_val_t self, es_val_t oport);
static void      es_error_print(es_ctx_t* ctx, es_val_t val, es_val_t oport);
static void      es_symbol_print(es_ctx_t* ctx, es_val_t sym, es_val_t oport);
static void      es_bytecode_print(es_ctx_t* ctx, es_val_t val, es_val_t oport);
static size_t    es_string_size_of(es_val_t value);
static size_t    es_args_size_of(es_val_t value);
static void      es_proc_print(es_ctx_t* ctx, es_val_t val, es_val_t port);
static void*     es_alloc(es_ctx_t* ctx, es_type_t type, size_t size);
static void      es_vec_mark_copy(es_heap_t* heap, es_val_t pval, char** next);
static void      es_closure_mark_copy(es_heap_t* heap, es_val_t pval, char** next);
static void      es_pair_mark_copy(es_heap_t* heap, es_val_t pval, char** next);
static void      es_env_mark_copy(es_heap_t* heap, es_val_t pval, char** next);
static void      es_args_mark_copy(es_heap_t* heap, es_val_t pval, char** next);
static void      es_bytecode_mark_copy(es_heap_t* heap, es_val_t pval, char** next);
static es_val_t  compile(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_form(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_call(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_args(es_ctx_t* ctx, es_val_t bc, es_val_t exp, es_val_t scope);
static es_val_t  compile_lambda(es_ctx_t* ctx, es_val_t bc, es_val_t formals, es_val_t body, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_if(es_ctx_t* ctx, es_val_t bc, es_val_t cond, es_val_t bthen, es_val_t belse, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_seq(es_ctx_t* ctx, es_val_t bc, es_val_t seq, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_define(es_ctx_t* ctx, es_val_t bc, es_val_t binding, es_val_t val, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_set(es_ctx_t* ctx, es_val_t bc, es_val_t sym, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_ref(es_ctx_t* ctx, es_val_t bc, es_val_t sym, int tail_pos, int next, es_val_t scope);
static es_val_t  compile_const(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope);
static es_val_t  es_vm(es_ctx_t* ctx, int start, es_val_t env);

const es_val_t es_nil       = es_tagged_val(es_nil_type, es_value_tag);
const es_val_t es_true      = es_tagged_val(1, es_boolean_tag);
const es_val_t es_false     = es_tagged_val(0, es_boolean_tag);
const es_val_t es_eof_obj   = es_tagged_val(es_eof_obj_type, es_value_tag);
const es_val_t es_void      = es_tagged_val(es_void_type, es_value_tag);
const es_val_t es_unbound   = es_tagged_val(es_unbound_type, es_value_tag);
const es_val_t es_defined   = es_tagged_val(es_defined_type, es_value_tag);
const es_val_t es_undefined = es_tagged_val(es_undefined_type, es_value_tag);

static const es_val_t symbol_define     = es_tagged_val(0, es_symbol_tag);
static const es_val_t symbol_if         = es_tagged_val(1, es_symbol_tag);
static const es_val_t symbol_begin      = es_tagged_val(2, es_symbol_tag);
static const es_val_t symbol_set        = es_tagged_val(3, es_symbol_tag);
static const es_val_t symbol_lambda     = es_tagged_val(4, es_symbol_tag);
static const es_val_t symbol_quote      = es_tagged_val(5, es_symbol_tag);
static const es_val_t symbol_quasiquote = es_tagged_val(6, es_symbol_tag);

//=================
// Context
//=================
ES_API es_ctx_t* es_ctx_new(size_t heap_size) {
  es_ctx_t* ctx = malloc(sizeof(es_ctx_t));
  es_vm(ctx, -1, es_nil);
  es_ctx_init(ctx, heap_size);
  return ctx;
}

static void es_ctx_init(es_ctx_t* ctx, size_t heap_size) {
  es_heap_init(&ctx->heap, heap_size);
  es_ctx_set_env(ctx, es_env_new(ctx, 65536));
  ctx->bytecode = es_bytecode_new(ctx);
  es_symtab_init(&ctx->symtab);
  es_symbol_intern(ctx, "define");
  es_symbol_intern(ctx, "if");
  es_symbol_intern(ctx, "begin");
  es_symbol_intern(ctx, "set!");
  es_symbol_intern(ctx, "lambda");
  es_symbol_intern(ctx, "quote");
  es_symbol_intern(ctx, "quasiquote");

  ctx_init_env(ctx);
}

ES_API void es_ctx_free(es_ctx_t* ctx)  { 
  free(ctx); 
}

ES_API es_val_t es_ctx_iport(es_ctx_t* ctx) { 
  return ctx->iport; 
}

ES_API es_val_t es_ctx_oport(es_ctx_t* ctx) { 
  return ctx->oport; 
}

ES_API void es_ctx_set_iport(es_ctx_t* ctx, es_val_t port) { 
  ctx->iport = port; 
}

ES_API void es_ctx_set_oport(es_ctx_t* ctx, es_val_t port) { 
  ctx->oport = port; 
}

ES_API es_val_t es_ctx_env(es_ctx_t* ctx) { 
  return ctx->env; 
}

ES_API void es_ctx_set_env(es_ctx_t* ctx, es_val_t env) { 
  ctx->env = env; 
}

//=================
// Allocator/GC
//=================
static void* es_alloc(es_ctx_t* ctx, es_type_t type, size_t size) {
  void* mem;
  while(!(mem = es_heap_alloc(&ctx->heap, size))) { 
    //printf("out of memory\n");
    es_gc(ctx); 
  }
  es_obj_init(es_tagged_obj(mem), type);
  return mem;
}

ES_API void es_gc_root_p(es_ctx_t* ctx, es_val_t *pv) {
  gc_root(ctx, *pv);
}

ES_API void es_gc_unroot(es_ctx_t* ctx, int n) {
  gc_unroot(ctx, n);
}

ES_API void es_gc(es_ctx_t* ctx) {
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

  es_mark_copy(heap, &ctx->state.env, &next);
  for(int i = 0; i < ctx->state.sp; i++) {
    es_mark_copy(heap, &ctx->state.stack[i], &next);
  }
  for(int i = 1; i < ctx->state.fp; i++) {
    es_mark_copy(heap, &ctx->state.frames[i].args, &next);
  }

  while(scan < next) {
    es_val_t obj = es_tagged_obj(scan);
    switch(es_type_of(obj)) {
    case es_pair_type:      es_pair_mark_copy(heap, obj, &next);     break;
    case es_closure_type:   es_closure_mark_copy(heap, obj, &next);  break;
    case es_vector_type:    es_vec_mark_copy(heap, obj, &next);      break;
    case es_env_type:       es_env_mark_copy(heap, obj, &next);      break;
    case es_args_type:      es_args_mark_copy(heap, obj, &next);     break;
    case es_bytecode_type:  es_bytecode_mark_copy(heap, obj, &next); break;
    default:                                                         break;
    } 
    scan = es_align(scan + es_size_of(obj), es_default_alignment);
  }

  tmp              = heap->from_space;
  heap->from_space = heap->to_space;
  heap->end        = heap->from_space + heap->size;
  heap->to_space   = tmp;
  heap->next       = next;

  //gettimeofday(&t1, NULL);
  //timeval_subtract(&dt, &t1, &t0);
  //printf("gc time: %f\n", dt.tv_sec * 1000.0 + dt.tv_usec / 1000.0);
}

static int ceil_to(int n, int u) { 
  return ((n + u - 1) / u) * u; 
}

static void* es_heap_alloc(es_heap_t* heap, size_t size) {
  void* mem;
  heap->next = es_align(heap->next, es_default_alignment);
  if (size > (heap->end - heap->next)) {
    return NULL;
  }
  mem = heap->next;
  heap->next += size;
  return mem;
}

static void es_heap_init(es_heap_t* heap, size_t size) {
  heap->requested  = size;
  heap->size       = ceil_to((size + 1) / 2, es_default_alignment);
  heap->buffer     = malloc(heap->size * 2);
  heap->from_space = es_align(heap->buffer, es_default_alignment);
  heap->to_space   = es_align(heap->from_space + heap->size, es_default_alignment);
  heap->next       = heap->from_space;
  heap->end        = heap->from_space + size;
}

ES_API void es_mark_copy(es_heap_t* heap, es_val_t* ref, char** next) {
  if (!es_is_obj(*ref))
    return;

  if (es_obj_is_reloc(*ref)) {
    ref->obj = ref->obj->reloc; // Update stale reference to relocated object
  } else {
    size_t size  = es_size_of(*ref);               // Get size of object
    *next = es_align(*next, es_default_alignment); // Ensure next pointer is aligned
    memcpy(*next, ref->obj, size);                 // Copy object from_space from_space-space into to_space-space
    ref->obj->reloc = (es_obj_t*)*next;            // Leave forwarding pointer in old from_space-space object
    ref->obj = ref->obj->reloc;                    // Update current reference to_space point to_space new object in to_space-space
    ref->obj->reloc = NULL;                        // Reset tombstone
    *next += size;                                 // Update next pointer
  }
}

//=================
// Values
//=================
ES_API es_type_t es_type_of(es_val_t val) {
  switch(es_tag(val)) {
  case es_obj_tag:          return es_obj_type_of(val);
  case es_value_tag:        return es_payload(es_type_t, val);
  case es_boolean_tag:      return es_boolean_type;
  case es_fixnum_tag:       return es_fixnum_type;
  case es_symbol_tag:       return es_symbol_type;
  case es_char_tag:         return es_char_type;
  default:                  return es_invalid_type;
  }
}

ES_API void es_print(es_ctx_t* ctx, es_val_t val, es_val_t oport) {
  switch(es_type_of(val)) {
  case es_nil_type:          es_port_printf(ctx, oport,"()");          break;
  case es_boolean_type:      es_port_printf(ctx, oport, "#%c", es_is_eq(val, es_false) ? 'f' : 't'); break;
  case es_fixnum_type:       es_fixnum_print(ctx, val, oport);             break; 
  case es_symbol_type:       es_symbol_print(ctx, val, oport);             break;
  case es_char_type:         es_char_print(ctx, val, oport);               break;
  case es_string_type:       es_string_print(ctx, val, oport);             break;
  case es_pair_type:         es_pair_print(ctx, val, oport);               break;
  case es_eof_obj_type:      es_port_printf(ctx, oport, "#<eof-obj>");     break;
  case es_closure_type:      es_port_printf(ctx, oport,"#<closure>");      break;
  case es_fn_type:           es_port_printf(ctx, oport, "#<fn>");          break;
  case es_unbound_type:      es_port_printf(ctx, oport,"#<unbound>");      break;
  case es_void_type:         es_port_printf(ctx, oport,"#<void>");  break;
  case es_port_type:         es_port_printf(ctx, oport,"#<port>");         break;
  case es_vector_type:       es_vec_print(ctx, val, oport);                break;
  case es_error_type:        es_error_print(ctx, val, oport);              break;
  case es_bytecode_type:     es_bytecode_print(ctx, val, oport);           break;
  case es_proc_type:         es_proc_print(ctx, val, oport);               break;
  //case es_env_type:          es_print(ctx, es_to_env(val)->bindings, oport); break;
  case es_invalid_type:
  default:
    break;
  }
}

ES_API size_t es_size_of(es_val_t val) { 
  switch(es_type_of(val)) {
  case es_string_type:       return es_string_size_of(val);
  case es_pair_type:         return sizeof(es_pair_t);
  case es_closure_type:      return sizeof(es_closure_t);
  case es_port_type:         return sizeof(es_port_t);
  case es_vector_type:       return es_vec_size_of(val);
  case es_fn_type:           return sizeof(es_fn_t);
  case es_bytecode_type:     return sizeof(es_bytecode_t);
  case es_error_type:        return sizeof(es_error_t);
  case es_env_type:          return sizeof(es_env_t);
  case es_proc_type:         return sizeof(es_proc_t);
  case es_args_type:         return es_args_size_of(val);
  case es_invalid_type:      return -1;
  default:                   return sizeof(es_val_t);
  }
}

ES_API int es_is_eq(es_val_t v1, es_val_t v2) { 
  return v1.obj == v2.obj; 
}

//=================
// Booleans
//=================
ES_API es_val_t es_boolean_new(int value) { 
  return es_tagged_val(value == 0 ? 0 : 1, es_boolean_tag); 
}

ES_API int es_is_boolean(es_val_t val) { 
  return es_boolean_type == es_type_of(val); 
}

ES_API int es_is_true(es_val_t val) { 
  return es_to_boolean(val) != 0; 
}

ES_API int es_to_boolean(es_val_t val) { 
  return es_payload(int, val); 
}

//=================
// Characters
//=================
ES_API es_val_t es_char_new(int charcode) { 
  return es_tagged_val(charcode, es_char_tag); 
}

ES_API int es_to_char(es_val_t val) { 
  return es_payload(int, val); 
}

ES_API es_val_t es_char_str(char* buf) {
  if (strcmp("space", buf) == 0) {
    return es_char_new(' ');
  } else if (strcmp("newline", buf) == 0) {
    return es_char_new('\n');
  } else if (strcmp("tab", buf) == 0) {
    return es_char_new('\t');
  } else {
    return es_char_new(*buf);
  }
}

ES_API int es_is_char(es_val_t val) { 
  return es_char_type == es_type_of(val); 
}

static void es_char_print(es_ctx_t* ctx, es_val_t val, es_val_t port) {
  int c = es_to_char(val);
  switch(c) {
    case ' ':  es_port_printf(ctx, port, "#\\space");   break;
    case '\n': es_port_printf(ctx, port, "#\\newline"); break;
    case '\t': es_port_printf(ctx, port, "#\\tab");     break;
    default:   es_port_printf(ctx, port, "#\\%c", c);   break;
  }
}

//=================
// Object
//=================
ES_API int es_is_obj(es_val_t val) { 
  return es_obj_tag == es_tag(val) && val.obj; 
}

es_obj_t* es_to_obj(es_val_t val) { 
  return es_obj_to(es_obj_t*, val); 
}

static es_type_t es_obj_type_of(es_val_t val) { 
  return es_to_obj(val)->type; 
}

static es_obj_t* es_obj_reloc(es_val_t obj) { 
  return es_to_obj(obj)->reloc; 
}

static void es_obj_init(es_val_t self, es_type_t type) {
  es_obj_t* obj = es_to_obj(self);
  obj->type  = type;
  obj->reloc = NULL;
}

static int es_obj_is_reloc(es_val_t val) {
  return es_obj_reloc(val) != NULL;
}

//=================
// Errors
//=================
ES_API es_val_t es_error_new(es_ctx_t* ctx, char* errstr) {
  es_error_t* error = es_alloc(ctx, es_error_type, sizeof(es_error_t));
  error->errstr = errstr;
  return es_tagged_obj(error);
}

ES_API int es_is_error(es_val_t val) { 
  return es_error_type == es_type_of(val); 
}

es_error_t* es_to_error(es_val_t val) { 
  return es_obj_to(es_error_t*, val); 
}

static void es_error_print(es_ctx_t* ctx, es_val_t val, es_val_t port) {
  es_port_printf(ctx, port, "#<error: %s>", es_to_error(val)->errstr);
}

//=================
// Numerics
//=================
ES_API es_val_t es_fixnum_new(long value) { 
  return es_tagged_val(value, es_fixnum_tag); 
}

ES_API es_val_t es_fixnum_new_cstr(char* val) { 
  return es_fixnum_new(atoi(val)); 
}

ES_API es_val_t es_fixnum_new_cbuf(char* buf, size_t bytes) { 
  return es_fixnum_new(atoi(buf)); 
}

ES_API int es_is_fixnum(es_val_t val) { 
  return es_fixnum_type == es_type_of(val); 
}

ES_API long es_to_fixnum(es_val_t fixnum) { 
  return es_payload(long, fixnum); 
}

ES_API es_val_t es_number_add(es_val_t v1, es_val_t v2) {
  long a, b;
  a = es_to_fixnum(v1);
  b = es_to_fixnum(v2);
  return es_fixnum_new(a + b);
}

ES_API es_val_t es_number_sub(es_val_t v1, es_val_t v2) {
  long a, b;
  a = es_to_fixnum(v1);
  b = es_to_fixnum(v2);
  return es_fixnum_new(a - b);
}

ES_API es_val_t es_number_mul(es_val_t v1, es_val_t v2) {
  long a, b;
  a = es_to_fixnum(v1);
  b = es_to_fixnum(v2);
  return es_fixnum_new(a * b);
}

ES_API es_val_t es_number_div(es_val_t v1, es_val_t v2) {
  long a, b;
  a = es_to_fixnum(v1);
  b = es_to_fixnum(v2);
  return es_fixnum_new(a / b);
}

ES_API int es_number_is_eq(es_val_t a, es_val_t b) { 
  return es_is_eq(a, b); 
}

static void es_fixnum_print(es_ctx_t* ctx, es_val_t val, es_val_t port) {
  es_port_printf(ctx, port, "%ld", es_to_fixnum(val));
}

//=================
// Pairs
//=================
ES_API es_val_t es_pair_new(es_ctx_t* ctx, es_val_t head, es_val_t tail) {
  es_pair_t* pair;
  gc_root2(ctx, head, tail);
  pair = es_alloc(ctx, es_pair_type, sizeof(es_pair_t));
  pair->head = head;
  pair->tail = tail;
  gc_unroot(ctx, 2);
  return es_tagged_obj(pair);
}

ES_API int es_is_pair(es_val_t val) { 
  return es_pair_type == es_type_of(val); 
}

ES_API es_pair_t* es_to_pair(es_val_t val) { 
  return es_obj_to(es_pair_t*, val); 
}

ES_API es_val_t es_pair_car(es_val_t pair)    { 
  return es_to_pair(pair)->head; 
}

ES_API es_val_t es_pair_cdr(es_val_t pair) { 
  return es_to_pair(pair)->tail; 
}

ES_API void es_pair_set_head(es_val_t pair, es_val_t value) { 
  es_to_pair(pair)->head = value; 
}

ES_API void es_pair_set_tail(es_val_t pair, es_val_t value) { 
  es_to_pair(pair)->tail = value; 
}

static void es_pair_mark_copy(es_heap_t* heap, es_val_t pval, char** next) {
  es_pair_t* pair = es_to_pair(pval);
  es_mark_copy(heap, &pair->head, next);
  es_mark_copy(heap, &pair->tail, next);
}

static void es_pair_print(es_ctx_t* ctx, es_val_t val, es_val_t port) {
  es_val_t list = val;
  es_port_printf(ctx, port, "(");
  while(es_is_pair(es_cdr(list))) {
    es_print(ctx, es_car(list), port);
    es_port_printf(ctx, port, " ");
    list = es_cdr(list);
  }
  es_print(ctx, es_car(list), port);
  if (!es_is_nil(es_cdr(list))) {
    es_port_printf(ctx, port, " . ");
    es_print(ctx, es_cdr(list), port);
  }
  es_port_printf(ctx, port, ")");
}

//=================
// Ports
//=================
ES_API es_val_t es_port_new(es_ctx_t* ctx, FILE* stream) {
  es_port_t* port;
  port = es_alloc(ctx, es_port_type, sizeof(es_port_t));
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
  return es_tagged_obj(port);
}

ES_API int es_is_port(es_val_t val) { 
  return es_port_type == es_type_of(val); 
}

ES_API es_port_t* es_to_port(es_val_t val) { 
  return es_obj_to(es_port_t*, val); 
}

ES_API es_val_t es_port_printf(es_ctx_t* ctx, es_val_t port, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(es_to_port(port)->stream, fmt, args);
  va_end(args);
  return es_void;
}

ES_API es_val_t es_read(es_ctx_t* ctx, es_val_t port) {
  return es_port_read(ctx, port);
}

ES_API es_val_t es_port_read(es_ctx_t* ctx, es_val_t port) {
  return es_parse(ctx, port);
}

ES_API int es_port_getc(es_val_t port) {
  es_port_t* p = es_to_port(port);
  if (p->cur < p->tail) {
    return p->buf[p->cur++];
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
    }
    return c;
  }
}

ES_API int es_port_peekc(es_val_t port) {
  es_port_t* p = es_to_port(port);
  if (p->cur < p->tail) {
    return p->buf[p->cur];
  } else {
    es_port_mark(port);
    int c = es_port_getc(port);
    es_port_reset(port);
    return c;  
  }
}

ES_API void es_port_mark(es_val_t port) {
  es_port_t* p = es_to_port(port);
  //assert(!p->mark);
  p->state[p->top].cur    = p->cur;
  p->state[p->top].nbytes = p->nbytes;
  p->state[p->top].linum  = p->linum;
  p->state[p->top].colnum = p->colnum;
  p->top++;
}

ES_API void es_port_reset(es_val_t port) {
  es_port_t* p = es_to_port(port);
  assert(p->top > 0);
  --p->top;
  p->cur    = p->state[p->top].cur;
  p->nbytes = p->state[p->top].nbytes;
  p->linum  = p->state[p->top].linum;
  p->colnum = p->state[p->top].colnum;
}

ES_API void es_port_resume(es_val_t port) {
  es_port_t* p = es_to_port(port);
}

ES_API es_val_t es_port_read_char(es_val_t port) {
  int c = es_port_getc(port);
  return c == EOF ? es_eof_obj : es_char_new(c);
}

ES_API es_val_t es_port_peek_char(es_val_t port) {
  int c = es_port_peekc(port);
  return c == EOF ? es_eof_obj : es_char_new(c);
}

ES_API void es_port_close(es_val_t iport) {
  es_port_t* port = es_to_port(iport);
  fclose(port->stream);
  port->stream = NULL;
}

ES_API int es_port_linum(es_val_t port) {
  return es_to_port(port)->linum;
}

ES_API int es_port_colnum(es_val_t port) {
  return es_to_port(port)->colnum;
}

ES_API es_val_t es_port_write_char(es_val_t oport, es_val_t c) {
  fputc(es_to_char(c), es_to_port(oport)->stream);
  return es_void;
}

ES_API es_val_t es_port_write(es_ctx_t* ctx, es_val_t oport, es_val_t obj) {
  //es_print(ctx, es_to_output_port(oport)->stream, obj);
  return es_void;
}

//=================
// Functions
//=================
ES_API es_val_t es_fn_new(es_ctx_t* ctx, int arity, es_pfn_t pfn) {
  es_fn_t* fn = es_alloc(ctx, es_fn_type, sizeof(es_fn_t));
  fn->arity   = arity;
  fn->pfn     = pfn;
  return es_tagged_obj(fn);
}

ES_API int es_is_fn(es_val_t val) { 
  return es_fn_type == es_type_of(val); 
}

ES_API es_fn_t* es_to_fn(es_val_t val) { 
  return es_obj_to(es_fn_t*, val); 
}

static es_val_t es_fn_apply_argv(es_ctx_t* ctx, es_val_t fn, int argc, es_val_t* argv) {
  return es_to_fn(fn)->pfn(ctx, argc, argv);
}

//=================
// Procedures
//=================
es_val_t es_proc_new(es_ctx_t* ctx, int arity, int rest, int addr) {
  es_proc_t* proc = es_alloc(ctx, es_proc_type, sizeof(es_proc_t));
  proc->arity     = arity;
  proc->rest      = rest;
  proc->addr      = addr;
  return es_tagged_obj(proc);
}

ES_API es_proc_t* es_to_proc(es_val_t val) { 
  return es_obj_to(es_proc_t*, val); 
}

void es_proc_print(es_ctx_t* ctx, es_val_t val, es_val_t port) {
  es_port_printf(ctx, port,"#<compiled-proc %bc>", es_to_proc(val)->addr);
}

int es_proc_addr(es_val_t proc) { 
  return es_to_proc(proc)->addr; 
}

int es_proc_arity(es_val_t proc) { 
  return es_to_proc(proc)->arity; 
}

//=================
// Strings
//=================
ES_API es_val_t es_string_new(es_ctx_t* ctx, char* value) {
  es_string_t* string;
  size_t       len;
  len    = strlen(value);
  string = es_alloc(ctx, es_string_type, sizeof(es_string_t) + len + 1);
  string->length = len;
  strcpy(string->value, value);
  return es_tagged_obj(string);
}

ES_API int es_is_string(es_val_t val) { 
  return es_string_type == es_type_of(val); 
}

ES_API es_string_t* es_to_string(es_val_t val) { 
  return es_obj_to(es_string_t*, val); 
}

ES_API es_val_t es_string_to_symbol(es_ctx_t* ctx, es_val_t val) {
  return es_unbound;
}

static size_t es_string_size_of(es_val_t value) {
  es_string_t* string = es_to_string(value);
  return sizeof(es_string_t) + string->length + 1;
}

static void es_string_print(es_ctx_t* ctx, es_val_t self, es_val_t port) {
  es_port_printf(ctx, port,"\"%s\"", es_to_string(self)->value);
}

//=================
// Symbols
//=================
ES_API es_val_t es_symbol_new(int value) { 
  return es_tagged_val(value, es_symbol_tag); 
}

ES_API int es_is_symbol(es_val_t val) { 
  return es_symbol_type == es_type_of(val); 
}

ES_API int es_to_symbol(es_val_t val) { 
  return es_payload(int, val); 
}

ES_API es_val_t es_symbol_intern(es_ctx_t* ctx, char* cstr) {
  es_symtab_t* symtab = &ctx->symtab;
  int id = es_symtab_find_or_create(symtab, cstr);
  if (id < 0) {
    return es_error_new(ctx, "symtab count exceeded");
  }
  return es_symbol_new(id);
}

ES_API es_val_t es_symbol_to_string(es_ctx_t* ctx, es_val_t val) {
  es_symtab_t* symtab = &ctx->symtab;
  int          symval = es_to_symbol(val);
  return es_string_new(ctx, es_symtab_find_by_id(symtab, symval));
}

static void es_symbol_print(es_ctx_t* ctx, es_val_t sym, es_val_t port) {
  char* string = es_symtab_find_by_id(&ctx->symtab, es_to_symbol(sym));
  es_port_printf(ctx, port, "%s", string);
}

//=================
// Misc Values
//=================
ES_API int es_is_unbound(es_val_t val) { 
  return es_unbound_type == es_type_of(val);
}

ES_API int es_is_defined(es_val_t val) { 
  return es_defined_type == es_type_of(val);
}

ES_API int es_is_undefined(es_val_t val) { 
  return es_undefined_type == es_type_of(val);
}  

ES_API int es_is_void(es_val_t val) { 
  return es_void_type == es_type_of(val); 
}

ES_API int es_is_nil(es_val_t val) { 
  return es_nil_type == es_type_of(val); 
}

ES_API int es_is_eof_obj(es_val_t val) { 
  return es_eof_obj_type == es_type_of(val); 
}

//=================
// Vectors
//=================
ES_API es_val_t es_vec_new(es_ctx_t* ctx, int size) {
  es_vec_t* vector = es_alloc(ctx, es_vector_type, sizeof(es_vec_t) + size * sizeof(es_val_t));
  vector->length = size;
  for(int i = 0; i < size; i++)
    vector->array[i] = es_unbound;
  return es_tagged_obj(vector);
}

ES_API int es_is_vec(es_val_t val) { 
  return es_vector_type == es_type_of(val); 
}

ES_API es_vec_t* es_to_vec(es_val_t val) { 
  return es_obj_to(es_vec_t*, val); 
}

ES_API int es_vec_len(es_val_t vector) { 
  return es_to_vec(vector)->length; 
}

ES_API void es_vec_set(es_val_t vector, int idx, es_val_t val) { 
  es_to_vec(vector)->array[idx] = val; 
}

ES_API es_val_t es_vec_ref(es_val_t vector, int idx) { 
  return es_to_vec(vector)->array[idx]; 
}

ES_API es_val_t es_vec_from_list(es_ctx_t* ctx, es_val_t list) {
  es_val_t vector;
  int i, length;
  gc_root(ctx, list);
  length = es_list_length(list);
  vector = es_vec_new(ctx, length);
  for(i = 0; i < length; i++, list = es_cdr(list)) {
    es_vec_set(vector, i, es_car(list));
  }
  gc_unroot(ctx, 1);
  return vector;
}

static size_t es_vec_size_of(es_val_t vecval) {
  return sizeof(es_vec_t) + es_to_vec(vecval)->length * sizeof(es_val_t);
}

static void es_vec_mark_copy(struct es_heap* heap, es_val_t pval, char** next) {
  es_vec_t* vector = es_to_vec(pval);
  for(int i = 0; i < vector->length; i++) {
    es_mark_copy(heap, &vector->array[i], next);
  }
}

static void es_vec_print(es_ctx_t* ctx, es_val_t self, es_val_t port) {
  int i, len;
  es_port_printf(ctx, port, "#("); 
  es_print(ctx, es_vec_ref(self, 0), port);
  for(i = 1, len = es_vec_len(self); i < len; i++) {
    es_port_printf(ctx, port, " "); 
    es_print(ctx, es_vec_ref(self, i), port);
  }
  es_port_printf(ctx, port, ")");
}

//=================
// Environment
//=================
es_val_t es_env_new(es_ctx_t* ctx, int size){
  es_env_t* env = es_alloc(ctx, es_env_type, sizeof(es_env_t) + sizeof(es_slot_t) * size);
  env->count = 0;
  env->size  = size;
  return es_tagged_obj(env);
}

ES_API es_env_t* es_to_env(es_val_t val) { 
  return es_obj_to(es_env_t*, val); 
}

static void es_env_mark_copy(struct es_heap* heap, es_val_t pval, char** next) {
  es_env_t* env = es_to_env(pval);
  for(int i = 0; i < env->count; i++) {
    es_mark_copy(heap, &env->slots[i].val, next);
  }
}

static int es_env_loc(es_val_t _env, es_val_t sym) {
  es_env_t* env = es_to_env(_env);
  for(int i = 0; i < env->count; i++) {
    if (es_is_eq(env->slots[i].sym, sym)) {
      return i;
    }
  }
  return -1;
}

static int es_env_reserve_loc(es_val_t _env, es_val_t sym, es_val_t init) {
  es_env_t* env = es_to_env(_env);
  int loc = es_env_loc(_env, sym);
  if (loc > -1) {
    if (es_is_undefined(env->slots[loc].val)) {
      env->slots[loc].val = init;
    }
    return loc;
  }
  env->slots[env->count].sym = sym;
  env->slots[env->count].val = init;
  return env->count++;
}

ES_API es_val_t es_define_symbol(es_ctx_t* ctx, es_val_t _env, es_val_t sym, es_val_t val) {
  es_env_t* env = es_to_env(_env);
  int loc = es_env_reserve_loc(_env, sym, es_defined);
  env->slots[loc].val = val;
  return es_void;
}

ES_API es_val_t es_define(es_ctx_t* ctx, char* symbol, es_val_t val) {
  return es_define_symbol(ctx, es_ctx_env(ctx), es_symbol_intern(ctx, symbol), val);
}

ES_API es_val_t es_define_fn(es_ctx_t* ctx, char* name, es_pfn_t fn, int arity) {
  return es_define(ctx, name, es_fn_new(ctx, arity, fn));
}

ES_API es_val_t es_lookup_symbol(es_ctx_t* ctx, es_val_t _env, es_val_t sym) {
  es_val_t val  = es_unbound;
  es_env_t* env = es_to_env(_env);
  int loc = es_env_loc(_env, sym);
  if (loc > -1) {
    val = env->slots[loc].val; 
  }
  return es_is_undefined(val) || es_is_defined(val) ? es_error_new(ctx, "unbound symbol") : val;
}

es_val_t es_env_set(es_ctx_t* ctx, es_val_t env, int slot, es_val_t val) {
  es_env_t* e = es_to_env(env);
  if (es_is_undefined(e->slots[slot].val)) {
    return es_error_new(ctx, "unbound symbol");
  }
  e->slots[slot].val = val;
  return es_void;
}

es_val_t es_env_ref(es_ctx_t* ctx, es_val_t env, int slot) {
  es_env_t* e = es_to_env(env);
  es_val_t val = e->slots[slot].val;
  return !es_is_undefined(val) ? val : es_error_new(ctx, "unbound symbol");
}

static es_val_t es_args_new(es_ctx_t* ctx, es_val_t parent, int arity, int rest, int argc, es_val_t* argv) {
  gc_root(ctx, parent);
  es_args_t* env = es_alloc(ctx, es_args_type, sizeof(es_args_t) + argc * sizeof(es_val_t));
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
  }
  gc_unroot(ctx, 1);
  return es_tagged_obj(env);
}

#define es_to_args(val) ((es_args_t*)val.obj)

size_t es_args_size_of(es_val_t val) {
  es_args_t* args = es_obj_to(es_args_t*, val);
  return sizeof(es_args_t) + args->size * sizeof(es_val_t);
}

static void es_args_mark_copy(struct es_heap* heap, es_val_t pval, char** next) {
  es_args_t* args = es_obj_to(es_args_t*, pval);
  es_mark_copy(heap, &args->parent, next);
  for(int i = 0; i < args->size; i++)
    es_mark_copy(heap, &args->args[i], next);
}

//=================
// Bytecode
//=================
es_val_t es_bytecode_new(es_ctx_t* ctx) {
  es_bytecode_t* b = es_alloc(ctx, es_bytecode_type, sizeof(es_bytecode_t));
  b->inst       = malloc(1024 * sizeof(es_inst_t));
  b->inst_size  = 1024;
  b->cpool_size = 256;
  b->cpi        = 0;
  b->ip         = 0;
  return es_tagged_obj(b);
}

ES_API int es_is_bytecode(es_val_t val) { 
  return es_bytecode_type == es_type_of(val); 
}

ES_API es_bytecode_t* es_to_bytecode(es_val_t val) { 
  return es_obj_to(es_bytecode_t*, val); 
}

static void es_bytecode_mark_copy(struct es_heap* heap, es_val_t pval, char** next) {
  es_bytecode_t* bc = es_obj_to(es_bytecode_t*, pval);
  for(int i = 0; i < bc->cpi; i++)
    es_mark_copy(heap, &bc->cpool[i], next);
}

//static void indent(int depth) { int i; for(i = 0; i < depth; i++) { printf("  "); } }

static void print_inst(es_inst_t* inst) {
  es_opcode_info_t* i = NULL; //&opcode_info[inst->op];
  switch(i->arity) {
    case 0: printf("%s", i->name);                         break;
    case 1: printf("%s %d", i->name, inst->op1);             break;
    case 2: printf("%s %d %d", i->name, inst->op1, inst->op2); break;
  }
}

static int inst_arity(es_opcode_t op) {
  return opcode_info[op].arity;
}

static void es_bytecode_print(es_ctx_t* ctx, es_val_t val, es_val_t port) {
  es_bytecode_t* bc = es_to_bytecode(val);
  int i;
  i = 0;
  es_port_printf(ctx, port, " instructions \n"); 
  es_port_printf(ctx, port, "---------\n");
  while(i < bc->ip) {
    es_port_printf(ctx, port, "%4d : ", i);
    print_inst(bc->inst + i);
    i++;
    es_port_printf(ctx, port, "\n");
  }
  es_port_printf(ctx, port, "\n");
  es_port_printf(ctx, port," const pool \n");
  es_port_printf(ctx, port,"--------\n");
  for(i = 0; i < bc->cpi; i++) {
    es_port_printf(ctx, port,"%4d : ", i); 
    es_print(ctx, bc->cpool[i], port);
    es_port_printf(ctx, port, "\n");
  }
}

static void emit_inst(es_val_t code, es_opcode_t op, short op1, short op2) {
  es_bytecode_t* b = es_to_bytecode(code);
  if (b->ip >= b->inst_size) {
    b->inst_size += b->inst_size / 2;
    b->inst = realloc(b->inst, b->inst_size);
  }
  b->inst[b->ip].op  = get_op(op);
  b->inst[b->ip].op1 = op1;
  b->inst[b->ip].op2 = op2;
  b->ip++;
}

static void emit_global_set(es_val_t code, int cpi) { 
  emit_inst(code, GLOBAL_SET, cpi, 0);
}

static void emit_const(es_val_t code, int cpi) { 
  emit_inst(code, CONST, cpi, 0); 
}

static void emit_halt(es_val_t code) { 
  emit_inst(code, HALT, 0, 0); 
}

static void emit_global_ref(es_val_t code, int cpi) { 
  emit_inst(code, GLOBAL_REF, cpi, 0);
}

static void emit_arg_ref(es_val_t code, int idx) { 
  emit_inst(code, ARG_REF, idx, 0); 
}

static void emit_arg_set(es_val_t code, int idx) { 
  emit_inst(code, ARG_SET, idx, 0); 
}

static void emit_pop(es_val_t code) { 
  emit_inst(code, POP, 0, 0); 
}

static void emit_bf(es_val_t code, int dIp) { 
  emit_inst(code, BF, dIp, 0);
}

static void emit_jmp(es_val_t code, int dIp) { 
  emit_inst(code, JMP, dIp, 0);
}

static void emit_closure(es_val_t code, int cpi) { 
  emit_inst(code, CLOSURE, cpi, 0);
}

static void emit_call(es_val_t code, int argc) { 
  emit_inst(code, CALL, argc, 0); 
}

static void emit_tail_call(es_val_t code, int argc) { 
  emit_inst(code, TAIL_CALL, argc, 0);
}

static void emit_closed_ref(es_val_t code, int depth, int idx) { 
  emit_inst(code, CLOSED_REF, depth, idx); 
}

static void emit_closed_set(es_val_t code, int depth, int idx) { 
  emit_inst(code, CLOSED_SET, depth, idx);
}

static int alloc_const(es_val_t code, es_val_t v) {
  es_bytecode_t* b = es_to_bytecode(code);
  for(int i = 0; i < b->cpi; i++) {
    if (es_is_eq(v, b->cpool[i]))
      return i;
  }
  b->cpool[b->cpi] = v; 
  return b->cpi++; 
}
  
#define label(code) es_to_bytecode(code)->ip

//=================
// Closures
//=================
es_val_t es_closure_new(es_ctx_t* ctx, es_val_t env, es_val_t proc) {
  es_closure_t* closure;
  gc_root2(ctx, env, proc);
  closure = es_alloc(ctx, es_closure_type, sizeof(es_closure_t));
  closure->proc = proc;
  closure->env  = env;
  gc_unroot(ctx, 2);
  return es_tagged_obj(closure);
}

ES_API int es_is_closure(es_val_t value) { 
  return es_closure_type == es_type_of(value); 
}

ES_API es_closure_t* es_to_closure(es_val_t val) { 
  return es_obj_to(es_closure_t*, val); 
}

static void es_closure_mark_copy(struct es_heap* heap, es_val_t pval, char** next) {
  es_closure_t* closure = es_to_closure(pval);
  es_mark_copy(heap, &closure->proc, next);
  es_mark_copy(heap, &closure->env, next);
}

//=================
// Utils
//=================
ES_API es_val_t es_list(es_ctx_t* ctx, ...) {
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

ES_API es_val_t es_list_argv(es_ctx_t* ctx, int argc, es_val_t* argv) {
  es_val_t list;
  int      i;
  for(i = 0, list = es_nil; i < argc; i++) {
    list = es_cons(ctx, argv[i], list);
  }
  return list;
}

ES_API int es_list_length(es_val_t list) {
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
  tk_fbool, tk_str, tk_dot, tk_sym, tk_quot, tk_qquot, 
  tk_unquot, tk_unquot_splice, tk_unknown
} es_token_t;

typedef enum lstate { 
  lstate_start, lstate_sign, lstate_int, lstate_dot, lstate_sym
} lstate_t;

#define pgetc(port)  es_port_getc(port)
#define ppeekc(port) es_port_peekc(port)

static int escape(int c) {
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

static int eat_ws(es_val_t port) {
  int res = 0;
  while(isspace(ppeekc(port))) { 
    pgetc(port); 
    res = 1;
  }
  return res;
}

static int eat_cmnt(es_val_t port) {
  int res;
  if ((res = (ppeekc(port) == ';'))) {
    while(pgetc(port) != '\n') {}
  }
  return res;
}

static void eat_sp(es_val_t port) {
  while(eat_ws(port) || eat_cmnt(port)) {};
}

static void eat_line(es_val_t port) {
  while(pgetc(port) != '\n') {}
}

static lstate_t step(lstate_t s, int c) {
  if (s == lstate_start) {
    if (strchr("+-", c)) s = lstate_sign;
    else if (isdigit(c)) s = lstate_int;
    else if ('.' == c)   s = lstate_dot;
    else                 s = lstate_sym;
  } else if (s == lstate_sign) {
    s = isdigit(c) ? lstate_int : lstate_sym;
  } else if (s == lstate_int && !isdigit(c)) {
    s = lstate_sym;
  } else if (s == lstate_dot) {
    s = lstate_sym;
  }
  return s;
}

es_token_t next(es_val_t port, char* pbuf) {
  memset(pbuf, '\0', 1024);
  eat_sp(port);
  int c = pgetc(port);
  switch(c) {
  case -1:   return tk_eof;
  case '(':  return tk_lpar;
  case ')':  return tk_rpar;
  case '\'': return tk_quot;
  case '`':  return tk_qquot;
  case ',':  return tk_unquot;
  case '"':
    while((c = pgetc(port)) != '"')
      *pbuf++ = '\\' == c ? escape(pgetc(port)) : c;
    return tk_str;
  case '#':
    c = pgetc(port);
    if (c == 't')      return tk_tbool;
    else if (c == 'f') return tk_fbool;
    else if (c == '(') return tk_hlpar;
  default: {
    lstate_t s = step(lstate_start, c);
    *pbuf++ = c;
    while(!strchr(" \t\n\r()'", ppeekc(port))) {
      s = step(s, (*pbuf++ = c = pgetc(port)));
    }
    if (s == lstate_sign || s == lstate_sym) 
      return tk_sym;
    else if (s == lstate_int)                
      return tk_int;
    else if (s == lstate_dot)
      return tk_dot;
    else                             
      return tk_unknown;
  }
  }
}

static es_token_t peek(es_val_t port, char* pbuf) {
  es_port_mark(port);
  es_token_t t = next(port, pbuf);
  es_port_reset(port);
  return t;
}

static es_val_t reverse(es_val_t lst) {
  es_val_t res = es_nil;
  while(!es_is_nil(lst)) {
    es_val_t next = es_cdr(lst);
    es_pair_set_tail(lst, res);
    res = lst;
    lst = next;
  }
  return res;
}

static es_val_t es_parse(es_ctx_t* ctx, es_val_t port) {
  char buf[1024];
  switch(next(port, buf)) {
    case tk_eof:   return es_eof_obj;
    case tk_str:   return es_string_new(ctx, buf);
    case tk_sym:   return es_symbol_intern(ctx, buf);
    case tk_int:   return es_fixnum_new(atoi(buf));
    case tk_tbool: return es_true;
    case tk_fbool: return es_false;
    case tk_quot: {
      es_val_t e = es_parse(ctx, port);
      return es_list(ctx, symbol_quote, e, es_void);
    }
    case tk_hlpar: {
      es_val_t lst = es_nil;
      while(peek(port, buf) != tk_rpar) {
        lst = es_cons(ctx, es_parse(ctx, port), lst);
      }
      next(port, buf);
      return es_vec_from_list(ctx, reverse(lst));
    }
    case tk_lpar: {
      if (peek(port, buf) == tk_rpar) {
        next(port, buf); return es_nil;
      } else {
        es_val_t e = es_parse(ctx, port);
        if (es_is_error(e)) return e;
        es_val_t lst = es_cons(ctx, e, es_nil);
        es_val_t node = lst;
        while(peek(port, buf) != tk_rpar) {
          if (peek(port, buf) == tk_dot) {
            next(port, buf);
            es_set_cdr(node, es_parse(ctx, port));
            if (peek(port, buf) != tk_rpar) {
              eat_line(port);
              return es_error_new(ctx, "syntax dotted list");
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
    }
    default: return es_error_new(ctx, "Invalid syntax");
  }
}

static void es_symtab_init(es_symtab_t* symtab) {
  symtab->next_id = 0;
}

static char* es_symtab_find_by_id(es_symtab_t* symtab, int id) {
  return symtab->table[id];
}

static int es_symtab_id_by_string(es_symtab_t* symtab, char* cstr) {
  for(int i = 0; i < symtab->next_id; i++) {
    if (strcmp(symtab->table[i], cstr) == 0) {
      return i;
    }
  }
  return -1;
}

static int es_symtab_count(es_symtab_t* symtab) { 
  return symtab->next_id; 
}

static int es_symtab_find_or_create(es_symtab_t* symtab, char* cstr) {
  int id = es_symtab_id_by_string(symtab, cstr);
  if (id < 0) {
    return es_symtab_add_string(symtab, cstr);
  }
  return id;
}

static int es_symtab_add_string(es_symtab_t* symtab, char* cstr) {
  if (symtab->next_id < es_symtab_size) {
    char* newstr = malloc(strlen(cstr) + 1);
    strcpy(newstr, cstr);
    symtab->table[symtab->next_id] = newstr;
    return symtab->next_id++;
  }
  return -1;
}

static es_val_t flatten_args(es_ctx_t* ctx, es_val_t args) {
  if (es_is_nil(args))
    return es_nil;
  if (es_is_symbol(args))
    return es_cons(ctx, args, es_nil);
  else
    return es_cons(ctx, es_car(args), flatten_args(ctx, es_cdr(args)));
}

static int index_of(es_val_t lst, es_val_t e) {
  for(int i = 0; !es_is_nil(lst); i++, lst = es_cdr(lst)) {
    if (es_is_eq(es_car(lst), e))
      return i;
  }
  return -1;
}

static es_val_t scope_args(es_val_t scope) { 
  return es_car(scope); 
}

static es_val_t mkscope(es_ctx_t* ctx, es_val_t args, es_val_t parent) { 
  return es_cons(ctx, flatten_args(ctx, args), parent); 
}

static es_val_t scope_parent(es_val_t scope) { 
  return es_cdr(scope); 
}

static int arg_idx(es_val_t scope, es_val_t symbol, int* pidx, int* pdepth) {
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

static int lambda_arity(es_val_t formals, int* rest) {
  int arity = 0;
  if (es_is_symbol(formals)) { 
    *rest = 1; 
    return arity; 
  }
  while(es_is_pair(formals)) {
    arity++;
    formals = es_cdr(formals);
  }
  *rest = es_is_nil(formals) ? 0 : 1;
  return arity;
}

static es_val_t compile(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope) {
  if (es_is_pair(exp)) {
    compile_form(ctx, bc, exp, tail_pos, next, scope);
  } else if(es_is_symbol(exp)) {
    compile_ref(ctx, bc, exp, tail_pos, next, scope);
  } else {
    compile_const(ctx, bc, exp, tail_pos, next, scope);
  }
  return es_void;
}

static es_val_t compile_form(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope) {
  es_val_t op   = es_car(exp);
  es_val_t args = es_cdr(exp);
  if (es_is_symbol(op)) {
    if (es_is_eq(op, symbol_define)) {
      compile_define(ctx, bc, es_car(args), es_cdr(args), tail_pos, next, scope);
    } else if (es_is_eq(op, symbol_if)) {
      es_val_t cond  = es_car(args);
      es_val_t bthen = es_cadr(args);
      es_val_t belse = es_caddr(args);
      compile_if(ctx, bc, cond, bthen, belse, tail_pos, next, scope);
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
      if (tail_pos) emit_inst(bc, next, 0, 0);
    } else {
      compile_call(ctx, bc, exp, tail_pos, next, scope);
    }
  } else {
    compile_call(ctx, bc, exp, tail_pos, next, scope);
  }
  return es_void;
}

static es_val_t compile_const(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope) {
  emit_const(bc, alloc_const(bc, exp));
  if (tail_pos) emit_inst(bc, next, 0, 0);
  return es_void;
}
static es_val_t compile_ref(es_ctx_t* ctx, es_val_t bc, es_val_t sym, int tail_pos, int next, es_val_t scope) {
  int idx, depth;
  if (arg_idx(scope, sym, &idx, &depth)) {
    if (depth == 0) {
      emit_arg_ref(bc, idx);
    } else {
      emit_closed_ref(bc, depth, idx);
    }
  } else {
    emit_global_ref(bc, es_env_reserve_loc(es_ctx_env(ctx), sym, es_undefined));
  }
  if (tail_pos) emit_inst(bc, next, 0, 0);
  return es_void;
}

static es_val_t compile_set(es_ctx_t* ctx, es_val_t bc, es_val_t sym, es_val_t exp, int tail_pos, int next, es_val_t scope) {
  compile(ctx, bc, exp, 0, 0, scope);
  int idx, depth;
  if (arg_idx(scope, sym, &idx, &depth)) {
    if (depth == 0) {
      emit_arg_set(bc, idx);
    } else {
      emit_closed_set(bc, depth, idx);
    }
  } else {
    emit_global_set(bc, es_env_reserve_loc(es_ctx_env(ctx), sym, es_undefined));
  }
  if (tail_pos) emit_inst(bc, next, 0, 0);
  return es_void;
}

static es_val_t compile_call(es_ctx_t* ctx, es_val_t bc, es_val_t exp, int tail_pos, int next, es_val_t scope) {
  es_val_t op = es_car(exp);
  es_val_t args = es_cdr(exp);
  int argc = es_list_length(args);
  compile_args(ctx, bc, args, scope);
  compile(ctx, bc, op, 0, 0, scope);
  if (tail_pos) {
    emit_tail_call(bc, argc);
  } else {
    emit_call(bc, argc);
  }
  return es_void;
}

static es_val_t compile_lambda(es_ctx_t* ctx, es_val_t bc, es_val_t formals, es_val_t body, int tail_pos, int next, es_val_t scope) {
  int label1 = label(bc); 
  emit_jmp(bc, -1);
  int label2 = label(bc);
  scope = mkscope(ctx, formals, scope);
  compile_seq(ctx, bc, body, 1, RETURN, scope);
  int label3 = label(bc);
  int rest;
  int arity = lambda_arity(formals, &rest);
  es_val_t proc = es_proc_new(ctx, arity, rest, label2);
  emit_closure(bc, alloc_const(bc, proc));
  es_to_bytecode(bc)->inst[label1].op1 = label3 - label1;
  if (tail_pos) emit_inst(bc, next, 0, 0);
  return es_void;
}

static es_val_t compile_if(es_ctx_t* ctx, es_val_t bc, es_val_t cond, es_val_t bthen, es_val_t belse, int tail_pos, int next, es_val_t scope) {
  compile(ctx, bc, cond, 0, 0, scope);
  int label1 = label(bc); 
  emit_bf(bc, -1);
  compile(ctx, bc, bthen, tail_pos, next, scope);
  int label2 = label(bc); 
  if (!tail_pos) emit_jmp(bc, -1);
  int label3 = label(bc); 
  compile(ctx, bc, belse, tail_pos, next, scope);
  int label4 = label(bc);
  es_to_bytecode(bc)->inst[label1].op1 = label3 - label1;
  if (!tail_pos) es_to_bytecode(bc)->inst[label2].op1 = label4 - label2;
  return es_void;
}

static es_val_t compile_define(es_ctx_t* ctx, es_val_t bc, es_val_t binding, es_val_t val, int tail_pos, int next, es_val_t scope) {
  if (es_is_symbol(binding)) {
    compile(ctx, bc, es_car(val), 0, 0, scope);
    emit_global_set(bc, es_env_reserve_loc(es_ctx_env(ctx), binding, es_defined));
    if (tail_pos) emit_inst(bc, next, 0, 0);
  } else if (es_is_pair(binding)) {
    es_val_t sym     = es_car(binding);
    es_val_t formals = es_cdr(binding);
    es_val_t body    = val;
    compile_lambda(ctx, bc, formals, body, 0, 0, scope);
    emit_global_set(bc, es_env_reserve_loc(es_ctx_env(ctx), sym, es_defined));
    if (tail_pos) emit_inst(bc, next, 0, 0);
  } else {
    return es_error_new(ctx, "invalid define syntax");
  }
  return es_void;
}

static es_val_t compile_seq(es_ctx_t* ctx, es_val_t bc, es_val_t seq, int tail_pos, int next, es_val_t scope) {
  while(!es_is_nil(es_cdr(seq))) {
    compile(ctx, bc, es_car(seq), 0, 0, scope);
    emit_pop(bc);
    seq = es_cdr(seq);
  }
  compile(ctx, bc, es_car(seq), tail_pos, next, scope);
  return es_void;
}

static es_val_t compile_args(es_ctx_t* ctx, es_val_t bc, es_val_t args, es_val_t scope) {
  if (!es_is_nil(args)) {
    compile(ctx, bc, es_car(args), 0, 0, scope);
    compile_args(ctx, bc, es_cdr(args), scope);
  }
  return es_void;
}

//=============
// VM
//=============
#define pop(ctx)             ctx->state.stack[--ctx->state.sp] 
#define push(ctx, v)         ctx->state.stack[ctx->state.sp++] = v
#define get_const(idx)       es_to_bytecode(ctx->bytecode)->cpool[idx]
#define get_arg(ctx, idx)    es_to_args(ctx->state.args)->args[idx]
#define set_arg(ctx, idx, v) es_to_args(ctx->state.args)->args[idx] = v
#define get_args(ctx)        ctx->state.args
#define set_args(ctx, v)     ctx->state.args = v
#define get_env(ctx)         ctx->state.env
#define get_sp(ctx)          ctx->state.sp
#define set_sp(ctx, i)       ctx->state.sp = (i)
#define get_stack(ctx)       ctx->state.stack
#define restore(ctx)         ctx->state.fp--; ctx->state.args = ctx->state.frames[ctx->state.fp].args; ip = ctx->state.frames[ctx->state.fp].knt;
#define save(ctx)            ctx->state.frames[ctx->state.fp].args = ctx->state.args; ctx->state.frames[ctx->state.fp].knt = ip; ctx->state.fp++;

#ifdef LABELS_AS_VALUES
  #define dispatch(o, opcodes) goto *(o); opcodes
  #define opcode(o, code)      o: { code } goto *(ip->op);
#else
  #define dispatch(o, opcodes) switch(o) { opcodes }
  #define opcode(o, code)      case o: { code } break;
#endif

static es_val_t es_vm(es_ctx_t* ctx, int start, es_val_t env) {
  ctx->state.env  = env;
  ctx->state.args = es_nil;
  ctx->state.sp   = 0;
  ctx->state.fp   = 0;

#ifdef LABELS_AS_VALUES
  static void* opcode_jmp_tbl[] = {
    &&HALT, &&CONST, &&POP, &&GLOBAL_REF, &&GLOBAL_SET, &&CLOSED_REF, &&CLOSED_SET, 
    &&ARG_REF, &&ARG_SET, &&JMP, &&BF, &&CALL, &&TAIL_CALL, &&RETURN, &&CLOSURE
  };

  inst_addrs = opcode_jmp_tbl;
#endif

  if (start == -1) {
    return es_void;
  }

  es_inst_t* inst = es_to_bytecode(ctx->bytecode)->inst;
  es_inst_t* ip = inst + start;

  while(1) {
    dispatch(inst_op(ip),
      opcode(HALT,
        return pop(ctx);
      )
      opcode(CONST,
        push(ctx, get_const(inst_op1(ip))); 
        ip++;
      )
      opcode(POP,
        pop(ctx);
        ip++;
      )
      opcode(BF,
        ip += es_is_true(pop(ctx)) ? 1 : inst_op1(ip);
      )
      opcode(JMP,
        ip += inst_op1(ip);
      )
      opcode(GLOBAL_REF,
        push(ctx, es_env_ref(ctx, get_env(ctx), inst_op1(ip))); 
        ip++;
      )
      opcode(GLOBAL_SET,
        es_val_t v = pop(ctx);
        push(ctx, es_env_set(ctx, get_env(ctx), inst_op1(ip), v)); 
        ip++;
      )
      opcode(CLOSED_REF,
        es_val_t   cenv  = get_args(ctx);
        int        depth = inst_op1(ip);
        int        idx   = inst_op2(ip);
        while(depth-- > 0) 
          cenv = es_to_args(cenv)->parent;
        push(ctx, es_to_args(cenv)->args[idx]);
        ip++;
      )
      opcode(CLOSED_SET,
        es_val_t   cenv  = get_args(ctx);
        int        depth = inst_op1(ip); 
        int        idx   = inst_op2(ip);
        while(depth-- > 0) 
          cenv = es_to_args(cenv)->parent;
        es_to_args(cenv)->args[idx] = pop(ctx);
        push(ctx, es_void);
        ip++;
      )
      opcode(ARG_REF,
        push(ctx, get_arg(ctx, inst_op1(ip))); 
        ip++;
      )
      opcode(ARG_SET,
        set_arg(ctx, inst_op1(ip), pop(ctx)); 
        ip++;
      )
      opcode(CLOSURE,
        push(ctx, es_closure_new(ctx, get_args(ctx), get_const(inst_op1(ip))));
        ip++;
      )
      opcode(RETURN,
        restore(ctx);
      )
      opcode(CALL,
        int argc = inst_op1(ip);
        ip++;
        es_val_t proc = pop(ctx);
        if (es_is_fn(proc)) { 
          es_val_t res = es_fn_apply_argv(ctx, proc, argc, get_stack(ctx) + get_sp(ctx) - argc); 
          set_sp(ctx, get_sp(ctx) - argc);
          push(ctx, res);
        } else if (es_is_closure(proc)) {
          es_closure_t* closure = es_to_closure(proc);
          es_proc_t* proc = es_obj_to(es_proc_t*, closure->proc);
          save(ctx);
          set_args(ctx, es_args_new(ctx, closure->env, proc->arity, proc->rest, argc, get_stack(ctx) + get_sp(ctx) - argc));
          set_sp(ctx, get_sp(ctx) - argc);
          ip = inst + proc->addr;
        }
      )
      opcode(TAIL_CALL,
        int argc = inst_op1(ip); 
        ip++;
        es_val_t proc = pop(ctx);
        if (es_is_fn(proc)) { 
          es_val_t res = es_fn_apply_argv(ctx, proc, argc, get_stack(ctx) + get_sp(ctx) - argc); 
          set_sp(ctx, get_sp(ctx) - argc); 
          push(ctx, res);
          restore(ctx);
        } else if (es_is_closure(proc)) {
          es_closure_t* closure = es_to_closure(proc);
          es_proc_t* proc = es_obj_to(es_proc_t*, closure->proc);
          set_args(ctx, es_args_new(ctx, closure->env, proc->arity, proc->rest, argc, get_stack(ctx) + get_sp(ctx) - argc));
          set_sp(ctx, get_sp(ctx) - argc);
          ip = inst + proc->addr;
        }
      )
    )
  }
  return pop(ctx);
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

ES_API int es_compile(es_ctx_t* ctx, es_val_t exp) {
  int start;
  es_val_t b = ctx->bytecode;
  start = label(b);
  compile(ctx, b, exp, 0, 0, es_nil);
  emit_halt(b);
  return start;
}

ES_API es_val_t es_load(es_ctx_t* ctx, char* file_name) {
  es_val_t exp, port;
  port = es_port_new(ctx, fopen(file_name, "r"));
  while(!es_is_eof_obj(exp = es_read(ctx, port))) {
    es_eval(ctx, exp, es_ctx_env(ctx));
  }
  es_port_close(port);
  return es_void;
}

ES_API es_val_t es_eval(es_ctx_t* ctx, es_val_t exp, es_val_t env) {
  //struct timeval t0, t1, dt;
  es_val_t res;
  int start = es_compile(ctx, exp);
  //gettimeofday(&t0, NULL);
  res = es_vm(ctx, start, es_ctx_env(ctx));  
  //gettimeofday(&t1, NULL);
  //timeval_subtract(&dt, &t1, &t0);
  //printf("time: %f\n", dt.tv_sec * 1000.0 + dt.tv_usec / 1000.0);
  return res;
}

static es_val_t fn_bytecode(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return ctx->bytecode; 
}

static es_val_t fn_env(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_ctx_env(ctx); 
}

static es_val_t fn_write(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  es_print(ctx, argv[0], argv[0]); return es_void; 
}

static es_val_t fn_cons(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_cons(ctx, argv[0], argv[1]); 
}

static es_val_t fn_car(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_car(argv[0]); 
}

static es_val_t fn_cdr(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_cdr(argv[0]); 
}

static es_val_t fn_add(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_add(argv[0], argv[1]); 
}

static es_val_t fn_sub(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_sub(argv[0], argv[1]); 
}

static es_val_t fn_mul(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_mul(argv[0], argv[1]); 
}

static es_val_t fn_div(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_div(argv[0], argv[1]); 
}

static es_val_t fn_is_num_eq(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_number_is_eq(argv[0], argv[1])); 
}

static es_val_t fn_is_boolean(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_boolean(argv[0])); 
}

static es_val_t fn_is_symbol(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_symbol(argv[0])); 
}

static es_val_t fn_is_char(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_char(argv[0])); 
}

static es_val_t fn_is_vec(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_vec(argv[0])); 
}

static es_val_t fn_is_procedure(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_fn(argv[0]) || es_is_closure(argv[0])); 
}

static es_val_t fn_is_pair(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_pair(argv[0])); 
}

static es_val_t fn_is_number(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_fixnum(argv[0])); 
}

static es_val_t fn_is_string(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_string(argv[0])); 
}

static es_val_t fn_is_port(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_port(argv[0])); 
}

static es_val_t fn_is_null(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_nil(argv[0])); 
}

static es_val_t fn_is_eq(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_eq(argv[0], argv[1])); 
}

static es_val_t fn_quit(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_eof_obj; 
}

static es_val_t fn_gc(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  es_gc(ctx); 
  return es_void;
}

static es_val_t fn_read_char(es_ctx_t* ctx, int argc, es_val_t* argv) {
  return es_port_read_char(argc == 0 ? ctx->iport : argv[0]);
}

static es_val_t fn_close(es_ctx_t* ctx, int argc, es_val_t* argv) {
  es_port_t* port = es_to_port(argv[0]);
  fclose(port->stream);
  port->stream = NULL;
  return es_void;
}

static es_val_t fn_compile(es_ctx_t* ctx, int argc, es_val_t argv[]) {
  es_val_t b = es_bytecode_new(ctx);
  compile(ctx, b, argv[0], 0, 0, es_nil);
  emit_halt(b);
  return b;
}

static es_val_t fn_apply(es_ctx_t* ctx, int argc, es_val_t argv[]) {
  return es_nil;
}

static void ctx_init_env(es_ctx_t* ctx) {
  es_define_fn(ctx, "bytecode",   fn_bytecode,     0);
  es_define_fn(ctx, "global-env", fn_env,          0);
  es_define_fn(ctx, "cons",       fn_cons,         2);
  es_define_fn(ctx, "car",        fn_car,          1);
  es_define_fn(ctx, "cdr",        fn_cdr,          1);
  es_define_fn(ctx, "+",          fn_add,          2);
  es_define_fn(ctx, "-",          fn_sub,          2);
  es_define_fn(ctx, "*",          fn_mul,          2);
  es_define_fn(ctx, "/",          fn_div,          2);
  es_define_fn(ctx, "compile",    fn_compile,      1);
  es_define_fn(ctx, "boolean?",   fn_is_boolean,   1);
  es_define_fn(ctx, "symbol?",    fn_is_symbol,    1);
  es_define_fn(ctx, "char?",      fn_is_char,      1);
  es_define_fn(ctx, "vector?",    fn_is_vec,       1);
  es_define_fn(ctx, "procedure?", fn_is_procedure, 1);
  es_define_fn(ctx, "pair?",      fn_is_pair,      1);
  es_define_fn(ctx, "number?",    fn_is_number,    1);
  es_define_fn(ctx, "string?",    fn_is_string,    1);
  es_define_fn(ctx, "port?",      fn_is_port,      1);
  es_define_fn(ctx, "null?",      fn_is_null,      2);
  es_define_fn(ctx, "=",          fn_is_num_eq,    2);
  es_define_fn(ctx, "eq?",        fn_is_eq,        2);
  es_define_fn(ctx, "quit",       fn_quit,         2);
  es_define_fn(ctx, "gc",         fn_gc,           0);
  es_define_fn(ctx, "write",      fn_write,        2);
  es_define_fn(ctx, "read-char",  fn_read_char,    2);
  es_define_fn(ctx, "close",      fn_close,        1);

  es_load(ctx, "scm/init.scm");
}