/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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


#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "event.h"
#include "prop/prop.h"
#include "arch/arch.h"
#include "arch/threads.h"

#include "media.h"
#include "audio2/audio_ext.h"
#include "backend/backend.h"
#include "navigator.h"
#include "settings.h"
#include "keyring.h"
#include "notifications.h"
#include "sd/sd.h"
#include "misc/callout.h"
#include "runcontrol.h"
#include "service.h"
#include "keymapper.h"
#include "plugins.h"
#include "blobcache.h"
#include "i18n.h"
#include "misc/str.h"
#include "image/image.h"
#include "video/video_settings.h"
#include "metadata/metadata.h"
#include "subtitles/subtitles.h"
#include "db/db_support.h"
#include "htsmsg/htsmsg_store.h"
#include "js/js.h"
#include "db/kvstore.h"
#include "upgrade.h"
#include "usage.h"
#if ENABLE_GLW
#include "src/ui/glw/glw_settings.h"
#endif
#include "playqueue.h"

#include "networking/asyncio.h"

#if ENABLE_LIBAV
#include <libavformat/avformat.h>
#include <libavformat/version.h>
#include <libavcodec/version.h>
#include <libavutil/avutil.h>
#endif


#include "misc/fs.h"

inithelper_t *inithelpers;

/**
 *
 */

gconf_t gconf;

#if ENABLE_LIBAV
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
    free(*mtx);
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
  if(!gconf.ffmpeglog)
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
#endif


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
  prop_set_string(prop_create(s, "copyright"), "© 2006 - 2012 Andreas Öman");

}

/**
 *
 */
void
init_group(int group)
{
  const inithelper_t *ih;
  for(ih = inithelpers; ih != NULL; ih = ih->next) {
    if(ih->group == group)
      ih->fn();
  }
}


/**
 *
 */
static void *
swthread(void *aux)
{
  plugins_init2();
  
  hts_mutex_lock(&gconf.state_mutex);
  gconf.state_plugins_loaded = 1;
  hts_cond_broadcast(&gconf.state_cond);
  hts_mutex_unlock(&gconf.state_mutex);

  upgrade_init();

  for(int i = 0; i < 10; i++) {
    if(!plugins_upgrade_check())
      break;
    TRACE(TRACE_DEBUG, "plugins",
          "Failed to update repo, retrying in %d seconds", i + 1);
    sleep(i + i);
  }

  for(int i = 0; i < 10; i++) {
    if(!upgrade_refresh())
      break;
    sleep(i + 1);
    TRACE(TRACE_DEBUG, "upgrade",
          "Failed to check for app upgrade, retrying in %d seconds", i + 1);
  }

  usage_report_send(1);

  hts_mutex_lock(&gconf.state_mutex);
  gconf.swrefresh = 0;

  while(1) {

    int timeout = 0;

    while(gconf.swrefresh == 0) {
      timeout = hts_cond_wait_timeout(&gconf.state_cond, &gconf.state_mutex,
				      12 * 3600 * 1000);
      if(timeout)
	break;
    }
    
    gconf.swrefresh = 0;
    hts_mutex_unlock(&gconf.state_mutex);
    if(!timeout)
      plugins_upgrade_check();
    upgrade_refresh();
    usage_report_send(0);
    hts_mutex_lock(&gconf.state_mutex);
  }
  return NULL;
}


/**
 *
 */
void
showtime_swrefresh(void)
{
  hts_mutex_lock(&gconf.state_mutex);
  gconf.swrefresh = 1;
  hts_cond_broadcast(&gconf.state_cond);
  hts_mutex_unlock(&gconf.state_mutex);
}



/**
 *
 */
