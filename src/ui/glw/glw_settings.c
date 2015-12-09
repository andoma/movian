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

static prop_t *screensaver_items;
static HTS_MUTEX_DECL(screensaver_mutex);
static char *screensaver_user_images;

#if 0
/**
 *
 */
static void
populate_screensaver_items(void)
{
  char errbuf[256];
  buf_t *b;


  b = fa_load("http://screensaver.movian.tv/images.json",
               FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
               FA_LOAD_FLAGS(FA_DISABLE_AUTH | FA_COMPRESSION),
               NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "Screensaver", "Unable to load images -- %s",
          errbuf);
    return;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(buf_cstr(b));
  buf_release(b);

  if(doc == NULL) {
    TRACE(TRACE_ERROR, "STOS", "Malformed JSON");
    return;
  }


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

    prop_t *item = prop_create_root(NULL);
    prop_set(item, "url", PROP_SET_STRING, s);
    if(prop_set_parent(item, screensaver_items))
      prop_destroy(item);
    }
    screensaver_items_loaded++;
  }
  htsmsg_release(doc);
}
#endif

/**
 *
 */
static void
screensaver_add_item(const char *url, const char *info, int *cleared)
{
  prop_t *item = prop_create_root(NULL);
  prop_set(item, "url", PROP_SET_STRING, url);
  prop_set(item, "info", PROP_SET_STRING, info);

  if(*cleared == 0) {
    prop_destroy_childs(screensaver_items);
    *cleared = 1;
  }

  if(prop_set_parent(item, screensaver_items)) {
    prop_destroy(item);
  }
}


/**
 *
 */
static void
bing_images(int *cleared)
{
  char url[1024];
  char errbuf[256];
  buf_t *b;

  b = fa_load("http://www.bing.com/HPImageArchive.aspx?format=js&idx=22&n=2",
               FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
               FA_LOAD_FLAGS(FA_DISABLE_AUTH | FA_COMPRESSION | FA_NO_COOKIES),
               NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "Screensaver", "Unable to load images -- %s",
          errbuf);
    return;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(buf_cstr(b));
  buf_release(b);

  if(doc == NULL) {
    TRACE(TRACE_ERROR, "STOS", "Malformed JSON");
    return;
  }

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
      screensaver_add_item(url, htsmsg_get_str(m, "copyright"), cleared);
    }
  }

  htsmsg_release(doc);
}

/**
 *
 */
static void
screensaver_load_user_images(int *cleared)
{
  const char *path = NULL;

  hts_mutex_lock(&screensaver_mutex);
  if(screensaver_user_images != NULL && screensaver_user_images[0] != 0)
    path = mystrdupa(screensaver_user_images);

  hts_mutex_unlock(&screensaver_mutex);

  if(path == NULL)
    return;

  fa_dir_t *fd = fa_scandir(path, NULL, 0);
  if(fd == NULL)
    return;

  fa_dir_entry_t *fde;
  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    screensaver_add_item(rstr_get(fde->fde_url), NULL, cleared);
  }
  fa_dir_free(fd);
}


/**
 *
 */
static void
init_screensaver_items_load(void *opaque, prop_event_t event, ...)
{
  int cleared = 0;

  if(event == PROP_SUBSCRIPTION_MONITOR_ACTIVE) {
    screensaver_load_user_images(&cleared);

    if(glw_settings.gs_bing_image)
      bing_images(&cleared);
  }
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
  glw_settings.gs_settings_store = htsmsg_store_load("glw");

  if(glw_settings.gs_settings_store == NULL)
    glw_settings.gs_settings_store = htsmsg_create_map();


  glw_settings.gs_settings = prop_create_root(NULL);
  prop_concat_add_source(gconf.settings_look_and_feel,
			 prop_create(glw_settings.gs_settings, "nodes"),
			 NULL);

  prop_t *s = glw_settings.gs_settings;
  htsmsg_t *store = glw_settings.gs_settings_store;

  glw_settings.gs_setting_size =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Font and icon size")),
                   SETTING_RANGE(-10, 30),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_size),
                   SETTING_HTSMSG("size", store, "glw"),
                   NULL);

  glw_settings.gs_setting_underscan_h =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Interface horizontal shrink")),
                   SETTING_RANGE(-100, 100),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_underscan_h),
                   SETTING_HTSMSG("underscan_h", store, "glw"),
                   NULL);

  glw_settings.gs_setting_underscan_v =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Interface vertical shrink")),
                   SETTING_RANGE(-100, 100),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_underscan_v),
                   SETTING_HTSMSG("underscan_v", store, "glw"),
                   NULL);

  glw_settings.gs_setting_wrap =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Wrap when reaching beginning/end of lists")),
                   SETTING_VALUE(1),
                   SETTING_WRITE_BOOL(&glw_settings.gs_wrap),
                   SETTING_HTSMSG("wrap", store, "glw"),
                   NULL);

#ifdef __linux__
  glw_settings.gs_setting_wheel_mapping =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Emulate Up/Down buttons with mouse wheel")),
                   SETTING_HTSMSG("map_mouse_wheel_to_keys", store, "glw"),
                   SETTING_WRITE_BOOL(&glw_settings.gs_map_mouse_wheel_to_keys),
                   NULL);
#endif

  settings_create_separator(s, _p("Background"));

  glw_settings.gs_setting_custom_bg =
    setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE | SETTINGS_FILE,
                   SETTING_TITLE(_p("Custom background image")),
                   SETTING_HTSMSG("custom_bg", store, "glw"),
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
                   SETTING_HTSMSG("screensaver", store, "glw"),
                   NULL);

  glw_settings.gs_setting_bing_image =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Use Bing images of the day")),
                   SETTING_VALUE(1),
                   SETTING_WRITE_BOOL(&glw_settings.gs_bing_image),
                   SETTING_HTSMSG("bingimageoftheday", store, "glw"),
                   NULL);

  glw_settings.gs_setting_user_images =
    setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE | SETTINGS_DIR,
                   SETTING_TITLE(_p("Folder for screensaver images")),
                   SETTING_CALLBACK(set_screensaver_image_folder, NULL),
                   SETTING_HTSMSG("userscreensaverimages", store, "glw"),
                   SETTING_MUTEX(&screensaver_mutex),
                   NULL);

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
  htsmsg_release(glw_settings.gs_settings_store);
}
