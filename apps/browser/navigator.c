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

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "apps/dvdplayer/dvd.h"
#include "apps/playlist/playlist.h"
#include "play_file.h"
#include "browser.h"
#include "filebrowser.h"  // XXX: REMOVE ME
#include "navigator.h"

static navdir_t *
nav_find_dir_by_id(nav_t *n, int id)
{
  navdir_t *nd;
  unsigned int hash = id % NAV_HASH;
  LIST_FOREACH(nd, &n->n_alldirs[hash], nd_link)
    if(nd->nd_id == id)
      return nd;
  return NULL;
}



static naventry_t *
nav_find_entry_by_id(nav_t *n, int id)
{
  naventry_t *ne;
  unsigned int hash = id % NAV_HASH;
  LIST_FOREACH(ne, &n->n_allentries[hash], ne_link)
    if(ne->ne_id == id)
      return ne;
  return NULL;
}





static int 
naventry_cb(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}

static naventry_t *
nav_curentry(nav_t *n)
{
  navdir_t *nd = n->n_curdir;
  glw_t *w;

  if((w = nd->nd_widget->glw_selected) == NULL)
    return NULL;
  return glw_get_opaque(w, naventry_cb);
}



static navdir_t *
navdir_open(nav_t *n, const char *path, int makewidget)
{
  navdir_t *nd;
  unsigned int hash, id = ++n->n_dir_id_tally;

  void *be;

  be = n->n_interface->bi_open(n->n_interface, path, id, 0);
  if(be == NULL)
    return NULL;

  nd = calloc(1, sizeof(navdir_t));
  TAILQ_INIT(&nd->nd_childs);
  nd->nd_id = id;
  nd->nd_backend = be;
  hash = nd->nd_id % NAV_HASH;
  LIST_INSERT_HEAD(&n->n_alldirs[hash], nd, nd_link);

  if(makewidget)
    nd->nd_widget = glw_create(GLW_NAV,
			       GLW_ATTRIB_Y_SLICES, 15,
			       NULL);
  return nd;
}


static void
navdir_close(nav_t *n, navdir_t *nd)
{
  if(nd->nd_backend == n->n_curprobe)
    n->n_curprobe = NULL;

  n->n_interface->bi_close(nd->nd_backend);
  LIST_REMOVE(nd, nd_link);

  if(n->n_backdrop_dir == nd)
    n->n_backdrop_dir = NULL;

  free(nd);
}


#define STRIPDOT(dst, src) 			\
  strncpy(dst, src, sizeof(dst));		\
  if(dst[sizeof(dst) - 1])			\
    strcpy(&dst[sizeof(dst) - 4], "...");


static void
naventry_update_title(naventry_t *ne, browser_message_t *bm)
{
  const char *icon;
  char stripbuf[40];
  glw_t *x;
  char buf[300];

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, ne->ne_title_xfader,
		 NULL);

  switch(bm->bm_mi.mi_type) {
  case MI_IMAGE:
    snprintf(buf, sizeof(buf), "thumb://%s", bm->bm_url);
    icon = buf;
    break;
  case MI_DIR:
    icon = "icon://dir.png";
    break;
  case MI_AUDIO:
    icon = "icon://audio.png";
    break;
  case MI_VIDEO:
    icon = "icon://video.png";
    break;
  case MI_ISO:
  case MI_DIR_DVD:
    icon = "icon://cd.png";
    break;
  default:
    icon = "icon://file.png";
    break;
  }
  
  glw_create(GLW_BITMAP,
	     GLW_ATTRIB_WEIGHT, 0.05,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_FILENAME, icon,
	     NULL);

  STRIPDOT(stripbuf, bm->bm_mi.mi_title);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_ASPECT, 15.0f,
	     GLW_ATTRIB_CAPTION, stripbuf,
	     NULL);
}


