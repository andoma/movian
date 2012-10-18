/*
 *  Arch specifics for PS3
 *
 *  Copyright (C) 2011 Andreas Öman
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

#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <dirent.h>
#include <assert.h>

#include <netinet/in.h>
#include <net/net.h>
#include <net/netctl.h>


#include <sysmodule/sysmodule.h>
#include <psl1ght/lv2.h>
#include <psl1ght/lv2/spu.h>
#include <lv2/process.h>

#include <rtc.h>

#include "arch/threads.h"
#include "arch/atomic.h"
#include "arch/arch.h"
#include "showtime.h"
#include "service.h"
#include "misc/callout.h"
#include "text/text.h"
#include "notifications.h"
#include "fileaccess/fileaccess.h"
#include "arch/halloc.h"
#include "ps3.h"

#if ENABLE_PS3_VDEC
#include "video/ps3_vdec.h"
#endif

// #define EMERGENCY_EXIT_THREAD




static uint64_t ticks_per_us;

static callout_t memlogger;


static uint64_t
mftb(void)
{
  uint64_t ret;
  asm volatile ("1: mftb %[tmp];       "
		"   cmpwi 7, %[tmp], 0;"
		"   beq-  7, 1b;       "
		: [tmp] "=r" (ret):: "cr7");
  return ret;
}

static prop_t *sysprop;
static prop_t *memprop;

#define LOW_MEM_LOW_WATER  20 * 1024 * 1024
#define LOW_MEM_HIGH_WATER 30 * 1024 * 1024


static void
memlogger_fn(callout_t *co, void *aux)
{
  static int low_mem_warning;

  callout_arm(&memlogger, memlogger_fn, NULL, 1);

  struct {
    uint32_t total;
    uint32_t avail;
  } meminfo;


  Lv2Syscall1(352, (uint64_t) &meminfo);

  prop_set_int(prop_create(memprop, "systotal"), meminfo.total / 1024);
  prop_set_int(prop_create(memprop, "sysfree"), meminfo.avail / 1024);

  struct mallinfo mi = mallinfo();
  prop_set_int(prop_create(memprop, "arena"), (mi.hblks + mi.arena) / 1024);
  prop_set_int(prop_create(memprop, "unusedChunks"), mi.ordblks);
  prop_set_int(prop_create(memprop, "activeMem"), mi.uordblks / 1024);
  prop_set_int(prop_create(memprop, "inactiveMem"), mi.fordblks / 1024);

  if(meminfo.avail < LOW_MEM_LOW_WATER && !low_mem_warning) {
    low_mem_warning = 1;
    notify_add(NULL, NOTIFY_ERROR, NULL, 5,
	       _("System is low on memory (%d kB RAM available)"),
	       meminfo.avail / 1024);
  }

  if(meminfo.avail > LOW_MEM_HIGH_WATER)
    low_mem_warning = 0;

}




/**
 *
 */
int64_t
showtime_get_ts(void)
{
  return mftb() / ticks_per_us;
}

/**
 *
 */
typedef struct rootfsnode {
  LIST_ENTRY(rootfsnode) link;
  char *name;
  service_t *service;
  int mark;
} rootfsnode_t;
 
static LIST_HEAD(, rootfsnode) rootfsnodes;

/**
 *
 */
static void
scan_root_fs(callout_t *co, void *aux)
{
  struct dirent *d;
  struct stat st;
  DIR *dir;
  char fname[32];
  char dpyname[32];
  rootfsnode_t *rfn, *next;

  LIST_FOREACH(rfn, &rootfsnodes, link)
    rfn->mark = 1;

  callout_arm(co, scan_root_fs, NULL, 1);

  if((dir = opendir("/")) == NULL)
    return;

  while((d = readdir(dir)) != NULL) {
    if(strncmp(d->d_name, "dev_", strlen("dev_")))
      continue;
    if(!strncmp(d->d_name, "dev_flash", strlen("dev_flash")))
      continue;

    snprintf(fname, sizeof(fname), "/%s", d->d_name);
    if(stat(fname, &st))
      continue;

    if((st.st_mode & S_IFMT) != S_IFDIR)
      continue;

    LIST_FOREACH(rfn, &rootfsnodes, link)
      if(!strcmp(rfn->name, d->d_name))
	break;

    if(rfn == NULL) {
      rfn = malloc(sizeof(rootfsnode_t));
      rfn->name = strdup(d->d_name);

      snprintf(fname, sizeof(fname), "file:///%s", d->d_name);

      const char *name = d->d_name;
      const char *type = "other";
      if(!strcmp(name, "dev_hdd0"))
	name = "PS3 HDD";
      else if(!strncmp(name, "dev_usb", strlen("dev_usb"))) {
	snprintf(dpyname, sizeof(dpyname), "USB Drive %d",
		 atoi(name + strlen("dev_usb")));
	type = "usb";
	name = dpyname;
      }
      else if(!strcmp(name, "dev_bdvd") ||
	      !strcmp(name, "dev_ps2disc")) {
	name = "BluRay Drive";
	type = "bluray";
      }

      rfn->service = service_create(name, name, fname, type, NULL, 0, 1,
				    SVC_ORIGIN_MEDIA);
      LIST_INSERT_HEAD(&rootfsnodes, rfn, link);
    }
    rfn->mark = 0;
  }
  closedir(dir);
  
  for(rfn = LIST_FIRST(&rootfsnodes); rfn != NULL; rfn = next) {
    next = LIST_NEXT(rfn, link);
    if(!rfn->mark)
      continue;

    LIST_REMOVE(rfn, link);
    service_destroy(rfn->service);
    free(rfn->name);
    free(rfn);
  }

}





