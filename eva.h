//=============================================================================
/* eva.h
 *
 * Copyright (C) 2014 Pedro Oñate
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to 
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 * THE SOFTWARE.
 */
//===========================================================================

#ifndef __EVA_H__
#define __EVA_H__

#ifdef __cplusplus
  extern "C" {
#endif

#include <inttypes.h>
#include <stdio.h>

#define es_version_str "0.3.0"

typedef struct es_ctx     es_ctx_t;
typedef enum   es_type    es_type_t;
typedef union  es_val     es_val_t;
typedef struct es_obj     es_obj_t;
typedef struct es_pair    es_pair_t;
typedef struct es_string  es_string_t;
typedef struct es_fn      es_fn_t;
typedef struct es_closure es_closure_t;
typedef struct es_vec     es_vec_t;
typedef struct es_proc    es_proc_t;
typedef struct es_port    es_port_t;
typedef struct es_error   es_error_t;
typedef struct es_env     es_env_t;
typedef struct es_args    es_args_t;
typedef union  es_pfn     es_pfn_t;

/**
 * Wrapper for first-class objects
 */
union es_val {
  es_obj_t* obj;
  uintptr_t val;
};

/*
 * Function pointer for native c functions
 */
typedef es_val_t (*es_pcfn_t)(es_ctx_t* ctx, int argc, es_val_t* argv);

enum es_type {
  es_invalid_type = -1, /**< Invalid type. */
  es_nil_type,          /**< Nil type. */
  es_boolean_type,      /**< Boolean type. */
  es_fixnum_type,       /**< Type of fixnum integers. */
  es_symbol_type,       /**< Type of symbols. */
  es_char_type,         /**< Type of characters. */
  es_string_type,       /**< Type of strings. */
  es_pair_type,         /**< Type of pairs. */
  es_eof_obj_type,      /**< Type of the EOF object */
  es_closure_type,      /**< Type of closures. */
  es_unbound_type,
  es_defined_type,      /**< Type of undefined symbols. */
  es_undefined_type,
  es_void_type,         /**< Unspecified type. */
  es_port_type,         /**< Type of port objects. */
  es_vector_type,       /**< Vector type. */
  es_fn_type,
  es_env_type,
  es_args_type,
  es_proc_type,
  es_bytecode_type,
  es_error_type
};

extern const es_val_t es_nil;
extern const es_val_t es_true;
extern const es_val_t es_false;
extern const es_val_t es_eof_obj;
extern const es_val_t es_void;
extern const es_val_t es_unbound;
extern const es_val_t es_defined;
extern const es_val_t es_undefined;

es_ctx_t*     es_ctx_new(size_t heap_size);
void          es_ctx_free(es_ctx_t* ctx);
es_val_t      es_ctx_iport(es_ctx_t* ctx);
es_val_t      es_ctx_oport(es_ctx_t* ctx);
es_val_t      es_ctx_env(es_ctx_t* ctx);
void          es_ctx_set_iport(es_ctx_t* ctx, es_val_t port);
void          es_ctx_set_oport(es_ctx_t* ctx, es_val_t port);
void          es_ctx_set_env(es_ctx_t* ctx, es_val_t env);

es_type_t     es_type_of(es_val_t value);
size_t        es_size_of(es_val_t value);
int           es_is_eq(es_val_t v1, es_val_t v2);
int           es_is_obj(es_val_t val);
int           es_is_immediate(es_val_t val);
int           es_is_pair(es_val_t val);
int           es_is_fixnum(es_val_t val);
int           es_is_char(es_val_t val);
int           es_is_unspecified(es_val_t val);
int           es_is_unbound(es_val_t val);
int           es_is_nil(es_val_t val);
int           es_is_boolean(es_val_t val);
int           es_is_string(es_val_t val);
int           es_is_true(es_val_t val);
int           es_is_vec(es_val_t val);
int           es_is_closure(es_val_t value);
int           es_is_port(es_val_t val);
int           es_is_error(es_val_t val);
int           es_is_symbol(es_val_t val);
int           es_is_eof_obj(es_val_t val);
int           es_is_fn(es_val_t val);
int           es_is_proc(es_val_t val);
int           es_is_bytecode(es_val_t val);
long          es_to_fixnum(es_val_t val);
int           es_to_boolean(es_val_t val);
int           es_to_char(es_val_t val);
int           es_to_symbol(es_val_t val);
es_string_t*  es_to_string(es_val_t val);
es_obj_t*     es_to_obj(es_val_t val);
es_pair_t*    es_to_pair(es_val_t val);
es_vec_t*     es_to_vec(es_val_t val);
es_closure_t* es_to_closure(es_val_t val);
es_port_t*    es_to_port(es_val_t val);
es_error_t*   es_to_error(es_val_t val);
es_env_t*     es_to_env(es_val_t val);

es_val_t      es_fixnum_new(long val);
es_val_t      es_fixnum_new_cstr(char* val);
es_val_t      es_fixnum_new_cbuf(char* buf, size_t bytes);
es_val_t      es_boolean_new(int val);
es_val_t      es_char_new(int charcode);
es_val_t      es_char_str(char* buf);
es_val_t      es_string_new(es_ctx_t* ctx, char* cstr);
es_val_t      es_pair_new(es_ctx_t* ctx, es_val_t head, es_val_t tail);
es_val_t      es_list(es_ctx_t* ctx, ...);
es_val_t      es_vec_new(es_ctx_t* ctx, int size);
es_val_t      es_vec_from_list(es_ctx_t* ctx, es_val_t list);
es_val_t      es_closure_new(es_ctx_t* ctx, es_val_t env, es_val_t proc);
es_val_t      es_port_new(es_ctx_t* ctx, FILE* stream);
es_val_t      es_bytecode_new(es_ctx_t* ctx);
es_val_t      es_error_new(es_ctx_t* ctx, char* errstr);
es_val_t      es_symbol_new(int id);
es_val_t      es_env_new(es_ctx_t* ctx, int size);
es_val_t      es_fn_new(es_ctx_t* ctx, int arity, es_pcfn_t pcfn);

es_val_t      es_number_add(es_val_t a, es_val_t b);
es_val_t      es_number_sub(es_val_t a, es_val_t b);
es_val_t      es_number_mul(es_val_t a, es_val_t b);
es_val_t      es_number_div(es_val_t a, es_val_t b);
int           es_number_is_eq(es_val_t a, es_val_t b);

es_val_t      es_pair_car(es_val_t pair);
es_val_t      es_pair_cdr(es_val_t pair);
void          es_pair_set_head(es_val_t pair, es_val_t val);
void          es_pair_set_tail(es_val_t pair, es_val_t val);
es_val_t      es_assq(es_val_t lst, es_val_t key);
es_val_t      es_list_argv(es_ctx_t* ctx, int argc, es_val_t* argv);
int           es_list_length(es_val_t list);

void          es_vec_set(es_val_t vector, int idx, es_val_t element);
es_val_t      es_vec_ref(es_val_t vector, int idx);
int           es_vec_len(es_val_t vector);

void          es_port_close(es_val_t port);
es_val_t      es_port_read(es_ctx_t* ctx, es_val_t port);
es_val_t      es_port_read_char(es_val_t port);
es_val_t      es_port_peek_char(es_val_t port);
int           es_port_getc(es_val_t port);
int           es_port_peekc(es_val_t port);
void          es_port_close(es_val_t port);
int           es_port_linum(es_val_t port);
int           es_port_colnum(es_val_t port);
es_val_t      es_port_write_char(es_val_t port, es_val_t c);
es_val_t      es_port_write(es_ctx_t* ctx, es_val_t port, es_val_t val);
es_val_t      es_port_printf(es_ctx_t* ctx, es_val_t port, const char* fmt, ...);

es_val_t      es_symbol_intern(es_ctx_t* ctx, char* cstr);
es_val_t      es_symbol_to_string(es_ctx_t* ctx, es_val_t val);

es_val_t      es_read(es_ctx_t* ctx, es_val_t port);
void          es_print(es_ctx_t* ctx, es_val_t exp, es_val_t port);

es_val_t      es_define(es_ctx_t* ctx, char* symbol, es_val_t value);
es_val_t      es_define_symbol(es_ctx_t* ctx, es_val_t env, es_val_t symbol, es_val_t value);
es_val_t      es_lookup_symbol(es_ctx_t* ctx, es_val_t env, es_val_t symbol);

es_val_t      es_eval(es_ctx_t* ctx, es_val_t exp, es_val_t env);
es_val_t      es_load(es_ctx_t* ctx, char* file);

void          es_gc(es_ctx_t* ctx);
void          es_gc_root_p(es_ctx_t* ctx, es_val_t* pv);
void          es_gc_unroot(es_ctx_t* ctx, int n);
#define       es_gc_root(c, v) es_gc_root_p(c, &(v))

#define es_cons(ctx, a, b)    es_pair_new(ctx, a, b)
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