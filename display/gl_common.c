/*
 *  Common GL init
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

#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "display.h"



static int
intcmp(const void *A, const void *B)
{
  const int *a = A;
  const int *b = B;
  return *a - *b;
}


#define FRAME_DURATION_SAMPLES 31 /* should be an odd number */

int
gl_update_timings(void)
{
  struct timeval tv;
  static int64_t lastts, firstsample;
  static int deltaarray[FRAME_DURATION_SAMPLES];
  static int deltaptr;
  static int lastframedur;
  int d, r = 0;
  
  gettimeofday(&tv, NULL);
  wallclock = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
  walltime = tv.tv_sec;
  
  if(lastts != 0) {
    d = wallclock - lastts;
    if(deltaptr == 0)
      firstsample = wallclock;

    deltaarray[deltaptr++] = d;

    if(deltaptr == FRAME_DURATION_SAMPLES) {
      qsort(deltaarray, deltaptr, sizeof(int), intcmp);
      d = deltaarray[FRAME_DURATION_SAMPLES / 2];
      
      if(lastframedur == 0) {
	lastframedur = d;
      } else {
	lastframedur = (d + lastframedur) / 2;
      }
      frame_duration = lastframedur;
      r = 1;
      deltaptr = 0;
    }
  }
  lastts = wallclock;
  return r;
}
