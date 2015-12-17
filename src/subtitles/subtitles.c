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
#include "main.h"
#include "prop/prop.h"
#include "arch/threads.h"
#include "fileaccess/fileaccess.h"
#include "misc/isolang.h"
#include "media/media.h"
#include "vobsub.h"
#include "backend/backend.h"

#include "subtitles.h"
#include "video/video_settings.h"
#include "backend/backend.h"
#include "prop/prop_concat.h"
#include "settings.h"
#include "htsmsg/htsmsg_store.h"
#include "misc/str.h"

TAILQ_HEAD(subtitle_provider_queue, subtitle_provider);

static hts_mutex_t subtitle_provider_mutex;
static struct subtitle_provider_queue subtitle_providers;
static int num_subtitle_providers;
static prop_t *subtitle_settings_group;
static char *central_path;

static subtitle_provider_t *sp_same_filename;
static subtitle_provider_t *sp_any_filename;
static subtitle_provider_t *sp_central_dir_same_filename;
static subtitle_provider_t *sp_central_dir_any_filename;
static subtitle_provider_t *sp_embedded;

static htsmsg_t *sp_cfg;


/**
 *
 */
static int
subtitle_score(const subtitle_provider_t *sp)
{
  return (1 + num_subtitle_providers - sp->sp_prio) * 1000;
}

/**
 *
 */
int
subtitles_embedded_score(void)
{
  if(!sp_embedded->sp_enabled)
    return -1;
  return subtitle_score(sp_embedded);
}


/**
 *
 */
int
subtitles_embedded_autosel(void)
{
  return sp_embedded->sp_autosel;
}


/**
 *
 */
static void
sp_set_enable(void *aux, int enabled)
{
  subtitle_provider_t *sp = aux;
  sp->sp_enabled = enabled;
  prop_setv(sp->sp_settings, "metadata", "enabled", NULL, PROP_SET_INT,
	    sp->sp_enabled);

  htsmsg_t *c = htsmsg_get_map(sp_cfg, sp->sp_id);
  htsmsg_delete_field(c, "enabled");
  htsmsg_add_u32(c, "enabled", sp->sp_enabled);
  htsmsg_store_save(sp_cfg, "subtitleproviders");
}


/**
 *
 */
static void
sp_set_autosel(void *aux, int autosel)
{
  subtitle_provider_t *sp = aux;
  sp->sp_autosel = autosel;

  htsmsg_t *c = htsmsg_get_map(sp_cfg, sp->sp_id);
  htsmsg_delete_field(c, "autosel");
  htsmsg_add_u32(c, "autosel", sp->sp_autosel);
  htsmsg_store_save(sp_cfg, "subtitleproviders");
}


/**
 *
 */
static int
sp_prio_cmp(const subtitle_provider_t *a, const subtitle_provider_t *b)
{
  return a->sp_prio - b->sp_prio;
}


/**
 *
 */
void
subtitle_provider_register(subtitle_provider_t *sp, const char *id,
                           prop_t *title, int default_prio,
                           const char *subtype, int default_enable,
                           int default_autosel)
{
  sp->sp_id = strdup(id);
  sp->sp_prio = default_prio ?: 1000000;

  sp->sp_enabled = default_enable;
  sp->sp_autosel = default_autosel;
  sp->sp_settings =settings_add_dir(subtitle_settings_group,
                                    title, subtype, NULL, NULL, NULL);

  prop_tag_set(sp->sp_settings, &subtitle_providers, sp);

  hts_mutex_lock(&subtitle_provider_mutex);

  htsmsg_t *c = htsmsg_get_map(sp_cfg, sp->sp_id);
  if(c != NULL) {
    sp->sp_enabled = htsmsg_get_u32_or_default(c, "enabled", sp->sp_enabled);
    sp->sp_autosel = htsmsg_get_u32_or_default(c, "autosel", sp->sp_autosel);
    sp->sp_prio    = htsmsg_get_u32_or_default(c, "prio",    sp->sp_prio);
  } else {
    htsmsg_add_msg(sp_cfg, sp->sp_id, htsmsg_create_map());
  }

  sp->sp_setting_enabled =
    setting_create(SETTING_BOOL, sp->sp_settings, 0,
                   SETTING_VALUE(sp->sp_enabled),
                   SETTING_TITLE(_p("Enabled")),
                   SETTING_CALLBACK(sp_set_enable, sp),
                   SETTING_MUTEX(&subtitle_provider_mutex),
                   NULL);

  sp->sp_setting_autosel =
    setting_create(SETTING_BOOL, sp->sp_settings, 0,
                   SETTING_VALUE(sp->sp_autosel),
                   SETTING_TITLE(_p("Automatically select from this source")),
                   SETTING_CALLBACK(sp_set_autosel, sp),
                   SETTING_MUTEX(&subtitle_provider_mutex),
                   NULL);

  num_subtitle_providers++;
  TAILQ_INSERT_SORTED(&subtitle_providers, sp, sp_link, sp_prio_cmp,
                      subtitle_provider_t);

  subtitle_provider_t *n = TAILQ_NEXT(sp, sp_link);
  prop_move(sp->sp_settings, n ? n->sp_settings : NULL);

  hts_mutex_unlock(&subtitle_provider_mutex);

  prop_setv(sp->sp_settings, "metadata", "enabled", NULL, PROP_SET_INT,
	    sp->sp_enabled);

}


