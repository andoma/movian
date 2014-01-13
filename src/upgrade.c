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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

#include "showtime.h"
#include "upgrade.h"
#include "arch/arch.h"
#include "arch/halloc.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_json.h"
#include "htsmsg/htsmsg_store.h"
#include "misc/sha.h"
#include "misc/str.h"
#include "settings.h"
#include "notifications.h"

#if CONFIG_BSPATCH
#include "ext/bspatch/bspatch.h"
#endif

#if STOS
#include <sys/mount.h>
#endif

extern char *showtime_bin;

static const char *ctrlbase = "http://showtime.lonelycoder.com/upgrade/";
static const char *artifact_type;
static const char *archname;

static prop_t *upgrade_root;
static prop_t *upgrade_status;
static prop_t *upgrade_error;
static prop_t *upgrade_progress;
static prop_t *upgrade_task;
static char *upgrade_track;

static char *showtime_download_url;
static uint8_t showtime_download_digest[20];
static int showtime_download_size;

static int notify_upgrades;
static int inhibit_checks = 1;
static prop_t *news_ref;

#if STOS
static const char *ctrlbase_stos = "http://showtime.lonelycoder.com/stos/";
static int stos_upgrade_needed;
static int stos_current_version;
static int stos_req_version;
static int stos_avail_version;
static htsmsg_t *stos_artifacts;
#endif

/**
 *
 * global.upgrade
 *
 *   .track ("testing", "stable", etc)
 *   .availableVersion
 *   .status   upToDate
 *             checking
 *             canUpgrade
 *             prepare
 *             download
 *             patch
 *             installing
 *             upgradeError
 *             checkError
 *             countdown
 *   .errorstr
 *   .downloadSize
 *   .progress (in percent)
 *   .eventSink
 *   .changelog
 *        .version
 *        .text
 *
 */


/**
 *
 */
static void
install_error(const char *str, const char *url)
{
  prop_set_string(upgrade_error, str);
  prop_set_string(upgrade_status, "upgradeError");
  if(url)
    TRACE(TRACE_ERROR, "upgrade", "Download of %s failed -- %s", 
	  url, str);
  else
    TRACE(TRACE_ERROR, "upgrade", "Error occured: %s", str);
}


static int current_download_size;

/**
 *
 */
static int
download_callback(void *opaque, int loaded, int total)
{
  if(!total)
    total = current_download_size;

  prop_set(upgrade_root, "size", PROP_SET_INT, total);
  prop_set_float(upgrade_progress, (float)loaded / (float)total);
  return 0;
}

/**
 *
 */
