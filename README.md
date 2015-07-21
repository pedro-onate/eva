#Eva v0.3.1

[![Build Status](https://travis-ci.org/pedro-onate/eva.svg?branch=master)](https://travis-ci.org/pedro-onate/eva)

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

/* Define an external function accessible by the eva runtime */
es_val_t fn_factorial(es_ctx_t* ctx, int argc, es_val_t argv[])
{
    if (!es_is_fixnum(argv[0])) {
        return es_make_error(ctx, "expected numeric value");
    }

    int n = es_fixnum_val(argv[0]);
    int acc = 1;
    while(n > 0) {
        acc *= n--;
    }
    return es_make_fixnum(acc);
}

const size_t HEAP_SIZE = 64 * 1000000;

int main(int argc, char** argv)
{
    /* Create an Eva runtime context with heap size of 64 MB */
    es_ctx_t* ctx = es_ctx_new(HEAP_SIZE);

    /* Register our factorial function with the eva runtime */
    es_define_fn(ctx, "factorial", fn_factorial, 1);

    /* load and evaluate external scheme file */
    es_load(ctx, "example.scm");

    /* Release context */
    es_ctx_free(ctx);
}
```
example.scm
```scheme
; example.scm
(factorial 5)
```