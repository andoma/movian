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
#include "prop/prop_concat.h"
#include "settings.h"
#include "glw.h"
#include "glw_settings.h"
#include "htsmsg/htsmsg_store.h"
#include "htsmsg/htsmsg_json.h"
#include "db/kvstore.h"
#include "fileaccess/fileaccess.h"
#include "misc/prng.h"

static prop_t *screensaver_items;
static HTS_MUTEX_DECL(screensaver_mutex);
static char *screensaver_user_images;


typedef struct screensaver_item {
  rstr_t *url;
  rstr_t *info;
  prop_t *root;

} screensaver_item_t;

typedef struct screensaver_scanner {

  int num_scanned;
  int max_output;

  prng_t rng;

  screensaver_item_t *output;

} screensaver_scanner_t;



/**
 *
 */
static void
screensaver_emit_items(screensaver_scanner_t *ss)
{
  assert(ss->num_scanned <= ss->max_output);
  for(int i = 0; i < ss->num_scanned; i++) {
    int r = prng_get(&ss->rng) % ss->num_scanned;
    screensaver_item_t tmp = ss->output[i];
    ss->output[i] = ss->output[r];
    ss->output[r] = tmp;
  }

  prop_destroy_childs(screensaver_items);

  for(int i = 0; i < ss->num_scanned; i++) {
    screensaver_item_t *si = ss->output + i;
    prop_t *item = prop_create_root(NULL);
    prop_set(item, "url", PROP_SET_RSTRING, si->url);
    prop_set(item, "info", PROP_SET_RSTRING, si->info);
    if(prop_set_parent(item, screensaver_items)) {
      prop_destroy(item);
    } else {
      si->root = item;
    }
  }
}


/**
 *
 */
static void
screensaver_add_item(rstr_t *url, rstr_t *info,
                     screensaver_scanner_t *ss)
{
  if(ss->num_scanned < ss->max_output) {
    ss->output[ss->num_scanned].url  = rstr_dup(url);
    ss->output[ss->num_scanned].info = rstr_dup(info);

    ss->num_scanned++;
    if(ss->num_scanned == ss->max_output)
      screensaver_emit_items(ss);
    return;
  }

  int j = prng_get(&ss->rng) % ss->num_scanned;
  if(j < ss->max_output) {
    rstr_set(&ss->output[j].url, url);
    rstr_set(&ss->output[j].info, info);
  }
  ss->num_scanned++;
}


/**
 *
 */
static void
bing_images(screensaver_scanner_t *ss)
{
  char url[1024];
  char errbuf[256];
  buf_t *b;
  int idx = 0;
  while(1) {

    snprintf(url, sizeof(url),
             "http://www.bing.com/HPImageArchive.aspx?format=js&idx=%d&n=8",
             idx);

    b = fa_load(url,
                FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                FA_LOAD_FLAGS(FA_DISABLE_AUTH | FA_COMPRESSION | FA_NO_COOKIES),
                NULL);
    idx += 8;
    if(b == NULL) {
      TRACE(TRACE_ERROR, "Screensaver", "Unable to load images -- %s",
            errbuf);
      return;
    }

    htsmsg_t *doc = htsmsg_json_deserialize(buf_cstr(b));
    buf_release(b);

    if(doc == NULL)
      break;
    int got_something = 0;
    htsmsg_t *list = htsmsg_get_list(doc, "images");
    if(list != NULL) {
      htsmsg_field_t *f;
      HTSMSG_FOREACH(f, list) {
        htsmsg_t *m = htsmsg_get_map_by_field(f);
        if(m == NULL)
          continue;

        const char *s = htsmsg_get_str(m, "url");
        if(s == NULL)
          continue;
        snprintf(url, sizeof(url), "%s%s", "http://www.bing.com", s);
        screensaver_add_item(rstr_alloc(url),
                             rstr_alloc(htsmsg_get_str(m, "copyright")),
                             ss);
        got_something = 1;
      }
    }
    htsmsg_release(doc);
    if(!got_something)
      break;
  }
}


static int
is_probably_image(const char *filename)
{
  const char *e = strrchr(filename, '.');
  if(e == NULL)
    return 0;
  e++;

  return
    !strcasecmp(e, "png") ||
    !strcasecmp(e, "bmp") ||
    !strcasecmp(e, "gif") ||
    !strcasecmp(e, "jpg") ||
    !strcasecmp(e, "jpeg");
}


