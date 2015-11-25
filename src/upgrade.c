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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>

#include "main.h"
#include "upgrade.h"
#include "arch/arch.h"
#include "arch/halloc.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/http_client.h"
#include "htsmsg/htsmsg_json.h"
#include "htsmsg/htsmsg_store.h"
#include "misc/sha.h"
#include "misc/str.h"
#include "settings.h"
#include "notifications.h"
#include "backend/backend.h"
#include "misc/minmax.h"
#include "usage.h"

#if CONFIG_BSPATCH
#include "ext/bspatch/bspatch.h"
#endif

#if STOS
#include <sys/mount.h>
#include <sys/utsname.h>
#endif

#ifdef __ANDROID__
#include "arch/android/android.h"
#endif

static HTS_MUTEX_DECL(upgrade_mutex);

static const char *ctrlbase = "http://upgrade.movian.tv/upgrade/2";
static const char *artifact_type;
static const char *archname;

static prop_t *upgrade_root;
static prop_t *upgrade_status;
static prop_t *upgrade_error;
static prop_t *upgrade_progress;
static prop_t *upgrade_task;
static char *upgrade_track;

static char *app_download_url;
static uint8_t app_download_digest[20];
static int app_download_size;
static char *app_download_name;

static int notify_upgrades;
static int inhibit_checks = 1;
static prop_t *news_ref;

#if STOS
static const char *ctrlbase_stos = "http://upgrade.movian.tv/stos/1";
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
 *             download
 *             upgradeError
 *             checkError
 *   .errorstr
 *   .downloadSize
 *   .progress (in percent)
 *   .eventSink
 *   .changelog
 *        .version
 *        .text
 *
 */


TAILQ_HEAD(artifact_queue, artifact);

typedef struct artifact {
  TAILQ_ENTRY(artifact) a_link;
  int a_size;
  char *a_url;
  char *a_temp_path;
  char *a_final_path;
  rstr_t *a_task;
  char *a_name;

  uint8_t a_digest[20];

  char a_check_partial_update;

  float a_progress_offset;
  float a_progress_scale;

  int a_progress_part;
  int a_progress_num_parts;

} artifact_t;


/**
 *
 */
static void
artifacts_free(struct artifact_queue *aq)
{
  artifact_t *a;
  while((a = TAILQ_FIRST(aq)) != NULL) {
    TAILQ_REMOVE(aq, a, a_link);
    free(a->a_url);
    free(a->a_temp_path);
    free(a->a_final_path);
    free(a->a_name);
    rstr_release(a->a_task);
    free(a);
  }
}


/**
 *
 */
static void
artifacts_compute_progressbar_scale(struct artifact_queue *aq)
{
  int total_size = 0;
  artifact_t *a;
  float offset = 0;

  TAILQ_FOREACH(a, aq, a_link)
    total_size += a->a_size;

  if(total_size == 0)
    return;

  TAILQ_FOREACH(a, aq, a_link) {
    a->a_progress_scale = (float)a->a_size / total_size;
    a->a_progress_offset = offset;
    offset += a->a_progress_scale;
  }
}


/**
 *
 */
static void
artifact_update_progress(const artifact_t *a, float p)
{
  p = p / a->a_progress_num_parts;
  p += (float)a->a_progress_part / a->a_progress_num_parts;

  prop_set_float(upgrade_progress,
                 a->a_progress_offset + a->a_progress_scale * p);
}
/**
 *
 */
