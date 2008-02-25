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

typedef struct lfectrl {
  glw_t *lc_w;

  glw_t *lc_nav[4]; /* up down left right */

#define LC_UP    0
#define LC_DOWN  1
#define LC_LEFT  2
#define LC_RIGHT 3

} lfectrl_t;

static glw_t *
find_anything_with_id(glw_t *w)
{
  glw_t *c, *r;

  if(w == NULL || w->glw_id != NULL)
    return w;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    if((r = find_anything_with_id(c)) != NULL)
      return r;
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
  lfectrl_t *lc = opaque;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_UP:
    n = lc->lc_nav[LC_UP];
    break;
  case GLW_SIGNAL_DOWN:
    n = lc->lc_nav[LC_DOWN];
    break;
  case GLW_SIGNAL_LEFT:
    n = lc->lc_nav[LC_LEFT];
    break;
  case GLW_SIGNAL_RIGHT:
    n = lc->lc_nav[LC_RIGHT];
    break;
  default:
    break;
  }

  va_end(ap);

  if(n != NULL) {
    //    glw_lose_focus(w);
    //    glw_set_focus(n);
    return 1;
  }

  return 0;
}

/**
 *
 */
int
layout_form_query(layout_form_entry_t lfes[], int nlfes, const char *model)
{
  glw_t *w;
  int i;
  lfectrl_t *lc;
  glw_t *cur = NULL;

  lc = alloca(sizeof(lfectrl_t) * nlfes);

  memset(lc, 0, sizeof(lfectrl_t) * nlfes);

  w = glw_create(GLW_MODEL,
		 GLW_ATTRIB_FILENAME, model,
		 NULL);

  for(i = 0; i < nlfes; i++) {
    lc[i].lc_w = glw_find_by_id(w, lfes[i].lfe_id);
    if(lc[i].lc_w == NULL)
      continue;
    if(cur == NULL)
      cur = lc[i].lc_w;
    printf("%s\n", lc[i].lc_w->glw_id);
    lc[i].lc_nav[LC_UP   ] = layout_form_find(lc[i].lc_w, 1, GLW_CONTAINER_Y);
    lc[i].lc_nav[LC_DOWN ] = layout_form_find(lc[i].lc_w, 0, GLW_CONTAINER_Y);
    lc[i].lc_nav[LC_LEFT ] = layout_form_find(lc[i].lc_w, 1, GLW_CONTAINER_X);
    lc[i].lc_nav[LC_RIGHT] = layout_form_find(lc[i].lc_w, 0, GLW_CONTAINER_X);

    glw_set(lc[i].lc_w,
	    GLW_ATTRIB_SIGNAL_HANDLER, layout_form_callback, &lc[i], 400,
	    NULL);

    printf("%s UP    -> %s\n",
	   lc[i].lc_w->glw_id, lc[i].lc_nav[LC_UP   ] ? 
	   lc[i].lc_nav[LC_UP   ]->glw_id : "nothing");

   printf("%s DOWN  -> %s\n",
	   lc[i].lc_w->glw_id, lc[i].lc_nav[LC_DOWN ] ? 
	   lc[i].lc_nav[LC_DOWN ]->glw_id : "nothing");

   printf("%s LEFT  -> %s\n",
	   lc[i].lc_w->glw_id, lc[i].lc_nav[LC_LEFT ] ? 
	   lc[i].lc_nav[LC_LEFT ]->glw_id : "nothing");

   printf("%s RIGHT -> %s\n",
	   lc[i].lc_w->glw_id, lc[i].lc_nav[LC_RIGHT] ? 
	   lc[i].lc_nav[LC_RIGHT]->glw_id : "nothing");
  }

  //  layout_world_add_entry(w);

  if(cur != NULL) {
    //    glw_set_focus(cur);
  }
  while(1) {
    sleep(1);
  }


  return 0;
}
