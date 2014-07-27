#include <stdio.h>
#include "average.h"
#include "compiler.h"
#include "misc/minmax.h"

/**
 *
 */
void
average_fill(average_t *avg, int now, int64_t value)
{
  int dt = now - avg->last;

  if(dt <= 0)
    return;

  avg->last = now;

  int delta = value - avg->last_val;
  avg->last_val = value;

  dt = MIN(dt, 3);

  while(dt > 1) {
    avg->slots[avg->slotptr] = 0;
    avg->slotptr = (avg->slotptr + 1) & 3;
    dt--;
  }

  avg->slots[avg->slotptr] = delta;
  avg->slotptr = (avg->slotptr + 1) & 3;
}


/**
 *
 */
int
average_read(average_t *avg, int now)
{
  int dt = now - avg->last;
  dt--;
  if(dt < 0)
    dt = 0;

  int slots = 4 - dt;

  if(slots <= 0)
    return 0;

  int x = 0;

  for(int i = 0; i < slots; i++) {
    x += avg->slots[(avg->slotptr - 1 - i) & 3];
  }
  return x / slots;
}
