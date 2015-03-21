#include "eva.h"

#include <assert.h>

#define es_assert(name, exp) do { \
                              int v##__LINE__ = exp; printf("%s: %s\n", name, v##__LINE__ ? "true" : "false"); \
                              assert(exp); \
                             } while (0)

void test_1() {
  es_ctx_t* ctx;

  ctx = es_ctx_new(64 * 100000);

  es_assert("nil should have nil type",                 es_nil_type         == es_type_of(es_nil));
  es_assert("unbound should have unbound type",         es_unbound_type     == es_type_of(es_unbound));
  es_assert("unspecified should have unspecified type", es_undefined_type   == es_type_of(es_undefined));
  es_assert("eof should have eof type",                 es_eof_obj_type     == es_type_of(es_eof_obj));
  es_assert("fixnum should have fixnum type",           es_fixnum_type      == es_type_of(es_fixnum_new(3)));
  es_assert("pair should have pair type",               es_pair_type        == es_type_of(es_pair_new(ctx, es_nil, es_nil)));
  es_assert("closure should have closure type",         es_closure_type     == es_type_of(es_closure_new(ctx, es_nil, es_nil)));
  es_assert("port should have port type",               es_port_type        == es_type_of(es_port_new(ctx, NULL)));
  es_assert("character should have character type",     es_char_type   == es_type_of(es_char_new('c')));
  es_assert("string should have string type",           es_string_type      == es_type_of(es_string_new(ctx, "")));
  es_assert("symbol should have symbol type",           es_symbol_type      == es_type_of(es_symbol_new(0)));

  es_ctx_free(ctx);
}

void test_2() {
  es_ctx_t* ctx;

  ctx = es_ctx_new(64 * 100000);

  es_ctx_free(ctx);
}

void test_3() {
  es_ctx_t* ctx;

  ctx = es_ctx_new(64 * 100000);

  es_ctx_free(ctx);
}

int main() {

  test_1();
  test_2();
  test_3();
  
  return 0;
}