static glw_t *
navdir_open_preload(nav_t *n, navdir_t *parent, browser_message_t *bm,
		    naventry_t *ne)
{
  navdir_t *nd;

  nd = navdir_open(n, bm->bm_url, 1);
  if(nd == NULL) {
    printf("Cannot open %s\n", bm->bm_url);
    return NULL;
  }

  nd->nd_parent = parent;
  ne->ne_dir = nd;

  return nd->nd_widget;
}


static glw_t *
naventry_get_file_preview(nav_t *n, navdir_t *parent, browser_message_t *bm,
			  naventry_t *ne)
{
  char stripbuf[40];
  glw_t *ret, *y, *padw;
  const char *icon;
  mediainfo_t *mi = &bm->bm_mi;
  int i;
  int pad = 7;

  ret = glw_create(GLW_CONTAINER_Y,
		   NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, ret,
	     GLW_ATTRIB_WEIGHT, 1.0f,
	     NULL);

  y = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, ret,
		 GLW_ATTRIB_FILENAME, "icon://plate-titled.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, ret,
	     GLW_ATTRIB_WEIGHT, 1.0f,
	     NULL);

  /* Container spanning title */

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, y,
		 NULL);


  STRIPDOT(stripbuf, mi->mi_title);
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, stripbuf,
	     NULL);


  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.2,
	     NULL);

  if(mi->mi_icon != NULL)
    icon = mi->mi_icon;
  else switch(mi->mi_type) {
  case MI_AUDIO:
    icon = "icon://audio.png";
    break;
  case MI_VIDEO:
    icon = "icon://video.png";
    break;
  case MI_ISO:
    icon = "icon://cd.png";
    break;
  default:
    icon = "icon://file.png";
    break;
  }

  y = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_WEIGHT, 6.0f,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_ALPHA, 0.25f,
		 GLW_ATTRIB_FILENAME, icon,
		 NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  if(bm->bm_mi.mi_author) {
    STRIPDOT(stripbuf, mi->mi_author);
    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	       GLW_ATTRIB_CAPTION, stripbuf,
	       NULL);
    pad--;
  }

  if(bm->bm_mi.mi_album) {
    STRIPDOT(stripbuf, mi->mi_album);
    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	       GLW_ATTRIB_CAPTION, stripbuf,
	       NULL);
    pad--;
  }

  padw = glw_create(GLW_DUMMY,
		    GLW_ATTRIB_WEIGHT, 1.0,
		    GLW_ATTRIB_PARENT, y,
		    NULL);

  if(bm->bm_mi.mi_duration) {
    snprintf(stripbuf, sizeof(stripbuf), "Duration: %d:%02d",
	     mi->mi_duration / 60, mi->mi_duration % 60);	     

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_CAPTION, stripbuf,
	       NULL);
    pad--;
  }

  for(i = 0 ; i < mi->mi_streaminfo_num; i++) {
    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_CAPTION, mi->mi_streaminfo[i],
	       NULL);
    pad--;
  }

  if(pad < 1)
    glw_destroy(padw);
  else
    glw_set(padw,
	    GLW_ATTRIB_WEIGHT, (float)pad,
	    NULL);
  
  return ret;
}


