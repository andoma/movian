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
#include "prop/prop.h"
#include "arch/arch.h"

#include "audio/audio_defs.h"
#include "backend/backend.h"
#include "navigator.h"
#include "settings.h"
#include "ui/ui.h"
#include "keyring.h"
#include "bookmarks.h"
#include "notifications.h"
#include "sd/sd.h"
#include "ipc/ipc.h"
#include "misc/callout.h"
#include "api/api.h"
#include "runcontrol.h"
#include "service.h"
#include "keymapper.h"
#include "plugins.h"
#include "blobcache.h"
#include "i18n.h"
#include "misc/string.h"
#include "text/text.h"
#include "video/video_settings.h"

#if ENABLE_HTTPSERVER
#include "networking/http_server.h"
#include "networking/ssdp.h"
#include "upnp/upnp.h"
#endif

#include "misc/fs.h"

static void finalize(void) __attribute__((noreturn));

/**
 *
 */
int concurrency;
int trace_level;
int trace_to_syslog;
int listen_on_stdin;
#if ENABLE_SERDEV
int enable_serdev;
#endif
static int ffmpeglog;
static int showtime_retcode = 1;
const char *showtime_logtarget = SHOWTIME_DEFAULT_LOGTARGET;
char *showtime_cache_path;
char *showtime_persistent_path;

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
 * Set some info in the global property tree that might be interesting
 */
static void
init_global_info(void)
{
  prop_t *s = prop_create(prop_get_global(), "showtime");
  extern const char *htsversion;
  extern const char *htsversion_full;

  prop_set_string(prop_create(s, "version"), htsversion);
  prop_set_string(prop_create(s, "fullversion"), htsversion_full);
  prop_set_string(prop_create(s, "copyright"), "© 2006 - 2011 Andreas Öman");

}


/**
 * Showtime main
 */
int
main(int argc, char **argv)
{
  struct timeval tv;
  const char *uiargs[16];
  const char *argv0 = argc > 0 ? argv[0] : "showtime";
  const char *forceview = NULL;
  const char *devplugin = NULL;
  const char *plugin_repo = NULL;
  int nuiargs = 0;
  int can_standby = 0;
  int can_poweroff = 0;
  int r;

#if ENABLE_HTTPSERVER
  int do_upnp = 1;
#endif
  int do_sd = 1;

  trace_level = TRACE_INFO;

  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

#if ENABLE_BINREPLACE
  extern char *binary_to_replace;
  binary_to_replace = argv[0];
#endif

  arch_set_default_paths(argc, argv);

  /* We read options ourselfs since getopt() is broken on some (nintento wii)
     targets */

  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "-h") || !strcmp(argv[0], "--help")) {
      printf("HTS Showtime %s\n"
	     "Copyright (C) 2007-2010 Andreas Öman\n"
	     "\n"
	     "Usage: %s [options] [<url>]\n"
	     "\n"
	     "  Options:\n"
	     "   -h, --help        - This help text.\n"
	     "   -d                - Enable debug output.\n"
	     "   --ffmpeglog       - Print ffmpeg log messages.\n"
	     "   --with-standby    - Enable system standby.\n"
	     "   --with-poweroff   - Enable system power-off.\n"
	     "   -s <path>         - Non-default Showtime settings path.\n"
	     "   --ui <ui>         - Use specified user interface.\n"
	     "   -L <ip:host>      - Send log messages to remote <ip:host>.\n"
	     "   --syslog          - Send log messages to syslog.\n"
#if ENABLE_STDIN
	     "   --stdin           - Listen on stdin for events.\n"
#endif
	     "   -v <view>         - Use specific view for <url>.\n"
	     "   --cache <path>    - Set path for cache [%s].\n"
#if ENABLE_SERDEV
	     "   --serdev          - Probe service ports for devices.\n"
#endif
#if ENABLE_HTTPSERVER
	     "   --disable-upnp    - Disable UPNP/DLNA stack.\n"
#endif
	     "   --disable-sd      - Disable service discovery (mDNS, etc).\n"
	     "   -p                - Path to plugin directory to load\n"
	     "                       Intended for plugin development\n"
	     "   --plugin-repo     - URL to plugin repository\n"
	     "                       Intended for plugin development\n"
	     "\n"
	     "  URL is any URL-type supported by Showtime, "
	     "e.g., \"file:///...\"\n"
	     "\n",
	     htsversion_full,
	     argv0,
	     showtime_cache_path);
      exit(0);
      argc--;
      argv++;

    } else if(!strcmp(argv[0], "-d")) {
      trace_level++;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--ffmpeglog")) {
      ffmpeglog = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--syslog")) {
      trace_to_syslog = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--stdin")) {
      listen_on_stdin = 1;
      argc -= 1; argv += 1;
      continue;
#if ENABLE_SERDEV
    } else if(!strcmp(argv[0], "--serdev")) {
      enable_serdev = 1;
      argc -= 1; argv += 1;
      continue;
#endif
#if ENABLE_HTTPSERVER
    } else if(!strcmp(argv[0], "--disable-upnp")) {
      do_upnp = 0;
      argc -= 1; argv += 1;
      continue;
#endif
    } else if(!strcmp(argv[0], "--disable-sd")) {
      do_sd = 0;
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
    } else if(!strcmp(argv[0], "--ui") && argc > 1) {
      if(nuiargs < 16)
	uiargs[nuiargs++] = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "-L") && argc > 1) {
      showtime_logtarget = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "-p") && argc > 1) {
      devplugin = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--plugin-repo") && argc > 1) {
      plugin_repo = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if (!strcmp(argv[0], "-v") && argc > 1) {
      forceview = argv[1];
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--cache") && argc > 1) {
      mystrset(&showtime_cache_path, argv[1]);
      argc -= 2; argv += 2;
#ifdef __APPLE__
    /* ignore -psn argument, process serial number */
    } else if(!strncmp(argv[0], "-psn", 4)) {
      argc -= 1; argv += 1;
      continue;
#endif
    } else
      break;
  }


  unicode_init();

  /* Initialize property tree */
  prop_init();
  init_global_info();

  /* Initiailize logging */
  trace_init();

  /* Callout framework */
  callout_init();

  /* Notification framework */
  notifications_init();

  /* Architecture specific init */
  arch_init();

  /* Try to create cache path */
  if(showtime_cache_path != NULL &&
     (r = makedirs(showtime_cache_path)) != 0) {
    TRACE(TRACE_ERROR, "cache", "Unable to create cache path %s -- %s",
	  showtime_cache_path, strerror(r));
    showtime_cache_path = NULL;
  }

  /* Initializte blob cache */
  blobcache_init();

  /* Try to create settings path */
  if(showtime_persistent_path != NULL &&
     (r = makedirs(showtime_persistent_path)) != 0) {
    TRACE(TRACE_ERROR, "settings",
	  "Unable to create path for persistent storage %s -- %s",
	  showtime_persistent_path, strerror(r));
    showtime_persistent_path = NULL;
  }

  /* Initialize htsmsg_store() */
  htsmsg_store_init();

  /* Initialize keyring */
  keyring_init();

  /* Initialize settings */
  settings_init();

  /* Initialize libavcodec & libavformat */
  av_lockmgr_register(fflockmgr);
  av_log_set_callback(fflog);
  av_register_all();

  /* Freetype keymapper */
