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

#include "showtime.h"
#include "browser.h"
#include "browser_view.h"


/**
 *
 */
static int
browser_view_cont_callback(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  browser_node_t *bn = opaque;

  if(sig == GLW_SIGNAL_DESTROY) {
    printf("Widget for container %s destroyed\n", bn->bn_url);
    bn->bn_cont_xfader = NULL;
    browser_node_deref(bn);
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
    r = glw_get_opaque(w, browser_view_cont_callback);
    if(r != NULL) {
      browser_node_ref(r);
    }
  }

  glw_unlock();
  return r;
}


/**
 *
 */
void
browser_view_set(browser_node_t *bn, const char *viewname)
{
  char buf[256];
  
  free((void *)bn->bn_view);
  bn->bn_view = strdup(viewname);

  snprintf(buf, sizeof(buf), "browser/views/%s/view", viewname);

  glw_create(GLW_MODEL,
	     GLW_ATTRIB_FILENAME, buf,
	     GLW_ATTRIB_PARENT_HEAD, bn->bn_cont_xfader,
	     NULL);
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
  glw_t *w;

  pthread_mutex_lock(&bn->bn_mutex);
  
  browser_node_ref(bn);

  assert(bn->bn_cont_xfader == NULL);

  bn->bn_cont_xfader =
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_PARENT_HEAD, parent,
	       GLW_ATTRIB_SIGNAL_HANDLER, browser_view_cont_callback, bn, 1000,
	       NULL);

  browser_view_set(bn, "default");
 
  if((w = glw_find_by_id(bn->bn_cont_xfader, "node_container")) != NULL)
    glw_focus_set(gfs, w);

  if((w = glw_find_by_id(bn->bn_cont_xfader, "node_fullpath")) != NULL) {
    glw_set(w,
	    GLW_ATTRIB_CAPTION, bn->bn_url,
	    NULL);
  }
  pthread_mutex_unlock(&bn->bn_mutex);
}


/**
 *
 */
void
browser_view_collapse_node(browser_node_t *bn, glw_focus_stack_t *gfs)
{
  glw_t *w, *cont;

  pthread_mutex_lock(&bn->bn_mutex);
  glw_lock();

  w = bn->bn_cont_xfader;

  assert(bn->bn_cont_xfader != NULL);
  bn->bn_cont_xfader = NULL;

  glw_set(w, 
	  GLW_ATTRIB_SIGNAL_HANDLER, browser_view_cont_callback, bn, -1,
	  NULL);

  if((cont = glw_find_by_id(w, "node_container")) != NULL)
    glw_focus_lose(gfs, cont);

  glw_detach(w);

  glw_unlock();
  pthread_mutex_unlock(&bn->bn_mutex);
  
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
    printf("Widget for icon %s destroyed\n", bn->bn_url);
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
    w = glw_find_by_id(w, "node_container");
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
 *
 */
void
browser_view_node_update_filetype(browser_node_t *bn)
{
  browser_node_t *parent = bn->bn_parent;
  glw_t *w;
  const char *model;
  char buf[256];
  int64_t type;

  glw_lock();

  w = bn->bn_icon_xfader ? 
    glw_find_by_id(bn->bn_icon_xfader, "filetype_icon_container") : NULL;
  if(w != NULL) {
    
    switch(bn->bn_type) {
    case BN_DIR:
      model = "directory";
      break;
    default:
    case BN_FILE:
      model = "file";

      if(!filetag_get_int(&bn->bn_ftags, FTAG_FILETYPE, 0, &type)) {
	switch(type) {
	case FILETYPE_AUDIO:
	  model = "audio";
	  break;

	case FILETYPE_VIDEO:
	  model = "video";
	  break;
	}
      }
      break;
    }

    snprintf(buf, sizeof(buf), "browser/views/%s/%s", parent->bn_view, model);

    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, buf,
	       GLW_ATTRIB_PARENT, w,
	       NULL);
  }
  glw_unlock();
}

/**
 *
 */
void
browser_view_node_update_string(browser_node_t *bn, const char *id,
				const char *value)
{
  glw_t *w;

  glw_lock();

  if((w = glw_find_by_id(bn->bn_icon_xfader, id)) != NULL)
    glw_set(w, GLW_ATTRIB_CAPTION, value, NULL);

  glw_unlock();
}


/**
 *
 */
static void
browser_view_node_model_load(browser_node_t *bn)
{
  browser_node_t *parent = bn->bn_parent;
  char buf[256];
  const char *fname;

  snprintf(buf, sizeof(buf), "browser/views/%s/node", parent->bn_view);

  glw_create(GLW_MODEL,
	     GLW_ATTRIB_FILENAME, buf,
	     GLW_ATTRIB_PARENT, bn->bn_icon_xfader,
	     NULL);

  browser_view_node_update_filetype(bn);

  fname = strrchr(bn->bn_url, '/');
  fname = fname ? fname + 1 : bn->bn_url;

  browser_view_node_update_string(bn, "filename", fname);
}




/**
 * 
 */
void
browser_view_add_node(browser_node_t *bn)
{
  browser_node_t *parent = bn->bn_parent;
  glw_t *cont;

  if(parent->bn_cont_xfader == NULL)
    return;

  if((cont = glw_find_by_id(parent->bn_cont_xfader, "node_container")) == NULL)
    return;

  assert(bn->bn_refcnt > 0);

  pthread_mutex_lock(&bn->bn_mutex);

  browser_node_ref(bn);

  bn->bn_icon_xfader =
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_SIGNAL_HANDLER, browser_view_node_callback, bn, 1000,
	       GLW_ATTRIB_PARENT, cont,
	       NULL);

  browser_view_node_model_load(bn);

  pthread_mutex_unlock(&bn->bn_mutex);
}
