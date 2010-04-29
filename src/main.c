/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2009 Andreas Öman
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "showtime.h"
#include "event.h"
#include "prop.h"
#include "arch/arch.h"

#include "audio/audio.h"
#include "backend/backend.h"
#include "navigator.h"
#include "settings.h"
#include "ui/ui.h"
#include "keyring.h"
#include "bookmarks.h"
#include "notifications.h"
#include "sd/sd.h"
#include "ipc/ipc.h"
#include "scrappers/scrappers.h"
#include "misc/callout.h"
#include "api/opensubtitles.h"
#include "runcontrol.h"

/**
 *
 */
int concurrency;
int trace_level;
static int ffmpeglog;
static int showtime_retcode;
char *remote_logtarget; // Used on Wii

static int
fflockmgr(void **_mtx, enum AVLockOp op)
{
  hts_mutex_t **mtx = (hts_mutex_t **)_mtx;

  switch(op) {
  case AV_LOCK_CREATE:
    *mtx = malloc(sizeof(hts_mutex_t));
    hts_mutex_init(*mtx);
    break;
  case AV_LOCK_OBTAIN:
    hts_mutex_lock(*mtx);
    break;
  case AV_LOCK_RELEASE:
    hts_mutex_unlock(*mtx);
    break;
  case AV_LOCK_DESTROY:
    hts_mutex_destroy(*mtx);
    break;
  }
  return 0;
}


/**
 *
 */
static void
fflog(void *ptr, int level, const char *fmt, va_list vl)
{
  static char line[1024];
  AVClass *avc = ptr ? *(AVClass**)ptr : NULL;
  if(!ffmpeglog)
    return;

  if(level < AV_LOG_WARNING)
    level = TRACE_ERROR;
  else if(level < AV_LOG_DEBUG)
    level = TRACE_INFO;
  else
    level = TRACE_DEBUG;

  vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);

  if(line[strlen(line)-1] != '\n')
    return;
  line[strlen(line)-1] = 0;

  TRACE(level, avc ? avc->item_name(ptr) : "FFmpeg", "%s", line);
  line[0] = 0;
}



/**
 * Showtime main
 */
int
main(int argc, char **argv)
{
  struct timeval tv;
  const char *settingspath = NULL;
  const char *uiargs[16];
  const char *argv0 = argc > 0 ? argv[0] : "showtime";
  const char *startpage;
  int nuiargs = 0;
  int can_standby = 0;
  int can_poweroff = 0;

  trace_level = TRACE_ERROR;

  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

  /* We read options ourselfs since getopt() is broken on some (nintento wii)
     targets */

  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "-d")) {
      trace_level++;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "-dd")) {
      trace_level+=2;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--ffmpeglog")) {
      ffmpeglog = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--with-standby")) {
      can_standby = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--with-poweroff")) {
      can_poweroff = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "-s") && argc > 1) {
      settingspath = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--ui") && argc > 1) {
      if(nuiargs < 16)
	uiargs[nuiargs++] = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "-L") && argc > 1) {
      remote_logtarget = argv[1];
      argc -= 2; argv += 2;
      continue;
#ifdef __APPLE__
    /* ignore -psn argument, process serial number */
    } else if(!strncmp(argv[0], "-psn", 4)) {
      argc -= 1; argv += 1;
      continue;
#endif
    } else
      break;
  }

  startpage = argc > 0 ? argv[0] : NAV_HOME;

  /* Initialize property tree */
  prop_init();

  /* Callout framework */
  callout_init();

  /* Notification framework */
  notifications_init();

  /* Architecture specific init */
  arch_init();

  /* Initialize (and optionally load) settings */
  htsmsg_store_init("showtime", settingspath);

  /* Initialize keyring */
  keyring_init();

  /* Initialize settings */
  settings_init();

  /* Initialize libavcodec & libavformat */
  av_lockmgr_register(fflockmgr);
  av_log_set_callback(fflog);
  av_register_all();

  /* Initialize media subsystem */
  media_init();

  /* Initialize scrapping subsystem */
  scrappers_init();

  /* Initialize backend content handlers */
  backend_init();

  /* Initialize navigator */
  nav_init();

  /* Initialize audio subsystem */
  audio_init();

  /* Initialize bookmarks */
  bookmarks_init();

  /* Open initial page */
  nav_open(startpage, NULL, NULL);

  /* Various interprocess communication stuff (D-Bus on Linux, etc) */
  ipc_init();

  /* Service discovery. Must be after ipc_init() (d-bus and threads, etc) */
  sd_init();

  /* opensubtitles.org */
  opensub_init();

  /* */
  runcontrol_init(can_standby, can_poweroff);

  TRACE(TRACE_DEBUG, "core", "Starting UI");

  /* Initialize user interfaces */
  ui_start(nuiargs, uiargs, argv0);

  exit(0);
}

/**
 *
 */
void
trace(int level, const char *subsys, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  tracev(level, subsys, fmt, ap); // defined in arch/
  va_end(ap);
}


/**
 *
 */
static LIST_HEAD(, shutdown_hook) shutdown_hooks;

typedef struct shutdown_hook {
  LIST_ENTRY(shutdown_hook) link;
  void (*fn)(void *opaque);
  void *opaque;
} shutdown_hook_t;

/**
 *
 */
void *
shutdown_hook_add(void (*fn)(void *opaque), void *opaque)
{
  shutdown_hook_t *sh = malloc(sizeof(shutdown_hook_t));
  sh->fn = fn;
  sh->opaque = opaque;
  LIST_INSERT_HEAD(&shutdown_hooks, sh, link);
  return sh;
}


/**
 *
 */
static void *
showtime_shutdown0(void *aux)
{
  shutdown_hook_t *sh;

  LIST_FOREACH(sh, &shutdown_hooks, link)
    sh->fn(sh->opaque);

  ui_shutdown();

  arch_exit(showtime_retcode);
  
  return NULL;
}


/**
 *
 */
void
showtime_shutdown(int retcode)
{
  TRACE(TRACE_DEBUG, "core", "Shutdown requested, returncode = %d", retcode);

  showtime_retcode = retcode;
  hts_thread_create_detached("shutdown", showtime_shutdown0, NULL);
}
