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

#include "htsp.h"

#define TV_TYPE_HTSP 1

typedef struct tvconfig {
  char tc_title[40];
  char tc_url[256];
} tvconfig_t;


/**
 * Display a fatal error for the user
 *
 * Fatal is things as: Unable to connect to server, and such things
 *
 * If 'err' is NULL we should remove any display (i.e, it's okay again)
 */
void 
tv_fatal_error(tv_t *tv, const char *err)
{
  char errbuf[400];

  if(err == tv->tv_last_err)
    return;

  tv->tv_last_err = err;

  if(tv->tv_fatal_error != NULL) {
    glw_detach(tv->tv_fatal_error);
    tv->tv_fatal_error = NULL;
  }

  if(err == NULL)
    return;

  snprintf(errbuf, sizeof(errbuf),
	   "%s\n\n"
	   "%s\n\n"
	   "%s",
	   tv->tv_cfg->tc_title,
	   tv->tv_cfg->tc_url,
	   err);

  tv->tv_fatal_error = glw_create(GLW_MODEL,
				  GLW_ATTRIB_PARENT, tv->tv_stack,
				  GLW_ATTRIB_FILENAME, "tv/fatal-error",
				  NULL);

  layout_update_multilinetext(tv->tv_fatal_error, 
			      "text", errbuf, 5, GLW_ALIGN_CENTER);
}


/**
 *
 */
static int
tv_root_widget(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  tv_t *tv = opaque;
  appi_t *ai = tv->tv_ai;
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
 * Store information about this TV instance on disk
 */
static void
tv_store_instance(appi_t *ai, tvconfig_t *cfg, int type)
{
  FILE *fp = appi_setings_create(ai);
  const char *typetxt;

  if(fp == NULL)
    return;
  
  switch(type) {
  case TV_TYPE_HTSP:
    typetxt = "htsp";
    break;
  default:
    abort();
  }

  fprintf(fp, "type = %s\n", typetxt);
  fprintf(fp, "title = %s\n", cfg->tc_title);
  fprintf(fp, "url = %s\n", cfg->tc_url);
  fclose(fp);
}


#if 0
/**
 *  Return 0 if user wants to exit, otherwise -1
 */
static int
nav_access_error(navigator_t *nav, appi_t *ai, const char *dir, 
		 const char *errtxt)
{
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
}


/**
 *  Return 0 if user wants to exit, otherwise -1
 */
static int
nav_verify_exit(navigator_t *nav, appi_t *ai)
{
  struct layout_form_entry_list lfelist;
  inputevent_t ie;
  glw_t *m;

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, nav->nav_stack,
		 GLW_ATTRIB_FILENAME, "browser/exit",
		 NULL);

  LFE_ADD_BTN(&lfelist, "ok",     0);
  LFE_ADD_BTN(&lfelist, "cancel", -1); /* form will return '-1' if user
					  cancels form, so we use same
					  for the cancel button */
  
  layout_form_query(&lfelist, m, &ai->ai_gfs, &ie);
  glw_detach(m);
  return ie.u.u32;
}
#endif



/**
 *
 */
tv_ch_group_t *
tv_channel_group_find(tv_t *tv, const char *name, int create)
{
  tv_ch_group_t *tcg;
  struct layout_form_entry_list lfelist;
  glw_t *w;

  TAILQ_FOREACH(tcg, &tv->tv_groups, tcg_link)
    if(!strcmp(tcg->tcg_name, name))
      return tcg;

  if(!create)
    return NULL;

  tcg = calloc(1, sizeof(tv_ch_group_t));
  TAILQ_INSERT_TAIL(&tv->tv_groups, tcg, tcg_link);
  tcg->tcg_name = strdup(name);
  TAILQ_INIT(&tcg->tcg_channels);

  /**
   * Create widget
   */
  tcg->tcg_widget = glw_create(GLW_MODEL,
			       GLW_ATTRIB_FILENAME, "tv/channel-group",
			       NULL);

  layout_update_str(tcg->tcg_widget, "title", name);

  w = layout_form_add_tab2(tv->tv_root, "groups", tcg->tcg_widget,
			   "channel_list_container", "tv/channellist");
  tcg->tcg_tab = w;

  tcg->tcg_channel_list = glw_find_by_id(w, "channel_list", 0);

  TAILQ_INIT(&lfelist);
  LFE_ADD_MONITOR_CHILD(&lfelist, "channel_list", 4);
  layout_form_initialize(&lfelist, w, &tv->tv_ai->ai_gfs,
			 &tv->tv_ai->ai_ic, 0);

  return tcg;
}

/**
 *
 */
