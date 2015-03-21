/**
 * eva.h
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
typedef es_val_t (*es_pfn_t)(es_ctx_t* ctx, int argc, es_val_t* argv);

enum es_type {
  es_invalid_type = -1, /**< Invalid type. */
  es_nil_type,          /**< Type of the nil value */
  es_boolean_type,      /**< Type of boolean values. */
  es_fixnum_type,       /**< Type of fixnum integers. */
  es_symbol_type,       /**< Type of symbols. */
  es_char_type,         /**< Type of characters. */
  es_string_type,       /**< Type of strings. */
  es_pair_type,         /**< Type of pairs. */
  es_eof_obj_type,      /**< Type of the EOF object */
  es_closure_type,      /**< Type of closures. */
  es_unbound_type,      /**< Type of unbound locations */
  es_defined_type,      /**< Type of undefined symbols. */
  es_undefined_type,    /**< Type of the undefined value */
  es_void_type,         /**< Unspecified type. */
  es_port_type,         /**< Type of port objects. */
  es_vector_type,       /**< Vector type. */
  es_fn_type,           /**< Type of native procedures */
  es_env_type,          /**< Type of top-level environments */
  es_args_type,         /**< Type of procedure arguments */
  es_proc_type,         /**< Type of compiled procedures */
  es_bytecode_type,     /**< Type of bytecode */
  es_error_type         /**< Type of errors */
};

extern const es_val_t es_nil;
extern const es_val_t es_true;
extern const es_val_t es_false;
extern const es_val_t es_eof_obj;
extern const es_val_t es_void;
extern const es_val_t es_unbound;
extern const es_val_t es_defined;
extern const es_val_t es_undefined;

/**
 * Creates and returns a new context
 *
 * @param heap_size The size of the heap in bytes
 * @return          The context
 */
es_ctx_t*     es_ctx_new(size_t heap_size);

/**
 * Frees the context and it's resources
 *
 * @param ctx A context
 * @return    The context
 */
void          es_ctx_free(es_ctx_t* ctx);

/**
 * Returns the default input port
 *
 * @param ctx A context
 * @return    The default input port
 */
es_val_t      es_ctx_iport(es_ctx_t* ctx);

/**
 * Returns the default output port
 *
 * @param ctx The context
 * @return    The default output port
 */
es_val_t      es_ctx_oport(es_ctx_t* ctx);

/**
 * Returns the global environment
 *
 * @param ctx A context
 * @return    The global environment
 */
es_val_t      es_ctx_env(es_ctx_t* ctx);

/**
 * Sets the default input port
 *
 * @param ctx  A context
 * @param port An input port
 */
void          es_ctx_set_iport(es_ctx_t* ctx, es_val_t port);

/**
 * Sets the default output port
 *
 * @param ctx  A context
 * @param port An output port
 */
void          es_ctx_set_oport(es_ctx_t* ctx, es_val_t port);

/**
 * Sets the global environment
 *
 * @param ctx A context
 * @param env The environment
 */
void          es_ctx_set_env(es_ctx_t* ctx, es_val_t env);

/**
 * Returns a type for a given value
 *
 * @param val A value
 * @return    The type of this value
 */
es_type_t     es_type_of(es_val_t val);

/**
 * Returns the size in bytes of this value
 *
 * @param val A value
 * @return    Size of this value in bytes
 */
size_t        es_size_of(es_val_t val);

/**
 * Checks if two values are equal
 *
 * @param val1 The first value
 * @param val2 The second value
 * @return     Non-zero if equal, otherwise 0
 */
int           es_is_eq(es_val_t val1, es_val_t val2);

/**
 * Checks if value is a pair
 *
 * @param val A value
 * @return    True if value is a pair
 */
int           es_is_pair(es_val_t val);

/**
 * Checks if value is a fixed precision integer
 *
 * @param val A value
 * @return    True if value is a fixnum
 */
int           es_is_fixnum(es_val_t val);

/**
 * Checks if value is a character
 *
 * @param val A value
 * @return    True if value is a character
 */
int           es_is_char(es_val_t val);

/**
 * Checks if value is nil
 *
 * @param val A value
 * @return    True if value is nil
 */
int           es_is_nil(es_val_t val);

/**
 * Checks if value is a boolean
 *
 * @param val A value
 * @return    True if value is a boolean
 */
int           es_is_boolean(es_val_t val);

/**
 * Checks if value is a string
 *
 * @param val A value
 * @return    True if value is a string
 */
int           es_is_string(es_val_t val);

/**
 * Checks if value is truthy
 *
 * @param val A value
 * @return    True if value is not equal to false
 */
int           es_is_true(es_val_t val);

/**
 * Checks if value is a vector
 *
 * @param val A value
 * @return    True if value is a vector
 */
int           es_is_vec(es_val_t val);

/**
 * Checks if value is a closure
 *
 * @param val A value
 * @return    True if value is a closure
 */
