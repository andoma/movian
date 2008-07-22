/*
 *  Generic browser
 *  Copyright (C) 2007 Andreas Öman
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

#define _GNU_SOURCE
#include <pthread.h>

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"
#include "layout/layout_support.h"
#include "event.h"

#include "browser.h"
#include "browser_view.h"
#include "navigator.h"
#include "video/video_playback.h"
#include "apps/playlist/playlist.h"
#include "apps/dvdplayer/dvd.h"


typedef struct navconfig {
  char nc_title[40];
  char nc_rootpath[256];
  char nc_icon[256];
} navconfig_t;



typedef struct navigator {
  appi_t *nav_ai;
  glw_t *nav_miniature;
  glw_t *nav_splashcontainer;
  glw_t *nav_stack;
} navigator_t;


/**
 *
 */
static void
browser_enter(appi_t *ai, navigator_t *nav, browser_node_t *bn, int selected)
{
  int64_t type;
  int r;

  pthread_mutex_lock(&bn->bn_ftags_mutex);
  r = filetag_get_int(&bn->bn_ftags, FTAG_FILETYPE, &type);
  pthread_mutex_unlock(&bn->bn_ftags_mutex);

  if(r)
    return; 

  switch(type) {
  case FILETYPE_AUDIO:
    playlist_enqueue(bn->bn_url, NULL, !selected);
    break;

  case FILETYPE_VIDEO:
    play_video(bn->bn_url, ai, &ai->ai_geq, nav->nav_stack);
    break;

  case FILETYPE_IMAGE:
    browser_slideshow(bn, nav->nav_stack, &ai->ai_geq);
    break;

  case FILETYPE_ISO:
    dvd_main(ai, bn->bn_url, 0, nav->nav_stack);
    break;
  }
}


/**
 * Store information about this navigator instance on disk
 */
static void
nav_store_instance(appi_t *ai, navconfig_t *cfg)
{
  FILE *fp = appi_setings_create(ai);

  if(fp == NULL)
    return;
  
  fprintf(fp, "title = %s\n", cfg->nc_title);
  fprintf(fp, "rootpath = %s\n", cfg->nc_rootpath);
  fprintf(fp, "icon = %s\n", cfg->nc_icon);
  fclose(fp);
}



/**
 *  Return 0 if user wants to exit, otherwise -1
 */
static int
nav_access_error(navigator_t *nav, appi_t *ai, const char *dir, 
		 const char *errtxt)
{
  abort();
#if 0
  struct layout_form_entry_list lfelist;
  glw_t *m;
  inputevent_t ie;
  char errbuf[400];

  snprintf(errbuf, sizeof(errbuf),
	   "Unable to access\n"
	   "%s\n"
	   "%s", dir, errtxt);

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, nav->nav_stack,
		 GLW_ATTRIB_FILENAME, "browser/access-error",
		 NULL);

  layout_update_multilinetext(m, "text", errbuf, 5, GLW_ALIGN_CENTER);

  LFE_ADD_BTN(&lfelist, "ignore", 0);
  LFE_ADD_BTN(&lfelist, "exit",   -1);
  
  layout_form_query(&lfelist, m, &ai->ai_gfs, &ie);
  glw_detach(m);
  return ie.u.u32;
#endif

}


/**
 *  Return 0 if user wants to exit, otherwise -1
 */
static int
nav_verify_exit(glw_t *parent, appi_t *ai)
{
 glw_t *m;
 int r;

  m = glw_model_create("theme://browser/exit.model", parent);
  r = glw_wait_form_ok_cancel(m);
  glw_detach(m);

  return r;
}

/**
 *
 */
static int
nav_main(navigator_t *nav, appi_t *ai, navconfig_t *cfg)
{
  browser_root_t *br;
  browser_node_t *bn;
  glw_event_t *ge;
  const char *rooturl = cfg->nc_rootpath;
  int run = 1, r;

  /**
   * Swap task switcher miniature widget to the one configured
   */ 
  glw_lock();
  glw_destroy(nav->nav_miniature);

  nav->nav_miniature = 
    glw_model_create("theme://browser/browser-miniature.model", NULL);

  layout_switcher_appi_add(ai, nav->nav_miniature);
  glw_unlock();

  /**
   * Update title in task switcher miniature widget
   */
  glw_set_caption(nav->nav_miniature, "title", cfg->nc_title);

  /**
   * Create browser root
   */ 
  br = browser_root_create(rooturl, nav->nav_splashcontainer);
  bn = br->br_root;

  browser_view_expand_node(bn, nav->nav_stack);
  r = browser_scandir(bn, 0);

  if(r != 0 && nav_access_error(nav, ai, rooturl, strerror(r)))
    run = 0;

  nav_store_instance(ai, cfg);

  glw_event_flushqueue(&ai->ai_geq);

  while(run) {

    ge = glw_event_get(-1, &ai->ai_geq);

    switch(ge->ge_type) {
    default:
      break;

    case GEV_ENTER:
    case EVENT_KEY_SELECT:
      bn = browser_view_get_current_selected_node(nav->nav_stack);
      if(bn == NULL)
	break;

      switch(bn->bn_type) {
      case FA_DIR:
	if(ge->ge_type == EVENT_KEY_SELECT) {
	  playlist_build_from_dir(bn->bn_url);
	} else {
	  browser_view_expand_node(bn, nav->nav_stack);
	  browser_scandir(bn, 1);
	}
	break;
	
      case FA_FILE:
	browser_enter(ai, nav, bn, ge->ge_type == EVENT_KEY_SELECT);
	break;
      }
      browser_node_deref(bn);
      break;



    case GEV_BACKSPACE:
    case EVENT_KEY_BACK:
      bn = browser_view_get_current_node(nav->nav_stack);
      if(bn == NULL)
	break;

      if(bn->bn_parent != NULL) {
	browser_view_collapse_node(bn);
      } else {
	/* At top level, check before exit */
	r = nav_verify_exit(nav->nav_stack, ai);
	if(r == 0)
	  run = 0;
      }
      browser_node_deref(bn);
      break;

     case EVENT_KEY_SWITCH_VIEW:
	bn = browser_view_get_current_node(nav->nav_stack);
	if(bn == NULL)
	  break;

	browser_view_switch(bn);
	break;
    }
    glw_event_unref(ge);
  }

  bn = br->br_root;
  browser_view_collapse_node(bn);

  browser_root_destroy(br);
  return 0;
}


