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

  es_assert("nil should have nil type",                 ES_NIL_TYPE         == es_type_of(es_nil));
  es_assert("unbound should have unbound type",         ES_UNBOUND_TYPE     == es_type_of(es_unbound));
  es_assert("unspecified should have unspecified type", ES_UNDEFINED_TYPE   == es_type_of(es_undefined));
  es_assert("eof should have eof type",                 ES_EOF_OBJ_TYPE     == es_type_of(es_eof_obj));
  es_assert("fixnum should have fixnum type",           ES_FIXNUM_TYPE      == es_type_of(es_make_fixnum(3)));
  es_assert("pair should have pair type",               ES_PAIR_TYPE        == es_type_of(es_make_pair(ctx, es_nil, es_nil)));
  es_assert("closure should have closure type",         ES_CLOSURE_TYPE     == es_type_of(es_make_closure(ctx, es_nil, es_nil)));
  es_assert("port should have port type",               ES_PORT_TYPE        == es_type_of(es_make_port(ctx, NULL)));
  es_assert("character should have character type",     ES_CHAR_TYPE        == es_type_of(es_make_char('c')));
  es_assert("string should have string type",           ES_STRING_TYPE      == es_type_of(es_make_string(ctx, "")));
  es_assert("symbol should have symbol type",           ES_SYMBOL_TYPE      == es_type_of(es_make_symbol(0)));

onfail:
  es_ctx_free(ctx);
}

void test_gc() {
  es_ctx_t* ctx = es_ctx_new(64 * MB);

  es_gc(ctx);

onfail:
  es_ctx_free(ctx);
}

void test_apply() {
  es_ctx_t* ctx = es_ctx_new(64 * MB);

  es_ctx_set_iport(ctx, es_make_port(ctx, stdin));
  es_ctx_set_oport(ctx, es_make_port(ctx, stdout));

  es_val_t plus = es_lookup_symbol(ctx, es_ctx_env(ctx), es_symbol_intern(ctx, "+"));
  es_val_t args = es_make_list(ctx, es_make_fixnum(1), es_make_fixnum(2), es_void);

  es_val_t res = es_apply(ctx, plus, args);

onfail:
  es_ctx_free(ctx);
}

int main() {

  printf("Running tests...\n");

  es_run(test_0);
  es_run(test_1);
  es_run(test_gc);
  es_run(test_apply);

  printf("Tests complete. (%d tests passed, %d tests failed)\n", passed, failed);

  if (!failed)
    printf("Success!\n");
  else
    printf("Failed!\n");

  return failed;
}