static buf_t *
patched_config_file(buf_t *upd, char *upd_begin, char *upd_end,
		    const char *fname)
{
  char *cur_data = NULL;
  int   cur_len = 0;

  // Load currently installed file

  int fd = open(fname, O_RDONLY);
  if(fd == -1) {
    TRACE(TRACE_DEBUG, "upgrade",
	  "Partial update ignored, unable to open %s -- %s (%d)",
	  fname, strerror(errno), errno);
    return upd;
  }
  struct stat st;
  if(fstat(fd, &st)) {
    close(fd);
    return upd;
  }

  cur_len  = st.st_size;
  cur_data = malloc(cur_len);

  if(read(fd, cur_data, cur_len) != cur_len) {
    TRACE(TRACE_DEBUG, "upgrade",
	  "Partial update ignored, read failed from %s -- %s (%d)",
	  fname, strerror(errno), errno);
    free(cur_data);
    close(fd);
    return upd;
  }
  close(fd);


  // Search for markers in currently installed file

  char *cur_begin =
    find_str(cur_data, cur_len, "# BEGIN SHOWTIME CONFIG\n");

  if(cur_begin == NULL) {
    TRACE(TRACE_DEBUG, "upgrade",
	  "Partial update ignored, no BEGIN marker");
    goto fail;
  }

  char *cur_end =
    find_str(cur_data, cur_len, "# END SHOWTIME CONFIG\n");

  if(cur_end == NULL) {
    TRACE(TRACE_DEBUG, "upgrade",
	  "Partial update ignored, no END marker");
    goto fail;
  }


  if(cur_end < cur_begin) {
    TRACE(TRACE_DEBUG, "upgrade",
	  "Partial update ignored, END marker before BEGIN marker");
    goto fail;
  }

  char *cur_eof = cur_data + cur_len;

  /**
   * Ok, so new file will consist of
   *   seg1 cur_data  -> cur_begin
   *   seg2 upd_begin -> upd_end
   *   seg3 cur_end   -> cur_eof
   */

  const int seg1 = (cur_begin - cur_data);
  const int seg2 = (upd_end   - upd_begin);
  const int seg3 = (cur_eof   - cur_end);

  assert(seg1 >= 0);
  assert(seg2 >= 0);
  assert(seg3 >= 0);

  int new_size = seg1 + seg2 + seg3;
  buf_t *n = buf_create(new_size);

  char *out = buf_str(n);
  memcpy(out,               cur_data,  seg1);
  memcpy(out + seg1,        upd_begin, seg2);
  memcpy(out + seg1 + seg2, cur_end,   seg3);

  free(cur_data);

  buf_release(upd);
  return n;

 fail:
  free(cur_data);
  return upd;
}


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


/**
 *
 */
static int
download_callback(void *opaque, int loaded, int total)
{
  const artifact_t *a = opaque;

  if(!total)
    total = a->a_size;

  artifact_update_progress(a, (float)loaded / (float)total);
  return 0;
}

/**
 *
 */
static int
download_file(artifact_t *a, int try_patch)
{
  uint8_t digest[20];
  char digeststr[41];
  char errbuf[1024];
  int fd;
  struct http_header_list req_headers;
  struct http_header_list response_headers;

  void *current_data = NULL;
  int current_size = 0;

  if(a->a_url == NULL)
    return 0; // Nothing to download

  sha1_decl(shactx);

  TRACE(TRACE_INFO, "upgrade", "Downloading artifact %s", a->a_name);

  LIST_INIT(&req_headers);
  LIST_INIT(&response_headers);

  a->a_progress_num_parts = 2;

  a->a_progress_part = 0;
  artifact_update_progress(a, 0);
  prop_set_rstring(upgrade_task, a->a_task);

#if CONFIG_BSPATCH
  char ae[128];
  ae[0] = 0;
  if(try_patch) {

    TRACE(TRACE_DEBUG, "upgrade", "Computing hash of %s", a->a_final_path);

    // Figure out SHA-1 of currently running binary

    fd = open(a->a_final_path, O_RDONLY);
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
	    a->a_final_path, digeststr);
    }
  }
