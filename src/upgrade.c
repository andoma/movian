/*
 *  Built-in upgrade
 *  Copyright (C) 2012 Andreas Öman
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "showtime.h"
#include "upgrade.h"
#include "arch/arch.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/sha.h"
#include "misc/string.h"
#include "settings.h"


extern char *showtime_bin;

static const char *ctrlbase = "http://showtime.lonelycoder.com/upgrade/";
static const char *artifact_type;
static const char *archname;

static prop_t *upgrade_root;
static prop_t *upgrade_status;
static prop_t *upgrade_error;
static prop_t *upgrade_progress;
static char *upgrade_track;
static char *download_url;
static uint8_t download_digest[20];
static int download_size;
//static int autocheck;


/**
 *
 * global.upgrade
 *
 *   .track ("testing", "stable", etc)
 *   .availableVersion
 *   .status   upToDate
 *             checking
 *             canUpgrade
 *             downloading
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





static void
check_upgrade(void)
{
  char url[1024];
  char *result;
  htsmsg_t *json;
  char errbuf[1024];

  if(upgrade_track == NULL) {
    prop_set_string(upgrade_error, "No release track specified");
    goto err;
  }

  prop_set_string(upgrade_status, "checking");

  TRACE(TRACE_DEBUG, "Upgrade", "Checking upgrades for %s-%s",
	upgrade_track, archname);

  snprintf(url, sizeof(url), "%s/%s-%s.json", ctrlbase, upgrade_track,
	   archname);

  result = fa_load(url, NULL, NULL, errbuf, sizeof(errbuf),
		   NULL, 0, NULL, NULL);
  if(result == NULL) {
    prop_set_string(upgrade_error, errbuf);
  err:
    prop_set_string(upgrade_status, "checkError");
    return;
  }
  
  json = htsmsg_json_deserialize(result);
  free(result);

  if(json == NULL) {
    prop_set_string(upgrade_error, "Malformed JSON in repository");
    goto err;
  }

  // Find an artifact for us

  const char *dlurl = NULL;
  const char *sha1 = NULL;
  int dlsize = 0;
  

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

  if(dlurl == NULL || dlsize == 0 || sha1 == NULL) {
    prop_set_string(upgrade_error, "No URL or size present");
    goto err;
  }

  hex2bin(download_digest, sizeof(download_digest), sha1);

  mystrset(&download_url, dlurl);

  prop_set(upgrade_root, "track", NULL, PROP_SET_STRING, upgrade_track);

  const char *s;
  s = htsmsg_get_str(json, "version");
  prop_set(upgrade_root, "availableVersion", NULL, PROP_SET_STRING, s);

  download_size = dlsize;

  prop_set(upgrade_root, "size", NULL, PROP_SET_INT, dlsize);

  int canUpgrade = 0;
  
  if(s != NULL) {
    int current_ver = showtime_get_version_int();
    int available_ver = showtime_parse_version_int(s);
    if(available_ver > current_ver) {
      canUpgrade = 1;
    }
  }

  printf("canUpgrade=%d\n", canUpgrade);
  if(canUpgrade) {
    prop_set_string(upgrade_status, "canUpgrade");
  } else {
    prop_set_string(upgrade_status, "upToDate");
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
}


/**
 *
 */
static int
download_callback(void *opaque, int loaded, int total)
{
  if(!total)
    total = download_size;

  prop_set(upgrade_root, "size", NULL, PROP_SET_INT, total);
  prop_set_float(upgrade_progress, (float)loaded / (float)total);
  return 0;
}


static void
install_error(const char *str)
{
  prop_set_string(upgrade_error, str);
  prop_set_string(upgrade_status, "upgradeError");
  TRACE(TRACE_ERROR, "upgrade", "Download of %s failed -- %s", 
	download_url, str);

}

/**
 *
 */
