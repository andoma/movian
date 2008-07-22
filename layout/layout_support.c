/*
 *  Layout support functions
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

#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"
#include "layout_support.h"

/**
 * Update current time info in text widget
 */ 
void
layout_update_time(glw_t *w, const char *id, int s)
{
  char tmp[100];
  int m ,h;

  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  m = s / 60;
  h = s / 3600;
  
  if(h > 0) {
    snprintf(tmp, sizeof(tmp), "%d:%02d:%02d", h, m % 60, s % 60);
  } else {
    snprintf(tmp, sizeof(tmp), "%d:%02d", m % 60, s % 60);
  }

  glw_set(w, GLW_ATTRIB_CAPTION, tmp, NULL);
}

/**
 * Update a GLW_BAR
 */ 
void
layout_update_bar(glw_t *w, const char *id, float v)
{
  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  glw_set(w, GLW_ATTRIB_EXTRA, v, NULL);
}

/**
 * Set caption for a widget to integer 'v'
 */
void
layout_update_int(glw_t *w, const char *id, int v)
{
  char tmp[20];

  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  snprintf(tmp, sizeof(tmp), "%d", v);
  glw_set(w, GLW_ATTRIB_CAPTION, tmp, NULL);

}

/**
 * Set caption for a widget to string 'str'
 */
void
layout_update_str(glw_t *w, const char *id, const char *str)
{
  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  glw_set(w,
	  GLW_ATTRIB_CAPTION, str,
	  NULL);

}

/**
 * Set filename for a widget to 'file'
 */
void
layout_update_source(glw_t *w, const char *id, const char *filename)
{
  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  glw_set(w,
	  GLW_ATTRIB_SOURCE, filename,
	  NULL);

}


/**
 * Load a model into parent 'w'
 */
void
layout_update_model(glw_t *w, const char *id, const char *model)
{
  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  /* We only support doing this in a few parent classes,
     since they must autodestroy the previous widget */
     
  switch(w->glw_class) {
  case GLW_XFADER:
  case GLW_FLIPPER:
    break;
  default:
    return;
  }

  glw_model_create(model, w);
}

/**
 *
 */
void
layout_update_multilinetext(glw_t *w, const char *id, const char *txt, 
			    int total_lines, glw_alignment_t alignment)
{
  int lines = 1;
  int i, l, j;
  const char **vec, *s;
  char *copy;

  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  l = strlen(txt);
  for(i = 0; i < l; i++)
    if(txt[i] == '\n')
      lines++;

  copy = strdupa(txt);

  vec = alloca(lines * sizeof(const char *));
  j = 0;
  s = copy;
  for(i = 0; i < l; i++) {
    if(copy[i] == '\n') {
      vec[j++] = s;
      copy[i] = 0;
      s = copy + 1 + i;
    }
  }
  vec[j++] = s;

  assert(j == lines);

  glw_destroy_childs(w);

  for(i = 0; i < lines; i++)
    glw_create(GLW_LABEL,
	       GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_CAPTION, vec[i],
	       GLW_ATTRIB_ALIGNMENT, alignment,
	       NULL);

  if(i < total_lines) {
    glw_create(GLW_DUMMY,
	       GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_WEIGHT, (float)(total_lines - i),
	       NULL);
  }
}