#endif

  TRACE(TRACE_DEBUG, "upgrade", "Starting download of %s (%d bytes)",
	a->a_url, a->a_size);

  buf_t *b;

  int r = http_req(a->a_url,
                   HTTP_RESULT_PTR(&b),
                   HTTP_ERRBUF(errbuf, sizeof(errbuf)),
                   HTTP_FLAGS(FA_COMPRESSION),
                   HTTP_RESPONSE_HEADERS(&response_headers),
                   HTTP_REQUEST_HEADERS(&req_headers),
                   HTTP_PROGRESS_CALLBACK(download_callback, a),
                   NULL);

  if(r) {
    http_headers_free(&response_headers);
    install_error(errbuf, a->a_url);

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

      buf_t *new = bspatch(current_data, current_size, b->b_ptr, b->b_size);
      buf_release(b);
      if(new == NULL) {
	TRACE(TRACE_DEBUG, "upgrade", "Patch is corrupt");
	hfree(current_data, current_size);
        http_headers_free(&response_headers);
	return -1;
      }
      b = new;
    }
    hfree(current_data, current_size);
  }
#endif

  http_headers_free(&response_headers);

  TRACE(TRACE_DEBUG, "upgrade", "Verifying SHA-1 of %d bytes",
        (int)b->b_size);


  int match;

  sha1_init(shactx);
  sha1_update(shactx, b->b_ptr, b->b_size);
  sha1_final(shactx, digest);

  match = !memcmp(digest, a->a_digest, 20);

  bin2hex(digeststr, sizeof(digeststr), digest, sizeof(digest));
  TRACE(TRACE_DEBUG, "upgrade", "SHA-1 of downloaded file: %s (%s)", digeststr,
	match ? "match" : "no match");

  if(!match) {
    install_error("SHA-1 sum mismatch", a->a_url);
    buf_release(b);
    return -1;
  }

  if(a->a_check_partial_update) {
    char *new_begin =
      find_str(buf_str(b), buf_len(b), "# BEGIN SHOWTIME CONFIG\n");

    char *new_end =
      find_str(buf_str(b), buf_len(b), "# END SHOWTIME CONFIG\n");

    if(new_begin && new_end > new_begin) {
      TRACE(TRACE_DEBUG, "upgrade",
	    "Attempting partial rewrite of %s", a->a_final_path);
      b = patched_config_file(b, new_begin, new_end, a->a_final_path);
    }
  }

  const char *dstpath = a->a_temp_path ? a->a_temp_path : a->a_final_path;


  TRACE(TRACE_DEBUG, "upgrade", "Writing %s from %d bytes received",
	dstpath, (int)b->b_size);

  int flags = O_CREAT | O_RDWR | O_TRUNC;
#if STOS
  flags |= O_SYNC;