int           es_is_closure(es_val_t val);

/**
 * Checks if value is a port
 *
 * @param val A value
 * @return    True if value is a port
 */
int           es_is_port(es_val_t val);

/**
 * Checks if value is an error
 *
 * @param val A value
 * @return    True if value is an error
 */
int           es_is_error(es_val_t val);

/**
 * Checks if value is a symbol
 *
 * @param val A value
 * @return    True if value is a symbol
 */
int           es_is_symbol(es_val_t val);

/**
 * Checks if value is the end-of-file object
 *
 * @param val A value
 * @return    True if value is the eof object
 */
int           es_is_eof_obj(es_val_t val);

/**
 * Checks if value is a builtin procedure
 *
 * @param val A value
 * @return    True if value is a builtin procedure
 */
int           es_is_fn(es_val_t val);

/**
 * Checks if value is a compiled procedure
 *
 * @param val A value
 * @return    True if value is a compiled procedure
 */
int           es_is_proc(es_val_t val);

/**
 * Checks if value is a bytecode object
 *
 * @param val A value
 * @return    True if value is a bytecode object
 */
int           es_is_bytecode(es_val_t val);

/**
 * Checks if value is the unspecified value
 *
 * @param val A value
 * @return    True if value is unspecified
 */
int           es_is_unspecified(es_val_t val);

/**
 * Checks if a value is the unbound value
 *
 * @param val A value
 * @return    True if value is unbound
 */
int           es_is_unbound(es_val_t val);

/**
 * Returns the value of this fixed precision number as an integer
 *
 * @param val A value
 * @return    True if value is unbound
 */
int           es_fixnum_val(es_val_t val);

/**
 * Returns the boolean value as an integer
 *
 * @param val A value
 * @return    True if value is unbound
 */
int           es_bool_val(es_val_t val);

/**
 * Returns the integer code for this character
 *
 * @param val A value
 * @return    True if value is unbound
 */
int           es_char_val(es_val_t val);

/**
 * Returns the integer value mapped to this symbol
 *
 * @param val A value
 * @return    True if value is unbound
 */
int           es_symbol_val(es_val_t val);

/**
 * Converts a value to a string
 *
 * @param val A value
 * @return    True if value is unbound
 */
es_string_t*  es_string_val(es_val_t val);

/**
 * Converts a value to a pair
 *
 * @param val A value
 * @return    True if value is unbound
 */
es_pair_t*    es_pair_val(es_val_t val);

/**
 * Converts a value to a vector
 *
 * @param val A value
 * @return    True if value is unbound
 */
es_vec_t*     es_vector_val(es_val_t val);

/**
 * Converts value to a closure
 *
 * @param val A value
 * @return    True if value is unbound
 */
es_closure_t* es_closure_val(es_val_t val);

/**
 * Converts value to a port
 *
 * @param val A value
 * @return    True if value is unbound
 */
es_port_t*    es_port_val(es_val_t val);

/**
 * Converts value to an error
 *
 * @param val A value
 * @return    An error object
 */
es_error_t*   es_error_val(es_val_t val);

/**
 * Converts value to an environment
 *
 * @param val A value
 * @return    True if value is unbound
 */
es_env_t*     es_env_val(es_val_t val);

/**
 * Makes a fixed precision integer
 *
 * @param val The value of this integer
 * @return    The integer as an eva value
 */
es_val_t      es_make_fixnum(int val);

/**
 * Makes a boolean value
 *
 * @param val An integer boolean
 * @return    The integer as an eva value
 */
es_val_t      es_make_bool(int val);

/**
 * Makes a character value
 *
 * @param val An integer boolean
 * @return    The integer as an eva value
 */
es_val_t      es_make_char(int charcode);

/**
 * Makes a character value from a string
 *
 * @param cstr A null terminated string
 * @return     A character
 */
es_val_t      es_make_char_cstr(char* cstr);

/**
 * Makes a string
 *
 * @param ctx A context
 * @param val An integer boolean
 * @return    A string value
 */
es_val_t      es_make_string(es_ctx_t* ctx, char* cstr);

/**
 * Makes a pair
 *
 * @param ctx  A context
 * @param head The head position value
 * @param tail The tail position value
 * @return     A pair value
 */
es_val_t      es_make_pair(es_ctx_t* ctx, es_val_t head, es_val_t tail);

/**
 * Makes a list
 *
 * @param ctx A context
 * @param val An integer boolean
 * @return    The integer as an eva value
 */
es_val_t      es_make_list(es_ctx_t* ctx, ...);

/**
 * Makes a vector
 *
 * @param ctx  A context
 * @param size The size of the vector
 * @return     The vector
 */
es_val_t      es_make_vec(es_ctx_t* ctx, int size);

/**
 * Makes a vector from a list
 *
 * @param ctx  A context
 * @param list A proper nil-terminated list
 * @return     The vector
 */
es_val_t      es_make_vec_from_list(es_ctx_t* ctx, es_val_t list);

