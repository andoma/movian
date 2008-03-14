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

#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"

#include "browser.h"
#include "browser_view.h"
#include "navigator.h"
#include "play_file.h"


#define NAVIGATOR_FILESYSTEM 1

typedef struct navconfig {
  char nc_title[40];
  char nc_rootpath[256];
} navconfig_t;



typedef struct navigator {
  appi_t *nav_ai;
} navigator_t;




static int
navigator_root_widget(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  navigator_t *nav = opaque;
  appi_t *ai = nav->nav_ai;
  inputevent_t *ie;

  va_list ap;
  va_start(ap, sig);
  
  switch(sig) {
  default:
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&ai->ai_ic, ie);
    return 1;
  }
  va_end(ap);
  return 0;
}

/**
 *
 */
static void
browser_enter(appi_t *ai, browser_node_t *bn)
{
  int64_t type;
  int r;
  glw_t *w = NULL;

  pthread_mutex_lock(&bn->bn_ftags_mutex);
  r = filetag_get_int(&bn->bn_ftags, FTAG_FILETYPE, &type);
  pthread_mutex_unlock(&bn->bn_ftags_mutex);

  if(r)
    return; 

  switch(type) {
  case FILETYPE_AUDIO:
    play_file(bn->bn_url, ai, &ai->ai_ic, NULL);
    break;

  case FILETYPE_VIDEO:
    w = glw_create(GLW_CONTAINER,
		   GLW_ATTRIB_PARENT, ai->ai_widget,
		   NULL);
    play_file(bn->bn_url, ai, &ai->ai_ic, w);
    break;
  }

  if(w)
    glw_destroy(w);
}



/**
 *
 */
static int
nav_main(navigator_t *nav, appi_t *ai, int navtype, navconfig_t *cfg)
{
  browser_root_t *br;
  browser_node_t *bn;
  inputevent_t ie;
  char rooturl[400];

  switch(navtype) {
  case NAVIGATOR_FILESYSTEM:
    if(cfg->nc_rootpath[0] == 0) {
      cfg->nc_rootpath[0] = '/';
      cfg->nc_rootpath[1] = 0;
    }

    snprintf(rooturl, sizeof(rooturl), "file://%s", cfg->nc_rootpath);
    break;
  }


  br = browser_root_create(rooturl);
  bn = br->br_root;

  browser_view_expand_node(bn, ai->ai_widget, &ai->ai_gfs);
  browser_scandir(bn);

  while(1) {

    input_getevent(&ai->ai_ic, 1, &ie, NULL);
    
    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:

      switch(ie.u.key) {
      default:
	break;

      case INPUT_KEY_ENTER:
	bn = browser_view_get_current_selected_node(ai->ai_widget);
	printf("ENTER: bn = %p\n", bn);
	if(bn == NULL)
	  break;

	switch(bn->bn_type) {
	case FA_DIR:
	  browser_view_expand_node(bn, ai->ai_widget, &ai->ai_gfs);
	  browser_scandir(bn);
	  break;

	case FA_FILE:
	  browser_enter(ai, bn);
	  break;

	}
	browser_node_deref(bn);
	break;

      case INPUT_KEY_BACK:
	bn = browser_view_get_current_node(ai->ai_widget);
	if(bn == NULL)
	  break;

	if(bn->bn_parent != NULL) 
	  browser_view_collapse_node(bn, &ai->ai_gfs);
	browser_node_deref(bn);
	break;

      }
    }
  }

  return 0;
}



static int
nav_setup(navigator_t *nav, appi_t *ai)
{
  struct layout_form_entry_list lfelist;
  glw_t *m;
  int r;

  navconfig_t fs;

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, ai->ai_widget,
		 GLW_ATTRIB_FILENAME, "browser/setup",
		 NULL);

  LFE_ADD(&lfelist, "storage_type_list");


  /**
   * Local filesystem
   */
  memset(&fs, 0, sizeof(navconfig_t));
  layout_form_add_tab(m,
		      "storage_type_list",       "browser/setup-file-icon",
		      "storage_type_container",  "browser/setup-file-tab");

  strcpy(fs.nc_title, "Filesystem");
  strcpy(fs.nc_rootpath, "/");

  LFE_ADD_STR(&lfelist, "fs_title", fs.nc_title, sizeof(fs.nc_title));
  LFE_ADD_STR(&lfelist, "fs_path", fs.nc_rootpath, sizeof(fs.nc_rootpath));
  LFE_ADD_BTN(&lfelist, "fs_ok", NAVIGATOR_FILESYSTEM);


  /**
   * CIFS share 
   */ 
#if 0
  layout_form_add_tab(m,
		      "storage_type_list",      "browser/setup-smb-icon",
		      "storage_type_container", "browser/setup-smb-tab");

  LFE_ADD(&lfelist, "smb_title");
  LFE_ADD(&lfelist, "smb_hostname");
  LFE_ADD(&lfelist, "smb_path");
  LFE_ADD_STR(&lfelist, "smb_username", username, sizeof(username));
  LFE_ADD(&lfelist, "smb_password");
  LFE_ADD_BTN(&lfelist, "smb_connect", 2);
#endif

  r = layout_form_query(&lfelist, m, &ai->ai_gfs);
  glw_destroy(m);
  
  
  switch(r) {
  case NAVIGATOR_FILESYSTEM:
    r = nav_main(nav, ai, r, &fs);
    break;
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
  appi_t *ai = appi_create("navigator");
  FILE *settings = aux;

  memset(nav, 0, sizeof(navigator_t));
  nav->nav_ai = ai;

  ai->ai_widget =
    glw_create(GLW_CUBESTACK,
	       GLW_ATTRIB_SIGNAL_HANDLER, navigator_root_widget, nav, 1000,
	       NULL);

  ai->ai_widget_miniature = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "browser/switcher-icon",
	       NULL);

  layout_switcher_appi_add(ai);
  layout_world_appi_show(ai);

  if(settings == NULL) {
    /* From launcher, ask user for settings */
    nav_setup(nav, ai);
  } else {
    abort();
  }
  return NULL;
}






/**
 * Start a new navigator thread
 */
static void
nav_spawn(FILE *settings)
{
  pthread_t ptid;
  pthread_create(&ptid, NULL, nav_launch, settings);
}


app_t app_navigator = {
  .app_spawn = nav_spawn,
  .app_name = "Navigator",
  .app_model = "browser/start-icon",
};