tv_channel_t *
tv_channel_find(tv_t *tv, tv_ch_group_t *tcg, const char *name, int create)
{
  tv_channel_t *ch;

  TAILQ_FOREACH(ch, &tcg->tcg_channels, ch_link)
    if(!strcmp(ch->ch_name, name))
      return ch;

  if(!create)
    return NULL;

  ch = calloc(1, sizeof(tv_channel_t));
  TAILQ_INSERT_TAIL(&tcg->tcg_channels, ch, ch_link);
  ch->ch_name = strdup(name);

  /**
   * Create widget
   */
  ch->ch_widget =
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_PARENT, tcg->tcg_channel_list,
	       GLW_ATTRIB_FILENAME, "tv/channel",
	       NULL);

  layout_update_str(ch->ch_widget, "title", ch->ch_name);
  return ch;
}

/**
 *
 */
tv_channel_t *
tv_channel_find_by_tag(tv_t *tv, uint32_t tag)
{
  tv_ch_group_t *tcg;
  tv_channel_t *ch;

  TAILQ_FOREACH(tcg, &tv->tv_groups, tcg_link)
    TAILQ_FOREACH(ch, &tcg->tcg_channels, ch_link)
      if(ch->ch_tag == tag)
        return ch;
  return NULL;
}




/**
 *
 */
void
tv_channel_set_icon(tv_channel_t *ch, const char *icon)
{
  glw_t *w;

  if((w = glw_find_by_id(ch->ch_widget, "icon", 0)) == NULL)
    return;

  glw_set(w,
	  GLW_ATTRIB_FILENAME, icon,
	  NULL);
}


/**
 *
 */
void
tv_channel_set_current_event(tv_channel_t *ch, int index, 
			     const char *title, time_t start, time_t stop)
{
  char id[20];
  char buf[30];
  struct tm tm1, tm2;

  snprintf(id, sizeof(id), "title%d", index + 1);
  layout_update_str(ch->ch_widget, id, title);

  snprintf(id, sizeof(id), "time%d", index + 1);
  if(start && stop) {
    localtime_r(&start, &tm1);
    localtime_r(&stop,  &tm2);
    
    snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d",
	     tm1.tm_hour, tm1.tm_min, tm2.tm_hour, tm2.tm_min);

    layout_update_str(ch->ch_widget, id, buf);
  } else {
    layout_update_str(ch->ch_widget, id, NULL);
  }
}

/**
 *
 */
static void
tv_channel_destroy(tv_ch_group_t *tcg, tv_channel_t *ch)
{
  glw_destroy(ch->ch_widget);

  TAILQ_REMOVE(&tcg->tcg_channels, ch, ch_link);
  free(ch->ch_name);
  free(ch);
}

/**
 *
 */
static void
tv_channel_group_destroy(tv_t *tv, tv_ch_group_t *tcg)
{
  glw_destroy(tcg->tcg_widget);
  glw_destroy(tcg->tcg_tab);

  TAILQ_REMOVE(&tv->tv_groups, tcg, tcg_link);
  free(tcg->tcg_name);
  free(tcg);
}



/**
 *
 */
void
tv_remove_all(tv_t *tv)
{
  tv_ch_group_t *tcg;
  tv_channel_t *ch;

  while((tcg = TAILQ_FIRST(&tv->tv_groups)) != NULL) {

    while((ch = TAILQ_FIRST(&tcg->tcg_channels)) != NULL)
      tv_channel_destroy(tcg, ch);

    tv_channel_group_destroy(tv, tcg);
  }
}


/**
 *
 */
static int
tv_main(tv_t *tv, appi_t *ai, int type, tvconfig_t *cfg)
{
  htsp_connection_t *hc;
  glw_t *w;
  struct layout_form_entry_list lfelist;

  tv->tv_cfg = cfg;

  TAILQ_INIT(&tv->tv_groups);

  tv_store_instance(ai, cfg, type);

  /**
   * Update title in task switcher miniature widget
   */
  if((w = glw_find_by_id(tv->tv_miniature, "title", 0)) != NULL)
    glw_set(w, GLW_ATTRIB_CAPTION, cfg->tc_title, NULL);

  /**
   * Create group root
   */ 
  
  tv->tv_root = glw_create(GLW_MODEL,
			   GLW_ATTRIB_PARENT, tv->tv_stack,
			   GLW_ATTRIB_FILENAME, "tv/root",
			   NULL);


  /**
   * Setup widget navigation
   */
  TAILQ_INIT(&lfelist);
  LFE_ADD(&lfelist, "groups");
  //  LFE_ADD_BTN(&lfelist, "rename_playlist", PL_EVENT_RENAME_PLAYLIST);
  //  LFE_ADD_BTN(&lfelist, "delete_playlist", PL_EVENT_DELETE_PLAYLIST);
  layout_form_initialize(&lfelist, tv->tv_root, &ai->ai_gfs, &ai->ai_ic, 1);


  hc = htsp_create(cfg->tc_url, tv);



  while(1) {
    sleep(1);
  }

#if 0

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

      case INPUT_KEY_MENU:
	app_settings(ai, nav->nav_stack, cfg->nc_title, cfg->nc_icon);
	nav_store_instance(ai, cfg, navtype);
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
#endif
  return 0;
}



