/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#ifndef AVGTIME_H__
#define AVGTIME_H__

#include "showtime.h"
#include "prop/prop.h"

typedef struct avgtime {
  int samples[10];
  int ptr;

  int start;

  int peak;
  int avg;
} avgtime_t;

static __inline void avgtime_start(avgtime_t *a)
{
  a->start = showtime_get_ts();
}

static __inline int avgtime_stop(avgtime_t *a, prop_t *avg, prop_t *peak)
{
  int64_t now = showtime_get_ts();
  int d = now - a->start;
  int i, sum;

  a->ptr++;
  if(a->ptr == 10)
    a->ptr = 0;
  
  a->samples[a->ptr] = d;
  
  if(d > a->peak)
    a->peak = d;
  
  for(sum = 0, i = 0; i < 10; i++)
    sum += a->samples[i];
  a->avg = sum / 10;

  if(avg != NULL)
    prop_set_int(avg, a->avg / 1000);

  if(peak != NULL)
    prop_set_int(peak, a->peak / 1000);

  return a->avg;
}

#endif /* AVGTIME_H__ */
