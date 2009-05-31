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
#include "navigator.h"
#include "settings.h"
#include "ui/ui.h"
#include "ui/keymapper.h"
#include "keyring.h"
#include "bookmarks.h"
#include "notifications.h"

/**
 *
 */
int concurrency;
int trace_level;
static int showtime_retcode;


static int
fflockmgr(void **mtx, enum AVLockOp op)
{
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
 * Showtime main
 */
int
main(int argc, char **argv)
{
  struct timeval tv;
  const char *settingspath = NULL;
  const char *uiargs[16];
  const char *argv0 = argc > 0 ? argv[0] : "showtime";
  int nuiargs = 0;

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
    } else if(!strcmp(argv[0], "-s") && argc > 1) {
      settingspath = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--ui") && argc > 1) {
      if(nuiargs < 16)
	uiargs[nuiargs++] = argv[1];
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

  /* Callout framework */
  callout_init();

  /* Initialize property tree */
  prop_init();

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

  /* Initialize event dispatcher */
  event_init();

  /* Initialize libavcodec & libavformat */
  av_lockmgr_register(fflockmgr);
  av_log_set_level(AV_LOG_QUIET);
  av_register_all();

  /* Initialize media subsystem */
  media_init();

  /* Initialize navigator and each of the content handlers */
  nav_init();

  /* Initialize audio subsystem */
  audio_init();

  /* Initialize bookmarks */
  bookmarks_init();

  /* Open initial page */
  nav_open(argc > 0 ? argv[0] : "page://mainmenu", NAV_OPEN_ASYNC);

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
static void *
showtime_shutdown0(void *aux)
{
  /* Very ugly */
#ifdef CONFIG_SPOTIFY
  {
    extern void spotify_shutdown(void);
    spotify_shutdown();
  }
#endif
  ui_shutdown();
   
  exit(showtime_retcode);
}


/**
 *
 */
void
showtime_shutdown(int retcode)
{
  TRACE(TRACE_DEBUG, "core", "Shutdown requested, returncode = %d", retcode);

  showtime_retcode = retcode;
  hts_thread_create_detached(showtime_shutdown0, NULL);
}
