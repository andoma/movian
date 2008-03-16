/*
 *  Browser view
 *  Copyright (C) 2008 Andreas Öman
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

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "showtime.h"
#include "browser.h"
#include "browser_view.h"
#include "layout/layout_support.h"

struct browser_view_queue browser_views;

browser_view_t *defaultview;

static void browser_view_update_wset_from_node(glw_t *root,
					       browser_node_t *bn);

static int browser_view_node_callback(glw_t *w, void *opaque,
				      glw_signal_t sig, ...);

/**
 *
 */
static int
browser_view_xfader_callback(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  browser_node_t *bn = opaque;

  if(sig == GLW_SIGNAL_DESTROY) {
    bn->bn_cont_xfader = NULL;
    browser_node_deref(bn);
  }
  return 0;
}


/**
 *
 */
static int
browser_view_cont_callback(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  glw_t *c;
  browser_node_t *parent = opaque, *bn;

  va_list ap;
  va_start(ap, sig);

  if(sig == GLW_SIGNAL_SELECTED_CHANGED) {
    c = va_arg(ap, void *);

    /* Get the new node that was selected */

    bn = glw_get_opaque(c, browser_view_node_callback);
    if(bn != NULL && parent->bn_cont_xfader != NULL) {
      browser_view_update_wset_from_node(parent->bn_cont_xfader, bn);
    }
  }
  return 0;
}


/**
 * Return the node the represents the currently focused view (directory)
 *
 * It will incr the refcount on the node
 */
browser_node_t *
browser_view_get_current_node(glw_t *stack)
{
  glw_t *w;
  browser_node_t *r = NULL;

  glw_lock();

  w = stack->glw_selected;
  if(w != NULL) {
    r = glw_get_opaque(w, browser_view_xfader_callback);
    if(r != NULL) {
      browser_node_ref(r);
    }
  }

  glw_unlock();
  return r;
}


/**
 * Create a model (faded in via the node container XFADER)
 *
 * Return the new model
 */
glw_t *
browser_view_set(browser_node_t *bn, browser_view_t *bv, 
		 glw_focus_stack_t *gfs)
{
  char buf[256];
  glw_t *m, *w;

  bn->bn_view = bv;

  snprintf(buf, sizeof(buf), "browser/views/%s/view", bv->bv_name);

  m = glw_create(GLW_MODEL,
	     GLW_ATTRIB_FILENAME, buf,
	     GLW_ATTRIB_PARENT, bn->bn_cont_xfader,
	     NULL);

 
  if((w = glw_find_by_id(m, "node_container", 0)) != NULL) {
    glw_focus_set(gfs, w);
    glw_set(w,
	    GLW_ATTRIB_SIGNAL_HANDLER, browser_view_cont_callback, bn, 1000,
	    NULL);
  }

  layout_update_str(m, "node_fullpath", bn->bn_url);
  return m;
}



/**
 * Create the 'view' GLW_XFADER for a node.
 *
 * This 'view' is used to display the node's childs
 *
 * We take a reference when doing so, and free it once the widget
 * is destroyed, which will be done when the navigator view renderer
 * (that is, our parent widget) has moved this widget out of display.
 */
void
browser_view_expand_node(browser_node_t *bn, glw_t *parent, 
			 glw_focus_stack_t *gfs)
{
  browser_node_ref(bn);

  assert(bn->bn_cont_xfader == NULL);

  glw_lock();

  bn->bn_cont_xfader =
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_PARENT, parent,
	       GLW_ATTRIB_SIGNAL_HANDLER,
	       browser_view_xfader_callback, bn, 1000,
	       NULL);

  browser_view_set(bn, defaultview, gfs);
  glw_unlock();
}


/**
 *
 */
void
browser_view_collapse_node(browser_node_t *bn, glw_focus_stack_t *gfs)
{
  glw_t *w, *cont;

  glw_lock();

  w = bn->bn_cont_xfader;

  assert(bn->bn_cont_xfader != NULL);
  bn->bn_cont_xfader = NULL;

  glw_set(w, 
	  GLW_ATTRIB_SIGNAL_HANDLER, browser_view_xfader_callback, bn, -1,
	  NULL);

  if((cont = glw_find_by_id(w, "node_container", 0)) != NULL)
    glw_focus_lose(gfs, cont);

  glw_detach(w);

  glw_unlock();
  
  browser_node_deref(bn);
}


/**
 *
 */
static int
browser_view_node_callback(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  browser_node_t *bn = opaque;

  if(sig == GLW_SIGNAL_DESTROY) {
    bn->bn_icon_xfader = NULL;
    browser_node_deref(bn);
  }

  return 0;
}


/**
 * Return the node the represents the currently selected node in the 
 * currently focused view (directory)
 *
 * It will incr the refcount on the node
 */
browser_node_t *
browser_view_get_current_selected_node(glw_t *stack)
{
  glw_t *w;
  browser_node_t *r = NULL;

  glw_lock();

  w = stack->glw_selected;
  if(w != NULL) {
    w = glw_find_by_id(w, "node_container", 0);
    if(w != NULL) {
      w = w->glw_selected;
      if(w != NULL) {
	r = glw_get_opaque(w, browser_view_node_callback);
	if(r != NULL) {
	  browser_node_ref(r);
	}
      }
    }
  }

  glw_unlock();
  return r;
}

