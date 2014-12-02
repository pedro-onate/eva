#include "eva.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <ctype.h>

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

enum es_tag {
  es_obj_tag     = 0x0, /**< b000 - Heap allocated object */
  es_fixnum_tag  = 0x1, /**< b001 - Fixnums */
  es_symbol_tag  = 0x2, /**< b010 - Symbols tag */
  es_value_tag   = 0x3, /**< b011 - Immediate values: Nil, EOF, Unspecified, Unbound */
  es_boolean_tag = 0x4, /**< b100 - Symbol tag */
  es_char_tag    = 0x5, /**< b101 - Character tag */
  es_max_tag
};

enum { es_symtab_size = 65536 };
enum { es_tag_bits = 3 };
enum { es_tag_mask = (1 << es_tag_bits) - 1 };
enum { es_val_payload_mask = ~(uintptr_t)es_tag_mask };
enum { es_val_payload_bits = sizeof(uintptr_t) * 8 - es_tag_bits };
enum { es_fixnum_min = -1 << es_val_payload_bits, es_fixnum_max = 1 << es_val_payload_bits };
enum { es_default_alignment = 16 };
enum { es_default_heap_size = 256 * 1000000 };

/* VM Opcodes */
enum es_opcode { HALT, CONST, POP, GLOBAL_REF, GLOBAL_SET, CLOSED_REF, CLOSED_SET, 
  ARG_REF, ARG_SET, JMP, BF, CALL, TAIL_CALL, RETURN, CLOSURE };

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
  char*  end;
  size_t requested;  /**< Requested heap size in bytes */
};

struct es_symtab {
  char* table[es_symtab_size];
  int   next_id;
};

struct es_frame {
  es_val_t   env;
  char*      knt;
};

struct es_state {
  es_val_t       genv;
  es_val_t       env;
  int            sp;
  es_val_t       stack[256];
  int            fp;
  es_frame_t     frames[256];
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
  es_state_t* state;
};

struct es_string {
  es_obj_t base;
  size_t   length;
  char     value[];
};

struct es_env {
  es_obj_t  base;
  es_val_t  parent;
  es_val_t  bindings;
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
  stream_t* srec;
  size_t    nbytes;
  int       nlines;
};

struct es_error {
  es_obj_t base;
  char*    errstr;
};

struct es_fn {
  es_obj_t  base;
  int       arity;
  es_pcfn_t pcfn;
};

struct es_proc {
  es_obj_t base;
  int      arity;
  int      rest;
  int      addr;
};

struct es_bytecode {
  es_obj_t  base;
  es_val_t  cpool[256];
  int       cpool_size;
  int       cpi;
  char*     inst;
  int       inst_size;
  int       ip;
};

typedef struct state {
  int mark;
  int nbytes;
  int lnum;
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
  int     lnum;
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
static es_obj_t* es_obj_tombstone(es_val_t obj);
static void      es_obj_init(es_val_t obj, enum es_type type);
static void      es_mark_copy(es_heap_t* heap, es_val_t* pval, char** next);
static int       es_obj_is_forwarded(es_val_t val);
static void*     es_align(void* p, int align) { return (void*)((((uintptr_t)p / align) + 1) * align); }
static void*     es_heap_alloc(es_heap_t* heap, size_t size);
static int       es_ceil_to(int n, int u);
static void      es_heap_init(es_heap_t* heap, size_t size);
static int       es_heap_to_contains(es_heap_t* heap, es_val_t val);
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
static int       stream_match_c(stream_t* stream, int c);
static int       stream_match_cs(stream_t* stream, char* cstr);
static int       stream_accept_c(stream_t* stream, int c);
static int       stream_acceptfn(stream_t* stream, int (*fn)(int));
static int       timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);
static es_val_t  es_parse(es_ctx_t* ctx, es_val_t iport);
static es_val_t  es_parse_exp(es_ctx_t* ctx, stream_t* stream, int depth, int qq);
static es_val_t  es_parse_list(es_ctx_t* ctx, stream_t* stream, int depth, int dot, int qq);
static es_val_t  es_parse_vector(es_ctx_t* ctx, stream_t* stream, int depth, int qq);
static es_val_t  es_parse_atom(es_ctx_t* ctx, stream_t* stream, int depth);
static es_val_t  es_parse_hashform(es_ctx_t* ctx, stream_t* stream, int depth, int qq);
static es_val_t  es_parse_string(es_ctx_t* ctx, stream_t* stream, int depth);
static es_val_t  es_parse_symnum(es_ctx_t* ctx, stream_t* stream, int depth);
static es_val_t  es_parse_char(stream_t* stream, int depth);
static void      es_accept_space(stream_t* stream);
static int       es_accept_whitespace(stream_t* stream);
static int       es_accept_comment(stream_t* stream);
static void      es_consume_line(stream_t* stream);
static void      es_until_terminator(stream_t* stream);
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
static void      compile(es_ctx_t* ctx, es_val_t p, es_val_t exp, int tail_pos, int next, es_val_t scope);
static void      compile_args(es_ctx_t* ctx, es_val_t p, es_val_t exp, es_val_t scope);
static void      compile_lambda(es_ctx_t* ctx, es_val_t bc, es_val_t formals, es_val_t body, int tail_pos, int next, es_val_t scope);

const es_val_t es_nil      = es_tagged_val(es_nil_type, es_value_tag);
const es_val_t es_true     = es_tagged_val(1, es_boolean_tag);
const es_val_t es_false    = es_tagged_val(0, es_boolean_tag);
const es_val_t es_eof_obj  = es_tagged_val(es_eof_obj_type, es_value_tag);
const es_val_t es_void     = es_tagged_val(es_void_type, es_value_tag);
const es_val_t es_unbound  = es_tagged_val(es_unbound_type, es_value_tag);

static const es_val_t symbol_define = es_tagged_val(0, es_symbol_tag);
static const es_val_t symbol_if     = es_tagged_val(1, es_symbol_tag);
static const es_val_t symbol_begin  = es_tagged_val(2, es_symbol_tag);
static const es_val_t symbol_set    = es_tagged_val(3, es_symbol_tag);
static const es_val_t symbol_lambda = es_tagged_val(4, es_symbol_tag);
static const es_val_t symbol_quote  = es_tagged_val(5, es_symbol_tag);

