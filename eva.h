/*
  eva.h

  Copyright (C) 2014 Pedro Oñate

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to
  deal in the Software without restriction, including without limitation the
  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
  sell copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#ifndef __EVA_H__
#define __EVA_H__

#ifdef __cplusplus
  extern "C" {
#endif

#include <inttypes.h>
#include <stdio.h>

#define ES_VERSION_STR "0.3.1"

typedef struct es_ctx es_ctx_t;
typedef enum es_type  es_type_t;
typedef uintptr_t     es_val_t;

/* Function pointer for native c functions */
typedef es_val_t (*es_pfn_t)(es_ctx_t* ctx, int argc, es_val_t argv[]);

//=====================
// Types
//=====================
enum es_type {
  ES_INVALID_TYPE = -1,
  ES_NIL_TYPE,
  ES_BOOL_TYPE,
  ES_FIXNUM_TYPE,
  ES_SYMBOL_TYPE,
  ES_CHAR_TYPE,
  ES_STRING_TYPE,
  ES_PAIR_TYPE,
  ES_EOF_OBJ_TYPE,
  ES_CLOSURE_TYPE,
  ES_UNBOUND_TYPE,
  ES_UNDEFINED_TYPE,
  ES_VOID_TYPE,
  ES_PORT_TYPE,
  ES_VECTOR_TYPE,
  ES_FN_TYPE,
  ES_ENV_TYPE,
  ES_ARGS_TYPE,
  ES_PROC_TYPE,
  ES_BYTECODE_TYPE,
  ES_CONT_TYPE,
  ES_MACRO_TYPE,
  ES_BUFFER_TYPE,
  ES_ERROR_TYPE
};

extern const es_val_t es_nil;
extern const es_val_t es_true;
extern const es_val_t es_false;
extern const es_val_t es_eof_obj;
extern const es_val_t es_void;
extern const es_val_t es_unbound;
extern const es_val_t es_undefined;

//=====================
// Context
//=====================
es_ctx_t* es_ctx_new(size_t heap_size);
void      es_ctx_free(es_ctx_t* ctx);
es_val_t  es_ctx_iport(es_ctx_t* ctx);
es_val_t  es_ctx_oport(es_ctx_t* ctx);
es_val_t  es_ctx_env(es_ctx_t* ctx);
void      es_ctx_set_iport(es_ctx_t* ctx, es_val_t port);
void      es_ctx_set_oport(es_ctx_t* ctx, es_val_t port);
void      es_ctx_set_env(es_ctx_t* ctx, es_val_t env);

//=====================
// Constructors
//=====================
es_val_t  es_make_fixnum(int val);
es_val_t  es_make_bool(int val);
es_val_t  es_make_char(int charcode);
es_val_t  es_make_char_cstr(char* cstr);
es_val_t  es_make_string(es_ctx_t* ctx, char* cstr);
es_val_t  es_make_pair(es_ctx_t* ctx, es_val_t head, es_val_t tail);
es_val_t  es_make_list(es_ctx_t* ctx, ... /* terminate w/ NULL */);
es_val_t  es_make_vec(es_ctx_t* ctx, int size);
es_val_t  es_make_vec_from_list(es_ctx_t* ctx, es_val_t list);
es_val_t  es_make_closure(es_ctx_t* ctx, es_val_t env, es_val_t proc);
es_val_t  es_make_port(es_ctx_t* ctx, FILE* stream);
es_val_t  es_make_bytecode(es_ctx_t* ctx);
es_val_t  es_make_error(es_ctx_t* ctx, char* msg);
es_val_t  es_make_symbol(int id);
es_val_t  es_make_cont(es_ctx_t* ctx);
es_val_t  es_make_macro(es_ctx_t* ctx, es_val_t trans);
es_val_t  es_make_env(es_ctx_t* ctx, int size);
es_val_t  es_make_fn(es_ctx_t* ctx, int arity, es_pfn_t pcfn);
es_val_t  es_make_buffer(es_ctx_t* ctx, size_t size);
es_val_t  es_symbol_intern(es_ctx_t* ctx, const char* cstr);
es_val_t  es_symbol_to_string(es_ctx_t* ctx, es_val_t val);
es_val_t  es_gensym(es_ctx_t* ctx);

//=====================
// Predicates
//=====================
int       es_is_eq(es_val_t val1, es_val_t val2);
int       es_is_pair(es_val_t val);
int       es_is_fixnum(es_val_t val);
int       es_is_char(es_val_t val);
int       es_is_nil(es_val_t val);
int       es_is_boolean(es_val_t val);
int       es_is_string(es_val_t val);
int       es_is_true(es_val_t val);
int       es_is_vector(es_val_t val);
int       es_is_closure(es_val_t val);
int       es_is_port(es_val_t val);
int       es_is_error(es_val_t val);
int       es_is_symbol(es_val_t val);
int       es_is_eof_obj(es_val_t val);
int       es_is_fn(es_val_t val);
int       es_is_proc(es_val_t val);
int       es_is_cont(es_val_t val);
int       es_is_bytecode(es_val_t val);
int       es_is_unspecified(es_val_t val);
int       es_is_unbound(es_val_t val);
int       es_is_macro(es_val_t val);