static int trace_fd = -1;
static struct sockaddr_in log_server;

void
my_trace(const char *fmt, ...)
{
  char msg[1000];
  va_list ap;

  if(trace_fd == -2)
    return;

  if(trace_fd == -1) {
    int port = 4000;
    char *p;

    log_server.sin_len = sizeof(log_server);
    log_server.sin_family = AF_INET;
    
    snprintf(msg, sizeof(msg), "%s", SHOWTIME_DEFAULT_LOGTARGET);
    p = strchr(msg, ':');
    if(p != NULL) {
      *p++ = 0;
      port = atoi(p);
    }
    log_server.sin_port = htons(port);
    if(inet_pton(AF_INET, msg, &log_server.sin_addr) != 1) {
      trace_fd = -2;
      return;
    }

    trace_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(trace_fd == -1)
      return;
  }

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  sendto(trace_fd, msg, strlen(msg), 0,
	 (struct sockaddr*)&log_server, sizeof(log_server));
}


static int decorate_trace = 1;

/**
 *
 */
void
trace_arch(int level, const char *prefix, const char *str)
{
  const char *sgr, *sgroff;

  switch(level) {
  case TRACE_EMERG: sgr = "\033[31m"; break;
  case TRACE_ERROR: sgr = "\033[31m"; break;
  case TRACE_INFO:  sgr = "\033[33m"; break;
  case TRACE_DEBUG: sgr = "\033[32m"; break;
  default:          sgr = "\033[35m"; break;
  }

  if(!decorate_trace) {
    sgr = "";
    sgroff = "";
  } else {
    sgroff = "\033[0m";
  }

  my_trace("%s%s %s%s\n", sgr, prefix, str, sgroff);
}


#ifdef EMERGENCY_EXIT_THREAD
/**
 *
 */
