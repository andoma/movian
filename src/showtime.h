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
#include <string.h>
#include <sys/time.h>
#include <htsmsg/htsmsg_store.h>
#include <arch/threads.h>

// NLS


#define _(string) nls_get_rstring(string)
#define _p(string) nls_get_prop(string)

struct rstr;
struct rstr *nls_get_rstring(const char *string);

struct prop;
struct prop *nls_get_prop(const char *string);

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define URL_MAX 2048
#define HOSTNAME_MAX 256 /* FQDN is max 255 bytes including ending dot */

void showtime_shutdown(int retcode);

uint32_t showtime_get_version_int(void);

uint32_t showtime_parse_version_int(const char *str);

extern int64_t showtime_get_ts(void);

extern const char *showtime_get_system_type(void);

extern uint64_t arch_get_seed(void);

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
#define CONTENT_ALBUM    9
#define CONTENT_PLUGIN   10
#define CONTENT_MAX      10 /* Update me! */

/**
 * Returns a "type" property name for the given CONTENT_..
 * or NULL on non-match/non-applicability.
 */
static inline const char *content2type (int ctype) __attribute__((unused));
static inline const char *content2type (int ctype) {
  static const char *types[CONTENT_MAX+1] = {
    [CONTENT_UNKNOWN]  = "unknown",
    [CONTENT_DIR]      = "directory",
    [CONTENT_FILE]     = "file",
    [CONTENT_AUDIO]    = "audio",
    [CONTENT_ARCHIVE]  = "archive",
    [CONTENT_VIDEO]    = "video",
    [CONTENT_PLAYLIST] = "playlist",
    [CONTENT_DVD]      = "dvd",
    [CONTENT_IMAGE]    = "image",
    [CONTENT_ALBUM]    = "album",
    [CONTENT_PLUGIN]   = "plugin",
  };

  if (ctype < 0 || ctype > CONTENT_MAX)
    return NULL;

  return types[ctype];
}


/**
 *
 */

enum {
  TRACE_ERROR,
  TRACE_INFO,
  TRACE_DEBUG
};

#define TRACE_NO_PROP 0x1

void trace_init(void);

void trace(int flags, int level, const char *subsys, const char *fmt, ...);

void tracev(int flags, int level, const char *subsys, const char *fmt, va_list ap);

void trace_arch(int level, const char *prefix, const char *buf);

#define TRACE(level, subsys, fmt...) trace(0, level, subsys, fmt)

#define mystrdupa(n) ({ int my_l = strlen(n); \
 char *my_b = alloca(my_l + 1); \
 memcpy(my_b, n, my_l + 1); })

#define mystrndupa(n, len) ({ \
 char *my_b = alloca(len + 1); \
 my_b[len] = 0; \
 memcpy(my_b, n, len); \
})


static inline unsigned int mystrhash(const char *s)
{
  unsigned int v = 5381;
  while(*s)
    v += (v << 5) + v + *s++;
  return v;
}

static inline void mystrset(char **p, const char *s)
{
  free(*p);
  *p = s ? strdup(s) : NULL;
}


static inline const char *mystrbegins(const char *s1, const char *s2)
{
  while(*s2)
    if(*s1++ != *s2++)
      return NULL;
  return s1;
}


void runcontrol_activity(void);

void *shutdown_hook_add(void (*fn)(void *opaque, int exitcode), void *opaque,
			int early);

#define SHOWTIME_EXIT_OK       0
#define SHOWTIME_EXIT_STANDBY  10
#define SHOWTIME_EXIT_POWEROFF 11

extern char *showtime_cache_path;
extern char *showtime_persistent_path;


/* From version.c */
extern const char *htsversion;
extern const char *htsversion_full;


#endif /* SHOWTIME_H */