//=====================
// Selectors
//=====================
es_type_t es_type_of(es_val_t val);
size_t    es_size_of(es_val_t val);
es_val_t  es_pair_car(es_val_t pair);
es_val_t  es_pair_cdr(es_val_t pair);
void      es_pair_set_head(es_val_t pair, es_val_t val);
void      es_pair_set_tail(es_val_t pair, es_val_t val);
es_val_t  es_list_argv(es_ctx_t* ctx, int argc, es_val_t* argv);
int       es_list_length(es_val_t list);
void      es_vector_set(es_val_t vec, int idx, es_val_t val);
es_val_t  es_vector_ref(es_val_t vec, int idx);
int       es_vector_len(es_val_t vec);
int       es_string_ref(es_val_t str, int k);
int       es_fixnum_val(es_val_t val);
int       es_bool_val(es_val_t val);
int       es_char_val(es_val_t val);
int       es_symbol_val(es_val_t val);

//=====================
// Numerics
//=====================
es_val_t  es_number_add(es_ctx_t* ctx, es_val_t a, es_val_t b);
es_val_t  es_number_sub(es_ctx_t* ctx, es_val_t a, es_val_t b);
es_val_t  es_number_mul(es_ctx_t* ctx, es_val_t a, es_val_t b);
es_val_t  es_number_div(es_ctx_t* ctx, es_val_t a, es_val_t b);
int       es_number_is_eq(es_val_t a, es_val_t b);

//=====================
// I/O
//=====================
es_val_t  es_read(es_ctx_t* ctx);
void      es_write(es_ctx_t* ctx, es_val_t exp);
void      es_printf(es_ctx_t* ctx, const char * fmt, ...);
void      es_port_close(es_val_t port);
es_val_t  es_port_read(es_ctx_t* ctx, es_val_t port);
es_val_t  es_port_read_char(es_val_t port);
es_val_t  es_port_peek_char(es_val_t port);
int       es_port_getc(es_val_t port);
int       es_port_peekc(es_val_t port);
void      es_port_reset(es_val_t port);
void      es_port_close(es_val_t port);
int       es_port_nbytes(es_val_t port);
int       es_port_linum(es_val_t port);
int       es_port_colnum(es_val_t port);
es_val_t  es_port_write_char(es_val_t port, es_val_t c);
es_val_t  es_port_write(es_ctx_t* ctx, es_val_t port, es_val_t val);
es_val_t  es_port_printf(es_ctx_t* ctx, es_val_t port, const char* fmt, ...);

//=====================
// Environment
//=====================
es_val_t  es_define(es_ctx_t* ctx, char* name, es_val_t value);
es_val_t  es_define_fn(es_ctx_t* ctx, char* name, es_pfn_t fn, int arity);
es_val_t  es_define_symbol(es_ctx_t* ctx, es_val_t env, es_val_t symbol, es_val_t value);
es_val_t  es_lookup_symbol(es_ctx_t* ctx, es_val_t env, es_val_t sym);

//=====================
// Evaluation
//=====================
es_val_t  es_eval(es_ctx_t* ctx, es_val_t exp);
es_val_t  es_apply(es_ctx_t* ctx, es_val_t proc, es_val_t args);
es_val_t  es_load(es_ctx_t* ctx, const char* file_name);

//=====================
// GC
//=====================
void      es_gc(es_ctx_t* ctx);
void      es_gc_root_p(es_ctx_t* ctx, es_val_t* pv);
void      es_gc_unroot(es_ctx_t* ctx, int n);
#define   es_gc_root(c, v) es_gc_root_p(c, (es_val_t*)&(v))

#define es_cons(ctx, a, b)    es_make_pair(ctx, a, b)
#define es_car(e)             es_pair_car(e)
#define es_cdr(e)             es_pair_cdr(e)
#define es_caar(e)            es_car(es_car(e))
#define es_cadr(e)            es_car(es_cdr(e))
#define es_cdar(e)            es_cdr(es_car(e))
#define es_cddr(e)            es_cdr(es_cdr(e))
#define es_caadr(e)           es_car(es_cadr(e))
#define es_caddr(e)           es_car(es_cddr(e))
#define es_cdadr(e)           es_cdr(es_cadr(e))
#define es_cdddr(e)           es_cdr(es_cddr(e))
#define es_cadddr(e)          es_car(es_cdddr(e))
#define es_set_car(e, v)      es_pair_set_head(e, v)
#define es_set_cdr(e, v)      es_pair_set_tail(e, v)

#ifdef __cplusplus
  }
#endif

#endif