//=================
// Context
//=================
ES_API es_ctx_t* es_ctx_new(size_t heap_size) {
  es_ctx_t* ctx = malloc(sizeof(es_ctx_t));
  es_ctx_init(ctx, heap_size);
  return ctx;
}

static void es_ctx_init(es_ctx_t* ctx, size_t heap_size) {
  es_heap_init(&ctx->heap, heap_size);
  ctx->env      = es_env_new(ctx, es_nil);
  ctx->bytecode = es_bytecode_new(ctx);
  es_symtab_init(&ctx->symtab);
  es_symbol_intern(ctx, "define");
  es_symbol_intern(ctx, "if");
  es_symbol_intern(ctx, "begin");
  es_symbol_intern(ctx, "set!");
  es_symbol_intern(ctx, "lambda");
  es_symbol_intern(ctx, "quote");

  ctx_init_env(ctx);

  //es_load(ctx, "scm/http-server.scm");
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

  if (ctx->state) {
    es_state_t* state = ctx->state;
    es_mark_copy(heap, &state->genv, &next);
    for(int i = 0; i < state->sp; i++) {
      es_mark_copy(heap, &state->stack[i], &next);
    }
    for(int i = 1; i < state->fp; i++) {
      es_mark_copy(heap, &state->frames[i].env, &next);
    }
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

static int es_heap_from_contains(es_heap_t* heap, es_val_t val) {
  char* address = es_obj_to(char*, val);
  return address >= heap->from_space 
    && address < heap->from_space + heap->size;
}

static int es_heap_to_contains(es_heap_t* heap, es_val_t val) {
  char* address = es_obj_to(char*, val);
  return address >= heap->to_space 
    && address < heap->to_space + heap->size;
}

ES_API void es_mark_copy(es_heap_t* heap, es_val_t* ref, char** next) {
  if (es_is_obj(*ref) && !es_heap_to_contains(heap, *ref)) {
    if (es_obj_is_forwarded(*ref)) {
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
  case es_env_type:          es_print(ctx, es_to_env(val)->bindings, oport); break;
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

static es_obj_t* es_obj_tombstone(es_val_t obj) { 
  return es_to_obj(obj)->reloc; 
}

static void es_obj_init(es_val_t self, es_type_t type) {
  es_obj_t* obj = es_to_obj(self);
  obj->type  = type;
  obj->reloc = NULL;
}

static int es_obj_is_forwarded(es_val_t val) {
  if (es_is_obj(val)) {
    return es_obj_tombstone(val) != NULL;
  } else {
    return 0;
  }
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
  int   size   = 2048;
  char* buf    = malloc(size);
  port->srec   = stream_open(stream, buf, size);
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

ES_API es_val_t es_port_read_char(es_val_t iport) {
  es_port_t* p = es_to_port(iport);
  int c = getc(p->stream);
  return c == EOF ? es_eof_obj : es_char_new(c);
}

static int es_peekc(FILE* stream) {
  return ungetc(getc(stream), stream);
}

ES_API es_val_t es_port_peek_char(es_val_t iport) {
  int c = es_peekc(es_to_port(iport)->stream);
  return c == EOF ? es_eof_obj : es_char_new(c);
}

ES_API void es_port_close(es_val_t iport) {
  es_port_t* port = es_to_port(iport);
  fclose(port->stream);
  port->stream = NULL;
}

ES_API int es_port_linum(es_val_t iport) {
  return stream_linum(es_to_port(iport)->srec);
}

ES_API int es_port_colnum(es_val_t iport) {
  return stream_colnum(es_to_port(iport)->srec);
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
ES_API es_val_t es_fn_new(es_ctx_t* ctx, int arity, es_pcfn_t pcfn) {
  es_fn_t* fn = es_alloc(ctx, es_fn_type, sizeof(es_fn_t));
  fn->arity   = arity;
  fn->pcfn    = pcfn;
  return es_tagged_obj(fn);
}

ES_API int es_is_fn(es_val_t val) { 
  return es_fn_type == es_type_of(val); 
}

ES_API es_fn_t* es_to_fn(es_val_t val) { 
  return es_obj_to(es_fn_t*, val); 
}

static es_val_t es_fn_apply_argv(es_ctx_t* ctx, es_val_t fn, int argc, es_val_t* argv) {
  return es_to_fn(fn)->pcfn(ctx, argc, argv);
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
  es_port_printf(ctx, port,"#<compiled-proc %p>", es_to_proc(val)->addr);
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
es_val_t es_env_new(es_ctx_t* ctx, es_val_t parent){
  es_env_t* env;
  gc_root(ctx, parent);
  env = es_alloc(ctx, es_env_type, sizeof(es_env_t));
  env->parent = parent;
  env->bindings = es_nil;
  gc_unroot(ctx, 1);
  return es_tagged_obj(env);
}

ES_API es_env_t* es_to_env(es_val_t val) { 
  return es_obj_to(es_env_t*, val); 
}

static void es_env_mark_copy(struct es_heap* heap, es_val_t pval, char** next) {
  es_env_t* env = es_to_env(pval);
  es_mark_copy(heap, &env->parent, next);
  es_mark_copy(heap, &env->bindings, next);
}

ES_API es_val_t es_define_symbol(es_ctx_t* ctx, es_val_t env, es_val_t symbol, es_val_t value) {
  es_val_t res;
  gc_root3(ctx, env, symbol, value);
  res = lookup_binding(env, symbol);
  if (!es_is_unbound(res)) {
    es_set_cdr(res, value);
  } else {
    res = es_cons(ctx, symbol, value);
    res = es_cons(ctx, res, es_to_env(env)->bindings);
    es_to_env(env)->bindings = res;
  }
  gc_unroot(ctx, 3);
  return es_void;
}

ES_API es_val_t es_define(es_ctx_t* ctx, char* symbol, es_val_t val) {
  return es_define_symbol(ctx, ctx->env, es_symbol_intern(ctx, symbol), val);
}

ES_API es_val_t es_lookup_symbol(es_ctx_t* ctx, es_val_t env, es_val_t symbol) {
  es_val_t binding = lookup_binding(env, symbol);
  return !es_is_unbound(binding) ? es_cdr(binding) : es_error_new(ctx, "unbound symbol");
}

static es_val_t lookup_binding(es_val_t _env, es_val_t symbol) {
  es_env_t* env = es_to_env(_env);
  es_val_t binding = es_assq(env->bindings, symbol);
  if (!es_is_nil(binding)) {
    return binding;
  } else if (!es_is_nil(env->parent)) {
    return lookup_binding(env->parent, symbol);
  } else {
    return es_unbound;
  }
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
  b->inst       = malloc(1024);
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

static void print_inst(char* inst) {
  switch(*inst) {
  case CONST:      printf("const %d", inst[1]); break;
  case CLOSURE:    printf("closure %d", inst[1]); break;
  case POP:        printf("pop"); break;
  case GLOBAL_REF: printf("global-ref %d", inst[1]); break;
  case GLOBAL_SET: printf("global-set %d", inst[1]); break;
  case CLOSED_REF: printf("closed-ref %d %d", inst[1], inst[2]); break;
  case CLOSED_SET: printf("closed-set %d %d", inst[1], inst[2]); break;
  case ARG_REF:    printf("arg-ref %d", inst[1]); break;
  case ARG_SET:    printf("arg-set %d", inst[1]); break;
  case JMP:        printf("jmp %d", inst[1]); break;
  case BF:         printf("bf %d", inst[1]); break;
  case RETURN:     printf("return"); break;
  case CALL:       printf("call %d", inst[1]); break;
  case TAIL_CALL:  printf("tail-call %d", inst[1]); break;
  case HALT:       printf("halt"); break;
  }
}

static int inst_arity(es_opcode_t op) {
  switch(op) {
  case CONST:      return 1;
  case CLOSURE:    return 1;
  case POP:        return 0;
  case GLOBAL_REF: return 1;
  case GLOBAL_SET: return 1;
  case CLOSED_REF: return 2;
  case CLOSED_SET: return 2;
  case ARG_REF:    return 1;
  case ARG_SET:    return 1;
  case JMP:        return 1;
  case BF:         return 1;
  case RETURN:     return 0;
  case CALL:       return 1;
  case TAIL_CALL:  return 1;
  case HALT:       return 0;
  }
  return -1;
}

static void es_bytecode_print(es_ctx_t* ctx, es_val_t val, es_val_t port) {
  es_bytecode_t* p = es_to_bytecode(val);
  int i;
  i = 0;
  es_port_printf(ctx, port, " instructions \n"); 
  es_port_printf(ctx, port, "---------\n");
  while(i < p->ip) {
    es_port_printf(ctx, port, "%4d : ", i);
    print_inst(p->inst + i);
    i += inst_arity(p->inst[i]) + 1;
    es_port_printf(ctx, port, "\n");
  }
  es_port_printf(ctx, port, "\n");
  es_port_printf(ctx, port," const pool \n");
  es_port_printf(ctx, port,"--------\n");
  for(i = 0; i < p->cpi; i++) {
    es_port_printf(ctx, port,"%4d : ", i); 
    es_print(ctx, p->cpool[i], port);
    es_port_printf(ctx, port, "\n");
  }
}

static void emit_byte(es_bytecode_t* p, char byte) {
  if (p->ip >= p->inst_size) {
    p->inst_size += p->inst_size / 2;
    p->inst = realloc(p->inst, p->inst_size);
  }
  p->inst[p->ip++] = byte; 
}

static void emit_global_set(es_bytecode_t* p, int cpi) { 
  emit_byte(p, GLOBAL_SET); 
  emit_byte(p, cpi); 
}

static void emit_const(es_bytecode_t* p, int cpi) { 
  emit_byte(p, CONST); emit_byte(p, cpi); 
}

static void emit_halt(es_bytecode_t* p) { 
  emit_byte(p, HALT); 
}

static void emit_global_ref(es_bytecode_t* p, int cpi) { 
  emit_byte(p, GLOBAL_REF); 
  emit_byte(p, cpi); 
}

static void emit_arg_ref(es_bytecode_t* p, int idx) { 
  emit_byte(p, ARG_REF); 
  emit_byte(p, idx); 
}

static void emit_arg_set(es_bytecode_t* p, int idx) { 
  emit_byte(p, ARG_SET); 
  emit_byte(p, idx); 
}

static void emit_pop(es_bytecode_t* p) { 
  emit_byte(p, POP); 
}

static void emit_bf(es_bytecode_t* p, int dIp) { 
  emit_byte(p, BF); 
  emit_byte(p, dIp); 
}

static void emit_jmp(es_bytecode_t* p, int dIp) { 
  emit_byte(p, JMP); 
  emit_byte(p, dIp); 
}

static void emit_closure(es_bytecode_t* p, int cpi) { 
  emit_byte(p, CLOSURE); 
  emit_byte(p, cpi); 
}

static void emit_call(es_bytecode_t* p, int argc) { 
  emit_byte(p, CALL); 
  emit_byte(p, argc); 
}

static void emit_tail_call(es_bytecode_t* p, int argc) { 
  emit_byte(p, TAIL_CALL); 
  emit_byte(p, argc); 
}

static void emit_closed_ref(es_bytecode_t* p, int depth, int idx) { 
  emit_byte(p, CLOSED_REF); 
  emit_byte(p, depth); 
  emit_byte(p, idx); 
}

static void emit_closed_set(es_bytecode_t* p, int depth, int idx) { 
  emit_byte(p, CLOSED_SET); 
  emit_byte(p, depth); 
  emit_byte(p, idx); 
}

static int alloc_const(es_bytecode_t* p, es_val_t v) {
  for(int i = 0; i < p->cpi; i++) {
    if (es_is_eq(v, p->cpool[i]))
      return i;
  }
  p->cpool[p->cpi] = v; 
  return p->cpi++; 
}
  
#define label(p) p->ip

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

ES_API es_val_t es_assq(es_val_t lst, es_val_t key) {
  es_val_t e;
  while(!es_is_nil(lst)) {
    e = es_car(lst);
    if (es_is_eq(es_car(e), key)) {
      return e;
    }
    lst = es_cdr(lst);
  }
  return es_nil;
}

static int strcatc(char* buf, int len, int c, char** pbuf) {
  if (*pbuf < buf + len) { 
    *(*pbuf)++ = c; 
    **pbuf     = '\0';
    return 1;
  } 
  return 0;
}

enum { TKBUFSIZE = 2048 };
enum { MAXDEPTH  = 256  };

static void stream_record_resize(stream_t* s);
static int  update_stats(stream_t* s, int c);

static stream_t* stream_open(FILE* handle, char* srec, size_t len) {
  stream_t* s;

  s = (stream_t*)malloc(sizeof(stream_t));

  s->handle = handle;
  s->srec   = srec;
  s->len    = len;
  s->mark   = -1;
  s->cur    = 0;
  s->tail   = 0;
  s->eof    = 0;
  s->sp     = 0;
  s->nbytes = 0;
  s->lnum   = 0;
  s->colnum = 0;

  return s;
}

static void sprint(stream_t* s) {
  printf("buf: [%s], cur: %d, tail: %d\n", s->srec, s->cur, s->tail);
}

static int stream_linum(stream_t* s) {
  return s->lnum;
}

static int stream_colnum(stream_t* s) {
  return s->colnum;
}

static void stream_free(stream_t* s) {    
  fclose(s->handle);
  free(s);
}

static int stream_copybuf(stream_t* s, char* buf, size_t len) {
  int l = stream_reclen(s);
  strncpy(buf, stream_record(s), len);
  buf[l] = '\0';
  return 1;
}

static char* stream_record(stream_t* s) { 
  return s->srec + s->mark; 
}

static int stream_has_cursor(stream_t* s) { 
  return s->cur < s->tail; 
}

static int stream_reclen(stream_t* s) { 
  return s->cur - s->mark; 
}

static int stream_stack(stream_t* s) { 
  return s->sp; 
}

static void stream_record_reset(stream_t* s) {
  if (s->sp == 0) {
    s->cur = s->tail = 0;
  }
}

static void stream_mark(stream_t* s) {
  if (!stream_has_cursor(s)) {
    stream_record_reset(s);
  }

  s->stack[s->sp] = s->mark;
  s->sss[s->sp]   = (state_t){s->mark, s->nbytes, s->lnum, s->colnum};
  s->sp++;
  s->mark         = s->cur;
}

static void stream_resume(stream_t* s) {
  --s->sp;

  s->mark   = s->stack[s->sp];

  s->lnum   = s->sss[s->sp].lnum;
  s->nbytes = s->sss[s->sp].nbytes;
  s->colnum = s->sss[s->sp].colnum;
}

static void stream_reset(stream_t* s) {
  if (s->mark > -1) {
    s->cur = s->mark;

    if (s->sp > 0) {
      --s->sp;

      s->mark   = s->stack[s->sp];

      s->lnum   = s->sss[s->sp].lnum;
      s->nbytes = s->sss[s->sp].nbytes;
      s->colnum = s->sss[s->sp].colnum;
    }
  }
}

static void stream_set_cursor(stream_t* s, int cur) { 
  s->cur = cur; 
}

static void stream_advance_cursor(stream_t* s) { 
  s->cur++; 
}

static int stream_ismarked(stream_t* s) { 
  return s->mark > -1; 
}

static int stream_eof(stream_t* s) { 
  return s->eof; 
}

static void stream_seteof(stream_t* s) { 
  s->eof = 1; 
}

static void stream_record_append(stream_t* s, int c) {
  /* Resize stream record if necessary */
  if (s->tail >= s->len) {
    s->srec = realloc(s->srec, s->len <<= 1);
  }

  s->srec[s->tail++] = c;
  s->srec[s->tail]   = '\0';
}

static int stream_record_getc(stream_t* s) { 
  return update_stats(s, s->srec[s->cur++]); 
}

static int stream_record_peekc(stream_t* s) { 
  return s->srec[s->cur]; 
}

static int update_stats(stream_t* s, int c) {
  s->nbytes++;

  if ('\n' == c) {
    s->colnum = 0;
    s->lnum++;
    //printf("[eol:%d]\n", s->lnum);
  }

  s->colnum++;

  return c;
}

static int stream_getc(stream_t* s) {
  int c;

  /* Read from stream record if cursor is active */
  if (stream_has_cursor(s))
    return stream_record_getc(s);

  /* If cursor has caught up and EOF is set return */
  if (stream_eof(s))
    return EOF;

  /* Cursor has caught up with tail, read from file stream */
  c = fgetc(s->handle);

  /* If EOF is encountered set flag and return EOF */
  if (EOF == c) {
    stream_seteof(s);
    return c;
  }

  update_stats(s, c);

  /* We are not recording characters, just return */
  if (!stream_ismarked(s))
    return c;

  /* Append character to tail of stream record */
  stream_record_append(s, c);

  /* Advance stream cursor */
  stream_advance_cursor(s);

  return c;
}

static int stream_peekc(stream_t* s) {
  int c;

  /* Read next character from stream record if cursor is active*/
  if (stream_has_cursor(s))
    return stream_record_peekc(s);

  /* If cursor has caught up and EOF is set return */
  if (stream_eof(s))
    return EOF;

  if (!stream_ismarked(s)) {
    stream_mark(s);
    c = stream_getc(s);
    stream_reset(s);
    return c;
  } 

  /* Read next character from stream */
  c = fgetc(s->handle);

  /* If EOF is encountered set flag and return EOF */
  if (EOF == c) {
    stream_seteof(s);
    return c;
  }
  
  update_stats(s, c);

  /* Append character to tail of stream record */
  stream_record_append(s, c);

  return c;
}

static int stream_match_c(stream_t* stream, int c) { 
  return stream_peekc(stream) == c; 
}

static int stream_match_cs(stream_t* stream, char* cstr) { 
  return strchr(cstr, stream_peekc(stream)) != NULL; 
}

static int stream_accept_c(stream_t* stream, int c) {
  if (stream_peekc(stream) == c) {
    stream_getc(stream);
    return 1;
  }
  return 0;
}

static int stream_acceptfn(stream_t* stream, int (*fn)(int)) { 
  return fn(stream_peekc(stream)) ? stream_getc(stream) : 0; 
}

static es_val_t es_parse(es_ctx_t* ctx, es_val_t iport) {
  return es_parse_exp(ctx, es_to_port(iport)->srec, 0, 0);
}

static es_val_t es_parse_exp(es_ctx_t* ctx, stream_t* stream, int depth, int qq) {
  es_val_t exp;

  /* Return error if exceeded max parse depth */
  if (depth >= MAXDEPTH) {
    return es_error_new(ctx, "exceeded parse depth");
  }

  es_accept_space(stream);

  if (stream_accept_c(stream, '(')) {
    return es_parse_list(ctx, stream, depth + 1, 1, qq);
  } else if (stream_match_c(stream, '#')) {
    return es_parse_hashform(ctx, stream, depth, qq);
  } else if (stream_accept_c(stream, '\'')) {
    exp = es_parse_exp(ctx, stream, depth + 1, qq);
    if (es_is_error(exp)) return exp;
    return es_list(ctx, es_symbol_intern(ctx, "quote"), exp, es_void);
  } else if (stream_accept_c(stream, '`')) {
    exp = es_parse_exp(ctx, stream, depth + 1, qq + 1);
    if (es_is_error(exp)) return exp;
    return es_list(ctx, es_symbol_intern(ctx, "quasiquote"), exp, es_void);
  } else {
    return es_parse_atom(ctx, stream, depth + 1);
  }
}

static es_val_t es_parse_list(es_ctx_t* ctx, stream_t* stream, int depth, int dot, int qq) {
  es_val_t head, tail;
  es_accept_space(stream);
  if (stream_accept_c(stream, ')')) {
    return es_nil;
  } else {
    head = es_parse_exp(ctx, stream, depth + 1, qq);
    if (es_is_error(head)) return head;
    es_accept_space(stream);
    stream_mark(stream);
    if (dot && stream_accept_c(stream, '.')) {
      if (stream_acceptfn(stream, isspace)) {
        stream_resume(stream);
        tail = es_parse_exp(ctx, stream, depth + 1, qq);
        if (es_is_error(tail)) return tail;
        es_accept_space(stream);
        if (stream_accept_c(stream, ')')) {
          return es_cons(ctx, head, tail);
        } else {
          es_consume_line(stream);
          return es_error_new(ctx, "bad syntax: illegal use of '.'");
        }
      } else {
        stream_reset(stream);
        tail = es_parse_list(ctx, stream, depth + 1, dot, qq);
        if (es_is_error(tail)) return tail;
        return es_cons(ctx, head, tail);  
      }
    } else {
      stream_reset(stream);
      tail = es_parse_list(ctx, stream, depth + 1, dot, qq);
      if (es_is_error(tail)) return tail;
      return es_cons(ctx, head, tail);
    }
  }
}

static es_val_t es_parse_atom(es_ctx_t* ctx, stream_t* stream, int depth) {
  es_accept_space(stream);
  if (stream_match_c(stream, EOF)) {
    return es_eof_obj;
  } else if(stream_accept_c(stream, '"')) {
    return es_parse_string(ctx, stream, depth + 1);
  } else {
    return es_parse_symnum(ctx, stream, depth + 1);  
  }
}

static es_val_t es_parse_hashform(es_ctx_t* ctx, stream_t* stream, int depth, int qq) {
  if (stream_accept_c(stream, '#')) {
    switch(stream_getc(stream)) {
    case '(':  return es_parse_vector(ctx, stream, depth + 1, qq);
    case '\\': return es_parse_char(stream, depth + 1);
    case 't':  return es_true;
    case 'f':  return es_false;
    default:   return es_consume_line(stream), es_error_new(ctx, "bad syntax: invalid token");
    }
  }
  return es_error_new(ctx, "bad syntax: not a hashform");
}

static es_val_t es_parse_vector(es_ctx_t* ctx, stream_t* stream, int depth, int qq) {
  es_val_t exp = es_parse_list(ctx, stream, depth + 1, 0, qq);
  if (es_is_error(exp)) return exp;
  return es_vec_from_list(ctx, exp);
}

static int escape_char(int c) {
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

static es_val_t es_parse_string(es_ctx_t* ctx, stream_t* stream, int depth) {
  char  buf[TKBUFSIZE] = {0};
  char* pbuf           = buf;
  int   c;

  while((c = stream_getc(stream)) != '"') {
    if ('\\' == c) {
      c = escape_char(stream_getc(stream));
    }
    strcatc(buf, TKBUFSIZE, c, &pbuf);
  }
  return es_string_new(ctx, buf);
}

/**
 * Parses an
 */
enum type { initial, sign, dot, integer, decimal, rational, symbol, unknown };

static int classify_symnum(char* symnum) {
  enum type t = initial;
  int c, i, len, lastc;
  
  for(i = 0, len = strlen(symnum); i < len; i++) {
    c = symnum[i];
    lastc = i == len - 1;
    if (initial == t) {
      if ('+' == c || '-' == c) {
        t = lastc ? symbol : sign;
      } else if (isdigit(c)) {
        t = integer;
      } else if ('.' == c) {
        t = lastc ? dot : decimal;
      } else {
        t = symbol;
      }
    } else if (sign == t) {
      if ('.' == c) {
        t = decimal;
      } else if (isdigit(c)) {
        t = integer;
      } else {
        t = symbol;
      }
    } else if (integer == t) {
      if ('.' == c) {
        t = decimal;
      } else if('/' == c) {
        t = rational;
      } else if (!isdigit(c)) {
        t = symbol;
      }
    } else if (decimal == t) {
      if (!isdigit(c)) {
        t = symbol;
      }
    } else if (rational == t) {
      if (!isdigit(c)) {
        t = symbol;
      }
    }
  }

  return t;
}

static es_val_t es_parse_symnum(es_ctx_t* ctx, stream_t* stream, int depth) {
  char buf[TKBUFSIZE];

  stream_mark(stream);
  es_until_terminator(stream);
  stream_copybuf(stream, buf, TKBUFSIZE);
  stream_resume(stream);

  switch(classify_symnum(buf)) {
  case initial:
  case unknown: 
    es_consume_line(stream); 
    return es_error_new(ctx, "bad syntax: unexpected input");
  case dot:     
    es_consume_line(stream); 
    return es_error_new(ctx, "bad syntax: illegal use of '.'");
  case integer:     
    return es_fixnum_new(atoi(buf));
  case symbol:   
    return es_symbol_intern(ctx, buf);
  case decimal:
    es_consume_line(stream); 
    return es_error_new(ctx, "floating point numbers not currently supported.");
  case rational:
    es_consume_line(stream); 
    return es_error_new(ctx, "rational numbers not currently supported.");
  default:
    return es_void;
  }
}

static es_val_t es_parse_char(stream_t* stream, int depth) {
  char     buf[TKBUFSIZE];
  es_val_t c;

  stream_mark(stream);
  es_until_terminator(stream);
  stream_copybuf(stream, buf, TKBUFSIZE);
  c = es_char_str(buf);
  stream_resume(stream);

  return c;
}

static void es_accept_space(stream_t* stream) {
  while(es_accept_whitespace(stream) || es_accept_comment(stream)) {};
}

static int es_accept_whitespace(stream_t* stream) {
  int res = 0;
  while(stream_acceptfn(stream, isspace)) { res = 1; }
  return res;
}

static int es_accept_comment(stream_t* stream) {
  int res;
  if ((res = stream_accept_c(stream, ';'))) {
    while(stream_getc(stream) != '\n') {}
  }
  return res;
}

static void es_consume_line(stream_t* stream) {
  while(stream_getc(stream) != '\n') {}
}

static void es_until_terminator(stream_t* stream) {
  while(!stream_match_cs(stream, " \t\n\r()'")) { stream_getc(stream); }
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

static void compile_args(es_ctx_t* ctx, es_val_t bc, es_val_t args, es_val_t scope) {
  es_bytecode_t* p = es_to_bytecode(bc);
  if (!es_is_nil(args)) {
    compile(ctx, bc, es_car(args), 0, 0, scope);
    compile_args(ctx, bc, es_cdr(args), scope);
  }
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

static void compile_lambda(es_ctx_t* ctx, 
                           es_val_t  bc, 
                           es_val_t  formals, 
                           es_val_t  body, 
                           int       tail_pos, 
                           int       next, 
                           es_val_t  scope) {
  es_bytecode_t* p = es_to_bytecode(bc);
  int label1 = label(p); 
  emit_jmp(p, -1);
  int label2 = label(p);
  scope = mkscope(ctx, formals, scope);
  while(!es_is_nil(es_cdr(body))) {
    compile(ctx, bc, es_car(body), 0, 0, scope);
    emit_pop(p);
    body = es_cdr(body);
  }
  compile(ctx, bc, es_car(body), 1, RETURN, scope);
  int label3 = label(p);
  int rest;
  int arity = lambda_arity(formals, &rest);
  es_val_t proc = es_proc_new(ctx, arity, rest, label2);
  emit_closure(p, alloc_const(p, proc));
  p->inst[label1 + 1] = label3 - label1;
  if (tail_pos) emit_byte(p, next);
}

static void compile(es_ctx_t* ctx, 
                    es_val_t  bc, 
                    es_val_t  exp, 
                    int       tail_pos, 
                    int       next, 
                    es_val_t  scope) {
  es_bytecode_t* p = es_to_bytecode(bc);
  if (es_is_pair(exp)) {
    es_val_t op   = es_car(exp);
    es_val_t args = es_cdr(exp);
    if (es_is_symbol(op)) {
      if (es_is_eq(op, symbol_define)) {
        if (es_is_symbol(es_car(args))) {
          compile(ctx, bc, es_cadr(args), 0, 0, scope);
          emit_global_set(p, alloc_const(p, es_car(args)));
          if (tail_pos) emit_byte(p, next);
        } else if (es_is_pair(es_car(args))) {
          es_val_t sym     = es_caar(args);
          es_val_t formals = es_cdar(args);
          es_val_t body    = es_cdr(args);
          compile_lambda(ctx, bc, formals, body, 0, 0, scope);
          emit_global_set(p, alloc_const(p, sym));
          if (tail_pos) emit_byte(p, next);
        } else if (es_is_eq(op, symbol_begin)) {
          while(!es_is_nil(es_cdr(args))) {
            compile(ctx, bc, es_car(args), 0, 0, scope);
            emit_pop(p);
            args = es_cdr(args);
          }
          compile(ctx, bc, es_car(args), tail_pos, next, scope);
        }
      } else if (es_is_eq(op, symbol_if)) {
        es_val_t cond  = es_car(args);
        es_val_t bthen = es_cadr(args);
        es_val_t belse = es_caddr(args);
        compile(ctx, bc, cond, 0, 0, scope);
        int label1 = label(p); 
        emit_bf(p, -1);
        compile(ctx, bc, bthen, tail_pos, next, scope);
        int label2 = label(p); 
        if (!tail_pos) emit_jmp(p, -1);
        int label3 = label(p); 
        compile(ctx, bc, belse, tail_pos, next, scope);
        int label4 = label(p);
        p->inst[label1 + 1] = label3 - label1;
        if (!tail_pos) p->inst[label2 + 1] = label4 - label2;
      } else if (es_is_eq(op, symbol_lambda)) {
        es_val_t formals = es_car(args);
        es_val_t body    = es_cdr(args);
        compile_lambda(ctx, bc, formals, body, tail_pos, next, scope);
      } else if (es_is_eq(op, symbol_begin)) {
        while(!es_is_nil(es_cdr(args))) {
          compile(ctx, bc, es_car(args), 0, 0, scope);
          emit_pop(p);
          args = es_cdr(args);
        }
        compile(ctx, bc, es_car(args), tail_pos, next, scope);
      } else if (es_is_eq(op, symbol_set)) {
        compile(ctx, bc, es_cadr(args), 0, 0, scope);
        int idx, depth;
        if (arg_idx(scope, es_car(args), &idx, &depth))
          if (depth == 0)
            emit_arg_set(p, idx);
          else
            emit_closed_set(p, depth, idx);
        else
          emit_global_set(p, alloc_const(p, es_car(args)));
        if (tail_pos) emit_byte(p, next);
      } else if (es_is_eq(op, symbol_quote)) {
        emit_const(p, alloc_const(p, es_car(args)));
        if (tail_pos) emit_byte(p, next);
      } else {
        int argc = es_list_length(args);
        compile_args(ctx, bc, args, scope);
        compile(ctx, bc, op, 0, 0, scope);
        if (tail_pos)
          emit_tail_call(p, argc);
        else
          emit_call(p, argc);
      }
    } else {
      int argc = es_list_length(args);
      compile_args(ctx, bc, args, scope);
      compile(ctx, bc, op, 0, 0, scope);
      if (tail_pos)
        emit_tail_call(p, argc);
      else
        emit_call(p, argc);
    }
  } else if(es_is_symbol(exp)) {
    int idx, depth;
    if (arg_idx(scope, exp, &idx, &depth))
      if (depth == 0)
        emit_arg_ref(p, idx);
      else
        emit_closed_ref(p, depth, idx);
    else
      emit_global_ref(p, alloc_const(p, exp));
    if (tail_pos) emit_byte(p, next);
  } else {
    emit_const(p, alloc_const(p, exp));
    if (tail_pos) emit_byte(p, next);
  }
}

/*
static void print_stack(es_ctx_t* ctx, int count, es_val_t* args) {
  printf("[");
  for(int i = 0; i < count; i++) {
    es_print(ctx, args[i], ctx->oport); if ( i < count - 1) printf(", ");
  }
  printf("]");
}*/

//=============
// VM
//=============
#define pop()           state.stack[--state.sp] 
#define push(v)         state.stack[state.sp++] = v
#define get_const(idx)  es_to_bytecode(ctx->bytecode)->cpool[idx]
#define get_arg(idx)    es_to_args(state.env)->args[idx]
#define set_arg(idx, v) es_to_args(state.env)->args[idx] = v
#define get_env()       state.env
#define get_genv()      state.genv
#define get_sp()        state.sp
#define get_stack()     state.stack
#define restore()       state.fp--; state.env = state.frames[state.fp].env; ip = state.frames[state.fp].knt;
#define save()          state.frames[state.fp].env = state.env; state.frames[state.fp].knt = ip; state.fp++;
#define next()          goto *opcode_addrs[(int)*ip] /*break*/

static es_val_t es_vm(es_ctx_t* ctx, int start, es_val_t genv) {
  es_state_t state;
  state.genv  = genv;
  state.env   = es_nil;
  state.sp    = 0;
  state.fp    = 0;

  char* inst = es_to_bytecode(ctx->bytecode)->inst;
  char* ip   = inst + start;

  ctx->state = &state;

  static void* opcode_addrs[] = {
    &&HALT, &&CONST, &&POP, &&GLOBAL_REF, &&GLOBAL_SET, &&CLOSED_REF, &&CLOSED_SET, 
    &&ARG_REF, &&ARG_SET, &&JMP, &&BF, &&CALL, &&TAIL_CALL, &&RETURN, &&CLOSURE
  };

  while(1) {
    //getc(stdin);
    //printf("%4d : ", ip); print_inst(inst + ip); printf(", "); print_stack(ctx, sp, stack); printf(", fp: %d\n", fp);
    switch(*ip) {
    HALT: case HALT: ctx->state = NULL;
      return pop();
      break;
    CONST: case CONST:
      push(get_const((int)*(ip + 1))); 
      ip += 2; 
      next();
    POP: case POP:
      pop(); 
      ip++;
      next();
    BF: case BF:
      ip += es_is_true(pop()) ? 2 : *(ip + 1);
      next();
    JMP: case JMP:
      ip += *(ip + 1);
      next();
    GLOBAL_REF: case GLOBAL_REF:
      push(es_lookup_symbol(ctx, get_genv(), get_const((int)*(ip + 1)))); 
      ip += 2; 
      next();
    GLOBAL_SET: case GLOBAL_SET: { 
      es_val_t v = pop(); 
      push(es_define_symbol(ctx, get_genv(), get_const((int)*(ip + 1)), v)); 
      ip += 2;
      next();
    }
    CLOSED_REF: case CLOSED_REF: {
      es_val_t   cenv  = get_env();
      int        depth = *(ip + 1); 
      int        idx   = *(ip + 2);
      while(depth-- > 0) 
        cenv = es_to_args(cenv)->parent;
      push(es_to_args(cenv)->args[idx]);
      ip += 3;
      next();
    }
    CLOSED_SET: case CLOSED_SET: {
      es_val_t   cenv  = get_env();
      int        depth = *(ip + 1); 
      int        idx   = *(ip + 2);
      while(depth-- > 0) 
        cenv = es_to_args(cenv)->parent;
      es_to_args(cenv)->args[idx] = pop();
      push(es_void);
      ip += 3;
      next();
    }
    ARG_REF: case ARG_REF:    
      push(get_arg((int)*(ip + 1))); 
      ip += 2; 
      next();
    ARG_SET: case ARG_SET:
      set_arg((int)*(ip + 1), pop()); 
      ip += 2;
      next();
    CLOSURE: case CLOSURE:
      push(es_closure_new(ctx, get_env(), get_const((int)*(ip + 1)))); 
      ip += 2;
      next();
    RETURN: case RETURN: 
      restore();
      next();
    CALL: case CALL: { 
      int argc = *(ip + 1);
      ip += 2;
      es_val_t proc = pop();
      if (es_is_fn(proc)) { 
        es_val_t res = es_fn_apply_argv(ctx, proc, argc, get_stack() + get_sp() - argc); 
        get_sp() -= argc;
        push(res);
      } else if (es_is_closure(proc)) {
        es_closure_t* closure = es_to_closure(proc);
        es_proc_t* proc = es_obj_to(es_proc_t*, closure->proc);
        save();
        get_env() = es_args_new(ctx, closure->env, proc->arity, proc->rest, argc, get_stack() + get_sp() - argc);
        get_sp() -= argc;
        ip = inst + proc->addr;
      }
      next();
    }
    TAIL_CALL: case TAIL_CALL: {
      int argc = *(ip + 1); 
      ip += 2;
      es_val_t proc = pop();
      if (es_is_fn(proc)) { 
        es_val_t res = es_fn_apply_argv(ctx, proc, argc, get_stack() + get_sp() - argc); 
        get_sp() -= argc; 
        push(res);
        restore();
      } else if (es_is_closure(proc)) {
        es_closure_t* closure = es_to_closure(proc);
        es_proc_t* proc = es_obj_to(es_proc_t*, closure->proc);
        get_env() = es_args_new(ctx, closure->env, proc->arity, proc->rest, argc, get_stack() + get_sp() - argc);
        get_sp() -= argc;
        ip = inst + proc->addr;
      }
      next();
    }
    }
  }
  return pop();
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
  es_bytecode_t* p = es_to_bytecode(b);
  start = p->ip;
  compile(ctx, b, exp, 0, 0, es_nil);
  emit_halt(p);
  return start;
}

ES_API es_val_t es_load(es_ctx_t* ctx, char* file_name) {
  es_val_t exp, port;
  port = es_port_new(ctx, fopen(file_name, "r"));
  while(!es_is_eof_obj(exp = es_read(ctx, port))) {
    es_eval(ctx, exp, ctx->env);
  }
  es_port_close(port);
  return es_void;
}

ES_API es_val_t es_eval(es_ctx_t* ctx, es_val_t exp, es_val_t env) {
  //struct timeval t0, t1, dt;
  es_val_t res;
  int start = es_compile(ctx, exp);
  //es_print(ctx, ctx->bytecode, ctx->oport);
  //es_port_printf(ctx, ctx->oport, "\n");

  //gettimeofday(&t0, NULL);
  res = es_vm(ctx, start, ctx->env);  
  //gettimeofday(&t1, NULL);
  //timeval_subtract(&dt, &t1, &t0);
  //printf("time: %f\n", dt.tv_sec * 1000.0 + dt.tv_usec / 1000.0);

  return res;
}

static es_val_t cfn_bytecode(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return ctx->bytecode; 
}

static es_val_t cfn_env(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return ctx->env; 
}

static es_val_t cfn_write(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  es_print(ctx, argv[0], argv[0]); return es_void; 
}

static es_val_t cfn_cons(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_cons(ctx, argv[0], argv[1]); 
}

static es_val_t cfn_car(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_car(argv[0]); 
}

static es_val_t cfn_cdr(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_cdr(argv[0]); 
}

static es_val_t cfn_add(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_add(argv[0], argv[1]); 
}

static es_val_t cfn_sub(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_sub(argv[0], argv[1]); 
}

static es_val_t cfn_mul(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_mul(argv[0], argv[1]); 
}

static es_val_t cfn_div(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_number_div(argv[0], argv[1]); 
}

static es_val_t cfn_num_is_eq(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_number_is_eq(argv[0], argv[1])); 
}

static es_val_t cfn_is_boolean(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_boolean(argv[0])); 
}

static es_val_t cfn_is_symbol(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_symbol(argv[0])); 
}

static es_val_t cfn_is_char(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_char(argv[0])); 
}

static es_val_t cfn_is_vec(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_vec(argv[0])); 
}

static es_val_t cfn_is_procedure(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_fn(argv[0]) || es_is_closure(argv[0])); 
}

static es_val_t cfn_is_pair(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_pair(argv[0])); 
}

static es_val_t cfn_is_number(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_fixnum(argv[0])); 
}

static es_val_t cfn_is_string(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_string(argv[0])); 
}

static es_val_t cfn_is_port(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_port(argv[0])); 
}

static es_val_t cfn_is_null(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_nil(argv[0])); 
}

static es_val_t cfn_is_eq(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_boolean_new(es_is_eq(argv[0], argv[1])); 
}

static es_val_t cfn_quit(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  return es_eof_obj; 
}

static es_val_t cfn_gc(es_ctx_t* ctx, int argc, es_val_t* argv) { 
  es_gc(ctx); 
  return es_void;
}

static es_val_t cfn_read_char(es_ctx_t* ctx, int argc, es_val_t* argv) {
  return es_port_read_char(argc == 0 ? ctx->iport : argv[0]);
}

static es_val_t cfn_close(es_ctx_t* ctx, int argc, es_val_t* argv) {
  es_port_t* port = es_to_port(argv[0]);
  fclose(port->stream);
  port->stream = NULL;
  return es_void;
}

static es_val_t cfn_compile(es_ctx_t* ctx, int argc, es_val_t argv[]) {
  es_val_t b = es_bytecode_new(ctx);
  compile(ctx, b, argv[0], 0, 0, es_nil);
  es_bytecode_t* p = es_to_bytecode(b);
  emit_halt(p);
  return b;
}

static void ctx_init_env(es_ctx_t* ctx) {
  es_define(ctx, "bytecode",   es_fn_new(ctx, 0, cfn_bytecode));
  es_define(ctx, "global-env", es_fn_new(ctx, 0, cfn_env));
  es_define(ctx, "cons",       es_fn_new(ctx, 2, cfn_cons));
  es_define(ctx, "car",        es_fn_new(ctx, 1, cfn_car));
  es_define(ctx, "cdr",        es_fn_new(ctx, 1, cfn_cdr));
  es_define(ctx, "+",          es_fn_new(ctx, 2, cfn_add));
  es_define(ctx, "*",          es_fn_new(ctx, 2, cfn_mul));
  es_define(ctx, "/",          es_fn_new(ctx, 2, cfn_div));
  es_define(ctx, "compile",    es_fn_new(ctx, 1, cfn_compile));
  es_define(ctx, "boolean?",   es_fn_new(ctx, 1, cfn_is_boolean));
  es_define(ctx, "symbol?",    es_fn_new(ctx, 1, cfn_is_symbol));
  es_define(ctx, "char?",      es_fn_new(ctx, 1, cfn_is_char));
  es_define(ctx, "vector?",    es_fn_new(ctx, 1, cfn_is_vec));
  es_define(ctx, "procedure?", es_fn_new(ctx, 1, cfn_is_procedure));
  es_define(ctx, "pair?",      es_fn_new(ctx, 1, cfn_is_pair));
  es_define(ctx, "number?",    es_fn_new(ctx, 1, cfn_is_number));
  es_define(ctx, "string?",    es_fn_new(ctx, 1, cfn_is_string));
  es_define(ctx, "port?",      es_fn_new(ctx, 1, cfn_is_port));
  es_define(ctx, "null?",      es_fn_new(ctx, 2, cfn_is_null));
  es_define(ctx, "eq?",        es_fn_new(ctx, 2, cfn_is_eq));
  es_define(ctx, "quit",       es_fn_new(ctx, 2, cfn_quit));
  es_define(ctx, "gc",         es_fn_new(ctx, 0, cfn_gc));
  es_define(ctx, "write",      es_fn_new(ctx, 2, cfn_write));
  es_define(ctx, "read-char",  es_fn_new(ctx, 2, cfn_read_char));
  es_define(ctx, "close",      es_fn_new(ctx, 1, cfn_close));
  es_define(ctx, "-",          es_fn_new(ctx, 2, cfn_sub));
  es_define(ctx, "=",          es_fn_new(ctx, 2, cfn_num_is_eq));
}