static void 
nav_add_entry(nav_t *n, navdir_t *nd, browser_message_t *bm, int makewidget)
{
  naventry_t *ne;
  glw_t *x, *content = NULL, *preview = NULL;
  unsigned int hash;

  ne = calloc(1, sizeof(naventry_t));
  mediaprobe_dup(&ne->ne_mi, &bm->bm_mi);

  ne->ne_id = bm->bm_entry_id;
  hash = ne->ne_id % NAV_HASH;
  LIST_INSERT_HEAD(&n->n_allentries[hash], ne, ne_link);
  TAILQ_INSERT_TAIL(&nd->nd_childs, ne, ne_parent_link);

  ne->ne_url = strdup(bm->bm_url);

  if(!makewidget)
    return;


  switch(bm->bm_mi.mi_type) {
  case MI_DIR:
    if(nd == n->n_curdir) {
      /* If it is a dir-type in our current directory, then
	 we open it at once */
      content = navdir_open_preload(n, nd, bm, ne);
    }
    break;

  case MI_AUDIO:
  case MI_VIDEO:
  case MI_ISO:
    preview = naventry_get_file_preview(n, nd, bm, ne);
    break;

  default:
    break;
  }

  ne->ne_naventry = 
    glw_create(GLW_NAV_ENTRY,
	       GLW_ATTRIB_SIGNAL_HANDLER, naventry_cb, ne, 0,
	       GLW_ATTRIB_CONTENT, content,
	       GLW_ATTRIB_PREVIEW, preview,
	       GLW_ATTRIB_PARENT, nd->nd_widget,
	       NULL);

  x = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, ne->ne_naventry,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  ne->ne_title_xfader =
    glw_create(GLW_XFADER,
		 GLW_ATTRIB_PARENT, x,
		 NULL);

  naventry_update_title(ne, bm);
}


static void 
nav_update_entry(nav_t *n, navdir_t *nd, naventry_t *ne, 
		 browser_message_t *bm)
{
  glw_t *preview, *content;

  mediaprobe_free(&ne->ne_mi);
  mediaprobe_dup(&ne->ne_mi, &bm->bm_mi);

  naventry_update_title(ne, bm);

  switch(bm->bm_mi.mi_type) {
  case MI_AUDIO:
  case MI_VIDEO:
  case MI_ISO:
    preview = naventry_get_file_preview(n, nd, bm, ne);
    glw_set(ne->ne_naventry,
	    GLW_ATTRIB_PREVIEW, preview,
	    NULL);
    break;

  case MI_IMAGE:
    content = glw_create(GLW_BITMAP,
			 GLW_ATTRIB_FILENAME, bm->bm_url,
			 NULL);

    glw_set(ne->ne_naventry,
	    GLW_ATTRIB_CONTENT, content,
	    GLW_ATTRIB_PREVIEW, NULL,
	    NULL);
    break;

  default:
    break;
  }
}


static void
nav_del_entry(nav_t *n, navdir_t *nd, naventry_t *ne)
{
  if(ne->ne_flush_content_time)
    TAILQ_REMOVE(&n->n_flushqueue, ne, ne_flush_link);

  LIST_REMOVE(ne, ne_link);
  TAILQ_REMOVE(&nd->nd_childs, ne, ne_parent_link);

  if(ne->ne_naventry != NULL)
    glw_destroy(ne->ne_naventry);

  free((void *)ne->ne_url);
  mediaprobe_free(&ne->ne_mi);
  free(ne);
}

/**
 * Update backdrop to the icon for the current directory
 */
static void
nav_update_backdrop(nav_t *n, navdir_t *nd)
{
  if(nd == n->n_backdrop_dir)
    return;

  n->n_backdrop_dir = nd;

  if(nd == NULL || nd->nd_icon == NULL) {
    glw_create(GLW_DUMMY,
	       GLW_ATTRIB_PARENT, n->n_backdrop_xfader,
	       NULL);

  } else {
    glw_create(GLW_BITMAP,
	       GLW_ATTRIB_PARENT, n->n_backdrop_xfader,
	       GLW_ATTRIB_FILENAME, nd->nd_icon,
	       NULL);
  }
}

/**
 * When the selected entry changes, see if it points to a directory
 * and if that dir has an icon associated with it, use it as backdrop
 */
static void
nav_update_backdrop_selected_changed(nav_t *n, glw_t *w)
{
  naventry_t *ne = nav_curentry(n);
  if(ne == NULL || !NE_IS_DIR(ne))
    return;
  nav_update_backdrop(n, ne->ne_dir);
}


