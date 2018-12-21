/*
 *  Copyright (C) 2006-2018 Lonelycoder AB
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

#include "main.h"
#include "event.h"
#include "prop/prop.h"
#include "arch/arch.h"
#include "arch/threads.h"

#include "media/media.h"
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
#include "htsmsg/htsmsg_json.h"
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

#include "fileaccess/fileaccess.h"

static LIST_HEAD(, inithelper) inithelpers;

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

  if(!gconf.libavlog)
    return;

  if(level < AV_LOG_WARNING) {
    level = TRACE_ERROR;
  } else {
    if(level < AV_LOG_DEBUG)
      level = TRACE_INFO;
    else
      level = TRACE_DEBUG;
  }

  vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);
  if(line[0] == 0)
    return;

  if(line[strlen(line)-1] != '\n')
    return;
  line[strlen(line)-1] = 0;

  TRACE(level, avc ? avc->item_name(ptr) : "libav", "%s", line);
  line[0] = 0;
}
#endif


/**
 * Set some info in the global property tree that might be interesting
 */
static void
init_global_info(void)
{
  prop_t *s = prop_create(prop_get_global(), "app");

  prop_set(s, "name", PROP_SET_STRING, APPNAMEUSER);
  prop_set(s, "version", PROP_SET_STRING, appversion);
  prop_set(s, "fullversion", PROP_SET_STRING, appversion);
  prop_set(s, "copyright", PROP_SET_STRING, "© 2006 - 2018 Lonelycoder AB");
}


/**
 *
 */
static int
ihcmp(const inithelper_t *a, const inithelper_t *b)
{
  return a->prio - b->prio;
}


/**
 *
 */
void
inithelper_register(inithelper_t *ih)
{
  LIST_INSERT_SORTED(&inithelpers, ih, link, ihcmp, inithelper_t);
}


/**
 *
 */
void
init_group(int group)
{
  const inithelper_t *ih;
  LIST_FOREACH(ih, &inithelpers, link) {
    if(ih->group == group && ih->init != NULL)
      ih->init();
  }
}


/**
 *
 */
void
fini_group(int group)
{
  const inithelper_t *ih;
  LIST_FOREACH(ih, &inithelpers, link) {
    if(ih->group == group && ih->fini != NULL)
      ih->fini();
  }
}


/**
 *
 */
static void
navigator_can_start(void)
{
  hts_mutex_lock(&gconf.state_mutex);
  gconf.navigator_can_start = 1;
  hts_cond_broadcast(&gconf.state_cond);
  hts_mutex_unlock(&gconf.state_mutex);
}


/**
 *
 */
static void *
geothread(void *aux)
{
  for(int i = 0; i < 10; i++) {

    buf_t *b = fa_load("http://ifconfig.co/json", NULL);
    if(b == NULL) {
      sleep(i * 2);
      continue;
    }
    htsmsg_t *msg = htsmsg_json_deserialize(buf_cstr(b));
    buf_release(b);
    if(msg != NULL) {
      const char *cc = htsmsg_get_str(msg, "country_iso");
      if(cc != NULL) {
        TRACE(TRACE_DEBUG, "GEO", "Current country: %s", cc);
        prop_setv(prop_get_global(), "location", "cc", NULL,
                  PROP_SET_STRING, cc);
      }

      htsmsg_release(msg);
    }
    break;
  }
  return NULL;
}




/**
 *
 */
