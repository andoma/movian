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
#pragma once
#include "config.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#include "arch/threads.h"
#include "misc/rstr.h"

#include "compiler.h"

#ifndef static_assert
#if (__GNUC__ >= 4 && __GNUC_MINOR__ >=6) || (__GNUC__ >= 5)
#define static_assert(x, y) _Static_assert(x, y)
#else
#define static_assert(x, y)
#endif
#endif

void parse_opts(int argc, char **argv);

void main_init(void);

void main_fini(void);

void swrefresh(void);

extern void panic(const char *fmt, ...)
  attribute_printf(1,2) attribute_noreturn;

extern const char *app_dataroot(void);

#define BYPASS_CACHE  ((int *)-1)
#define DISABLE_CACHE ((int *)-2)
#define NOT_MODIFIED ((void *)-1)


#define ONLY_CACHED(p) ((p) != BYPASS_CACHE && (p) != NULL)

// NLS


#define _(string) nls_get_rstring(string)
#define _pl(a,b,c) nls_get_rstringp(a,b,c)
#define _p(string) nls_get_prop(string)

rstr_t *nls_get_rstring(const char *string);

struct prop;
struct prop *nls_get_prop(const char *string);

rstr_t *nls_get_rstringp(const char *string, const char *singularis, int val);

#define URL_MAX 2048
#define HOSTNAME_MAX 256 /* FQDN is max 255 bytes including ending dot */

void app_shutdown(int retcode);

void app_flush_caches(void);

uint32_t app_get_version_int(void);

uint32_t parse_version_int(const char *str);

extern int64_t arch_get_ts(void);

extern int64_t arch_get_avtime(void);

extern const char *arch_get_system_type(void);

extern uint64_t arch_get_seed(void);

/**
 *
 */

enum {
  TRACE_EMERG,
  TRACE_ERROR,
  TRACE_INFO,
  TRACE_DEBUG
};

#define TRACE_NO_PROP 0x1

void trace_init(void);

void trace_fini(void);

void tracelog(int flags, int level, const char *subsys, const char *fmt, ...);

void tracev(int flags, int level, const char *subsys, const char *fmt, va_list ap);

void trace_arch(int level, const char *prefix, const char *buf);

#define TRACE(level, subsys, fmt, ...) \
  tracelog(0, level, subsys, fmt, ##__VA_ARGS__)


void hexdump(const char *pfx, const void *data, int len);

#define mystrdupa(n) ({ int my_l = strlen(n); \
 char *my_b = alloca(my_l + 1); \
 memcpy(my_b, n, my_l + 1); })

#define mystrndupa(n, len) ({ \
 char *my_b = alloca(len + 1); \
 my_b[len] = 0; \
 memcpy(my_b, n, len); \
})


static __inline unsigned int mystrhash(const char *s)
{
  unsigned int v = 5381;
  while(*s)
    v += (v << 5) + v + *s++;
  return v;
}

static __inline void mystrset(char **p, const char *s)
{
  free(*p);
  *p = s ? strdup(s) : NULL;
}


static __inline const char *mystrbegins(const char *s1, const char *s2)
{
  while(*s2)
    if(*s1++ != *s2++)
      return NULL;
  return s1;
}


/*
 * Memory allocation wrappers
 * These are used whenever the caller can deal with failure 
 * Some platform may have the standard libc ones to assert() on
 * OOM conditions
 */

void *mymalloc(size_t size);

void *myrealloc(void *ptr, size_t size);

static __inline void *myreallocf(void *ptr, size_t size)
{
  void *r = myrealloc(ptr, size);
  if(ptr != NULL && size > 0 && r == NULL)
    free(ptr);
  return r;
}

void *mycalloc(size_t count, size_t size);

void *mymemalign(size_t align, size_t size);

void runcontrol_activity(void);

void shutdown_hook_run(int early);

void *shutdown_hook_add(void (*fn)(void *opaque, int exitcode), void *opaque,
			int early);

#define APP_EXIT_OK       0
#define APP_EXIT_STANDBY  10
#define APP_EXIT_POWEROFF 11
#define APP_EXIT_LOGOUT   12
#define APP_EXIT_RESTART  13
#define APP_EXIT_SHELL    14
#define APP_EXIT_REBOOT   15