/**
 * When recursive scan has completed for a given directory,
 * this function is called. We put all found files on the playlist, and
 * all directories found are further scanned.
 * After this is done, the directory is closed.
 */
static void
rscan_process_dir(nav_t *n, navdir_t *nd)
{
  naventry_t *ne;
  
  TAILQ_FOREACH(ne, &nd->nd_childs, ne_parent_link) {
    if(ne->ne_mi.mi_type == MI_DIR)
      navdir_open(n, ne->ne_url, 0);
  }

  /* This close() will also send BM_DELETE for all the entries we've
     just scanned */

  navdir_close(n, nd);
}


/**
 * Dispatcher for messages from browser backend
 *
 * This function deals with both messages from a normal browsed director
 * and for a recursive fast scan for playlist enqueue. It's a bit messy,
 * but it's still better than a lot of code dup.
 */
static void
navigator_input(nav_t *n, browser_message_t *bm)
{
  navdir_t *nd   = nav_find_dir_by_id(n,   bm->bm_parent_id);
  naventry_t *ne = nav_find_entry_by_id(n, bm->bm_entry_id);
  int rscan;  /* Set if we are doing a recursive scan */

#if 0
  printf("Got msg %d\n", bm->bm_action);
  printf("\tparent %d = %p\n", bm->bm_parent_id, nd);
  printf("\t entry %d = %p\n", bm->bm_entry_id, ne);
#endif

  if(nd != NULL) {

    rscan = nd->nd_widget ? 0 : 1;

    switch(bm->bm_action) {

      /* New file */
    case BM_ADD:
      assert(ne == NULL);
      nav_add_entry(n, nd, bm, !rscan);

      if(rscan) switch(bm->bm_mi.mi_type) {
      case MI_FILE:
      case MI_AUDIO:
	playlist_enqueue(bm->bm_url, &bm->bm_mi, 0);
	break;
      default:
	break;
      }
      break;

    /* Update a file entry (after probing, etc) */
  case BM_UPDATE:
    if(rscan)
      break;

    assert(ne != NULL);
    nav_update_entry(n, nd, ne, bm);
    break;

    /* Delete a file entry */
  case BM_DELETE:
    if(rscan)
      break;

    assert(ne != NULL);
    nav_del_entry(n, nd, ne);
    break;

    /* Set icon for folder (parent_id has folder id) */
  case BM_FOLDERICON:
    if(rscan)
      break;

    nd->nd_icon = strdup(bm->bm_mi.mi_icon);
    ne = nav_curentry(n);

    if(nd == n->n_curdir || nd == ne->ne_dir)
      nav_update_backdrop(n, nd);
    break;

  case BM_SCAN_COMPLETE:
    if(!rscan)
      break;
    rscan_process_dir(n, nd);
    break;
    }
  }
  browser_message_destroy(bm);
}



static void
nav_enter_dir(nav_t *n, navdir_t *nd)
{
  naventry_t *ne;
  n->n_curdir = nd;
  assert(nd->nd_parent != NULL);

  TAILQ_FOREACH(ne, &nd->nd_childs, ne_parent_link) {
    if(ne->ne_mi.mi_type != MI_DIR)
      continue;

    assert(ne->ne_dir == NULL);

    if(ne->ne_flush_content_time) {
      TAILQ_REMOVE(&n->n_flushqueue, ne, ne_flush_link);
      ne->ne_flush_content_time = 0;
    }

    ne->ne_dir = navdir_open(n, ne->ne_url, 1);
    ne->ne_dir->nd_parent = nd;
    glw_set(ne->ne_naventry,
	    GLW_ATTRIB_CONTENT, ne->ne_dir->nd_widget,
	    NULL);
  }
}



