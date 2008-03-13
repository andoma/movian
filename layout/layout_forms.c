/*
 *  Layout engine
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

#include <string.h>
#include <math.h>
#include <unistd.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"
#include "layout_forms.h"


typedef struct layout_form {
  glw_focus_stack_t *lf_gfs;

} layout_form_t;

static int layout_form_callback(glw_t *w, void *opaque,
				glw_signal_t signal, ...);


/**
 * Find a target we can move to
 *
 * It must have a layout_form_callback attached to it so we
 * can 'escape' from it
 */

static glw_t *
find_anything_with_id(glw_t *w)
{
  glw_t *c, *r;

  if(w == NULL || glw_get_opaque(w, layout_form_callback) != NULL)
    return w;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(w->glw_class == GLW_DECK) {
      if(c != w->glw_selected)
	continue;
    }

    if((r = find_anything_with_id(c)) != NULL)
      return r;
  }
  return NULL;
}




/**
 *
 */
static glw_t *
layout_form_find(glw_t *w, int rev, glw_class_t cont_class)
{
  glw_t *c, *p, *s;
  
  while(1) {
    p = w->glw_parent;
    if(p == NULL)
      return NULL;
    if(p->glw_class == cont_class) {
      s = w;
      while(1) {
	if(rev)
	  s = TAILQ_PREV(s, glw_queue, glw_parent_link);
	else
	  s = TAILQ_NEXT(s, glw_parent_link);

	if(s == NULL)
	  break;

	if((c = find_anything_with_id(s)) != NULL)
	  return c;
      }
    }
    w = w->glw_parent;
  }
}


/**
 * Attach this callback to widgets in order to perform navigation
 * between them
 */
static int
layout_form_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  glw_t *n = NULL;
  layout_form_t *lf = opaque;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_UP:
    n = layout_form_find(w, 1, GLW_CONTAINER_Y);
    break;
  case GLW_SIGNAL_DOWN:
    n = layout_form_find(w, 0, GLW_CONTAINER_Y);
    break;
  case GLW_SIGNAL_LEFT:
    n = layout_form_find(w, 1, GLW_CONTAINER_X);
    break;
  case GLW_SIGNAL_RIGHT:
    n = layout_form_find(w, 0, GLW_CONTAINER_X);
    break;
  default:
    break;
  }

  va_end(ap);

  if(n != NULL) {
    glw_focus_lose(lf->lf_gfs, w);
    glw_focus_set(lf->lf_gfs, n);
    return 1;
  }
  return 0;
}



/**
 *
 */
int
layout_form_query(struct layout_form_entry_list *lfelist, glw_t *m,
		  glw_focus_stack_t *gfs)
{
  glw_t *w;
  glw_t *ff = NULL;  /* first widget to focus */
  layout_form_entry_t *lfe;
  layout_form_t lf;

  memset(&lf, 0, sizeof(lf));
  lf.lf_gfs = gfs;

  TAILQ_FOREACH(lfe, lfelist, lfe_link) {
    w = lfe->lfe_widget = glw_find_by_id(m, lfe->lfe_id, 1);
    if(w == NULL)
      continue;

    if(ff == NULL)
      ff = w;

    glw_set(w,
	    GLW_ATTRIB_SIGNAL_HANDLER, layout_form_callback, &lf, 400,
	    NULL);
  }


  if(ff != NULL)
    glw_focus_set(gfs, ff);

  glw_focus_stack_activate(gfs);

  while(1) {
    sleep(1);
  }

  return 0;
}




/**
 * Callback for coupling display of a tab entry (deck child) to a list entry
 */
static int
layout_form_tab_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  glw_t *tab = opaque;

  switch(signal) {
  case GLW_SIGNAL_SELECTED_SELF:
    tab->glw_parent->glw_selected = tab;
    return 1;
  default:
    break;
  }
  return 0;
}



void
layout_form_add_tab(glw_t *m, const char *listname, const char *listmodel,
		    const char *deckname, const char *tabmodel)
{
  glw_t *w, *d, *t;

  w = glw_find_by_id(m, listname, 0);
  d = glw_find_by_id(m, deckname, 0);

  if(w == NULL || d == NULL)
    return;

  t = glw_create(GLW_MODEL,
		 GLW_ATTRIB_FILENAME, tabmodel,
		 GLW_ATTRIB_PARENT, d,
		 NULL);

  glw_create(GLW_MODEL,
	     GLW_ATTRIB_FILENAME, listmodel,
	     GLW_ATTRIB_SIGNAL_HANDLER, layout_form_tab_callback, t, 400,
	     GLW_ATTRIB_PARENT, w,
	     NULL);
}