/**
 * In the 'root' widget set, find 'filetype_icon_container' and update
 * it with a new model based on the current filetype
 * 
 * GLW must be locked while doing this
 */
static void
browser_view_set_filetype(glw_t *root, browser_node_t *bn)
{
  browser_node_t *parent = bn->bn_parent;
  glw_t *w;
  const char *model;
  char buf[512];
  int64_t type;
  
  glw_lock();

  w = glw_find_by_id(root, "node_filetype_icon_container", 0);
  if(w == NULL)
    goto out;

  switch(bn->bn_type) {
  case FA_DIR:
    model = "directory";
    break;
  default:
  case FA_FILE:
    model = "file";

    if(!filetag_get_int(&bn->bn_ftags, FTAG_FILETYPE, &type)) {
      switch(type) {
      case FILETYPE_AUDIO:
	model = "audio";
	break;

      case FILETYPE_VIDEO:
	model = "video";
	break;

      case FILETYPE_IMAGE:
	snprintf(buf, sizeof(buf), "thumb://%s", bn->bn_url);

	glw_create(GLW_BITMAP,
		   GLW_ATTRIB_FILENAME, buf,
		   GLW_ATTRIB_FLAGS, GLW_KEEP_ASPECT,
		   GLW_ATTRIB_PARENT, w,
		   NULL);
	goto out;
      }
    }
    break;
  }

  snprintf(buf, sizeof(buf), "browser/views/%s/%s", 
	   parent->bn_view->bv_name, model);

  glw_create(GLW_MODEL,
	     GLW_ATTRIB_FILENAME, buf,
	     GLW_ATTRIB_PARENT, w,
	     NULL);
 out:
  glw_unlock();
}


/**
 * In the 'root' widget set, find 'id' and update its caption
 * 
 * GLW must be locked while doing this
 */
static void
browser_view_set_caption(glw_t *root, const char *id, const char *value)
{
  glw_t *w;

  glw_lock();

  if((w = glw_find_by_id(root, id, 0)) != NULL)
    glw_set(w, 
	    GLW_ATTRIB_CAPTION, value,
	    GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	    NULL);

  glw_unlock();
}


static void
size_quantify(char *buf, size_t buflen, int64_t i64)
{
  if(i64 < 1000) {
    snprintf(buf, buflen, "%lld b", i64);
  } else if(i64 < 1000 * 1000) {
    snprintf(buf, buflen, "%.2f kb", (float)i64 / 1000);
  } else if(i64 < 1000 * 1000 * 1000) {
    snprintf(buf, buflen, "%.2f Mb", (float)i64 / 1000 / 1000);
  } else {
    snprintf(buf, buflen, "%.2f Gb", (float)i64 / 1000 / 1000 / 1000);
  }
}

/**
 *
 */
static void
browser_view_update_wset_from_node(glw_t *root, browser_node_t *bn)
{
  const char *fname;
  char buf[100];
  int64_t i64;
  int32_t i32;
  const char *title;
  time_t t;
  struct tm tm;

  browser_view_set_filetype(root, bn);

  fname = strrchr(bn->bn_url, '/');
  fname = fname ? fname + 1 : bn->bn_url;
  browser_view_set_caption(root, "node_filename", fname);

  title = filetag_get_str2(&bn->bn_ftags, FTAG_TITLE);
  if(title == NULL)
    title = fname;

  browser_view_set_caption(root, "node_title", title);

  browser_view_set_caption(root, "node_author",
			   filetag_get_str2(&bn->bn_ftags, FTAG_AUTHOR));

  browser_view_set_caption(root, "node_album",
			   filetag_get_str2(&bn->bn_ftags, FTAG_ALBUM));

  browser_view_set_caption(root, "node_mediaformat",
			   filetag_get_str2(&bn->bn_ftags, FTAG_MEDIAFORMAT));

  browser_view_set_caption(root, "node_videoinfo",
			   filetag_get_str2(&bn->bn_ftags, FTAG_VIDEOINFO));

  browser_view_set_caption(root, "node_audioinfo",
			   filetag_get_str2(&bn->bn_ftags, FTAG_AUDIOINFO));

  if(!filetag_get_int(&bn->bn_ftags, FTAG_FILESIZE, &i64)) {
    size_quantify(buf, sizeof(buf), i64);
    browser_view_set_caption(root, "node_filesize", buf);
  } else {
    browser_view_set_caption(root, "node_filesize", NULL);
  }

  if(!filetag_get_int(&bn->bn_ftags, FTAG_DURATION, &i64)) {
    i32 = i64;
    if(i32 > 3600) {
      snprintf(buf, sizeof(buf), "%d:%02d:%02d",
	       i32 / 3600, (i32 % 3600) / 60, i32 % 60);
    } else {
      snprintf(buf, sizeof(buf), "%d:%02d", i32 / 60, i32 % 60);
    }
    browser_view_set_caption(root, "node_duration", buf);
  } else {
    browser_view_set_caption(root, "node_duration", NULL);
  }

  if(!filetag_get_int(&bn->bn_ftags, FTAG_ORIGINAL_DATE, &i64)) {
    t = i64;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%X %x", &tm);
    browser_view_set_caption(root, "node_original_date", buf);
  } else {
    browser_view_set_caption(root, "node_original_date", NULL);
  }

}