static void
nav_enter(appi_t *ai, nav_t *n, naventry_t *ne, int key)
{
  glw_t *content, *w = n->n_curdir->nd_widget;
  int sel = key == INPUT_KEY_SELECT;

  if(ne->ne_mi.mi_type == MI_DIR) {
    if(sel) {
      playlist_flush();
      printf("Scanning dir %s\n", ne->ne_url);
      navdir_open(n, ne->ne_url, 0);
    } else {
      glw_nav_signal(w, GLW_SIGNAL_ENTER);
      nav_enter_dir(n, ne->ne_dir);
      nav_update_backdrop(n, ne->ne_dir);
    }
    return;
  }

  if(key == INPUT_KEY_RIGHT)
    return;

  if(ne->ne_mi.mi_type == MI_IMAGE) {
    nav_slideshow(n, n->n_curdir, ne);
    return;
  }
    


  /*
   * Make sure our widget has a 'content' container for displaying stuff in
   * After glw_set() this is 'owned' by the NAVENTRY widget and will be
   * destryed if a new is set, or destroyed upon NAVENTRY destruction
   */

  content = glw_create(GLW_CONTAINER, NULL);
  glw_set(ne->ne_naventry,
	  GLW_ATTRIB_CONTENT, content,
	  NULL);

  switch(ne->ne_mi.mi_type) {
  default:
    break;

  case MI_AUDIO:
    glw_nav_signal(w, GLW_SIGNAL_CLICK);
    playlist_enqueue(ne->ne_url, &ne->ne_mi, !sel);
    break;

  case MI_VIDEO:
    glw_nav_signal(w, GLW_SIGNAL_ENTER);
    play_file(ne->ne_url, ai, &ai->ai_ic, &ne->ne_mi, NULL, content);
    break;

  case MI_DIR_DVD:
  case MI_ISO:
    glw_nav_signal(w, GLW_SIGNAL_ENTER);
    dvd_main(ai, ne->ne_url, 0, content);
    break;
  }
  w->glw_flags &= ~GLW_ZOOMED; /* XXX: fix locking */
}




static void
nav_leave_dir(nav_t *n, navdir_t *nd)
{
  naventry_t *ne;
  time_t now;
  time(&now);

  n->n_curdir = nd->nd_parent;

  TAILQ_FOREACH(ne, &nd->nd_childs, ne_parent_link) {
    if(ne->ne_mi.mi_type != MI_DIR)
      continue;

    assert(ne->ne_dir != NULL);

    if(ne->ne_flush_content_time)
      TAILQ_REMOVE(&n->n_flushqueue, ne, ne_flush_link);

    ne->ne_flush_content_time = now + 4;
    TAILQ_INSERT_TAIL(&n->n_flushqueue, ne, ne_flush_link);
    
    navdir_close(n, ne->ne_dir);
    ne->ne_dir = NULL;
  }
}

static void
probe_selected_dir(nav_t *n)
{
  glw_t *w;
  naventry_t *ne;

  w = n->n_curdir->nd_widget->glw_selected;
  if(w == NULL)
    return;

  ne = glw_get_opaque(w, naventry_cb);
  if(ne->ne_mi.mi_type != MI_DIR || ne->ne_dir == NULL)
    return;

  if(n->n_curprobe == ne->ne_dir->nd_backend)
    return;

  if(n->n_curprobe != NULL) {
    n->n_interface->bi_probe(n->n_curprobe, 0);
    n->n_curprobe = NULL;
  }
  
  n->n_curprobe = ne->ne_dir->nd_backend;
  n->n_interface->bi_probe(ne->ne_dir->nd_backend, 1);
}

/**
 * This function removes nav entry content and preview stuff after
 * the specified time has passed. This is needed because we dont
 * want to destroy it when we leave the entry, it has to stay for a
 * few seconds so it will fade off screen nicely
 */
static void
flush_nav_content(nav_t *n, time_t now)
{
  naventry_t *ne;

  while(1) {
    ne = TAILQ_FIRST(&n->n_flushqueue);
    if(ne == NULL || now < ne->ne_flush_content_time)
      return;

    ne->ne_flush_content_time = 0;
    TAILQ_REMOVE(&n->n_flushqueue, ne, ne_flush_link);
    glw_set(ne->ne_naventry,
	    GLW_ATTRIB_CONTENT, NULL,
	    GLW_ATTRIB_PREVIEW, NULL,
	    NULL);
  }
}