static void *
swthread(void *aux)
{
#if ENABLE_PLUGINS
  plugins_load_all();
#endif

  upgrade_init();

  usage_start();

  if(!gconf.disable_upgrades) {

#if ENABLE_PLUGINS
    for(int i = 0; i < 10; i++) {
      if(!plugins_upgrade_check())
        break;
      navigator_can_start();
      TRACE(TRACE_DEBUG, "plugins",
            "Failed to update repo, retrying in %d seconds", i + 1);
      sleep(i + i);
    }
#endif
    navigator_can_start();

    for(int i = 0; i < 10; i++) {
      if(!upgrade_refresh())
        break;
      sleep(i + 1);
      TRACE(TRACE_DEBUG, "upgrade",
            "Failed to check for app upgrade, retrying in %d seconds", i + 1);
    }
  } else {
    navigator_can_start();
  }

  hts_mutex_lock(&gconf.state_mutex);
  gconf.swrefresh = 0;

  while(!gconf.disable_upgrades) {

    int timeout = 0;

    while(gconf.swrefresh == 0) {
      timeout = hts_cond_wait_timeout(&gconf.state_cond, &gconf.state_mutex,
				      12 * 3600 * 1000);
      if(timeout)
	break;
    }
    
    gconf.swrefresh = 0;
    hts_mutex_unlock(&gconf.state_mutex);
#if ENABLE_PLUGINS
    if(!timeout)
      plugins_upgrade_check();
#endif
    upgrade_refresh();
    hts_mutex_lock(&gconf.state_mutex);
  }
  hts_mutex_unlock(&gconf.state_mutex);
  return NULL;
}


/**
 *
 */
void
swrefresh(void)
{
  hts_mutex_lock(&gconf.state_mutex);
  gconf.swrefresh = 1;
  hts_cond_broadcast(&gconf.state_cond);
  hts_mutex_unlock(&gconf.state_mutex);
}


/**
 *
 */
static void
generate_device_id(void)
{
  arch_get_random_bytes(gconf.running_instance, sizeof(gconf.running_instance));

  if(gconf.device_id[0] != 0)
    return;

  rstr_t *s = htsmsg_store_get_str("deviceid", "deviceid");
  if(s != NULL) {
    snprintf(gconf.device_id, sizeof(gconf.device_id), "%s", rstr_get(s));
    rstr_release(s);
  } else {
    uint8_t d[20];
    char uuid[40];

    arch_get_random_bytes(d, sizeof(d));

    snprintf(uuid, sizeof(uuid),
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
             "%02x%02x%02x%02x%02x%02x",
             d[0x0], d[0x1], d[0x2], d[0x3],
             d[0x4], d[0x5], d[0x6], d[0x7],
             d[0x8], d[0x9], d[0xa], d[0xb],
             d[0xc], d[0xd], d[0xe], d[0xf]);

    snprintf(gconf.device_id, sizeof(gconf.device_id), "%s", uuid);
    htsmsg_store_set("deviceid", "deviceid", HMF_STR, uuid);
  }
}


/**
 *
 */