/**
 *
 */
static subtitle_provider_t *
subtitle_provider_create(const char *id, prop_t *title, int default_prio,
                         const char *subtype, int default_enable,
                         int default_autosel)
{
  subtitle_provider_t *sp = calloc(1, sizeof(subtitle_provider_t));
  subtitle_provider_register(sp, id, title, default_prio, subtype,
                             default_enable, default_autosel);
  return sp;
}


/**
 *
 */
void
subtitle_provider_unregister(subtitle_provider_t *sp)
{
  hts_mutex_lock(&subtitle_provider_mutex);
  prop_tag_clear(sp->sp_settings, &subtitle_providers);
  TAILQ_REMOVE(&subtitle_providers, sp, sp_link);
  num_subtitle_providers--;
  setting_destroy(sp->sp_setting_enabled);
  setting_destroy(sp->sp_setting_autosel);
  hts_mutex_unlock(&subtitle_provider_mutex);
  free(sp->sp_id);
  prop_destroy(sp->sp_settings);
}


/**
 * video - filename of video
 * sub - URL to subtitle
 *
 * return 1 if the subtitle filename matches the video filename
 */
static int
fs_sub_match(const char *video, const char *sub)
{
  // Get last path component of sub to form a filename
  sub = strrchr(sub, '/');
  if(sub == NULL)
    return 0;
  sub++;

  int vl = strlen(video);
  int sl = strlen(sub);

  if(sl >= vl && sub[vl] == '.' && !strncasecmp(sub, video, vl))
    return 1;

  char *x = strrchr(sub, '.');
  if(x != NULL) {
    size_t off = x - sub;
    if(vl > off) {
      if((video[off] == '.' || video[off] == ' ') &&
	 !strncasecmp(sub, video, off))
	return 1;
    }
  }
  return 0;
}


/**
 *
 */
static void
check_subtitle_file(sub_scanner_t *ss,
                    const char * const sub_filename,
                    const char * const sub_url,
                    const char * const video_filename,
                    int base_score, int match_result, int autosel,
                    const char *lang)
{
  const char *postfix = strrchr(sub_filename, '.');
  const char *type = NULL;
  if(postfix == NULL)
    return;

  if(match_result == -1) {

    base_score += fs_sub_match(video_filename, sub_url);

  } else {
    if(fs_sub_match(video_filename, sub_url) != match_result)
      return;
  }

  if(!strcasecmp(postfix, ".srt")) {
    if(postfix - sub_filename > 4 && postfix[-4] == '.') {
      char b[4];
      memcpy(b, postfix - 3, 3);
      b[3] = 0;
      const isolang_t *il = isolang_find(b);
      if(il != NULL)
        lang = il->iso639_2;
    }

    type = "SRT";

  } else if(!strcasecmp(postfix, ".ass") || !strcasecmp(postfix, ".ssa")) {
    if(postfix - sub_filename > 4 && postfix[-4] == '.') {
      char b[4];
      memcpy(b, postfix - 3, 3);
      b[3] = 0;
      const isolang_t *il = isolang_find(b);
      if(il != NULL)
        lang = il->iso639_2;
    }

    type = "ASS / SSA";

  } else if(!strcasecmp(postfix, ".sub") ||
            !strcasecmp(postfix, ".txt") ||
            !strcasecmp(postfix, ".xml") ||
            !strcasecmp(postfix, ".mpl")) {

    type = subtitles_probe(sub_url);

    if(type == NULL) {
      TRACE(TRACE_DEBUG, "Subtitles", "%s is not a recognized subtitle format",
            sub_url);
      return;
    }
    TRACE(TRACE_DEBUG, "Subtitles", "%s probed as %s", sub_url, type);

  } else if(!strcasecmp(postfix, ".idx")) {

    hts_mutex_lock(&ss->ss_mutex);
    vobsub_probe(sub_url, sub_filename, base_score, ss->ss_proproot, NULL,
                 autosel);
    hts_mutex_unlock(&ss->ss_mutex);
    return;

  } else {
    return;
  }

  hts_mutex_lock(&ss->ss_mutex);
  mp_add_track(ss->ss_proproot, sub_filename, sub_url, type, NULL, lang, NULL,
               _p("External file"), base_score, autosel);
  hts_mutex_unlock(&ss->ss_mutex);
  return;
}



