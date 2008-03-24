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
#include "input.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"
#include "layout/layout_support.h"

#include "browser.h"
#include "browser_view.h"
#include "navigator.h"
#include "play_video.h"
#include "apps/playlist/playlist.h"

#define NAVIGATOR_FILESYSTEM 1

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

    if(ie->type == INPUT_KEY && ie->u.key == INPUT_KEY_MENU)
      ai->ai_display_menu = !ai->ai_display_menu;

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
    play_video(bn->bn_url, ai, &ai->ai_ic, nav->nav_stack);
    break;

  case FILETYPE_IMAGE:
    browser_slideshow(bn, nav->nav_stack, &ai->ai_ic);
    break;
  }
}


/**
 * Store information about this navigator instance on disk
 */
static void
nav_store_instance(appi_t *ai, navconfig_t *cfg, int type)
{
  FILE *fp = appi_setings_create(ai);
  const char *typetxt;

  if(fp == NULL)
    return;
  
  switch(type) {
  case NAVIGATOR_FILESYSTEM:
    typetxt = "fs";
    break;
  default:
    abort();
  }

  fprintf(fp, "type = %s\n", typetxt);
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
  struct layout_form_entry_list lfelist;
  glw_t *m;
  int r;

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
  
  r = layout_form_query(&lfelist, m, &ai->ai_gfs);
  glw_detach(m);
  return r;
}


/**
 *  Return 0 if user wants to exit, otherwise -1
 */
static int
nav_verify_exit(navigator_t *nav, appi_t *ai)
{
  struct layout_form_entry_list lfelist;
  glw_t *m;
  int r;

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, nav->nav_stack,
		 GLW_ATTRIB_FILENAME, "browser/exit",
		 NULL);

  LFE_ADD_BTN(&lfelist, "ok",     0);
  LFE_ADD_BTN(&lfelist, "cancel", -1); /* form will return '-1' if user
					  cancels form, so we use same
					  for the cancel button */
  
  r = layout_form_query(&lfelist, m, &ai->ai_gfs);
  glw_detach(m);
  return r;
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
  glw_t *w;
  int run = 1, r;

  switch(navtype) {
  case NAVIGATOR_FILESYSTEM:
    if(cfg->nc_rootpath[0] == 0) {
      cfg->nc_rootpath[0] = '/';
      cfg->nc_rootpath[1] = 0;
    }

    snprintf(rooturl, sizeof(rooturl), "file://%s", cfg->nc_rootpath);
    break;
  }

  /**
   * Swap task switcher miniature widget to the one configured
   */ 
  glw_lock();
  glw_destroy(nav->nav_miniature);

  nav->nav_miniature =
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, cfg->nc_icon,
	       NULL);

  layout_switcher_appi_add(ai, nav->nav_miniature);
  glw_unlock();

  /**
   * Update title in task switcher miniature widget
   */
  if((w = glw_find_by_id(nav->nav_miniature, "title", 0)) != NULL)
    glw_set(w, GLW_ATTRIB_CAPTION, cfg->nc_title, NULL);

  /**
   * Create browser root
   */ 
  br = browser_root_create(rooturl, &ai->ai_gfs, nav->nav_splashcontainer);
  bn = br->br_root;

  browser_view_expand_node(bn, nav->nav_stack, &ai->ai_gfs);
  r = browser_scandir(bn, 0);

  if(r != 0 && nav_access_error(nav, ai, rooturl, strerror(r)))
    run = 0;

  nav_store_instance(ai, cfg, navtype);

  input_flush_queue(&ai->ai_ic);

  while(run) {

    input_getevent(&ai->ai_ic, 1, &ie, NULL);
    
    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:

      switch(ie.u.key) {
      default:
	break;

      case INPUT_KEY_SWITCH_VIEW:
	bn = browser_view_get_current_node(nav->nav_stack);
	if(bn == NULL)
	  break;

	browser_view_switch(bn, &ai->ai_gfs);
	break;

      case INPUT_KEY_ENTER:
	bn = browser_view_get_current_selected_node(nav->nav_stack);
	if(bn == NULL)
	  break;

	switch(bn->bn_type) {
	case FA_DIR:
	  browser_view_expand_node(bn, nav->nav_stack, &ai->ai_gfs);
	  browser_scandir(bn, 1);
	  break;

	case FA_FILE:
	  browser_enter(ai, nav, bn, 0);
	  break;
	}
	browser_node_deref(bn);
	break;


      case INPUT_KEY_SELECT:
	bn = browser_view_get_current_selected_node(nav->nav_stack);
	if(bn == NULL)
	  break;

	switch(bn->bn_type) {
	case FA_FILE:
	  browser_enter(ai, nav, bn, 1);
	  break;
	case FA_DIR:
	  playlist_build_from_dir(bn->bn_url);
	  break;
	}
	browser_node_deref(bn);
	break;

      case INPUT_KEY_BACK:
	bn = browser_view_get_current_node(nav->nav_stack);
	if(bn == NULL)
	  break;

	if(bn->bn_parent != NULL) {
	  browser_view_collapse_node(bn, &ai->ai_gfs);
	} else {
	  /* At top level, check before exit */
	  r = nav_verify_exit(nav, ai);
	  if(r == 0)
	    run = 0;
	}
	browser_node_deref(bn);
	break;

      }
    }
  }

  bn = br->br_root;
  browser_view_collapse_node(bn, &ai->ai_gfs);

  printf("Destroying browser\n");
  browser_root_destroy(br);
  return 0;
}