static int
upgrade_file(int accept_patch, const char *fname, const char *url,
	     int size, const uint8_t *expected_digest, const char *task)
{
  uint8_t digest[20];
  char digeststr[41];
  char errbuf[1024];
  int fd;
  struct http_header_list req_headers;
  struct http_header_list response_headers;

  void *current_data = NULL;
  int current_size = 0;

  current_download_size = size;

  sha1_decl(shactx);

  LIST_INIT(&req_headers);
  LIST_INIT(&response_headers);

  prop_set_string(upgrade_task, task);

  prop_set_float(upgrade_progress, 0);
  prop_set_string(upgrade_status, "prepare");

#if CONFIG_BSPATCH
  char ae[128];
  ae[0] = 0;
  if(accept_patch) {

    // Figure out SHA-1 of currently running binary

    fd = open(fname, O_RDONLY);
    if(fd != -1) {

      struct stat st;
      if(fstat(fd, &st)) {
	close(fd);
	return -1;
      }

      current_size = st.st_size;
      current_data = halloc(current_size);
      if(current_data == NULL) {
	close(fd);
	return -1;
      }
      if(read(fd, current_data, current_size) != current_size) {
	hfree(current_data, current_size);
	close(fd);
	return -1;
      }
      close(fd);

      sha1_init(shactx);
      sha1_update(shactx, current_data, current_size);
      sha1_final(shactx, digest);
      bin2hex(digeststr, sizeof(digeststr), digest, sizeof(digest));
      snprintf(ae, sizeof(ae), "bspatch-from-%s", digeststr);
      http_header_add(&req_headers, "Accept-Encoding", ae, 0);
      TRACE(TRACE_DEBUG, "upgrade", "Asking for patch for %s (%s)",
	    fname, digeststr);
    }
  }
#endif

  prop_set_string(upgrade_status, "download");
  TRACE(TRACE_INFO, "upgrade", "Starting download of %s (%d bytes)", 
	url, size);
 
  buf_t *b;

  int r = http_req(url,
                   HTTP_RESULT_PTR(&b),
                   HTTP_ERRBUF(errbuf, sizeof(errbuf)),
                   HTTP_FLAGS(FA_COMPRESSION),
                   HTTP_RESPONSE_HEADERS(&response_headers),
                   HTTP_REQUEST_HEADERS(&req_headers),
                   HTTP_PROGRESS_CALLBACK(download_callback, NULL),
                   NULL);
  
  if(r) {
    install_error(errbuf, url);

    if(current_data)
      hfree(current_data, current_size);

    return -1;
  }

#if CONFIG_BSPATCH

  const char *encoding = http_header_get(&response_headers,
					 "Content-Encoding");

  int got_patch = encoding && !strcmp(encoding, ae);

  if(current_data) {

    if(got_patch) {
      TRACE(TRACE_DEBUG, "upgrade", "Received upgrade as patch (%d bytes)",
	    (int)b->b_size);

      prop_set_string(upgrade_status, "patch");
      buf_t *new = bspatch(current_data, current_size, b->b_ptr, b->b_size);
      buf_release(b);
      if(new == NULL) {
	TRACE(TRACE_DEBUG, "upgrade", "Patch is corrupt");
	hfree(current_data, current_size);
	return -1;
      }
      b = new;
    }
    hfree(current_data, current_size);
  }
#endif

  TRACE(TRACE_DEBUG, "upgrade", "Verifying SHA-1 of %d bytes",
        (int)b->b_size);


  prop_set_string(upgrade_status, "verify");

  int match;

  sha1_init(shactx);
  sha1_update(shactx, b->b_ptr, b->b_size);
  sha1_final(shactx, digest);

  match = !memcmp(digest, expected_digest, 20);

  bin2hex(digeststr, sizeof(digeststr), digest, sizeof(digest));
  TRACE(TRACE_DEBUG, "upgrade", "SHA-1 of downloaded file: %s (%s)", digeststr,
	match ? "match" : "no match");

  if(!match) {
    install_error("SHA-1 sum mismatch", url);
    buf_release(b);
    return -1;
  }

  prop_set_string(upgrade_status, "install");

  prop_set(upgrade_root, "size", PROP_SET_INT, b->b_size);
  prop_set_float(upgrade_progress, 0);

  const char *instpath;

  int overwrite = 1;
#ifdef STOS
  overwrite = 0;
#endif

  if(overwrite) {

    if(unlink(fname)) {
      if(gconf.upgrade_path == NULL) {
	install_error("Unlink failed", url);
	buf_release(b);
	return -1;
      }
    } else {
      TRACE(TRACE_DEBUG, "upgrade", "Executable removed, rewriting");
    }

    instpath = fname;

  } else {

    char dlpath[PATH_MAX];
    snprintf(dlpath, sizeof(dlpath), "%s.tmp", fname);
    instpath = dlpath;


  }

  TRACE(TRACE_INFO, "upgrade", "Writing %s from %d bytes received",
	instpath, (int)b->b_size);

  int flags = O_CREAT | O_RDWR;
#ifdef STOS
  flags |= O_SYNC;
#endif

  if(!overwrite)
    flags |= O_TRUNC;

  fd = open(instpath, flags, 0777);
  if(fd == -1) {
    install_error("Unable to open file", url);
    buf_release(b);
    return -1;
  }


  int len = b->b_size;
  void *ptr = b->b_ptr;


  while(len > 0) {
    int to_write = MIN(len, 65536);
    r = write(fd, ptr, to_write);
    if(r != to_write) {
      install_error(strerror(errno), url);
      close(fd);
      unlink(instpath);
      buf_release(b);
      return -1;
    }

    len -= to_write;
    ptr += to_write;
    prop_set_float(upgrade_progress,
		   (float)(b->b_size - len) / (float)b->b_size);
  }

  buf_release(b);

  if(close(fd)) {
    install_error(strerror(errno), url);
    unlink(instpath);
    return -1;
  }

  if(!overwrite) {
    TRACE(TRACE_INFO, "upgrade", "Renaming %s -> %s", instpath, fname);
    if(rename(instpath, fname)) {
      install_error(strerror(errno), url);
      unlink(instpath);
      return -1;
    }
  }

#ifdef STOS
  arch_sync_path(fname);
#endif
  return 0;
}