/**
 * bn_ftags_mutex should be held heere
 */
void
browser_view_node_model_update(browser_node_t *bn)
{
  glw_t *w;
  browser_root_t *br = bn->bn_root;
  browser_node_t *parent;

  pthread_mutex_lock(&br->br_hierarchy_mutex);

  parent = bn->bn_parent;
  parent->bn_refcnt++;

  pthread_mutex_unlock(&br->br_hierarchy_mutex);

  if(bn->bn_icon_xfader == NULL)
    return;

  glw_lock();

  w = bn->bn_icon_xfader;

  if(w != NULL) {

    if(w->glw_parent->glw_selected == w && parent->bn_cont_xfader != NULL)
      browser_view_update_wset_from_node(parent->bn_cont_xfader, bn);

    browser_view_update_wset_from_node(bn->bn_icon_xfader, bn);
  }
  glw_unlock();

  browser_node_deref(parent);

}


/**
 *
 */
static void
browser_view_node_model_load(browser_node_t *bn)
{
  browser_node_t *parent = bn->bn_parent;
  char buf[256];
  glw_t *w;

  pthread_mutex_lock(&bn->bn_ftags_mutex);

  snprintf(buf, sizeof(buf), "browser/views/%s/node",
	   parent->bn_view->bv_name);

  w = glw_create(GLW_MODEL,
		 GLW_ATTRIB_FILENAME, buf,
		 GLW_ATTRIB_PARENT, bn->bn_icon_xfader,
		 NULL);

  browser_view_update_wset_from_node(w, bn);

  pthread_mutex_unlock(&bn->bn_ftags_mutex);
}




/**
 * 
 */
void
browser_view_add_node(browser_node_t *bn, glw_t *c, int select_it)
{
  browser_node_t *parent = bn->bn_parent;

  c = c ? c : parent->bn_cont_xfader;
  if(c == NULL)
    return;
  
  if((c = glw_find_by_id(c, "node_container", 0)) == NULL)
    return;

  assert(bn->bn_refcnt > 0);

  browser_node_ref(bn);

  bn->bn_icon_xfader =
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_SIGNAL_HANDLER, browser_view_node_callback, bn, 1000,
	       GLW_ATTRIB_PARENT, c,
	       NULL);

  if(select_it)
    glw_select(c, bn->bn_icon_xfader);

  browser_view_node_model_load(bn);
}


/**
 * Builds index of all available views for this theme
 */
int
browser_view_index(void)
{
  char buf[256];
  char fullpath[256];
  struct dirent **namelist, *d;
  int n, i;
  browser_view_t *bv;

  if(defaultview)
    return 0; /* already indexed */

  TAILQ_INIT(&browser_views);

  snprintf(buf, sizeof(buf), "%s/browser/views", 
	   config_get_str("theme", "themes/default"));
  
  n = scandir(buf, &namelist, NULL, alphasort);
  if(n < 0)
    return -1;

  for(i = 0; i < n; i++) {
    d = namelist[i];
    if(d->d_name[0] == '.')
      continue;

    snprintf(fullpath, sizeof(fullpath), "%s/%s", buf, d->d_name);

    bv = calloc(1, sizeof(browser_view_t));

    TAILQ_INSERT_TAIL(&browser_views, bv, bv_link);
    bv->bv_path = strdup(fullpath);
    bv->bv_name = strdup(d->d_name);
    if(defaultview == NULL)
      defaultview = bv;

    if(!strcmp(d->d_name, "default"))
      defaultview = bv;
  }
  free(namelist);

  return defaultview == NULL ? -1 : 0;
}







/**
 * Switch current view
 */
void
browser_view_switch(browser_node_t *bn, glw_focus_stack_t *gfs)
{
  browser_view_t *bv = bn->bn_view;
  browser_node_t *c, *sel;
  glw_t *m, *w;

  bv = TAILQ_NEXT(bv, bv_link);
  if(bv == NULL)
    bv = TAILQ_FIRST(&browser_views);

  if(bv == bn->bn_view)
    return; /* Same view, probably only one view available, don't do
	       anything */

  glw_lock();

  sel = NULL;
  w = glw_find_by_id(bn->bn_cont_xfader, "node_container", 0);
  if(w != NULL) {
    w = w->glw_selected;
    if(w != NULL) {
      sel = glw_get_opaque(w, browser_view_node_callback);
    }
  }
  
  m = browser_view_set(bn, bv, gfs);

  TAILQ_FOREACH(c, &bn->bn_childs, bn_parent_link)
    browser_view_add_node(c, m, c == sel);

  glw_unlock();

}