/**
 * url - directory to scan in
 * video - filename of video
 */
static void
fs_sub_scan_dir(sub_scanner_t *ss, const char *url, const char *video,
                int descend_all, unsigned int level,
                const subtitle_provider_t *sp1,
                const subtitle_provider_t *sp2,
                const char *lang)
{
  fa_dir_t *fd;
  fa_dir_entry_t *fde;
  char errbuf[256];

  if(level == 0)
    return;

  TRACE(TRACE_DEBUG, "Video", "Scanning for subs in %s for %s", url, video);

  if((fd = fa_scandir(url, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "Video", "Unable to scan %s for subtitles: %s",
	  url, errbuf);
    return;
  }

  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(ss->ss_stop)
      break;

    const char *filename = rstr_get(fde->fde_filename);

    if(fde->fde_type == CONTENT_DIR || fde->fde_type == CONTENT_SHARE) {

      if(descend_all || !strcasecmp(filename, "subs")) {
	fs_sub_scan_dir(ss, rstr_get(fde->fde_url), video, descend_all,
			level - 1, sp1, sp2, lang);

      } else if(!strncasecmp(filename, "subs-", 5)) {
        const isolang_t *il = isolang_find(filename + 5);
        if(il != NULL)
          lang = il->iso639_2;

        fs_sub_scan_dir(ss, rstr_get(fde->fde_url), video, descend_all,
			level - 1, sp1, sp2, lang);
      }
      continue;
    }

    const char *postfix = strrchr(rstr_get(fde->fde_url), '.');
    if(postfix != NULL && !strcasecmp(postfix, ".zip")) {
      char zipurl[1024];
      snprintf(zipurl, sizeof(zipurl), "zip://%s", rstr_get(fde->fde_url));
      fs_sub_scan_dir(ss, zipurl, video, descend_all, level - 1, sp1, sp2,
                      NULL);
      continue;
    }

    const char *url      = rstr_get(fde->fde_url);

    if(sp1->sp_enabled)
      check_subtitle_file(ss, filename, url, video, subtitle_score(sp1),
                          1, sp1->sp_autosel, lang);

    if(sp2->sp_enabled)
      check_subtitle_file(ss, filename, url, video, subtitle_score(sp2),
                          0, sp2->sp_autosel, lang);
  }
  fa_dir_free(fd);
}


/**
 *
 */
void
sub_scanner_release(sub_scanner_t *ss)
{
  if(atomic_dec(&ss->ss_refcount))
    return;

  rstr_release(ss->ss_title);
  rstr_release(ss->ss_imdbid);
  free(ss->ss_url);
  hts_mutex_destroy(&ss->ss_mutex);
  free(ss);
}


/**
 *
 */
void
sub_scanner_retain(sub_scanner_t *ss)
{
  atomic_inc(&ss->ss_refcount);
}



/**
 *
 */
