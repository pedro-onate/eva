#include "eva.h"

enum { MB = 1000000 };

int main() {
  es_ctx_t* ctx = es_ctx_new(500 * MB);

  es_ctx_set_iport(ctx, es_port_new(ctx, stdin));
  es_ctx_set_oport(ctx, es_port_new(ctx, stdout));

  es_port_printf(ctx, es_ctx_oport(ctx), "[eva v%s]\n", es_version_str);

  es_val_t val = es_unbound;

  do {
    es_port_printf(ctx, es_ctx_oport(ctx), ">> ");
    val = es_read(ctx, es_ctx_iport(ctx));
    val = es_eval(ctx, val, es_ctx_env(ctx));
    es_print(ctx, val, es_ctx_oport(ctx));
    es_port_printf(ctx, es_ctx_oport(ctx), "\n");
    es_gc(ctx);  
  } while (!es_is_eof_obj(val));

  es_ctx_free(ctx);

  return 0;
}