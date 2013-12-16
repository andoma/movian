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
#ifndef PERFTIMER_H__
#define PERFTIMER_H__

#include "showtime.h"

typedef struct perftimer {
  int avg;
  int peak;
  int64_t start;
  int logsec;
} perftimer_t;

static inline void perftimer_start(perftimer_t *pt)
{
  pt->start = showtime_get_ts();
}

static inline void perftimer_stop(perftimer_t *pt, const char *str)
{
  int64_t now = showtime_get_ts();
  int d = now - pt->start;
  int s;

  if(pt->avg == 0) {
    pt->avg = d;
  } else {
    pt->avg = (d + pt->avg * 15) / 16;
  }
  
  if(d > pt->peak)
    pt->peak = d;

  s = now / 1000000;

  if(s != pt->logsec) {
    TRACE(TRACE_DEBUG, "timer", "%s: avg:%d, peak:%d",
	  str, pt->avg, pt->peak);
    pt->logsec = s;
  }
}

#endif /* PERFTIMER_H__ */