/**
 *
 */
#if STOS
static int
stos_check_upgrade(void)
{
  char url[1024];
  char errbuf[256];
  buf_t *b;
  snprintf(url, sizeof(url), "%s/master-%s.json", ctrlbase_stos, archname);

  b = fa_load(url, NULL, errbuf, sizeof(errbuf),
              NULL, FA_DISABLE_AUTH, NULL, NULL);
  if(b == NULL) {
    TRACE(TRACE_ERROR, "STOS", "Unable to query for STOS manifest -- %s",
	  errbuf);
    return -1;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(buf_cstr(b));
  buf_release(b);

  if(doc == NULL) {
    TRACE(TRACE_ERROR, "STOS", "Malformed JSON");
    return -1;
  }

  const char *version = htsmsg_get_str(doc, "version");
  if(version == NULL) {
    htsmsg_destroy(doc);
    return -1;
  }

  stos_avail_version = showtime_parse_version_int(version);
  TRACE(TRACE_DEBUG, "STOS", "Available version: %s (%d)",
	version, stos_avail_version);
  

  htsmsg_destroy(stos_artifacts);
  stos_artifacts = NULL;

  htsmsg_field_t *f = htsmsg_field_find(doc, "artifacts");

  if(f == NULL) {
    htsmsg_destroy(doc);
    return -1;
  }
  stos_artifacts = htsmsg_detach_submsg(f);
  htsmsg_destroy(doc);

  if(stos_artifacts == NULL)
    return -1;

  HTSMSG_FOREACH(f, stos_artifacts) {
    htsmsg_t *a;
    if((a = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    const char *type = htsmsg_get_str(a, "type");
    if(type == NULL)
      goto bad_artifacts;

    if(strcmp(type, "sqfs") && strcmp(type, "bin"))
      continue;

    const char *dlurl = htsmsg_get_str(a, "url");
    const char *sha1  = htsmsg_get_str(a, "sha1");
    int dlsize        = htsmsg_get_u32_or_default(a, "size", 0);
    const char *name  = htsmsg_get_str(a, "name");
    if(dlurl == NULL || sha1 == NULL || dlsize == 0 || name == NULL)
      goto bad_artifacts;
  }


  return 0;

 bad_artifacts:
  htsmsg_destroy(stos_artifacts);
  stos_artifacts = NULL;
  return 1;
}
#endif


/**
 *
 */
static void
check_upgrade_err(const char *msg)
{
  prop_set_string(upgrade_error, msg);
  prop_set_string(upgrade_status, "checkError");
}


/**
 *
 */
static int
check_upgrade(int set_news)
{
  char url[1024];
  buf_t *b;
  htsmsg_t *json;
  char errbuf[1024];

  if(inhibit_checks)
    return 0;

  if(upgrade_track == NULL) {
    check_upgrade_err("No release track specified");
    return 0;
  }

  prop_set_string(upgrade_status, "checking");

  TRACE(TRACE_DEBUG, "Upgrade", "Checking upgrades for %s-%s",
	upgrade_track, archname);

  snprintf(url, sizeof(url), "%s/%s-%s.json", ctrlbase, upgrade_track,
	   archname);

  b = fa_load(url, NULL, errbuf, sizeof(errbuf),
              NULL, FA_DISABLE_AUTH, NULL, NULL);
  if(b == NULL) {
    check_upgrade_err(errbuf);
    return 1;
  }

  json = htsmsg_json_deserialize(buf_cstr(b));
  buf_release(b);

  if(json == NULL) {
    check_upgrade_err("Malformed JSON in repository");
    return 0;
  }

#if STOS
  stos_upgrade_needed = 0;
  htsmsg_t *manifest = htsmsg_get_map(json, "manifest");
  if(manifest != NULL) {

    const char *stosVersion = htsmsg_get_str(manifest, "stosVersion");
    if(stosVersion != NULL) {
      stos_req_version = showtime_parse_version_int(stosVersion);

      if(stos_current_version < stos_req_version) {
	stos_upgrade_needed = 1;
	TRACE(TRACE_DEBUG, "STOS", "Required version for upgrade: %s (%d)",
	      stosVersion, stos_req_version);

	TRACE(TRACE_DEBUG, "STOS",
	      "Need to perform STOS upgrade, checking what is available");
	if(stos_check_upgrade()) {
	  prop_set_string(upgrade_error,
			  "Failed to find any STOS updates");
	  goto err;
	}

	if(stos_avail_version < stos_req_version) {
	  prop_set_string(upgrade_error,
			  "Required STOS version not available");
	  goto err;
	}

      }
    }
  }
#endif
  // Find an artifact for us

  const char *dlurl = NULL;
  const char *sha1 = NULL;
  int dlsize = 0;
  const char *ver;

  htsmsg_t *artifacts = htsmsg_get_list(json, "artifacts");
  if(artifacts != NULL) {
    htsmsg_field_t *f;
    HTSMSG_FOREACH(f, artifacts) {
      htsmsg_t *a;
      if((a = htsmsg_get_map_by_field(f)) == NULL)
	continue;

      const char *type = htsmsg_get_str(a, "type");

      if(type == NULL || strcmp(artifact_type, type))
	continue;

      dlurl = htsmsg_get_str(a, "url");
      sha1 = htsmsg_get_str(a, "sha1");
      dlsize = htsmsg_get_u32_or_default(a, "size", 0);
      break;
    }
  }

  ver = htsmsg_get_str(json, "version");

  if(dlurl == NULL || dlsize == 0 || sha1 == NULL || ver == NULL) {
    prop_set_string(upgrade_error, "No URL or size present");
    goto err;
  }

  hex2bin(showtime_download_digest, sizeof(showtime_download_digest), sha1);

  mystrset(&showtime_download_url, dlurl);

  prop_set(upgrade_root, "track", PROP_SET_STRING, upgrade_track);
  prop_set(upgrade_root, "availableVersion", PROP_SET_STRING, ver);

  showtime_download_size = dlsize;

  prop_set(upgrade_root, "size", PROP_SET_INT, dlsize);

  int canUpgrade = gconf.enable_omnigrade;
  
  if(ver != NULL) {
    int current_ver = showtime_get_version_int();
    int available_ver = showtime_parse_version_int(ver);
    if(available_ver > current_ver) {
      canUpgrade = 1;
    }
  }

  if(canUpgrade) {
    prop_set_string(upgrade_status, "canUpgrade");
  } else {
    prop_set_string(upgrade_status, "upToDate");
  }

  prop_destroy(news_ref);
  prop_ref_dec(news_ref);

  if(set_news && canUpgrade) {
    rstr_t *r = _("Showtime version %s is available");
    rstr_t *s = _("Open download page");
    char buf[128];
    snprintf(buf, sizeof(buf), rstr_get(r), ver);
    news_ref = add_news(buf, buf, "page:upgrade", rstr_get(s));
    rstr_release(r);
    rstr_release(s);
  }

  // Update changelog

  prop_t *changelog = prop_create(upgrade_root, "changelog");
  prop_destroy_childs(changelog);
  htsmsg_t *chlog = htsmsg_get_list(json, "changelog");

  if(chlog) {
    htsmsg_field_t *f;
    HTSMSG_FOREACH(f, chlog) {
      htsmsg_t *le;
      if((le = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      const char *version = htsmsg_get_str(le, "version");
      const char *text    = htsmsg_get_str(le, "desc");
      prop_t *q = prop_create_root(NULL);
      prop_set_string(prop_create(q, "version"), version);
      prop_set_string(prop_create(q, "text"), text);
      if(prop_set_parent(q, changelog))
	abort();
    }
  }
  htsmsg_destroy(json);
  return 0;
 err:
  prop_set_string(upgrade_status, "checkError");
  htsmsg_destroy(json);
  return 0;
}

#if STOS
static int
stos_perform_upgrade(int accept_patch)
{
  char localfile[256];
  int rval = 0;
  if(mount("/dev/mmcblk0p1", "/boot", "vfat", MS_REMOUNT, NULL)) {
    install_error("Unable to remount /boot to read-write", NULL);
    return 2;
  }

  htsmsg_field_t *f;
  HTSMSG_FOREACH(f, stos_artifacts) {
    htsmsg_t *a;
    if((a = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    const char *type = htsmsg_get_str(a, "type");
    if(strcmp(type, "sqfs") && strcmp(type, "bin"))
      continue;

    const char *dlurl = htsmsg_get_str(a, "url");
    const char *sha1  = htsmsg_get_str(a, "sha1");
    int dlsize        = htsmsg_get_u32_or_default(a, "size", 0);
    const char *name  = htsmsg_get_str(a, "name");

    uint8_t digest[20];
    hex2bin(digest, sizeof(digest), sha1);
    char *n = mystrdupa(name);

    char *postfix = strrchr(n, '.');
    if(postfix == NULL)
      continue;
    char *dash = strchr(n, '-');
    if(dash == NULL)
      continue;

    *dash = 0;
    
    snprintf(localfile, sizeof(localfile), "/boot/%s%s", n, postfix);

    TRACE(TRACE_DEBUG, "STOS", "Downloading %s (%s) to %s",
	  dlurl, name, localfile);

    if(upgrade_file(accept_patch, localfile, dlurl, dlsize, digest, n)) {
      rval = 1;
      break;
    }
  }
  TRACE(TRACE_DEBUG, "STOS", "Syncing filesystems");
  sync();
  TRACE(TRACE_DEBUG, "STOS", "Syncing filesystems done");
  return rval;
}

#endif


/**
 *
 */
static int
attempt_upgrade(int accept_patch)
{
#if STOS
  if(stos_upgrade_needed) {
    int r = stos_perform_upgrade(accept_patch);
    if(r)
      return r;
  }
#endif
  const char *fname = gconf.upgrade_path ?: gconf.binary;

  if(upgrade_file(accept_patch, fname,
		  showtime_download_url,
		  showtime_download_size,
		  showtime_download_digest,
		  "Showtime"))
    return 1;

  TRACE(TRACE_INFO, "upgrade", "All done, restarting");
  prop_set_string(upgrade_status, "countdown");
  prop_t *cnt = prop_create(upgrade_root, "countdown");
  int i;
  for(i = 3; i > 0; i--) {
    prop_set_int(cnt, i);
    sleep(1);
  }

#if STOS
  if(stos_upgrade_needed)
    showtime_shutdown(SHOWTIME_EXIT_REBOOT);
  else
#endif
    showtime_shutdown(SHOWTIME_EXIT_RESTART);
  return 0;
}



static void *
install_thread(void *aux)
{
#if CONFIG_BSPATCH
  int r = attempt_upgrade(1);
  if(r != -1)
    return NULL;
#endif

  attempt_upgrade(0);
  return NULL;
}


static void
install(void)
{
  hts_thread_create_detached("upgrade", install_thread, NULL,
			     THREAD_PRIO_BGTASK);
}


/**
 *
 */
static void
upgrade_cb(void *opaque, prop_event_t event, ...)
{
  va_list ap;
  event_t *e;
  va_start(ap, event);

  switch(event) {
  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      if(!strcmp(e->e_payload, "checkUpdates")) 
	check_upgrade(0);
      if(!strcmp(e->e_payload, "install")) 
	install();
    }
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
set_upgrade_track(void *opaque, const char *str)
{
  mystrset(&upgrade_track, str);
  check_upgrade(0);
}


#if STOS
/**
 *
 */
static void
stos_get_current_version(void)
{
  char buf[128] = {0};
  FILE *fp = fopen("/stosversion", "r");
  if(fp == NULL)
    return;
  if(fgets(buf, 127, fp) != NULL) {
    char *x = strchr(buf, '\n');
    if(x)
      *x = 0;
    stos_current_version = showtime_parse_version_int(buf);
    TRACE(TRACE_DEBUG, "STOS", "Current version: %s (%d)", buf, 
	  stos_current_version);
  }
  fclose(fp);
}



#endif

/**
 *
 */
void
upgrade_init(void)
{
  const char *fname = gconf.upgrade_path ?: gconf.binary;

  if(fname == NULL)
    return;

#if STOS
  stos_get_current_version();
#endif

#if RPISTOS
  artifact_type = "sqfs";
  archname = "rpi";
#endif

#if PS3
  artifact_type = "self";
  archname = "ps3";
#endif

  if(artifact_type == NULL || archname == NULL)
    return;

  upgrade_root     = prop_create(prop_get_global(), "upgrade");
  upgrade_status   = prop_create(upgrade_root, "status");
  upgrade_progress = prop_create(upgrade_root, "progress");
  upgrade_error    = prop_create(upgrade_root, "error");
  upgrade_task     = prop_create(upgrade_root, "task");


  htsmsg_t *store;

  if((store = htsmsg_store_load("upgrade")) == NULL)
    store = htsmsg_create_map();

  settings_create_separator(gconf.settings_general,
			  _p("Software upgrade"));

  setting_create(SETTING_MULTIOPT, gconf.settings_general,
                 SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Upgrade to releases from")),
                 SETTING_HTSMSG("track", store, "upgrade"),
#if defined(PS3)
                 SETTING_OPTION("stable",  _p("Stable")),
#endif
                 SETTING_OPTION("testing", _p("Testing")),
                 SETTING_OPTION_CSTR("master", "Bleeding Edge (Very unstable)"),
                 SETTING_CALLBACK(set_upgrade_track, NULL),
                 NULL);


  setting_create(SETTING_BOOL, gconf.settings_general, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Notify about upgrades")),
                 SETTING_VALUE(1),
                 SETTING_HTSMSG("check", store, "upgrade"),
                 SETTING_WRITE_BOOL(&notify_upgrades),
                 NULL);

  prop_t *p = prop_create_root(NULL);
  prop_link(_p("Check for updates now"),
	    prop_create(prop_create(p, "metadata"), "title"));
  prop_set_string(prop_create(p, "type"), "load");
  prop_set_string(prop_create(p, "url"), "page:upgrade");

  if(prop_set_parent(p, prop_create(gconf.settings_general, "nodes")))
     abort();

  inhibit_checks = 0;

  prop_subscribe(0,
		 PROP_TAG_CALLBACK, upgrade_cb, NULL,
		 PROP_TAG_NAME("global", "upgrade", "eventSink"),
		 NULL);
}


/**
 *
 */
int
upgrade_refresh(void)
{
  return check_upgrade(notify_upgrades);
}
