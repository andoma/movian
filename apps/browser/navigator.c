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

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "layout/layout.h"
#include "event.h"

#include "browser.h"
#include "browser_view.h"
#include "navigator.h"
#include "video/video_playback.h"
#include "apps/playlist/playlist.h"
#include "apps/dvdplayer/dvd.h"

typedef struct navigator {
  appi_t *nav_ai;
  glw_t *nav_stack;
  glw_prop_t *nav_prop_root;
  glw_prop_t *nav_prop_icon;
  glw_prop_t *nav_prop_path;

  enum {
    NAV_RS_RUN,
    NAV_RS_RECONFIGURE,
    NAV_RS_STOP,
  } nav_runstatus;

} navigator_t;


/**
 *
 */
static void
browser_enter(appi_t *ai, navigator_t *nav, browser_node_t *bn, int selected)
{
  int64_t type;
  int r;

  hts_mutex_lock(&bn->bn_ftags_mutex);
  r = filetag_get_int(&bn->bn_ftags, FTAG_FILETYPE, &type);
  hts_mutex_unlock(&bn->bn_ftags_mutex);

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
    browser_slideshow(bn, nav->nav_stack, ai);
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
nav_store_instance(appi_t *ai, navigator_t *nav)
{
  htsmsg_t *m = appi_settings_create(ai);
  
  htsmsg_add_str(m, "rootpath", glw_prop_get_string(nav->nav_prop_path));
  htsmsg_add_str(m, "icon",     glw_prop_get_string(nav->nav_prop_icon));

  appi_settings_save(ai, m);
}



/**
 *  Return 0 if user wants to exit, otherwise -1
 */
static int
nav_access_error(navigator_t *nav, appi_t *ai, const char *dir, 
		 const char *errtxt)
{
  int r;
  glw_t *m;
  glw_prop_t *p;

  p = glw_prop_create(NULL, "error", GLW_GP_DIRECTORY);
  glw_prop_set_stringf(glw_prop_create(p, "details", GLW_GP_STRING),
		       "\"%s\"\n%s", dir, errtxt);

  m = glw_model_create("theme://browser/access-error.model", nav->nav_stack,
		       0, p, NULL);
  r = glw_wait_form_ok_cancel(m);
  glw_detach(m);
  glw_prop_destroy(p);
  return r;
}

typedef struct {
  glw_t *parent;
  const char *current;
} iconloadctx_t;

/**
 *
 */
static void
icons_callback(void *aux, const char *url, const char *filename, int type)
{
  iconloadctx_t *ctx = aux;
  glw_t *m;

  m = glw_model_create(url, ctx->parent, 0, NULL);
  if(!strcmp(url, ctx->current))
    ctx->parent->glw_selected = m;
}


/**
 * Locate the 'fs_icon_list' and fill it with icons the user may
 * choose to use for this navigator instance.
 */
static void
load_nav_icons_in_tab(glw_t *m, const char *name, const char *current)
{
  iconloadctx_t ctx;

  ctx.current = current;

  if((ctx.parent = glw_find_by_id(m, name, 0)) == NULL) 
    return;

  fileaccess_scandir("theme://browser/icons", icons_callback, &ctx);
}


/**
 * Navigator configuration
 */
static void
nav_config(navigator_t *nav, appi_t *ai)
{
  glw_t *m;
  char buf[128];
  glw_event_t *ge;
  glw_event_appmethod_t *gea;

  m = glw_model_create("theme://browser/config.model", nav->nav_stack, 0,
		       prop_global, 
		       nav->nav_prop_root, 
		       ai->ai_prop_root, 
		       NULL);

  load_nav_icons_in_tab(m, "icon_container",
			glw_prop_get_string(nav->nav_prop_icon));
  appi_speedbutton_mapper(m, "speedbutton", ai);

  ge = glw_wait_form(m);

  if(ge->ge_type == GEV_OK) {
    glw_prop_set_from_widget(m, "title",    ai->ai_prop_title);
    glw_prop_set_from_widget(m, "rootpath", nav->nav_prop_path);

    glw_get_model(m, "icon_container", buf, sizeof(buf));
    glw_prop_set_string(nav->nav_prop_icon, buf);

    nav_store_instance(ai, nav);

    nav->nav_runstatus = NAV_RS_RECONFIGURE;
  } else if(ge->ge_type == GEV_CANCEL) {
    nav->nav_runstatus = NAV_RS_RUN;

  } else if(ge->ge_type == GEV_APPMETHOD) {
    gea = (glw_event_appmethod_t *)ge;

    if(!strcmp(gea->method, "appQuit"))
      nav->nav_runstatus = NAV_RS_STOP;
  }

  glw_event_unref(ge);
  glw_detach(m);
}


/**
 *
 */
static int
nav_main(navigator_t *nav, appi_t *ai)
{
  browser_root_t *br;
  browser_node_t *bn;
  glw_event_t *ge;
  const char *rooturl;
  int r;

  glw_event_flushqueue(&ai->ai_geq);

  while(nav->nav_runstatus != NAV_RS_STOP) {

    /**
     * Create browser root
     */ 
    rooturl = glw_prop_get_string(nav->nav_prop_path);
    
    br = browser_root_create(rooturl);
    bn = br->br_root;

    browser_view_expand_node(bn, nav->nav_stack);
    
    if((r = browser_scandir(bn, 0)) != 0) {
      nav_access_error(nav, ai, rooturl, strerror(r));
      nav_config(nav, ai);
    } else {
      nav->nav_runstatus = NAV_RS_RUN;
    }

    while(nav->nav_runstatus == NAV_RS_RUN) {

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
	browser_node_unref(bn);
	break;


      case EVENT_KEY_MENU:
	nav_config(nav, ai);
	break;

      case GEV_BACKSPACE:
	bn = browser_view_get_current_node(nav->nav_stack);
	if(bn == NULL)
	  break;

	if(bn->bn_parent != NULL) {
	  browser_view_collapse_node(bn);
	} else {
	  /* At top level, return to main menu */
	  mainmenu_show(ai);
	}
	browser_node_unref(bn);
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
  }

  return 0;
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
  const char *s;

  if(browser_view_index()) {
    fprintf(stderr, "No browser views found, cannot start navigator\n");
    appi_destroy(ai);
    return NULL;
  }

  memset(nav, 0, sizeof(navigator_t));
  nav->nav_ai = ai;

  nav->nav_prop_root = glw_prop_create(NULL, "nav", GLW_GP_DIRECTORY);
  nav->nav_prop_icon = glw_prop_create(nav->nav_prop_root, 
				       "icon", GLW_GP_STRING);
  nav->nav_prop_path = glw_prop_create(nav->nav_prop_root,
				       "rootpath", GLW_GP_STRING);

  ai->ai_widget = glw_model_create("theme://browser/browser-app.model", NULL,
				   0,  
				   nav->nav_prop_root, 
				   ai->ai_prop_root,
				   prop_global, 
				   NULL);

  if((nav->nav_stack = glw_find_by_id(ai->ai_widget, "stack", 0)) == NULL) {
    fprintf(stderr, "No navigation 'stack' found\n");
    appi_destroy(ai);
    return NULL;
  }

  glw_set(ai->ai_widget,
	  GLW_ATTRIB_SIGNAL_HANDLER, glw_event_enqueuer, &ai->ai_geq, 1000,
	  NULL);

  /**
   *  Switcher miniature
   */

  ai->ai_miniature =
    glw_model_create("theme://browser/browser-miniature.model", NULL,  0, 
		     nav->nav_prop_root, 
		     ai->ai_prop_root,
		     prop_global, 
		     NULL);

  mainmenu_appi_add(ai, 1);
  


  /**
   * load configuration (if present)
   */
  if(ai->ai_settings != NULL) {
    if((s = htsmsg_get_str(ai->ai_settings, "rootpath")) != NULL)
      glw_prop_set_string(nav->nav_prop_path, s);

    if((s = htsmsg_get_str(ai->ai_settings, "icon")) != NULL)
      glw_prop_set_string(nav->nav_prop_icon, s);

  } else {

    glw_prop_set_string(nav->nav_prop_icon,
			"theme://browser/icons/1default.model");

    glw_prop_set_string(nav->nav_prop_path, "file:///");

    layout_appi_show(ai);
    nav_config(nav, ai);
  }

  nav_main(nav, ai);

  glw_destroy(ai->ai_miniature);
  glw_destroy(ai->ai_widget);

  appi_destroy(ai);
  glw_prop_destroy(nav->nav_prop_root);
  return NULL;
}






/**
 * Start a new navigator thread
 */
static void
nav_spawn(appi_t *ai)
{
  hts_thread_t tid;
  hts_thread_create_detached(&tid, nav_launch, ai);
}


app_t app_navigator = {
  .app_spawn = nav_spawn,
  .app_name = "Navigator",
  .app_model = "theme://browser/start-icon.model",
};
