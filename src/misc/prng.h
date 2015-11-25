#pragma once

typedef struct { uint32_t a; uint32_t b; uint32_t c; uint32_t d; } prng_t;

uint32_t prng_get(prng_t *x);

void prng_init(prng_t *x, uint32_t a, uint32_t b);