#endif

  fd = open(dstpath, flags, 0777);
  if(fd == -1) {
    install_error("Unable to open file", dstpath);
    buf_release(b);
    return -1;
  }


  int len = b->b_size;
  void *ptr = b->b_ptr;

  a->a_progress_part++;

  while(len > 0) {
    int to_write = MIN(len, 65536);
    r = write(fd, ptr, to_write);
    if(r == -1) {

      if(errno == EAGAIN || errno == EINTR || errno == EINPROGRESS)
        continue;

      char err[256];
      snprintf(err, sizeof(err), "Write(%d) failed (%d): %s (%d)",
               to_write, r, strerror(errno), errno);
      install_error(err, dstpath);
      close(fd);
      unlink(dstpath);
      buf_release(b);
      return -1;
    }

    len -= r;
    ptr += r;

    artifact_update_progress(a, (float)(b->b_size - len) / b->b_size);
  }

  buf_release(b);

  if(close(fd)) {
    char err[256];
    snprintf(err, sizeof(err), "Close failed: %s (%d)", strerror(errno), errno);
    install_error(err, dstpath);
    unlink(dstpath);
    return -1;
  }
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

  b = fa_load(url,
               FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
               FA_LOAD_FLAGS(FA_DISABLE_AUTH | FA_COMPRESSION),
               NULL);
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
    htsmsg_release(doc);
    return -1;
  }

  stos_avail_version = parse_version_int(version);
  TRACE(TRACE_DEBUG, "STOS", "Available version: %s (%d)",
	version, stos_avail_version);


  htsmsg_release(stos_artifacts);
  stos_artifacts = NULL;

  htsmsg_field_t *f = htsmsg_field_find(doc, "artifacts");

  if(f == NULL) {
    htsmsg_release(doc);
    return -1;
  }
  stos_artifacts = htsmsg_detach_submsg(f);
  htsmsg_release(doc);

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
  htsmsg_release(stos_artifacts);
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

  b = fa_load(url,
               FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
               FA_LOAD_FLAGS(FA_DISABLE_AUTH | FA_COMPRESSION),
               NULL);

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
  stos_upgrade_needed = gconf.enable_omnigrade;
  htsmsg_t *manifest = htsmsg_get_map(json, "manifest");
  if(manifest != NULL) {

    const char *stosVersion = htsmsg_get_str(manifest, "stosVersion");
    if(stosVersion != NULL) {
      stos_req_version = parse_version_int(stosVersion);

      if(stos_current_version < stos_req_version) {
	stos_upgrade_needed = 1;
	TRACE(TRACE_DEBUG, "STOS", "Required version for upgrade: %s (%d)",
	      stosVersion, stos_req_version);

	TRACE(TRACE_DEBUG, "STOS",
	      "Need to perform STOS upgrade, checking what is available");
      }

      if(stos_upgrade_needed) {

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
  const char *name = NULL;
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
      name = htsmsg_get_str(a, "name");
      dlsize = htsmsg_get_u32_or_default(a, "size", 0);
      break;
    }
  }

  ver = htsmsg_get_str(json, "version");

  if(dlurl == NULL || dlsize == 0 || sha1 == NULL || ver == NULL) {
    prop_set_string(upgrade_error, "No URL or size present");
    goto err;
  }

  hex2bin(app_download_digest, sizeof(app_download_digest), sha1);

  mystrset(&app_download_url, dlurl);
  mystrset(&app_download_name, name);

  prop_set(upgrade_root, "track", PROP_SET_STRING, upgrade_track);
  prop_set(upgrade_root, "availableVersion", PROP_SET_STRING, ver);

  app_download_size = dlsize;

  int canUpgrade = gconf.enable_omnigrade;

  if(ver != NULL) {
    int current_ver = app_get_version_int();
    int available_ver = parse_version_int(ver);
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
  news_ref = NULL;

  if(set_news && canUpgrade) {
    rstr_t *r = _("%s version %s is available");
    rstr_t *s = _("Open download page");
    char buf[128];
    snprintf(buf, sizeof(buf), rstr_get(r), APPNAMEUSER, ver);
    news_ref = add_news(buf, buf, "showtime:upgrade", rstr_get(s));
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
  htsmsg_release(json);
  return 0;
 err:
  prop_set_string(upgrade_status, "checkError");
  htsmsg_release(json);
  return 0;
}



/**
 *
 */
static void
app_add_artifact(struct artifact_queue *aq)
{
  artifact_t *a = calloc(1, sizeof(artifact_t));
  a->a_name = strdup(app_download_name ?: APPNAME);
  a->a_task = rstr_alloc(APPNAMEUSER);
  a->a_url = strdup(app_download_url);
  a->a_size = app_download_size;
  memcpy(a->a_digest, app_download_digest, 20);

  a->a_final_path = strdup(gconf.upgrade_path ?: gconf.binary);

#if STOS
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s.tmp", a->a_final_path);
  a->a_temp_path = strdup(tmp);
#endif

  TAILQ_INSERT_TAIL(aq, a, a_link);
}


#if STOS
/**
 *
 */
static void
clean_dl_dir(void)
{
  char fullpath[PATH_MAX];
  const char *path = "/boot/dl";
  struct dirent **namelist;
  int n;

  n = scandir(path, &namelist, 0, alphasort);
  if(n < 0) {
    TRACE(TRACE_ERROR, "Unable to scan directory %s -- %s (%d)",
          path, strerror(errno), errno);
    return;
  }

  while(n--) {
    const char *f = namelist[n]->d_name;
    if(strcmp(f, ".") && strcmp(f, "..")) {
      snprintf(fullpath, sizeof(fullpath), "%s/%s", path, namelist[n]->d_name);
      if(unlink(fullpath)) {
        TRACE(TRACE_ERROR, "Unable to delete %s -- %s (%d)",
              fullpath, strerror(errno), errno);
      }
    }
    free(namelist[n]);
  }
  free(namelist);
}


/**
 *
 */
static void
stos_add_artifacts(struct artifact_queue *aq)
{
  struct utsname uts;
  uname(&uts);

  htsmsg_field_t *f;

  HTSMSG_FOREACH(f, stos_artifacts) {
    htsmsg_t *msg;
    if((msg = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    const char *type = htsmsg_get_str(msg, "type");
    if(type == NULL)
      continue;
    if(strcmp(type, "sqfs") && strcmp(type, "bin") && strcmp(type, "txt"))
      continue;

    const char *dlurl = htsmsg_get_str(msg, "url");
    const char *sha1  = htsmsg_get_str(msg, "sha1");
    int dlsize        = htsmsg_get_u32_or_default(msg, "size", 0);
    const char *name  = htsmsg_get_str(msg, "name");
    const char *selectors  = htsmsg_get_str(msg, "selectors");
    if(dlurl == NULL || sha1 == NULL || name == NULL)
      continue;


    /**
     * Construct local filename
     *
     * We need to convert from  foobar-1.2.3.img to foobar.img
     *
     */

    char *n = mystrdupa(name);
    char *postfix = strrchr(n, '.');
    if(postfix == NULL)
      continue;
    char *dash = strchr(n, '-');
    if(dash == NULL)
      continue;

    *dash = 0;

    char dlpath[256];
    char finalpath[256];

    snprintf(dlpath,    sizeof(dlpath),    "/boot/dl/%s%s", n, postfix);
    snprintf(finalpath, sizeof(finalpath), "/boot/%s%s",    n, postfix);

    artifact_t *a = calloc(1, sizeof(artifact_t));


    if(selectors != NULL) {
      char *s = strdup(selectors);
      char *s2 = s;
      while(s2) {
        const char *key = s2;
        char *value = strchr(key, '=');
        if(value == NULL)
          goto done;
        *value = 0;
        value++;
        char *n = strchr(value, ',');
        if(n != NULL) {
          *n = 0;
          s2 = n + 1;
        } else {
          s2 = NULL;
        }

        if(!strcmp(key, "machine")) {
          // Machine must match for this artifact to be used, otherwise delete
          if(strcmp(value, uts.machine)) {
            TRACE(TRACE_DEBUG, "Upgrade",
                  "%s [%s] skipped (not for this machine [%s])",
                  name, value, uts.machine);
            dlurl = NULL;
          }
        }
      }

    done:
      free(s);
    }

    a->a_task = _("System");
    a->a_name = strdup(name);
    a->a_url = dlurl ? strdup(dlurl) : NULL;
    a->a_temp_path = strdup(dlpath);
    a->a_final_path = strdup(finalpath);
    hex2bin(a->a_digest, sizeof(a->a_digest), sha1);
    a->a_size = dlsize;

    a->a_check_partial_update = !strcmp(type, "txt");
    TAILQ_INSERT_TAIL(aq, a, a_link);
  }
}

#endif


/**
 *
 */
static void
move_files_into_place(struct artifact_queue *aq)
{
  artifact_t *a;
#if STOS
  TRACE(TRACE_DEBUG, "Upgrade", "Syncing filesystems");
  sync();
  TRACE(TRACE_DEBUG, "Upgrade", "Syncing filesystems done");
#endif

  TRACE(TRACE_DEBUG, "Upgrade", "Moving files into place");

  TAILQ_FOREACH(a, aq, a_link) {

    if(a->a_temp_path == NULL || a->a_url == NULL)
      continue;

    TRACE(TRACE_DEBUG, "Upgrade", "Moving %s -> %s",
          a->a_temp_path, a->a_final_path);

    int r = rename(a->a_temp_path, a->a_final_path);
    if(r) {
      TRACE(TRACE_ERROR, "Upgrade", "Rename of %s -> %s failed -- %s (%d)",
            a->a_temp_path, a->a_final_path, strerror(errno), errno);
    }
  }

#if STOS
  TRACE(TRACE_DEBUG, "Upgrade", "Syncing filesystems");
  sync();
  TRACE(TRACE_DEBUG, "Upgrade", "Syncing filesystems done");
#endif
}


/**
 *
 */
static void
delete_unused_files(struct artifact_queue *aq)
{
  artifact_t *a;

  TAILQ_FOREACH(a, aq, a_link) {

    if(a->a_url != NULL)
      continue;


    if(unlink(a->a_final_path) == -1) {
      if(errno != ENOENT)
        TRACE(TRACE_INFO, "Upgrade", "Unable to delete %s -- %s",
              a->a_final_path, strerror(errno));
    } else {
      TRACE(TRACE_DEBUG, "Upgrade", "Deleted %s", a->a_final_path);
    }
  }
}


/**
 *
 */
static void
print_summary(const struct artifact_queue *aq)
{
  const artifact_t *a;
  TRACE(TRACE_DEBUG, "Upgrade", "Summary of what will be done");
  TAILQ_FOREACH(a, aq, a_link) {
    if(a->a_url == NULL)
      TRACE(TRACE_DEBUG, "Upgrade", "File %s will be deleted",
            a->a_final_path);
    else
      TRACE(TRACE_DEBUG, "Upgrade", "File %s will be downloaded from %s (%s)",
            a->a_final_path, a->a_url, a->a_name);
  }
}

/**
 *
 */
static void
install_locked(struct artifact_queue *aq)
{
  if(app_download_url == NULL)
    return;

#if STOS
  int rval;
  // First, remount /boot as readwrite
  if(mount("/dev/mmcblk0p1", "/boot", "vfat", MS_REMOUNT, NULL)) {
    install_error("Unable to remount /boot to read-write", NULL);
    usage_event("Upgrade error", 1, USAGE_SEG("reason", "Remount"));
    return;
  }

  rval = mkdir("/boot/dl", 0770);
  if(rval == -1 && errno != EEXIST) {
    install_error("Unable to create temp directory /boot/dl", NULL);
    usage_event("Upgrade error", 1, USAGE_SEG("reason", "mkdir"));
    return;
  }

  // Clean the temporary download directory
  clean_dl_dir();

  // Add STOS artifacts if we need to upgrade stos
  if(stos_upgrade_needed && stos_artifacts != NULL)
    stos_add_artifacts(aq);

#endif

  usage_event("Upgrade", 1,
              USAGE_SEG("arch", archname,
                        "track", upgrade_track));

  app_add_artifact(aq);

  print_summary(aq);

  delete_unused_files(aq);

  artifacts_compute_progressbar_scale(aq);

  artifact_t *a;

  prop_set_string(upgrade_status, "download");

  TAILQ_FOREACH(a, aq, a_link) {

#if CONFIG_BSPATCH
    // Try to download patch
    if(!download_file(a, 1))
      continue; // OK
    // Failed, try normal download
#endif

    if(download_file(a, 0))
      return;
  }

#ifdef __ANDROID__
  android_install_apk(gconf.upgrade_path);
  return;
#endif

  move_files_into_place(aq);

  TRACE(TRACE_INFO, "upgrade", "All done, restarting");
  sleep(1);

#if STOS
  if(stos_upgrade_needed)
    app_shutdown(APP_EXIT_REBOOT);
  else
#endif
    app_shutdown(APP_EXIT_RESTART);
  return;
}




static void *
install_thread(void *aux)
{
  struct artifact_queue aq;
  TAILQ_INIT(&aq);

  hts_mutex_lock(&upgrade_mutex);
  install_locked(&aq);
  artifacts_free(&aq);
  hts_mutex_unlock(&upgrade_mutex);
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
      const event_payload_t *ep = (const event_payload_t *)e;
      if(!strcmp(ep->payload, "checkUpdates"))
	check_upgrade(0);
      if(!strcmp(ep->payload, "install"))
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
    stos_current_version = parse_version_int(buf);
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

#ifdef __ANDROID__
  artifact_type = "apk";
  archname = "android";
#endif

#ifdef __APPLE__
  if(gconf.upgrade_path == NULL)
    return;
  artifact_type = "bin";
  archname = "osx";
#endif

  if(artifact_type == NULL || archname == NULL)
    return;

  upgrade_root     = prop_create(prop_get_global(), "upgrade");
  upgrade_status   = prop_create(upgrade_root, "status");
  upgrade_progress = prop_create(upgrade_root, "progress");
  upgrade_error    = prop_create(upgrade_root, "error");
  upgrade_task     = prop_create(upgrade_root, "task");

  // Set status to "upToDate" until we know better

  prop_set_string(upgrade_status, "upToDate");


  htsmsg_t *store;

  if((store = htsmsg_store_load("upgrade")) == NULL)
    store = htsmsg_create_map();

  settings_create_separator(gconf.settings_general,
			  _p("Software upgrade"));

  setting_create(SETTING_MULTIOPT, gconf.settings_general,
                 SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Upgrade to releases from")),
                 SETTING_HTSMSG("track-4-10", store, "upgrade"),
#ifndef __ANDROID__
                 SETTING_OPTION("stable",  _p("Stable")),
                 SETTING_OPTION("testing", _p("Testing")),
#endif
                 SETTING_OPTION_CSTR("master", "Bleeding Edge (Very unstable)"),
                 SETTING_CALLBACK(set_upgrade_track, NULL),
                 SETTING_MUTEX(&upgrade_mutex),
                 NULL);


  setting_create(SETTING_BOOL, gconf.settings_general, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Notify about upgrades")),
                 SETTING_VALUE(1),
                 SETTING_HTSMSG("check", store, "upgrade"),
                 SETTING_WRITE_BOOL(&notify_upgrades),
                 SETTING_MUTEX(&upgrade_mutex),
                 NULL);

  prop_t *p = prop_create_root(NULL);
  prop_link(_p("Check for updates now"),
	    prop_create(prop_create(p, "metadata"), "title"));
  prop_set_string(prop_create(p, "type"), "load");
  prop_set_string(prop_create(p, "url"), "showtime:upgrade");

  if(prop_set_parent(p, prop_create(gconf.settings_general, "nodes")))
     abort();

  inhibit_checks = 0;

  prop_subscribe(0,
                 PROP_TAG_MUTEX, &upgrade_mutex,
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
  hts_mutex_lock(&upgrade_mutex);
  int r = check_upgrade(notify_upgrades);
  hts_mutex_unlock(&upgrade_mutex);
  return r;
}


/**
 *
 */
static int
upgrade_canhandle(const char *url)
{
  return !strcmp(url, "showtime:upgrade");
}


/**
 *
 */
static int
upgrade_open_url(prop_t *page, const char *url, int sync)
{
  if(!strcmp(url, "showtime:upgrade")) {
    usage_page_open(sync, "Upgrade");
    backend_page_open(page, "page:upgrade", sync);
    upgrade_refresh();
    prop_set(page, "directClose", PROP_SET_INT, 1);
  } else {
    nav_open_error(page, "Invalid URI");
  }
  return 0;
}


/**
 *
 */
char *
upgrade_get_track(void)
{
  hts_mutex_lock(&upgrade_mutex);
  char *r = upgrade_track ? strdup(upgrade_track) : NULL;
  hts_mutex_unlock(&upgrade_mutex);
  return r;
}

/**
 *
 */
static backend_t be_upgrade = {
  .be_canhandle = upgrade_canhandle,
  .be_open = upgrade_open_url,
};

BE_REGISTER(upgrade);