static void *
sub_scanner_thread(void *aux)
{
  sub_scanner_t *ss = aux;
  subtitle_provider_t *sp, **v;

  hts_mutex_lock(&subtitle_provider_mutex);

  int cnt = 0;
  TAILQ_FOREACH(sp, &subtitle_providers, sp_link)
    sp->sp_prio = ++cnt;

  v = alloca(cnt * sizeof(void *));
  cnt = 0;
  TAILQ_FOREACH(sp, &subtitle_providers, sp_link) {
    if(!sp->sp_enabled)
      continue;
    v[cnt++] = sp;
    if(sp->sp_retain != NULL)
      sp->sp_retain(sp);
  }

  hts_mutex_unlock(&subtitle_provider_mutex);

  char *fname = mystrdupa(ss->ss_url);
  fname = strrchr(fname, '/') ?: fname;
  fname++;
  char *dot = strrchr(fname, '.');
  if(dot)
    *dot = 0;

  if(!(ss->ss_beflags & BACKEND_VIDEO_NO_FS_SCAN)) {
    char parent[URL_MAX];
    if(!fa_parent(parent, sizeof(parent), ss->ss_url))
      fs_sub_scan_dir(ss, parent, fname, 0, 2,
		      sp_same_filename, sp_any_filename, NULL);
  }

  hts_mutex_lock(&subtitle_provider_mutex);
  if(central_path != NULL && central_path[0]) {
    const char *path = mystrdupa(central_path);
    hts_mutex_unlock(&subtitle_provider_mutex);

    fs_sub_scan_dir(ss, path, fname, 1, 2,
                    sp_central_dir_same_filename,
                    sp_central_dir_any_filename, NULL);

  } else {
    hts_mutex_unlock(&subtitle_provider_mutex);
  }



  int i;
  for(i = 0; i < cnt; i++) {
    subtitle_provider_t *sp = v[i];
    if(sp->sp_query != NULL)
      sp->sp_query(sp, ss->ss_stop ? NULL : ss, subtitle_score(sp),
		   sp->sp_autosel);
  }

  sub_scanner_release(ss);
  return NULL;
}



/**
 *
 */
sub_scanner_t *
sub_scanner_create(const char *url, prop_t *proproot,
		   const video_args_t *va, int duration)
{
  int noscan = va->title == NULL && va->imdb == NULL && !va->hash_valid;

  TRACE(TRACE_DEBUG, "Subscanner",
        "%s subtitle scan for %s (imdbid:%s) "
        "year:%d season:%d episode:%d duration:%d opensubhash:%016llx",
        noscan ? "No" : "Starting",
        va->title ?: "<unknown>",
        va->imdb ?: "<unknown>",
        va->year,
        va->season,
        va->episode,
        duration,
        va->opensubhash);

  if(noscan)
    return NULL;

  sub_scanner_t *ss = calloc(1, sizeof(sub_scanner_t));
  hts_mutex_init(&ss->ss_mutex);
  atomic_set(&ss->ss_refcount, 2); // one for thread, one for caller
  ss->ss_url = url ? strdup(url) : NULL;
  ss->ss_beflags = va->flags;
  ss->ss_title = rstr_alloc(va->title);
  ss->ss_imdbid = rstr_alloc(va->imdb);
  ss->ss_proproot = prop_ref_inc(proproot);
  ss->ss_fsize = va->filesize;
  ss->ss_year   = va->year;
  ss->ss_season = va->season;
  ss->ss_episode = va->episode;
  ss->ss_duration = duration;

  ss->ss_hash_valid = va->hash_valid;
  ss->ss_opensub_hash = va->opensubhash;
  memcpy(ss->ss_subdbhash, va->subdbhash, 16);

  hts_thread_create_detached("subscanner", sub_scanner_thread, ss,
			     THREAD_PRIO_METADATA);
  return ss;
}

/**
 *
 */
void
sub_scanner_destroy(sub_scanner_t *ss)
{
  if(ss == NULL)
    return;
  ss->ss_stop = 1;
  hts_mutex_lock(&ss->ss_mutex);
  prop_ref_dec(ss->ss_proproot);
  ss->ss_proproot = NULL;
  hts_mutex_unlock(&ss->ss_mutex);
  sub_scanner_release(ss);
}
/**
 *
 */