static void *
emergency_thread(void *aux)
{
  struct sockaddr_in si = {0};
  int s;
  int one = 1, r;
  struct pollfd fds;

  s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
  si.sin_family = AF_INET;
  si.sin_port = htons(31337);

  if(bind(s, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
    TRACE(TRACE_ERROR, "ER", "Unable to bind");
    return NULL;
  }

  fds.fd = s;
  fds.events = POLLIN;

  while(1) {
    r = poll(&fds, 1 , 1000);
    if(r > 0 && fds.revents & POLLIN)
      exit(0);
  }
  return NULL;
}
#endif


/**
 *
 */
static void
ps3_early_init(int argc, char **argv)
{
  char buf[PATH_MAX], *x;

  hts_mutex_init(&thread_info_mutex);


  netInitialize();
  netCtlInit();

  
  ticks_per_us = Lv2Syscall0(147) / 1000000;
  my_trace("Ticks per µs = %ld\n", ticks_per_us);


  if(argc == 0) {
    my_trace("Showtime starting from ???\n");
    return;
  }
  my_trace("Showtime starting from %s\n", argv[0]);
  gconf.binary = strdup(argv[0]);

  snprintf(buf, sizeof(buf), "%s", argv[0]);
  x = strrchr(buf, '/');
  if(x == NULL) {
    my_trace("Showtime starting but argv[0] seems invalid");
    exit(0);
  }
  x++;
  *x = 0;
  gconf.dirname = strdup(buf);
  strcpy(x, "settings");
  gconf.persistent_path = strdup(buf);
  strcpy(x, "cache");
  gconf.cache_path = strdup(buf);
  SysLoadModule(SYSMODULE_RTC);

  thread_info_t *ti = malloc(sizeof(thread_info_t));
  snprintf(ti->name, sizeof(ti->name), "main");
  sys_ppu_thread_get_id(&ti->id);
  hts_mutex_lock(&thread_info_mutex);
  LIST_INSERT_HEAD(&threads, ti, link);
  hts_mutex_unlock(&thread_info_mutex);

  sys_ppu_thread_t tid;
  s32 r = sys_ppu_thread_create(&tid, (void *)thread_reaper, 0,
				2, 16384, 0, (char *)"reaper");
  if(r) {
    my_trace("Failed to create reaper thread: %x", r);
    exit(0);
  }

#ifdef EMERGENCY_EXIT_THREAD
  r = sys_ppu_thread_create(&tid, (void *)emergency_thread, 0,
				2, 16384, 0, (char *)"emergency");
  if(r) {
    my_trace("Failed to create emergency thread: %x", r);
    exit(0);
  }
#endif

  my_trace("The binary is: %s\n", gconf.binary);
}


int64_t
arch_cache_avail_bytes(void)
{
  return 1024 * 1024 * 1024;
}

/**
 *
 */
uint64_t
arch_get_seed(void)
{
  return mftb();
}



/**
 *
 */
static void
preload_fonts(void)
{
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-VR-R-LATIN2.TTF",
		     FONT_DOMAIN_FALLBACK, NULL);
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-NR-R-JPN.TTF",
		     FONT_DOMAIN_FALLBACK, NULL);
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-YG-R-KOR.TTF",
		     FONT_DOMAIN_FALLBACK, NULL);
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-DH-R-CGB.TTF",
		     FONT_DOMAIN_FALLBACK, NULL);
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-CP-R-KANA.TTF",
		     FONT_DOMAIN_FALLBACK, NULL);
}

const char *
showtime_get_system_type(void)
{
  return "PS3";
}




/**
 *
 */
void *
halloc(size_t size)
{
#define ROUND_UP(p, round) ((p + (round) - 1) & ~((round) - 1))

  size_t allocsize = ROUND_UP(size, 64*1024);
  u32 taddr;

  if(Lv2Syscall3(348, allocsize, 0x200, (u64)&taddr))
    panic("halloc(%d) failed", (int)size);

  return (void *)(uint64_t)taddr;
}


/**
 *
 */
void
hfree(void *ptr, size_t size)
{
  Lv2Syscall1(349, (uint64_t)ptr);
}


void
my_localtime(const time_t *now, struct tm *tm)
{
  rtc_datetime dt;
  rtc_tick utc, local;

  rtc_convert_time_to_datetime(&dt, *now);
  rtc_convert_datetime_to_tick(&dt, &utc);
  rtc_convert_utc_to_localtime(&utc, &local);
  rtc_convert_tick_to_datetime(&dt, &local);

  memset(tm, 0, sizeof(struct tm));

  tm->tm_year = dt.year - 1900;
  tm->tm_mon  = dt.month - 1;
  tm->tm_mday = dt.day;
  tm->tm_hour = dt.hour;
  tm->tm_min  = dt.minute;
  tm->tm_sec  = dt.second;
}


void
arch_exit(void)
{
  if(gconf.exit_code == SHOWTIME_EXIT_STANDBY)
    Lv2Syscall3(379, 0x100, 0, 0 );

  if(gconf.exit_code == SHOWTIME_EXIT_RESTART)
    sysProcessExitSpawn2(gconf.binary, 0, 0, 0, 0, 1200, 0x70);

  exit(gconf.exit_code);
}


/**
 *
 */
int
main(int argc, char **argv)
{
  ps3_early_init(argc, argv);

  gconf.concurrency = 2;
  gconf.can_standby = 1;
  gconf.trace_level = TRACE_DEBUG;

  load_syms();

  my_trace("The binary is: %s\n", gconf.binary);

  showtime_init();

  sysprop = prop_create(prop_get_global(), "system");
  memprop = prop_create(sysprop, "mem");
  callout_arm(&memlogger, memlogger_fn, NULL, 1);

#if ENABLE_PS3_VDEC
  TRACE(TRACE_DEBUG, "SPU", "Initializing SPUs");
  lv2SpuInitialize(6, 0);
#endif

  preload_fonts();

  static callout_t co;
  scan_root_fs(&co, NULL);

  extern void glw_ps3_start(void);
  glw_ps3_start();

  showtime_fini();

  arch_exit();


}
