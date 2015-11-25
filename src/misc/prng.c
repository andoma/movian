#include <stdint.h>
#include "prng.h"

// PRNG from http://burtleburtle.net/bob/rand/smallprng.html (Public Domain)

#define rot(x,k) (((x)<<(k))|((x)>>(32-(k))))

uint32_t
prng_get(prng_t *x)
{
    uint32_t e = x->a - rot(x->b, 27);
    x->a = x->b ^ rot(x->c, 17);
    x->b = x->c + x->d;
    x->c = x->d + e;
    x->d = e + x->a;
    return x->d;
}


void
prng_init(prng_t *x, uint32_t b, uint32_t c)
{
  x->a = 0xf1ea5eed;
  x->b = b;
  x->c = c;
  x->d = b;
  for(int i=0; i<20; i++) {
    prng_get(x);
  }
}