/**
 * Locate the 'fs_icon_list' and fill it with icons the user may
 * choose to use for this navigator instance.
 */
static void
load_nav_icons_in_tab(glw_t *tab, const char *name)
{
  glw_t *w;
  char path[300];
  char fullpath[400];
  struct dirent **namelist, *d;
  struct stat st;
  int n, i;
  char *r;

  if((w = glw_find_by_id(tab, name, 0)) == NULL) 
    return;

  snprintf(path, sizeof(path), "%s/browser/icons",
	   config_get_str("theme", "themes/default"));

  n = scandir(path, &namelist, NULL, alphasort);
  if(n < 0)
    return;

  for(i = 0; i < n; i++) {
    d = namelist[i];
    if(d->d_name[0] == '.')
      continue;

    snprintf(fullpath, sizeof(fullpath), "%s/%s", path, d->d_name);
    r = strrchr(fullpath, '.');
    if(r == NULL)
      continue;
    if(strcasecmp(r, ".model"))
      continue;
    if(stat(fullpath, &st))
      continue;

    if((st.st_mode & S_IFMT) != S_IFREG)
      continue;

    snprintf(fullpath, sizeof(fullpath), "browser/icons/%s",d->d_name);

    r = strrchr(fullpath, '.');
    if(r == NULL)
      continue;
    *r = 0;
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_ID, fullpath,
	       GLW_ATTRIB_FILENAME, fullpath,
	       NULL);
  }
}

/**
 *  Setup a navigator and ask for user configuration
 */
static int
nav_setup(navigator_t *nav, appi_t *ai)
{
  struct layout_form_entry_list lfelist;
  glw_t *m, *t;
  int r;

  navconfig_t fs;

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, nav->nav_stack,
		 GLW_ATTRIB_FILENAME, "browser/setup",
		 NULL);

  LFE_ADD(&lfelist, "storage_type_list");


  /**
   * Local filesystem
   */
  memset(&fs, 0, sizeof(navconfig_t));
  t = layout_form_add_tab(m,
			  "storage_type_list",       "browser/setup-file-icon",
			  "storage_type_container",  "browser/setup-file-tab");

  load_nav_icons_in_tab(t, "fs_icon_list");

  strcpy(fs.nc_title, "Filesystem");
  strcpy(fs.nc_rootpath, "/");
  strcpy(fs.nc_icon, "browser/icons/1default-icon");

  LFE_ADD_STR(&lfelist, "fs_title", fs.nc_title, sizeof(fs.nc_title), 0);
  LFE_ADD_LIST(&lfelist, "fs_icon_list", fs.nc_icon, sizeof(fs.nc_icon));
  LFE_ADD_STR(&lfelist, "fs_path", fs.nc_rootpath, sizeof(fs.nc_rootpath), 0);
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
  glw_detach(m);
  
  
  switch(r) {
  case NAVIGATOR_FILESYSTEM:
    r = nav_main(nav, ai, r, &fs);
    break;
  }

  return 0;
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
  int type;

  if((s = config_get_str_sub(l, "type", NULL)) == NULL)
    return -1;
  
  if(!strcmp(s, "fs")) {
    type = NAVIGATOR_FILESYSTEM;
  } else {
    return -1;
  }
  
  if((s = config_get_str_sub(l, "title", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.nc_title, s, sizeof(cfg.nc_title));

  if((s = config_get_str_sub(l, "rootpath", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.nc_rootpath, s, sizeof(cfg.nc_rootpath));

  if((s = config_get_str_sub(l, "icon", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.nc_icon, s, sizeof(cfg.nc_icon));
  
  return nav_main(nav, ai, type, &cfg);
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

  ai->ai_widget =
    glw_create(GLW_CONTAINER_Z,
	       GLW_ATTRIB_SIGNAL_HANDLER, navigator_root_widget, nav, 1000,
	       NULL);

  nav->nav_stack = 
    glw_create(GLW_CUBESTACK,
	       GLW_ATTRIB_PARENT, ai->ai_widget,
	       NULL);

  nav->nav_splashcontainer = 
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_SPEED, 0.2,
	       GLW_ATTRIB_PARENT, ai->ai_widget,
	       NULL);

  /**
   *  Switcher miniature
   */
  nav->nav_miniature =
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "browser/switcher-icon",
	       NULL);

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
  .app_model = "browser/start-icon",
};