/**
 * Layout navigator main widget
 */
static void
navi_layout(nav_t *n, glw_rctx_t *rc)
{
  navdir_t *nd = n->n_curdir;

  if(n->n_slideshow_mode) {
    n->n_slideshow_blend = GLW_MIN(n->n_slideshow_blend + 0.02, 1.0f);
  } else {
    n->n_slideshow_blend = GLW_MAX(n->n_slideshow_blend - 0.02, 0.0f);
  }

  
  if(n->n_slideshow_blend < 0.99) {
    if(n->n_top_nav != NULL)
      glw_layout(n->n_top_nav, rc);

    if(n->n_backdrop_xfader != NULL)
      glw_layout(n->n_backdrop_xfader, rc);
  }
  
  if(nd->nd_slideshow != NULL) {
    if(n->n_slideshow_blend > 0.01) {
      glw_layout(nd->nd_slideshow, rc);
    } else {
      glw_destroy(nd->nd_slideshow);
      nd->nd_slideshow = NULL;
    }
  }
}

  

/**
 * render navigator main widget
 */
static void
navi_render(nav_t *n, glw_rctx_t *rc)
{
  glw_rctx_t rc0;
  float a = GLW_S(n->n_slideshow_blend);
  float b = 1. - a;
  navdir_t *nd = n->n_curdir;

  rc0 = *rc;

  if(n->n_backdrop_xfader != NULL) {
    rc0.rc_alpha = rc->rc_alpha * b * 0.2;
    if(rc0.rc_alpha > 0.01) {
      glPushMatrix();
      glTranslatef(0.0, 0.0, -0.2f);
      glw_render(n->n_backdrop_xfader, &rc0);
      glPopMatrix();
    }
  }

  if(n->n_top_nav != NULL) {
    rc0.rc_alpha = rc->rc_alpha * b;
    if(rc0.rc_alpha > 0.01)
      glw_render(n->n_top_nav, &rc0);
  }

  if(nd->nd_slideshow != NULL) {
    rc0.rc_alpha = rc->rc_alpha * a;
    if(rc0.rc_alpha > 0.01)
      glw_render(nd->nd_slideshow, &rc0);
  }
}


/**
 * Naviagor main widget
 *
 * We use this for painting the backdrop (album art, etc)
 * and also make smooth transitions between slideshow mode and main mode
 */
static int 
navi_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  int r = 0;
  inputevent_t *ie;
  nav_t *n = opaque;
  appi_t *ai = n->n_ai;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    navi_layout(n,  va_arg(ap, void *));
    break;

  case GLW_SIGNAL_RENDER:
    navi_render(n,  va_arg(ap, void *));
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&ai->ai_ic, ie);
    r = 1;
    break;

  default:
    break;
  }
  va_end(ap);
  return r;
}




