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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "browser.h"
#include "browser_view.h"
#include "fileaccess/fa_rawloader.h"

struct browser_view_queue browser_views;

browser_view_t *defaultview;

static int browser_view_node_callback(glw_t *w, void *opaque,
				      glw_signal_t sig, void *extra);

/**
 *
 */
static int
browser_view_xfader_callback(glw_t *w, void *opaque, glw_signal_t sig,
			     void *extra)
{
  browser_node_t *bn = opaque;

  if(sig == GLW_SIGNAL_DESTROY) {
    bn->bn_cont_xfader = NULL;
    browser_node_deref(bn);
  }
  return 0;
}

#if 0
/**
 *
 */
static int
browser_view_cont_callback(glw_t *w, void *opaque, glw_signal_t sig,
			   void *extra)
{
  glw_t *c;
  browser_node_t *parent = opaque, *bn;


  if(sig == GLW_SIGNAL_SELECTED_CHANGED) {
    c = extra;

    /* Get the new node that was selected */

    bn = glw_get_opaque(c, browser_view_node_callback);
    if(bn != NULL && parent->bn_cont_xfader != NULL) {
      browser_view_update_wset_from_node(parent->bn_cont_xfader, bn);
    }
  }
  return 0;
}
#endif


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
browser_view_set(browser_node_t *bn, browser_view_t *bv)
{
  char buf[256];
  glw_t *m, *w;

  bn->bn_view = bv;

  snprintf(buf, sizeof(buf), "theme://browser/views/%s/view.model",
	   bv->bv_name);

  m = glw_model_create(buf, bn->bn_cont_xfader, 0, NULL);
 
  if((w = glw_find_by_id(m, "node_container", 0)) != NULL) {
    glw_select(w);
#if 0
    glw_set(w,
	    GLW_ATTRIB_SIGNAL_HANDLER, browser_view_cont_callback, bn, 1000,
	    NULL);
#endif
  }

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
browser_view_expand_node(browser_node_t *bn, glw_t *parent)
{
  browser_view_t *bv;

  browser_node_ref(bn);

  assert(bn->bn_cont_xfader == NULL);

  glw_lock();

  bn->bn_cont_xfader =
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_PARENT, parent,
	       GLW_ATTRIB_SIGNAL_HANDLER,
	       browser_view_xfader_callback, bn, 1000,
	       NULL);

  bv = bn->bn_parent ? bn->bn_parent->bn_view : defaultview;
  browser_view_set(bn, bv);
  glw_unlock();
}


/**
 *
 */
void
browser_view_collapse_node(browser_node_t *bn)
{
  glw_t *w;

  glw_lock();

  w = bn->bn_cont_xfader;

  assert(bn->bn_cont_xfader != NULL);
  bn->bn_cont_xfader = NULL;

  glw_set(w, 
	  GLW_ATTRIB_SIGNAL_HANDLER, browser_view_xfader_callback, bn, -1,
	  NULL);

  glw_detach(w);

  glw_unlock();
  
  browser_node_deref(bn);
}


/**
 *
 */