void
main_init(void)
{
  char errbuf[512];

  hts_mutex_init(&gconf.state_mutex);
  hts_cond_init(&gconf.state_cond, &gconf.state_mutex);

  gconf.exit_code = 1;

  unicode_init();

  /* Initialize property tree */
  prop_init();
  init_global_info();

  /* Callout framework */
  callout_init();

  /* Network init */
  asyncio_init_early();
  init_group(INIT_GROUP_NET);


  /* Initiailize logging */
  trace_init();

  prop_init_late();

  /* Initialize settings */
  settings_init();

  /* Notification framework */
  notifications_init();

  TRACE(TRACE_DEBUG, "core", "Loading resources from %s", app_dataroot());

  TRACE(TRACE_DEBUG, "core", "Cache path: %s", gconf.cache_path);

  /* Try to create cache path */
  if(gconf.cache_path != NULL &&
     fa_makedirs(gconf.cache_path, errbuf, sizeof(errbuf))) {
    TRACE(TRACE_ERROR, "core", "Unable to create cache path %s -- %s",
	  gconf.cache_path, errbuf);
    gconf.cache_path = NULL;
  }

  /* Initialize sqlite3 */
#if ENABLE_SQLITE
  db_init();
#endif

  /* Initializte blob cache */
  blobcache_init();


  TRACE(TRACE_DEBUG, "core", "Persistent path: %s", gconf.persistent_path);

  /* Try to create settings path */
  if(gconf.persistent_path != NULL &&
     fa_makedirs(gconf.persistent_path, errbuf, sizeof(errbuf))) {
    TRACE(TRACE_ERROR, "core",
	  "Unable to create path for persistent storage %s -- %s",
	  gconf.persistent_path, errbuf);
    gconf.persistent_path = NULL;
  }

  /* Per-item key/value store */
  kvstore_init();

  /* Metadata init */
#if ENABLE_METADATA
  metadata_init();
  metadb_init();
  decoration_init();
#endif

  subtitles_init();

  /* Initialize keyring */
  keyring_init();

#if ENABLE_LIBAV
  /* Initialize libavcodec & libavformat */
  av_lockmgr_register(fflockmgr);
  av_log_set_callback(fflog);
  av_register_all();

  TRACE(TRACE_INFO, "libav", LIBAVFORMAT_IDENT", "LIBAVCODEC_IDENT", "LIBAVUTIL_IDENT" cpuflags:0x%x", av_get_cpu_flags());
#endif

  init_group(INIT_GROUP_GRAPHICS);

#if ENABLE_GLW
  glw_settings_init();
#endif

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

#if ENABLE_PLUGINS
  /* Initialize plugin manager */
  plugins_init(gconf.devplugins);
#endif

  generate_device_id();
  TRACE(TRACE_DEBUG, "SYSTEM", "Hashed device ID: %s", gconf.device_id);
  if(gconf.device_type[0])
    TRACE(TRACE_DEBUG, "SYSTEM", "Device type: %s", gconf.device_type);

  /* Start software installer thread (plugins, upgrade, etc) */
  hts_thread_create_detached("geothread", geothread, NULL, THREAD_PRIO_BGTASK);
  hts_thread_create_detached("swinst", swthread, NULL, THREAD_PRIO_BGTASK);

  /* Internationalization */
  i18n_init();

  /* Video settings */
  video_settings_init();

  /* Various interprocess communication stuff (D-Bus on Linux, etc) */
  init_group(INIT_GROUP_IPC);

  /* Service discovery. Must be after ipc_init() (d-bus and threads, etc) */
  if(!gconf.disable_sd)
    sd_init();

  /* Initialize various external APIs */
  init_group(INIT_GROUP_API);

  /* Asynchronous IO (Used by HTTP server, etc) */
  asyncio_start();

  runcontrol_init();

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

  gconf.shell_fd = -1;

  while(argc > 0) {
    if(!strcmp(argv[0], "-h") || !strcmp(argv[0], "--help")) {
      printf(APPNAMEUSER" %s\n"
	     "Copyright (C) 2006-2018 Lonelycoder AB\n"
	     "\n"
	     "Usage: %s [options] [<url>]\n"
	     "\n"
	     "  Options:\n"
	     "   -h, --help          - This help text.\n"
	     "   -d                  - Enable debug output.\n"
	     "   --no-ui             - Start without UI.\n"
	     "   --fullscreen        - Start in fullscreen mode.\n"
	     "   --libav-log         - Print libav log messages.\n"
	     "   --with-standby      - Enable system standby.\n"
	     "   --with-poweroff     - Enable system power-off.\n"
	     "   -s <path>           - Non-default settings path.\n"
	     "   --ui <ui>           - Use specified user interface.\n"
	     "   -L <ip:host>        - Send log messages to remote <ip:host>.\n"
	     "   --syslog            - Send log messages to syslog.\n"
#if ENABLE_STDIN
	     "   --stdin             - Listen on stdin for events.\n"
#endif
	     "   -v <view>           - Use specific view for <url>.\n"
	     "   --cache <path>      - Set path for cache [%s].\n"
	     "   --persistent <path> - Set path for persistent stuff [%s].\n"
#if ENABLE_HTTPSERVER
	     "   --disable-upnp      - Disable UPNP/DLNA stack.\n"
#endif
	     "   --disable-sd        - Disable service discovery (mDNS, etc).\n"
	     "   -p                  - Path to plugin directory to load\n"
	     "                         Intended for plugin development\n"
	     "   --plugin-repo       - URL to plugin repository\n"
	     "                         Intended for plugin development\n"
	     "   --proxy <host:port> - Use SOCKS 4/5 proxy for http requests.\n"
	     "   -j <path>           - Load javascript file\n"
	     "   --skin <skin>       - Select skin (for GLW ui)\n"
	     "\n"
	     "  URL is any URL-type supported, "
	     "e.g., \"file:///...\"\n"
	     "\n",
	     appversion,
	     argv0,
	     gconf.cache_path,
	     gconf.persistent_path);
      exit(0);

    } else if(!strcmp(argv[0], "-d")) {
      gconf.trace_level = TRACE_DEBUG;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--libav-log")) {
      gconf.libavlog = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--debug-glw")) {
      gconf.debug_glw = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--pointer-is-touch")) {
      gconf.convert_pointer_to_touch = 1;
      argc -= 1; argv += 1;
      continue;
    } else if(!strcmp(argv[0], "--show-usage-events")) {
      gconf.show_usage_events = 1;
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
    } else if(!strcmp(argv[0], "--disable-upgrades")) {
      gconf.disable_upgrades = 1;
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
      strvec_addp(&gconf.devplugins,argv[1]);
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--plugin-repo") && argc > 1) {
      gconf.plugin_repo = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--bypass-ecmascript-acl")) {
      gconf.bypass_ecmascript_acl = 1;
      argc -= 1; argv += 1;
    } else if(!strcmp(argv[0], "--ecmascript") && argc > 1) {
      gconf.load_ecmascript = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--vmir-bitcode") && argc > 1) {
      gconf.load_np = argv[1];
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
      gconf.shell_fd = atoi(argv[1]);
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
app_flush_caches(void)
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
static void *
do_shutdown(void *aux)
{
  event_dispatch(event_create_action(ACTION_STOP));
  app_flush_caches();

  TRACE(TRACE_DEBUG, "core", "Caches flushed");

  int r = arch_stop_req();

  TRACE(TRACE_DEBUG, "core", "arch stop=%d", r);

  switch(r) {
    // See arch.h for detailed meaning of those
  case ARCH_STOP_IS_PROGRESSING:
    break;

  case ARCH_STOP_IS_NOT_HANDLED:
    break;

  case ARCH_STOP_CALLER_MUST_HANDLE:
    main_fini();
    arch_exit();
    break;
  }

  return NULL;
}

/**
 *
 */
void
app_shutdown(int retcode)
{
  TRACE(TRACE_DEBUG, "core", "Shutdown requested, returncode = %d", retcode);

  if(gconf.exit_code != 1) {
    // Force exit
    gconf.exit_code = retcode;
    arch_exit();
  }

  gconf.exit_code = retcode;

  hts_thread_create_detached("eject", shutdown_eject, NULL, THREAD_PRIO_BGTASK);
  hts_thread_create_detached("shutdown", do_shutdown, NULL, THREAD_PRIO_BGTASK);

}


/**
 * The end of all things
 */
void
main_fini(void)
{
  shutdown_hook_run(1);
  prop_destroy_by_name(prop_get_global(), "popups");
  fini_group(INIT_GROUP_API);
  TRACE(TRACE_DEBUG, "core", "API group finished");
  fini_group(INIT_GROUP_IPC);
  TRACE(TRACE_DEBUG, "core", "IPC group finished");
#if ENABLE_PLAYQUEUE
  playqueue_fini();
  TRACE(TRACE_DEBUG, "core", "Playqueue finished");
#endif
  audio_fini();
  TRACE(TRACE_DEBUG, "core", "Audio finished");
  nav_fini();
  TRACE(TRACE_DEBUG, "core", "Navigator finished");
  backend_fini();
  TRACE(TRACE_DEBUG, "core", "Backend finished");
  shutdown_hook_run(0);
  TRACE(TRACE_DEBUG, "core", "Slow shutdown hooks finished");
  blobcache_fini();
  TRACE(TRACE_DEBUG, "core", "Blobcache finished");
#if ENABLE_METADATA
  metadb_fini();
  TRACE(TRACE_DEBUG, "core", "Metadb finished");
#endif
  kvstore_fini();
  notifications_fini();
  htsmsg_store_flush();
  TRACE(TRACE_DEBUG, "core", APPNAMEUSER" terminated normally");
  trace_fini();
}
