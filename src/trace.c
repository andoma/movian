/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2010 Andreas Öman
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

#include <stdio.h>

#include "prop/prop.h"
#include "showtime.h"

static hts_mutex_t trace_mutex;
static prop_t *log_root;

static int entries;

TAILQ_HEAD(trace_entry_queue, trace_entry);

struct trace_entry_queue traces;

typedef struct trace_entry {
  TAILQ_ENTRY(trace_entry) link;
  prop_t *p;
} trace_entry_t;

extern int trace_level;

static int trace_initialized;


/**
 *
 */
static void
trace_prop(int l, const char *pfx, const char *msg, const char *sev)
{
  trace_entry_t *te = malloc(sizeof(trace_entry_t));
  te->p = prop_create_root(NULL);

  prop_set_string(prop_create(te->p, "prefix"), pfx);
  prop_set_string(prop_create(te->p, "message"), msg);
  prop_set_string(prop_create(te->p, "severity"), sev);

  TAILQ_INSERT_TAIL(&traces, te, link);


  if(prop_set_parent(te->p, log_root))
    abort();

  entries++;

  if(entries > 50) {
    te = TAILQ_FIRST(&traces);
    TAILQ_REMOVE(&traces, te, link);
    prop_destroy(te->p);
    free(te);
    entries--;
  }
}


/**
 *
 */
void
tracev(int flags, int level, const char *subsys, const char *fmt, va_list ap)
{
  static char buf[1024];
  char buf2[64];
  char *s, *p;
  const char *leveltxt;
  int l;

  if(!trace_initialized)
    return;

  hts_mutex_lock(&trace_mutex);

  switch(level) {
  case TRACE_ERROR: leveltxt = "ERROR"; break;
  case TRACE_INFO:  leveltxt = "INFO";  break;
  case TRACE_DEBUG: leveltxt = "DEBUG"; break;
  default:          leveltxt = "?????"; break;
  }

  vsnprintf(buf, sizeof(buf), fmt, ap);

  p = buf;

  snprintf(buf2, sizeof(buf2), "%s [%s]:", subsys, leveltxt);
  l = strlen(buf2);

  while((s = strsep(&p, "\n")) != NULL) {
    if(level <= trace_level)
      trace_arch(level, buf2, s);
    if(!(flags & TRACE_NO_PROP))
      trace_prop(level, buf2, s, leveltxt);
    memset(buf2, ' ', l);
  }


  hts_mutex_unlock(&trace_mutex);
}




/**
 *
 */
void
trace(int flags, int level, const char *subsys, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  tracev(flags, level, subsys, fmt, ap);
  va_end(ap);
}


/**
 *
 */
void
hexdump(const char *pfx, const void *data_, int len)
{
  int i, j;
  const uint8_t *data = data_;
  char buf[100];
  
  for(i = 0; i < len; i+= 16) {
    int p = snprintf(buf, sizeof(buf), "0x%06x: ", i);

    for(j = 0; j + i < len && j < 16; j++) {
      p += snprintf(buf + p, sizeof(buf) - p, "%s%02x ",
		    j==8 ? " " : "", data[i+j]);
    }
    const int cnt = (17 - j) * 3 + (j < 8);
    memset(buf + p, ' ', cnt);
    p += cnt;

    for(j = 0; j + i < len && j < 16; j++)
      buf[p++] = data[i+j] < 32 || data[i+j] > 126 ? '.' : data[i+j];
    buf[p] = 0;
    TRACE(TRACE_DEBUG, pfx, buf);
  }
}



/**
 *
 */
void
trace_init(void)
{
  TAILQ_INIT(&traces);
  log_root = prop_create(prop_get_global(), "logbuffer");
  hts_mutex_init(&trace_mutex);
  trace_initialized = 1;
}