static void
navigator(appi_t *ai, browser_interface_t *bi, const char *rootpath)
{
  
  inputevent_t ie;
  nav_t *n;
  navdir_t *cwd;
  naventry_t *ne;
  struct timespec ts, *tsp;

  n = calloc(1, sizeof(nav_t));
  n->n_ai = ai;

  ai->ai_widget = 
    glw_create(GLW_EXT,
	       GLW_ATTRIB_SIGNAL_HANDLER, navi_widget_callback, n, 0,
	       NULL);

  n->n_backdrop_xfader = 
    glw_create(GLW_FLIPPER,
	       NULL);

  bi->bi_mailbox = &ai->ai_ic;
  TAILQ_INIT(&n->n_flushqueue);

  n->n_interface = bi;
  n->n_curdir = navdir_open(n, rootpath, 1);
  n->n_top_nav = n->n_curdir->nd_widget;

  if(n->n_curdir == NULL)
    goto out;
  
  n->n_interface->bi_probe(n->n_curdir->nd_backend, 1);

  while(1) {
    probe_selected_dir(n);

    ts.tv_nsec = 0;
    ts.tv_sec = time(NULL) + 1;
    tsp = &ts;

    flush_nav_content(n, ts.tv_sec);

    if(input_getevent(&ai->ai_ic, 1, &ie, tsp))
      continue;

    cwd = n->n_curdir;

    switch(ie.type) {
    default:
      break;

    case INPUT_SPECIAL:
      navigator_input(n, ie.u.ptr);
      break;

    case INPUT_KEY:

      switch(ie.u.key) {

      case INPUT_KEY_NEXT:
      case INPUT_KEY_PREV:
      case INPUT_KEY_RESTART_TRACK:
      case INPUT_KEY_SEEK_FORWARD:
      case INPUT_KEY_SEEK_BACKWARD:
      case INPUT_KEY_STOP:
      case INPUT_KEY_PAUSE:
      case INPUT_KEY_PLAY:
      case INPUT_KEY_PLAYPAUSE:
      case INPUT_KEY_EJECT:
	playlist_eventstrike(&ie);
	break;

      case INPUT_KEY_UP:
	glw_nav_signal(cwd->nd_widget, GLW_SIGNAL_UP);
	nav_update_backdrop_selected_changed(n, cwd->nd_widget);
	break;

      case INPUT_KEY_DOWN:
	glw_nav_signal(cwd->nd_widget, GLW_SIGNAL_DOWN);
	nav_update_backdrop_selected_changed(n, cwd->nd_widget);
	break;

      case INPUT_KEY_BACK:
      case INPUT_KEY_LEFT:
	if(cwd->nd_parent != NULL) {
	  nav_leave_dir(n, cwd);
	  cwd = n->n_curdir;
	  cwd->nd_widget->glw_flags &= ~GLW_ZOOMED;
	  nav_update_backdrop_selected_changed(n, cwd->nd_widget);
	} else {
	  layout_hide(ai);
	}
	break;

      case INPUT_KEY_ENTER:
      case INPUT_KEY_RIGHT:
      case INPUT_KEY_SELECT:
	if((ne = nav_curentry(n)) != NULL)
	  nav_enter(ai, n, ne, ie.u.key);
	break;
      default:
	break;
      }
      break;
    }
  }

 out:
  glw_lock();
  glw_destroy(ai->ai_widget);
  ai->ai_widget = NULL;
  glw_destroy(n->n_top_nav);
  glw_destroy(n->n_backdrop_xfader);
  glw_unlock();

  bi->bi_destroy(bi);
}




static void *
browser_start(void *aux)
{
  browser_interface_t *bi;
  const char *path;
  char buf[100];
  appi_t *ai = aux;

  while(1) {
    path = config_get_str("mediaroot", "/");
    bi = filebrowser_create();
    navigator(aux, bi, path);

    snprintf(buf, sizeof(buf), "Path \"%s\" cannot be opened", path);

    ai->ai_widget = glw_create(GLW_TEXT_BITMAP,
			       GLW_ATTRIB_SIGNAL_HANDLER,
			       appi_widget_post_key, ai, 0,
			       GLW_ATTRIB_CAPTION, buf,
			       NULL);

    ai->ai_no_input_events = 1;
    sleep(3);
    ai->ai_no_input_events = 0;

    glw_lock();
    glw_destroy(ai->ai_widget);
    ai->ai_widget = NULL;
    glw_unlock();
  }

  return NULL;
}



void browser_spawn(void);


void
browser_spawn(void)
{
  appi_t *ai = appi_spawn("Browser", "icon://library.png");
  pthread_create(&ai->ai_tid, NULL, browser_start, ai);
}