static void
screensaver_load_path(screensaver_scanner_t *ss, const char *path)
{
  fa_dir_t *fd = fa_scandir(path, NULL, 0);
  if(fd == NULL)
    return;

  if(fd->fd_count > 0) {

    fa_dir_entry_t *fde = RB_FIRST(&fd->fd_entries);

    int skip = prng_get(&ss->rng) % fd->fd_count;
    for(int i = 0; i < skip; i++) {
      fde = RB_NEXT(fde, fde_link);
    }
    for(int i = 0; i < fd->fd_count; i++) {
      if(fde == NULL)
        fde = RB_FIRST(&fd->fd_entries);

      if(content_dirish(fde->fde_type)) {
        screensaver_load_path(ss, rstr_get(fde->fde_url));
      } else {

        if(is_probably_image(rstr_get(fde->fde_url))) {
          screensaver_add_item(fde->fde_url, NULL, ss);
        }
      }

      fde = RB_NEXT(fde, fde_link);
    }
  }
  fa_dir_free(fd);
}



/**
 *
 */
static void
screensaver_load_user_images(screensaver_scanner_t *ss)
{
  const char *path = NULL;

  hts_mutex_lock(&screensaver_mutex);
  if(screensaver_user_images != NULL && screensaver_user_images[0] != 0)
    path = mystrdupa(screensaver_user_images);

  hts_mutex_unlock(&screensaver_mutex);
  if(path == NULL)
    return;

  TRACE(TRACE_DEBUG, "GLW", "Scanning %s for screen saver images", path);

  screensaver_load_path(ss, path);
}


/**
 *
 */
static void
init_screensaver_items_load(void *opaque, prop_event_t event, ...)
{
  if(event != PROP_SUBSCRIPTION_MONITOR_ACTIVE)
    return;

  screensaver_scanner_t ss;
  prng_init2(&ss.rng);
  ss.num_scanned = 0;
  ss.max_output = 200;
  ss.output = calloc(ss.max_output, sizeof(screensaver_item_t));

  if(glw_settings.gs_bing_image)
    bing_images(&ss);

  screensaver_load_user_images(&ss);

  if(ss.num_scanned < ss.max_output) {
    screensaver_emit_items(&ss);
  } else {

    // Don't overwrite first item as it's already displaying
    for(int i = 1; i < ss.max_output; i++) {
      screensaver_item_t *si = ss.output + i;
      prop_set(si->root, "url", PROP_SET_RSTRING, si->url);
      prop_set(si->root, "info", PROP_SET_RSTRING, si->info);
    }
  }

  for(int i = 0; i < ss.max_output; i++) {
    screensaver_item_t *si = ss.output + i;
    rstr_release(si->url);
    rstr_release(si->info);
  }

  free(ss.output);
}

/**
 *
 */
static void
init_screensaver_items(void)
{
  screensaver_items = prop_create_multi(prop_get_global(),
                                        "glw", "screensaver", "items", NULL);

  prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR,
                 PROP_TAG_CALLBACK, init_screensaver_items_load, NULL,
                 PROP_TAG_ROOT, screensaver_items,
                 NULL);
}


/**
 *
 */
static void
set_screensaver_image_folder(void *opaque, const char *str)
{
  mystrset(&screensaver_user_images, str);
}



/**
 *
 */
static void
set_custom_bg(void *opaque, const char *str)
{
  prop_t *glw = prop_create(prop_get_global(), "glw");
  if(str != NULL && *str == 0)
    str = NULL;
  // Maybe stat image?
  prop_set(glw, "background", PROP_SET_STRING, str);
}



/**
 *
 */
void
glw_settings_adj_size(int delta)
{
  if(delta == 0)
    setting_set(glw_settings.gs_setting_size, SETTING_INT, 0);
  else
    settings_add_int(glw_settings.gs_setting_size, delta);
}


/**
 *
 */
