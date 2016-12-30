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
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "main.h"
#include "prop/prop.h"
#include "misc/str.h"

#if ENABLE_NETLOG
#include <netinet/in.h>
#include <sys/socket.h>
#endif


static hts_mutex_t trace_mutex;
static prop_t *log_root;

static int entries;

#define UI_LOG_LINES 200

TAILQ_HEAD(trace_entry_queue, trace_entry);
SIMPLEQ_HEAD(tracetmp_queue, tracetmp);



typedef struct tracetmp {
  SIMPLEQ_ENTRY(tracetmp) link;
  const char *s1;
  rstr_t *s2;
} tracetmp_t;


extern int trace_level;

static int trace_initialized;
static int log_fd;
static int64_t log_start_ts;


#if ENABLE_NETLOG
/**
 *
 */
static void
trace_net_raw(const char *fmt, ...)
{
  static int trace_fd = -1;
  static int logport, logaddr;
  static struct sockaddr_in log_server;

  char msg[1000];
  va_list ap;

  if(trace_fd == -1 ||
     logport != gconf.log_server_port || 
     logaddr != gconf.log_server_ipv4) {

    logport = gconf.log_server_port;
    logaddr = gconf.log_server_ipv4;


    if(logaddr == 0) {
      if(trace_fd != -1) {
	close(trace_fd);
      }
      return;
    }

#if defined(__APPLE__)
    log_server.sin_len = sizeof(log_server);
#endif
    log_server.sin_family = AF_INET;
    log_server.sin_port = htons(logport);
    log_server.sin_addr.s_addr = logaddr;
    trace_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(trace_fd == -1)
      return;
  }

  if(trace_fd == -1)
    return;

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  sendto(trace_fd, msg, strlen(msg), 0,
	 (struct sockaddr*)&log_server, sizeof(log_server));
}



/**
 *
 */
static void
trace_net(int level, const char *prefix, const char *str)
{
  const char *sgr;

  switch(level) {
  case TRACE_EMERG: sgr = "\033[31m"; break;
  case TRACE_ERROR: sgr = "\033[31m"; break;
  case TRACE_INFO:  sgr = "\033[33m"; break;
  case TRACE_DEBUG: sgr = "\033[32m"; break;
  default:          sgr = "\033[35m"; break;
  }

  trace_net_raw("%s%s %s\033[0m\n", sgr, prefix, str);
}
#endif


/**
 *
 */
void
tracev(int flags, int level, const char *subsys, const char *fmt, va_list ap)
{
  char buf2[64];
  char buf3[64];
  char *s, *p;
  const char *leveltxt;
  int l;
  struct tracetmp_queue q;
  tracetmp_t *tt;

  SIMPLEQ_INIT(&q);

  if(!trace_initialized)
    return;

  hts_mutex_lock(&trace_mutex);

  switch(level) {
  case TRACE_EMERG: leveltxt = "EMERG"; break;
  case TRACE_ERROR: leveltxt = "ERROR"; break;
  case TRACE_INFO:  leveltxt = "INFO";  break;
  case TRACE_DEBUG: leveltxt = "DEBUG"; break;
  default:          leveltxt = "?"; break;
  }

  char * const buf = fmtstrv(fmt, ap);
  p = buf;

  snprintf(buf2, sizeof(buf2), "%-15s [%-5s]:", subsys, leveltxt);
  l = strlen(buf2);

  while((s = strsep(&p, "\n")) != NULL) {
    if(!*s)
      continue; // Avoid empty lines

#if ENABLE_NETLOG
    trace_net(level, buf2, s);
#endif

    if(level <= gconf.trace_level)
      trace_arch(level, buf2, s);
    if(!(flags & TRACE_NO_PROP) && level != TRACE_EMERG) {
      tt = alloca(sizeof(tracetmp_t));
      tt->s1 = mystrdupa(buf2);
      tt->s2 = rstr_alloc(s);
      SIMPLEQ_INSERT_TAIL(&q, tt, link);
      entries++;
    }
    if(log_fd != -1) {
      int ts = (arch_get_ts() - log_start_ts) / 1000LL;
      snprintf(buf3, sizeof(buf3), "%02d:%02d:%02d.%03d: ",
	       ts / 3600000,
	       (ts / 60000) % 60,
	       (ts / 1000) % 60,
	       ts % 1000);

      if(write(log_fd, buf3, strlen(buf3)) != strlen(buf3) ||
	 write(log_fd, buf2, strlen(buf2)) != strlen(buf2) ||
	 write(log_fd, s, strlen(s)) != strlen(s) ||
	 write(log_fd, "\n", 1) != 1) {
	close(log_fd);
	log_fd = -1;
      }
    }
    memset(buf2, ' ', l);
  }


  int zapcnt = 0;
  if(entries > UI_LOG_LINES) {
    zapcnt = entries - UI_LOG_LINES;
    entries = UI_LOG_LINES;
  }

  hts_mutex_unlock(&trace_mutex);

  rstr_t *rlev = rstr_alloc(leveltxt);

  SIMPLEQ_FOREACH(tt, &q, link) {
    prop_t *p = prop_create_root(NULL);

    prop_set(p, "prefix", PROP_SET_STRING, tt->s1);
    prop_set(p, "message", PROP_ADOPT_RSTRING, tt->s2);
    prop_set(p, "severity", PROP_SET_RSTRING, rlev);

    if(prop_set_parent(p, log_root))
      abort();
  }

  rstr_release(rlev);

  while(zapcnt > 0) {
    prop_destroy_first(log_root);
    zapcnt--;
  }
  free(buf);
}




