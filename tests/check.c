#include "eva.c"

static int failed = 0, passed = 0, total = 0;
#define es_run(n)         printf("%s\n", #n); n()
#define es_assert(name, exp) do { if (!(exp)) { printf("\tFAILED: '%s'\n", name); \
                                  failed++; goto onfail; } else { \
                                  printf("\tPASSED: '%s'\n", name); \
                                  passed++; } total++; } while (0)

enum{ MB = 1000000 };

void test_0() {
  es_ctx_t* ctx = es_ctx_new(64 * MB);

  es_assert("context should allocate", ctx != NULL);

onfail:
  es_ctx_free(ctx);
}

void test_1() {
  es_ctx_t* ctx = es_ctx_new(64 * MB);

  es_assert("nil should have nil type",                 es_nil_type         == es_type_of(es_nil));
  es_assert("unbound should have unbound type",         es_unbound_type     == es_type_of(es_unbound));
  es_assert("unspecified should have unspecified type", es_undefined_type   == es_type_of(es_undefined));
  es_assert("eof should have eof type",                 es_eof_obj_type     == es_type_of(es_eof_obj));
  es_assert("fixnum should have fixnum type",           es_fixnum_type      == es_type_of(es_make_fixnum(3)));
  es_assert("pair should have pair type",               es_pair_type        == es_type_of(es_make_pair(ctx, es_nil, es_nil)));
  es_assert("closure should have closure type",         es_closure_type     == es_type_of(es_make_closure(ctx, es_nil, es_nil)));
  es_assert("port should have port type",               es_port_type        == es_type_of(es_make_port(ctx, NULL)));
  es_assert("character should have character type",     es_char_type        == es_type_of(es_make_char('c')));
  es_assert("string should have string type",           es_string_type      == es_type_of(es_make_string(ctx, "")));
  es_assert("symbol should have symbol type",           es_symbol_type      == es_type_of(es_make_symbol(0)));

onfail:
  es_ctx_free(ctx);
}

void test_gc() {
  es_ctx_t* ctx = es_ctx_new(64 * MB);

  es_gc(ctx);

onfail:
  es_ctx_free(ctx);
}

int main() {

  printf("Running tests...\n");

  es_run(test_0);
  es_run(test_1);
  es_run(test_gc);

  printf("Tests complete. (%d tests passed, %d tests failed)\n", passed, failed);

  if (!failed)
    printf("Success!\n");
  else
    printf("Failed!\n");
  
  return failed;
}