/**
 *
 */
static void
icons_callback(void *arg, const char *url, const char *filename, int type)
{
  glw_model_create(url, arg);

}


/**
 * Locate the 'fs_icon_list' and fill it with icons the user may
 * choose to use for this navigator instance.
 */
static void
load_nav_icons_in_tab(glw_t *w, const char *name)
{
  if((w = glw_find_by_id(w, name, 0)) == NULL) 
    return;

  fileaccess_scandir("theme://browser/icons", icons_callback, w);
}

/**
 *  Setup a navigator and ask for user configuration
 */
static void
nav_setup(navigator_t *nav, appi_t *ai)
{
  glw_t *m;
  navconfig_t nc;
  int r;

  m = glw_model_create("theme://browser/setup.model", nav->nav_stack);
  load_nav_icons_in_tab(m, "icon_container");

  r = glw_wait_form_ok_cancel(m);

  glw_get_caption(m, "title", nc.nc_title, sizeof(nc.nc_title));
  glw_get_caption(m, "rootpath", nc.nc_rootpath, sizeof(nc.nc_rootpath));

  glw_detach(m);

  if(r == 0)
    nav_main(nav, ai, &nc);
}

/**
 *  Setup a navigator based on stored configuration
 */
static int
nav_autolaunch(navigator_t *nav, appi_t *ai)
{
  navconfig_t cfg;
  const char *s;
  struct config_head *l = ai->ai_settings;

  if((s = config_get_str_sub(l, "title", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.nc_title, s, sizeof(cfg.nc_title));

  if((s = config_get_str_sub(l, "rootpath", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.nc_rootpath, s, sizeof(cfg.nc_rootpath));

  if((s = config_get_str_sub(l, "icon", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.nc_icon, s, sizeof(cfg.nc_icon));
  
  return nav_main(nav, ai, &cfg);
}

/**
 * Launch a navigator
 *
 * If aux (settings) is non-NULL, we read settings from it, otherwise
 * ask user for settings
 */
static void *
nav_launch(void *aux)
{
  navigator_t *nav = alloca(sizeof(navigator_t));
  appi_t *ai = aux;

  if(browser_view_index()) {
    fprintf(stderr, "No browser views found, cannot start navigator\n");
    appi_destroy(ai);
    return NULL;
  }

  memset(nav, 0, sizeof(navigator_t));
  nav->nav_ai = ai;

  ai->ai_widget = glw_model_create("theme://browser/browser-app.model", NULL);

  if((nav->nav_stack = glw_find_by_id(ai->ai_widget, "stack", 0)) == NULL) {
    fprintf(stderr, "No navigation 'stack' found\n");
    appi_destroy(ai);
    return NULL;
  }

  glw_set(ai->ai_widget,
	  GLW_ATTRIB_SIGNAL_HANDLER, glw_event_enqueuer, &ai->ai_geq, 1000,
	  NULL);

#if 0
  nav->nav_splashcontainer = 
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_SPEED, 0.2,
	       GLW_ATTRIB_PARENT, ai->ai_widget,
	       NULL);
#endif

  /**
   *  Switcher miniature
   */
  nav->nav_miniature =
    glw_model_create("theme://browser/browser-miniature.model", NULL);

  layout_switcher_appi_add(ai, nav->nav_miniature);
  
  layout_world_appi_show(ai);

  if(ai->ai_settings == NULL) {
    /* From launcher, ask user for settings */
    nav_setup(nav, ai);
  } else {
    /* Autolaunched */
    nav_autolaunch(nav, ai);
  }

  glw_destroy(nav->nav_miniature);
  glw_destroy(ai->ai_widget);

  appi_destroy(ai);
  return NULL;
}






/**
 * Start a new navigator thread
 */
static void
nav_spawn(appi_t *ai)
{
  pthread_t ptid;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&ptid, &attr, nav_launch, ai);
}


app_t app_navigator = {
  .app_spawn = nav_spawn,
  .app_name = "Navigator",
  .app_model = "theme://browser/start-icon.model",
};