/**
 *  Setup a TV client and ask for user configuration
 */
static int
tv_setup(tv_t *tv, appi_t *ai)
{
  struct layout_form_entry_list lfelist;
  glw_t *m, *t;
  inputevent_t ie;

  tvconfig_t htsp_cfg;

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, tv->tv_stack,
		 GLW_ATTRIB_FILENAME, "tv/setup",
		 NULL);

  LFE_ADD(&lfelist, "tv_type_list");

  /**
   * HTSP client
   */
  memset(&htsp_cfg, 0, sizeof(tvconfig_t));
  t = layout_form_add_tab(m,
			  "tv_type_list",       "tv/setup-htsp-icon",
			  "tv_type_container",  "tv/setup-htsp-tab");

  strcpy(htsp_cfg.tc_title, "HTS Tvheadend");
  strcpy(htsp_cfg.tc_url, "htsp://127.0.0.1:9910");

  LFE_ADD_STR(&lfelist, "htsp_title", 
	      htsp_cfg.tc_title, sizeof(htsp_cfg.tc_title), 0);

  LFE_ADD_STR(&lfelist, "htsp_url", 
	      htsp_cfg.tc_url, sizeof(htsp_cfg.tc_url), 0);

  LFE_ADD_KEYDESC(&lfelist, "fs_speedbutton", 
	      ai->ai_speedbutton, sizeof(ai->ai_speedbutton));
  LFE_ADD_BTN(&lfelist, "fs_ok", TV_TYPE_HTSP);


  layout_form_query(&lfelist, m, &ai->ai_gfs, &ie);
  glw_detach(m);

  switch(ie.u.u32) {
  case TV_TYPE_HTSP:
    tv_main(tv, ai, TV_TYPE_HTSP, &htsp_cfg);
    break;
  }

  return 0;
}

/**
 *  Setup a TV based on stored configuration
 */
static int
tv_autolaunch(tv_t *nav, appi_t *ai)
{
  tvconfig_t cfg;
  const char *s;
  struct config_head *l = ai->ai_settings;
  int type;

  if((s = config_get_str_sub(l, "type", NULL)) == NULL)
    return -1;
  
  if(!strcmp(s, "htsp")) {
    type = TV_TYPE_HTSP;
  } else {
    return -1;
  }
  
  if((s = config_get_str_sub(l, "title", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.tc_title, s, sizeof(cfg.tc_title));

  if((s = config_get_str_sub(l, "url", NULL)) == NULL)
    return -1;
  av_strlcpy(cfg.tc_url, s, sizeof(cfg.tc_url));
  
  return tv_main(nav, ai, type, &cfg);
}

/**
 * Launch a TV client
 *
 * If aux (settings) is non-NULL, we read settings from it, otherwise
 * ask user for settings
 */
static void *
tv_launch(void *aux)
{
  tv_t *tv = alloca(sizeof(tv_t));
  appi_t *ai = aux;

  memset(tv, 0, sizeof(tv_t));
  tv->tv_ai = ai;

  ai->ai_widget =
    glw_create(GLW_CONTAINER_Z,
	       GLW_ATTRIB_SIGNAL_HANDLER, tv_root_widget, tv, 1000,
	       NULL);

  tv->tv_stack = 
    glw_create(GLW_ZSTACK,
	       GLW_ATTRIB_PARENT, ai->ai_widget,
	       NULL);

  /**
   *  Switcher miniature
   */
  tv->tv_miniature =
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "tv/switcher-icon",
	       NULL);

  layout_switcher_appi_add(ai, tv->tv_miniature);
  
  layout_world_appi_show(ai);

  if(ai->ai_settings == NULL) {
    /* From launcher, ask user for settings */
    tv_setup(tv, ai);
  } else {
    /* Autolaunched */
    tv_autolaunch(tv, ai);
  }

  glw_destroy(tv->tv_miniature);
  glw_destroy(ai->ai_widget);

  appi_destroy(ai);
  return NULL;
}






/**
 * Start a new TV application
 */
static void
tv_spawn(appi_t *ai)
{
  pthread_t ptid;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&ptid, &attr, tv_launch, ai);
}


app_t app_tv = {
  .app_spawn = tv_spawn,
  .app_name = "TV",
  .app_model = "tv/start-icon",
};