#if ENABLE_LIBFREETYPE
  freetype_init();
#endif

  /* Global keymapper */
  keymapper_init();

  /* Initialize media subsystem */
  media_init();

  /* Service handling */
  service_init();

  /* Initialize backend content handlers */
  backend_init();

  /* Initialize navigator */
  nav_init();

  /* Initialize audio subsystem */
  audio_init();

  /* Initialize bookmarks */
  bookmarks_init();

  /* Initialize plugin manager and load plugins */
  plugins_init(devplugin, plugin_repo);

  /* Internationalization */
  i18n_init();

  /* Video settings */
  video_settings_init();


  nav_open(NAV_HOME, NULL);

  /* Open initial page */
  if(argc > 0)
    nav_open(argv[0], forceview);

  /* Various interprocess communication stuff (D-Bus on Linux, etc) */
  ipc_init();

  /* Service discovery. Must be after ipc_init() (d-bus and threads, etc) */
  if(do_sd)
    sd_init();

  /* Initialize various external APIs */
  api_init();

  /* HTTP server and UPNP */
#if ENABLE_HTTPSERVER
  http_server_init();
  if(do_upnp)
    upnp_init();
#endif


  /* */
  runcontrol_init(can_standby, can_poweroff);

  TRACE(TRACE_DEBUG, "core", "Starting UI");

  /* Initialize user interfaces */
  ui_start(nuiargs, uiargs, argv0);

  finalize();
}

/**
 *
 */
static LIST_HEAD(, shutdown_hook) shutdown_hooks;

typedef struct shutdown_hook {
  LIST_ENTRY(shutdown_hook) link;
  void (*fn)(void *opaque, int exitcode);
  void *opaque;
  int early;
} shutdown_hook_t;

/**
 *
 */
void *
shutdown_hook_add(void (*fn)(void *opaque, int exitcode), void *opaque,
		  int early)
{
  shutdown_hook_t *sh = malloc(sizeof(shutdown_hook_t));
  sh->fn = fn;
  sh->opaque = opaque;
  sh->early = early;
  LIST_INSERT_HEAD(&shutdown_hooks, sh, link);
  return sh;
}


/**
 *
 */
static void
shutdown_hook_run(int early)
{
  shutdown_hook_t *sh;
  LIST_FOREACH(sh, &shutdown_hooks, link)
    if(sh->early == early)
      sh->fn(sh->opaque, showtime_retcode);
}

/**
 *
 */
static void *
showtime_shutdown0(void *aux)
{
  finalize();
  return NULL;
}


/**
 *
 */
void
showtime_shutdown(int retcode)
{
  TRACE(TRACE_DEBUG, "core", "Shutdown requested, returncode = %d", retcode);

  if(showtime_retcode != 1) {
    // Force exit
    arch_exit(retcode);
  }

  showtime_retcode = retcode;

  // run early shutdown hooks (those must be fast)
  shutdown_hook_run(1);

  htsmsg_store_flush();

  if(ui_shutdown() == -1) {
    // Primary UI has no shutdown method, launch a new thread to stop
    hts_thread_create_detached("shutdown", showtime_shutdown0, NULL,
			       THREAD_PRIO_NORMAL);
  }
}


/**
 * The end of all things
 */
static void
finalize(void)
{
  audio_fini();
  backend_fini();
  shutdown_hook_run(0);
  arch_exit(showtime_retcode);
}
