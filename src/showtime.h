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
#include <stdarg.h>
#include <sys/time.h>
#include <htsmsg/htsmsg_store.h>
#include <arch/threads.h>

void showtime_shutdown(int retcode);

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

struct callout;
typedef void (callout_callback_t)(struct callout *c, void *opaque);

typedef struct callout {
  LIST_ENTRY(callout) c_link;
  callout_callback_t *c_callback;
  void *c_opaque;
  time_t c_expire;
} callout_t;

void callout_arm(callout_t *c, callout_callback_t *callback,
		  void *opaque, int delta);

void callout_arm_abs(callout_t *c, callout_callback_t *callback,
		      void *opaque, time_t when);

void callout_disarm(callout_t *c);

void callout_init(void);

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

/**
 *
 */

enum {
  TRACE_ERROR,
  TRACE_INFO,
  TRACE_DEBUG
};

void tracev(int level, const char *subsys, const char *fmt, va_list ap);

void trace(int level, const char *subsys, const char *fmt, ...);

#define TRACE(level, subsys, fmt...) trace(level, subsys, fmt)

#endif /* SHOWTIME_H */