static void
install(void)
{
  char errbuf[1024];

  prop_set_float(upgrade_progress, 0);
  prop_set_string(upgrade_status, "download");
  TRACE(TRACE_INFO, "upgrade", "Starting download of %s (%d bytes)", 
	download_url, download_size);
 
  char *result;
  size_t result_size;

  int r = http_request(download_url, NULL, &result, &result_size,
		       errbuf, sizeof(errbuf), NULL, NULL, FA_DEBUG,
		       NULL, NULL, NULL, download_callback, NULL, NULL);
  
  if(r) {
    install_error(errbuf);
    return;
  }

  TRACE(TRACE_DEBUG, "upgrade", "Verifying SHA-1 of %d bytes",
	(int)result_size);

  prop_set_string(upgrade_status, "install");

  sha1_decl(shactx);
  uint8_t digest[20];
  sha1_init(shactx);
  sha1_update(shactx, (void *)result, result_size);
  sha1_final(shactx, digest);

  char digeststr[41];
  bin2hex(digeststr, sizeof(digeststr), digest, sizeof(digest));
  
  int match = !memcmp(digest, download_digest, 20);

  TRACE(TRACE_DEBUG, "upgrade", "SHA-1 of downloaded file: %s (%s)", digeststr,
	match ? "match" : "no match");

  if(!match) {
    install_error("SHA-1 sum mismatch");
    free(result);
    return;
  }

  const char *fname = showtime_bin;

  TRACE(TRACE_INFO, "upgrade", "Replacing %s with %d bytes received",
	fname, (int)result_size);

  int fd = open(fname, O_TRUNC | O_RDWR, 0777);
  if(fd == -1) {
    install_error("Unable to open file");
    free(result);
    return;
  }

  int fail = write(fd, result, result_size) != result_size || close(fd);
  free(result);

  if(fail) {
    install_error("Unable to write to file");
    return;
  }

  TRACE(TRACE_INFO, "upgrade", "All done, restarting");

  prop_set_string(upgrade_status, "countdown");
  prop_t *cnt = prop_create(upgrade_root, "countdown");
  int i;
  for(i = 3; i > 0; i--) {
    prop_set_int(cnt, i);
    sleep(1);
  }
  showtime_shutdown(13);
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
	check_upgrade();
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
  check_upgrade();
}



/**
 *
 */
/*
static void
set_check_for_upgrades(void *opaque, int v)
{
  autocheck = v;
}
*/

/**
 *
 */
void
upgrade_init(void)
{
  
  if(showtime_bin == NULL)
    return;

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


  htsmsg_t *store;

  if((store = htsmsg_store_load("upgrade")) == NULL)
    store = htsmsg_create_map();

  setting_t *x;

  settings_create_divider(settings_general,
			  _p("Software upgrade"));

  x = settings_create_multiopt(settings_general, "track",
			       _p("Upgrade to releases from"), 0);

  //  settings_multiopt_add_opt(x, "stable", _p("Stable"), 1); // not yet
  settings_multiopt_add_opt(x, "testing", _p("Testing"), 1);

  settings_multiopt_initiate(x, set_upgrade_track, NULL, NULL, 
			     store, settings_generic_save_settings,
                             (void *)"upgrade");
  /*
  settings_create_bool(settings_general, "check",
		       _p("Automatically check for upgrades"), 0,
		       store, set_check_for_upgrades, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"upgrade");
  */

  prop_t *p = prop_create_root(NULL);
  prop_link(_p("Check for updates now"),
	    prop_create(prop_create(p, "metadata"), "title"));
  prop_set_string(prop_create(p, "type"), "load");
  prop_set_string(prop_create(p, "url"), "page:upgrade");

  if(prop_set_parent(p, prop_create(settings_general, "nodes")))
     abort();

  check_upgrade();

  prop_subscribe(0,
		 PROP_TAG_CALLBACK, upgrade_cb, NULL,
		 PROP_TAG_NAME("global", "upgrade", "eventSink"),
		 NULL);
}
