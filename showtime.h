/*
 *  Misc globals
 *  Copyright (C) 2007 Andreas Öman
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
 */

#ifndef SHOWTIME_H
#define SHOWTIME_H

#include <inttypes.h>
#include <pthread.h>
#include <sys/time.h>
#include <libhts/htssettings.h>
#include <libavutil/avstring.h>

extern pthread_mutex_t ffmutex;

#define fflock() pthread_mutex_lock(&ffmutex)

#define ffunlock() pthread_mutex_unlock(&ffmutex)

extern int64_t wallclock;
extern time_t walltime;

static inline int64_t
showtime_get_ts(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

extern int frame_duration;

#define AVG_BUFSIZE 4

typedef struct {
  int values[AVG_BUFSIZE];
  int ptr;
  int avg;
  int64_t clock;
} average_t;


extern inline void average_update(average_t *avg, int value);

extern inline void
average_update(average_t *avg, int value)
{
  int v, i;

  if(avg->clock + 1000000 < wallclock) {
    avg->clock = wallclock;
    v = 0;
    for(i = 0; i < AVG_BUFSIZE; i++)
      v += avg->values[i];
    avg->avg = v / AVG_BUFSIZE;
    avg->ptr = (avg->ptr + 1) & (AVG_BUFSIZE - 1);
    avg->values[avg->ptr] = 0;
  }

  avg->values[avg->ptr] += value;
}

void showtime_exit(int suspend);

extern int has_analogue_pad;
extern int mp_show_extra_info;

#endif /* SHOWTIME_H */
