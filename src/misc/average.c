/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
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