typedef struct gconf {
  int exit_code;


  char *dirname;   // Directory where executable resides
  char *binary;    // Executable itself
  char *upgrade_path; // What to upgrade

  char *cache_path;
  char *persistent_path;

  int concurrency;
  int trace_level;
  int trace_to_syslog;
  int listen_on_stdin;
  int libavlog;
  int noui;
  int fullscreen;
  int swrefresh;
  int debug_glw;
  int show_usage_events;

  int can_standby;
  int can_poweroff;
  int can_open_shell;
  int can_logout;
  int can_restart;
  int can_not_exit;

  int disable_upnp;
  int disable_upgrades;
  int disable_sd;
  int convert_pointer_to_touch;

  int disable_analytics;
  int enable_bin_replace;
  int enable_omnigrade;
  int enable_http_debug;
  int disable_http_reuse;
  int enable_experimental;
  int enable_detailed_avdiff;
  int enable_hls_debug;
  int enable_ftp_client_debug;
  int enable_ftp_server_debug;
  int enable_cec_debug;
  int enable_fa_scanner_debug;
  int enable_smb_debug;
  int enable_mem_debug;
  int enable_nav_always_close;
  int enable_kvstore_debug;
  int enable_icecast_debug;
  int enable_image_debug;
  int enable_settings_debug;
  int enable_thread_debug;
  int enable_metadata_debug;
  int enable_upnp_debug;
  int enable_ecmascript_debug;
  int enable_input_event_debug;

  int enable_torrent_debug;
  int enable_torrent_tracker_debug;
  int enable_torrent_diskio_debug;
  int enable_torrent_peer_connection_debug;
  int enable_torrent_peer_upload_debug;
  int enable_torrent_peer_download_debug;


  char **devplugins;
  const char *plugin_repo;
  const char *load_ecmascript;
  int bypass_ecmascript_acl;

  const char *initial_url;
  const char *initial_view;

  char *ui;
  char *skin;

#define TIME_FORMAT_UNSET 0
#define TIME_FORMAT_24    1
#define TIME_FORMAT_12    2

  int time_format;
  int time_format_system;

  struct prop *settings_apps;
  struct prop *settings_sd;
  struct prop *settings_general;
  struct prop *settings_dev;
  struct prop *settings_network;
  struct prop_concat *settings_look_and_feel;
  struct prop *settings_bittorrent;

  struct setting *setting_av_volume; // Maybe move to audio.h
  struct setting *setting_av_sync;   // Maybe move to audio.h

  hts_mutex_t state_mutex;
  hts_cond_t state_cond;

  int state_plugins_loaded;

  int fa_allow_delete;
  int fa_kvstore_as_xattr;
  int fa_browse_archives;
  int show_filename_extensions;
  int ignore_the_prefix;

  uint32_t log_server_ipv4;
  int log_server_port;

  int shell_fd;

  char proxy_host[64];
  uint16_t proxy_port;

  char system_name[64];

  char device_id[64];

  char os_info[128];

  char lang[32];

  char device_type[64];

  const char *remote_ui;

} gconf_t;

extern gconf_t gconf;

/* From version.c */
extern const char *htsversion;
extern const char *htsversion_full;


typedef struct inithelper {
  struct inithelper *next;
  enum {
    INIT_GROUP_NET,
    INIT_GROUP_API,
    INIT_GROUP_IPC,
    INIT_GROUP_ASYNCIO,
    INIT_GROUP_GRAPHICS,
  } group;
  void (*init)(void);
  void (*fini)(void);
} inithelper_t;

extern inithelper_t *inithelpers;

#define INITME(group_, init_, fini_)                               \
  static inithelper_t HTS_JOIN(inithelper, __LINE__) = {	   \
    .group = group_,						   \
    .init = init_,                                                 \
    .fini = fini_                                                  \
  };								   \
  INITIALIZER(HTS_JOIN(inithelperctor, __LINE__))                  \
  {								   \
    inithelper_t *ih = &HTS_JOIN(inithelper, __LINE__);		   \
    ih->next = inithelpers;					   \
    inithelpers = ih;						   \
  }

void init_group(int group);

void fini_group(int group);