void
showtime_init(void)
{
  int r;

  hts_mutex_init(&gconf.state_mutex);
  hts_cond_init(&gconf.state_cond, &gconf.state_mutex);

  gconf.exit_code = 1;

  net_init();

  unicode_init();

  /* Initialize property tree */
  prop_init();
  init_global_info();

  /* Initiailize logging */
  trace_init();

  /* Callout framework */
  callout_init();
  prop_init_late();

  /* Initialize htsmsg_store() */
  htsmsg_store_init();

  /* Notification framework */
  notifications_init();

  /* Initialize settings */
  settings_init();

  /* Usage counters */
  usage_init();

  TRACE(TRACE_DEBUG, "core", "Loading resources from %s", showtime_dataroot());

  /* Try to create cache path */
  if(gconf.cache_path != NULL &&
     (r = makedirs(gconf.cache_path)) != 0) {
    TRACE(TRACE_ERROR, "cache", "Unable to create cache path %s -- %s",
	  gconf.cache_path, strerror(r));
    gconf.cache_path = NULL;
  }

  /* Initialize sqlite3 */
  db_init();

  /* Initializte blob cache */
  blobcache_init();

  /* Try to create settings path */
  if(gconf.persistent_path != NULL &&
     (r = makedirs(gconf.persistent_path)) != 0) {
    TRACE(TRACE_ERROR, "settings",
	  "Unable to create path for persistent storage %s -- %s",
	  gconf.persistent_path, strerror(r));
    gconf.persistent_path = NULL;
  }

  /* Per-item key/value store */
  kvstore_init();

  /* Metadata init */
  metadata_init();
  metadb_init();
  subtitles_init();

  /* Metadata decoration init */
  decoration_init();

  /* Initialize keyring */
  keyring_init();

#if ENABLE_LIBAV
  /* Initialize libavcodec & libavformat */
  av_lockmgr_register(fflockmgr);
  av_log_set_callback(fflog);
  av_register_all();

  TRACE(TRACE_INFO, "libav", LIBAVFORMAT_IDENT", "LIBAVCODEC_IDENT", "LIBAVUTIL_IDENT);
#endif

  init_group(INIT_GROUP_GRAPHICS);

#if ENABLE_GLW
  glw_settings_init();
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

  /* Initialize plugin manager */
  plugins_init(gconf.devplugin);

  /* Start software installer thread (plugins, upgrade, etc) */
  hts_thread_create_detached("swinst", swthread, NULL, THREAD_PRIO_BGTASK);

  /* Internationalization */
  i18n_init();

  /* Video settings */
  video_settings_init();

  if(gconf.load_jsfile)
    js_load(gconf.load_jsfile);

  /* Various interprocess communication stuff (D-Bus on Linux, etc) */
  init_group(INIT_GROUP_IPC);

  /* Service discovery. Must be after ipc_init() (d-bus and threads, etc) */
  if(!gconf.disable_sd)
    sd_init();

  /* Initialize various external APIs */
  init_group(INIT_GROUP_API);

  /* Asynchronous IO (Used by HTTP server, etc) */
  asyncio_init();

  runcontrol_init();

  TRACE(TRACE_DEBUG, "SYSTEM", "Hashed device ID: %s", gconf.device_id);
}


/**
 *
 */