void
glw_settings_init(void)
{
  glw_settings.gs_settings = prop_create_root(NULL);
  prop_concat_add_source(gconf.settings_look_and_feel,
			 prop_create(glw_settings.gs_settings, "nodes"),
			 NULL);

  prop_t *s = glw_settings.gs_settings;

  glw_settings.gs_setting_size =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Font and icon size")),
                   SETTING_RANGE(-10, 30),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_size),
                   SETTING_STORE("glw", "size"),
                   NULL);

  glw_settings.gs_setting_underscan_h =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Interface horizontal shrink")),
                   SETTING_RANGE(-100, 100),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_underscan_h),
                   SETTING_STORE("glw", "underscan_h"),
                   NULL);

  glw_settings.gs_setting_underscan_v =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Interface vertical shrink")),
                   SETTING_RANGE(-100, 100),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_underscan_v),
                   SETTING_STORE("glw", "underscan_v"),
                   NULL);

  glw_settings.gs_setting_wrap =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Wrap when reaching beginning/end of lists")),
                   SETTING_VALUE(1),
                   SETTING_WRITE_BOOL(&glw_settings.gs_wrap),
                   SETTING_STORE("glw", "wrap"),
                   NULL);

#ifdef __linux__
  glw_settings.gs_setting_wheel_mapping =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Emulate Up/Down buttons with mouse wheel")),
                   SETTING_STORE("glw", "map_mouse_wheel_to_keys"),
                   SETTING_WRITE_BOOL(&glw_settings.gs_map_mouse_wheel_to_keys),
                   NULL);
#endif

  settings_create_separator(s, _p("Background"));

  glw_settings.gs_setting_custom_bg =
    setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE | SETTINGS_FILE,
                   SETTING_TITLE(_p("Custom background image")),
                   SETTING_STORE("glw", "custom_bg"),
                   SETTING_CALLBACK(set_custom_bg, NULL),
                   NULL);

  settings_create_separator(s, _p("Screensaver"));

  glw_settings.gs_setting_screensaver_timer =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Screensaver delay")),
                   SETTING_VALUE(10),
                   SETTING_RANGE(0, 60),
		   SETTING_ZERO_TEXT(_p("Off")),
                   SETTING_UNIT_CSTR("min"),
                   SETTING_WRITE_INT(&glw_settings.gs_screensaver_delay),
                   SETTING_STORE("glw", "screensaver"),
                   NULL);

  glw_settings.gs_setting_bing_image =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Use Bing images of the day")),
                   SETTING_VALUE(1),
                   SETTING_WRITE_BOOL(&glw_settings.gs_bing_image),
                   SETTING_STORE("glw", "bingimageoftheday"),
                   NULL);

  glw_settings.gs_setting_user_images =
    setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE | SETTINGS_DIR,
                   SETTING_TITLE(_p("Folder for screensaver images")),
                   SETTING_CALLBACK(set_screensaver_image_folder, NULL),
                   SETTING_STORE("glw", "userscreensaverimages"),
                   SETTING_MUTEX(&screensaver_mutex),
                   NULL);

  prop_t *id = prop_create_multi(prop_get_global(), "glw", "screensaver",
                                 "imageDuration", NULL);

  glw_settings.gs_setting_per_image_timeout =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Seconds per image")),
                   SETTING_WRITE_PROP(id),
                   SETTING_VALUE(15),
                   SETTING_RANGE(5, 60),
                   SETTING_STORE("glw", "screensaverimageduration"),
                   NULL);
  prop_ref_dec(id);

  prop_t *p = prop_create(prop_get_global(), "glw");
  p = prop_create(p, "osk");
  kv_prop_bind_create(p, "showtime:glw:osk");

  init_screensaver_items();
}


/**
 *
 */
void
glw_settings_fini(void)
{
  setting_destroy(glw_settings.gs_setting_user_images);
  setting_destroy(glw_settings.gs_setting_screensaver_timer);
  setting_destroy(glw_settings.gs_setting_per_image_timeout);
  setting_destroy(glw_settings.gs_setting_bing_image);
  setting_destroy(glw_settings.gs_setting_underscan_v);
  setting_destroy(glw_settings.gs_setting_underscan_h);
  setting_destroy(glw_settings.gs_setting_size);
  setting_destroy(glw_settings.gs_setting_wrap);
#ifdef __linux__
  setting_destroy(glw_settings.gs_setting_wheel_mapping);
#endif
  setting_destroy(glw_settings.gs_setting_custom_bg);
  prop_destroy(glw_settings.gs_settings);
}