static void
subtitle_provider_handle_move(subtitle_provider_t *sp,
                              subtitle_provider_t *before)
{
  TAILQ_REMOVE(&subtitle_providers, sp, sp_link);

  if(before) {
    TAILQ_INSERT_BEFORE(before, sp, sp_link);
  } else {
    TAILQ_INSERT_TAIL(&subtitle_providers, sp, sp_link);
  }

  int prio = 0;

  TAILQ_FOREACH(sp, &subtitle_providers, sp_link) {
    sp->sp_prio = ++prio;
    htsmsg_t *c = htsmsg_get_map(sp_cfg, sp->sp_id);
    htsmsg_delete_field(c, "prio");
    htsmsg_add_u32(c, "prio", sp->sp_prio);
  }
  htsmsg_store_save(sp_cfg, "subtitleproviders");
}

/**
 *
 */
static void
subtitle_settings_group_cb(void *opaque, prop_event_t event, ...)
{
  prop_t *p1, *p2;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_REQ_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    subtitle_provider_handle_move(prop_tag_get(p1, &subtitle_providers),
                                  p2 ? prop_tag_get(p2, &subtitle_providers)
                                  : NULL);
    prop_move(p1, p2);
    break;

  default:
    break;
  }
  va_end(ap);
}


struct subtitle_settings subtitle_settings;

static int
parse_bgr(const char *str)
{
  int bgr;
  if(*str == '#')
    str++;

  if(strlen(str) == 6) {

    bgr  = hexnibble(str[0]) << 4;
    bgr |= hexnibble(str[1]);
    bgr |= hexnibble(str[2]) << 12;
    bgr |= hexnibble(str[3]) << 8;
    bgr |= hexnibble(str[4]) << 20;
    bgr |= hexnibble(str[5]) << 16;
    return bgr;
  }

  return 0;
}


static void
set_subtitle_color(void *opaque, const char *str)
{
  subtitle_settings.color = parse_bgr(str);
}

static void
set_subtitle_shadow_color(void *opaque, const char *str)
{
  subtitle_settings.shadow_color = parse_bgr(str);
}

static void
set_subtitle_outline_color(void *opaque, const char *str)
{
  subtitle_settings.outline_color = parse_bgr(str);
}

static void
set_central_dir(void *opaque, const char *str)
{
  hts_mutex_lock(&subtitle_provider_mutex);
  mystrset(&central_path, str);
  hts_mutex_unlock(&subtitle_provider_mutex);
}



/**
 *
 */
static void
subtitles_init_settings(prop_concat_t *pc)
{
  prop_t *s = prop_create_root(NULL);
  prop_concat_add_source(pc, prop_create(s, "nodes"), NULL);

  //----------------------------------------------------------

  settings_create_separator(s, _p("Central subtitle folder"));

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE | SETTINGS_DIR,
                 SETTING_TITLE(_p("Path to central folder")),
                 SETTING_CALLBACK(set_central_dir, NULL),
                 SETTING_STORE("subtitles", "subtitlefolder"),
                 NULL);

  settings_create_separator(s, _p("Subtitle size and positioning"));

  subtitle_settings.scaling_setting =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Subtitle size")),
                   SETTING_VALUE(100),
                   SETTING_RANGE(30, 500),
                   SETTING_STEP(5),
                   SETTING_UNIT_CSTR("%"),
                   SETTING_STORE("subtitles", "scale"),
                   SETTING_VALUE_ORIGIN("global"),
                   NULL);

  subtitle_settings.align_on_video_setting =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Force subtitles to reside on video frame")),
                   SETTING_STORE("subtitles", "subonvideoframe"),
                   SETTING_VALUE_ORIGIN("global"),
                   NULL);

  setting_create(SETTING_MULTIOPT, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Subtitle position")),
                 SETTING_STORE("subtitles", "align"),
                 SETTING_WRITE_INT(&subtitle_settings.alignment),
                 SETTING_OPTION("2", _p("Center")),
                 SETTING_OPTION("1", _p("Left")),
                 SETTING_OPTION("3", _p("Right")),
                 SETTING_OPTION("0", _p("Auto")),
                 NULL);

  subtitle_settings.vertical_displacement_setting =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Subtitle vertical displacement")),
                   SETTING_RANGE(-300, 300),
                   SETTING_STEP(5),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_STORE("subtitles", "vdisplace"),
                   SETTING_VALUE_ORIGIN("global"),
                   NULL);

  subtitle_settings.horizontal_displacement_setting =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Subtitle horizontal displacement")),
                   SETTING_RANGE(-300, 300),
                   SETTING_STEP(5),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_STORE("subtitles", "hdisplace"),
                   SETTING_VALUE_ORIGIN("global"),
                   NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Color")),
                 SETTING_VALUE("FFFFFF"),
                 SETTING_STORE("subtitles", "color"),
                 SETTING_CALLBACK(set_subtitle_color, NULL),
                 NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Shadow color")),
                 SETTING_VALUE("000000"),
                 SETTING_STORE("subtitles", "shadowcolor"),
                 SETTING_CALLBACK(set_subtitle_shadow_color, NULL),
                 NULL);

  setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Shadow offset")),
                 SETTING_VALUE(2),
                 SETTING_RANGE(0, 10),
                 SETTING_STEP(1),
                 SETTING_UNIT_CSTR("px"),
                 SETTING_STORE("subtitles", "shadowcolorsize"),
                 SETTING_WRITE_INT(&subtitle_settings.shadow_displacement),
                 NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Outline color")),
                 SETTING_VALUE("000000"),
                 SETTING_STORE("subtitles", "outlinecolor"),
                 SETTING_CALLBACK(set_subtitle_outline_color, NULL),
                 NULL);

  setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Outline size")),
                 SETTING_VALUE(1),
                 SETTING_RANGE(0, 4),
                 SETTING_STEP(1),
                 SETTING_UNIT_CSTR("px"),
                 SETTING_STORE("subtitles", "shadowoutlinesize"),
                 SETTING_WRITE_INT(&subtitle_settings.outline_size),
                 NULL);

  setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Ignore embedded styling")),
                 SETTING_STORE("subtitles", "styleoverride"),
                 SETTING_WRITE_BOOL(&subtitle_settings.style_override),
                 NULL);
}



