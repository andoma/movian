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
#include <sys/time.h>
#include <htsmsg/htsmsg_store.h>
#include <arch/threads.h>

extern hts_mutex_t ffmutex;

#define fflock() hts_mutex_lock(&ffmutex)

#define ffunlock() hts_mutex_unlock(&ffmutex)

static inline int64_t
showtime_get_ts(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

struct deferred;
typedef void (deferred_callback_t)(struct deferred *d, void *opaque);

typedef struct deferred {
  LIST_ENTRY(deferred) d_link;
  deferred_callback_t *d_callback;
  void *d_opaque;
  time_t d_expire;
} deferred_t;

void deferred_arm(deferred_t *d, deferred_callback_t *callback,
		  void *opaque, int delta);

void deferred_arm_abs(deferred_t *d, deferred_callback_t *callback,
		      void *opaque, time_t when);

void deferred_disarm(deferred_t *d);

/**
 * Content types
 */
#define CONTENT_UNKNOWN  0
#define CONTENT_DIR      1
#define CONTENT_FILE     2
#define CONTENT_ARCHIVE  3 /* Archive (a file, but we can dive into it) */
#define CONTENT_AUDIO    4
#define CONTENT_VIDEO    5
#define CONTENT_PLAYLIST 6
#define CONTENT_DVD      7
#define CONTENT_IMAGE    8


#endif /* SHOWTIME_H */
