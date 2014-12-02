#Eva v0.3.0
A lightweight, embeddable, scheme implementation
##Build
```bash
make
```
##Run
```bash
./eva
```
##Integration Example
example.c
```c
/*
* example.c
*/

#include "eva.h"

/* Define a foreign function */
es_val_t my_factorial(es_ctx_t* ctx, int argc, es_val_t argv[]) {
    if (!es_is_fixnum(argv[0])) {
        return es_error_new(ctx, "expected numeric value");
    }

    long n = es_to_fixnum(argv[0]);
    long acc = 1;
    while(n > 0) {
        acc *= n--;
    }
    return es_fixnum_new(acc);
}

int main(int argc, char** argv) {
    /* create context */
    es_ctx_t* ctx = es_ctx_new(<heap size bytes>);
    ...
    /* register function  */
    es_val_t fn = es_fn_new(ctx, 
        1, /* number of arguments */
        my_factorial
    );
    es_define(ctx, "factorial", fn);
    
    /* load and evaluate external scheme file */
    es_load(ctx, "example.scm");
}
```
example.scm
```scheme
; example.scm
(factorial 5)
```