/**
 *
 */
static void
subtitles_init_providers(prop_concat_t *pc)
{
  sp_cfg = htsmsg_store_load("subtitleproviders") ?: htsmsg_create_map();

  prop_t *d = prop_create_root(NULL);

  prop_link(_p("Providers for Subtitles"),
            prop_create(prop_create(d, "metadata"), "title"));
  prop_set(d, "type", PROP_SET_STRING, "separator");

  prop_t *c = prop_create_root(NULL);
  subtitle_settings_group = c;

  prop_t *n = prop_create(c, "nodes");

  prop_concat_add_source(pc, n, d);

  prop_subscribe(0,
                 PROP_TAG_CALLBACK, subtitle_settings_group_cb, NULL,
                 PROP_TAG_MUTEX, &subtitle_provider_mutex,
                 PROP_TAG_ROOT, n,
                 NULL);

  //------------------------------------------------

  sp_embedded =
    subtitle_provider_create("showtime_embedded_subs",
                             _p("Subtitles embedded in video file"),
                             400000, "video", 1, 1);

  //------------------------------------------------

  sp_same_filename =
    subtitle_provider_create("showtime_same_filename",
                             _p("Subtitles with matching filename in same folder as video"),
                             500000, "subtitle", 1, 1);

  //------------------------------------------------

  sp_any_filename =
    subtitle_provider_create("showtime_any_filename",
                             _p("Any subtitle in same folder as video"),
                             501000, "subtitle", 0, 1);

  //------------------------------------------------

  sp_central_dir_same_filename =
    subtitle_provider_create("showtime_central_dir_same_filename",
                             _p("Subtitles with matching filename in central folder"),
                             502000, "subtitle", 1, 1);

  //------------------------------------------------

  sp_central_dir_any_filename =
    subtitle_provider_create("showtime_central_dir_any_filename",
                             _p("Any subtitle in central folder"),
                             503000, "subtitle", 0, 1);


  int prio = 0;
  subtitle_provider_t *sp;
  TAILQ_FOREACH(sp, &subtitle_providers, sp_link) {
    sp->sp_prio = ++prio;
  }

}

/**
 *
 */
void
subtitles_init(void)
{
  TAILQ_INIT(&subtitle_providers);
  hts_mutex_init(&subtitle_provider_mutex);

  prop_t *s = settings_add_dir(NULL, _p("Subtitles"), "subtitle", NULL,
                               _p("Generic settings for video subtitles"),
                               "settings:subtitles");

  prop_concat_t *pc = prop_concat_create(prop_create(s, "nodes"));

  subtitles_init_providers(pc);
  subtitles_init_settings(pc);
}