/**
 *
 */
void
tracelog(int flags, int level, const char *subsys, const char *fmt, ...)
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
    TRACE(TRACE_DEBUG, pfx, "%s", buf);
  }
}


/**
 *
 */
void
trace_fini(void)
{
  hts_mutex_lock(&trace_mutex);
  static const char logmark[] = "--MARK-- END\n";
  if(write(log_fd, logmark, strlen(logmark))) {}
  close(log_fd);
  log_fd = -1;
  hts_mutex_unlock(&trace_mutex);
}

/**
 *
 */
void
trace_init(void)
{
  char p1[PATH_MAX], p2[PATH_MAX];
  int i;

  if(!gconf.trace_level)
    gconf.trace_level = TRACE_INFO;

  snprintf(p1, sizeof(p1), "%s/log", gconf.cache_path);
  mkdir(p1, 0777);

  // Remove legacy logfile names. This can be removed some time in the future
  for(i = 0; i <= 5; i++) {
    snprintf(p1, sizeof(p1), "%s/log/showtime.log.%d", gconf.cache_path, i);
    snprintf(p1, sizeof(p1), "%s/log/showtime-%d.log", gconf.cache_path, i);
    unlink(p1);
  }

  // Rotate logfiles

  snprintf(p1, sizeof(p1), "%s/log/"APPNAME"-5.log", gconf.cache_path);
  unlink(p1);

  for(i = 4; i >= 0; i--) {
    snprintf(p1, sizeof(p1), "%s/log/"APPNAME"-%d.log", gconf.cache_path,i);
    snprintf(p2, sizeof(p2), "%s/log/"APPNAME"-%d.log", gconf.cache_path,i+1);
    rename(p1, p2);
  }
  
  snprintf(p1, sizeof(p1), "%s/log/"APPNAME"-0.log", gconf.cache_path);
  log_fd = open(p1, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  static const char logstartmark[] = "--MARK-- START\n";
  if(write(log_fd, logstartmark, strlen(logstartmark)) !=
     strlen(logstartmark)) {
    close(log_fd);
    log_fd = -1;
  }
  log_start_ts = arch_get_ts();
  log_root = prop_create(prop_get_global(), "logbuffer");
  hts_mutex_init(&trace_mutex);
  trace_initialized = 1;

  TRACE(TRACE_INFO, "SYSTEM",
        APPNAMEUSER" %s starting. %d CPU cores. Systemtype:%s OS:%s",
        appversion, gconf.concurrency,
        arch_get_system_type(),
        gconf.os_info[0] ? gconf.os_info : "<unknown>");
}