void
parse_opts(int argc, char **argv)
{
  const char *argv0 = argv[0];

  argv++;
  argc--;

  gconf.showtime_shell_fd = -1;

  while(argc > 0) {
    if(!strcmp(argv[0], "-h") || !strcmp(argv[0], "--help")) {
      printf("Showtime %s\n"
	     "Copyright (C) 2007-2012 Andreas Öman\n"
	     "\n"
	     "Usage: %s [options] [<url>]\n"
	     "\n"
	     "  Options:\n"
	     "   -h, --help        - This help text.\n"
	     "   -d                - Enable debug output.\n"
	     "   --no-ui           - Start without UI.\n"
	     "   --fullscreen      - Start in fullscreen mode.\n"
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
	     "   --persistent <path> - Set path for persistent stuff [%s].\n"
#if ENABLE_HTTPSERVER
	     "   --disable-upnp    - Disable UPNP/DLNA stack.\n"
#endif
	     "   --disable-sd      - Disable service discovery (mDNS, etc).\n"
	     "   -p                - Path to plugin directory to load\n"
	     "                       Intended for plugin development\n"
	     "   --plugin-repo     - URL to plugin repository\n"
	     "                       Intended for plugin development\n"
	     "   -j <path>           Load javascript file\n"
	     "   --skin <skin>     Select skin (for GLW ui)\n"
	     "\n"
	     "  URL is any URL-type supported by Showtime, "
	     "e.g., \"file:///...\"\n"
	     "\n",
	     htsversion_full,
	     argv0,
	     gconf.cache_path,
	     gconf.persistent_path);
      exit(0);
      argc--;
      argv++;

    } else if(!strcmp(argv[0], "-d")) {
      gconf.trace_level = TRACE_DEBUG;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--ffmpeglog")) {
      gconf.ffmpeglog = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--no-ui")) {
      gconf.noui = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--fullscreen")) {
      gconf.fullscreen = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--syslog")) {
      gconf.trace_to_syslog = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--stdin")) {
      gconf.listen_on_stdin = 1;
      argc -= 1; argv += 1;
      continue;
#if ENABLE_HTTPSERVER
    } else if(!strcmp(argv[0], "--disable-upnp")) {
      gconf.disable_upnp = 1;
      argc -= 1; argv += 1;
      continue;
#endif
    } else if(!strcmp(argv[0], "--disable-sd")) {
      gconf.disable_sd = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--with-standby")) {
      gconf.can_standby = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--with-poweroff")) {
      gconf.can_poweroff = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--with-logout")) {
      gconf.can_logout = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--with-openshell")) {
      gconf.can_open_shell = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--without-exit")) {
      gconf.can_not_exit = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--with-restart")) {
      gconf.can_restart = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "-p") && argc > 1) {
      gconf.devplugin = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--plugin-repo") && argc > 1) {
      gconf.plugin_repo = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "-j") && argc > 1) {
      gconf.load_jsfile = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if (!strcmp(argv[0], "-v") && argc > 1) {
      gconf.initial_view = argv[1];
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--cache") && argc > 1) {
      mystrset(&gconf.cache_path, argv[1]);
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--persistent") && argc > 1) {
      mystrset(&gconf.persistent_path, argv[1]);
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--ui") && argc > 1) {
      mystrset(&gconf.ui, argv[1]);
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--skin") && argc > 1) {
      mystrset(&gconf.skin, argv[1]);
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--upgrade-path") && argc > 1) {
      mystrset(&gconf.upgrade_path, argv[1]);
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--showtime-shell-fd") && argc > 1) {
      gconf.showtime_shell_fd = atoi(argv[1]);
      argc -= 2; argv += 2;
    } else if (!strcmp(argv[0], "--proxy") && argc > 1) {
      char *x = mystrdupa(argv[1]);
      char *pstr = strchr(x, ':');

      if(pstr != NULL) {
        *pstr++ = 0;
        gconf.proxy_port = atoi(pstr);
      } else {
        gconf.proxy_port = 1080;
      }
      snprintf(gconf.proxy_host, sizeof(gconf.proxy_host), "%s", x);
      printf("Proxy set to %s:%d\n", gconf.proxy_host, gconf.proxy_port);
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

  if(argc > 0)
    gconf.initial_url = argv[0];
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
void
shutdown_hook_run(int early)
{
  shutdown_hook_t *sh;
  LIST_FOREACH(sh, &shutdown_hooks, link)
    if(sh->early == early)
      sh->fn(sh->opaque, gconf.exit_code);
}


/**
 *
 */
void
showtime_flush_caches(void)
{
  kvstore_deferred_flush();
  htsmsg_store_flush();
}

/**
 * To avoid hang on exit we launch a special thread that will force
 * exit after 5 seconds
 */
static void *
shutdown_eject(void *aux)
{
  sleep(5);
  arch_exit();
  return NULL;
}


/**
 *
 */
void
showtime_shutdown(int retcode)
{
  TRACE(TRACE_DEBUG, "core", "Shutdown requested, returncode = %d", retcode);

  if(gconf.exit_code != 1) {
    // Force exit
    gconf.exit_code = retcode;
    arch_exit();
  }

  hts_thread_create_detached("eject", shutdown_eject, NULL, THREAD_PRIO_BGTASK);

  event_dispatch(event_create_action(ACTION_STOP));
  prop_destroy_by_name(prop_get_global(), "popups");
  gconf.exit_code = retcode;

  showtime_flush_caches();

  TRACE(TRACE_DEBUG, "core", "Caches flushed");

  int r = arch_stop_req();

  TRACE(TRACE_DEBUG, "core", "arch stop=%d", r);

  if(!r) {
    // If arch_stop_req() returns -1 it will not actually
    // exit showtime but rather suspend the UI and turn off HDMI ,etc
    // Typically used on some targets where we want to enter a 
    // semi-standby state.
    // So only do shutdown hooks if we are about to exit for real.

    // Run early shutdown hooks (those must be fast since this
    // function may be called from UI thread)
    shutdown_hook_run(1);
  }
}


/**
 * The end of all things
 */
void
showtime_fini(void)
{
  prop_destroy_by_name(prop_get_global(), "popups");
  playqueue_fini();
  TRACE(TRACE_DEBUG, "core", "Playqueue finished");
  audio_fini();
  TRACE(TRACE_DEBUG, "core", "Audio finihsed");
  nav_fini();
  TRACE(TRACE_DEBUG, "core", "Navigator finihsed");
  backend_fini();
  TRACE(TRACE_DEBUG, "core", "Backend finished");
  shutdown_hook_run(0);
  TRACE(TRACE_DEBUG, "core", "Slow shutdown hooks finished");
  blobcache_fini();
  TRACE(TRACE_DEBUG, "core", "Blobcache finished");
  metadb_fini();
  TRACE(TRACE_DEBUG, "core", "Metadb finished");
  kvstore_fini();
  notifications_fini();
  usage_fini();
  htsmsg_store_flush();
  TRACE(TRACE_DEBUG, "core", "Showtime terminated normally");
  trace_fini();
}