/**
 * Makes a closure
 *
 * @param ctx  A context
 * @param env  The defining scope
 * @param proc The procedure
 * @return     A closure
 */
es_val_t      es_make_closure(es_ctx_t* ctx, es_val_t env, es_val_t proc);

/**
 * Makes a port from a file pointer
 *
 * @param ctx    A context
 * @param stream A file pointer
 * @return       The port
 */
es_val_t      es_make_port(es_ctx_t* ctx, FILE* stream);

/**
 * Makes a bytecode object
 *
 * @param ctx  A context
 * @return     A bytecode object
 */
es_val_t      es_make_bytecode(es_ctx_t* ctx);

/**
 * Makes an error
 *
 * @param ctx A context
 * @param msg The error message
 * @return    An error
 */
es_val_t      es_make_error(es_ctx_t* ctx, char* msg);

/**
 * Makes a symbol
 *
 * @param id An identifier representing this symbol
 * @return   A symbol
 */
es_val_t      es_make_symbol(int id);

/**
 * Makes an environment
 *
 * @param ctx  A context
 * @param size The size of this environment
 * @return     An environment
 */
es_val_t      es_make_env(es_ctx_t* ctx, int size);

/**
 * Makes a native procedure
 *
 * @param ctx   A context
 * @param arity The expected number of arguments
 * @param pcfn  A function pointer
 * @return      The procedure
 */
es_val_t      es_make_fn(es_ctx_t* ctx, int arity, es_pfn_t pcfn);

es_val_t      es_number_add(es_val_t a, es_val_t b);
es_val_t      es_number_sub(es_val_t a, es_val_t b);
es_val_t      es_number_mul(es_val_t a, es_val_t b);
es_val_t      es_number_div(es_val_t a, es_val_t b);
int           es_number_is_eq(es_val_t a, es_val_t b);

es_val_t      es_pair_car(es_val_t pair);
es_val_t      es_pair_cdr(es_val_t pair);
void          es_pair_set_head(es_val_t pair, es_val_t val);
void          es_pair_set_tail(es_val_t pair, es_val_t val);
es_val_t      es_list_argv(es_ctx_t* ctx, int argc, es_val_t* argv);
int           es_list_length(es_val_t list);

void          es_vec_set(es_val_t vec, int idx, es_val_t val);
es_val_t      es_vec_ref(es_val_t vec, int idx);
int           es_vec_len(es_val_t vec);

int           es_string_ref(es_val_t str, int k);

void          es_port_close(es_val_t port);
es_val_t      es_port_read(es_ctx_t* ctx, es_val_t port);
es_val_t      es_port_read_char(es_val_t port);
es_val_t      es_port_peek_char(es_val_t port);
int           es_port_getc(es_val_t port);
int           es_port_peekc(es_val_t port);
void          es_port_reset(es_val_t port);
void          es_port_close(es_val_t port);
int           es_port_nbytes(es_val_t port);
int           es_port_linum(es_val_t port);
int           es_port_colnum(es_val_t port);
es_val_t      es_port_write_char(es_val_t port, es_val_t c);
es_val_t      es_port_write(es_ctx_t* ctx, es_val_t port, es_val_t val);
es_val_t      es_port_printf(es_ctx_t* ctx, es_val_t port, const char* fmt, ...);

es_val_t      es_symbol_intern(es_ctx_t* ctx, char* cstr);
es_val_t      es_symbol_to_string(es_ctx_t* ctx, es_val_t val);

/**
 * Reads the next value from the input port
 *
 * @param ctx  A context
 * @param port An input port
 * @return     The next value from this port
 */
es_val_t      es_read(es_ctx_t* ctx, es_val_t port);
void          es_print(es_ctx_t* ctx, es_val_t exp, es_val_t port);

es_val_t      es_define(es_ctx_t* ctx, char* name, es_val_t value);
es_val_t      es_define_fn(es_ctx_t* ctx, char* name, es_pfn_t fn, int arity);
es_val_t      es_define_symbol(es_ctx_t* ctx, es_val_t env, es_val_t symbol, es_val_t value);
es_val_t      es_lookup_symbol(es_ctx_t* ctx, es_val_t env, es_val_t symbol);


/**
 * Evaluates an expression
 *
 * @param ctx  A context
 * @param exp  An expression value
 * @param env  The environment
 * @return     The result of evaluating the expression
 */
es_val_t      es_eval(es_ctx_t* ctx, es_val_t exp, es_val_t env);
es_val_t      es_load(es_ctx_t* ctx, char* file);

/**
 * Runs the garbage collector
 *
 * @param ctx A context
 */
void          es_gc(es_ctx_t* ctx);
void          es_gc_root_p(es_ctx_t* ctx, es_val_t* pv);
void          es_gc_unroot(es_ctx_t* ctx, int n);
#define       es_gc_root(c, v) es_gc_root_p(c, &(v))

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