static int
browser_view_node_callback(glw_t *w, void *opaque, glw_signal_t sig, 
			   void *extra)
{
  browser_node_t *bn = opaque;

  if(sig == GLW_SIGNAL_DESTROY) {
    bn->bn_model = NULL;
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
 * 
 */
void
browser_view_add_node(browser_node_t *bn, glw_t *c, int select_it, int hidden)
{
  browser_node_t *parent = bn->bn_parent;
  char buf[256];

  c = c ? c : parent->bn_cont_xfader;
  if(c == NULL)
    return;
  
  if((c = glw_find_by_id(c, "node_container", 0)) == NULL)
    return;

  snprintf(buf, sizeof(buf), "theme://browser/views/%s/node.model",
	   parent->bn_view->bv_name);

  bn->bn_model = glw_model_create(buf, c, GLW_MODEL_CACHE,
				  bn->bn_prop_root, prop_global, NULL);

  assert(bn->bn_refcnt > 0);
  browser_node_ref(bn);
  
  glw_set(bn->bn_model,
	  GLW_ATTRIB_SIGNAL_HANDLER, browser_view_node_callback, bn, 1000,
	  GLW_ATTRIB_FLAGS, hidden ? GLW_HIDE : 0,
	  NULL);

  if(select_it)
    glw_select(bn->bn_model);
}

/**
 *
 */
static void
bvi_callback(void *arg, const char *url, const char *filename, int type)
{
  browser_view_t *bv;
  const char *cf;
  char buf[256];
  uint32_t filtermask;

  bv = calloc(1, sizeof(browser_view_t));
  
  TAILQ_INSERT_TAIL(&browser_views, bv, bv_link);
  bv->bv_path = strdup(url);
  bv->bv_name = strdup(filename);
  if(defaultview == NULL)
    defaultview = bv;
  
  if(!strcmp(filename, "default"))
    defaultview = bv;

  filtermask = 0xffffffff;
  
  snprintf(buf, sizeof(buf), "%s/contentfilter", url);

  printf("Opening contentfilter %s\n", buf);

  if((cf = fa_rawloader(buf, NULL)) != NULL) {
    filtermask = 0;
    if(strstr(cf, "image\n"))
      filtermask |= 1 << FILETYPE_IMAGE;
    fa_rawunload(cf);
  }
  bv->bv_contentfilter = filtermask;
}

/**
 * Builds index of all available views for this theme
 */
int
browser_view_index(void)
{
  if(defaultview)
    return 0; /* already indexed */

  TAILQ_INIT(&browser_views);

  fileaccess_scandir("theme://browser/views", bvi_callback, NULL);

  return defaultview == NULL ? -1 : 0;
};


/**
 *
 */
static void
browser_view_splash(browser_view_t *bv, glw_t *parent)
{
  char buf[200];

  snprintf(buf, sizeof(buf), 
	   "theme://browser/views/%s/splash.model", bv->bv_name);

  glw_model_create(buf, parent, 0, NULL);
}



/**
 * Inner parts of view switching
 */
static void
browser_view_switch0(browser_node_t *bn, browser_view_t *bv)
{
  browser_root_t *br = bn->bn_root;
  browser_node_t *sel, *c, **a;
  glw_t *m, *w;
  int hide, cnt;
  int64_t type;

  browser_view_splash(bv, br->br_splashcontainer);

  glw_lock();

  sel = NULL;
  w = glw_find_by_id(bn->bn_cont_xfader, "node_container", 0);
  if(w != NULL) {
    w = w->glw_selected;
    if(w != NULL) {
      sel = glw_get_opaque(w, browser_view_node_callback);
      browser_node_ref(sel);
    }
  }
  
  glw_unlock();

  m = browser_view_set(bn, bv);

  /**
   * Create new nodes, hide nodes that does not match the 
   * views contentmask
   */

  a = browser_get_array_of_childs(br, bn);
  for(cnt = 0; (c = a[cnt]) != NULL; cnt++) {
    hts_mutex_lock(&c->bn_ftags_mutex);
    hide = !filetag_get_int(&c->bn_ftags, FTAG_FILETYPE, &type) &&
      !(1 << type & bv->bv_contentfilter);
    hts_mutex_unlock(&c->bn_ftags_mutex);

    browser_view_add_node(c, m, c == sel, hide);
    browser_node_deref(c); /* 'c' may be free'd here */
  }

  free(a);
  if(sel != NULL)
    browser_node_deref(sel);

  /* Destroy splash */

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, br->br_splashcontainer,
	     NULL);
}



/**
 * Switch current view
 */
void
browser_view_switch(browser_node_t *bn)
{
  browser_root_t *br = bn->bn_root;
  browser_view_t *bv = bn->bn_view;
  browser_node_t *c, **a;
  int64_t type;
  uint32_t contentmask = 0, cnt;

  /**
   * Collect a mask with all the content types in the node
   */
  a = browser_get_array_of_childs(br, bn);
  for(cnt = 0; (c = a[cnt]) != NULL; cnt++) {
    hts_mutex_lock(&c->bn_ftags_mutex);
    switch(c->bn_type) {
    case FA_DIR:
      contentmask = 1 << FILETYPE_DIR;
      break;

    case FA_FILE:
      if(!filetag_get_int(&c->bn_ftags, FTAG_FILETYPE, &type))
	contentmask = 1 << type;
      break;
    }
    hts_mutex_unlock(&c->bn_ftags_mutex);
    browser_node_deref(c); /* 'c' may be free'd here */
  }
  free(a);

  while(1) {

    bv = TAILQ_NEXT(bv, bv_link);
    if(bv == NULL)
      bv = TAILQ_FIRST(&browser_views);

    if(bv == bn->bn_view)
      return; /* All views tried, back at current, dont do anything */


    if(!(bv->bv_contentfilter & contentmask))
      continue; /* this view would not display anything, skip it */

    break;
  }

  browser_view_switch0(bn, bv);
}


/**
 * Switch to the named view, if view is not found, nothing happens
 */
void
browser_view_switch_by_name(browser_node_t *bn, const char *name)
{
  browser_view_t *bv;

  TAILQ_FOREACH(bv, &browser_views, bv_link)
    if(!strcmp(bv->bv_name, name))
      break;

  if(bv != NULL)
    browser_view_switch0(bn, bv);
}
